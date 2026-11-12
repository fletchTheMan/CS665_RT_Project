#include <vulkan/vulkan.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>

class VulkanApp {
public:
	void run() {
		initVulkan();
		mainLoop();
		cleanup();
	}

private :
	void initVulkan(){}
	void mainLoop(){}
	void cleanup(){}
};

int main(){
	VulkanApp app;
	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
