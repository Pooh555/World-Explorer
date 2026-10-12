#include "vulkan_swapchain.hpp"
#include "vulkan_device.hpp"
#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace world_explorer {

vulkan_swapchain::vulkan_swapchain(vulkan_device& device, VkSurfaceKHR surface,
                                   VkFormat preferred_format)
    : allocator_(device), device_(device.device()), physical_device_(device.physical_device()),
      surface_(surface), preferred_format_(preferred_format) {}

vulkan_swapchain::~vulkan_swapchain() {
    cleanup();
    if (render_pass_)
        vkDestroyRenderPass(device_, render_pass_, nullptr);
}

bool vulkan_swapchain::create(SDL_Window* window, std::size_t framebuffer_budget) {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height))
        throw std::runtime_error(SDL_GetError());
    if (width <= 0 || height <= 0)
        return false;

    VkFormatProperties floating_format{};
    vkGetPhysicalDeviceFormatProperties(physical_device_, VK_FORMAT_R32G32B32A32_SFLOAT,
                                        &floating_format);
    constexpr VkFormatFeatureFlags REQUIRED = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if ((floating_format.optimalTilingFeatures & REQUIRED) != REQUIRED)
        throw std::runtime_error("device lacks FP32 color attachments; no UNORM fallback");

    if (swapchain_) {
        detail::check(vkDeviceWaitIdle(device_), "wait for swapchain resize");
        cleanup();
    }

    // Vulkan fills every field before any enum is inspected.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    VkSurfaceCapabilitiesKHR supported{};
    detail::check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &supported),
                  "query surface capabilities");

    std::uint32_t count = 0;
    detail::check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, nullptr),
                  "query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    detail::check(
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, formats.data()),
        "query surface formats");

    const auto selected = std::ranges::find_if(formats, [](auto item) {
        return (item.format == VK_FORMAT_B8G8R8A8_UNORM ||
                item.format == VK_FORMAT_R8G8B8A8_UNORM) &&
               item.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (selected == formats.end())
        throw std::runtime_error(
            "surface has no SDR UNORM format for display-encoded Gaussian compositing");

    format_ = preferred_format_ == VK_FORMAT_UNDEFINED ? selected->format : preferred_format_;
    extent_ = supported.currentExtent;
    if (extent_.width == std::numeric_limits<std::uint32_t>::max()) {
        extent_ = {std::clamp(static_cast<std::uint32_t>(width), supported.minImageExtent.width,
                              supported.maxImageExtent.width),
                   std::clamp(static_cast<std::uint32_t>(height), supported.minImageExtent.height,
                              supported.maxImageExtent.height)};
    }
    minimum_images_ = std::max(2U, supported.minImageCount);
    std::uint32_t image_count = minimum_images_ + 1;
    if (supported.maxImageCount)
        image_count = std::min(image_count, supported.maxImageCount);
    if (image_count < 2)
        throw std::runtime_error("surface requires unsupported single-image presentation");

    auto info =
        detail::structure<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = format_;
    info.imageColorSpace = selected->colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    screenshot_supported_ = (supported.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (screenshot_supported_)
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = supported.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto mode :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (supported.supportedCompositeAlpha & mode) {
            info.compositeAlpha = mode;
            break;
        }
    }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    detail::check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "create swapchain");

    detail::check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr),
                  "query swapchain images");
    images_.resize(image_count);
    detail::check(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, images_.data()),
                  "query swapchain images");

    if (!render_pass_) {
        VkAttachmentDescription floating{0,
                                         VK_FORMAT_R32G32B32A32_SFLOAT,
                                         VK_SAMPLE_COUNT_1_BIT,
                                         VK_ATTACHMENT_LOAD_OP_CLEAR,
                                         VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                         VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkAttachmentDescription display{0,
                                              format_,
                                              VK_SAMPLE_COUNT_1_BIT,
                                              VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                              VK_ATTACHMENT_STORE_OP_STORE,
                                              VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                              VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                              VK_IMAGE_LAYOUT_UNDEFINED,
                                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        const std::array<VkAttachmentDescription, 2> attachments{floating, display};
        const VkAttachmentReference floating_output{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference floating_input{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkAttachmentReference display_output{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        std::array<VkSubpassDescription, 2> subpasses{};
        for (auto& subpass : subpasses) {
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
        }
        subpasses[0].pColorAttachments = &floating_output;
        subpasses[1].pColorAttachments = &display_output;
        subpasses[1].inputAttachmentCount = 1;
        subpasses[1].pInputAttachments = &floating_input;
        std::array<VkSubpassDependency, 3> dependencies{};
        // Reused floating images cannot be cleared before earlier input reads.
        dependencies[0] = {
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_DEPENDENCY_BY_REGION_BIT};
        // Discarding a written attachment still uses color-write access after
        // its last input read. Synchronize the store as well as the read.
        dependencies[1] = {
            0,
            1,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_DEPENDENCY_BY_REGION_BIT};
        // Acquire waits at color output, including the display image's first use.
        dependencies[2] = {VK_SUBPASS_EXTERNAL,
                           1,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_DEPENDENCY_BY_REGION_BIT};

        auto pass =
            detail::structure<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
        pass.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        pass.pAttachments = attachments.data();
        pass.subpassCount = static_cast<std::uint32_t>(subpasses.size());
        pass.pSubpasses = subpasses.data();
        pass.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        pass.pDependencies = dependencies.data();
        detail::check(vkCreateRenderPass(device_, &pass, nullptr, &render_pass_),
                      "create scene render pass");
    }

    for (auto image : images_) {
        auto view =
            detail::structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView handle{};
        detail::check(vkCreateImageView(device_, &view, nullptr, &handle), "create swapchain view");
        views_.push_back(handle);

        auto target = allocator_.allocate_color_image(extent_, VK_FORMAT_R32G32B32A32_SFLOAT,
                                                      framebuffer_budget - allocated_bytes_);
        allocated_bytes_ += static_cast<std::size_t>(target.bytes);
        color_targets_.push_back(std::move(target));
        const std::array<VkImageView, 2> attachments{color_targets_.back().view, views_.back()};

        auto framebuffer =
            detail::structure<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
        framebuffer.renderPass = render_pass_;
        framebuffer.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        framebuffer.pAttachments = attachments.data();
        framebuffer.width = extent_.width;
        framebuffer.height = extent_.height;
        framebuffer.layers = 1;
        VkFramebuffer frame_handle{};
        detail::check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &frame_handle),
                      "create framebuffer");
        framebuffers_.push_back(frame_handle);

        auto semaphore =
            detail::structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore finished{};
        detail::check(vkCreateSemaphore(device_, &semaphore, nullptr, &finished),
                      "create per-image present semaphore");
        present_semaphores_.push_back(finished);
    }
    return true;
}

void vulkan_swapchain::cleanup() {
    for (auto framebuffer : framebuffers_)
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    framebuffers_.clear();
    color_targets_.clear();
    allocated_bytes_ = 0;
    for (auto view : views_)
        vkDestroyImageView(device_, view, nullptr);
    views_.clear();
    for (auto semaphore : present_semaphores_)
        vkDestroySemaphore(device_, semaphore, nullptr);
    present_semaphores_.clear();
    images_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

} // namespace world_explorer
