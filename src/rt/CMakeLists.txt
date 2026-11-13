file(GLOB RT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

if(RT_SOURCES)
    add_library(rt STATIC ${RT_SOURCES})
    target_include_directories(rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(rt PUBLIC
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        glm::glm
        assimp::assimp
    )
endif()
