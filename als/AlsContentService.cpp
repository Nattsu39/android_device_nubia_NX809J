/*
 * Copyright (C) 2026 The Avium Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "Nx809jAlsContent"

#include "LatestLcdQueue.h"
#include "LinkerNamespace.h"

#include <android-base/stringprintf.h>
#include <android/dlext.h>
#include <binder/Binder.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <dlfcn.h>
#include <log/log.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace nx809j::als {
namespace {

constexpr char kServiceName[] = "nx809j.als_content";
constexpr char kDescriptor[] = "org.avium.display.IAlsContentBridge";
constexpr char kLibraryDir[] = "/system_ext/lib64/nx809j-als";
constexpr uid_t kSystemUid = 1000;
constexpr uid_t kShellUid = 2000;
constexpr uint32_t kSetActive = android::IBinder::FIRST_CALL_TRANSACTION;
constexpr uint32_t kSetParams = android::IBinder::FIRST_CALL_TRANSACTION + 1;

// A private namespace containing only the pinned sender's dependency closure.
// libsensorapi's own dlopen("libsns_set_brightness_vendor.so") runs in this
// namespace too. No vendor search path or namespace is exposed to this process.
class VendorSender {
  public:
    using SetLcdParams = int (*)(int, int, int, int, int, int);

    bool load() {
        if (mSetParams != nullptr) return true;
        if (mNamespace == nullptr) {
            mNamespace = android_create_namespace("nx809j_als", nullptr, kLibraryDir,
                    kIsolatedNamespace, kLibraryDir, nullptr);
            if (mNamespace == nullptr) {
                ALOGE("Cannot create isolated sender namespace: %s", dlerror());
                return false;
            }
            constexpr char kSharedLibraries[] =
                    "libc.so:libm.so:libdl.so:liblog.so:libc++.so:libvndksupport.so";
            if (!android_link_namespaces(mNamespace, nullptr, kSharedLibraries)) {
                ALOGE("Cannot link sender's platform dependencies: %s", dlerror());
                return false;
            }
            mNamespaceLinked = true;
        }
        if (!mNamespaceLinked) return false;
        android_dlextinfo info{};
        info.flags = ANDROID_DLEXT_USE_NAMESPACE;
        info.library_namespace = mNamespace;
        void* handle = android_dlopen_ext("libsensorapi_vendor.so", RTLD_NOW | RTLD_LOCAL, &info);
        if (handle == nullptr) {
            ALOGE("Cannot load LCD sender and its dependencies: %s", dlerror());
            return false;
        }
        dlerror();
        auto setParams = reinterpret_cast<SetLcdParams>(dlsym(handle, "sensor_set_LCD_params"));
        const char* error = dlerror();
        if (error != nullptr || setParams == nullptr) {
            ALOGE("Missing sensor_set_LCD_params: %s", error == nullptr ? "null" : error);
            dlclose(handle);
            return false;
        }
        // Keep the handle alive for the process lifetime. Reloading per sample
        // cannot by itself repair the vendor library's cached QSH connection.
        mSetParams = setParams;
        return true;
    }

    int send(const LcdParams& p) {
        if (!load()) return -1;
        // Return 0 is local string "1", not proof of a DSP ACK or applied compensation.
        return mSetParams(p[0], p[1], p[2], p[3], p[4], p[5]);
    }

  private:
    android_namespace_t* mNamespace = nullptr;
    bool mNamespaceLinked = false;
    SetLcdParams mSetParams = nullptr;
};

class AlsContentService final : public android::BBinder {
  public:
    AlsContentService() : mWorker(&AlsContentService::sendLoop, this) {}

    ~AlsContentService() override {
        {
            std::lock_guard lock(mMutex);
            mStopping = true;
        }
        mCondition.notify_one();
        mWorker.join();
    }

    const android::String16& getInterfaceDescriptor() const override {
        static const android::String16 descriptor(kDescriptor);
        return descriptor;
    }

  protected:
    android::status_t onTransact(uint32_t code, const android::Parcel& data,
            android::Parcel* reply, uint32_t flags) override {
        if (code != kSetActive && code != kSetParams) {
            return BBinder::onTransact(code, data, reply, flags);
        }
        if (android::IPCThreadState::self()->getCallingUid() != kSystemUid) {
            return android::PERMISSION_DENIED;
        }
        if (reply == nullptr || !data.enforceInterface(getInterfaceDescriptor())) {
            return android::BAD_VALUE;
        }
        bool accepted;
        {
            std::lock_guard lock(mMutex);
            if (code == kSetActive) {
                int32_t active;
                if (data.readInt32(&active) != android::OK || data.dataAvail() != 0
                        || (active != 0 && active != 1)) {
                    return android::BAD_VALUE;
                }
                mQueue.setActive(active == 1);
                accepted = true;
            } else {
                LcdParams params{};
                for (auto& value : params) {
                    if (data.readInt32(&value) != android::OK) return android::BAD_VALUE;
                }
                if (data.dataAvail() != 0 || !validParams(params)) return android::BAD_VALUE;
                accepted = mQueue.submit(params);
                if (accepted) ++mSubmissions;
            }
        }
        mCondition.notify_one();
        reply->writeNoException();
        reply->writeInt32(accepted ? 1 : 0);
        return android::OK;
    }

    android::status_t dump(int fd, const android::Vector<android::String16>&) override {
        const uid_t uid = android::IPCThreadState::self()->getCallingUid();
        if (uid != 0 && uid != kSystemUid && uid != kShellUid) {
            return android::PERMISSION_DENIED;
        }
        std::lock_guard lock(mMutex);
        dprintf(fd, "active=%d pending=%d submissions=%llu attempts=%llu localAccepted=%llu "
                "lastLocalResult=%d\n", mQueue.active(), mQueue.latest().has_value(),
                static_cast<unsigned long long>(mSubmissions),
                static_cast<unsigned long long>(mAttempts),
                static_cast<unsigned long long>(mLocalAccepted), mLastResult);
        dprintf(fd, "Local acceptance does not confirm DSP application. "
                "One vendor call may finish after disable.\n");
        return android::OK;
    }

  private:
    void sendLoop() {
        VendorSender sender;
        std::unique_lock lock(mMutex);
        while (!mStopping) {
            mCondition.wait(lock, [this] { return mStopping || mQueue.latest().has_value(); });
            if (mStopping) break;
            const auto sample = mQueue.latest().value();
            lock.unlock();
            // All dlopen, SUID lookup and QSH waits run here, never on a Binder thread.
            const int result = sender.send(sample.params);
            lock.lock();
            ++mAttempts;
            if (mQueue.isCurrent(sample)) {
                mLastResult = result;
                if (result == 0) ++mLocalAccepted;
            }
            // Refresh even locally accepted values because the vendor return does
            // not reflect SUID discovery/ACK timeout. Changes preempt the wait.
            const auto delay = result == 0 ? std::chrono::seconds(5) : std::chrono::seconds(2);
            mCondition.wait_for(lock, delay, [this, &sample] {
                const auto next = mQueue.latest();
                return mStopping || !mQueue.isCurrent(sample)
                        || (next && next->params != sample.params);
            });
        }
    }

    std::mutex mMutex;
    std::condition_variable mCondition;
    LatestLcdQueue mQueue;
    bool mStopping = false;
    uint64_t mSubmissions = 0;
    uint64_t mAttempts = 0;
    uint64_t mLocalAccepted = 0;
    int mLastResult = -1;
    std::thread mWorker;
};

}  // namespace
}  // namespace nx809j::als

int main() {
    android::ProcessState::self()->setThreadPoolMaxThreadCount(2);
    const auto service = android::sp<nx809j::als::AlsContentService>::make();
    const auto status = android::defaultServiceManager()->addService(
            android::String16(nx809j::als::kServiceName), service);
    if (status != android::OK) {
        ALOGE("Cannot register ALS content bridge: %d", status);
        return 1;
    }
    android::ProcessState::self()->startThreadPool();
    android::IPCThreadState::self()->joinThreadPool();
    return 0;
}
