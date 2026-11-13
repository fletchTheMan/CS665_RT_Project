file(GLOB RASTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

if(RASTER_SOURCES)
    add_library(raster STATIC ${RASTER_SOURCES})
    target_include_directories(raster PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(raster PUBLIC
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        glm::glm
        assimp::assimp
    )
endif()
