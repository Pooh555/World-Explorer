#include "vulkan_device.hpp"
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace world_explorer {

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_device::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* context) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<vulkan_device*>(context)->validation_errors_.fetch_add(1);
    std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    return VK_FALSE;
}

vulkan_device::vulkan_device(SDL_Window* window, const renderer_options& options,
                             renderer_capabilities& capabilities,
                             std::atomic<std::uint32_t>& validation_errors)
    : window_(window), options_(options), capabilities_(capabilities),
      validation_errors_(validation_errors) {}

vulkan_device::~vulkan_device() {
    if (device_)
        vkDeviceWaitIdle(device_);
    if (timeline_)
        vkDestroySemaphore(device_, timeline_, nullptr);
    if (device_)
        vkDestroyDevice(device_, nullptr);
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_messenger_) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(instance_, debug_messenger_, nullptr);
    }
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}

void vulkan_device::initialize() {
    if (!window_ || !options_.max_splats || options_.max_splats > 10000000)
        throw std::runtime_error("invalid renderer window or splat budget (maximum 10 million)");

    std::uint32_t loader_version = VK_API_VERSION_1_0;
    detail::check(vkEnumerateInstanceVersion(&loader_version), "query Vulkan loader version");
    if (loader_version < VK_API_VERSION_1_3)
        throw std::runtime_error(
            "Vulkan 1.3 with synchronization2 and timeline semaphores is required");

    capabilities_.instance_version = std::min(loader_version, VK_API_VERSION_1_4);
    std::uint32_t extension_count = 0;
    detail::check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr),
                  "enumerate instance extensions");
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    detail::check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                                         available_extensions.data()),
                  "enumerate instance extensions");

    const auto has_extension = [&](const char* name) {
        return std::ranges::any_of(available_extensions, [&](const auto& item) {
            return std::strcmp(item.extensionName, name) == 0;
        });
    };

    std::uint32_t sdl_extension_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (!sdl_extensions)
        throw std::runtime_error(SDL_GetError());

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_extension_count);
    VkInstanceCreateFlags instance_flags = 0;
    if (has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char*> layers;
    if (options_.validation) {
        std::uint32_t layer_count = 0;
        detail::check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr),
                      "enumerate layers");
        std::vector<VkLayerProperties> available_layers(layer_count);
        detail::check(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()),
                      "enumerate layers");
        if (std::ranges::any_of(available_layers,
                                [](const auto& item) {
                                    return std::strcmp(item.layerName,
                                                       "VK_LAYER_KHRONOS_validation") == 0;
                                }) &&
            has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            capabilities_.validation_enabled = true;
        }
    }

    auto application = detail::structure<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "World Explorer";
    application.apiVersion = capabilities_.instance_version;

    auto debug = detail::structure<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType =
        VkDebugUtilsMessageTypeFlagsEXT{VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT} |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_callback;
    debug.pUserData = this;

    auto create = detail::structure<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    create.flags = instance_flags;
    create.pApplicationInfo = &application;
    create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    create.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create.ppEnabledLayerNames = layers.data();

    const VkValidationFeatureEnableEXT synchronization_validation =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    auto validation_features =
        detail::structure<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    if (capabilities_.validation_enabled) {
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        validation_features.enabledValidationFeatureCount = 1;
        validation_features.pEnabledValidationFeatures = &synchronization_validation;
        validation_features.pNext = &debug;
        create.pNext = &validation_features;
    }

    detail::check(vkCreateInstance(&create, nullptr, &instance_), "create Vulkan instance");
    if (capabilities_.validation_enabled) {
        const auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        detail::check(make(instance_, &debug, nullptr, &debug_messenger_),
                      "create validation messenger");
    }

    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
        throw std::runtime_error(SDL_GetError());

    select_physical_device();
    create_logical_device();
}

void vulkan_device::select_physical_device() {
    constexpr auto COMPOSITION_SHARED_BYTES = std::size_t{256} * 48 + 10 * sizeof(std::uint32_t);
    std::uint32_t count = 0;
    detail::check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    detail::check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate GPUs");

    for (auto candidate : devices) {
        VkPhysicalDeviceProperties candidate_properties{};
        vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
        if (candidate_properties.apiVersion < VK_API_VERSION_1_3 ||
            candidate_properties.limits.maxComputeWorkGroupInvocations < 256 ||
            candidate_properties.limits.maxComputeWorkGroupSize[0] < 256 ||
            candidate_properties.limits.maxStorageBufferRange <
                options_.max_splats * sizeof(detail::gpu_splat) ||
            candidate_properties.limits.maxComputeWorkGroupSize[1] < 16 ||
            candidate_properties.limits.maxPushConstantsSize < sizeof(detail::push_parameters) ||
            candidate_properties.limits.maxComputeSharedMemorySize < COMPOSITION_SHARED_BYTES ||
            candidate_properties.limits.maxPerStageDescriptorStorageBuffers < 10 ||
            candidate_properties.limits.maxDescriptorSetStorageBuffers < 10 ||
            candidate_properties.limits.maxComputeWorkGroupCount[0] <
                (options_.max_splats + 255) / 256)
            continue;

        auto features13 = detail::structure<VkPhysicalDeviceVulkan13Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        auto features12 = detail::structure<VkPhysicalDeviceVulkan12Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        features12.pNext = &features13;
        auto features = detail::structure<VkPhysicalDeviceFeatures2>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features12.timelineSemaphore || !features13.synchronization2)
            continue;

        std::uint32_t count_extensions = 0;
        detail::check(
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count_extensions, nullptr),
            "query device extensions");
        std::vector<VkExtensionProperties> extensions(count_extensions);
        detail::check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count_extensions,
                                                           extensions.data()),
                      "query device extensions");
        if (!std::ranges::any_of(extensions, [](const auto& item) {
                return std::strcmp(item.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            }))
            continue;

        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (std::uint32_t index = 0; index < family_count; ++index) {
            VkBool32 present = VK_FALSE;
            detail::check(
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface_, &present),
                "query presentation support");
            if (present && (families[index].queueFlags &
                            (VkQueueFlags{VK_QUEUE_GRAPHICS_BIT} | VK_QUEUE_COMPUTE_BIT)) ==
                               (VkQueueFlags{VK_QUEUE_GRAPHICS_BIT} | VK_QUEUE_COMPUTE_BIT)) {
                physical_ = candidate;
                queue_family_ = index;
                properties_ = candidate_properties;
                capabilities_.device_name = properties_.deviceName;
                capabilities_.device_version = properties_.apiVersion;
                capabilities_.max_splats = options_.max_splats;
                capabilities_.portability_subset =
                    std::ranges::any_of(extensions, [](const auto& item) {
                        return std::strcmp(item.extensionName, "VK_KHR_portability_subset") == 0;
                    });
                vkGetPhysicalDeviceMemoryProperties(physical_, &memory_properties_);
                return;
            }
        }
    }

    throw std::runtime_error("no GPU meets the Vulkan 1.3 graphics/compute/present, timeline, "
                             "synchronization2 and configured storage limits");
}

void vulkan_device::create_logical_device() {
    float priority = 1;
    auto queue_info =
        detail::structure<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    auto features13 = detail::structure<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    features13.synchronization2 = VK_TRUE;

    auto features12 = detail::structure<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (capabilities_.portability_subset)
        extensions.push_back("VK_KHR_portability_subset");

    auto info = detail::structure<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    info.pNext = &features12;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    detail::check(vkCreateDevice(physical_, &info, nullptr, &device_), "create Vulkan device");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    auto type =
        detail::structure<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    auto semaphore =
        detail::structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphore.pNext = &type;
    detail::check(vkCreateSemaphore(device_, &semaphore, nullptr, &timeline_),
                  "create retirement timeline");
}

// Vulkan's distinct integer typedefs retain their native argument order at this boundary.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
detail::gpu_buffer vulkan_device::allocate(VkDeviceSize bytes, VkBufferUsageFlags usage,
                                           bool host_visible, std::size_t budget) {
    detail::gpu_buffer output;
    output.device = device_;
    output.bytes = std::max<VkDeviceSize>(bytes, 16);
    auto info = detail::structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = output.bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    detail::check(vkCreateBuffer(device_, &info, nullptr, &output.buffer), "create buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, output.buffer, &requirements);
    if (requirements.size > budget)
        throw std::runtime_error("GPU buffer exceeds remaining residency budget");
    const VkMemoryPropertyFlags required =
        host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                     : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    std::uint32_t memory_type = memory_properties_.memoryTypeCount;
    for (std::uint32_t index = 0; index < memory_properties_.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) &&
            (memory_properties_.memoryTypes[index].propertyFlags & required) == required) {
            memory_type = index;
            break;
        }
    }
    if (memory_type == memory_properties_.memoryTypeCount)
        throw std::runtime_error("no compatible Vulkan memory type");

    auto allocation =
        detail::structure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    detail::check(vkAllocateMemory(device_, &allocation, nullptr, &output.memory),
                  "allocate GPU memory");
    output.allocation_bytes = requirements.size;
    detail::check(vkBindBufferMemory(device_, output.buffer, output.memory, 0), "bind GPU memory");
    if (host_visible)
        detail::check(vkMapMemory(device_, output.memory, 0, output.bytes, 0, &output.mapped),
                      "map staging memory");
    return output;
}

VkDeviceSize vulkan_device::buffer_allocation_bytes(buffer_allocation_request request) const {
    detail::gpu_buffer buffer;
    buffer.device = device_;
    auto info = detail::structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = request.bytes;
    info.usage = request.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    detail::check(vkCreateBuffer(device_, &info, nullptr, &buffer.buffer),
                  "query scratch allocation size");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer.buffer, &requirements);
    return requirements.size;
}

VkShaderModule vulkan_device::load_shader_module(const std::filesystem::path& shader_directory,
                                                 const char* name) const {
    const auto path = shader_directory / (std::string(name) + ".spv");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open shader: " + path.string());
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0 || size > std::streamoff{16} * 1024 * 1024)
        throw std::runtime_error("invalid SPIR-V module size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(words.data()), size);
    if (!input)
        throw std::runtime_error("short SPIR-V read");

    auto info =
        detail::structure<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = static_cast<std::size_t>(size);
    info.pCode = words.data();
    VkShaderModule module{};
    detail::check(vkCreateShaderModule(device_, &info, nullptr, &module), "create shader module");
    return module;
}

VkPipeline
vulkan_device::create_compute_pipeline(const char* name, VkPipelineLayout pipeline_layout,
                                       const std::filesystem::path& shader_directory) const {
    const auto module = load_shader_module(shader_directory, name);
    auto stage = detail::structure<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    auto info = detail::structure<VkComputePipelineCreateInfo>(
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    info.stage = stage;
    info.layout = pipeline_layout;
    VkPipeline output{};
    const auto status =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &output);
    vkDestroyShaderModule(device_, module, nullptr);
    detail::check(status, "create compute pipeline");
    return output;
}

void vulkan_device::create_descriptor_layout(descriptor_resources& resources) {
    std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
    for (std::uint32_t index = 0; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        if (index == 5)
            bindings[index].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    auto layout = detail::structure<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    detail::check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &resources.layout),
                  "create scene descriptor layout");

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 80};
    auto pool = detail::structure<VkDescriptorPoolCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = 8;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    detail::check(vkCreateDescriptorPool(device_, &pool, nullptr, &resources.scene_pool),
                  "create scene descriptor pool");

    VkDescriptorPoolSize overlay_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 256;
    pool.pPoolSizes = &overlay_size;
    detail::check(vkCreateDescriptorPool(device_, &pool, nullptr, &resources.overlay_pool),
                  "create overlay descriptor pool");

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             sizeof(detail::push_parameters)};
    auto pipeline = detail::structure<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline.setLayoutCount = 1;
    pipeline.pSetLayouts = &resources.layout;
    pipeline.pushConstantRangeCount = 1;
    pipeline.pPushConstantRanges = &push;
    detail::check(vkCreatePipelineLayout(device_, &pipeline, nullptr, &resources.pipeline_layout),
                  "create pipeline layout");
}

} // namespace world_explorer
