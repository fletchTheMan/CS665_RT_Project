#include <cstring>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

enum RenderStyle {
	Raster,
	RayTrace
};

class VulkanApp {
public:
	RenderStyle s;
	void run() {
		initVulkan();
		mainLoop();
		cleanup();
	}

private :
	SDL_Window* win;
	void initVulkan(){
		if(!SDL_Init(SDL_INIT_VIDEO)){
			throw std::runtime_error("Failed to initialize SDL\n");
		}
		this->win = SDL_CreateWindow("rendering test", 1920, 1080, SDL_WINDOW_VULKAN);
		if(this->win == NULL){
			throw std::runtime_error("Failed to create window\n");
		}
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
