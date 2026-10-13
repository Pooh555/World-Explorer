#include "job_state.hpp"
#include "preferences.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/navigation/fly_camera.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <utility>
#include <vulkan/vulkan.h>

namespace {
using namespace world_explorer;
using viewer::application_state;
using viewer::is_synthetic_scene;
using viewer::preferences;
using viewer::read_preferences;
using viewer::save_preferences;

template <class handle_type> handle_type native(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<handle_type>)
        // Opaque Vulkan handles cross the public renderer boundary as uintptr_t.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<handle_type>(value);
    else
        return static_cast<handle_type>(value);
}
int viewer_main(int argc, char** argv) {
    std::filesystem::path scene_path, reconstruction_request, review_request, screenshot,
        settings_path, input_trace_path, shader_path = WE_SHADER_DIRECTORY;
    worker_configuration worker{WE_WORKER_SCRIPT};
    std::size_t max_splats = 250000;
    std::size_t residency_mib = 512;
    bool diagnostic_fixture = false, validation = true;
    bool inspect_source = false;
    unsigned frame_limit = 0;
    for (int argument = 1; argument < argc; ++argument) {
        const std::string option = argv[argument];
        if (option == "--help") {
            std::cout
                << "we_viewer [--scene BUNDLE] [--diagnostic-fixture] [--frames N]\n"
                   "          [--screenshot FILE.ppm] [--settings FILE] [--shader-dir DIR]\n"
                   "          [--worker-python EXE] [--worker-script FILE] [--no-validation]\n"
                   "          [--max-splats N] [--residency-mib N]\n";
            std::cout
                << "          [--source-view] (explicit capture-orientation spectator reset)\n";
            std::cout
                << "          [--reconstruction-request JOB.json] [--review-request JOB.json]\n";
            std::cout << "          [--input-trace FILE.jsonl] (bounded native input diagnostics; "
                         "F12 readback)\n";
            return 0;
        }
        if (option == "--diagnostic-fixture")
            diagnostic_fixture = true;
        else if (option == "--no-validation")
            validation = false;
        else if (option == "--source-view")
            inspect_source = true;
        else if (argument + 1 < argc) {
            const std::string value = argv[++argument];
            if (option == "--scene")
                scene_path = value;
            else if (option == "--reconstruction-request")
                reconstruction_request = value;
            else if (option == "--review-request")
                review_request = value;
            else if (option == "--screenshot")
                screenshot = value;
            else if (option == "--settings")
                settings_path = value;
            else if (option == "--input-trace")
                input_trace_path = value;
            else if (option == "--shader-dir")
                shader_path = value;
            else if (option == "--worker-python")
                worker.python = value;
            else if (option == "--worker-script")
                worker.script = value;
            else if (option == "--frames")
                frame_limit = static_cast<unsigned>(std::stoul(value));
            else if (option == "--max-splats")
                max_splats = std::stoull(value);
            else if (option == "--residency-mib")
                residency_mib = std::stoull(value);
            else
                throw std::runtime_error("Unknown option: " + option);
        } else
            throw std::runtime_error("Missing option value: " + option);
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    const bool require_reconstruction_success = !reconstruction_request.empty();
    const bool require_review_success = !review_request.empty();
    if (require_review_success && require_reconstruction_success)
        throw std::runtime_error("Choose either startup review or startup reconstruction");
    struct sdl_owner {
        ~sdl_owner() { SDL_Quit(); }
    } sdl;
    if (settings_path.empty()) {
        const char* pref = SDL_GetPrefPath("WorldExplorer", "WorldExplorer");
        if (pref)
            settings_path = std::filesystem::path(pref) / "settings.json";
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer — development", 1440, 900,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window)
        throw std::runtime_error(SDL_GetError());
    SDL_SetWindowMinimumSize(window.get(), 800, 480);
    renderer_options options;
    if (!max_splats || max_splats > 2000000 || residency_mib < 64 || residency_mib > 4096)
        throw std::runtime_error("Viewer budget requires 1–2,000,000 splats and 64–4,096 MiB");
    options.max_splats = max_splats;
    options.residency_bytes = residency_mib * 1024 * 1024;
    options.shader_directory = shader_path;
    options.validation = validation;
    auto renderer_result = vulkan_renderer::create(window.get(), options);
    if (!renderer_result)
        throw std::runtime_error(renderer_result.error().message);
    auto renderer = std::move(*renderer_result);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    struct gui_owner {
        vulkan_renderer* renderer;
        bool sdl{}, vulkan{};
        ~gui_owner() {
            renderer->set_overlay_callback({});
            if (const auto completed = renderer->wait_for_idle(); !completed)
                std::cerr << "GPU shutdown: " << completed.error().message << '\n';
            if (vulkan)
                ImGui_ImplVulkan_Shutdown();
            if (sdl)
                ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
    } gui{renderer.get()};
    auto& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    float scale = 1;
    std::array<ImFont*, 13> fonts{};
    for (std::size_t index = 0; index < fonts.size(); ++index) {
        ImFontConfig font;
        font.SizePixels = 13.F * (1.F + static_cast<float>(index) * 0.25F);
        fonts[index] = io.Fonts->AddFontDefault(&font);
        if (!fonts[index])
            throw std::runtime_error("Cannot prepare display font atlas");
    }
    viewer::apply_theme(scale);
    gui.sdl = ImGui_ImplSDL3_InitForVulkan(window.get());
    if (!gui.sdl)
        throw std::runtime_error("ImGui SDL initialization failed");
    const auto context = renderer->overlay_context();
    // The external initialization struct must start zeroed; samples is assigned below.
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = renderer->capabilities().instance_version;
    init.Instance = native<VkInstance>(context.instance);
    init.PhysicalDevice = native<VkPhysicalDevice>(context.physical_device);
    init.Device = native<VkDevice>(context.device);
    init.QueueFamily = context.queue_family;
    init.Queue = native<VkQueue>(context.queue);
    init.RenderPass = native<VkRenderPass>(context.render_pass);
    init.Subpass = context.subpass;
    init.DescriptorPoolSize = 128;
    init.MinImageCount = context.min_image_count;
    init.ImageCount = context.image_count;
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    gui.vulkan = ImGui_ImplVulkan_Init(&init);
    if (!gui.vulkan)
        throw std::runtime_error("ImGui Vulkan initialization failed");
    renderer->set_overlay_callback([](std::uintptr_t command) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), native<VkCommandBuffer>(command));
    });
    application_state state;
    state.load_limits.scene.max_splats = options.max_splats;
    state.load_limits.scene.max_payload_bytes = options.residency_bytes;
    preferences settings;
    read_preferences(settings_path, settings);
    fly_camera camera;
    double camera_fov = camera_view{}.vertical_fov_radians;
    struct saved_view {
        camera_bookmark pose;
        double vertical_fov_radians;
    };
    std::optional<saved_view> bookmark;
    std::optional<update_ticket> pending_ticket;
    scene_snapshot current;
    std::uint64_t next_operation = 1;
    bool running = true, captured = false, show_open = false, show_import = false,
         show_settings = false;
    int remapping = -1;
    std::ofstream input_trace;
    unsigned input_readbacks{};
    nlohmann::json input_widgets = nlohmann::json::object();
    if (!input_trace_path.empty()) {
        if (std::filesystem::exists(std::filesystem::symlink_status(input_trace_path)))
            throw std::runtime_error("Choose a new input trace file");
        input_trace.open(input_trace_path, std::ios::binary);
        if (!input_trace)
            throw std::runtime_error("Cannot create input trace");
    }
    const auto trace_item = [&](const char* name) {
        if (!input_trace.is_open())
            return;
        const auto minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
        input_widgets[name] = {minimum.x, minimum.y, maximum.x, maximum.y, ImGui::IsItemVisible()};
    };
    std::array<char, 1024> path{}, source{}, output{}, request_path{}, pose_request_path{};
    std::snprintf(output.data(), output.size(), "%s", "artifacts/import-job");
    unsigned frames = 0;
    bool screenshot_requested = false;
    auto previous = std::chrono::steady_clock::now();
    if (diagnostic_fixture)
        state.loaded = make_diagnostic_scene();
    else if (!scene_path.empty())
        state.load(scene_path);
    auto release_mouse = [&] {
        SDL_SetWindowRelativeMouseMode(window.get(), false);
        captured = false;
    };
    while (running) {
        fly_input movement;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                release_mouse();
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
                release_mouse();
            if (input_trace.is_open() && input_readbacks < 16 && event.type == SDL_EVENT_KEY_DOWN &&
                !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12 && remapping < 0) {
                const auto readback =
                    input_trace_path.parent_path() /
                    (input_trace_path.stem().string() + "-" + std::to_string(frames) + ".ppm");
                const auto requested = renderer->request_screenshot(readback);
                if (!requested)
                    throw std::runtime_error(requested.error().message);
                ++input_readbacks;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && remapping >= 0) {
                if (event.key.scancode != SDL_SCANCODE_ESCAPE)
                    settings.keys[static_cast<std::size_t>(remapping)] = event.key.scancode;
                remapping = -1;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && captured) {
                movement.mouse_x += event.motion.xrel;
                movement.mouse_y += event.motion.yrel;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && captured)
                movement.wheel += event.wheel.y;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse && !show_open &&
                !show_import && !show_settings) {
                captured = SDL_SetWindowRelativeMouseMode(window.get(), true);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (!reconstruction_request.empty() && !state.has_pending_work() && !pending_ticket) {
            state.reconstruction(std::exchange(reconstruction_request, {}));
        }
        if (!review_request.empty() && !state.has_pending_work() && !pending_ticket) {
            const auto selected = review_request.string();
            if (selected.size() >= request_path.size())
                throw std::runtime_error("Request path exceeds Import field capacity");
            std::snprintf(request_path.data(), request_path.size(), "%s", selected.c_str());
            state.review_request(std::exchange(review_request, {}));
            show_import = true;
            release_mouse();
        }
        movement.captured = captured;
        movement.focused = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
        movement.text_input = io.WantTextInput || show_open || show_import || show_settings;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        movement.forward =
            static_cast<int>(keys[settings.keys[0]]) - static_cast<int>(keys[settings.keys[1]]);
        movement.right =
            static_cast<int>(keys[settings.keys[3]]) - static_cast<int>(keys[settings.keys[2]]);
        movement.up =
            static_cast<int>(keys[settings.keys[4]]) - static_cast<int>(keys[settings.keys[5]]);
        movement.accelerate = keys[settings.keys[6]];
        camera.update(movement, elapsed);
        scene_snapshot loaded;
        {
            std::lock_guard lock(state.mutex);
            loaded = std::move(state.loaded);
        }
        if (loaded) {
            auto batch = make_snapshot_batch(loaded, operation_id{next_operation++});
            auto admitted = batch ? renderer->updates().enqueue(*batch)
                                  : result<update_ticket>{std::unexpected(batch.error())};
            std::lock_guard lock(state.mutex);
            if (admitted) {
                pending_ticket = *admitted;
                current = loaded;
                camera.up_axis = {0, 1, 0};
                camera.fit(world_bounds(*current));
                if (inspect_source) {
                    const auto inspection = source_view(*current);
                    if (!inspection)
                        throw std::runtime_error(inspection.error().message);
                    camera = inspection->camera;
                    camera_fov = inspection->vertical_fov_radians;
                }
                state.status = "Uploading validated scene...";
            } else
                state.status = "Scene admission failed: " + admitted.error().message +
                               ". Previous visible scene retained.";
        }
        const auto scaling = viewer::display_scaling(SDL_GetWindowDisplayScale(window.get()),
                                                     SDL_GetWindowPixelDensity(window.get()));
        if (!scaling)
            throw std::runtime_error(scaling.error().message);
        if (scale != scaling->layout_scale) {
            scale = scaling->layout_scale;
            viewer::apply_theme(scale);
        }
        io.FontDefault = fonts[scaling->font_index];
        io.FontGlobalScale = scaling->font_scale;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        input_widgets = nlohmann::json::object();
        const auto display = io.DisplaySize;
        const float top = 56 * scale, bottom = 120 * scale;
        const bool compact = display.x < 1200 * scale;
        constexpr auto PANEL_FLAGS = static_cast<ImGuiWindowFlags>(
            static_cast<unsigned>(ImGuiWindowFlags_NoMove) | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({display.x, top});
        ImGui::Begin("Toolbar", nullptr, PANEL_FLAGS);
        ImGui::TextUnformatted(is_synthetic_scene(current) ? "SYNTHETIC SCENE" : "WORLD EXPLORER");
        ImGui::SameLine(210 * scale);
        if (ImGui::Button("Open scene")) {
            show_open = true;
            release_mouse();
        }
        trace_item("open");
        ImGui::SameLine();
        if (ImGui::Button("Import video")) {
            show_import = true;
            release_mouse();
        }
        trace_item("import");
        ImGui::SameLine();
        if (ImGui::Button("Fit") && current) {
            camera.fit(world_bounds(*current));
            camera_fov = camera_view{}.vertical_fov_radians;
        }
        trace_item("fit");
        ImGui::SameLine();
        ImGui::BeginDisabled(!current || !current->provenance.contains("source_inspection_view"));
        if (ImGui::Button("Source view")) {
            const auto inspection = source_view(*current);
            if (inspection) {
                camera = inspection->camera;
                camera_fov = inspection->vertical_fov_radians;
            } else {
                std::lock_guard lock(state.mutex);
                state.status = inspection.error().message;
            }
        }
        trace_item("source");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Reset using the recorded capture orientation. Fly keeps this up "
                              "direction; gravity remains unmeasured. No camera following.");
        ImGui::SameLine();
        ImGui::TextUnformatted("Fly");
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Walk unavailable");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                "Walking requires independently measured scale and validated collision geometry.");
        ImGui::SameLine();
        if (ImGui::Button("Settings")) {
            show_settings = true;
            release_mouse();
        }
        trace_item("settings");
        ImGui::End();
        if (settings.left && !compact) {
            ImGui::SetNextWindowPos({0, top});
            ImGui::SetNextWindowSize({232 * scale, display.y - top - bottom});
            ImGui::Begin("Project", nullptr, PANEL_FLAGS);
            ImGui::TextUnformatted("PROJECT");
            ImGui::Separator();
            ImGui::TextWrapped("%s", current ? "Gaussian appearance" : "No scene open");
            if (is_synthetic_scene(current))
                ImGui::TextWrapped("Synthetic renderer diagnostic. Not a reconstructed capture.");
            if (current) {
                ImGui::Text("Revision %llu",
                            static_cast<unsigned long long>(current->revision.value));
                ImGui::Text("Chunks: %zu", current->chunks.size());
                ImGui::TextWrapped("Scale: %s", current->scale == scale_status::ARBITRARY
                                                    ? "arbitrary (not measured)"
                                                    : "measured metric");
                ImGui::TextWrapped("View up: %s (gravity unmeasured)",
                                   camera.up_axis == vec3d{0, 1, 0} ? "world axis"
                                                                    : "capture orientation");
                ImGui::TextWrapped("Coverage outside captured surfaces is unknown.");
                if (ImGui::Button("Save bookmark"))
                    bookmark = saved_view{{current->id, current->epoch, camera}, camera_fov};
                trace_item("save_bookmark");
                if (ImGui::Button("Return to bookmark") && bookmark) {
                    auto restored = restore_bookmark(bookmark->pose, current->id, current->epoch);
                    if (restored) {
                        camera = *restored;
                        camera_fov = bookmark->vertical_fov_radians;
                    }
                }
                trace_item("restore_bookmark");
            }
            ImGui::Spacing();
            ImGui::TextWrapped("Capture overlapping views while moving around surfaces. Avoid "
                               "zoom, mirrors and moving subjects.");
            ImGui::End();
        }
        if (settings.right && !compact) {
            ImGui::SetNextWindowPos({display.x - 248 * scale, top});
            ImGui::SetNextWindowSize({248 * scale, display.y - top - bottom});
            ImGui::Begin("Inspector", nullptr, PANEL_FLAGS);
            ImGui::TextUnformatted("INSPECTOR");
            ImGui::Separator();
            ImGui::TextWrapped("Navigation: %s", captured ? "Mouse captured (Esc releases)"
                                                          : "Click the viewport to fly");
            ImGui::Text("Speed: %.2f units/s", camera.speed);
            ImGui::Checkbox("Show controls", &settings.controls);
            ImGui::Checkbox("Diagnostics", &settings.diagnostics);
            if (settings.diagnostics) {
                const auto stats = renderer->diagnostics();
                ImGui::TextWrapped("%s", renderer->capabilities().device_name.c_str());
                ImGui::Text("Splats: %zu", stats.resident_splat_count);
                ImGui::Text("GPU allocated: %.1f MiB",
                            static_cast<double>(stats.allocated_bytes) / 1048576);
                ImGui::Text("FP32 framebuffers: %.1f MiB",
                            static_cast<double>(stats.framebuffer_bytes) / 1048576);
                ImGui::Text("CPU frame: %.2f ms", stats.last_cpu_frame_ms);
                ImGui::Text("Validation errors: %u", stats.validation_errors);
            }
            ImGui::End();
        }
        if (!current) {
            ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.45F}, ImGuiCond_Always,
                                    {0.5F, 0.5F});
            ImGui::SetNextWindowSize({420 * scale, 0});
            ImGui::Begin("Start", nullptr,
                         static_cast<ImGuiWindowFlags>(static_cast<unsigned>(PANEL_FLAGS) |
                                                       ImGuiWindowFlags_AlwaysAutoResize));
            ImGui::TextUnformatted("Explore a captured place");
            ImGui::TextWrapped(
                "Open a processed scene, or start a local reconstruction from your video.");
            if (ImGui::Button("Open scene bundle")) {
                show_open = true;
                release_mouse();
            }
            ImGui::SameLine();
            if (ImGui::Button("Import a video")) {
                show_import = true;
                release_mouse();
            }
            ImGui::End();
        }
        ImGui::SetNextWindowPos({0, display.y - bottom});
        ImGui::SetNextWindowSize({display.x, bottom});
        ImGui::Begin("Job status", nullptr, PANEL_FLAGS);
        bool cancel_requested = false;
        {
            std::lock_guard lock(state.mutex);
            ImGui::TextWrapped("%s", state.status.c_str());
            if (state.busy) {
                ImGui::SameLine();
                ImGui::Text("Elapsed %.1fs",
                            std::chrono::duration<double>(now - state.started).count());
                ImGui::SameLine();
                cancel_requested = ImGui::Button("Cancel");
            }
            if (state.progress && state.progress->backend_elapsed_seconds)
                ImGui::Text("Backend event elapsed %.1fs (ETA unknown)",
                            *state.progress->backend_elapsed_seconds);
            if (ImGui::CollapsingHeader("Diagnostic log"))
                ImGui::TextUnformatted(state.log.c_str());
        }
        if (cancel_requested)
            state.request_cancel();
        if (settings.controls)
            ImGui::TextWrapped("Click viewport: mouse look | %s/%s/%s/%s: move | %s/%s: vertical "
                               "| %s: accelerate | Wheel: speed | Esc: release. Remap in Settings.",
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[0])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[1])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[2])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[3])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[4])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[5])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[6])));
        ImGui::End();
        if (show_open)
            ImGui::OpenPopup("Open scene");
        if (ImGui::BeginPopupModal("Open scene", &show_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Choose an unpacked .wexplore directory.");
            ImGui::SetNextItemWidth(560 * scale);
            ImGui::InputText("Path", path.data(), path.size());
            ImGui::BeginDisabled(state.busy);
            if (ImGui::Button("Open")) {
                state.load(path.data());
                show_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                show_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (show_import)
            ImGui::OpenPopup("Import video");
        const ImVec2 import_limit{display.x - 32 * scale, display.y - 32 * scale};
        ImGui::SetNextWindowSizeConstraints(
            {std::min(480 * scale, import_limit.x), std::min(360 * scale, import_limit.y)},
            import_limit);
        ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.5F}, ImGuiCond_Always,
                                {0.5F, 0.5F});
        ImGui::SetNextWindowSize({std::min(820 * scale, display.x - 32 * scale),
                                  std::min(680 * scale, display.y - 32 * scale)},
                                 ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Import video", &show_import, ImGuiWindowFlags_NoMove)) {
            ImGui::TextWrapped("Review a local reconstruction request before running. "
                               "This development recipe has unqualified quality.");
            ImGui::TextUnformatted("Reconstruction request");
            ImGui::BeginDisabled(state.busy);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Reconstruction request", request_path.data(),
                                 request_path.size()))
                state.clear_request_review();
            ImGui::BeginDisabled(!request_path[0]);
            if (ImGui::Button("Review request"))
                state.review_request(request_path.data());
            trace_item("review");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            std::shared_ptr<const viewer::request_summary> summary;
            std::string review_error;
            {
                std::lock_guard lock(state.mutex);
                summary = state.reviewed_request;
                review_error = state.review_error;
            }
            ImGui::BeginChild(
                "Request details",
                {0, std::max(120 * scale, ImGui::GetContentRegionAvail().y - 88 * scale)},
                ImGuiChildFlags_Borders);
            if (!review_error.empty())
                ImGui::TextWrapped("Cannot run: %s", review_error.c_str());
            if (summary) {
                const auto rows = [&](const std::vector<viewer::request_summary_row>& values,
                                      const char* table) {
                    if (ImGui::BeginTable(table, 2, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed,
                                                170 * scale);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                        for (const auto& row : values) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", row.label.c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", row.value.c_str());
                        }
                        ImGui::EndTable();
                    }
                };
                rows(summary->rows, "Request summary");
                ImGui::TextWrapped("Review checks passed. Current storage is a snapshot, not a "
                                   "reservation. Inputs, storage and backend capabilities are "
                                   "checked when running. ETA unknown.");
                if (ImGui::CollapsingHeader("Advanced request details"))
                    rows(summary->advanced, "Backend details");
            } else if (review_error.empty())
                ImGui::TextWrapped("Select a request and choose Review request to see its "
                                   "actions and limits. Reviewing keeps scene viewing available.");
            if (ImGui::CollapsingHeader("Advanced local worker commands")) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("Video", source.data(), source.size());
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("Inspection output", output.data(), output.size());
                ImGui::TextWrapped(
                    "These fields apply to the separate worker commands below. "
                    "The reviewed request selects the reconstruction source and output.");
                ImGui::BeginDisabled(state.has_pending_work() || pending_ticket.has_value());
                if (ImGui::Button("Inspect video"))
                    state.worker(
                        worker, {"inspect", "--source", source.data(), "--job-dir", output.data()});
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("Pose-worker request", pose_request_path.data(),
                                 pose_request_path.size());
                ImGui::BeginDisabled(!pose_request_path[0]);
                if (ImGui::Button("Run configured pose job"))
                    state.worker(worker, {"run", "--request", pose_request_path.data()});
                ImGui::EndDisabled();
                if (ImGui::Button("Check worker capabilities"))
                    state.worker(worker, {"capabilities"});
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(!summary || state.has_pending_work() ||
                                 pending_ticket.has_value());
            if (ImGui::Button("Run reviewed request")) {
                state.run_reviewed_request();
                show_import = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                show_import = false;
                ImGui::CloseCurrentPopup();
            }
            trace_item("close_import");
            ImGui::TextWrapped("Running pauses scene rendering and retains the previous scene. "
                               "Changed requests require another review.");
            ImGui::EndPopup();
        }
        if (show_settings)
            ImGui::OpenPopup("Settings");
        if (ImGui::BeginPopupModal("Settings", &show_settings, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox("Project panel", &settings.left);
            ImGui::Checkbox("Inspector panel", &settings.right);
            if (ImGui::Button("Reset layout and bindings"))
                settings = preferences{};
            trace_item("reset_settings");
            constexpr std::array NAMES{"Forward", "Backward", "Left",      "Right",
                                       "Up",      "Down",     "Accelerate"};
            for (std::size_t index = 0; index < settings.keys.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextUnformatted(NAMES[index]);
                ImGui::SameLine(140 * scale);
                if (ImGui::Button(
                        remapping == static_cast<int>(index)
                            ? "Press a key..."
                            : SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[index]))))
                    remapping = static_cast<int>(index);
                if (index == 0)
                    trace_item("forward_binding");
                ImGui::PopID();
            }
            if (ImGui::Button("Close")) {
                show_settings = false;
                remapping = -1;
                ImGui::CloseCurrentPopup();
            }
            trace_item("close_settings");
            ImGui::EndPopup();
        }
        ImGui::Render();
        if (!screenshot.empty() && !screenshot_requested && frames >= 8 &&
            !state.has_pending_work() && reconstruction_request.empty() && review_request.empty() &&
            (!(require_reconstruction_success || !scene_path.empty()) ||
             (current && !pending_ticket))) {
            if (require_reconstruction_success) {
                std::lock_guard lock(state.mutex);
                if (!state.reconstruction_error.empty())
                    throw std::runtime_error(state.reconstruction_error);
            }
            if (require_review_success) {
                std::lock_guard lock(state.mutex);
                if (!state.reviewed_request)
                    throw std::runtime_error(state.review_error);
            }
            const auto requested = renderer->request_screenshot(screenshot);
            if (!requested)
                throw std::runtime_error(requested.error().message);
            screenshot_requested = true;
        }
        const auto drawn = renderer->draw(
            {camera.position, camera.right(), camera.down(), camera.forward(), camera_fov},
            !state.processing.load());
        if (!drawn)
            throw std::runtime_error(drawn.error().message);
        if (pending_ticket) {
            const auto progress = renderer->updates().query_progress(*pending_ticket);
            if (progress && progress->state == update_state::RENDER_VISIBLE) {
                std::lock_guard lock(state.mutex);
                state.status = is_synthetic_scene(current)
                                   ? "Ready: synthetic diagnostic fixture."
                                   : "Appearance ready. Geometry and walking unavailable.";
                pending_ticket.reset();
            }
        }
        ++frames;
        if (input_trace.is_open() && frames <= 4096) {
            bool reviewed{};
            {
                std::lock_guard lock(state.mutex);
                reviewed = state.reviewed_request != nullptr;
            }
            input_trace
                << nlohmann::json{{"frame", frames},
                                  {"position",
                                   {camera.position.x, camera.position.y, camera.position.z}},
                                  {"yaw", camera.yaw},
                                  {"fov", camera_fov},
                                  {"pitch", camera.pitch},
                                  {"speed", camera.speed},
                                  {"up", {camera.up_axis.x, camera.up_axis.y, camera.up_axis.z}},
                                  {"captured", captured},
                                  {"ui_mouse", io.WantCaptureMouse},
                                  {"mouse", {io.MousePos.x, io.MousePos.y}},
                                  {"focused", movement.focused},
                                  {"forward", movement.forward},
                                  {"text_input", movement.text_input},
                                  {"import", show_import},
                                  {"settings", show_settings},
                                  {"remapping", remapping},
                                  {"keys", settings.keys},
                                  {"busy", state.busy.load()},
                                  {"reviewed", reviewed},
                                  {"visible", current && !pending_ticket},
                                  {"logical", {display.x, display.y}},
                                  {"display_scale", SDL_GetWindowDisplayScale(window.get())},
                                  {"pixel_density", SDL_GetWindowPixelDensity(window.get())},
                                  {"layout_scale", scale},
                                  {"widgets", input_widgets}}
                       .dump()
                << '\n';
            input_trace.flush();
            if (!input_trace)
                throw std::runtime_error("Input trace write failed");
        }
        if (frame_limit && frames >= frame_limit && !state.has_pending_work() && !pending_ticket &&
            reconstruction_request.empty() && review_request.empty()) {
            if (require_reconstruction_success) {
                std::lock_guard lock(state.mutex);
                if (!state.reconstruction_error.empty())
                    throw std::runtime_error(state.reconstruction_error);
            }
            if (require_review_success) {
                std::lock_guard lock(state.mutex);
                if (!state.reviewed_request)
                    throw std::runtime_error(state.review_error);
            }
            if (!scene_path.empty() && !current) {
                std::lock_guard lock(state.mutex);
                throw std::runtime_error(state.status);
            }
            // Allow a requested scene to become visible before capturing it.
            if (screenshot.empty() || screenshot_requested)
                running = false;
        }
        if (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)
            SDL_Delay(20);
    }
    release_mouse();
    state.job.request_stop();
    if (state.job.joinable())
        state.job.join();
    if (const auto completed = renderer->wait_for_idle(); !completed)
        throw std::runtime_error(completed.error().message);
    if (const auto saved = save_preferences(settings_path, settings); !saved)
        std::cerr << "Settings not saved: " << saved.error().message << '\n';
    const auto stats = renderer->diagnostics();
    std::cout << nlohmann::json{{"device", renderer->capabilities().device_name},
                                {"frames", stats.submitted_frames},
                                {"resident_splats", stats.resident_splat_count},
                                {"allocated_bytes", stats.allocated_bytes},
                                {"framebuffer_bytes", stats.framebuffer_bytes},
                                {"fp32_compositing", renderer->capabilities().fp32_compositing},
                                {"validation_errors", stats.validation_errors},
                                {"fixture",
                                 is_synthetic_scene(current) ? "synthetic" : "user-selected"}}
                     .dump()
              << '\n';
    return stats.validation_errors ? 2 : 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        return viewer_main(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Viewer failed: " << error.what() << '\n';
        return 2;
    }
}
