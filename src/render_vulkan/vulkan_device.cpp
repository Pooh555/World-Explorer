#include "vulkan_device.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace world_explorer {

VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_device::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                              VkDebugUtilsMessageTypeFlagsEXT,
                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                              void* context) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        static_cast<vulkan_device*>(context)->validation_errors_.fetch_add(1);
    }
    std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    return VK_FALSE;
}

vulkan_device::vulkan_device(SDL_Window* window,
                             const RendererOptions& options,
                             RendererCapabilities& capabilities,
                             std::atomic<std::uint32_t>& validation_errors)
    : window_(window), options_(options), capabilities_(capabilities),
      validation_errors_(validation_errors) { }

vulkan_device::~vulkan_device() {
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
    if (timeline_) {
        vkDestroySemaphore(device_, timeline_, nullptr);
    }
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
    }
    if (device_) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debug_messenger_) {
        const auto K_DESTROY = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (K_DESTROY) {
            K_DESTROY(instance_, debug_messenger_, nullptr);
        }
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void vulkan_device::initialize() {
    if (!window_ || !options_.m_max_splats || options_.m_max_splats > 10000000) {
        throw std::runtime_error("invalid renderer window or splat budget (maximum 10 million)");
    }

    std::uint32_t loader_version = VK_API_VERSION_1_0;
    detail::check(vkEnumerateInstanceVersion(&loader_version), "query Vulkan loader version");
    if (loader_version < VK_API_VERSION_1_3) {
        throw std::runtime_error(
            "Vulkan 1.3 with synchronization2 and timeline semaphores is required");
    }

    capabilities_.m_instance_version = std::min(loader_version, VK_API_VERSION_1_4);
    std::uint32_t extension_count = 0;
    detail::check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr),
                  "enumerate instance extensions");
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    detail::check(vkEnumerateInstanceExtensionProperties(
                      nullptr, &extension_count, available_extensions.data()),
                  "enumerate instance extensions");

    const auto K_HAS_EXTENSION = [&](const char* name) {
        return std::ranges::any_of(available_extensions, [&](const auto& item) {
            return std::strcmp(item.extensionName, name) == 0;
        });
    };

    std::uint32_t sdl_extension_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (!sdl_extensions) {
        throw std::runtime_error(SDL_GetError());
    }

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_extension_count);
    VkInstanceCreateFlags instance_flags = 0;
    if (K_HAS_EXTENSION(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char*> layers;
    if (options_.m_validation) {
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
            K_HAS_EXTENSION(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            capabilities_.m_validation_enabled = true;
        }
    }

    auto application = detail::structure<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "World Explorer";
    application.apiVersion = capabilities_.m_instance_version;

    auto debug = detail::structure<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType =
        VkDebugUtilsMessageTypeFlagsEXT { VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT } |
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

    const VkValidationFeatureEnableEXT K_SYNCHRONIZATION_VALIDATION =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    auto validation_features =
        detail::structure<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    if (capabilities_.m_validation_enabled) {
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        validation_features.enabledValidationFeatureCount = 1;
        validation_features.pEnabledValidationFeatures = &K_SYNCHRONIZATION_VALIDATION;
        validation_features.pNext = &debug;
        create.pNext = &validation_features;
    }

    detail::check(vkCreateInstance(&create, nullptr, &instance_), "create Vulkan instance");
    if (capabilities_.m_validation_enabled) {
        const auto K_MAKE = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        detail::check(K_MAKE(instance_, &debug, nullptr, &debug_messenger_),
                      "create validation messenger");
    }

    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(SDL_GetError());
    }

    select_physical_device();
    create_logical_device();
}

void vulkan_device::select_physical_device() {
    constexpr auto K_COMPOSITION_SHARED_BYTES { 256uz * 48 + 10 * sizeof(std::uint32_t) };
    std::uint32_t count = 0;
    detail::check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    detail::check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate GPUs");

    for (auto candidate : devices) {
        VkPhysicalDeviceProperties candidate_properties {};
        vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
        if (candidate_properties.apiVersion < VK_API_VERSION_1_3 ||
            candidate_properties.limits.maxComputeWorkGroupInvocations < 256 ||
            candidate_properties.limits.maxComputeWorkGroupSize[0] < 256 ||
            candidate_properties.limits.maxStorageBufferRange <
                options_.m_max_splats * sizeof(detail::gpu_splat) ||
            candidate_properties.limits.maxComputeWorkGroupSize[1] < 16 ||
            candidate_properties.limits.maxPushConstantsSize < sizeof(detail::push_parameters) ||
            candidate_properties.limits.maxComputeSharedMemorySize < K_COMPOSITION_SHARED_BYTES ||
            candidate_properties.limits.maxPerStageDescriptorStorageBuffers < 10 ||
            candidate_properties.limits.maxDescriptorSetStorageBuffers < 10 ||
            candidate_properties.limits.maxComputeWorkGroupCount[0] <
                (options_.m_max_splats + 255) / 256) {
            continue;
        }

        auto features13 = detail::structure<VkPhysicalDeviceVulkan13Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        auto features12 = detail::structure<VkPhysicalDeviceVulkan12Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        features12.pNext = &features13;
        auto features = detail::structure<VkPhysicalDeviceFeatures2>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features12.timelineSemaphore || !features13.synchronization2) {
            continue;
        }

        std::uint32_t count_extensions = 0;
        detail::check(
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count_extensions, nullptr),
            "query device extensions");
        std::vector<VkExtensionProperties> extensions(count_extensions);
        detail::check(vkEnumerateDeviceExtensionProperties(
                          candidate, nullptr, &count_extensions, extensions.data()),
                      "query device extensions");
        if (!std::ranges::any_of(extensions, [](const auto& item) {
                return std::strcmp(item.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            })) {
            continue;
        }

        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (std::uint32_t index { 0 }; index < family_count; ++index) {
            VkBool32 present = VK_FALSE;
            detail::check(
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface_, &present),
                "query presentation support");
            if (present && (families[index].queueFlags &
                            (VkQueueFlags { VK_QUEUE_GRAPHICS_BIT } | VK_QUEUE_COMPUTE_BIT)) ==
                               (VkQueueFlags { VK_QUEUE_GRAPHICS_BIT } | VK_QUEUE_COMPUTE_BIT)) {
                physical_ = candidate;
                queue_family_ = index;
                properties_ = candidate_properties;
                capabilities_.m_device_name = properties_.deviceName;
                capabilities_.m_device_version = properties_.apiVersion;
                capabilities_.m_max_splats = options_.m_max_splats;
                capabilities_.m_timestamp_valid_bits = families[index].timestampValidBits;
                capabilities_.m_timestamp_period_ns = properties_.limits.timestampPeriod;
                capabilities_.m_gpu_timestamps_supported =
                    families[index].timestampValidBits > 0 &&
                    families[index].timestampValidBits <= 64 &&
                    properties_.limits.timestampComputeAndGraphics &&
                    std::isfinite(capabilities_.m_timestamp_period_ns) &&
                    capabilities_.m_timestamp_period_ns > 0;
                capabilities_.m_portability_subset =
                    std::ranges::any_of(extensions, [](const auto& item) {
                        return std::strcmp(item.extensionName, "VK_KHR_portability_subset") == 0;
                    });
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

    std::vector<const char*> extensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (capabilities_.m_portability_subset) {
        extensions.push_back("VK_KHR_portability_subset");
    }

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

    VmaAllocatorCreateInfo allocator_info {};
    allocator_info.instance = instance_;
    allocator_info.physicalDevice = physical_;
    allocator_info.device = device_;
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
    detail::check(vmaCreateAllocator(&allocator_info, &allocator_), "create VMA allocator");
}

// Vulkan's distinct integer typedefs retain their native argument order at this boundary.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
detail::gpu_buffer vulkan_device::allocate(VkDeviceSize bytes,
                                           VkBufferUsageFlags usage,
                                           bool host_visible,
                                           std::size_t budget) {
    detail::gpu_buffer output;
    output.bytes = std::max<VkDeviceSize>(bytes, 16);
    auto info = detail::structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = output.bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (buffer_allocation_bytes({ output.bytes, usage }) > budget) {
        throw std::runtime_error("GPU buffer exceeds remaining residency budget");
    }

    VmaAllocationCreateInfo allocation {};
    allocation.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocation.usage = VMA_MEMORY_USAGE_UNKNOWN;
    allocation.requiredFlags =
        host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                     : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (host_visible) {
        allocation.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo allocated {};
    detail::check(
        vmaCreateBuffer(
            allocator_, &info, &allocation, &output.buffer, &output.allocation, &allocated),
        "allocate GPU buffer");
    output.allocator = allocator_;
    output.allocation_bytes = allocated.size;
    output.mapped = allocated.pMappedData;
    if (host_visible && !output.mapped) {
        throw std::runtime_error("VMA did not map host-visible GPU buffer");
    }
    if (output.allocation_bytes > budget) {
        throw std::runtime_error("VMA allocation exceeds remaining residency budget");
    }
    return output;
}

detail::gpu_image vulkan_device::allocate_image(const VkImageCreateInfo& info, std::size_t budget) {
    VkImage probe {};
    detail::check(vkCreateImage(device_, &info, nullptr, &probe), "query mesh image allocation");
    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device_, probe, &requirements);
    vkDestroyImage(device_, probe, nullptr);
    if (requirements.size > budget) {
        throw std::runtime_error("Mesh image exceeds residency budget");
    }
    detail::gpu_image output;
    output.m_allocator = allocator_;
    VmaAllocationCreateInfo allocation {};
    allocation.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocation.usage = VMA_MEMORY_USAGE_UNKNOWN;
    allocation.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VmaAllocationInfo allocated {};
    detail::check(
        vmaCreateImage(
            allocator_, &info, &allocation, &output.m_image, &output.m_allocation, &allocated),
        "allocate mesh target image");
    output.m_allocation_bytes = allocated.size;
    if (allocated.size > budget) {
        throw std::runtime_error("Mesh image allocation exceeds residency budget");
    }
    return output;
}

VkDeviceSize vulkan_device::buffer_allocation_bytes(buffer_allocation_request request) const {
    VkBuffer buffer {};
    auto info = detail::structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = request.bytes;
    info.usage = request.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    detail::check(vkCreateBuffer(device_, &info, nullptr, &buffer), "query buffer allocation size");
    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    vkDestroyBuffer(device_, buffer, nullptr);
    return requirements.size;
}

VkShaderModule vulkan_device::load_shader_module(const std::filesystem::path& shader_directory,
                                                 const char* name) const {
    const auto K_PATH = shader_directory / (std::string(name) + ".spv");
    std::ifstream input(K_PATH, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open shader: " + K_PATH.string());
    }
    const auto K_SIZE = input.tellg();
    if (K_SIZE <= 0 || K_SIZE % 4 != 0 || K_SIZE > std::streamoff { 16 } * 1024 * 1024) {
        throw std::runtime_error("invalid SPIR-V module size");
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(K_SIZE) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(words.data()), K_SIZE);
    if (!input) {
        throw std::runtime_error("short SPIR-V read");
    }

    auto info =
        detail::structure<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = static_cast<std::size_t>(K_SIZE);
    info.pCode = words.data();
    VkShaderModule module {};
    detail::check(vkCreateShaderModule(device_, &info, nullptr, &module), "create shader module");
    return module;
}

VkPipeline
vulkan_device::create_compute_pipeline(const char* name,
                                       VkPipelineLayout pipeline_layout,
                                       const std::filesystem::path& shader_directory) const {
    const auto K_MODULE = load_shader_module(shader_directory, name);
    auto stage = detail::structure<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = K_MODULE;
    stage.pName = "main";
    auto info = detail::structure<VkComputePipelineCreateInfo>(
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    info.stage = stage;
    info.layout = pipeline_layout;
    VkPipeline output {};
    const auto K_STATUS =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &output);
    vkDestroyShaderModule(device_, K_MODULE, nullptr);
    detail::check(K_STATUS, "create compute pipeline");
    return output;
}

void vulkan_device::create_descriptor_layout(descriptor_resources& resources) {
    std::array<VkDescriptorSetLayoutBinding, 10> bindings {};
    for (std::uint32_t index { 0 }; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        if (index == 0 || index == 5) {
            bindings[index].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (index == 0) {
                bindings[index].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
            }
        }
    }
    auto layout = detail::structure<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    detail::check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &resources.layout),
                  "create scene descriptor layout");

    VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 80 };
    auto pool = detail::structure<VkDescriptorPoolCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = 8;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    detail::check(vkCreateDescriptorPool(device_, &pool, nullptr, &resources.scene_pool),
                  "create scene descriptor pool");

    VkDescriptorPoolSize overlay_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 };
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 256;
    pool.pPoolSizes = &overlay_size;
    detail::check(vkCreateDescriptorPool(device_, &pool, nullptr, &resources.overlay_pool),
                  "create overlay descriptor pool");

    VkPushConstantRange push { VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0,
                              sizeof(detail::push_parameters) };
    auto pipeline = detail::structure<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipeline.setLayoutCount = 1;
    pipeline.pSetLayouts = &resources.layout;
    pipeline.pushConstantRangeCount = 1;
    pipeline.pPushConstantRanges = &push;
    detail::check(vkCreatePipelineLayout(device_, &pipeline, nullptr, &resources.pipeline_layout),
                  "create pipeline layout");
}

}  // namespace world_explorer
