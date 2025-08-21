// Minimal Vulkan sphere-cloud with instancing + GLFW camera
// Build: see CMakeLists.txt

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <array>
#include <optional>
#include <chrono>
#include <random>
#include <stdexcept>
#include <fstream>
#include <iostream>

#define VK_CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) { \
    std::fprintf(stderr, "VK error %d at %s:%d\n", r, __FILE__, __LINE__); std::abort(); } } while(0)

static const uint32_t WIDTH = 1280, HEIGHT = 720;
static const int NUM_SPHERES = 800; // adjust if your iGPU is slow

// ---------------- Window & input (GLFW) ----------------
GLFWwindow* gWindow = nullptr;
bool gMouseLook = false;
double gLastX=0, gLastY=0;
float gYaw = -90.0f, gPitch = 0.0f;
glm::vec3 gCamPos(0, 0, 8);
float gMoveSpeed = 6.0f;
float gMouseSens = 0.12f;

bool gKeys[1024]{};

static void keyCb(GLFWwindow* w, int key, int sc, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) gKeys[key] = true;
        if (action == GLFW_RELEASE) gKeys[key] = false;
    }
}
static void mouseBtnCb(GLFWwindow* w, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        gMouseLook = (action == GLFW_PRESS);
        glfwSetInputMode(w, GLFW_CURSOR, gMouseLook ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(w, &gLastX, &gLastY);
    }
}
static void cursorCb(GLFWwindow* w, double x, double y) {
    if (!gMouseLook) return;
    double dx = x - gLastX, dy = y - gLastY;
    gLastX = x; gLastY = y;
    gYaw   += float(dx) * gMouseSens;
    gPitch -= float(dy) * gMouseSens;
    gPitch = glm::clamp(gPitch, -89.0f, 89.0f);
}

static glm::vec3 camForward() {
    float cy = cos(glm::radians(gYaw)),   sy = sin(glm::radians(gYaw));
    float cp = cos(glm::radians(gPitch)), sp = sin(glm::radians(gPitch));
    return glm::normalize(glm::vec3(cy*cp, sp, sy*cp));
}
static glm::vec3 camRight() { return glm::normalize(glm::cross(camForward(), glm::vec3(0,1,0))); }
static void updateCamera(float dt) {
    if (gKeys[GLFW_KEY_W]) gCamPos += camForward() * gMoveSpeed * dt;
    if (gKeys[GLFW_KEY_S]) gCamPos -= camForward() * gMoveSpeed * dt;
    if (gKeys[GLFW_KEY_A]) gCamPos -= camRight()   * gMoveSpeed * dt;
    if (gKeys[GLFW_KEY_D]) gCamPos += camRight()   * gMoveSpeed * dt;
    if (gKeys[GLFW_KEY_Q]) gCamPos.y -= gMoveSpeed * dt;
    if (gKeys[GLFW_KEY_E]) gCamPos.y += gMoveSpeed * dt;
}

// ---------------- Vulkan helpers ----------------
static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open file: " + path);
    return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}

struct QueueFamilyIndices { std::optional<uint32_t> gfx, present; bool complete() const { return gfx && present; } };

static uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i=0;i<mp.memoryTypeCount;++i) {
        if ((typeFilter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("No suitable memory type");
}

struct Buffer {
    VkDevice device{};
    VkBuffer buf{};
    VkDeviceMemory mem{};
    VkDeviceSize size{};
    void destroy() {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
        if (mem) vkFreeMemory(device, mem, nullptr);
        buf = VK_NULL_HANDLE; mem = VK_NULL_HANDLE; size = 0;
    }
};
static void createBuffer(VkPhysicalDevice phys, VkDevice dev, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props, Buffer& out) {
    out.device = dev; out.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &out.buf));
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(dev, out.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &out.mem));
    VK_CHECK(vkBindBufferMemory(dev, out.buf, out.mem, 0));
}

// ---------------- Geometry (UV sphere) ----------------
struct Vertex { glm::vec3 pos; glm::vec3 nrm; };
struct Instance { glm::mat4 model; glm::vec3 color; float pad; };
struct UBO { glm::mat4 view; glm::mat4 proj; };

static void make_uv_sphere(float r, int stacks, int slices,
                           std::vector<Vertex>& verts, std::vector<uint32_t>& idx) {
    verts.clear(); idx.clear();
    for (int y=0;y<=stacks;++y){
        float v = float(y)/stacks;
        float phi = v * glm::pi<float>();
        float cp = cos(phi), sp = sin(phi);
        for (int x=0;x<=slices;++x){
            float u = float(x)/slices;
            float th = u * glm::two_pi<float>();
            float ct = cos(th), st = sin(th);
            glm::vec3 p = r * glm::vec3(ct*sp, cp, st*sp);
            glm::vec3 n = glm::normalize(p);
            verts.push_back({p,n});
        }
    }
    auto id = [&](int y,int x){ return y*(slices+1)+x; };
    for (int y=0;y<stacks;++y){
        for(int x=0;x<slices;++x){
            uint32_t i0=id(y,x), i1=id(y+1,x), i2=id(y+1,x+1), i3=id(y,x+1);
            idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i3);
        }
    }
}

// ---------------- Vulkan state ----------------
struct VkApp {
    GLFWwindow* wnd{};
    VkInstance inst{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice phys{};
    VkDevice dev{};
    VkQueue qGfx{}, qPresent{};
    uint32_t qFamGfx=0, qFamPresent=0;

    VkSwapchainKHR swapchain{};
    VkFormat swapFormat{};
    VkExtent2D swapExtent{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;

    VkRenderPass rp{};
    VkPipelineLayout pipeLayout{};
    VkPipeline pipe{};
    VkDescriptorSetLayout dsl{};
    VkDescriptorPool descPool{};
    std::vector<VkDescriptorSet> descSets;

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool cmdPool{};
    std::vector<VkCommandBuffer> cmds;

    VkSemaphore semImage{}, semRender{};
    VkFence inFlight{};

    // Buffers
    Buffer vbo, ibo, instanceBuf;
    std::vector<Buffer> ubos;

    // Geometry counts
    uint32_t indexCount=0;
    uint32_t instanceCount=0;

    // Timing
    std::chrono::steady_clock::time_point tPrev;

    void init(GLFWwindow* window) {
        wnd = window;

        // --- Instance
        uint32_t extCount=0;
        const char** reqExt = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts(reqExt, reqExt+extCount);
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ii.pApplicationInfo = &ai;
        ii.enabledExtensionCount = (uint32_t)exts.size();
        ii.ppEnabledExtensionNames = exts.data();
        VK_CHECK(vkCreateInstance(&ii, nullptr, &inst));

        // --- Surface
        VK_CHECK(glfwCreateWindowSurface(inst, wnd, nullptr, &surface));

        // --- Pick physical + queues
        uint32_t nPhys=0; vkEnumeratePhysicalDevices(inst, &nPhys, nullptr);
        if (!nPhys) throw std::runtime_error("No Vulkan device");
        std::vector<VkPhysicalDevice> physList(nPhys); vkEnumeratePhysicalDevices(inst, &nPhys, physList.data());

        auto supports = [&](VkPhysicalDevice pd)->std::optional<std::pair<uint32_t,uint32_t>>{
            uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qfp(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qfp.data());
            std::optional<uint32_t> g, p;
            for (uint32_t i=0;i<qCount;++i){
                if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) g = i;
                VkBool32 present=VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
                if (present) p = i;
            }
            if (g && p) return std::make_pair(*g, *p);
            return std::nullopt;
        };

        for (auto pd : physList) {
            auto qp = supports(pd);
            if (qp) { phys = pd; qFamGfx = qp->first; qFamPresent = qp->second; break; }
        }
        if (!phys) throw std::runtime_error("No suitable GPU");

        // --- Device + queues
        float prio = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> qcis;
        std::vector<uint32_t> uniqueFamilies = (qFamGfx==qFamPresent) ?
            std::vector<uint32_t>{qFamGfx} : std::vector<uint32_t>{qFamGfx,qFamPresent};
        for (uint32_t fam : uniqueFamilies) {
            VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qci.queueFamilyIndex = fam;
            qci.queueCount = 1;
            qci.pQueuePriorities = &prio;
            qcis.push_back(qci);
        }
        const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = (uint32_t)qcis.size();
        dci.pQueueCreateInfos = qcis.data();
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;
        VK_CHECK(vkCreateDevice(phys, &dci, nullptr, &dev));
        vkGetDeviceQueue(dev, qFamGfx, 0, &qGfx);
        vkGetDeviceQueue(dev, qFamPresent, 0, &qPresent);

        // --- Swapchain
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
        uint32_t fmtCount=0; vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts.data());
        VkSurfaceFormatKHR chosen = fmts[0];
        for (auto& f : fmts) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format==VK_FORMAT_B8G8R8A8_SRGB) { chosen = f; break; }
        }
        swapFormat = chosen.format;
        swapExtent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{WIDTH, HEIGHT};
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync

        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = std::max(2u, caps.minImageCount);
        sci.imageFormat = swapFormat;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = swapExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (qFamGfx != qFamPresent) {
            uint32_t qidx[2] = { qFamGfx, qFamPresent };
            sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices = qidx;
        } else {
            sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = presentMode;
        sci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(dev, &sci, nullptr, &swapchain));
        uint32_t nImgs=0; vkGetSwapchainImagesKHR(dev, swapchain, &nImgs, nullptr);
        swapImages.resize(nImgs);
        vkGetSwapchainImagesKHR(dev, swapchain, &nImgs, swapImages.data());

        // --- Image views
        for (auto img : swapImages) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swapFormat;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            VkImageView view{};
            VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &view));
            swapViews.push_back(view);
        }

        // --- Render pass
        VkAttachmentDescription color{};
        color.format = swapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp= VK_ATTACHMENT_STORE_OP_STORE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1; rpci.pAttachments = &color;
        rpci.subpassCount = 1; rpci.pSubpasses = &sub;
        VK_CHECK(vkCreateRenderPass(dev, &rpci, nullptr, &rp));

        // --- Descriptor set layout (UBO at binding 0)
        VkDescriptorSetLayoutBinding uboB{};
        uboB.binding = 0;
        uboB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboB.descriptorCount = 1;
        uboB.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dsli.bindingCount = 1; dsli.pBindings = &uboB;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &dsli, nullptr, &dsl));

        // --- Pipeline layout
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        VK_CHECK(vkCreatePipelineLayout(dev, &plci, nullptr, &pipeLayout));

        // --- Shaders
        auto vsCode = readFile(std::string("spv/instanced_sphere.vert.spv"));
        auto fsCode = readFile(std::string("spv/instanced_sphere.frag.spv"));
        auto mkModule = [&](const std::vector<char>& code)->VkShaderModule{
            VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            smi.codeSize = code.size();
            smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule m{}; VK_CHECK(vkCreateShaderModule(dev, &smi, nullptr, &m)); return m;
        };
        VkShaderModule vs = mkModule(vsCode), fs = mkModule(fsCode);

        // --- Vertex input: binding 0 = sphere verts, binding 1 = instance data
        VkVertexInputBindingDescription binds[2]{};
        binds[0].binding = 0; binds[0].stride = sizeof(Vertex);   binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binds[1].binding = 1; binds[1].stride = sizeof(Instance); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 7> attrs{};
        // pos (0) + nrm(1)
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,pos)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,nrm)};
        // model matrix columns (2..5) from binding 1
        attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*0};
        attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*1};
        attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*2};
        attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, model) + sizeof(glm::vec4)*3};
        // color (6)
        attrs[6] = {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)};

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
        vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();

        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Viewport/scissor (fixed; no resize handling for brevity)
        VkViewport vp{0,0,(float)swapExtent.width,(float)swapExtent.height,0.0f,1.0f};
        VkRect2D sc{{0,0}, swapExtent};
        VkPipelineViewportStateCreateInfo vpci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vpci.viewportCount=1; vpci.pViewports=&vp; vpci.scissorCount=1; vpci.pScissors=&sc;

        // Rasterization
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        // Multisample
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Color blend
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount=1; cb.pAttachments=&cba;

        // Shaders stages
        VkPipelineShaderStageCreateInfo sVS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        sVS.stage = VK_SHADER_STAGE_VERTEX_BIT; sVS.module=vs; sVS.pName="main";
        VkPipelineShaderStageCreateInfo sFS{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        sFS.stage = VK_SHADER_STAGE_FRAGMENT_BIT; sFS.module=fs; sFS.pName="main";
        VkPipelineShaderStageCreateInfo stages[2]={sVS,sFS};

        // Pipeline
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount=2; gp.pStages=stages;
        gp.pVertexInputState=&vi;
        gp.pInputAssemblyState=&ia;
        gp.pViewportState=&vpci;
        gp.pRasterizationState=&rs;
        gp.pMultisampleState=&ms;
        gp.pColorBlendState=&cb;
        gp.layout=pipeLayout;
        gp.renderPass=rp; gp.subpass=0;
        VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe));
        vkDestroyShaderModule(dev, vs, nullptr);
        vkDestroyShaderModule(dev, fs, nullptr);

        // --- Framebuffers
        for (auto view : swapViews) {
            VkImageView atts[] = { view };
            VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbi.renderPass = rp;
            fbi.attachmentCount=1; fbi.pAttachments=atts;
            fbi.width=swapExtent.width; fbi.height=swapExtent.height; fbi.layers=1;
            VkFramebuffer fb{};
            VK_CHECK(vkCreateFramebuffer(dev, &fbi, nullptr, &fb));
            framebuffers.push_back(fb);
        }

        // --- Command pool + buffers
        VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpci.queueFamilyIndex = qFamGfx;
        VK_CHECK(vkCreateCommandPool(dev, &cpci, nullptr, &cmdPool));

        cmds.resize(framebuffers.size());
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = cmdPool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = (uint32_t)cmds.size();
        VK_CHECK(vkAllocateCommandBuffers(dev, &cai, cmds.data()));

        // --- Sync
        VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(dev, &sci2, nullptr, &semImage));
        VK_CHECK(vkCreateSemaphore(dev, &sci2, nullptr, &semRender));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT; // signaled so first frame doesn't wait forever
        VK_CHECK(vkCreateFence(dev, &fci, nullptr, &inFlight));

        // --- Geometry buffers
        std::vector<Vertex> sphereV;
        std::vector<uint32_t> sphereI;
        make_uv_sphere(0.25f, 16, 24, sphereV, sphereI);
        indexCount = (uint32_t)sphereI.size();

        VkDeviceSize vBytes = sizeof(Vertex) * sphereV.size();
        VkDeviceSize iBytes = sizeof(uint32_t) * sphereI.size();
        createBuffer(phys, dev, vBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vbo);
        createBuffer(phys, dev, iBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ibo);

        void* ptr=nullptr;
        VK_CHECK(vkMapMemory(dev, vbo.mem, 0, vBytes, 0, &ptr));
        std::memcpy(ptr, sphereV.data(), (size_t)vBytes);
        vkUnmapMemory(dev, vbo.mem);
        VK_CHECK(vkMapMemory(dev, ibo.mem, 0, iBytes, 0, &ptr));
        std::memcpy(ptr, sphereI.data(), (size_t)iBytes);
        vkUnmapMemory(dev, ibo.mem);

        // --- Instance buffer (model + color per instance)
        instanceCount = NUM_SPHERES;
        std::vector<Instance> instances(instanceCount);
        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> U(-1.0f, 1.0f), C(0.2f, 1.0f);
        for (uint32_t i=0;i<instanceCount;++i){
            glm::vec3 p(U(rng)*6.0f, U(rng)*3.0f, U(rng)*6.0f);
            float s = 0.3f + 0.7f * (C(rng)*0.5f);
            glm::mat4 M = glm::translate(glm::mat4(1.0f), p) * glm::scale(glm::mat4(1.0f), glm::vec3(s));
            instances[i].model = M;
            instances[i].color = glm::vec3(C(rng), C(rng), C(rng));
            instances[i].pad   = 0.0f;
        }
        VkDeviceSize instBytes = sizeof(Instance) * instances.size();
        createBuffer(phys, dev, instBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instanceBuf);
        VK_CHECK(vkMapMemory(dev, instanceBuf.mem, 0, instBytes, 0, &ptr));
        std::memcpy(ptr, instances.data(), (size_t)instBytes);
        vkUnmapMemory(dev, instanceBuf.mem);

        // --- UBO per swap image
        ubos.resize(swapImages.size());
        for (auto& u : ubos) {
            createBuffer(phys, dev, sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, u);
        }

        // --- Descriptors
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)ubos.size()};
        VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpci.maxSets = (uint32_t)ubos.size();
        dpci.poolSizeCount=1; dpci.pPoolSizes=&poolSize;
        VK_CHECK(vkCreateDescriptorPool(dev, &dpci, nullptr, &descPool));

        std::vector<VkDescriptorSetLayout> layouts(ubos.size(), dsl);
        VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsai.descriptorPool = descPool;
        dsai.descriptorSetCount = (uint32_t)layouts.size();
        dsai.pSetLayouts = layouts.data();
        descSets.resize(layouts.size());
        VK_CHECK(vkAllocateDescriptorSets(dev, &dsai, descSets.data()));

        for (size_t i=0;i<ubos.size();++i){
            VkDescriptorBufferInfo bi{ubos[i].buf, 0, sizeof(UBO)};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descSets[i];
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.descriptorCount = 1;
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
        }

        // --- Record command buffers (static)
        for (size_t i=0;i<cmds.size();++i){
            VkCommandBufferBeginInfo bi2{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cmds[i], &bi2));

            VkClearValue clear{{{0.10f,0.10f,0.25f,1.0f}}};
            VkRenderPassBeginInfo rpb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpb.renderPass = rp;
            rpb.framebuffer = framebuffers[i];
            rpb.renderArea.offset = {0,0};
            rpb.renderArea.extent = swapExtent;
            rpb.clearValueCount = 1; rpb.pClearValues = &clear;

            vkCmdBeginRenderPass(cmds[i], &rpb, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmds[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

            VkBuffer vbs[2] = { vbo.buf, instanceBuf.buf };
            VkDeviceSize offs[2] = { 0, 0 };
            vkCmdBindVertexBuffers(cmds[i], 0, 2, vbs, offs);
            vkCmdBindIndexBuffer(cmds[i], ibo.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(cmds[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout,
                                    0, 1, &descSets[i], 0, nullptr);

            vkCmdDrawIndexed(cmds[i], indexCount, instanceCount, 0, 0, 0);
            vkCmdEndRenderPass(cmds[i]);
            VK_CHECK(vkEndCommandBuffer(cmds[i]));
        }

        tPrev = std::chrono::steady_clock::now();
    }

    void updateUBO(size_t imageIndex, float dt) {
        (void)dt;
        UBO u{};
        glm::mat4 V = glm::lookAt(gCamPos, gCamPos + camForward(), glm::vec3(0,1,0));
        glm::mat4 P = glm::perspective(glm::radians(60.0f), float(swapExtent.width)/float(swapExtent.height), 0.1f, 200.0f);
        // GLM is OpenGL-style (clip space -Z). Vulkan also uses -Z with GLM default; but P[1][1] flips Y if GLM_FORCE_DEPTH_ZERO_TO_ONE is set.
        u.view = V;
        u.proj = P;

        void* ptr=nullptr;
        VK_CHECK(vkMapMemory(dev, ubos[imageIndex].mem, 0, sizeof(UBO), 0, &ptr));
        std::memcpy(ptr, &u, sizeof(UBO));
        vkUnmapMemory(dev, ubos[imageIndex].mem);
    }

    void drawFrame() {
        VK_CHECK(vkWaitForFences(dev, 1, &inFlight, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(dev, 1, &inFlight));

        uint32_t imgIndex=0;
        VK_CHECK(vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, semImage, VK_NULL_HANDLE, &imgIndex));

        // Update UBO (camera)
        auto tNow = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        updateCamera(dt);
        updateUBO(imgIndex, dt);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &semImage;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmds[imgIndex];
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &semRender;
        VK_CHECK(vkQueueSubmit(qGfx, 1, &si, inFlight));

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &semRender;
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imgIndex;
        (void)vkQueuePresentKHR(qPresent, &pi);
    }

    void cleanup() {
        vkDeviceWaitIdle(dev);
        vbo.destroy(); ibo.destroy(); instanceBuf.destroy();
        for (auto& b : ubos) b.destroy();
        vkDestroyDescriptorPool(dev, descPool, nullptr);
        vkDestroyDescriptorSetLayout(dev, dsl, nullptr);
        vkDestroyPipeline(dev, pipe, nullptr);
        vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
        for (auto fb : framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
        vkDestroyRenderPass(dev, rp, nullptr);
        for (auto v : swapViews) vkDestroyImageView(dev, v, nullptr);
        vkDestroySwapchainKHR(dev, swapchain, nullptr);
        vkDestroyFence(dev, inFlight, nullptr);
        vkDestroySemaphore(dev, semRender, nullptr);
        vkDestroySemaphore(dev, semImage, nullptr);
        vkDestroyCommandPool(dev, cmdPool, nullptr);
        vkDestroyDevice(dev, nullptr);
        vkDestroySurfaceKHR(inst, surface, nullptr);
        vkDestroyInstance(inst, nullptr);
    }
};

int main() {
    if (!glfwInit()) return 1;
    if (!glfwVulkanSupported()) { std::fprintf(stderr,"Vulkan not supported.\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    gWindow = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Sphere Cloud", nullptr, nullptr);
    glfwSetKeyCallback(gWindow, keyCb);
    glfwSetMouseButtonCallback(gWindow, mouseBtnCb);
    glfwSetCursorPosCallback(gWindow, cursorCb);

    VkApp app;
    try {
        app.init(gWindow);
        while (!glfwWindowShouldClose(gWindow)) {
            glfwPollEvents();
            app.drawFrame();
        }
        app.cleanup();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        app.cleanup();
        glfwTerminate();
        return 2;
    }

    glfwTerminate();
    return 0;
}

