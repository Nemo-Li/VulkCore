cmake_minimum_required(VERSION 3.24)
project(VulkCorePlayer LANGUAGES C CXX)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../engine" "${CMAKE_CURRENT_BINARY_DIR}/engine")
