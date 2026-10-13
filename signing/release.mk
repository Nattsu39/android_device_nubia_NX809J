# NX809J release signing. Private key material is provisioned separately.
ifeq ($(TARGET_BUILD_VARIANT),user)
PRODUCT_DEFAULT_DEV_CERTIFICATE := vendor/lineage-priv/keys/releasekey
# AOSP otherwise keeps the Bluetooth SELinux signer on the public test key.
PRODUCT_MAINLINE_BLUETOOTH_SEPOLICY_DEV_CERTIFICATES := vendor/lineage-priv/keys

# Missing APEX keys must fail before Soong can fall back to public test keys.
NX809J_RELEASE_CERTIFICATES := bluetooth com.android.adbd com.android.adservices com.android.adservices.api com.android.appsearch com.android.appsearch.apk com.android.art com.android.bt com.android.cellbroadcast com.android.configinfrastructure com.android.connectivity.resources com.android.conscrypt com.android.crashrecovery com.android.devicelock com.android.extservices com.android.federatedcompute com.android.hardware.cas com.android.health.connect.backuprestore com.android.healthconnect.controller com.android.healthfitness com.android.hotspot2.osulogin com.android.i18n com.android.ipsec com.android.media com.android.media.swcodec com.android.mediaprovider com.android.neuralnetworks com.android.nfcservices com.android.ondevicepersonalization com.android.os.statsd com.android.permission com.android.profiling com.android.resolv com.android.rkpd com.android.runtime com.android.safetycenter.resources com.android.scheduling com.android.sdkext com.android.telephonycore com.android.tethering com.android.tzdata com.android.uprobestats com.android.uwb com.android.uwb.resources com.android.virt com.android.wifi com.android.wifi.dialog com.android.wifi.resources media networkstack nfc platform releasekey sdk_sandbox shared
NX809J_RELEASE_PAYLOAD_KEYS := avb com.android.adbd com.android.adservices com.android.appsearch com.android.art com.android.bt com.android.cellbroadcast com.android.configinfrastructure com.android.conscrypt com.android.crashrecovery com.android.devicelock com.android.extservices com.android.hardware com.android.healthfitness com.android.i18n com.android.ipsec com.android.media com.android.media.swcodec com.android.mediaprovider com.android.neuralnetworks com.android.nfcservices com.android.ondevicepersonalization com.android.os.statsd com.android.permission com.android.profiling com.android.resolv com.android.rkpd com.android.runtime com.android.scheduling com.android.sdkext com.android.telephonycore com.android.tethering com.android.tzdata com.android.uprobestats com.android.uwb com.android.virt com.android.wifi vbmeta_system
NX809J_RELEASE_KEY_FILES := Android.bp keys.mk testkey.pk8 testkey.x509.pem \
    $(addsuffix .pk8,$(NX809J_RELEASE_CERTIFICATES)) \
    $(addsuffix .x509.pem,$(NX809J_RELEASE_CERTIFICATES)) \
    $(addsuffix .pem,$(NX809J_RELEASE_PAYLOAD_KEYS)) \
    $(addsuffix .avbpubkey,$(NX809J_RELEASE_PAYLOAD_KEYS))
$(foreach key,$(NX809J_RELEASE_KEY_FILES),\
    $(if $(wildcard vendor/lineage-priv/keys/$(key)),,\
        $(error NX809J release key input missing: vendor/lineage-priv/keys/$(key))))

# Certificate modules otherwise keep their upstream module-local test keys.
PRODUCT_CERTIFICATE_OVERRIDES += \
    com.android.adbd:nx809j_com.android.adbd \
    com.android.adservices:nx809j_com.android.adservices \
    com.android.appsearch:nx809j_com.android.appsearch \
    com.android.art:nx809j_com.android.art \
    com.android.bt:nx809j_com.android.bt \
    com.android.cellbroadcast:nx809j_com.android.cellbroadcast \
    com.android.configinfrastructure:nx809j_com.android.configinfrastructure \
    com.android.conscrypt:nx809j_com.android.conscrypt \
    com.android.crashrecovery:nx809j_com.android.crashrecovery \
    com.android.devicelock:nx809j_com.android.devicelock \
    com.android.extservices:nx809j_com.android.extservices \
    com.android.hardware.cas:nx809j_com.android.hardware.cas \
    com.android.healthfitness:nx809j_com.android.healthfitness \
    com.android.i18n:nx809j_com.android.i18n \
    com.android.ipsec:nx809j_com.android.ipsec \
    com.android.media:nx809j_com.android.media \
    com.android.media.swcodec:nx809j_com.android.media.swcodec \
    com.android.mediaprovider:nx809j_com.android.mediaprovider \
    com.android.neuralnetworks:nx809j_com.android.neuralnetworks \
    com.android.nfcservices:nx809j_com.android.nfcservices \
    com.android.ondevicepersonalization:nx809j_com.android.ondevicepersonalization \
    com.android.os.statsd:nx809j_com.android.os.statsd \
    com.android.permission:nx809j_com.android.permission \
    com.android.profiling:nx809j_com.android.profiling \
    com.android.resolv:nx809j_com.android.resolv \
    com.android.rkpd:nx809j_com.android.rkpd \
    com.android.runtime:nx809j_com.android.runtime \
    com.android.scheduling:nx809j_com.android.scheduling \
    com.android.sdkext:nx809j_com.android.sdkext \
    com.android.telephonycore:nx809j_com.android.telephonycore \
    com.android.tethering:nx809j_com.android.tethering \
    com.android.tzdata:nx809j_com.android.tzdata \
    com.android.uprobestats:nx809j_com.android.uprobestats \
    com.android.uwb:nx809j_com.android.uwb \
    com.android.virt:nx809j_com.android.virt \
    com.android.wifi:nx809j_com.android.wifi \
    AdServicesApk:nx809j_com.android.adservices.api \
    com.android.appsearch.apk:nx809j_com.android.appsearch.apk \
    HealthConnectBackupRestore:nx809j_com.android.health.connect.backuprestore \
    HealthConnectController:nx809j_com.android.healthconnect.controller \
    FederatedCompute:nx809j_com.android.federatedcompute \
    SafetyCenterResources:nx809j_com.android.safetycenter.resources \
    ServiceConnectivityResources:nx809j_com.android.connectivity.resources \
    ServiceUwbResources:nx809j_com.android.uwb.resources \
    OsuLogin:nx809j_com.android.hotspot2.osulogin \
    WifiDialog:nx809j_com.android.wifi.dialog \
    ServiceWifiResources:nx809j_com.android.wifi.resources

endif
