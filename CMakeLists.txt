cmake_minimum_required(VERSION 3.24)

project(VulkCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

find_program(VULKCORE_GLSLC_EXECUTABLE glslc REQUIRED)

include(FetchContent)

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
set(VULKCORE_DEFAULT_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/mill-scene.fbx")
set(VULKCORE_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vulkcore.ini")
set(VULKCORE_DEFAULT_TERRAIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets/terrain")
set(VULKCORE_MODEL_PATH "${VULKCORE_DEFAULT_MODEL_PATH}" CACHE FILEPATH "Model path; FBX files are supported")
set(VULKCORE_TERRAIN_ROOT "${VULKCORE_DEFAULT_TERRAIN_ROOT}" CACHE PATH
    "BOTW extraction root containing MainField.tscb and terrain/*.hght")
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

if(NOT VULKCORE_GLFW_TARGET)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR
            "GLFW was not found. Install GLFW or provide a glfw3 CMake package."
        )
    endif()

    pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
    set(VULKCORE_GLFW_TARGET PkgConfig::GLFW3)
endif()

if(NOT VULKCORE_GLM_TARGET)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR
            "GLM was not found. Install GLM or provide a glm CMake package."
        )
    endif()

    pkg_check_modules(GLM REQUIRED IMPORTED_TARGET glm)
    set(VULKCORE_GLM_TARGET PkgConfig::GLM)
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
    src/Terrain/Terrain.cpp
    src/Utils/FileUtils.cpp
    src/Utils/ModelLoader.cpp
    src/Utils/StbImage.cpp
)

add_dependencies(VulkCore VulkCoreShaders)

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
        VULKCORE_TERRAIN_ROOT=\"${VULKCORE_TERRAIN_ROOT}\"
)

target_link_libraries(VulkCore
    PRIVATE
        Vulkan::Vulkan
        ${VULKCORE_GLFW_TARGET}
        ${VULKCORE_GLM_TARGET}
        ${VULKCORE_ASSIMP_TARGET}
        vulkcore_imgui
)

if(MSVC)
    target_compile_options(VulkCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(VulkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(VulkCore PROPERTIES
    OUTPUT_NAME "vulkcore"
)
