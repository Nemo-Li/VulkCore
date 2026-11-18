# Reuse PhysX's Android CPU backend while compiling against OHOS. Keep all
# adaptations in the build directory; the shared dependency checkout is untouched.
set(VULKCORE_OHOS_PHYSX_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ohos-physx")
set(VULKCORE_OHOS_PHYSX_PREPROCESSOR "${VULKCORE_OHOS_PHYSX_DIRECTORY}/PxPreprocessor.h")
file(MAKE_DIRECTORY "${VULKCORE_OHOS_PHYSX_DIRECTORY}")

# Select the Android implementation inside PhysX as well as its CMake rules.
# Do not define __ANDROID__: SDK headers and other libraries must still see OHOS.
file(READ "${VULKCORE_PHYSX_ROOT}/include/foundation/PxPreprocessor.h" preprocessor)
string(REPLACE "#elif defined(__ANDROID__)"
    "#elif defined(__ANDROID__) || defined(VULKCORE_OHOS)" preprocessor "${preprocessor}")
string(APPEND preprocessor "\n#if !PX_ANDROID || PX_LINUX\n#error \"OHOS PhysX must use the Android implementation\"\n#endif\n")
file(CONFIGURE OUTPUT "${VULKCORE_OHOS_PHYSX_PREPROCESSOR}" CONTENT "${preprocessor}" @ONLY)

function(vulkcore_configure_ohos_physx_target target)
    target_compile_definitions(${target} PRIVATE VULKCORE_OHOS)
    # Force the platform header before upstream headers, including PxPhysicsAPI.h
    # which resolves its quoted include relative to the original include root.
    target_compile_options(${target} PRIVATE -include "${VULKCORE_OHOS_PHYSX_PREPROCESSOR}")
endfunction()

get_property(physx_targets DIRECTORY "${VULKCORE_PHYSX_ROOT}/source/compiler/cmake" PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS physx_targets)
    vulkcore_configure_ohos_physx_target(${target})
endforeach()

function(vulkcore_ohos_physx_source filename)
    set(original "${VULKCORE_PHYSX_ROOT}/source/foundation/unix/${filename}")
    file(READ "${original}" content)
    if(filename STREQUAL "FdUnixThread.cpp")
        # Replace the NDK-only CPU helper with the native OHOS POSIX query.
        string(REPLACE "#if PX_ANDROID\nextern \"C\" {\nint android_getCpuCount(void);\n}\n#endif" "" content "${content}")
        string(REPLACE "return android_getCpuCount();"
            "const long count = sysconf(_SC_NPROCESSORS_ONLN);\n\treturn count > 0 ? static_cast<uint32_t>(count) : 1;"
            content "${content}")
        string(REPLACE "#if PX_ANDROID && (__ANDROID_API__ > 8)" "#if PX_ANDROID" content "${content}")
    elseif(filename STREQUAL "FdUnixPrintString.cpp")
        string(REPLACE "#include <android/log.h>" "#include <hilog/log.h>" content "${content}")
        string(REPLACE "__android_log_print(ANDROID_LOG_INFO, \"PsPrintString\", \"%s\", str);"
            "OH_LOG_Print(LOG_APP, LOG_INFO, 0x3200, \"PhysX\", \"%{public}s\", str);" content "${content}")
    elseif(filename STREQUAL "FdUnixSocket.cpp")
        string(REPLACE "#include <sys/poll.h>" "#include <poll.h>" content "${content}")
    endif()
    set(generated "${VULKCORE_OHOS_PHYSX_DIRECTORY}/${filename}")
    file(CONFIGURE OUTPUT "${generated}" CONTENT "${content}" @ONLY)
    get_target_property(sources PhysXFoundation SOURCES)
    list(REMOVE_ITEM sources "${original}")
    set_property(TARGET PhysXFoundation PROPERTY SOURCES "${sources}")
    target_sources(PhysXFoundation PRIVATE "${generated}")
endfunction()

# The Android CMake preset lists the legacy NDK cpufeatures source. OHOS needs
# neither the NDK nor liblog; CPU discovery and logging are adapted above.
get_target_property(foundation_sources PhysXFoundation SOURCES)
list(FILTER foundation_sources EXCLUDE REGEX "/sources/android/cpufeatures/cpu-features\\.c$")
set_property(TARGET PhysXFoundation PROPERTY SOURCES "${foundation_sources}")
foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(libraries PhysXFoundation ${property})
    if(libraries)
        list(REMOVE_ITEM libraries "log" "$<LINK_ONLY:log>")
        set_property(TARGET PhysXFoundation PROPERTY ${property} "${libraries}")
    endif()
endforeach()
target_link_libraries(PhysXFoundation PRIVATE hilog_ndk.z)

foreach(filename FdUnixThread.cpp FdUnixPrintString.cpp FdUnixSocket.cpp)
    vulkcore_ohos_physx_source("${filename}")
endforeach()
