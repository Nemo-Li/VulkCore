if(NOT OHOS_ARCH STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The bundled OHOS PhysX SDK requires arm64-v8a (OHOS_ARCH='${OHOS_ARCH}').")
endif()

set(physx_sdk "${CMAKE_CURRENT_SOURCE_DIR}/third_party/physx")
foreach(header IN ITEMS PxPhysicsAPI.h foundation/PxPreprocessor.h)
    if(NOT EXISTS "${physx_sdk}/include/${header}")
        message(FATAL_ERROR "Missing bundled OHOS PhysX header: ${physx_sdk}/include/${header}")
    endif()
endforeach()

add_library(vulkcore_ohos_physx INTERFACE)
target_include_directories(vulkcore_ohos_physx SYSTEM INTERFACE "${physx_sdk}/include")
# Match the platform and Release settings of the archives in all player builds.
target_compile_definitions(vulkcore_ohos_physx INTERFACE VULKCORE_OHOS NDEBUG)

foreach(component IN ITEMS PhysXExtensions PhysXPvdSDK PhysX PhysXCooking PhysXCommon PhysXFoundation)
    set(archive "${physx_sdk}/lib/${OHOS_ARCH}/lib${component}_static_64.a")
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Missing bundled OHOS PhysX archive: ${archive}")
    endif()
    add_library(VulkCorePhysX::${component} STATIC IMPORTED)
    set_target_properties(VulkCorePhysX::${component} PROPERTIES IMPORTED_LOCATION "${archive}")
    target_link_libraries(vulkcore_ohos_physx INTERFACE VulkCorePhysX::${component})
endforeach()

target_link_libraries(vulkcore_ohos_physx INTERFACE dl hilog_ndk.z m)
