#include "FBXLoader.h"
#include "SceneGraph.h"
#include "lightVulkanGraphics.h"

int main()
{
	lightGraphics::FBXLoader loader;
	lightGraphics::LightVulkanGraphicsCreateInfo createInfo;
	createInfo.width = 1280;
	createInfo.height = 720;
	createInfo.manageGlfwLifecycle = false;
	createInfo.enableDebugOutput = false;

	(void) loader;
	lightGraphics::Transform transform;
	(void) transform;
	lightGraphics::lightVulkanGraphics app("package-consumer", createInfo);
	(void) app;
	return 0;
}
