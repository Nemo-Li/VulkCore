cmake_minimum_required(VERSION 3.24)

project(VulkCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

find_program(VULKCORE_GLSLC_EXECUTABLE glslc REQUIRED)

set(VULKCORE_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src")
set(VULKCORE_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(GLOB VULKCORE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${VULKCORE_SHADER_SOURCE_DIR}/*.vert"
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

add_executable(VulkCore
    src/main.cpp
    src/Application.cpp
    src/Renderer/Buffer.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/SwapChain.cpp
    src/Renderer/VulkanContext.cpp
    src/Utils/FileUtils.cpp
)

add_dependencies(VulkCore VulkCoreShaders)

target_include_directories(VulkCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(VulkCore
    PRIVATE
        VULKCORE_SHADER_DIR=\"${VULKCORE_SHADER_BINARY_DIR}\"
)

target_link_libraries(VulkCore
    PRIVATE
        Vulkan::Vulkan
        ${VULKCORE_GLFW_TARGET}
        ${VULKCORE_GLM_TARGET}
)

if(MSVC)
    target_compile_options(VulkCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(VulkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(VulkCore PROPERTIES
    OUTPUT_NAME "vulkcore"
)
