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
#include <vulkan/vulkan.h>

namespace {
using namespace world_explorer;
using viewer::application_state;
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
    std::filesystem::path scene_path, screenshot, settings_path, shader_path = WE_SHADER_DIRECTORY;
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
            std::cout << "          [--source-view] (explicit upright spectator reset)\n";
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
            else if (option == "--screenshot")
                screenshot = value;
            else if (option == "--settings")
                settings_path = value;
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
    const float scale = std::clamp(SDL_GetWindowDisplayScale(window.get()), 1.F, 3.F);
    viewer::apply_theme(scale);
    io.FontGlobalScale = scale;
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
    std::optional<camera_bookmark> bookmark;
    std::optional<update_ticket> pending_ticket;
    scene_snapshot current;
    std::uint64_t next_operation = 1;
    bool running = true, captured = false, show_open = false, show_import = false,
         show_settings = false;
    int remapping = -1;
    std::array<char, 1024> path{}, source{}, output{}, request_path{};
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
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
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
        ImGui::TextUnformatted("WORLD EXPLORER");
        ImGui::SameLine(210 * scale);
        if (ImGui::Button("Open scene")) {
            show_open = true;
            release_mouse();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import video")) {
            show_import = true;
            release_mouse();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit") && current) {
            camera.fit(world_bounds(*current));
            camera_fov = camera_view{}.vertical_fov_radians;
        }
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
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Reset toward the first recorded source view, keeping world up. No "
                              "camera following.");
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
        ImGui::End();
        if (settings.left && !compact) {
            ImGui::SetNextWindowPos({0, top});
            ImGui::SetNextWindowSize({232 * scale, display.y - top - bottom});
            ImGui::Begin("Project", nullptr, PANEL_FLAGS);
            ImGui::TextUnformatted("PROJECT");
            ImGui::Separator();
            ImGui::TextWrapped("%s", current ? "Gaussian appearance" : "No scene open");
            if (diagnostic_fixture)
                ImGui::TextWrapped("Synthetic renderer diagnostic. Not a reconstructed capture.");
            if (current) {
                ImGui::Text("Revision %llu",
                            static_cast<unsigned long long>(current->revision.value));
                ImGui::Text("Chunks: %zu", current->chunks.size());
                ImGui::TextWrapped("Scale: %s", current->scale == scale_status::ARBITRARY
                                                    ? "arbitrary (not measured)"
                                                    : "measured metric");
                ImGui::TextWrapped("Coverage outside captured surfaces is unknown.");
                if (ImGui::Button("Save bookmark"))
                    bookmark = camera_bookmark{current->id, current->epoch, camera};
                if (ImGui::Button("Return to bookmark") && bookmark) {
                    auto restored = restore_bookmark(*bookmark, current->id, current->epoch);
                    if (restored)
                        camera = *restored;
                }
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
        {
            std::lock_guard lock(state.mutex);
            ImGui::TextWrapped("%s", state.status.c_str());
            if (state.busy) {
                ImGui::SameLine();
                ImGui::Text("Elapsed %.1fs",
                            std::chrono::duration<double>(now - state.started).count());
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    state.job.request_stop();
            }
            if (ImGui::CollapsingHeader("Diagnostic log"))
                ImGui::TextUnformatted(state.log.c_str());
        }
        if (settings.controls)
            ImGui::TextWrapped(
                "Click viewport: mouse look | W/S/A/D: move | Space/Shift: vertical "
                "| Ctrl: accelerate | Wheel: speed | Esc: release. Remap in Settings.");
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
        if (ImGui::BeginPopupModal("Import video", &show_import,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Local processing only. Source video is retained. Inspect metadata "
                               "before reconstruction.");
            ImGui::SetNextItemWidth(560 * scale);
            ImGui::InputText("Video", source.data(), source.size());
            ImGui::SetNextItemWidth(560 * scale);
            ImGui::InputText("Job directory", output.data(), output.size());
            ImGui::TextWrapped("Standard preset: 7,000 appearance steps. Missing worker packages "
                               "are reported explicitly.");
            ImGui::BeginDisabled(state.busy);
            if (ImGui::Button("Inspect video"))
                state.worker(worker,
                             {"inspect", "--source", source.data(), "--job-dir", output.data()});
            ImGui::SameLine();
            if (ImGui::Button("Run configured job")) {
                if (request_path[0])
                    state.worker(worker, {"run", "--request", request_path.data()});
                else {
                    std::lock_guard lock(state.mutex);
                    state.status = "Select a job request JSON with backend paths and resource "
                                   "limits under Advanced.";
                }
            }
            ImGui::EndDisabled();
            if (ImGui::CollapsingHeader("Advanced")) {
                ImGui::SetNextItemWidth(560 * scale);
                ImGui::InputText("Job request JSON", request_path.data(), request_path.size());
                ImGui::TextWrapped("This development build runs the same versioned job request as "
                                   "we_process. Full preflight controls remain in progress.");
                if (ImGui::Button("Check worker capabilities"))
                    state.worker(worker, {"capabilities"});
            }
            if (ImGui::Button("Close")) {
                show_import = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (show_settings)
            ImGui::OpenPopup("Settings");
        if (ImGui::BeginPopupModal("Settings", &show_settings, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox("Project panel", &settings.left);
            ImGui::Checkbox("Inspector panel", &settings.right);
            if (ImGui::Button("Reset layout and bindings"))
                settings = preferences{};
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
                ImGui::PopID();
            }
            if (ImGui::Button("Close")) {
                show_settings = false;
                remapping = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Render();
        if (!screenshot.empty() && !screenshot_requested && frames >= 8 &&
            (scene_path.empty() || (current && !pending_ticket))) {
            const auto requested = renderer->request_screenshot(screenshot);
            if (!requested)
                throw std::runtime_error(requested.error().message);
            screenshot_requested = true;
        }
        const auto drawn = renderer->draw(
            {camera.position, camera.right(), camera.down(), camera.forward(), camera_fov});
        if (!drawn)
            throw std::runtime_error(drawn.error().message);
        if (pending_ticket) {
            const auto progress = renderer->updates().query_progress(*pending_ticket);
            if (progress && progress->state == update_state::RENDER_VISIBLE) {
                std::lock_guard lock(state.mutex);
                state.status = diagnostic_fixture
                                   ? "Ready: synthetic diagnostic fixture."
                                   : "Appearance ready. Geometry and walking unavailable.";
                pending_ticket.reset();
            }
        }
        ++frames;
        if (frame_limit && frames >= frame_limit && !state.busy && !pending_ticket) {
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
                                {"validation_errors", stats.validation_errors},
                                {"fixture", diagnostic_fixture ? "synthetic" : "user-selected"}}
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
