cmake_minimum_required(VERSION 3.24)

# Modern Apple/Windows linkers rescan static archives and do not require the
# repeated libraries emitted for traditional Unix linkers. Enable CMake's
# capability-aware de-duplication when the running CMake supports it.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()

project(VulkCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared dependencies" OFF)

find_package(Vulkan REQUIRED COMPONENTS glslc)

find_program(VULKCORE_GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT VULKCORE_GLSLC_EXECUTABLE)
    set(VULKCORE_GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
endif()

include(FetchContent)

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

if(WIN32)
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

# Assimp is used for importing model geometry. Prefer an installed package so
# local package managers can provide a shared build; FetchContent keeps a fresh
# checkout buildable when Assimp is not installed on the host.
find_package(assimp CONFIG QUIET)
if(NOT TARGET assimp::assimp)
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v6.0.5
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(assimp)
endif()

if(TARGET assimp::assimp)
    set(VULKCORE_ASSIMP_TARGET assimp::assimp)
elseif(TARGET assimp)
    set(VULKCORE_ASSIMP_TARGET assimp)
else()
    message(FATAL_ERROR "Assimp was not found or could not be built.")
endif()

set(VULKCORE_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src")
set(VULKCORE_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VULKCORE_DEFAULT_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/mill-scene/mill-scene.fbx")
set(VULKCORE_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vulkcore.ini")
set(VULKCORE_MODEL_PATH "${VULKCORE_DEFAULT_MODEL_PATH}" CACHE FILEPATH "Model path; FBX files are supported")
if(NOT VULKCORE_MODEL_PATH)
    set(VULKCORE_MODEL_PATH "${VULKCORE_DEFAULT_MODEL_PATH}" CACHE FILEPATH "Model path; FBX files are supported" FORCE)
endif()
file(GLOB VULKCORE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${VULKCORE_SHADER_SOURCE_DIR}/*.vert"
    "${VULKCORE_SHADER_SOURCE_DIR}/*.tesc"
    "${VULKCORE_SHADER_SOURCE_DIR}/*.tese"
    "${VULKCORE_SHADER_SOURCE_DIR}/*.geom"
    "${VULKCORE_SHADER_SOURCE_DIR}/*.frag"
    "${VULKCORE_SHADER_SOURCE_DIR}/*.comp"
)

if(NOT VULKCORE_SHADER_SOURCES)
    message(FATAL_ERROR "No GLSL shader sources were found in ${VULKCORE_SHADER_SOURCE_DIR}.")
endif()

set(VULKCORE_SHADER_BINARIES)
foreach(SHADER_SOURCE IN LISTS VULKCORE_SHADER_SOURCES)
    get_filename_component(SHADER_FILE_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_BINARY "${VULKCORE_SHADER_BINARY_DIR}/${SHADER_FILE_NAME}.spv")

    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKCORE_SHADER_BINARY_DIR}"
        COMMAND "${VULKCORE_GLSLC_EXECUTABLE}" -O "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling shader ${SHADER_FILE_NAME}"
        VERBATIM
    )
    list(APPEND VULKCORE_SHADER_BINARIES "${SHADER_BINARY}")
endforeach()

add_custom_target(VulkCoreShaders ALL DEPENDS ${VULKCORE_SHADER_BINARIES})

# Prefer the CMake package exported by the dependency, then fall back to
# pkg-config for environments where only the package metadata is installed.
find_package(glfw3 3.3 CONFIG QUIET)
if(TARGET glfw)
    set(VULKCORE_GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(VULKCORE_GLFW_TARGET glfw3)
endif()

find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
    set(VULKCORE_GLM_TARGET glm::glm)
elseif(TARGET glm::GLM)
    set(VULKCORE_GLM_TARGET glm::GLM)
endif()

if(NOT VULKCORE_GLFW_TARGET OR NOT VULKCORE_GLM_TARGET)
    find_package(PkgConfig QUIET)
endif()

if(NOT VULKCORE_GLFW_TARGET AND PkgConfig_FOUND)
    pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
    if(TARGET PkgConfig::GLFW3)
        set(VULKCORE_GLFW_TARGET PkgConfig::GLFW3)
    endif()
endif()
if(NOT VULKCORE_GLFW_TARGET)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
    set(VULKCORE_GLFW_TARGET glfw)
endif()

if(NOT VULKCORE_GLM_TARGET AND PkgConfig_FOUND)
    pkg_check_modules(GLM QUIET IMPORTED_TARGET glm)
    if(TARGET PkgConfig::GLM)
        set(VULKCORE_GLM_TARGET PkgConfig::GLM)
    endif()
endif()
if(NOT VULKCORE_GLM_TARGET)
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
    set(VULKCORE_GLM_TARGET glm::glm)
endif()

# Dear ImGui is kept as an upstream checkout and compiled with only the
# platform and renderer backends used by VulkCore.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    # Dear ImGui intentionally does not ship a root CMakeLists.txt. Populate
    # the repository here and expose the small set of source files we need.
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(imgui)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()

add_library(vulkcore_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

target_include_directories(vulkcore_imgui
    PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)

target_link_libraries(vulkcore_imgui
    PUBLIC
        Vulkan::Vulkan
        ${VULKCORE_GLFW_TARGET}
)

add_library(vulkcore_physics STATIC
    src/Physics/PhysicsWorld.cpp
)

target_include_directories(vulkcore_physics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(vulkcore_physics SYSTEM
    PRIVATE
        "${VULKCORE_PHYSX_ROOT}/include"
)

target_compile_definitions(vulkcore_physics PRIVATE PX_PHYSX_STATIC_LIB)

target_link_libraries(vulkcore_physics
    PUBLIC
        Vulkan::Headers
        ${VULKCORE_GLM_TARGET}
    PRIVATE
        PhysXExtensions
        PhysXCooking
)

if(MSVC)
    target_compile_options(vulkcore_physics PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(vulkcore_physics PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(vulkcore_physics PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(VulkCore
    src/main.cpp
    src/Application.cpp
    src/Renderer/Buffer.cpp
    src/Renderer/ParticleSystem.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/RendererFrame.cpp
    src/Renderer/RendererPipelines.cpp
    src/Renderer/RendererPrivate.cpp
    src/Renderer/RendererResources.cpp
    src/Renderer/RendererScene.cpp
    src/Renderer/RendererSceneGraph.cpp
    src/Renderer/RendererUI.cpp
    src/Renderer/SwapChain.cpp
    src/Renderer/Texture.cpp
    src/Renderer/VulkanContext.cpp
    src/Gameplay/Player.cpp
    src/Terrain/Grass.cpp
    src/Terrain/Terrain.cpp
    src/Utils/FileUtils.cpp
    src/Utils/ModelLoader.cpp
    src/Utils/Screenshot.cpp
    src/Utils/StbImage.cpp
)

add_dependencies(VulkCore VulkCoreShaders)

if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/vulkcore.rc.in "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc" @ONLY)
    target_sources(VulkCore PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vulkcore.ico"
    )
endif()

target_include_directories(VulkCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_definitions(VulkCore
    PRIVATE
        VULKCORE_SHADER_DIR=\"${VULKCORE_SHADER_BINARY_DIR}\"
        VULKCORE_MODEL_PATH=\"${VULKCORE_MODEL_PATH}\"
        VULKCORE_CONFIG_PATH=\"${VULKCORE_CONFIG_PATH}\"
)

target_link_libraries(VulkCore
    PRIVATE
        Vulkan::Vulkan
        ${VULKCORE_GLFW_TARGET}
        ${VULKCORE_GLM_TARGET}
        ${VULKCORE_ASSIMP_TARGET}
        vulkcore_imgui
        vulkcore_physics
)

if(MSVC)
    target_compile_options(VulkCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(VulkCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_property(TARGET VulkCore PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    target_compile_options(VulkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(VulkCore PROPERTIES
    OUTPUT_NAME "vulkcore"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(vulkcore_physics_smoke
        tests/PhysicsWorldSmoke.cpp
    )
    target_link_libraries(vulkcore_physics_smoke PRIVATE vulkcore_physics)
    add_test(NAME vulkcore_physics_smoke COMMAND vulkcore_physics_smoke)

    add_executable(vulkcore_particle_smoke
        tests/ParticleSystemSmoke.cpp
        src/Renderer/ParticleSystem.cpp
    )
    target_include_directories(vulkcore_particle_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(vulkcore_particle_smoke PRIVATE Vulkan::Vulkan ${VULKCORE_GLM_TARGET})
    add_test(NAME vulkcore_particle_smoke COMMAND vulkcore_particle_smoke)

    add_executable(vulkcore_terrain_smoke
        tests/TerrainSmoke.cpp
        src/Terrain/Terrain.cpp
        src/Utils/FileUtils.cpp
    )
    target_include_directories(vulkcore_terrain_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(vulkcore_terrain_smoke PRIVATE Vulkan::Headers ${VULKCORE_GLM_TARGET})
    add_test(NAME vulkcore_terrain_smoke COMMAND vulkcore_terrain_smoke)
endif()
