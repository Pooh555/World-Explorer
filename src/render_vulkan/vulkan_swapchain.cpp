#include "vulkan_swapchain.hpp"

#include "vulkan_device.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace world_explorer {

#if defined(WE_VULKAN_TEST_FAULTS)
namespace detail {

thread_local bool fail_swapchain_after_create {};

void fail_next_swapchain_after_create() noexcept {
    fail_swapchain_after_create = true;
}

}  // namespace detail
#endif

vulkan_swapchain::vulkan_swapchain(vulkan_device& device,
                                   VkSurfaceKHR surface,
                                   VkFormat preferred_format)
    : device_(device.device()), physical_device_(device.physical_device()), surface_(surface),
      preferred_format_(preferred_format) { }

vulkan_swapchain::vulkan_swapchain(const vulkan_swapchain& source, bool /*staged*/)
    : device_(source.device_), physical_device_(source.physical_device_), surface_(source.surface_),
      render_pass_(source.render_pass_),
      preferred_format_(source.format_ == VK_FORMAT_UNDEFINED ? source.preferred_format_
                                                              : source.format_),
      owns_render_pass_(source.render_pass_ == VK_NULL_HANDLE) { }

vulkan_swapchain::~vulkan_swapchain() {
    cleanup();
    if (render_pass_ && owns_render_pass_) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
    }
}

VkExtent2D vulkan_swapchain::extent_for_window(SDL_Window* window) const {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        throw std::runtime_error(SDL_GetError());
    }
    if (width <= 0 || height <= 0) {
        return {};
    }

    // Vulkan fills every field before any enum is inspected.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    VkSurfaceCapabilitiesKHR supported {};
    detail::check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &supported),
                  "query surface capabilities");
    auto requested = supported.currentExtent;
    if (requested.width == std::numeric_limits<std::uint32_t>::max()) {
        requested = { std::clamp(static_cast<std::uint32_t>(width),
                                supported.minImageExtent.width,
                                supported.maxImageExtent.width),
                     std::clamp(static_cast<std::uint32_t>(height),
                                supported.minImageExtent.height,
                                supported.maxImageExtent.height) };
    }
    return requested;
}

bool vulkan_swapchain::create(SDL_Window* window, std::optional<VkExtent2D> expected_extent) {
    const auto K_REQUESTED_EXTENT = extent_for_window(window);
    if (!K_REQUESTED_EXTENT.width || !K_REQUESTED_EXTENT.height) {
        return false;
    }
    if (expected_extent && (K_REQUESTED_EXTENT.width != expected_extent->width ||
                            K_REQUESTED_EXTENT.height != expected_extent->height)) {
        throw std::runtime_error("surface extent changed during swapchain preparation");
    }

    if (swapchain_) {
        detail::check(vkDeviceWaitIdle(device_), "wait for swapchain resize");
    }
    vulkan_swapchain next(*this, true);
    bool handoff_attempted = false;
    try {
        next.create_resources(K_REQUESTED_EXTENT, swapchain_, handoff_attempted);
    } catch (...) {
        // Vulkan retires oldSwapchain when the create call starts, even if it fails.
        if (handoff_attempted) {
            cleanup();
        }
        throw;
    }
    cleanup();
    swapchain_ = std::exchange(next.swapchain_, VK_NULL_HANDLE);
    format_ = next.format_;
    extent_ = next.extent_;
    minimum_images_ = next.minimum_images_;
    screenshot_supported_ = next.screenshot_supported_;
    images_.swap(next.images_);
    views_.swap(next.views_);
    framebuffers_.swap(next.framebuffers_);
    present_semaphores_.swap(next.present_semaphores_);
    if (!render_pass_) {
        render_pass_ = std::exchange(next.render_pass_, VK_NULL_HANDLE);
        owns_render_pass_ = true;
    }
    return true;
}

void vulkan_swapchain::create_resources(VkExtent2D requested_extent,
                                        VkSwapchainKHR old_swapchain,
                                        bool& handoff_attempted) {
    // Vulkan fills every field before any enum is inspected.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    VkSurfaceCapabilitiesKHR supported {};
    detail::check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &supported),
                  "query surface capabilities");
    if (supported.currentExtent.width != std::numeric_limits<std::uint32_t>::max() &&
        (supported.currentExtent.width != requested_extent.width ||
         supported.currentExtent.height != requested_extent.height)) {
        throw std::runtime_error("surface extent changed during swapchain preparation");
    }

    std::uint32_t count = 0;
    detail::check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, nullptr),
                  "query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    detail::check(
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, formats.data()),
        "query surface formats");

    const auto K_SELECTED = std::ranges::find_if(formats, [](auto item) {
        return (item.format == VK_FORMAT_B8G8R8A8_UNORM ||
                item.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               item.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (K_SELECTED == formats.end()) {
        throw std::runtime_error(
            "surface has no SDR UNORM format for display-encoded Gaussian compositing");
    }

    format_ = preferred_format_ == VK_FORMAT_UNDEFINED ? K_SELECTED->format : preferred_format_;
    extent_ = requested_extent;
    minimum_images_ = std::max(2U, supported.minImageCount);
    std::uint32_t image_count = minimum_images_ + 1;
    if (supported.maxImageCount) {
        image_count = std::min(image_count, supported.maxImageCount);
    }
    if (image_count < 2) {
        throw std::runtime_error("surface requires unsupported single-image presentation");
    }

    auto info =
        detail::structure<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = format_;
    info.imageColorSpace = K_SELECTED->colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    screenshot_supported_ = (supported.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (screenshot_supported_) {
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = supported.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto mode : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR }) {
        if (supported.supportedCompositeAlpha & mode) {
            info.compositeAlpha = mode;
            break;
        }
    }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    // This surface requires a handoff; the call retires old presentation even on failure.
    info.oldSwapchain = old_swapchain;
    handoff_attempted = old_swapchain != VK_NULL_HANDLE;
    detail::check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "create swapchain");
#if defined(WE_VULKAN_TEST_FAULTS)
    if (detail::fail_swapchain_after_create) {
        detail::fail_swapchain_after_create = false;
        throw std::runtime_error("injected post-create swapchain failure");
    }
#endif

    detail::check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr),
                  "query swapchain images");
    images_.resize(image_count);
    detail::check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data()),
                  "query swapchain images");

    if (!render_pass_) {
        const VkAttachmentDescription K_DISPLAY { 0,
                                                 format_,
                                                 VK_SAMPLE_COUNT_1_BIT,
                                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                 VK_ATTACHMENT_STORE_OP_STORE,
                                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        const VkAttachmentReference K_OUTPUT { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &K_OUTPUT;
        const VkSubpassDependency K_DEPENDENCY { VK_SUBPASS_EXTERNAL,
                                                0,
                                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                0,
                                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                VK_DEPENDENCY_BY_REGION_BIT };

        auto pass =
            detail::structure<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
        pass.attachmentCount = 1;
        pass.pAttachments = &K_DISPLAY;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies = &K_DEPENDENCY;
        detail::check(vkCreateRenderPass(device_, &pass, nullptr, &render_pass_),
                      "create scene render pass");
    }

    for (auto image : images_) {
        auto view =
            detail::structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView handle {};
        detail::check(vkCreateImageView(device_, &view, nullptr, &handle), "create swapchain view");
        views_.push_back(handle);

        auto framebuffer =
            detail::structure<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
        framebuffer.renderPass = render_pass_;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &views_.back();
        framebuffer.width = extent_.width;
        framebuffer.height = extent_.height;
        framebuffer.layers = 1;
        VkFramebuffer frame_handle {};
        detail::check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &frame_handle),
                      "create framebuffer");
        framebuffers_.push_back(frame_handle);

        auto semaphore =
            detail::structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore finished {};
        detail::check(vkCreateSemaphore(device_, &semaphore, nullptr, &finished),
                      "create per-image present semaphore");
        present_semaphores_.push_back(finished);
    }
}

void vulkan_swapchain::cleanup() {
    for (auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    for (auto view : views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    views_.clear();
    for (auto semaphore : present_semaphores_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    present_semaphores_.clear();
    images_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

}  // namespace world_explorer
