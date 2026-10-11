#ifndef WORLD_EXPLORER_RENDER_VULKAN_DEVICE_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_DEVICE_HPP

#include "vulkan_common.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace world_explorer {

class vulkan_device {
  public:
    vulkan_device(SDL_Window* window, const renderer_options& options,
                  renderer_capabilities& capabilities, renderer_diagnostics& diagnostics,
                  std::atomic<std::uint32_t>& validation_errors);
    ~vulkan_device();
    vulkan_device(const vulkan_device&) = delete;
    vulkan_device& operator=(const vulkan_device&) = delete;

    void initialize();
    detail::gpu_buffer allocate(VkDeviceSize bytes, VkBufferUsageFlags usage, bool host_visible);
    VkShaderModule load_shader_module(const std::filesystem::path& shader_directory,
                                      const char* name) const;
    VkPipeline create_compute_pipeline(const char* name, VkPipelineLayout pipeline_layout,
                                       const std::filesystem::path& shader_directory) const;
    void create_descriptor_layout(VkDescriptorSetLayout& descriptor_layout,
                                  VkDescriptorPool& descriptor_pool, VkDescriptorPool& overlay_pool,
                                  VkPipelineLayout& pipeline_layout);

    VkInstance instance() const noexcept { return instance_; }
    VkSurfaceKHR surface() const noexcept { return surface_; }
    VkPhysicalDevice physical_device() const noexcept { return physical_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue queue() const noexcept { return queue_; }
    std::uint32_t queue_family() const noexcept { return queue_family_; }
    VkPhysicalDeviceMemoryProperties memory_properties() const noexcept {
        return memory_properties_;
    }
    VkPhysicalDeviceProperties properties() const noexcept { return properties_; }
    VkSemaphore timeline() const noexcept { return timeline_; }

  private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* context);
    void select_physical_device();
    void create_logical_device();

    SDL_Window* window_{};
    renderer_options options_;
    renderer_capabilities& capabilities_;
    renderer_diagnostics& diagnostics_;
    std::atomic<std::uint32_t>& validation_errors_;

    VkInstance instance_{};
    VkDebugUtilsMessengerEXT debug_messenger_{};
    VkSurfaceKHR surface_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    std::uint32_t queue_family_{};
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    VkPhysicalDeviceProperties properties_{};
    VkSemaphore timeline_{};
};

} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_VULKAN_DEVICE_HPP
