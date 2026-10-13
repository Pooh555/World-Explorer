#ifndef WORLD_EXPLORER_RENDER_VULKAN_SWAPCHAIN_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_SWAPCHAIN_HPP

#include "vulkan_common.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <vulkan/vulkan.h>

namespace world_explorer {
class vulkan_device;

class vulkan_swapchain {
  public:
    vulkan_swapchain(vulkan_device& device, VkSurfaceKHR surface,
                     VkFormat preferred_format = VK_FORMAT_UNDEFINED);
    ~vulkan_swapchain();
    vulkan_swapchain(const vulkan_swapchain&) = delete;
    vulkan_swapchain& operator=(const vulkan_swapchain&) = delete;

    bool create(SDL_Window* window);
    void cleanup();

    VkSwapchainKHR swapchain() const noexcept { return swapchain_; }
    VkRenderPass render_pass() const noexcept { return render_pass_; }
    VkFormat format() const noexcept { return format_; }
    VkExtent2D extent() const noexcept { return extent_; }
    std::uint32_t minimum_images() const noexcept { return minimum_images_; }
    bool screenshot_supported() const noexcept { return screenshot_supported_; }
    const std::vector<VkImage>& images() const noexcept { return images_; }
    const std::vector<VkImageView>& image_views() const noexcept { return views_; }
    const std::vector<VkFramebuffer>& framebuffers() const noexcept { return framebuffers_; }
    const std::vector<VkSemaphore>& present_semaphores() const noexcept {
        return present_semaphores_;
    }

  private:
    VkDevice device_{};
    VkPhysicalDevice physical_device_{};
    VkSurfaceKHR surface_{};
    VkSwapchainKHR swapchain_{};
    VkRenderPass render_pass_{};
    VkFormat preferred_format_{VK_FORMAT_UNDEFINED};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{};
    std::uint32_t minimum_images_{};
    bool screenshot_supported_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> present_semaphores_;
};

} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_VULKAN_SWAPCHAIN_HPP
