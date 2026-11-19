# PhysX 5.x's upstream public presets currently cover Windows and Linux only.
# O3DE maintains the same BSD-3-Clause PhysX sources with the missing macOS,
# iOS, and Android build definitions. Pin the exact PhysX 5.6.1-compatible
# revision so dependency updates cannot silently change the physics ABI.
FetchContent_Declare(
    physx
    GIT_REPOSITORY https://github.com/o3de/PhysX.git
    GIT_TAG d5c964f23c3ba5553a305bbc7af3b50a5fa47a12
    EXCLUDE_FROM_ALL
)

FetchContent_GetProperties(physx)
if(NOT physx_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(physx)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()

set(VULKCORE_PHYSX_ROOT "${physx_SOURCE_DIR}/physx")
set(PHYSX_ROOT_DIR "${VULKCORE_PHYSX_ROOT}" CACHE INTERNAL "PhysX source root" FORCE)
set(PX_OUTPUT_LIB_DIR "${physx_BINARY_DIR}/output" CACHE PATH "PhysX library output" FORCE)
set(PX_OUTPUT_BIN_DIR "${physx_BINARY_DIR}/output" CACHE PATH "PhysX binary output" FORCE)
set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "Build PhysX as static libraries" FORCE)
set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "Disable CUDA PhysX" FORCE)
set(PX_BUILDSNIPPETS OFF CACHE BOOL "Disable PhysX snippets" FORCE)
set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "Disable OmniPVD runtime" FORCE)
set(PUBLIC_RELEASE 1 CACHE BOOL "Use the PhysX public API" FORCE)
set(NV_FORCE_64BIT_SUFFIX ON CACHE BOOL "Use 64-bit PhysX library names" FORCE)

if(VULKCORE_OHOS)
    # Use PhysX's Android build rules with the OHOS toolchain and libc.
    set(TARGET_BUILD_PLATFORM android CACHE STRING "PhysX target platform" FORCE)
    set(ANDROID_ABI "${OHOS_ARCH}")
    set(CMAKE_ANDROID_ARCH_ABI "${OHOS_ARCH}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(PX_OUTPUT_ARCH arm CACHE STRING "PhysX output architecture family" FORCE)
    else()
        set(PX_OUTPUT_ARCH x86 CACHE STRING "PhysX output architecture family" FORCE)
    endif()
elseif(WIN32)
    # Match the application's Debug CRT and iterator ABI.
    set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "Use the Debug CRT in PhysX Debug builds" FORCE)
    # The public CPU SDK incorrectly copies FreeGLUT DLLs even with snippets
    # disabled. Restrict that upstream step to builds that actually use them.
    set(VULKCORE_PHYSX_WINDOWS_CMAKE "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake/windows/CMakeLists.txt")
    file(READ "${VULKCORE_PHYSX_WINDOWS_CMAKE}" VULKCORE_PHYSX_WINDOWS_CONTENT)
    string(REPLACE "IF(PX_COPY_EXTERNAL_DLL OR PUBLIC_RELEASE)"
        "IF(PX_COPY_EXTERNAL_DLL OR (PUBLIC_RELEASE AND PX_BUILDSNIPPETS))"
        VULKCORE_PHYSX_WINDOWS_CONTENT "${VULKCORE_PHYSX_WINDOWS_CONTENT}")
    file(CONFIGURE OUTPUT "${VULKCORE_PHYSX_WINDOWS_CMAKE}"
        CONTENT "${VULKCORE_PHYSX_WINDOWS_CONTENT}" @ONLY)
    set(TARGET_BUILD_PLATFORM windows CACHE STRING "PhysX target platform" FORCE)
    set(PX_OUTPUT_ARCH x86 CACHE STRING "PhysX output architecture family" FORCE)
elseif(APPLE)
    set(TARGET_BUILD_PLATFORM mac CACHE STRING "PhysX target platform" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(PX_OUTPUT_ARCH arm CACHE STRING "PhysX output architecture family" FORCE)
    else()
        set(PX_OUTPUT_ARCH x86 CACHE STRING "PhysX output architecture family" FORCE)
    endif()
elseif(UNIX)
    set(TARGET_BUILD_PLATFORM linux CACHE STRING "PhysX target platform" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(PX_OUTPUT_ARCH arm CACHE STRING "PhysX output architecture family" FORCE)
    else()
        set(PX_OUTPUT_ARCH x86 CACHE STRING "PhysX output architecture family" FORCE)
    endif()
else()
    message(FATAL_ERROR "VulkCore's PhysX integration does not support this platform.")
endif()

set(VULKCORE_SAVED_CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}")
list(APPEND CMAKE_MODULE_PATH
    "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake"
    "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake/modules"
    "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake/${TARGET_BUILD_PLATFORM}"
)
set(VULKCORE_SAVED_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}")
set(VULKCORE_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
if(MSVC)
    # PhysX's Windows build uses C++14. Inheriting C++20 turns its enum flag
    # operations into C5054 errors under the SDK's /WX setting.
    set(CMAKE_CXX_STANDARD 14)
endif()
add_subdirectory(
    "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake"
    "${physx_BINARY_DIR}/sdk"
    EXCLUDE_FROM_ALL
)
set(CMAKE_CXX_STANDARD "${VULKCORE_SAVED_CXX_STANDARD}")
# PhysX replaces the parent's configuration cache with its own SDK presets.
if(VULKCORE_SAVED_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "${VULKCORE_SAVED_CONFIGURATION_TYPES}" CACHE STRING "Build configurations" FORCE)
endif()
set(CMAKE_MODULE_PATH "${VULKCORE_SAVED_CMAKE_MODULE_PATH}")
if(VULKCORE_OHOS)
    include("${CMAKE_CURRENT_LIST_DIR}/OhosPhysX.cmake")
endif()
