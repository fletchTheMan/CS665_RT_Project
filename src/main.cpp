#include <cstring>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <vulkan/vulkan_core.h>

enum RenderStyle {
	Raster,
	RayTrace
};

const bool use_validation_layers = false; //set to false for performance. Source None

class VulkanApp {
public:
	RenderStyle s;
	void run() {
		init();
		mainLoop();
		cleanup();
	}

private :
	VkInstance instance;
	VkPhysicalDevice phys_gpu;
	VkDevice gpu;
	VkSurfaceKHR surface; 
	VkDebugUtilsMessengerEXT debug_messenger;
	SDL_Window* win;
	void init(){
		if(!SDL_Init(SDL_INIT_VIDEO)){
			throw std::runtime_error("Failed to initialize SDL\n");
		}
		this->win = SDL_CreateWindow("rendering test", 1920, 1080, SDL_WINDOW_VULKAN);
		if(this->win == NULL){
			throw std::runtime_error("Failed to create window\n");
		}

		initVulkan();

	}

	void initVulkan(){
		vkb::InstanceBuilder builder;
		auto result = builder.set_app_name("Rend")
			.request_validation_layers(use_validation_layers)
			.use_default_debug_messenger()
			.require_api_version(1, 3, 0)
			.build();

		vkb::Instance vkbInstance = result.value();
		this->instance = vkbInstance.instance;
		this->debug_messenger = vkbInstance.debug_messenger;
		//The NULL is the allocator and NULL will use the default vulkan allocator 
		//source https://wiki.libsdl.org/SDL3/SDL_Vulkan_CreateSurface
		SDL_Vulkan_CreateSurface(this->win, this->instance, NULL, &(this->surface));

		VkPhysicalDeviceVulkan13Features features13 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		features13.dynamicRendering = true;
		features13.synchronization2 = true;
		VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.bufferDeviceAddress = true;
		features12.descriptorIndexing = true;
		features12.bufferDeviceAddressMultiDevice = true; //This should allow for the ray tracing to be done. source https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html


		vkb::PhysicalDeviceSelector selector{ vkbInstance};
		vkb::PhysicalDevice physDevice = selector.set_minimum_version(1, 3)
			.set_required_features_13(features13).set_required_features_12(features12)
			.set_surface(this->surface).select().value();

		vkb::DeviceBuilder deviceBuilder { physDevice};
		vkb::Device vkbDevice = deviceBuilder.build().value();

		this->gpu = vkbDevice.device;
		this->phys_gpu = physDevice.physical_device;
	}

	void mainLoop(){
		SDL_Event e;
		bool close = false;
		while(!close){
			while(SDL_PollEvent(&e) != 0){
				if(e.type == SDL_EVENT_QUIT){
					close = true;
				}
			}
			if(s == Raster){
				drawRaster();
			}
			else{
				drawRaytrace();
			}
		}
	}

	void cleanup(){
		SDL_DestroyWindow(win);
		SDL_Quit();
	}

	void drawRaytrace(){}
	void drawRaster(){}
};

int main(int argc, char **argv){
	VulkanApp app;
	if(argc == 0){
		app.s = Raster;
	}
	else if(strcmp(argv[1], "rt")){
		app.s = RayTrace;
	}
	else{
		app.s = Raster;
	}
	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
