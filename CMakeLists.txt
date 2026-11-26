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

project(VulkCore VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    set(VULKCORE_OHOS ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # The SDK passes --gcc-toolchain to Clang; PhysX treats driver warnings as errors.
    add_compile_options(-Wno-unused-command-line-argument)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared dependencies" OFF)

if(VULKCORE_OHOS)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    set_property(TARGET Vulkan::Vulkan PROPERTY INTERFACE_LINK_LIBRARIES Vulkan::Headers vulkan)
else()
    find_package(Vulkan REQUIRED COMPONENTS glslc)
    find_program(VULKCORE_GLSLC_EXECUTABLE glslc
        HINTS "$ENV{VULKAN_SDK}/Bin"
    )
    if(NOT VULKCORE_GLSLC_EXECUTABLE)
        set(VULKCORE_GLSLC_EXECUTABLE "${Vulkan_GLSLC_EXECUTABLE}")
    endif()
endif()

include(FetchContent)

if(VULKCORE_OHOS AND VULKCORE_OHOS_PREBUILT_PHYSX)
    include(cmake/OhosPrebuiltPhysX.cmake)
else()
    include(cmake/PhysXFromSource.cmake)
endif()

# Assimp is used for importing model geometry. Prefer an installed package so
# local package managers can provide a shared build; FetchContent keeps a fresh
# checkout buildable when Assimp is not installed on the host.
find_package(assimp CONFIG QUIET)
if(NOT TARGET assimp::assimp)
    if(VULKCORE_OHOS)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link mobile dependencies into the player" FORCE)
    endif()
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

file(GLOB VULKCORE_SHADER_INCLUDES CONFIGURE_DEPENDS "${VULKCORE_SHADER_SOURCE_DIR}/*.glsl")
set(VULKCORE_SHADER_BINARIES)
if(NOT VULKCORE_OHOS)
foreach(SHADER_SOURCE IN LISTS VULKCORE_SHADER_SOURCES)
    get_filename_component(SHADER_FILE_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_BINARY "${VULKCORE_SHADER_BINARY_DIR}/${SHADER_FILE_NAME}.spv")

    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKCORE_SHADER_BINARY_DIR}"
        COMMAND "${VULKCORE_GLSLC_EXECUTABLE}" -O "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}" ${VULKCORE_SHADER_INCLUDES}
        COMMENT "Compiling shader ${SHADER_FILE_NAME}"
        VERBATIM
    )
    list(APPEND VULKCORE_SHADER_BINARIES "${SHADER_BINARY}")
endforeach()

# The mobile terrain shader omits the road buffer and fragment queries.
set(OHOS_TERRAIN_SOURCE "${VULKCORE_SHADER_SOURCE_DIR}/shader_terrain.frag")
set(OHOS_TERRAIN_BINARY "${VULKCORE_SHADER_BINARY_DIR}/ohos/shader_terrain.frag.spv")
add_custom_command(
    OUTPUT "${OHOS_TERRAIN_BINARY}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKCORE_SHADER_BINARY_DIR}/ohos"
    COMMAND "${VULKCORE_GLSLC_EXECUTABLE}" -O -DVULKCORE_OHOS=1 "${OHOS_TERRAIN_SOURCE}" -o "${OHOS_TERRAIN_BINARY}"
    DEPENDS "${OHOS_TERRAIN_SOURCE}" ${VULKCORE_SHADER_INCLUDES}
    COMMENT "Compiling HarmonyOS terrain shader"
    VERBATIM
)
list(APPEND VULKCORE_SHADER_BINARIES "${OHOS_TERRAIN_BINARY}")

# Desktop scenes authored with the original brush can still edit their strokes.
set(LEGACY_TERRAIN_BINARY "${VULKCORE_SHADER_BINARY_DIR}/legacy/shader_terrain.frag.spv")
add_custom_command(
    OUTPUT "${LEGACY_TERRAIN_BINARY}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKCORE_SHADER_BINARY_DIR}/legacy"
    COMMAND "${VULKCORE_GLSLC_EXECUTABLE}" -O -DVULKCORE_LEGACY_ROAD_BRUSH=1 "${OHOS_TERRAIN_SOURCE}" -o "${LEGACY_TERRAIN_BINARY}"
    DEPENDS "${OHOS_TERRAIN_SOURCE}" ${VULKCORE_SHADER_INCLUDES}
    COMMENT "Compiling desktop terrain brush shader"
    VERBATIM
)
list(APPEND VULKCORE_SHADER_BINARIES "${LEGACY_TERRAIN_BINARY}")

# Compile depth-only forms of materials whose coverage is procedural. They reuse the
# visible shader's discard and alpha rules without accessing the receiving shadow map.
foreach(SHADOW_MATERIAL IN ITEMS grass grid particle surface_wipe tornado uv_ribbon)
    set(SHADER_SOURCE "${VULKCORE_SHADER_SOURCE_DIR}/shader_${SHADOW_MATERIAL}.frag")
    set(SHADER_BINARY "${VULKCORE_SHADER_BINARY_DIR}/sun_caster_${SHADOW_MATERIAL}.frag.spv")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKCORE_SHADER_BINARY_DIR}"
        COMMAND "${VULKCORE_GLSLC_EXECUTABLE}" -O -DVULKCORE_SHADOW_PASS=1 "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}" ${VULKCORE_SHADER_INCLUDES}
        COMMENT "Compiling sun shadow coverage for ${SHADOW_MATERIAL}"
        VERBATIM
    )
    list(APPEND VULKCORE_SHADER_BINARIES "${SHADER_BINARY}")
endforeach()

add_custom_target(VulkCoreShaders ALL DEPENDS ${VULKCORE_SHADER_BINARIES})
endif()

# Prefer the CMake package exported by the dependency, then fall back to
# pkg-config for environments where only the package metadata is installed.
if(NOT VULKCORE_OHOS)
find_package(glfw3 3.3 CONFIG QUIET)
if(TARGET glfw)
    set(VULKCORE_GLFW_TARGET glfw)
elseif(TARGET glfw3)
    set(VULKCORE_GLFW_TARGET glfw3)
endif()
endif()

find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
    set(VULKCORE_GLM_TARGET glm::glm)
elseif(TARGET glm::GLM)
    set(VULKCORE_GLM_TARGET glm::GLM)
endif()

if(VULKCORE_OHOS AND NOT VULKCORE_GLM_TARGET)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1 GIT_SHALLOW TRUE EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(glm)
    set(VULKCORE_GLM_TARGET glm::glm)
endif()

if((NOT VULKCORE_GLFW_TARGET AND NOT VULKCORE_OHOS) OR NOT VULKCORE_GLM_TARGET)
    find_package(PkgConfig QUIET)
endif()

if(NOT VULKCORE_OHOS AND NOT VULKCORE_GLFW_TARGET AND PkgConfig_FOUND)
    pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
    if(TARGET PkgConfig::GLFW3)
        set(VULKCORE_GLFW_TARGET PkgConfig::GLFW3)
    endif()
endif()
if(NOT VULKCORE_OHOS AND NOT VULKCORE_GLFW_TARGET)
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
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

if(NOT VULKCORE_OHOS)
    target_sources(vulkcore_imgui PRIVATE "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
endif()

target_include_directories(vulkcore_imgui
    PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)

target_compile_definitions(vulkcore_imgui PUBLIC IMGUI_USE_WCHAR32)

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
target_compile_definitions(vulkcore_physics PRIVATE PX_PHYSX_STATIC_LIB)
target_link_libraries(vulkcore_physics PUBLIC Vulkan::Headers ${VULKCORE_GLM_TARGET})
if(TARGET vulkcore_ohos_physx)
    target_link_libraries(vulkcore_physics PRIVATE vulkcore_ohos_physx)
else()
    target_include_directories(vulkcore_physics SYSTEM PRIVATE "${VULKCORE_PHYSX_ROOT}/include")
    if(VULKCORE_OHOS)
        vulkcore_configure_ohos_physx_target(vulkcore_physics)
    endif()
    target_link_libraries(vulkcore_physics PRIVATE PhysXExtensions PhysXCooking)
endif()

if(MSVC)
    target_compile_options(vulkcore_physics PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(vulkcore_physics PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(vulkcore_physics PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(yaml-cpp 0.8 CONFIG QUIET)
if(NOT TARGET yaml-cpp::yaml-cpp)
    function(vulkcore_fetch_yaml_cpp)
        # yaml-cpp 0.8 declares CMake 3.4. Scope compatibility to this dependency.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND
           "${CMAKE_POLICY_VERSION_MINIMUM}" VERSION_LESS 3.10)
            set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        endif()
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(yaml-cpp)
    endfunction()
    vulkcore_fetch_yaml_cpp()
endif()

set(VULKCORE_RUNTIME_SOURCES
    src/Renderer/Buffer.cpp
    src/Renderer/ParticleSystem.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/RendererFrame.cpp
    src/Renderer/RendererForest.cpp
    src/Renderer/RendererModelViewer.cpp
    src/Renderer/HdrComposition.cpp
    src/Renderer/RendererPipelines.cpp
    src/Renderer/RendererPrivate.cpp
    src/Renderer/RendererResources.cpp
    src/Renderer/RendererRealtimeEffects.cpp
    src/Renderer/RendererRoadPaint.cpp
    src/Renderer/RendererScene.cpp
    src/Renderer/RendererSceneGraph.cpp
    src/Renderer/RendererSunShadows.cpp
    src/Renderer/RendererUI.cpp
    src/Renderer/RendererGameEditor.cpp
    src/Renderer/SwapChain.cpp
    src/Renderer/Texture.cpp
    src/Renderer/VulkanContext.cpp
    src/Gameplay/Player.cpp
    src/Terrain/Grass.cpp
    src/Terrain/RoadLayout.cpp
    src/Terrain/RoadPaint.cpp
    src/Terrain/Terrain.cpp
    src/Utils/FileUtils.cpp
    src/Utils/ModelLoader.cpp
    src/Utils/SbfresLoader.cpp
    src/Utils/Screenshot.cpp
    src/Utils/StbImage.cpp
)

if(VULKCORE_OHOS)
    set(VULKCORE_RUNTIME_TARGET VulkCore)
    add_library(VulkCore SHARED ${VULKCORE_RUNTIME_SOURCES}
        src/Platform/OhosWindow.cpp src/Platform/OhosRuntime.cpp)
    target_compile_definitions(VulkCore PRIVATE VULKCORE_OHOS VK_USE_PLATFORM_OHOS NDEBUG)
    target_compile_definitions(vulkcore_imgui PUBLIC VK_USE_PLATFORM_OHOS)
    target_link_libraries(VulkCore PRIVATE ace_ndk.z ace_napi.z rawfile.z hilog_ndk.z native_window)
    # Resolve paths against the extracted rawfile directory at runtime.
    set(VULKCORE_SHADER_BINARY_DIR "shaders")
    set(VULKCORE_MODEL_PATH "assets/models/mill-scene.fbx")
    set(VULKCORE_CONFIG_PATH "vulkcore.ini")
    set(VULKCORE_ASSET_DIR "assets")
else()
    set(VULKCORE_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_library(vulkcore_export STATIC src/Export/OhosExporter.cpp)
    target_include_directories(vulkcore_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(vulkcore_export PRIVATE
        VULKCORE_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        VULKCORE_EXPORT_SHADER_DIR=\"${VULKCORE_SHADER_BINARY_DIR}\")
    find_package(Threads REQUIRED)
    set(VULKCORE_RUNTIME_TARGET vulkcore_runtime)
    add_library(vulkcore_runtime STATIC ${VULKCORE_RUNTIME_SOURCES}
        src/Platform/DesktopWindow.cpp
        src/Renderer/RendererEditorShaders.cpp
        src/Renderer/RendererResourceBrowser.cpp
        src/Renderer/RendererResourcePreview.cpp)
    target_compile_definitions(vulkcore_runtime PRIVATE
        VULKCORE_SHADER_SOURCE_DIR="${VULKCORE_SHADER_SOURCE_DIR}"
        VULKCORE_GLSLC="${VULKCORE_GLSLC_EXECUTABLE}")
    if(APPLE)
        enable_language(OBJCXX)
        target_sources(vulkcore_runtime PRIVATE src/Platform/EditorFileDialog.mm src/Platform/EditorTrash.mm)
        target_link_libraries(vulkcore_runtime PRIVATE
            "-framework AppKit" "-framework Foundation" "-framework UniformTypeIdentifiers")
        option(VULKCORE_EFFECT_EDITOR "Build the embedded effect editor" ON)
        if(VULKCORE_EFFECT_EDITOR)
            find_program(VULKCORE_NPM npm)
            if(VULKCORE_NPM)
                set(vfx_source "${CMAKE_CURRENT_SOURCE_DIR}/tools/vfx-studio")
                set(vfx_output "${CMAKE_CURRENT_BINARY_DIR}/vfx-studio")
                file(GLOB_RECURSE vfx_sources CONFIGURE_DEPENDS
                    "${vfx_source}/src/*" "${vfx_source}/scripts/*" "${vfx_source}/tests/*")
                add_custom_command(OUTPUT "${vfx_output}/.npm-ready"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${vfx_output}"
                    COMMAND "${VULKCORE_NPM}" ci --no-audit --no-fund
                    COMMAND "${CMAKE_COMMAND}" -E touch "${vfx_output}/.npm-ready"
                    WORKING_DIRECTORY "${vfx_source}"
                    DEPENDS "${vfx_source}/package.json" "${vfx_source}/package-lock.json"
                    COMMENT "Installing effect editor build dependencies"
                    VERBATIM)
                add_custom_command(OUTPUT "${vfx_output}/site/index.html"
                    COMMAND "${VULKCORE_NPM}" run build -- --outDir "${vfx_output}/site" --emptyOutDir
                    WORKING_DIRECTORY "${vfx_source}"
                    DEPENDS "${vfx_output}/.npm-ready" ${vfx_sources}
                        "${vfx_source}/index.html" "${vfx_source}/vite.config.ts"
                        "${vfx_source}/tsconfig.json"
                    COMMENT "Building the VulkCore effect editor"
                    VERBATIM)
                add_custom_target(VulkCoreEffectEditor DEPENDS "${vfx_output}/site/index.html")
                add_dependencies(vulkcore_runtime VulkCoreEffectEditor)
                target_sources(vulkcore_runtime PRIVATE src/Platform/EffectEditorView.mm)
                set_source_files_properties(src/Platform/EffectEditorView.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
                target_compile_definitions(vulkcore_runtime
                    PUBLIC VULKCORE_HAS_EFFECT_EDITOR
                    PRIVATE VULKCORE_EFFECT_EDITOR_DIR="${vfx_output}/site")
                target_link_libraries(vulkcore_runtime PRIVATE "-framework WebKit")
            else()
                message(STATUS "npm unavailable; skipping embedded effect editor")
            endif()
        endif()
    else()
        target_sources(vulkcore_runtime PRIVATE src/Platform/EditorTrash.cpp)
        if(WIN32)
            target_sources(vulkcore_runtime PRIVATE src/Platform/EditorFileDialog.cpp)
            target_link_libraries(vulkcore_runtime PRIVATE comdlg32 ole32 shell32 uuid)
        endif()
    endif()
    target_link_libraries(vulkcore_runtime PRIVATE vulkcore_export Threads::Threads)
    add_dependencies(vulkcore_runtime VulkCoreShaders)

    add_executable(VulkCore src/main.cpp src/Application.cpp)
    target_link_libraries(VulkCore PRIVATE vulkcore_runtime vulkcore_export)
    add_executable(sbfres_viewer src/Tools/SbfresViewer.cpp)
    target_link_libraries(sbfres_viewer PRIVATE vulkcore_runtime)
endif()

if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/vulkcore.rc.in "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc" @ONLY)
    target_sources(VulkCore PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/vulkcore.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vulkcore.ico"
    )
endif()

target_include_directories(${VULKCORE_RUNTIME_TARGET}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_definitions(${VULKCORE_RUNTIME_TARGET}
    PUBLIC
        VULKCORE_SHADER_DIR=\"${VULKCORE_SHADER_BINARY_DIR}\"
        VULKCORE_ASSET_DIR=\"${VULKCORE_ASSET_DIR}\"
        VULKCORE_MODEL_PATH=\"${VULKCORE_MODEL_PATH}\"
        VULKCORE_CONFIG_PATH=\"${VULKCORE_CONFIG_PATH}\"
)

target_link_libraries(${VULKCORE_RUNTIME_TARGET}
    PUBLIC
        Vulkan::Vulkan
        ${VULKCORE_GLFW_TARGET}
        ${VULKCORE_GLM_TARGET}
    PRIVATE
        ${VULKCORE_ASSIMP_TARGET}
        yaml-cpp::yaml-cpp
        vulkcore_imgui
        vulkcore_physics
)

set(VULKCORE_BUILD_TARGETS ${VULKCORE_RUNTIME_TARGET})
if(NOT VULKCORE_OHOS)
    list(APPEND VULKCORE_BUILD_TARGETS VulkCore sbfres_viewer)
endif()
foreach(target IN LISTS VULKCORE_BUILD_TARGETS)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        set_property(TARGET ${target} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

set_target_properties(VulkCore PROPERTIES
    OUTPUT_NAME "vulkcore"
)

include(CTest)
if(BUILD_TESTING AND NOT VULKCORE_OHOS)
    add_executable(vulkcore_movement_smoke tests/MovementInputSmoke.cpp)
    target_include_directories(vulkcore_movement_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME vulkcore_movement_smoke COMMAND vulkcore_movement_smoke)

    add_executable(vulkcore_touch_smoke tests/TouchLookSmoke.cpp)
    target_include_directories(vulkcore_touch_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME vulkcore_touch_smoke COMMAND vulkcore_touch_smoke)

    add_executable(vulkcore_surface_smoke tests/SurfacePresentationSmoke.cpp)
    target_include_directories(vulkcore_surface_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(vulkcore_surface_smoke PRIVATE Vulkan::Vulkan ${VULKCORE_GLM_TARGET})
    add_test(NAME vulkcore_surface_smoke COMMAND vulkcore_surface_smoke)

    add_executable(vulkcore_export_smoke tests/OhosExportSmoke.cpp)
    target_link_libraries(vulkcore_export_smoke PRIVATE vulkcore_export)
    add_dependencies(vulkcore_export_smoke VulkCoreShaders)
    add_test(NAME vulkcore_export_smoke COMMAND vulkcore_export_smoke)
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
        src/Terrain/Grass.cpp
        src/Terrain/RoadLayout.cpp
        src/Terrain/Terrain.cpp
        src/Utils/FileUtils.cpp
        src/Utils/SbfresLoader.cpp
    )
    target_include_directories(vulkcore_terrain_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(vulkcore_terrain_smoke PRIVATE Vulkan::Headers ${VULKCORE_GLM_TARGET} yaml-cpp::yaml-cpp)
    add_test(NAME vulkcore_terrain_smoke COMMAND vulkcore_terrain_smoke)
endif()
