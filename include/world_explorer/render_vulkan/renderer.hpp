#ifndef WORLD_EXPLORER_RENDER_VULKAN_RENDERER_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_RENDERER_HPP
#include "world_explorer/core/result.hpp"
#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace world_explorer {
struct renderer_options {
    std::size_t max_splats{250000};
    std::size_t residency_bytes{512ULL * 1024 * 1024};
    bool validation{true};
    std::filesystem::path shader_directory;
    // Per emitted tile batch; larger frames split without dropping contributors.
    std::size_t max_tile_references{8000000};
    // Opt-in diagnostics; fixed query pools, no extra completion waits.
    bool collect_timings{};
    bool reuse_tile_plans{true};
};
// Adapter-only handles. Public scene/core APIs never carry graphics objects.
struct vulkan_overlay_context {
    std::uintptr_t instance{}, physical_device{}, device{}, queue{}, descriptor_pool{},
        render_pass{};
    std::uint32_t queue_family{}, image_count{}, min_image_count{}, subpass{};
};
struct renderer_capabilities {
    std::string device_name;
    std::uint32_t instance_version{}, device_version{};
    bool validation_enabled{}, portability_subset{};
    std::size_t max_splats{};
    bool fp32_compositing{};
    bool gpu_timestamps_supported{};
    std::uint32_t timestamp_valid_bits{};
    double timestamp_period_ns{};
};
class vulkan_renderer {
  public:
    static result<std::unique_ptr<vulkan_renderer>> create(void* sdl_window,
                                                           renderer_options options);
    ~vulkan_renderer();
    vulkan_renderer(const vulkan_renderer&) = delete;
    vulkan_renderer& operator=(const vulkan_renderer&) = delete;
    renderer_update_sink& updates() noexcept;
    const renderer_capabilities& capabilities() const noexcept;
    renderer_diagnostics diagnostics() const noexcept;
    vulkan_overlay_context overlay_context() const noexcept;
    // Called on the render owner inside its color render pass. Command handle borrowed for callback
    // only.
    void set_overlay_callback(std::function<void(std::uintptr_t)> callback);
    // Overlay/presentation remain active when appearance is paused for a local
    // reconstruction job. Resident assets and update ownership are retained.
    result<void> draw(const camera_view& camera, bool draw_appearance = true);
    result<void> wait_for_idle();
    // Save the next successfully presented frame as an RGB PPM; explicit transfer capability is
    // required.
    result<void> request_screenshot(std::filesystem::path path);

  private:
    struct implementation;
    explicit vulkan_renderer(std::unique_ptr<implementation> impl);
    std::unique_ptr<implementation> impl_;
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_VULKAN_RENDERER_HPP
