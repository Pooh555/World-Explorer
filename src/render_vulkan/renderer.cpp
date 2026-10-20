#include "world_explorer/render_vulkan/renderer.hpp"

#include "mesh_pass.hpp"
#include "renderer_faults.hpp"
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "world_explorer/render_core/radix.hpp"
#include "world_explorer/render_core/sh_packing.hpp"
#include "world_explorer/render_core/tile_plan.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace world_explorer {

#if defined(WE_VULKAN_TEST_FAULTS)
namespace detail {

thread_local bool inject_device_loss_on_next_frame {};

void fail_next_frame_with_device_loss() {
    inject_device_loss_on_next_frame = true;
}

thread_local frame_acquire_delay inject_acquire_delay { frame_acquire_delay::NONE };

void defer_next_frame_acquisition(frame_acquire_delay delay) {
    inject_acquire_delay = delay;
}

}  // namespace detail
#endif
using detail::check;
using detail::compute_barrier;
using detail::floats;
using detail::gpu_buffer;
using detail::gpu_splat;
using detail::memory_barrier;
using detail::multiply;
using detail::push_parameters;
using detail::structure;

struct VulkanRenderer::Implementation {
    SDL_Window* m_window {};
    RendererOptions m_options;
    RendererUpdateSink m_sink;
    TileRegionPlan m_region_plan;
    RendererCapabilities m_capabilities;
    RendererDiagnostics m_diagnostics;
    std::atomic<std::uint32_t> m_validation_errors {};
    std::unique_ptr<vulkan_device> m_device_backend;
    std::unique_ptr<MeshPass> m_mesh_pass;
    std::unique_ptr<vulkan_swapchain> m_swapchain_backend;
    VkInstance m_instance {};
    VkSurfaceKHR m_surface {};
    VkPhysicalDevice m_physical {};
    VkDevice m_device {};
    VkQueue m_queue {};
    std::uint32_t m_queue_family {};
    VkPhysicalDeviceProperties m_properties {};
    VkSwapchainKHR m_swapchain {};
    VkFormat m_format {};
    VkExtent2D m_extent {};
    std::uint32_t m_minimum_images {};
    bool m_screenshot_supported {};
    std::span<const VkImage> m_images;
    std::span<const VkImageView> m_views;
    std::span<const VkFramebuffer> m_framebuffers;
    std::span<const VkSemaphore> m_present_semaphores;
    VkRenderPass m_render_pass {};
    descriptor_resources m_bindings;
    VkPipeline m_project_pipeline {}, m_histogram_pipeline {}, m_scan_pipeline {},
        m_scatter_pipeline {}, m_composite_pipeline {}, m_draw_pipeline {},
        m_radix_offsets_pipeline {}, m_tile_count_pipeline {}, m_tile_scan_pipeline {},
        m_tile_emit_pipeline {}, m_tile_ranges_pipeline {};
    VkSemaphore m_timeline {};
    std::uint64_t m_submitted_timeline {};
    std::size_t m_frame_index {};

    struct Frame {
        VkCommandPool m_pool {};
        VkCommandBuffer m_command {};
        VkSemaphore m_acquired {};
        std::uint64_t m_completion {};
        std::unique_ptr<MeshTarget> m_mesh_target;
        gpu_buffer m_projected, m_keys_a, m_keys_b, m_histogram, m_captured, m_overlap_offsets,
            m_group_offsets, m_control, m_tile_ranges, m_references_a, m_references_b;
        std::array<VkDescriptorSet, 4> m_descriptors {};
        VkQueryPool m_timing_queries {};
        std::array<bool, K_GPU_TIMING_STAGE_COUNT> m_timing_pending {};
        RendererFrameTiming m_timing;
        bool m_timing_finished {};
    };

    std::array<Frame, 2> m_frames;
    gpu_buffer m_scene_buffer;

    struct UploadRegion {
        std::size_t m_source_offset {}, m_target_offset {}, m_bytes {};
        bool m_resident {};
    };

    struct UploadResources {
        gpu_buffer m_staging, m_replacement;
        std::size_t m_bytes {}, m_copied {}, m_count {};
        Vec3d m_origin {};
        std::vector<UploadRegion> m_regions;
        std::size_t m_mesh_vertex_offset {}, m_mesh_index_offset {}, m_mesh_vertices {},
            m_mesh_indices {};
    };

    struct ActiveUpload {
        AdmittedUpdate m_update;
        UploadResources m_resources;
    };

    // The old scene remains renderable until the final transfer is submitted.
    // This owner keeps staging and replacement alive across frame completions.
    std::optional<ActiveUpload> m_uploading;
    SceneSnapshot m_visible_scene;
    Vec3d m_scene_origin {};
    std::size_t m_splat_count {};
    std::size_t m_mesh_vertex_offset {}, m_mesh_index_offset {}, m_mesh_vertices {},
        m_mesh_indices {};

    struct RetiredBuffer {
        std::uint64_t m_timeline;
        gpu_buffer m_buffer;
    };

    std::deque<RetiredBuffer> m_retired;
    std::function<void(std::uintptr_t)> m_overlay;
    std::optional<std::filesystem::path> m_screenshot_path;

    explicit Implementation(SDL_Window* native_window, RendererOptions settings)
        : m_window(native_window), m_options(std::move(settings)),
          m_sink({ m_options.m_residency_bytes, m_options.m_max_splats, 4 }) { }

    ~Implementation() {
        // All child resources must retire before their device owner.
        if (m_device) {
            vkDeviceWaitIdle(m_device);
        }
        m_retired.clear();
        m_uploading.reset();
        m_scene_buffer = {};
        for (auto& current : m_frames) {
            current.m_mesh_target.reset();
            current.m_projected = {};
            current.m_keys_a = {};
            current.m_keys_b = {};
            current.m_histogram = {};
            current.m_captured = {};
            current.m_overlap_offsets = {};
            current.m_group_offsets = {};
            current.m_control = {};
            current.m_tile_ranges = {};
            current.m_references_a = {};
            current.m_references_b = {};
            if (current.m_timing_queries) {
                vkDestroyQueryPool(m_device, current.m_timing_queries, nullptr);
            }
            if (current.m_acquired) {
                vkDestroySemaphore(m_device, current.m_acquired, nullptr);
            }
            if (current.m_pool) {
                vkDestroyCommandPool(m_device, current.m_pool, nullptr);
            }
        }
        for (auto pipeline : { m_draw_pipeline,
                              m_project_pipeline,
                              m_histogram_pipeline,
                              m_scan_pipeline,
                              m_scatter_pipeline,
                              m_composite_pipeline,
                              m_radix_offsets_pipeline,
                              m_tile_count_pipeline,
                              m_tile_scan_pipeline,
                              m_tile_emit_pipeline,
                              m_tile_ranges_pipeline }) {
            if (pipeline) {
                vkDestroyPipeline(m_device, pipeline, nullptr);
            }
        }
        if (m_bindings.pipeline_layout) {
            vkDestroyPipelineLayout(m_device, m_bindings.pipeline_layout, nullptr);
        }
        if (m_bindings.scene_pool) {
            vkDestroyDescriptorPool(m_device, m_bindings.scene_pool, nullptr);
        }
        if (m_bindings.overlay_pool) {
            vkDestroyDescriptorPool(m_device, m_bindings.overlay_pool, nullptr);
        }
        if (m_bindings.layout) {
            vkDestroyDescriptorSetLayout(m_device, m_bindings.layout, nullptr);
        }
        m_mesh_pass.reset();
        m_swapchain_backend.reset();
        m_device_backend.reset();
    }

    gpu_buffer allocate(VkDeviceSize bytes,
                        VkBufferUsageFlags usage,
                        bool host_visible,
                        VkDeviceSize transient_bytes = 0) {
        const auto K_LIVE = live_buffer_bytes();
        if (K_LIVE > m_options.m_residency_bytes ||
            transient_bytes > m_options.m_residency_bytes - K_LIVE) {
            throw std::runtime_error("GPU buffers exceed residency budget");
        }
        return m_device_backend->allocate(bytes,
                                          usage,
                                          host_visible,
                                          m_options.m_residency_bytes - K_LIVE -
                                              static_cast<std::size_t>(transient_bytes));
    }

    void initialize() {
        switch (m_options.m_splat_storage) {
        case GpuSplatStorage::FP32:
            m_capabilities.m_splat_stride_bytes = sizeof(gpu_splat);
            break;
        case GpuSplatStorage::FP32_GEOMETRY_FP16_SH:
            m_capabilities.m_splat_stride_bytes = sizeof(detail::gpu_splat_packed_sh);
            break;
        default:
            throw std::invalid_argument("unknown GPU splat storage profile");
        }
        m_capabilities.m_splat_storage = m_options.m_splat_storage;
        if (!m_window || !m_options.m_max_splats || m_options.m_max_splats > 10000000) {
            throw std::runtime_error(
                "invalid renderer window or splat budget (maximum 10 million)");
        }
        if (!m_options.m_max_tile_references || m_options.m_max_tile_references > 8000000) {
            throw std::runtime_error("invalid tile-reference limit (maximum 8 million)");
        }
        m_device_backend = std::make_unique<vulkan_device>(
            m_window, m_options, m_capabilities, m_validation_errors);
        m_device_backend->initialize();
        m_instance = m_device_backend->instance();
        m_surface = m_device_backend->surface();
        m_physical = m_device_backend->physical_device();
        m_device = m_device_backend->device();
        m_queue = m_device_backend->queue();
        m_queue_family = m_device_backend->queue_family();
        m_properties = m_device_backend->properties();
        m_timeline = m_device_backend->timeline();
        m_device_backend->create_descriptor_layout(m_bindings);
        refresh_swapchain();
        create_pipelines();
        create_frames();
        m_scene_buffer =
            allocate(16,
                     VkBufferUsageFlags { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT } |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     false);
        m_diagnostics.m_scratch_bytes = scratch_allocation_bytes();
        m_diagnostics.m_allocated_bytes = live_buffer_bytes();
    }

    void create_pipelines() {
        m_project_pipeline = m_device_backend->create_compute_pipeline(
            m_options.m_splat_storage == GpuSplatStorage::FP32 ? "project.comp"
                                                               : "project_packed.comp",
            m_bindings.pipeline_layout,
            m_options.m_shader_directory);
        m_histogram_pipeline = m_device_backend->create_compute_pipeline(
            "radix_histogram.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_scan_pipeline = m_device_backend->create_compute_pipeline(
            "radix_scan.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_scatter_pipeline = m_device_backend->create_compute_pipeline(
            "radix_scatter.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_composite_pipeline = m_device_backend->create_compute_pipeline(
            "composite.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_draw_pipeline = create_graphics_pipeline(
            "present.vert", "composite.frag", m_bindings.pipeline_layout, 0);
        m_radix_offsets_pipeline = m_device_backend->create_compute_pipeline(
            "radix_offsets.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_tile_count_pipeline = m_device_backend->create_compute_pipeline(
            "tile_count.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_tile_scan_pipeline = m_device_backend->create_compute_pipeline(
            "tile_scan.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_tile_emit_pipeline = m_device_backend->create_compute_pipeline(
            "tile_emit.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
        m_tile_ranges_pipeline = m_device_backend->create_compute_pipeline(
            "tile_ranges.comp", m_bindings.pipeline_layout, m_options.m_shader_directory);
    }

    VkPipeline create_graphics_pipeline(const char* vertex_name,
                                        const char* fragment_name,
                                        VkPipelineLayout layout,
                                        std::uint32_t subpass) {
        const auto K_VERTEX =
            m_device_backend->load_shader_module(m_options.m_shader_directory, vertex_name);
        VkShaderModule fragment {};
        try {
            fragment =
                m_device_backend->load_shader_module(m_options.m_shader_directory, fragment_name);
        } catch (...) {
            vkDestroyShaderModule(m_device, K_VERTEX, nullptr);
            throw;
        }
        const std::array<VkPipelineShaderStageCreateInfo, 2> K_STAGES {
            { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              nullptr,
              0,
              VK_SHADER_STAGE_VERTEX_BIT,
              K_VERTEX,
              "main",
              nullptr },
             { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              nullptr,
              0,
              VK_SHADER_STAGE_FRAGMENT_BIT,
              fragment,
              "main",
              nullptr } } };
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
        VkPipelineColorBlendAttachmentState blend {};
        blend.colorWriteMask = VkColorComponentFlags { VK_COLOR_COMPONENT_R_BIT } |
                               VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        auto blending = structure<VkPipelineColorBlendStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;
        const std::array<VkDynamicState, 2> K_DYNAMIC_STATES { VK_DYNAMIC_STATE_VIEWPORT,
                                                              VK_DYNAMIC_STATE_SCISSOR };
        auto dynamic = structure<VkPipelineDynamicStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = K_DYNAMIC_STATES.data();
        auto info = structure<VkGraphicsPipelineCreateInfo>(
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        info.stageCount = 2;
        info.pStages = K_STAGES.data();
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        info.renderPass = m_render_pass;
        info.subpass = subpass;
        VkPipeline pipeline {};
        const auto K_STATUS =
            vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        vkDestroyShaderModule(m_device, K_VERTEX, nullptr);
        vkDestroyShaderModule(m_device, fragment, nullptr);
        if (K_STATUS != VK_SUCCESS && pipeline) {
            vkDestroyPipeline(m_device, pipeline, nullptr);
        }
        check(K_STATUS, "create graphics pipeline");
        return pipeline;
    }

    void create_frames() {
        for (auto& current : m_frames) {
            if (m_options.m_collect_timings && m_capabilities.m_gpu_timestamps_supported) {
                auto queries =
                    structure<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
                queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
                queries.queryCount = static_cast<std::uint32_t>(K_GPU_TIMING_STAGE_COUNT * 2);
                check(vkCreateQueryPool(m_device, &queries, nullptr, &current.m_timing_queries),
                      "create bounded frame timestamp queries");
            }
            auto pool =
                structure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
            pool.queueFamilyIndex = m_queue_family;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(m_device, &pool, nullptr, &current.m_pool),
                  "create frame command pool");
            auto commands = structure<VkCommandBufferAllocateInfo>(
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            commands.commandPool = current.m_pool;
            commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commands.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(m_device, &commands, &current.m_command),
                  "allocate frame commands");
            auto semaphore =
                structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            check(vkCreateSemaphore(m_device, &semaphore, nullptr, &current.m_acquired),
                  "create acquire semaphore");
            current.m_projected = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_keys_a = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_keys_b = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_histogram = allocate(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_overlap_offsets = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_group_offsets = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_control = allocate(128, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
            current.m_references_a = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            current.m_references_b = allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
            const std::array<VkDescriptorSetLayout, 4> K_LAYOUTS {
                m_bindings.layout, m_bindings.layout, m_bindings.layout, m_bindings.layout };
            auto descriptors = structure<VkDescriptorSetAllocateInfo>(
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            descriptors.descriptorPool = m_bindings.scene_pool;
            descriptors.descriptorSetCount = 4;
            descriptors.pSetLayouts = K_LAYOUTS.data();
            check(vkAllocateDescriptorSets(m_device, &descriptors, current.m_descriptors.data()),
                  "allocate frame descriptors");
        }
    }

    void wait_timeline(std::uint64_t value) {
        if (!value) {
            return;
        }
        auto wait = structure<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        wait.semaphoreCount = 1;
        wait.pSemaphores = &m_timeline;
        wait.pValues = &value;
        check(vkWaitSemaphores(m_device, &wait, 5'000'000'000ULL),
              "wait GPU timeline (5 second timeout)");
    }

    static double elapsed_ms(std::chrono::steady_clock::time_point begin) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
            .count();
    }

    void begin_timing(Frame& current, GpuTimingStage stage) {
        if (!current.m_timing_queries) {
            return;
        }
        const auto K_INDEX = static_cast<std::uint32_t>(stage);
        if (current.m_timing_pending[K_INDEX]) {
            throw std::logic_error("uncollected GPU timing pair reused");
        }
        vkCmdResetQueryPool(current.m_command, current.m_timing_queries, K_INDEX * 2, 2);
        vkCmdWriteTimestamp2(current.m_command,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             current.m_timing_queries,
                             K_INDEX * 2);
    }

    void end_timing(Frame& current, GpuTimingStage stage) {
        if (!current.m_timing_queries) {
            return;
        }
        const auto K_INDEX = static_cast<std::uint32_t>(stage);
        vkCmdWriteTimestamp2(current.m_command,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             current.m_timing_queries,
                             K_INDEX * 2 + 1);
        current.m_timing_pending[K_INDEX] = true;
    }

    void collect_timing(Frame& current) {
        // The caller already completed this frame's submitted commands. Query
        // reads never add WAIT_BIT or a new GPU/CPU synchronization point.
        for (std::uint32_t index { 0 }; index < K_GPU_TIMING_STAGE_COUNT; ++index) {
            if (!current.m_timing_pending[index]) {
                continue;
            }
            std::array<std::uint64_t, 2> ticks {};
            check(vkGetQueryPoolResults(m_device,
                                        current.m_timing_queries,
                                        index * 2,
                                        2,
                                        sizeof(ticks),
                                        ticks.data(),
                                        sizeof(std::uint64_t),
                                        VK_QUERY_RESULT_64_BIT),
                  "read completed GPU timestamp pair");
            const auto K_DURATION = timestamp_duration_ms(ticks[0],
                                                          ticks[1],
                                                          m_capabilities.m_timestamp_valid_bits,
                                                          m_capabilities.m_timestamp_period_ns);
            if (!K_DURATION) {
                throw std::runtime_error(K_DURATION.error().m_message);
            }
            current.m_timing.m_gpu_ms[index] += *K_DURATION;
            current.m_timing_pending[index] = false;
        }
    }

    void descriptors(Frame& current) {
        for (std::size_t pass { 0uz }; pass < 4uz; ++pass) {
            const auto& keys_a = pass < 2 ? current.m_keys_a : current.m_references_a;
            const auto& keys_b = pass < 2 ? current.m_keys_b : current.m_references_b;
            const std::array<VkDescriptorBufferInfo, 10> K_BUFFERS {
                { { m_scene_buffer.buffer, 0, std::max(m_scene_buffer.bytes, 16uz) },
                 { current.m_projected.buffer, 0, current.m_projected.bytes },
                 { pass % 2 == 0 ? keys_a.buffer : keys_b.buffer, 0, keys_a.bytes },
                 { pass % 2 == 0 ? keys_b.buffer : keys_a.buffer, 0, keys_b.bytes },
                 { current.m_histogram.buffer, 0, current.m_histogram.bytes },
                 { current.m_captured.buffer, 0, current.m_captured.bytes },
                 { current.m_overlap_offsets.buffer, 0, current.m_overlap_offsets.bytes },
                 { current.m_group_offsets.buffer, 0, current.m_group_offsets.bytes },
                 { current.m_control.buffer, 0, current.m_control.bytes },
                 { current.m_tile_ranges.buffer, 0, current.m_tile_ranges.bytes } } };
            std::array<VkWriteDescriptorSet, 10> writes {};
            for (std::uint32_t binding { 0 }; binding < writes.size(); ++binding) {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = current.m_descriptors[pass];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &K_BUFFERS[binding];
            }
            vkUpdateDescriptorSets(
                m_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::size_t live_buffer_bytes() const {
        std::size_t bytes =
            static_cast<std::size_t>(m_scene_buffer.allocation_bytes) + scratch_allocation_bytes();
        if (m_uploading) {
            bytes +=
                static_cast<std::size_t>(m_uploading->m_resources.m_staging.allocation_bytes +
                                         m_uploading->m_resources.m_replacement.allocation_bytes);
        }
        for (const auto& current : m_frames) {
            bytes += static_cast<std::size_t>(current.m_captured.allocation_bytes);
            if (current.m_mesh_target) {
                bytes += current.m_mesh_target->allocation_bytes();
            }
        }
        for (const auto& item : m_retired) {
            bytes += static_cast<std::size_t>(item.m_buffer.allocation_bytes);
        }
        return bytes;
    }

    std::size_t scratch_allocation_bytes() const {
        std::size_t bytes = 0;
        for (const auto& current : m_frames) {
            bytes += static_cast<std::size_t>(
                current.m_projected.allocation_bytes + current.m_keys_a.allocation_bytes +
                current.m_keys_b.allocation_bytes + current.m_histogram.allocation_bytes +
                current.m_overlap_offsets.allocation_bytes +
                current.m_group_offsets.allocation_bytes + current.m_control.allocation_bytes +
                current.m_tile_ranges.allocation_bytes + current.m_references_a.allocation_bytes +
                current.m_references_b.allocation_bytes);
        }
        return bytes;
    }

    UploadResources prepare_upload(const SceneSnapshot& snapshot, Vec3d origin) {
        std::size_t count = 0;
        std::size_t mesh_vertices {}, mesh_indices {}, texture_bytes {};
        for (const auto& [K_ID, K_CHUNK] : snapshot->m_chunks) {
            static_cast<void>(K_ID);
            count += K_CHUNK->m_splats.size();
            if (K_CHUNK->m_mesh && K_CHUNK->m_geometry_valid) {
                const auto& mesh = *K_CHUNK->m_mesh;
                mesh_vertices +=
                    mesh.m_baked ? mesh.m_triangles.size() * 3uz : mesh.m_vertices.size();
                if (mesh.m_baked) {
                    texture_bytes += mesh.m_baked.value().m_rgba.size() * 16uz;
                }
                mesh_indices += K_CHUNK->m_mesh->m_triangles.size() * 3uz;
            }
        }
        if (count > m_options.m_max_splats) {
            throw std::runtime_error("scene exceeds configured Gaussian count");
        }
        const auto K_STRIDE = m_capabilities.m_splat_stride_bytes;
        const auto K_VERTEX_OFFSET = std::max(count * K_STRIDE, 16uz);
        const auto K_INDEX_OFFSET = K_VERTEX_OFFSET + mesh_vertices * sizeof(MeshGpuVertex);
        const auto K_TEXTURE_OFFSET = K_INDEX_OFFSET + mesh_indices * sizeof(std::uint32_t);
        const auto K_BYTES = K_TEXTURE_OFFSET + texture_bytes;
        if (K_BYTES > m_properties.limits.maxStorageBufferRange) {
            throw std::runtime_error("Scene exceeds storage-buffer address range");
        }
        const auto K_LIVE = live_buffer_bytes();
        if (K_LIVE > m_options.m_residency_bytes ||
            K_BYTES > (m_options.m_residency_bytes - K_LIVE) / 2) {
            throw std::runtime_error(
                "atomic replacement exceeds old + new + staging + scratch residency budget");
        }
        auto staging = allocate(K_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        auto replacement =
            allocate(K_BYTES,
                     VkBufferUsageFlags { VK_BUFFER_USAGE_STORAGE_BUFFER_BIT } |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     false,
                     staging.allocation_bytes);
        std::map<ChunkId, std::size_t> resident_offsets;
        if (m_options.m_reuse_resident_chunks && m_visible_scene && origin == m_scene_origin &&
            snapshot->m_epoch == m_visible_scene->m_epoch &&
            snapshot->m_world_frame.has_value() == m_visible_scene->m_world_frame.has_value() &&
            (!snapshot->m_world_frame || snapshot->m_world_frame->m_document_sha256 ==
                                             m_visible_scene->m_world_frame->m_document_sha256)) {
            std::size_t offset = 0;
            for (const auto& [K_ID, K_CHUNK] : m_visible_scene->m_chunks) {
                resident_offsets.emplace(K_ID, offset);
                offset += K_CHUNK->m_splats.size() * K_STRIDE;
            }
        }
        std::vector<UploadRegion> regions;
        auto* destination = static_cast<std::byte*>(staging.mapped);
        std::size_t index = 0;
        for (const auto& [K_ID, K_CHUNK] : snapshot->m_chunks) {
            if (K_CHUNK->m_splats.empty()) {
                continue;
            }
            const auto K_CHUNK_BYTES = K_CHUNK->m_splats.size() * K_STRIDE;
            const auto K_RESIDENT = resident_offsets.find(K_ID);
            if (K_RESIDENT != resident_offsets.end() &&
                m_visible_scene->m_chunks.at(K_ID) == K_CHUNK) {
                regions.push_back({ K_RESIDENT->second, index * K_STRIDE, K_CHUNK_BYTES, true });
                index += K_CHUNK->m_splats.size();
                continue;
            }
            regions.push_back({ index * K_STRIDE, index * K_STRIDE, K_CHUNK_BYTES, false });
            const auto K_TRANSFORM = scene_world_from_chunk(*snapshot, *K_CHUNK);
            for (const auto& source : K_CHUNK->m_splats) {
                gpu_splat target {};
                const auto K_POINT =
                    transform_point(K_TRANSFORM,
                                    { source.m_mean[0], source.m_mean[1], source.m_mean[2] }) -
                    origin;
                if (std::abs(K_POINT.m_x) > 1e7 || std::abs(K_POINT.m_y) > 1e7 ||
                    std::abs(K_POINT.m_z) > 1e7) {
                    throw std::runtime_error(
                        "scene exceeds camera-local float extent; spatial streaming is required");
                }
                target.mean_opacity = floats(K_POINT);
                target.mean_opacity[3] = source.m_opacity;
                target.scale_degree = { static_cast<float>(source.m_scale[0] * K_TRANSFORM.m_scale),
                                       static_cast<float>(source.m_scale[1] * K_TRANSFORM.m_scale),
                                       static_cast<float>(source.m_scale[2] * K_TRANSFORM.m_scale),
                                       static_cast<float>(source.m_sh_degree) };
                target.rotation = floats(multiply(K_TRANSFORM.m_rotation,
                                                  { source.m_rotation[0],
                                                   source.m_rotation[1],
                                                   source.m_rotation[2],
                                                   source.m_rotation[3] }));
                target.world_to_sh = floats(Quatd { K_TRANSFORM.m_rotation.m_w,
                                                   -K_TRANSFORM.m_rotation.m_x,
                                                   -K_TRANSFORM.m_rotation.m_y,
                                                   -K_TRANSFORM.m_rotation.m_z });
                target.sh = source.m_sh;
                if (m_options.m_splat_storage == GpuSplatStorage::FP32) {
                    std::memcpy(destination + index * K_STRIDE, &target, sizeof(target));
                } else {
                    const auto K_SH = pack_sh_binary16(source.m_sh);
                    if (!K_SH) {
                        throw std::invalid_argument(K_SH.error().m_message);
                    }
                    const detail::gpu_splat_packed_sh K_PACKED { target.mean_opacity,
                                                                target.scale_degree,
                                                                target.rotation,
                                                                target.world_to_sh,
                                                                *K_SH };
                    std::memcpy(destination + index * K_STRIDE, &K_PACKED, sizeof(K_PACKED));
                }
                ++index;
            }
        }
        std::size_t vertex_index {}, triangle_index {}, texture_offset { K_TEXTURE_OFFSET };
        for (const auto& [K_ID, K_CHUNK] : snapshot->m_chunks) {
            static_cast<void>(K_ID);
            if (!K_CHUNK->m_mesh || !K_CHUNK->m_geometry_valid) {
                continue;
            }
            const auto& mesh = *K_CHUNK->m_mesh;
            const auto K_BASE_VERTEX = vertex_index;
            const auto K_TRANSFORM = scene_world_from_chunk(*snapshot, *K_CHUNK);
            const auto K_WRITE_VERTEX = [&](const FusionVertex& source, std::array<float, 2> uv) {
                const auto K_POSITION =
                    transform_point(
                        K_TRANSFORM,
                        { source.m_position[0], source.m_position[1], source.m_position[2] }) -
                    origin;
                if (std::abs(K_POSITION.m_x) > 1e7 || std::abs(K_POSITION.m_y) > 1e7 ||
                    std::abs(K_POSITION.m_z) > 1e7) {
                    throw std::runtime_error("Mesh exceeds camera-local float extent");
                }
                const auto K_NORMAL =
                    rotate(K_TRANSFORM.m_rotation,
                           { source.m_normal[0], source.m_normal[1], source.m_normal[2] });
                MeshGpuVertex target { { static_cast<float>(K_POSITION.m_x),
                                       static_cast<float>(K_POSITION.m_y),
                                       static_cast<float>(K_POSITION.m_z) },
                                      { static_cast<float>(K_NORMAL.m_x),
                                       static_cast<float>(K_NORMAL.m_y),
                                       static_cast<float>(K_NORMAL.m_z) } };
                target.m_uv = uv;
                if (mesh.m_baked) {
                    target.m_texture = {
                        static_cast<std::uint32_t>(texture_offset / 4uz),
                        mesh.m_baked->m_width,
                        mesh.m_baked->m_height |
                            (mesh.m_baked->m_portable_parent_sha256.empty() ? 0U : 0x80000000U) };
                }
                std::memcpy(destination + K_VERTEX_OFFSET + vertex_index++ * sizeof(MeshGpuVertex),
                            &target,
                            sizeof(target));
            };
            std::size_t corner {};
            if (mesh.m_baked) {
                const auto& appearance = *mesh.m_baked;
                for (const auto& triangle : mesh.m_triangles) {
                    for (auto source_index : triangle) {
                        K_WRITE_VERTEX(mesh.m_vertices[source_index],
                                       appearance.m_corner_uv[corner++]);
                    }
                }
                const auto K_SIZE = appearance.m_rgba.size() * 16uz;
                std::memcpy(destination + texture_offset, appearance.m_rgba.data(), K_SIZE);
                texture_offset += K_SIZE;
            } else {
                for (const auto& source : mesh.m_vertices) {
                    K_WRITE_VERTEX(source, {});
                }
            }
            corner = 0uz;
            for (const auto& triangle : mesh.m_triangles) {
                for (auto source_index : triangle) {
                    const auto K_INDEX = static_cast<std::uint32_t>(
                        K_BASE_VERTEX + (mesh.m_baked ? corner++ : source_index));
                    std::memcpy(destination + K_INDEX_OFFSET + triangle_index++ * sizeof(K_INDEX),
                                &K_INDEX,
                                sizeof(K_INDEX));
                }
            }
        }
        if (K_BYTES > K_VERTEX_OFFSET) {
            regions.push_back({ K_VERTEX_OFFSET, K_VERTEX_OFFSET, K_BYTES - K_VERTEX_OFFSET, false });
        }
        if (regions.empty()) {
            regions.push_back({ 0, 0, K_BYTES, false });
        }
        return { std::move(staging),
                std::move(replacement),
                K_BYTES,
                0,
                count,
                origin,
                std::move(regions),
                K_VERTEX_OFFSET,
                K_INDEX_OFFSET,
                mesh_vertices,
                mesh_indices };
    }

    void copy_upload(Frame& current, UploadResources& resources, std::size_t limit) {
        // The visible buffer's last writes may have been made visible only to
        // compute. Also expose them to device-to-device transfer reads.
        memory_barrier(current.m_command,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);
        const auto K_END =
            resources.m_copied + std::min(resources.m_bytes - resources.m_copied, limit);
        for (const auto& region : resources.m_regions) {
            const auto K_FIRST = std::max(resources.m_copied, region.m_target_offset);
            const auto K_LAST = std::min(K_END, region.m_target_offset + region.m_bytes);
            if (K_FIRST >= K_LAST) {
                continue;
            }
            const VkBufferCopy K_COPY { region.m_source_offset + K_FIRST - region.m_target_offset,
                                       K_FIRST,
                                       K_LAST - K_FIRST };
            vkCmdCopyBuffer(current.m_command,
                            region.m_resident ? m_scene_buffer.buffer : resources.m_staging.buffer,
                            resources.m_replacement.buffer,
                            1,
                            &K_COPY);
            if (region.m_resident) {
                current.m_timing.m_resident_copy_bytes += K_LAST - K_FIRST;
            } else {
                current.m_timing.m_host_copy_bytes += K_LAST - K_FIRST;
            }
        }
        resources.m_copied = K_END;
    }

    void finish_upload(UploadResources& resources, std::uint64_t completion) {
        if (m_scene_buffer.buffer) {
            m_retired.push_back({ completion, std::move(m_scene_buffer) });
        }
        m_retired.push_back({ completion, std::move(resources.m_staging) });
        m_scene_buffer = std::move(resources.m_replacement);
        m_splat_count = resources.m_count;
        m_mesh_vertex_offset = resources.m_mesh_vertex_offset;
        m_mesh_index_offset = resources.m_mesh_index_offset;
        m_mesh_vertices = resources.m_mesh_vertices;
        m_mesh_indices = resources.m_mesh_indices;
        m_scene_origin = resources.m_origin;
    }

    void
    upload(Frame& current, const SceneSnapshot& snapshot, Vec3d origin, std::uint64_t completion) {
        auto resources = prepare_upload(snapshot, origin);
        begin_timing(current, GpuTimingStage::UPLOAD);
        copy_upload(current, resources, resources.m_bytes);
        memory_barrier(current.m_command,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                           VK_ACCESS_2_INDEX_READ_BIT);
        end_timing(current, GpuTimingStage::UPLOAD);
        finish_upload(resources, completion);
    }

    push_parameters parameters(const CameraView& camera,
                               const std::array<float, 4>& intrinsics) const {
        const auto K_POSITION = camera.m_position - m_scene_origin;
        const auto K_AFFINE_ROW = [&](Vec3d basis) {
            auto row = floats(basis);
            row[3] = static_cast<float>(-dot(basis, K_POSITION));
            return row;
        };
        return { K_AFFINE_ROW(camera.m_right),
                K_AFFINE_ROW(camera.m_down),
                K_AFFINE_ROW(camera.m_forward),
                floats(K_POSITION),
                intrinsics,
                { static_cast<float>(m_extent.width),
                 static_cast<float>(m_extent.height),
                 camera.m_near_plane,
                 camera.m_far_plane },
                { static_cast<std::uint32_t>(m_splat_count),
                 0,
                 static_cast<std::uint32_t>((m_splat_count + 255) / 256),
                 static_cast<std::uint32_t>(m_options.m_max_tile_references) },
                { 0, 0, (m_extent.width + 15) / 16, (m_extent.height + 15) / 16 } };
    }

    void bind_compute(Frame& current, const push_parameters& params, std::size_t descriptor = 0) {
        vkCmdBindDescriptorSets(current.m_command,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_bindings.pipeline_layout,
                                0,
                                1,
                                &current.m_descriptors[descriptor],
                                0,
                                nullptr);
        vkCmdPushConstants(current.m_command,
                           m_bindings.pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(params),
                           &params);
    }

    void record_sort(Frame& current, push_parameters& params, bool tile_keys) {
        const std::size_t K_DESCRIPTOR = tile_keys ? 2 : 0;
        std::uint32_t passes = 8;
        if (tile_keys) {
            const auto K_TILE_COUNT = static_cast<std::uint64_t>((m_extent.width + 15) / 16) *
                                      ((m_extent.height + 15) / 16);
            if (K_TILE_COUNT == 0 || K_TILE_COUNT - 1 > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("tile sort keys exceed 32-bit range");
            }
            passes = tile_radix_passes(static_cast<std::uint32_t>(K_TILE_COUNT - 1));
            if (m_options.m_collect_timings) {
                current.m_timing.m_tile_sort_passes = passes;
            }
        }
        for (std::uint32_t pass { 0 }; pass < passes; ++pass) {
            params.count_shift[1] = pass * 4;
            bind_compute(current, params, K_DESCRIPTOR + pass % 2);
            vkCmdBindPipeline(
                current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_histogram_pipeline);
            vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
            compute_barrier(current.m_command);
            vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_scan_pipeline);
            vkCmdDispatch(current.m_command, 16, 1, 1);
            compute_barrier(current.m_command);
            vkCmdBindPipeline(
                current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_radix_offsets_pipeline);
            vkCmdDispatch(current.m_command, (params.count_shift[2] + 15) / 16, 1, 1);
            compute_barrier(current.m_command);
            vkCmdBindPipeline(
                current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatter_pipeline);
            vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
            compute_barrier(current.m_command);
        }
    }

    void record_projection(Frame& current, push_parameters& params) {
        begin_timing(current, GpuTimingStage::PROJECTION);
        bind_compute(current, params);
        vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_project_pipeline);
        vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
        compute_barrier(current.m_command);
        end_timing(current, GpuTimingStage::PROJECTION);
        begin_timing(current, GpuTimingStage::DEPTH_SORT);
        record_sort(current, params, false);
        end_timing(current, GpuTimingStage::DEPTH_SORT);
    }

    void record_count(Frame& current, const push_parameters& params) {
        begin_timing(current, GpuTimingStage::COUNT_SCAN);
        compute_barrier(current.m_command);
        bind_compute(current, params);
        vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_tile_count_pipeline);
        vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
        compute_barrier(current.m_command);
        vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_tile_scan_pipeline);
        vkCmdDispatch(current.m_command, 1, 1, 1);
        memory_barrier(current.m_command,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_HOST_BIT,
                       VK_ACCESS_2_HOST_READ_BIT);
        end_timing(current, GpuTimingStage::COUNT_SCAN);
    }

    void collect_completed(std::uint64_t completed) {
        if (m_options.m_collect_timings) {
            for (std::size_t index { 0uz }; index < m_frames.size(); ++index) {
                auto& current = m_frames[index];
                if (current.m_timing_finished && current.m_completion <= completed) {
                    collect_timing(current);
                    m_diagnostics.m_completed_timings[index] = current.m_timing;
                    current.m_timing_finished = false;
                }
            }
        }
        while (!m_retired.empty() && m_retired.front().m_timeline <= completed) {
            m_retired.pop_front();
        }
        m_sink.retire_completed(completed);
        m_diagnostics.m_completed_timeline = completed;
    }

    void grow_buffer(gpu_buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        if (buffer.bytes >= bytes) {
            return;
        }
        // Only this completed frame owns the old scratch allocation.
        buffer = {};
        buffer = allocate(bytes, usage, false);
    }

    void grow_splat_scratch(Frame& current) {
        grow_buffer(current.m_projected,
                    std::max<std::size_t>(m_splat_count * 48, 16),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        for (auto* keys : { &current.m_keys_a, &current.m_keys_b }) {
            grow_buffer(*keys,
                        std::max<std::size_t>(m_splat_count * 8, 16),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        grow_buffer(current.m_overlap_offsets,
                    std::max<std::size_t>(m_splat_count * 4, 16),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        grow_buffer(current.m_group_offsets,
                    std::max<std::size_t>(((m_splat_count + 255) / 256) * 4, 16),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        grow_buffer(current.m_histogram,
                    std::max<std::size_t>(((m_splat_count + 255) / 256) * 64, 64),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    void submit_count(Frame& current, std::uint64_t completion) {
        check(vkEndCommandBuffer(current.m_command), "end tile-count commands");
        auto command =
            structure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        command.commandBuffer = current.m_command;
        auto signal = structure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signal.semaphore = m_timeline;
        signal.value = completion;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        auto submission = structure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submission.commandBufferInfoCount = 1;
        submission.pCommandBufferInfos = &command;
        submission.signalSemaphoreInfoCount = 1;
        submission.pSignalSemaphoreInfos = &signal;
        check(vkQueueSubmit2(m_queue, 1, &submission, VK_NULL_HANDLE), "submit tile-count prepass");
        m_submitted_timeline = completion;
        current.m_completion = completion;
        const auto K_WAIT_BEGIN = m_options.m_collect_timings
                                      ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point {};
        wait_timeline(completion);
        if (m_options.m_collect_timings) {
            current.m_timing.m_cpu_count_wait_ms += elapsed_ms(K_WAIT_BEGIN);
            ++current.m_timing.m_count_submissions;
            collect_timing(current);
        }
        collect_completed(completion);
    }

    void record_tiles(Frame& current, push_parameters& params, std::uint32_t count) {
        begin_timing(current, GpuTimingStage::TILE_INDEX);
        vkCmdFillBuffer(
            current.m_command, current.m_tile_ranges.buffer, 0, current.m_tile_ranges.bytes, 0);
        memory_barrier(current.m_command,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        if (count) {
            bind_compute(current, params);
            vkCmdBindPipeline(
                current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_tile_emit_pipeline);
            vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
            compute_barrier(current.m_command);
            params.count_shift[0] = count;
            params.count_shift[2] = (count + 255) / 256;
            record_sort(current, params, true);
            bind_compute(current, params, 2);
            vkCmdBindPipeline(
                current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_tile_ranges_pipeline);
            vkCmdDispatch(current.m_command, params.count_shift[2], 1, 1);
            compute_barrier(current.m_command);
        } else {
            params.count_shift[0] = 0;
        }
        end_timing(current, GpuTimingStage::TILE_INDEX);
    }

    void record_composition(Frame& current, push_parameters params, bool draw_appearance) {
        begin_timing(current, GpuTimingStage::COMPOSITION);
        if (!draw_appearance) {
            params.count_shift[0] = 0;
        }
        vkCmdBindDescriptorSets(current.m_command,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_bindings.pipeline_layout,
                                0,
                                1,
                                &current.m_descriptors[2],
                                0,
                                nullptr);
        vkCmdPushConstants(current.m_command,
                           m_bindings.pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(params),
                           &params);
        vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_COMPUTE, m_composite_pipeline);
        vkCmdDispatch(current.m_command, params.tiles[2], params.tiles[3], 1);
        memory_barrier(current.m_command,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT);
        end_timing(current, GpuTimingStage::COMPOSITION);
        if (m_options.m_collect_timings) {
            ++current.m_timing.m_tile_batches;
        }
    }

    void
    copy_screenshot(VkCommandBuffer command, std::uint32_t image_index, const gpu_buffer& target) {
        auto barrier = structure<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_images[image_index];
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        auto dependency = structure<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { m_extent.width, m_extent.height, 1 };
        vkCmdCopyImageToBuffer(command,
                               m_images[image_index],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               target.buffer,
                               1,
                               &region);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(command, &dependency);
        memory_barrier(command,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_HOST_BIT,
                       VK_ACCESS_2_HOST_READ_BIT);
    }

    void write_screenshot(const gpu_buffer& buffer) {
        if (!m_screenshot_path) {
            throw std::logic_error("Screenshot requested without a destination");
        }
        std::ofstream output(*m_screenshot_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write screenshot");
        }
        output << "P6\n" << m_extent.width << ' ' << m_extent.height << "\n255\n";
        const auto* pixels = static_cast<const char*>(buffer.mapped);
        const bool K_BGRA = m_format == VK_FORMAT_B8G8R8A8_UNORM;
        for (std::size_t index { 0uz };
             index < static_cast<std::size_t>(m_extent.width) * m_extent.height;
             ++index) {
            const std::array<char, 3> K_RGB { pixels[index * 4 + (K_BGRA ? 2 : 0)],
                                             pixels[index * 4 + 1],
                                             pixels[index * 4 + (K_BGRA ? 0 : 2)] };
            output.write(K_RGB.data(), 3);
        }
        if (!output) {
            throw std::runtime_error("screenshot write failed");
        }
        m_screenshot_path.reset();
    }

    bool refresh_swapchain() {
        if (!m_swapchain_backend) {
            m_swapchain_backend =
                std::make_unique<vulkan_swapchain>(*m_device_backend, m_surface, m_format);
        }
        const auto K_NEXT_EXTENT = m_swapchain_backend->extent_for_window(m_window);
        if (!K_NEXT_EXTENT.width || !K_NEXT_EXTENT.height) {
            return false;
        }
        const auto K_PIXEL_BYTES =
            static_cast<VkDeviceSize>(K_NEXT_EXTENT.width) * K_NEXT_EXTENT.height * 16;
        if (K_PIXEL_BYTES > m_properties.limits.maxStorageBufferRange ||
            (K_NEXT_EXTENT.width + 15) / 16 > m_properties.limits.maxComputeWorkGroupCount[0] ||
            (K_NEXT_EXTENT.height + 15) / 16 > m_properties.limits.maxComputeWorkGroupCount[1]) {
            throw std::runtime_error("framebuffer exceeds composition buffer or dispatch limits");
        }
        std::array<gpu_buffer, 2> next_captured, next_tile_ranges;
        std::size_t staged_bytes = 0;
        const auto K_TILE_RANGE_BYTES = static_cast<VkDeviceSize>((K_NEXT_EXTENT.width + 15) / 16) *
                                        ((K_NEXT_EXTENT.height + 15) / 16) * 8;
        for (std::size_t index { 0uz }; index < m_frames.size(); ++index) {
            if (m_frames[index].m_captured.bytes < K_PIXEL_BYTES) {
                next_captured[index] =
                    allocate(K_PIXEL_BYTES,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             false,
                             staged_bytes);
                staged_bytes += static_cast<std::size_t>(next_captured[index].allocation_bytes);
            }
            if (m_frames[index].m_tile_ranges.bytes < K_TILE_RANGE_BYTES) {
                next_tile_ranges[index] =
                    allocate(K_TILE_RANGE_BYTES,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             false,
                             staged_bytes);
                staged_bytes += static_cast<std::size_t>(next_tile_ranges[index].allocation_bytes);
            }
        }
        if (m_swapchain && m_submitted_timeline) {
            ++m_diagnostics.m_interactive_device_idle_waits;
            check(vkDeviceWaitIdle(m_device), "wait before resizing composition scratch");
            collect_completed(m_submitted_timeline);
        }
        try {
            if (!m_swapchain_backend->create(m_window, K_NEXT_EXTENT)) {
                return false;
            }
        } catch (...) {
            if (!m_swapchain_backend->swapchain()) {
                m_swapchain = VK_NULL_HANDLE;
                m_images = {};
                m_views = {};
                m_framebuffers = {};
                m_present_semaphores = {};
                m_minimum_images = 0;
                m_diagnostics.m_framebuffer_width = 0;
                m_diagnostics.m_framebuffer_height = 0;
            }
            throw;
        }
        for (std::size_t index { 0uz }; index < m_frames.size(); ++index) {
            if (next_captured[index].buffer) {
                m_frames[index].m_captured = std::move(next_captured[index]);
            }
            if (next_tile_ranges[index].buffer) {
                m_frames[index].m_tile_ranges = std::move(next_tile_ranges[index]);
            }
        }
        m_region_plan.reset();
        m_swapchain = m_swapchain_backend->swapchain();
        m_render_pass = m_swapchain_backend->render_pass();
        m_format = m_swapchain_backend->format();
        m_extent = m_swapchain_backend->extent();
        m_minimum_images = m_swapchain_backend->minimum_images();
        m_screenshot_supported = m_swapchain_backend->screenshot_supported();
        m_images = m_swapchain_backend->images();
        m_views = m_swapchain_backend->image_views();
        m_framebuffers = m_swapchain_backend->framebuffers();
        m_present_semaphores = m_swapchain_backend->present_semaphores();
        m_diagnostics.m_framebuffer_bytes = 0;
        for (const auto& current : m_frames) {
            m_diagnostics.m_framebuffer_bytes +=
                static_cast<std::size_t>(current.m_captured.allocation_bytes);
        }
        m_diagnostics.m_scratch_bytes = scratch_allocation_bytes();
        m_diagnostics.m_framebuffer_width = m_extent.width;
        m_diagnostics.m_framebuffer_height = m_extent.height;
        m_capabilities.m_fp32_compositing = true;
        return true;
    }

    void prepare_mesh_target(Frame& current) {
        if (!m_mesh_pass) {
            m_mesh_pass = std::make_unique<MeshPass>(*m_device_backend);
            m_mesh_pass->initialize(m_options.m_shader_directory, m_bindings.layout);
        }
        if (!current.m_mesh_target || current.m_mesh_target->m_extent.width != m_extent.width ||
            current.m_mesh_target->m_extent.height != m_extent.height) {
            // This frame slot's previous submission completed before target reuse.
            const auto K_LIVE = live_buffer_bytes();
            if (K_LIVE > m_options.m_residency_bytes) {
                throw std::runtime_error("Mesh target residency exhausted");
            }
            auto target =
                m_mesh_pass->create_target(m_extent, m_options.m_residency_bytes - K_LIVE);
            current.m_mesh_target = std::move(target);
        }
    }

    void draw(const CameraView& camera, bool draw_appearance, SceneRepresentation representation) {
        if (representation != SceneRepresentation::CAPTURED_APPEARANCE &&
            representation != SceneRepresentation::OBSERVED_MESH &&
            representation != SceneRepresentation::BAKED_APPEARANCE) {
            throw std::invalid_argument("Unknown scene representation");
        }
        const bool K_MESH_VIEW = representation != SceneRepresentation::CAPTURED_APPEARANCE;
        const auto K_BEGIN = std::chrono::steady_clock::now();
        if ((SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0) {
            return;
        }
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        if (width <= 0 || height <= 0) {
            return;
        }
        if (!m_swapchain || m_extent.width != static_cast<std::uint32_t>(width) ||
            m_extent.height != static_cast<std::uint32_t>(height)) {
            if (!refresh_swapchain()) {
                return;
            }
        }
        // Reject invalid cameras before admitting updates or recording GPU work.
        const auto K_CHECKED_CAMERA = raster_intrinsics(camera, m_extent.width, m_extent.height);
        if (!K_CHECKED_CAMERA) {
            throw std::runtime_error(K_CHECKED_CAMERA.error().m_message);
        }
        auto& current = m_frames[m_frame_index];
        const auto K_FRAME_WAIT_BEGIN = m_options.m_collect_timings
                                            ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point {};
        wait_timeline(current.m_completion);
        const auto K_FRAME_WAIT_MS =
            m_options.m_collect_timings ? elapsed_ms(K_FRAME_WAIT_BEGIN) : 0;
        std::uint64_t completed = 0;
#if defined(WE_VULKAN_TEST_FAULTS)
        if (std::exchange(detail::inject_device_loss_on_next_frame, false)) {
            // The fault is synthetic: drain real submissions before the test tears down
            // this healthy device. A real lost device reports its own completion failure.
            check(vkDeviceWaitIdle(m_device), "quiesce synthetic device loss");
            check(VK_ERROR_DEVICE_LOST, "injected retirement timeline query");
        }
#endif
        check(vkGetSemaphoreCounterValue(m_device, m_timeline, &completed),
              "query retirement timeline");
        collect_completed(completed);
        if (K_MESH_VIEW && draw_appearance) {
            prepare_mesh_target(current);
        }
        std::uint32_t image_index = 0;
        const auto K_ACQUIRE_BEGIN = m_options.m_collect_timings
                                         ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point {};
        VkResult acquired {};
#if defined(WE_VULKAN_TEST_FAULTS)
        const auto K_DELAY =
            std::exchange(detail::inject_acquire_delay, detail::frame_acquire_delay::NONE);
        if (K_DELAY != detail::frame_acquire_delay::NONE) {
            acquired = K_DELAY == detail::frame_acquire_delay::TIMEOUT ? VK_TIMEOUT : VK_NOT_READY;
        } else {
#endif
            acquired = vkAcquireNextImageKHR(m_device,
                                             m_swapchain,
                                             5'000'000'000ULL,
                                             current.m_acquired,
                                             VK_NULL_HANDLE,
                                             &image_index);
        }
        const auto K_ACQUIRE_MS = m_options.m_collect_timings ? elapsed_ms(K_ACQUIRE_BEGIN) : 0;
        // No image or semaphore signal was acquired. Retain the frame slot,
        // queued update and screenshot request for a later draw.
        if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
            return;
        }
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            refresh_swapchain();
            return;
        }
        if (acquired != VK_SUBOPTIMAL_KHR) {
            check(acquired, "acquire swapchain image");
        }
        if (m_options.m_collect_timings) {
            current.m_timing = {};
            current.m_timing.m_sequence = m_diagnostics.m_submitted_frames + 1;
            current.m_timing.m_width = m_extent.width;
            current.m_timing.m_height = m_extent.height;
            current.m_timing.m_gpu_valid = current.m_timing_queries != VK_NULL_HANDLE;
            current.m_timing.m_cpu_frame_wait_ms = K_FRAME_WAIT_MS;
            current.m_timing.m_cpu_acquire_ms = K_ACQUIRE_MS;
            current.m_timing_pending = {};
        }
        check(vkResetCommandPool(m_device, current.m_pool, 0), "reset frame commands");
        auto command_begin =
            structure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        command_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(current.m_command, &command_begin), "begin frame commands");
        auto completion = m_submitted_timeline + 1;
        std::optional<AdmittedUpdate> completed_update;
        if (!m_uploading) {
            if (auto next = m_sink.take_next()) {
                try {
                    const auto K_OFFSET = camera.m_position - m_scene_origin;
                    const auto K_ORIGIN = m_visible_scene && dot(K_OFFSET, K_OFFSET) <= 100000000
                                              ? m_scene_origin
                                              : camera.m_position;
                    auto resources = prepare_upload(next->m_batch.m_snapshot, K_ORIGIN);
                    m_uploading.emplace(ActiveUpload { std::move(*next), std::move(resources) });
                } catch (const detail::vulkan_error& exception) {
                    if (exception.status() == VK_ERROR_DEVICE_LOST) {
                        throw;
                    }
                    m_sink.fail_update(next->m_ticket,
                                       { ErrorCode::BACKEND_FAILURE, exception.what() });
                } catch (const std::invalid_argument& exception) {
                    m_sink.fail_update(next->m_ticket,
                                       { ErrorCode::INVALID_ARGUMENT, exception.what() });
                } catch (const std::exception& exception) {
                    m_sink.fail_update(next->m_ticket,
                                       { ErrorCode::RESOURCE_EXHAUSTED, exception.what() });
                }
            }
        }
        if (m_uploading) {
            auto& resources = m_uploading->m_resources;
            constexpr std::size_t K_MAX_UPLOAD_BYTES_PER_FRAME { 15uz * 1024uz * 1024uz };
            begin_timing(current, GpuTimingStage::UPLOAD);
            copy_upload(current, resources, K_MAX_UPLOAD_BYTES_PER_FRAME);
            if (resources.m_copied == resources.m_bytes) {
                // Prior copies are ordered on this queue; only the final frame
                // publishes the replacement to compute and to the update sink.
                memory_barrier(current.m_command,
                               VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                               VK_ACCESS_2_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                               VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                                   VK_ACCESS_2_INDEX_READ_BIT);
                finish_upload(resources, completion);
                m_sink.acknowledge_uploaded(m_uploading->m_update.m_ticket, completion);
                completed_update.emplace(std::move(m_uploading->m_update));
                m_uploading.reset();
            }
            end_timing(current, GpuTimingStage::UPLOAD);
        } else if (m_visible_scene && dot(camera.m_position - m_scene_origin,
                                          camera.m_position - m_scene_origin) > 100000000) {
            upload(current, m_visible_scene, camera.m_position, completion);
        }
        grow_splat_scratch(current);
        descriptors(current);
        auto params = parameters(camera, *K_CHECKED_CAMERA);
        std::size_t total_references = 0;
        if (m_splat_count && draw_appearance && !K_MESH_VIEW) {
            record_projection(current, params);
            const auto K_PLANNED = m_region_plan.begin({ params.tiles[2], params.tiles[3] },
                                                       m_options.m_reuse_tile_plans);
            if (!K_PLANNED) {
                throw std::runtime_error(K_PLANNED.error().m_message);
            }
            if (m_options.m_collect_timings) {
                current.m_timing.m_reused_tile_regions = static_cast<std::uint32_t>(*K_PLANNED);
            }
            for (;;) {
                const auto K_CANDIDATE = m_region_plan.next();
                if (!K_CANDIDATE) {
                    throw std::runtime_error(K_CANDIDATE.error().m_message);
                }
                if (!*K_CANDIDATE) {
                    break;
                }
                const auto K_REGION = **K_CANDIDATE;
                params.count_shift[0] = static_cast<std::uint32_t>(m_splat_count);
                params.count_shift[2] = static_cast<std::uint32_t>((m_splat_count + 255) / 256);
                params.tiles = { K_REGION.m_x, K_REGION.m_y, K_REGION.m_width, K_REGION.m_height };
                record_count(current, params);
                submit_count(current, completion);
                const auto K_REFERENCE_COUNT =
                    static_cast<const std::uint32_t*>(current.m_control.mapped)[0];
                const auto K_GROUPS = (K_REFERENCE_COUNT + 255) / 256;
                const auto K_CAPACITY = std::max<std::size_t>(
                    16, ((static_cast<std::size_t>(K_REFERENCE_COUNT) + 4095) / 4096) * 4096 * 8);
                const auto K_HISTOGRAM_BYTES =
                    std::max<std::size_t>(64, static_cast<std::size_t>(K_GROUPS) * 64);
                bool fits = K_REFERENCE_COUNT <= m_options.m_max_tile_references &&
                            K_GROUPS <= m_properties.limits.maxComputeWorkGroupCount[0] &&
                            K_CAPACITY <= m_properties.limits.maxStorageBufferRange;
                if (fits) {
                    auto non_reference_bytes = live_buffer_bytes();
                    for (const auto& buffered : m_frames) {
                        non_reference_bytes -=
                            static_cast<std::size_t>(buffered.m_references_a.allocation_bytes +
                                                     buffered.m_references_b.allocation_bytes);
                    }
                    if (current.m_histogram.bytes < K_HISTOGRAM_BYTES) {
                        non_reference_bytes -=
                            static_cast<std::size_t>(current.m_histogram.allocation_bytes);
                        non_reference_bytes +=
                            static_cast<std::size_t>(m_device_backend->buffer_allocation_bytes(
                                { K_HISTOGRAM_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT }));
                    }
                    // Reserve a checked SDR readback allocation, so requesting a
                    // screenshot cannot turn a valid fullscreen frame into an OOM.
                    non_reference_bytes +=
                        static_cast<std::size_t>(m_device_backend->buffer_allocation_bytes(
                            { static_cast<VkDeviceSize>(m_extent.width) * m_extent.height * 4,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT }));
                    const auto K_PAIR_BYTES =
                        2 * m_device_backend->buffer_allocation_bytes(
                                { K_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
                    fits = non_reference_bytes <= m_options.m_residency_bytes &&
                           K_PAIR_BYTES <= (m_options.m_residency_bytes - non_reference_bytes) /
                                               m_frames.size();
                    if (fits) {
                        // The count timeline completes every earlier frame on
                        // this queue. Trim cached pools against the new share,
                        // including the other frame after histogram growth.
                        const auto K_SHARE =
                            (m_options.m_residency_bytes - non_reference_bytes) / m_frames.size();
                        for (auto& buffered : m_frames) {
                            if (buffered.m_references_a.allocation_bytes +
                                    buffered.m_references_b.allocation_bytes >
                                K_SHARE) {
                                buffered.m_references_a = {};
                                buffered.m_references_b = {};
                                buffered.m_references_a =
                                    allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
                                buffered.m_references_b =
                                    allocate(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
                            }
                        }
                    }
                }
                check(vkResetCommandBuffer(current.m_command, 0), "reset completed count commands");
                check(vkBeginCommandBuffer(current.m_command, &command_begin),
                      "begin tile-region commands");
                completion = m_submitted_timeline + 1;
                if (!fits) {
                    const auto K_SPLIT = m_region_plan.split_current();
                    if (!K_SPLIT) {
                        throw std::runtime_error(K_SPLIT.error().m_message);
                    }
                    continue;
                }
                grow_buffer(current.m_references_a, K_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                grow_buffer(current.m_references_b, K_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                grow_buffer(
                    current.m_histogram, K_HISTOGRAM_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                descriptors(current);
                const VkDescriptorBufferInfo K_OUTPUT {
                    current.m_references_a.buffer, 0, current.m_references_a.bytes };
                auto write =
                    structure<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                write.dstSet = current.m_descriptors[0];
                write.dstBinding = 3;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.pBufferInfo = &K_OUTPUT;
                vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
                memory_barrier(current.m_command,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_ACCESS_2_SHADER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
                record_tiles(current, params, K_REFERENCE_COUNT);
                record_composition(current, params, true);
                total_references += K_REFERENCE_COUNT;
                const auto K_ACCEPTED = m_region_plan.accept_current();
                if (!K_ACCEPTED) {
                    throw std::runtime_error(K_ACCEPTED.error().m_message);
                }
            }
        } else {
            params.count_shift[0] = 0;
            record_tiles(current, params, 0);
            record_composition(current, params, false);
        }
        if (K_MESH_VIEW && m_mesh_indices && draw_appearance) {
            m_mesh_pass->record(current.m_command,
                                *current.m_mesh_target,
                                m_scene_buffer,
                                params,
                                { m_mesh_vertex_offset,
                                 m_mesh_index_offset,
                                 static_cast<std::uint32_t>(m_mesh_indices),
                                 representation == SceneRepresentation::BAKED_APPEARANCE },
                                current.m_captured.buffer,
                                current.m_descriptors[0]);
        }
        m_diagnostics.m_resident_mesh_vertices = 0;
        if (m_visible_scene) {
            for (const auto& [K_ID, K_CHUNK] : m_visible_scene->m_chunks) {
                static_cast<void>(K_ID);
                if (K_CHUNK->m_mesh && K_CHUNK->m_geometry_valid) {
                    m_diagnostics.m_resident_mesh_vertices += K_CHUNK->m_mesh->m_vertices.size();
                }
            }
        }
        m_diagnostics.m_resident_mesh_triangles = m_mesh_indices / 3uz;
        m_diagnostics.m_framebuffer_bytes = 0;
        for (const auto& frame : m_frames) {
            m_diagnostics.m_framebuffer_bytes +=
                static_cast<std::size_t>(frame.m_captured.allocation_bytes);
            if (frame.m_mesh_target) {
                m_diagnostics.m_framebuffer_bytes += frame.m_mesh_target->allocation_bytes();
            }
        }
        m_diagnostics.m_tile_reference_count = total_references;
        m_diagnostics.m_scratch_bytes = scratch_allocation_bytes();
        begin_timing(current, GpuTimingStage::PRESENTATION);
        VkClearValue clear {};
        clear.color = { { 0.045F, 0.051F, 0.063F, 1 } };
        auto pass = structure<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        pass.renderPass = m_render_pass;
        pass.framebuffer = m_framebuffers[image_index];
        pass.renderArea.extent = m_extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(current.m_command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport K_VIEWPORT {
            0, 0, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0, 1 };
        const VkRect2D K_SCISSOR { { 0, 0 }, m_extent };
        vkCmdSetViewport(current.m_command, 0, 1, &K_VIEWPORT);
        vkCmdSetScissor(current.m_command, 0, 1, &K_SCISSOR);
        {
            vkCmdBindPipeline(current.m_command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_draw_pipeline);
            vkCmdBindDescriptorSets(current.m_command,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_bindings.pipeline_layout,
                                    0,
                                    1,
                                    &current.m_descriptors[0],
                                    0,
                                    nullptr);
            vkCmdPushConstants(current.m_command,
                               m_bindings.pipeline_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(params),
                               &params);
            vkCmdDraw(current.m_command, 3, 1, 0, 0);
        }
        if (m_overlay) {
            m_overlay(reinterpret_cast<std::uintptr_t>(current.m_command));
        }
        vkCmdEndRenderPass(current.m_command);
        end_timing(current, GpuTimingStage::PRESENTATION);
        gpu_buffer screenshot;
        if (m_screenshot_path) {
            screenshot = allocate(static_cast<std::size_t>(m_extent.width) * m_extent.height * 4,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  true);
            copy_screenshot(current.m_command, image_index, screenshot);
        }
        check(vkEndCommandBuffer(current.m_command), "end frame commands");
        auto wait = structure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        wait.semaphore = current.m_acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto command =
            structure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        command.commandBuffer = current.m_command;
        std::array<VkSemaphoreSubmitInfo, 2> signals {};
        for (auto& signal : signals) {
            signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        signals[0].semaphore = m_present_semaphores[image_index];
        signals[1].semaphore = m_timeline;
        signals[1].value = completion;
        auto submission = structure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submission.waitSemaphoreInfoCount = 1;
        submission.pWaitSemaphoreInfos = &wait;
        submission.commandBufferInfoCount = 1;
        submission.pCommandBufferInfos = &command;
        submission.signalSemaphoreInfoCount = 2;
        submission.pSignalSemaphoreInfos = signals.data();
        check(vkQueueSubmit2(m_queue, 1, &submission, VK_NULL_HANDLE), "submit viewer frame");
        if (m_splat_count && draw_appearance && !K_MESH_VIEW) {
            const auto K_COMMITTED = m_region_plan.commit();
            if (!K_COMMITTED) {
                throw std::runtime_error(K_COMMITTED.error().m_message);
            }
        }
        m_submitted_timeline = completion;
        current.m_completion = completion;
        if (completed_update) {
            m_visible_scene = completed_update->m_batch.m_snapshot;
            m_sink.acknowledge_visible(completed_update->m_ticket, m_visible_scene, completion);
        }
        auto presentation = structure<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        presentation.waitSemaphoreCount = 1;
        presentation.pWaitSemaphores = &m_present_semaphores[image_index];
        presentation.swapchainCount = 1;
        presentation.pSwapchains = &m_swapchain;
        presentation.pImageIndices = &image_index;
        const auto K_PRESENT_BEGIN = m_options.m_collect_timings
                                         ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point {};
        const auto K_PRESENTED = vkQueuePresentKHR(m_queue, &presentation);
        if (m_options.m_collect_timings) {
            current.m_timing.m_cpu_present_ms = elapsed_ms(K_PRESENT_BEGIN);
        }
        if (m_screenshot_path) {
            wait_timeline(completion);
            write_screenshot(screenshot);
        }
        m_frame_index = (m_frame_index + 1) % m_frames.size();
        ++m_diagnostics.m_submitted_frames;
        current.m_timing_finished = m_options.m_collect_timings;
        m_diagnostics.m_resident_splat_count = m_splat_count;
        m_diagnostics.m_allocated_bytes = live_buffer_bytes();
        m_diagnostics.m_staging_bytes =
            m_uploading
                ? static_cast<std::size_t>(m_uploading->m_resources.m_staging.allocation_bytes)
                : 0;
        for (const auto& item : m_retired) {
            if (item.m_buffer.mapped) {
                m_diagnostics.m_staging_bytes +=
                    static_cast<std::size_t>(item.m_buffer.allocation_bytes);
            }
        }
        m_diagnostics.m_last_cpu_frame_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - K_BEGIN)
                .count();
        if (K_PRESENTED == VK_ERROR_OUT_OF_DATE_KHR || K_PRESENTED == VK_SUBOPTIMAL_KHR ||
            acquired == VK_SUBOPTIMAL_KHR) {
            refresh_swapchain();
        } else {
            check(K_PRESENTED, "present viewer frame");
        }
    }
};

VulkanRenderer::VulkanRenderer(std::unique_ptr<Implementation> impl) : m_impl(std::move(impl)) { }

VulkanRenderer::~VulkanRenderer() = default;

Result<std::unique_ptr<VulkanRenderer>> VulkanRenderer::create(void* sdl_window,
                                                               RendererOptions options) {
    try {
        auto impl = std::make_unique<Implementation>(static_cast<SDL_Window*>(sdl_window),
                                                     std::move(options));
        impl->initialize();
        return std::unique_ptr<VulkanRenderer>(new VulkanRenderer(std::move(impl)));
    } catch (const detail::vulkan_error& exception) {
        return fail(exception.status() == VK_ERROR_DEVICE_LOST ? ErrorCode::DEVICE_LOST
                                                               : ErrorCode::BACKEND_FAILURE,
                    exception.what());
    } catch (const std::exception& exception) {
        return fail(ErrorCode::BACKEND_FAILURE, exception.what());
    }
}

RendererUpdateSink& VulkanRenderer::updates() noexcept {
    return m_impl->m_sink;
}

const RendererCapabilities& VulkanRenderer::capabilities() const noexcept {
    return m_impl->m_capabilities;
}

RendererDiagnostics VulkanRenderer::diagnostics() const noexcept {
    auto output = m_impl->m_diagnostics;
    output.m_validation_errors = m_impl->m_validation_errors.load();
    return output;
}

VulkanOverlayContext VulkanRenderer::overlay_context() const noexcept {
    return { reinterpret_cast<std::uintptr_t>(m_impl->m_instance),
            reinterpret_cast<std::uintptr_t>(m_impl->m_physical),
            reinterpret_cast<std::uintptr_t>(m_impl->m_device),
            reinterpret_cast<std::uintptr_t>(m_impl->m_queue),
            reinterpret_cast<std::uintptr_t>(m_impl->m_bindings.overlay_pool),
            reinterpret_cast<std::uintptr_t>(m_impl->m_render_pass),
            m_impl->m_queue_family,
            static_cast<std::uint32_t>(m_impl->m_images.size()),
            m_impl->m_minimum_images,
            0 };
}

void VulkanRenderer::set_overlay_callback(std::function<void(std::uintptr_t)> callback) {
    m_impl->m_overlay = std::move(callback);
}

Result<void> VulkanRenderer::draw(const CameraView& camera,
                                  bool draw_appearance,
                                  SceneRepresentation representation) {
    try {
        m_impl->draw(camera, draw_appearance, representation);
        return {};
    } catch (const detail::vulkan_error& exception) {
        return fail(exception.status() == VK_ERROR_DEVICE_LOST ? ErrorCode::DEVICE_LOST
                                                               : ErrorCode::BACKEND_FAILURE,
                    exception.what());
    } catch (const std::exception& exception) {
        return fail(ErrorCode::BACKEND_FAILURE, exception.what());
    }
}

Result<void> VulkanRenderer::wait_for_idle() {
    const auto K_STATUS = vkDeviceWaitIdle(m_impl->m_device);
    if (K_STATUS != VK_SUCCESS) {
        return fail(K_STATUS == VK_ERROR_DEVICE_LOST ? ErrorCode::DEVICE_LOST
                                                     : ErrorCode::BACKEND_FAILURE,
                    "Vulkan idle wait failed (VkResult " + std::to_string(K_STATUS) + ")");
    }
    try {
        m_impl->collect_completed(m_impl->m_submitted_timeline);
        return {};
    } catch (const detail::vulkan_error& exception) {
        return fail(exception.status() == VK_ERROR_DEVICE_LOST ? ErrorCode::DEVICE_LOST
                                                               : ErrorCode::BACKEND_FAILURE,
                    exception.what());
    } catch (const std::exception& exception) {
        return fail(ErrorCode::BACKEND_FAILURE, exception.what());
    }
}

Result<void> VulkanRenderer::request_screenshot(std::filesystem::path path) {
    if (!m_impl->m_screenshot_supported) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION,
                    "surface does not support screenshot transfer");
    }
    if (path.empty()) {
        return fail(ErrorCode::INVALID_ARGUMENT, "screenshot path is empty");
    }
    m_impl->m_screenshot_path = std::move(path);
    return {};
}

}  // namespace world_explorer
