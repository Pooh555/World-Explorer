#pragma once

#include "world_explorer/core/result.hpp"
#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/render_core/update_sink.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace world_explorer {

enum class SceneRepresentation : std::uint8_t {
    CAPTURED_APPEARANCE,
    OBSERVED_MESH,
    BAKED_APPEARANCE
};

enum class GpuSplatStorage : std::uint8_t { FP32, FP32_GEOMETRY_FP16_SH };

struct RendererOptions {
    std::size_t m_max_splats { 250000uz };
    std::size_t m_residency_bytes { 512uz * 1024uz * 1024uz };
    bool m_validation { true };
    std::filesystem::path m_shader_directory;
    // Per emitted tile batch; larger frames split without dropping contributors.
    std::size_t m_max_tile_references { 8000000uz };
    // Opt-in diagnostics; fixed query pools, no extra completion waits.
    bool m_collect_timings {};
    bool m_reuse_tile_plans { true };
    bool m_reuse_resident_chunks { true };
    // Explicit opt-in until the packing profile is qualified; canonical assets
    // remain unchanged. No FP16 arithmetic or storage feature is required.
    GpuSplatStorage m_splat_storage { GpuSplatStorage::FP32 };
};

// Adapter-only handles. Public scene/core APIs never carry graphics objects.
struct VulkanOverlayContext {
    std::uintptr_t m_instance {}, m_physical_device {}, m_device {}, m_queue {},
        m_descriptor_pool {}, m_render_pass {};
    std::uint32_t m_queue_family {}, m_image_count {}, m_min_image_count {}, m_subpass {};
};

struct RendererCapabilities {
    std::string m_device_name;
    std::uint32_t m_instance_version {}, m_device_version {};
    bool m_validation_enabled {}, m_portability_subset {};
    std::size_t m_max_splats {};
    bool m_fp32_compositing {};
    bool m_gpu_timestamps_supported {};
    std::uint32_t m_timestamp_valid_bits {};
    double m_timestamp_period_ns {};
    GpuSplatStorage m_splat_storage { GpuSplatStorage::FP32 };
    std::size_t m_splat_stride_bytes { 256uz };
};

class VulkanRenderer {
  public:
    static Result<std::unique_ptr<VulkanRenderer>> create(void* sdl_window,
                                                          RendererOptions options);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    RendererUpdateSink& updates() noexcept;
    const RendererCapabilities& capabilities() const noexcept;
    RendererDiagnostics diagnostics() const noexcept;
    VulkanOverlayContext overlay_context() const noexcept;
    // Called on the render owner inside its color render pass. Command handle borrowed for callback
    // only.
    void set_overlay_callback(std::function<void(std::uintptr_t)> callback);
    // Overlay/presentation remain active when appearance is paused for a local
    // reconstruction job. Resident assets and update ownership are retained.
    Result<void>
    draw(const CameraView& camera,
         bool draw_appearance = true,
         SceneRepresentation representation = SceneRepresentation::CAPTURED_APPEARANCE);
    Result<void> wait_for_idle();
    // Save the next successfully presented frame as an RGB PPM; explicit transfer capability is
    // required.
    Result<void> request_screenshot(std::filesystem::path path);

  private:
    struct Implementation;
    explicit VulkanRenderer(std::unique_ptr<Implementation> impl);
    std::unique_ptr<Implementation> m_impl;
};

}  // namespace world_explorer
