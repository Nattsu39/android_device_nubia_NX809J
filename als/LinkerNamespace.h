/*
 * Copyright (C) 2026 The Avium Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <android/dlext.h>
#include <cstdint>

// Platform libdl_android exports these C entrypoints. The platform header module
// is not visible to device modules. Keep only the two opaque namespace operations
// needed here, with ABI checked against bionic/libc/platform/bionic/dlext_namespaces.h.
// This is a platform-only binary, not an NDK interface.
extern "C" android_namespace_t* android_create_namespace(const char* name,
        const char* ldLibraryPath, const char* defaultLibraryPath, uint64_t type,
        const char* permittedPath, android_namespace_t* parent);
extern "C" bool android_link_namespaces(android_namespace_t* from, android_namespace_t* to,
        const char* sharedLibrarySonames);

constexpr uint64_t kIsolatedNamespace = 1;
