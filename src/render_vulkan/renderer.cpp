#include "world_explorer/render_vulkan/renderer.hpp"
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace world_explorer {
using detail::check;
using detail::compute_barrier;
using detail::floats;
using detail::gpu_buffer;
using detail::gpu_splat;
using detail::memory_barrier;
using detail::multiply;
using detail::push_parameters;
using detail::structure;

struct vulkan_renderer::implementation {
    SDL_Window* window{};
    renderer_options options;
    renderer_update_sink sink;
    renderer_capabilities capabilities;
    renderer_diagnostics diagnostics;
    std::atomic<std::uint32_t> validation_errors{};
    std::unique_ptr<vulkan_device> device_backend;
    std::unique_ptr<vulkan_swapchain> swapchain_backend;
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queue_family{};
    VkPhysicalDeviceProperties properties{};
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    std::uint32_t minimum_images{};
    bool screenshot_supported{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> present_semaphores;
    VkRenderPass render_pass{};
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool descriptor_pool{};
    VkDescriptorPool overlay_pool{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline project_pipeline{}, histogram_pipeline{}, scan_pipeline{}, scatter_pipeline{},
        draw_pipeline{};
    VkSemaphore timeline{};
    std::uint64_t submitted_timeline{};
    std::size_t frame_index{};
    struct frame {
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkSemaphore acquired{};
        std::uint64_t completion{};
        gpu_buffer projected, keys_a, keys_b, histogram;
        std::array<VkDescriptorSet, 2> descriptors{};
    };
    std::array<frame, 2> frames;
    gpu_buffer scene_buffer;
    scene_snapshot visible_scene;
    vec3d scene_origin{};
    std::size_t splat_count{};
    struct retired_buffer {
        std::uint64_t timeline;
        gpu_buffer buffer;
    };
    std::deque<retired_buffer> retired;
    std::function<void(std::uintptr_t)> overlay;
    std::optional<std::filesystem::path> screenshot_path;
    explicit implementation(SDL_Window* native_window, renderer_options settings)
        : window(native_window), options(std::move(settings)),
          sink({options.residency_bytes, options.max_splats, 4}) {}
    ~implementation() {
        // All child resources must retire before their device owner.
        if (device)
            vkDeviceWaitIdle(device);
        retired.clear();
        scene_buffer = {};
        for (auto& current : frames) {
            current.projected = {};
            current.keys_a = {};
            current.keys_b = {};
            current.histogram = {};
            if (current.acquired)
                vkDestroySemaphore(device, current.acquired, nullptr);
            if (current.pool)
                vkDestroyCommandPool(device, current.pool, nullptr);
        }
        for (auto pipeline :
             {draw_pipeline, project_pipeline, histogram_pipeline, scan_pipeline, scatter_pipeline})
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (overlay_pool)
            vkDestroyDescriptorPool(device, overlay_pool, nullptr);
        if (descriptor_layout)
            vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        swapchain_backend.reset();
        device_backend.reset();
    }

    gpu_buffer allocate(VkDeviceSize bytes, VkBufferUsageFlags usage, bool host_visible) {
        return device_backend->allocate(bytes, usage, host_visible);
    }

    void initialize() {
        if (!window || !options.max_splats || options.max_splats > 10000000)
            throw std::runtime_error(
                "invalid renderer window or splat budget (maximum 10 million)");
        const std::size_t scratch =
            2 * (options.max_splats * (48 + 8 + 8) + ((options.max_splats + 255) / 256) * 64);
        if (scratch > options.residency_bytes ||
            options.max_splats * sizeof(gpu_splat) > options.residency_bytes - scratch)
            throw std::runtime_error(
                "renderer budget cannot hold its configured splat and scratch capacity");
        device_backend = std::make_unique<vulkan_device>(window, options, capabilities, diagnostics,
                                                         validation_errors);
        device_backend->initialize();
        instance = device_backend->instance();
        surface = device_backend->surface();
        physical = device_backend->physical_device();
        device = device_backend->device();
        queue = device_backend->queue();
        queue_family = device_backend->queue_family();
        properties = device_backend->properties();
        timeline = device_backend->timeline();
        device_backend->create_descriptor_layout(descriptor_layout, descriptor_pool, overlay_pool,
                                                 pipeline_layout);
        refresh_swapchain();
        create_pipelines();
        create_frames();
        scene_buffer = allocate(
            16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        diagnostics.scratch_bytes = scratch;
    }
    void create_pipelines() {
        project_pipeline = device_backend->create_compute_pipeline("project.comp", pipeline_layout,
                                                                   options.shader_directory);
        histogram_pipeline = device_backend->create_compute_pipeline(
            "radix_histogram.comp", pipeline_layout, options.shader_directory);
        scan_pipeline = device_backend->create_compute_pipeline("radix_scan.comp", pipeline_layout,
                                                                options.shader_directory);
        scatter_pipeline = device_backend->create_compute_pipeline(
            "radix_scatter.comp", pipeline_layout, options.shader_directory);
        const auto vertex =
            device_backend->load_shader_module(options.shader_directory, "splat.vert");
        VkShaderModule fragment{};
        try {
            fragment = device_backend->load_shader_module(options.shader_directory, "splat.frag");
        } catch (...) {
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto& stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        auto vertices = structure<VkPipelineVertexInputStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        auto assembly = structure<VkPipelineInputAssemblyStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        auto viewport = structure<VkPipelineViewportStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        auto raster = structure<VkPipelineRasterizationStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        auto multisample = structure<VkPipelineMultisampleStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        auto blending = structure<VkPipelineColorBlendStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;
        const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,
                                                           VK_DYNAMIC_STATE_SCISSOR};
        auto dynamic = structure<VkPipelineDynamicStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states.data();
        auto info = structure<VkGraphicsPipelineCreateInfo>(
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        info.stageCount = 2;
        info.pStages = stages.data();
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = pipeline_layout;
        info.renderPass = render_pass;
        const auto status =
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &draw_pipeline);
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
        check(status, "create Gaussian graphics pipeline");
    }
    void create_frames() {
        for (auto& current : frames) {
            auto pool =
                structure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
            pool.queueFamilyIndex = queue_family;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device, &pool, nullptr, &current.pool),
                  "create frame command pool");
            auto commands = structure<VkCommandBufferAllocateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            commands.commandPool = current.pool;
            commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commands.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &commands, &current.command),
                  "allocate frame commands");
            auto semaphore =
                structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            check(vkCreateSemaphore(device, &semaphore, nullptr, &current.acquired),
                  "create acquire semaphore");
            current.projected =
                allocate(options.max_splats * 48, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.keys_a =
                allocate(options.max_splats * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.keys_b =
                allocate(options.max_splats * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.histogram = allocate(((options.max_splats + 255) / 256) * 64,
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            const std::array<VkDescriptorSetLayout, 2> layouts{descriptor_layout,
                                                               descriptor_layout};
            auto descriptors = structure<VkDescriptorSetAllocateInfo>(
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            descriptors.descriptorPool = descriptor_pool;
            descriptors.descriptorSetCount = 2;
            descriptors.pSetLayouts = layouts.data();
            check(vkAllocateDescriptorSets(device, &descriptors, current.descriptors.data()),
                  "allocate frame descriptors");
        }
    }
    void wait_timeline(std::uint64_t value) {
        if (!value)
            return;
        auto wait = structure<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        wait.semaphoreCount = 1;
        wait.pSemaphores = &timeline;
        wait.pValues = &value;
        check(vkWaitSemaphores(device, &wait, 5'000'000'000ULL),
              "wait GPU timeline (5 second timeout)");
    }
    void descriptors(frame& current) {
        for (std::size_t pass = 0; pass < 2; ++pass) {
            const std::array<VkDescriptorBufferInfo, 5> buffers{
                {{scene_buffer.buffer, 0, scene_buffer.bytes},
                 {current.projected.buffer, 0, current.projected.bytes},
                 {pass == 0 ? current.keys_a.buffer : current.keys_b.buffer, 0,
                  current.keys_a.bytes},
                 {pass == 0 ? current.keys_b.buffer : current.keys_a.buffer, 0,
                  current.keys_a.bytes},
                 {current.histogram.buffer, 0, current.histogram.bytes}}};
            std::array<VkWriteDescriptorSet, 5> writes{};
            for (std::uint32_t binding = 0; binding < 5; ++binding) {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = current.descriptors[pass];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &buffers[binding];
            }
            vkUpdateDescriptorSets(device, 5, writes.data(), 0, nullptr);
        }
    }
    std::size_t live_buffer_bytes() const {
        std::size_t bytes = scene_buffer.bytes + diagnostics.scratch_bytes;
        for (const auto& item : retired)
            bytes += item.buffer.bytes;
        return bytes;
    }
    void upload(VkCommandBuffer command, const scene_snapshot& snapshot, vec3d origin,
                std::uint64_t completion) {
        std::size_t count = 0;
        for (const auto& [id, chunk] : snapshot->chunks) {
            static_cast<void>(id);
            count += chunk->splats.size();
        }
        if (count > options.max_splats)
            throw std::runtime_error("scene exceeds configured Gaussian count");
        const auto bytes = std::max<std::size_t>(count * sizeof(gpu_splat), 16);
        const auto live = live_buffer_bytes();
        if (live > options.residency_bytes || bytes > (options.residency_bytes - live) / 2)
            throw std::runtime_error(
                "atomic replacement exceeds old + new + staging + scratch residency budget");
        auto staging = allocate(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        auto replacement = allocate(
            bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        auto* destination = static_cast<gpu_splat*>(staging.mapped);
        std::size_t index = 0;
        for (const auto& [id, chunk] : snapshot->chunks) {
            static_cast<void>(id);
            const auto& transform = chunk->world_from_chunk;
            for (const auto& source : chunk->splats) {
                auto& target = destination[index++];
                const auto point =
                    transform_point(transform, {source.mean[0], source.mean[1], source.mean[2]}) -
                    origin;
                if (std::abs(point.x) > 1e7 || std::abs(point.y) > 1e7 || std::abs(point.z) > 1e7)
                    throw std::runtime_error(
                        "scene exceeds camera-local float extent; spatial streaming is required");
                target.mean_opacity = floats(point);
                target.mean_opacity[3] = source.opacity;
                target.scale_degree = {static_cast<float>(source.scale[0] * transform.scale),
                                       static_cast<float>(source.scale[1] * transform.scale),
                                       static_cast<float>(source.scale[2] * transform.scale),
                                       static_cast<float>(source.sh_degree)};
                target.rotation =
                    floats(multiply(transform.rotation, {source.rotation[0], source.rotation[1],
                                                         source.rotation[2], source.rotation[3]}));
                target.world_to_sh = floats(quatd{transform.rotation.w, -transform.rotation.x,
                                                  -transform.rotation.y, -transform.rotation.z});
                target.sh = source.sh;
            }
        }
        VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(command, staging.buffer, replacement.buffer, 1, &copy);
        memory_barrier(command, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        if (scene_buffer.buffer)
            retired.push_back({completion, std::move(scene_buffer)});
        retired.push_back({completion, std::move(staging)});
        scene_buffer = std::move(replacement);
        splat_count = count;
        scene_origin = origin;
    }
    push_parameters parameters(const camera_view& camera) const {
        const auto focal =
            static_cast<float>(0.5 * extent.height / std::tan(camera.vertical_fov_radians * 0.5));
        return {floats(camera.right),
                floats(camera.down),
                floats(camera.forward),
                floats(camera.position - scene_origin),
                {focal, focal, (static_cast<float>(extent.width) - 1) * 0.5F,
                 (static_cast<float>(extent.height) - 1) * 0.5F},
                {static_cast<float>(extent.width), static_cast<float>(extent.height),
                 camera.near_plane, camera.far_plane},
                {static_cast<std::uint32_t>(splat_count), 0,
                 static_cast<std::uint32_t>((splat_count + 255) / 256), 0}};
    }
    void record_projection(frame& current, push_parameters& params) {
        auto command = current.command;
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                                &current.descriptors[0], 0, nullptr);
        vkCmdPushConstants(command, pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(params), &params);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, project_pipeline);
        vkCmdDispatch(command, params.count_shift[2], 1, 1);
        compute_barrier(command);
        for (std::uint32_t pass = 0; pass < 8; ++pass) {
            params.count_shift[1] = pass * 4;
            vkCmdPushConstants(command, pipeline_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(params), &params);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                                    &current.descriptors[pass % 2], 0, nullptr);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, histogram_pipeline);
            vkCmdDispatch(command, params.count_shift[2], 1, 1);
            compute_barrier(command);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, scan_pipeline);
            vkCmdDispatch(command, 1, 1, 1);
            compute_barrier(command);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, scatter_pipeline);
            vkCmdDispatch(command, params.count_shift[2], 1, 1);
            compute_barrier(command);
        }
        memory_barrier(command, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT);
    }
    void copy_screenshot(VkCommandBuffer command, std::uint32_t image_index,
                         const gpu_buffer& target) {
        auto barrier = structure<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[image_index];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        auto dependency = structure<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(command, images[image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               target.buffer, 1, &region);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(command, &dependency);
        memory_barrier(command, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    }
    void write_screenshot(const gpu_buffer& buffer) {
        std::ofstream output(*screenshot_path, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("cannot write screenshot");
        output << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        const auto* pixels = static_cast<const char*>(buffer.mapped);
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM;
        for (std::size_t index = 0; index < static_cast<std::size_t>(extent.width) * extent.height;
             ++index) {
            const std::array<char, 3> rgb{pixels[index * 4 + (bgra ? 2 : 0)], pixels[index * 4 + 1],
                                          pixels[index * 4 + (bgra ? 0 : 2)]};
            output.write(rgb.data(), 3);
        }
        if (!output)
            throw std::runtime_error("screenshot write failed");
        screenshot_path.reset();
    }
    bool refresh_swapchain() {
        if (!swapchain_backend) {
            swapchain_backend =
                std::make_unique<vulkan_swapchain>(device, physical, surface, format);
            if (!swapchain_backend->create(window))
                return false;
        } else {
            if (swapchain_backend->create(window)) {
                // create() already reinitializes a resize-safe swapchain and populates the new
                // handles.
            } else {
                return false;
            }
        }
        swapchain = swapchain_backend->swapchain();
        render_pass = swapchain_backend->render_pass();
        format = swapchain_backend->format();
        extent = swapchain_backend->extent();
        minimum_images = swapchain_backend->minimum_images();
        screenshot_supported = swapchain_backend->screenshot_supported();
        images = swapchain_backend->images();
        views = swapchain_backend->image_views();
        framebuffers = swapchain_backend->framebuffers();
        present_semaphores = swapchain_backend->present_semaphores();
        return true;
    }
    void draw(const camera_view& camera) {
        const auto begin = std::chrono::steady_clock::now();
        if (!finite(camera.position) || !finite(camera.right) || !finite(camera.down) ||
            !finite(camera.forward) || !std::isfinite(camera.vertical_fov_radians) ||
            camera.vertical_fov_radians <= 0.05 || camera.vertical_fov_radians >= 3.1 ||
            !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
            camera.near_plane <= 0 || camera.far_plane <= camera.near_plane)
            throw std::runtime_error("invalid viewer camera");
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width <= 0 || height <= 0)
            return;
        if (!swapchain || extent.width != static_cast<std::uint32_t>(width) ||
            extent.height != static_cast<std::uint32_t>(height))
            if (!refresh_swapchain())
                return;
        auto& current = frames[frame_index];
        wait_timeline(current.completion);
        std::uint64_t completed = 0;
        check(vkGetSemaphoreCounterValue(device, timeline, &completed),
              "query retirement timeline");
        while (!retired.empty() && retired.front().timeline <= completed)
            retired.pop_front();
        sink.retire_completed(completed);
        diagnostics.completed_timeline = completed;
        std::uint32_t image_index = 0;
        const auto acquired = vkAcquireNextImageKHR(device, swapchain, 5'000'000'000ULL,
                                                    current.acquired, VK_NULL_HANDLE, &image_index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            refresh_swapchain();
            return;
        }
        if (acquired != VK_SUBOPTIMAL_KHR)
            check(acquired, "acquire swapchain image");
        check(vkResetCommandPool(device, current.pool, 0), "reset frame commands");
        auto command_begin =
            structure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        command_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(current.command, &command_begin), "begin frame commands");
        const auto completion = submitted_timeline + 1;
        auto update = sink.take_next();
        if (update) {
            try {
                upload(current.command, update->batch.snapshot, camera.position, completion);
                sink.acknowledge_uploaded(update->ticket, completion);
            } catch (const std::exception& exception) {
                sink.fail_update(update->ticket,
                                 {error_code::RESOURCE_EXHAUSTED, exception.what()});
                update.reset();
            }
        } else if (visible_scene &&
                   dot(camera.position - scene_origin, camera.position - scene_origin) > 100000000)
            upload(current.command, visible_scene, camera.position, completion);
        descriptors(current);
        auto params = parameters(camera);
        if (splat_count)
            record_projection(current, params);
        VkClearValue clear{};
        clear.color = {{0.045F, 0.051F, 0.063F, 1}};
        auto pass = structure<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[image_index];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(current.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{
            0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(current.command, 0, 1, &viewport);
        vkCmdSetScissor(current.command, 0, 1, &scissor);
        if (splat_count) {
            vkCmdBindPipeline(current.command, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline);
            vkCmdBindDescriptorSets(current.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout, 0, 1, &current.descriptors[0], 0, nullptr);
            vkCmdPushConstants(current.command, pipeline_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(params), &params);
            vkCmdDraw(current.command, 6, static_cast<std::uint32_t>(splat_count), 0, 0);
        }
        if (overlay)
            overlay(reinterpret_cast<std::uintptr_t>(current.command));
        vkCmdEndRenderPass(current.command);
        gpu_buffer screenshot;
        if (screenshot_path) {
            screenshot = allocate(static_cast<std::size_t>(extent.width) * extent.height * 4,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
            copy_screenshot(current.command, image_index, screenshot);
        }
        check(vkEndCommandBuffer(current.command), "end frame commands");
        auto wait = structure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        wait.semaphore = current.acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto command =
            structure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        command.commandBuffer = current.command;
        std::array<VkSemaphoreSubmitInfo, 2> signals{};
        for (auto& signal : signals) {
            signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        signals[0].semaphore = present_semaphores[image_index];
        signals[1].semaphore = timeline;
        signals[1].value = completion;
        auto submission = structure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submission.waitSemaphoreInfoCount = 1;
        submission.pWaitSemaphoreInfos = &wait;
        submission.commandBufferInfoCount = 1;
        submission.pCommandBufferInfos = &command;
        submission.signalSemaphoreInfoCount = 2;
        submission.pSignalSemaphoreInfos = signals.data();
        check(vkQueueSubmit2(queue, 1, &submission, VK_NULL_HANDLE), "submit viewer frame");
        submitted_timeline = completion;
        current.completion = completion;
        if (update) {
            visible_scene = update->batch.snapshot;
            sink.acknowledge_visible(update->ticket, visible_scene, completion);
        }
        auto presentation = structure<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        presentation.waitSemaphoreCount = 1;
        presentation.pWaitSemaphores = &present_semaphores[image_index];
        presentation.swapchainCount = 1;
        presentation.pSwapchains = &swapchain;
        presentation.pImageIndices = &image_index;
        const auto presented = vkQueuePresentKHR(queue, &presentation);
        if (screenshot_path) {
            wait_timeline(completion);
            write_screenshot(screenshot);
        }
        frame_index = (frame_index + 1) % frames.size();
        ++diagnostics.submitted_frames;
        diagnostics.resident_splat_count = splat_count;
        diagnostics.allocated_bytes = live_buffer_bytes();
        diagnostics.staging_bytes = 0;
        for (const auto& item : retired)
            if (item.buffer.mapped)
                diagnostics.staging_bytes += item.buffer.bytes;
        diagnostics.last_cpu_frame_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count();
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
            acquired == VK_SUBOPTIMAL_KHR)
            refresh_swapchain();
        else
            check(presented, "present viewer frame");
    }
};
vulkan_renderer::vulkan_renderer(std::unique_ptr<implementation> impl) : impl_(std::move(impl)) {}
vulkan_renderer::~vulkan_renderer() = default;
result<std::unique_ptr<vulkan_renderer>> vulkan_renderer::create(void* sdl_window,
                                                                 renderer_options options) {
    try {
        auto impl = std::make_unique<implementation>(static_cast<SDL_Window*>(sdl_window),
                                                     std::move(options));
        impl->initialize();
        return std::unique_ptr<vulkan_renderer>(new vulkan_renderer(std::move(impl)));
    } catch (const std::exception& exception) {
        return fail(error_code::BACKEND_FAILURE, exception.what());
    }
}
renderer_update_sink& vulkan_renderer::updates() noexcept { return impl_->sink; }
const renderer_capabilities& vulkan_renderer::capabilities() const noexcept {
    return impl_->capabilities;
}
renderer_diagnostics vulkan_renderer::diagnostics() const noexcept {
    auto output = impl_->diagnostics;
    output.validation_errors = impl_->validation_errors.load();
    return output;
}
vulkan_overlay_context vulkan_renderer::overlay_context() const noexcept {
    return {reinterpret_cast<std::uintptr_t>(impl_->instance),
            reinterpret_cast<std::uintptr_t>(impl_->physical),
            reinterpret_cast<std::uintptr_t>(impl_->device),
            reinterpret_cast<std::uintptr_t>(impl_->queue),
            reinterpret_cast<std::uintptr_t>(impl_->overlay_pool),
            reinterpret_cast<std::uintptr_t>(impl_->render_pass),
            impl_->queue_family,
            static_cast<std::uint32_t>(impl_->images.size()),
            impl_->minimum_images};
}
void vulkan_renderer::set_overlay_callback(std::function<void(std::uintptr_t)> callback) {
    impl_->overlay = std::move(callback);
}
result<void> vulkan_renderer::draw(const camera_view& camera) {
    try {
        impl_->draw(camera);
        return {};
    } catch (const std::exception& exception) {
        return fail(error_code::BACKEND_FAILURE, exception.what());
    }
}
result<void> vulkan_renderer::wait_for_idle() {
    const auto status = vkDeviceWaitIdle(impl_->device);
    if (status != VK_SUCCESS)
        return fail(error_code::BACKEND_FAILURE,
                    "Vulkan idle wait failed (device lost or driver failure)");
    return {};
}
result<void> vulkan_renderer::request_screenshot(std::filesystem::path path) {
    if (!impl_->screenshot_supported)
        return fail(error_code::UNSUPPORTED_OPERATION,
                    "surface does not support screenshot transfer");
    if (path.empty())
        return fail(error_code::INVALID_ARGUMENT, "screenshot path is empty");
    impl_->screenshot_path = std::move(path);
    return {};
}
} // namespace world_explorer
