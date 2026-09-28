#include "renderer.hpp"

#include "shader_spv.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kFramesInFlight = 2;
constexpr std::array<float, 4> kSkyBlue{0.53f, 0.81f, 0.92f, 1.0f};

struct MeshRange {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};

struct HostBuffer {
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
    void* mapped = nullptr;
    bool coherent = false;
};

struct Frame {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffers commandBuffers{nullptr};
    vk::raii::Fence fence{nullptr};
    vk::raii::Semaphore imageAvailable{nullptr};
    HostBuffer uniform;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT type,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::cerr << "[vulkan " << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(severity)) << " "
              << vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(type)) << "] " << data->pMessage << '\n';
    return VK_FALSE;
}

vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback};
}

bool hasValidation(const vk::raii::Context& context) {
    for (const vk::LayerProperties& layer : context.enumerateInstanceLayerProperties()) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

uint32_t findMemoryType(vk::PhysicalDevice device, uint32_t typeBits, vk::MemoryPropertyFlags required) {
    const vk::PhysicalDeviceMemoryProperties properties = device.getMemoryProperties();
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("no matching Vulkan memory type");
}

void flushBuffer(vk::raii::Device& device, const HostBuffer& buffer) {
    if (buffer.coherent || buffer.mapped == nullptr) {
        return;
    }
    device.flushMappedMemoryRanges(vk::MappedMemoryRange{*buffer.memory, 0, vk::WholeSize});
}

HostBuffer createBuffer(vk::raii::Device& device, vk::PhysicalDevice physical, const void* bytes, vk::DeviceSize size,
                        vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    HostBuffer host;
    host.buffer = vk::raii::Buffer(device, vk::BufferCreateInfo{{}, size, usage, vk::SharingMode::eExclusive});
    const vk::MemoryRequirements requirements = host.buffer.getMemoryRequirements();
    const uint32_t typeIndex = findMemoryType(physical, requirements.memoryTypeBits, properties);
    const vk::PhysicalDeviceMemoryProperties memoryProperties = physical.getMemoryProperties();
    host.coherent = static_cast<bool>(memoryProperties.memoryTypes[typeIndex].propertyFlags &
                                      vk::MemoryPropertyFlagBits::eHostCoherent);
    host.memory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo{requirements.size, typeIndex});
    host.buffer.bindMemory(*host.memory, 0);
    if (static_cast<bool>(properties & vk::MemoryPropertyFlagBits::eHostVisible)) {
        host.mapped = host.memory.mapMemory(0, requirements.size);
        if (bytes != nullptr && size > 0) {
            std::memcpy(host.mapped, bytes, static_cast<std::size_t>(size));
            flushBuffer(device, host);
        }
    }
    return host;
}

vk::raii::ImageView makeImageView(vk::raii::Device& device, vk::Image image, vk::Format format,
                                  vk::ImageAspectFlags aspect) {
    vk::ImageViewCreateInfo info{};
    info.image = image;
    info.viewType = vk::ImageViewType::e2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.layerCount = 1;
    return vk::raii::ImageView(device, info);
}

std::pair<std::vector<Vertex>, std::vector<MeshRange>> packVertices(const std::vector<SceneObject>& objects) {
    std::vector<Vertex> vertices;
    std::vector<MeshRange> ranges;
    for (const SceneObject& object : objects) {
        const auto first = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), object.vertices.begin(), object.vertices.end());
        ranges.push_back(MeshRange{first, static_cast<uint32_t>(object.vertices.size())});
    }
    return {std::move(vertices), std::move(ranges)};
}

glm::mat4 viewProjection(float aspect) {
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(glm::vec3(7.5f, 5.5f, 8.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

bool isOutOfDate(const vk::SystemError& error) {
    return error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR);
}

}  // namespace

struct Renderer::Impl {
    GLFWwindow* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance{nullptr};
    std::optional<vk::raii::DebugUtilsMessengerEXT> debug;
    vk::raii::SurfaceKHR surface{nullptr};
    vk::raii::PhysicalDevice physicalDevice{nullptr};
    vk::raii::Device device{nullptr};
    vk::raii::Queue queue{nullptr};
    vk::raii::DescriptorSetLayout descriptorLayout{nullptr};
    vk::raii::DescriptorPool descriptorPool{nullptr};
    vk::raii::DescriptorSets descriptorSets{nullptr};
    vk::raii::PipelineLayout pipelineLayout{nullptr};
    HostBuffer vertices;
    std::vector<Frame> frames;
    vk::raii::SwapchainKHR swapchain{nullptr};
    std::vector<vk::raii::ImageView> imageViews;
    vk::raii::DeviceMemory depthMemory{nullptr};
    vk::raii::Image depthImage{nullptr};
    vk::raii::ImageView depthView{nullptr};
    vk::raii::RenderPass renderPass{nullptr};
    vk::raii::Pipeline pipeline{nullptr};
    std::vector<vk::raii::Framebuffer> framebuffers;
    std::vector<vk::raii::Semaphore> renderFinished;
    vk::Format colorFormat = vk::Format::eUndefined;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::Extent2D extent{};
    std::vector<MeshRange> ranges;
    std::size_t frameIndex = 0;
    bool resized = false;

    explicit Impl(GLFWwindow* glfwWindow) : window(glfwWindow) {}

    void destroySwapchain();
    void rebuildSwapchain();
    void createDepth();
    void createRenderPass();
    void createPipeline();
    void writeUniform(std::size_t index);
    void record(std::size_t index, uint32_t imageIndex, const std::vector<glm::vec3>& translations);
};

Renderer::Renderer(GLFWwindow* window, const std::vector<SceneObject>& objects) : impl_(std::make_unique<Impl>(window)) {
    if (objects.empty()) {
        throw std::runtime_error("the stage has no meshes to draw");
    }
    Impl& self = *impl_;
    const bool validation = hasValidation(self.context);

    uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (glfwExtensions == nullptr) {
        throw std::runtime_error("GLFW did not return Vulkan instance extensions");
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + extensionCount);
    if (validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    vk::ApplicationInfo appInfo{"vulkan-openusd-rendering", vk::makeApiVersion(0, 0, 1, 0), "vulkan-openusd-rendering",
                                vk::makeApiVersion(0, 0, 1, 0), vk::ApiVersion12};
    vk::InstanceCreateInfo instanceInfo{};
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    auto debugInfo = debugCreateInfo();
    if (validation) {
        instanceInfo.enabledLayerCount = 1;
        instanceInfo.ppEnabledLayerNames = &validationLayer;
        instanceInfo.pNext = &debugInfo;
    }
    self.instance = vk::raii::Instance(self.context, instanceInfo);
    if (validation) {
        self.debug.emplace(self.instance, debugInfo);
    }

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(static_cast<VkInstance>(*self.instance), window, nullptr, &rawSurface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the window surface");
    }
    self.surface = vk::raii::SurfaceKHR(self.instance, rawSurface);

    std::optional<std::pair<vk::raii::PhysicalDevice, uint32_t>> fallback;
    for (vk::raii::PhysicalDevice& device : self.instance.enumeratePhysicalDevices()) {
        const auto families = device.getQueueFamilyProperties();
        std::optional<uint32_t> family;
        for (uint32_t index = 0; index < families.size(); ++index) {
            const bool graphics = static_cast<bool>(families[index].queueFlags & vk::QueueFlagBits::eGraphics);
            if (graphics && device.getSurfaceSupportKHR(index, *self.surface)) {
                family = index;
                break;
            }
        }
        if (!family) {
            continue;
        }
        if (device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            fallback = std::make_pair(std::move(device), *family);
            break;
        }
        if (!fallback) {
            fallback = std::make_pair(std::move(device), *family);
        }
    }
    if (!fallback) {
        throw std::runtime_error("no Vulkan device can present to the window");
    }
    self.physicalDevice = std::move(fallback->first);
    const uint32_t queueFamily = fallback->second;

    const float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo{{}, queueFamily, 1, &priority};
    const char* deviceExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    vk::DeviceCreateInfo deviceInfo{};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = &deviceExtension;
    self.device = vk::raii::Device(self.physicalDevice, deviceInfo);
    self.queue = self.device.getQueue(queueFamily, 0);

    for (vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint}) {
        const vk::FormatProperties properties = self.physicalDevice.getFormatProperties(format);
        if (static_cast<bool>(properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)) {
            self.depthFormat = format;
            break;
        }
    }
    if (self.depthFormat == vk::Format::eUndefined) {
        throw std::runtime_error("no depth format supports depth attachments");
    }

    vk::DescriptorSetLayoutBinding binding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex};
    self.descriptorLayout = vk::raii::DescriptorSetLayout(
        self.device, vk::DescriptorSetLayoutCreateInfo{{}, 1, &binding});
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer, static_cast<uint32_t>(kFramesInFlight)};
    self.descriptorPool = vk::raii::DescriptorPool(
        self.device, vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                                   static_cast<uint32_t>(kFramesInFlight), 1, &poolSize});
    const std::array<vk::DescriptorSetLayout, kFramesInFlight> layouts{*self.descriptorLayout, *self.descriptorLayout};
    self.descriptorSets = vk::raii::DescriptorSets(
        self.device, vk::DescriptorSetAllocateInfo{*self.descriptorPool, static_cast<uint32_t>(layouts.size()), layouts.data()});

    vk::PushConstantRange pushRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)};
    const vk::DescriptorSetLayout layout = *self.descriptorLayout;
    self.pipelineLayout = vk::raii::PipelineLayout(
        self.device, vk::PipelineLayoutCreateInfo{{}, 1, &layout, 1, &pushRange});

    const auto packed = packVertices(objects);
    const vk::MemoryPropertyFlags hostFlags =
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    vk::MemoryPropertyFlags vertexFlags = hostFlags;
    try {
        self.vertices = createBuffer(self.device, *self.physicalDevice, packed.first.data(),
                                     packed.first.size() * sizeof(Vertex), vk::BufferUsageFlagBits::eVertexBuffer, hostFlags);
    } catch (const std::runtime_error&) {
        vertexFlags = vk::MemoryPropertyFlagBits::eHostVisible;
        self.vertices = createBuffer(self.device, *self.physicalDevice, packed.first.data(),
                                     packed.first.size() * sizeof(Vertex), vk::BufferUsageFlagBits::eVertexBuffer, vertexFlags);
    }
    self.ranges = packed.second;

    const glm::mat4 identity(1.0f);
    self.frames.reserve(kFramesInFlight);
    for (std::size_t index = 0; index < kFramesInFlight; ++index) {
        Frame frame;
        try {
            frame.uniform = createBuffer(self.device, *self.physicalDevice, &identity, sizeof(identity),
                                         vk::BufferUsageFlagBits::eUniformBuffer, hostFlags);
        } catch (const std::runtime_error&) {
            frame.uniform = createBuffer(self.device, *self.physicalDevice, &identity, sizeof(identity),
                                         vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
        }
        vk::DescriptorBufferInfo bufferInfo{*frame.uniform.buffer, 0, sizeof(glm::mat4)};
        vk::WriteDescriptorSet write{};
        write.dstSet = *self.descriptorSets[index];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &bufferInfo;
        self.device.updateDescriptorSets(write, nullptr);
        frame.commandPool = vk::raii::CommandPool(
            self.device, vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamily});
        frame.commandBuffers = vk::raii::CommandBuffers(
            self.device, vk::CommandBufferAllocateInfo{*frame.commandPool, vk::CommandBufferLevel::ePrimary, 1});
        frame.fence = vk::raii::Fence(self.device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
        frame.imageAvailable = vk::raii::Semaphore(self.device, vk::SemaphoreCreateInfo{});
        self.frames.push_back(std::move(frame));
    }

    self.rebuildSwapchain();
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* glfwWindow, int, int) {
        static_cast<Renderer*>(glfwGetWindowUserPointer(glfwWindow))->noteResize();
    });
}

Renderer::~Renderer() {
    if (impl_ && *impl_->device) {
        impl_->device.waitIdle();
    }
}

void Renderer::noteResize() {
    impl_->resized = true;
}

void Renderer::draw(const std::vector<glm::vec3>& translations) {
    if (translations.size() != impl_->ranges.size()) {
        throw std::runtime_error("translation count does not match the meshes");
    }
    if (impl_->resized || !*impl_->swapchain) {
        impl_->rebuildSwapchain();
        impl_->resized = false;
    }
    if (impl_->extent.width == 0 || impl_->extent.height == 0 || !*impl_->swapchain) {
        return;
    }

    const std::size_t frameIndex = impl_->frameIndex;
    Frame& frame = impl_->frames[frameIndex];
    if (impl_->device.waitForFences(*frame.fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for the frame fence");
    }

    uint32_t imageIndex = 0;
    try {
        const auto acquired = impl_->swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.imageAvailable,
                                                                nullptr);
        if (acquired.first == vk::Result::eSuboptimalKHR) {
            impl_->resized = true;
        }
        imageIndex = acquired.second;
    } catch (const vk::SystemError& error) {
        if (isOutOfDate(error)) {
            impl_->resized = true;
            return;
        }
        throw;
    }

    impl_->device.resetFences(*frame.fence);
    impl_->writeUniform(frameIndex);
    impl_->record(frameIndex, imageIndex, translations);

    const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const vk::Semaphore waitSemaphore = *frame.imageAvailable;
    const vk::CommandBuffer commandBuffer = *frame.commandBuffers[0];
    const vk::Semaphore signal = *impl_->renderFinished[imageIndex];
    vk::SubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &waitSemaphore;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal;
    impl_->queue.submit(submit, *frame.fence);

    const vk::SwapchainKHR swapchain = *impl_->swapchain;
    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &signal;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &imageIndex;
    try {
        if (impl_->queue.presentKHR(present) == vk::Result::eSuboptimalKHR) {
            impl_->resized = true;
        }
    } catch (const vk::SystemError& error) {
        if (isOutOfDate(error)) {
            impl_->resized = true;
        } else {
            throw;
        }
    }
    impl_->frameIndex = (frameIndex + 1) % kFramesInFlight;
}

void Renderer::Impl::destroySwapchain() {
    renderFinished.clear();
    framebuffers.clear();
    imageViews.clear();
    depthView = vk::raii::ImageView{nullptr};
    depthImage = vk::raii::Image{nullptr};
    depthMemory = vk::raii::DeviceMemory{nullptr};
    pipeline = vk::raii::Pipeline{nullptr};
    renderPass = vk::raii::RenderPass{nullptr};
    swapchain = vk::raii::SwapchainKHR{nullptr};
}

void Renderer::Impl::rebuildSwapchain() {
    if (*swapchain || *renderPass) {
        device.waitIdle();
    }
    destroySwapchain();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width == 0 || height == 0) {
        extent = vk::Extent2D{0, 0};
        return;
    }

    const vk::SurfaceCapabilitiesKHR capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    const std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(*surface);
    if (formats.empty()) {
        throw std::runtime_error("the surface has no formats");
    }
    vk::SurfaceFormatKHR surfaceFormat = formats.front();
    for (const vk::SurfaceFormatKHR& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surfaceFormat = format;
            break;
        }
    }
    colorFormat = surfaceFormat.format;
    if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    } else {
        extent = capabilities.currentExtent;
    }
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const vk::CompositeAlphaFlagBitsKHR alphaCandidates[] = {
        vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied};
    vk::CompositeAlphaFlagBitsKHR alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    for (vk::CompositeAlphaFlagBitsKHR candidate : alphaCandidates) {
        if (static_cast<bool>(capabilities.supportedCompositeAlpha & candidate)) {
            alpha = candidate;
            break;
        }
    }
    const vk::SurfaceTransformFlagBitsKHR transform =
        static_cast<bool>(capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
            ? vk::SurfaceTransformFlagBitsKHR::eIdentity
            : capabilities.currentTransform;

    vk::SwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.surface = *surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = colorFormat;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapchainInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapchainInfo.preTransform = transform;
    swapchainInfo.compositeAlpha = alpha;
    swapchainInfo.presentMode = vk::PresentModeKHR::eFifo;
    swapchainInfo.clipped = VK_TRUE;
    swapchain = vk::raii::SwapchainKHR(device, swapchainInfo);

    for (vk::Image image : swapchain.getImages()) {
        imageViews.push_back(makeImageView(device, image, colorFormat, vk::ImageAspectFlagBits::eColor));
    }
    createDepth();
    createRenderPass();
    createPipeline();
    for (const vk::raii::ImageView& view : imageViews) {
        const std::array<vk::ImageView, 2> attachments{*view, *depthView};
        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = *renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        framebuffers.emplace_back(device, framebufferInfo);
    }
    renderFinished.reserve(imageViews.size());
    for (std::size_t index = 0; index < imageViews.size(); ++index) {
        renderFinished.emplace_back(device, vk::SemaphoreCreateInfo{});
    }
}

void Renderer::Impl::createDepth() {
    vk::ImageCreateInfo info{};
    info.imageType = vk::ImageType::e2D;
    info.format = depthFormat;
    info.extent = vk::Extent3D{extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.initialLayout = vk::ImageLayout::eUndefined;
    depthImage = vk::raii::Image(device, info);
    const vk::MemoryRequirements requirements = depthImage.getMemoryRequirements();
    const uint32_t typeIndex =
        findMemoryType(*physicalDevice, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthMemory = vk::raii::DeviceMemory(device, vk::MemoryAllocateInfo{requirements.size, typeIndex});
    depthImage.bindMemory(*depthMemory, 0);
    depthView = makeImageView(device, *depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

void Renderer::Impl::createRenderPass() {
    vk::AttachmentDescription color{};
    color.format = colorFormat;
    color.samples = vk::SampleCountFlagBits::e1;
    color.loadOp = vk::AttachmentLoadOp::eClear;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout = vk::ImageLayout::eUndefined;
    color.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = vk::SampleCountFlagBits::e1;
    depth.loadOp = vk::AttachmentLoadOp::eClear;
    depth.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depth.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth.initialLayout = vk::ImageLayout::eUndefined;
    depth.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    const vk::AttachmentDescription attachments[] = {color, depth};
    const vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
    const vk::AttachmentReference depthRef{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};
    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo info{};
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    renderPass = vk::raii::RenderPass(device, info);
}

void Renderer::Impl::createPipeline() {
    vk::ShaderModuleCreateInfo vertInfo{{}, sizeof(kVertexSpirv), kVertexSpirv};
    vk::ShaderModuleCreateInfo fragInfo{{}, sizeof(kFragmentSpirv), kFragmentSpirv};
    vk::raii::ShaderModule vertModule(device, vertInfo);
    vk::raii::ShaderModule fragModule(device, fragInfo);
    const vk::PipelineShaderStageCreateInfo stages[] = {
        {{}, vk::ShaderStageFlagBits::eVertex, *vertModule, "vertexMain"},
        {{}, vk::ShaderStageFlagBits::eFragment, *fragModule, "fragmentMain"},
    };

    const vk::VertexInputBindingDescription binding{0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    const std::array<vk::VertexInputAttributeDescription, 3> attributes{{
        {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
        {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)},
        {2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)},
    }};
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
    vk::PipelineViewportStateCreateInfo viewport{{}, 1, nullptr, 1, nullptr};
    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.polygonMode = vk::PolygonMode::eFill;
    raster.cullMode = vk::CullModeFlagBits::eBack;
    raster.frontFace = vk::FrontFace::eCounterClockwise;
    raster.lineWidth = 1.0f;
    vk::PipelineMultisampleStateCreateInfo multisample{{}, vk::SampleCountFlagBits::e1};
    vk::PipelineDepthStencilStateCreateInfo depth{};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = vk::CompareOp::eLess;
    vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic{{}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()};

    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = *pipelineLayout;
    info.renderPass = *renderPass;
    info.subpass = 0;
    pipeline = device.createGraphicsPipeline(nullptr, info);

    (void)vertModule;
    (void)fragModule;
}

void Renderer::Impl::writeUniform(std::size_t index) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    const glm::mat4 matrix = viewProjection(aspect);
    std::memcpy(frames[index].uniform.mapped, &matrix, sizeof(matrix));
    flushBuffer(device, frames[index].uniform);
}

void Renderer::Impl::record(std::size_t index, uint32_t imageIndex, const std::vector<glm::vec3>& translations) {
    vk::raii::CommandBuffer& commandBuffer = frames[index].commandBuffers[0];
    commandBuffer.reset();
    commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    const std::array<vk::ClearValue, 2> clears{vk::ClearColorValue{kSkyBlue},
                                               vk::ClearDepthStencilValue{1.0f, 0}};
    vk::RenderPassBeginInfo begin{};
    begin.renderPass = *renderPass;
    begin.framebuffer = *framebuffers[imageIndex];
    begin.renderArea.extent = extent;
    begin.clearValueCount = static_cast<uint32_t>(clears.size());
    begin.pClearValues = clears.data();
    commandBuffer.beginRenderPass(begin, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSets[index], nullptr);
    const vk::DeviceSize offset = 0;
    commandBuffer.bindVertexBuffers(0, *vertices.buffer, offset);
    const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    commandBuffer.setViewport(0, viewport);
    commandBuffer.setScissor(0, vk::Rect2D{{0, 0}, extent});

    for (std::size_t mesh = 0; mesh < ranges.size(); ++mesh) {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), translations[mesh]);
        commandBuffer.pushConstants<glm::mat4>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, model);
        commandBuffer.draw(ranges[mesh].vertexCount, 1, ranges[mesh].firstVertex, 0);
    }
    commandBuffer.endRenderPass();
    commandBuffer.end();
}
