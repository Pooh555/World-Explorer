#include "input_latency.hpp"
#include "job_state.hpp"
#include "preferences.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/navigation/fly_camera.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/world_frame.hpp"
#if defined(WE_VULKAN_TEST_FAULTS)
#include "renderer_faults.hpp"
#endif
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace {

using namespace world_explorer;
using viewer::application_state;
using viewer::is_synthetic_scene;
using viewer::preferences;
using viewer::read_preferences;
using viewer::save_preferences;

template <class HandleType> HandleType native(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<HandleType>) {
        // Opaque Vulkan handles cross the public renderer boundary as uintptr_t.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<HandleType>(value);
    } else {
        return static_cast<HandleType>(value);
    }
}

int viewer_main(int argc, char** argv) {
    std::filesystem::path scene_path, reconstruction_request, review_request, screenshot,
        settings_path, input_trace_path, input_latency_path, shader_path = WE_SHADER_DIRECTORY;
    std::filesystem::path fusion_request;
    std::string fusion_request_sha256;
    std::filesystem::path geometry_request;
    std::filesystem::path geometry_output;
    std::string geometry_request_sha256;
    DepthSpaceOpenRequest depth_space;
    SceneRepresentation representation { SceneRepresentation::CAPTURED_APPEARANCE };
    WorkerConfiguration worker { WE_WORKER_SCRIPT };
    std::size_t max_splats = 250000;
    std::size_t residency_mib = 512;
    bool diagnostic_fixture = false, validation = true;
    bool inspect_source = false;
    bool open_geometry = false;
    unsigned frame_limit = 0;
#if defined(WE_VULKAN_TEST_FAULTS)
    unsigned inject_device_loss_frame = 0;
#endif
    for (int argument { 1 }; argument < argc; ++argument) {
        const std::string K_OPTION = argv[argument];
        if (K_OPTION == "--help") {
            std::cout
                << "we_viewer [--scene BUNDLE] [--diagnostic-fixture] [--frames N]\n"
                   "          [--screenshot FILE.ppm] [--settings FILE] [--shader-dir DIR]\n"
                   "          [--worker-python EXE] [--worker-script FILE] [--no-validation]\n"
                   "          [--fusion-request FILE.json --fusion-request-sha SHA256]\n"
                   "          [--geometry-request FILE.json --geometry-request-sha SHA256\n"
                   "           --geometry-output NEW_JOB]\n"
                   "          [--max-splats N] [--residency-mib N]\n";
            std::cout << "          [--depth-space DIRECTORY --depth-space-report-sha SHA256\n"
                         "           --depth-space-source SOURCE --depth-space-source-sha SHA256\n"
                         "           --depth-space-request-sha SHA256]\n";
            std::cout
                << "          [--source-view] (explicit capture-orientation spectator reset)\n";
            std::cout
                << "          [--reconstruction-request JOB.json] [--review-request JOB.json]\n";
            std::cout << "          [--input-trace FILE.jsonl] (bounded native input diagnostics; "
                         "F12 readback)\n";
            std::cout << "          [--input-latency NEW.json] (buffered CPU input timing)\n";
#if defined(WE_VULKAN_TEST_FAULTS)
            std::cout << "          [--inject-device-loss-frame N] (synthetic Debug diagnostic)\n";
#endif
            return 0;
        }
        if (K_OPTION == "--diagnostic-fixture") {
            diagnostic_fixture = true;
        } else if (K_OPTION == "--no-validation") {
            validation = false;
        } else if (K_OPTION == "--source-view") {
            inspect_source = true;
        } else if (argument + 1 < argc) {
            const std::string K_VALUE = argv[++argument];
            if (K_OPTION == "--depth-space") {
                depth_space.m_directory = K_VALUE;
            } else if (K_OPTION == "--depth-space-report-sha") {
                depth_space.m_report_sha256 = K_VALUE;
            } else if (K_OPTION == "--depth-space-source") {
                depth_space.m_source_bundle = K_VALUE;
            } else if (K_OPTION == "--depth-space-source-sha") {
                depth_space.m_source_manifest_sha256 = K_VALUE;
            } else if (K_OPTION == "--depth-space-request-sha") {
                depth_space.m_request_sha256 = K_VALUE;
            } else if (K_OPTION == "--fusion-request") {
                fusion_request = K_VALUE;
            } else if (K_OPTION == "--geometry-request") {
                geometry_request = K_VALUE;
            } else if (K_OPTION == "--geometry-request-sha") {
                geometry_request_sha256 = K_VALUE;
            } else if (K_OPTION == "--geometry-output") {
                geometry_output = K_VALUE;
            } else if (K_OPTION == "--fusion-request-sha") {
                fusion_request_sha256 = K_VALUE;
            } else if (K_OPTION == "--scene") {
                scene_path = K_VALUE;
            } else if (K_OPTION == "--reconstruction-request") {
                reconstruction_request = K_VALUE;
            } else if (K_OPTION == "--review-request") {
                review_request = K_VALUE;
            } else if (K_OPTION == "--screenshot") {
                screenshot = K_VALUE;
            } else if (K_OPTION == "--settings") {
                settings_path = K_VALUE;
            } else if (K_OPTION == "--input-trace") {
                input_trace_path = K_VALUE;
            } else if (K_OPTION == "--input-latency") {
                input_latency_path = K_VALUE;
            } else if (K_OPTION == "--shader-dir") {
                shader_path = K_VALUE;
            } else if (K_OPTION == "--worker-python") {
                worker.m_python = K_VALUE;
            } else if (K_OPTION == "--worker-script") {
                worker.m_script = K_VALUE;
            } else if (K_OPTION == "--frames") {
                frame_limit = static_cast<unsigned>(std::stoul(K_VALUE));
#if defined(WE_VULKAN_TEST_FAULTS)
            } else if (K_OPTION == "--inject-device-loss-frame") {
                inject_device_loss_frame = static_cast<unsigned>(std::stoul(K_VALUE));
#endif
            } else if (K_OPTION == "--max-splats") {
                max_splats = std::stoull(K_VALUE);
            } else if (K_OPTION == "--residency-mib") {
                residency_mib = std::stoull(K_VALUE);
            } else {
                throw std::runtime_error("Unknown option: " + K_OPTION);
            }
        } else {
            throw std::runtime_error("Missing option value: " + K_OPTION);
        }
    }
    if (!depth_space.m_directory.empty() &&
        (!scene_path.empty() || diagnostic_fixture || !fusion_request.empty() ||
         !geometry_request.empty() || !reconstruction_request.empty() || !review_request.empty())) {
        throw std::runtime_error("Choose one startup scene source");
    }
    const bool K_SPACE_PINS {
        !depth_space.m_source_bundle.empty() && !depth_space.m_source_manifest_sha256.empty() &&
        !depth_space.m_report_sha256.empty() && !depth_space.m_request_sha256.empty() };
    const bool K_SPACE_OPTIONS {
        !depth_space.m_source_bundle.empty() || !depth_space.m_source_manifest_sha256.empty() ||
        !depth_space.m_report_sha256.empty() || !depth_space.m_request_sha256.empty() };
    if ((!depth_space.m_directory.empty() && !K_SPACE_PINS) ||
        (depth_space.m_directory.empty() && K_SPACE_OPTIONS)) {
        throw std::runtime_error("Depth-space open requires source, terminal and request pins");
    }
    if (!fusion_request.empty() && (!scene_path.empty() || diagnostic_fixture ||
                                    !reconstruction_request.empty() || !review_request.empty())) {
        throw std::runtime_error("Choose one startup scene source");
    }
    if (fusion_request.empty() != fusion_request_sha256.empty()) {
        throw std::runtime_error("Fusion request requires its SHA-256 pin");
    }
    if (geometry_request.empty() != geometry_request_sha256.empty() ||
        geometry_request.empty() != geometry_output.empty()) {
        throw std::runtime_error("Geometry processing requires request, SHA-256 and output");
    }
    if (!geometry_request.empty() &&
        (!fusion_request.empty() || !reconstruction_request.empty() || !review_request.empty())) {
        throw std::runtime_error("Choose one startup processing request");
    }
    if (!input_latency_path.empty() && (!input_trace_path.empty() || !screenshot.empty())) {
        throw std::runtime_error("Input latency requires traces and screenshots disabled");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }
    const bool K_REQUIRE_RECONSTRUCTION_SUCCESS =
        !reconstruction_request.empty() || !geometry_request.empty();
    const bool K_REQUIRE_REVIEW_SUCCESS = !review_request.empty();
    if (K_REQUIRE_REVIEW_SUCCESS && K_REQUIRE_RECONSTRUCTION_SUCCESS) {
        throw std::runtime_error("Choose either startup review or startup reconstruction");
    }

    struct SdlOwner {
        ~SdlOwner() {
            SDL_Quit();
        }
    } sdl;

    if (settings_path.empty()) {
        const char* pref = SDL_GetPrefPath("WorldExplorer", "WorldExplorer");
        if (pref) {
            settings_path = std::filesystem::path(pref) / "settings.json";
        }
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer — development",
                         1440,
                         900,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetWindowMinimumSize(window.get(), 800, 480);
    RendererOptions options;
    if (!max_splats || max_splats > 2000000 || residency_mib < 64 || residency_mib > 4096) {
        throw std::runtime_error("Viewer budget requires 1–2,000,000 splats and 64–4,096 MiB");
    }
    options.m_max_splats = max_splats;
    options.m_residency_bytes = residency_mib * 1024 * 1024;
    options.m_shader_directory = shader_path;
    options.m_validation = validation;
    auto renderer_result = VulkanRenderer::create(window.get(), options);
    if (!renderer_result) {
        throw std::runtime_error(renderer_result.error().m_message);
    }
    auto renderer = std::move(*renderer_result);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    struct GuiOwner {
        VulkanRenderer* m_renderer;
        bool m_sdl {}, m_vulkan {}, m_device_lost {};

        ~GuiOwner() {
            if (m_renderer) {
                m_renderer->set_overlay_callback({});
                if (!m_device_lost) {
                    if (const auto K_COMPLETED = m_renderer->wait_for_idle(); !K_COMPLETED) {
                        std::cerr << "GPU shutdown: " << K_COMPLETED.error().m_message << '\n';
                    }
                }
            }
            if (m_vulkan) {
                ImGui_ImplVulkan_Shutdown();
            }
            if (m_sdl) {
                ImGui_ImplSDL3_Shutdown();
            }
            ImGui::DestroyContext();
        }
    } gui { renderer.get() };

    auto& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    float scale = 1;
    std::array<ImFont*, 13> fonts {};
    for (std::size_t index { 0uz }; index < fonts.size(); ++index) {
        ImFontConfig font;
        font.SizePixels = 13.F * (1.F + static_cast<float>(index) * 0.25F);
        fonts[index] = io.Fonts->AddFontDefault(&font);
        if (!fonts[index]) {
            throw std::runtime_error("Cannot prepare display font atlas");
        }
    }
    viewer::apply_theme(scale);
    gui.m_sdl = ImGui_ImplSDL3_InitForVulkan(window.get());
    if (!gui.m_sdl) {
        throw std::runtime_error("ImGui SDL initialization failed");
    }
    const auto K_INITIALIZE_VULKAN_OVERLAY = [&] {
        const auto K_CONTEXT = renderer->overlay_context();
        // The external initialization struct must start zeroed; samples is assigned below.
        // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
        ImGui_ImplVulkan_InitInfo init {};
        init.ApiVersion = renderer->capabilities().m_instance_version;
        init.Instance = native<VkInstance>(K_CONTEXT.m_instance);
        init.PhysicalDevice = native<VkPhysicalDevice>(K_CONTEXT.m_physical_device);
        init.Device = native<VkDevice>(K_CONTEXT.m_device);
        init.QueueFamily = K_CONTEXT.m_queue_family;
        init.Queue = native<VkQueue>(K_CONTEXT.m_queue);
        init.RenderPass = native<VkRenderPass>(K_CONTEXT.m_render_pass);
        init.Subpass = K_CONTEXT.m_subpass;
        init.DescriptorPoolSize = 128;
        init.MinImageCount = K_CONTEXT.m_min_image_count;
        init.ImageCount = K_CONTEXT.m_image_count;
        init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        gui.m_vulkan = ImGui_ImplVulkan_Init(&init);
        if (!gui.m_vulkan) {
            throw std::runtime_error("ImGui Vulkan initialization failed");
        }
        renderer->set_overlay_callback([](std::uintptr_t command) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), native<VkCommandBuffer>(command));
        });
    };
    K_INITIALIZE_VULKAN_OVERLAY();
    application_state state;
    state.load_limits.m_scene.m_max_splats = options.m_max_splats;
    state.load_limits.m_scene.m_max_payload_bytes = options.m_residency_bytes;
    preferences settings;
    read_preferences(settings_path, settings);
    FlyCamera camera;
    double camera_fov = CameraView {}.m_vertical_fov_radians;

    struct SavedView {
        CameraBookmark m_pose;
        double m_vertical_fov_radians;
    };

    std::optional<SavedView> bookmark;
    std::optional<UpdateTicket> pending_ticket;
    auto previous_representation = representation;
    SceneSnapshot current;
    std::uint64_t next_operation = 1;
    bool running = true, captured = false, show_open = false, show_import = false,
         show_settings = false;
    int remapping = -1;
    std::ofstream input_trace;
    unsigned input_readbacks {};
    unsigned device_recoveries {};
    std::unique_ptr<viewer::input_latency_recorder> input_latency;
    if (!input_latency_path.empty()) {
        input_latency = std::make_unique<viewer::input_latency_recorder>(input_latency_path);
    }
    nlohmann::json input_widgets = nlohmann::json::object();
    if (!input_trace_path.empty()) {
        if (std::filesystem::exists(std::filesystem::symlink_status(input_trace_path))) {
            throw std::runtime_error("Choose a new input trace file");
        }
        input_trace.open(input_trace_path, std::ios::binary);
        if (!input_trace) {
            throw std::runtime_error("Cannot create input trace");
        }
    }
    const auto K_TRACE_ITEM = [&](const char* name) {
        if (!input_trace.is_open()) {
            return;
        }
        const auto K_MINIMUM = ImGui::GetItemRectMin(), K_MAXIMUM = ImGui::GetItemRectMax();
        input_widgets[name] = {
            K_MINIMUM.x, K_MINIMUM.y, K_MAXIMUM.x, K_MAXIMUM.y, ImGui::IsItemVisible() };
    };
    std::array<char, 1024> path {}, source {}, output {}, request_path {}, pose_request_path {};
    std::array<char, 1024> geometry_path {}, geometry_destination {};
    std::array<char, 65> geometry_pin {};
    std::snprintf(output.data(), output.size(), "%s", "artifacts/import-job");
    unsigned frames = 0;
    bool screenshot_requested = false;
    auto previous = std::chrono::steady_clock::now();
    if (!depth_space.m_directory.empty()) {
        state.load_depth_space(depth_space);
    } else if (!fusion_request.empty()) {
        representation = SceneRepresentation::OBSERVED_MESH;
        state.load_fusion(fusion_request, fusion_request_sha256);
    } else if (diagnostic_fixture) {
        state.loaded = make_diagnostic_scene();
    } else if (!scene_path.empty()) {
        state.load(scene_path);
    }
    auto release_mouse = [&] {
        SDL_SetWindowRelativeMouseMode(window.get(), false);
        captured = false;
    };
    while (running) {
        FlyInput movement;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (input_latency) {
                input_latency->observe(event, SDL_GetTicksNS(), settings.keys);
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                release_mouse();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                release_mouse();
            }
            if (input_trace.is_open() && input_readbacks < 16 && event.type == SDL_EVENT_KEY_DOWN &&
                !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12 && remapping < 0) {
                const auto K_READBACK =
                    input_trace_path.parent_path() /
                    (input_trace_path.stem().string() + "-" + std::to_string(frames) + ".ppm");
                const auto K_REQUESTED = renderer->request_screenshot(K_READBACK);
                if (!K_REQUESTED) {
                    throw std::runtime_error(K_REQUESTED.error().m_message);
                }
                ++input_readbacks;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && remapping >= 0) {
                if (event.key.scancode != SDL_SCANCODE_ESCAPE) {
                    settings.keys[static_cast<std::size_t>(remapping)] = event.key.scancode;
                }
                remapping = -1;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && captured) {
                movement.m_mouse_x += event.motion.xrel;
                movement.m_mouse_y += event.motion.yrel;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && captured) {
                movement.m_wheel += event.wheel.y;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse && !show_open &&
                !show_import && !show_settings) {
                captured = SDL_SetWindowRelativeMouseMode(window.get(), true);
            }
        }
        const auto K_NOW = std::chrono::steady_clock::now();
        const auto K_ELAPSED = std::chrono::duration<double>(K_NOW - previous).count();
        previous = K_NOW;
        if (!reconstruction_request.empty() && !state.has_pending_work() && !pending_ticket) {
            state.reconstruction(std::exchange(reconstruction_request, {}));
        }
        if (!geometry_request.empty() && !state.has_pending_work() && !pending_ticket) {
            state.geometry(
                std::exchange(geometry_request, {}), geometry_request_sha256, geometry_output);
        }
        if (!review_request.empty() && !state.has_pending_work() && !pending_ticket) {
            const auto K_SELECTED = review_request.string();
            if (K_SELECTED.size() >= request_path.size()) {
                throw std::runtime_error("Request path exceeds Import field capacity");
            }
            std::snprintf(request_path.data(), request_path.size(), "%s", K_SELECTED.c_str());
            state.review_request(std::exchange(review_request, {}));
            show_import = true;
            release_mouse();
        }
        movement.m_captured = captured;
        movement.m_focused = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
        movement.m_text_input = io.WantTextInput || show_open || show_import || show_settings;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        movement.m_forward =
            static_cast<int>(keys[settings.keys[0]]) - static_cast<int>(keys[settings.keys[1]]);
        movement.m_right =
            static_cast<int>(keys[settings.keys[3]]) - static_cast<int>(keys[settings.keys[2]]);
        movement.m_up =
            static_cast<int>(keys[settings.keys[4]]) - static_cast<int>(keys[settings.keys[5]]);
        movement.m_accelerate = keys[settings.keys[6]];
        const auto K_CAMERA_BEFORE = camera;
        camera.update(movement, K_ELAPSED);
        if (input_latency) {
            input_latency->finish_frame(frames + 1,
                                        SDL_GetTicksNS(),
                                        K_CAMERA_BEFORE,
                                        camera,
                                        movement,
                                        current && !pending_ticket && !state.processing.load());
        }
        SceneSnapshot loaded;
        {
            std::lock_guard lock(state.mutex);
            loaded = std::move(state.loaded);
        }
        if (loaded) {
            auto batch = make_snapshot_batch(loaded, OperationId { next_operation++ });
            auto admitted = batch ? renderer->updates().enqueue(*batch)
                                  : Result<UpdateTicket> { std::unexpected(batch.error()) };
            std::lock_guard lock(state.mutex);
            if (admitted) {
                pending_ticket = *admitted;
                previous_representation = representation;
                current = loaded;
                bool has_baked = false;
                bool has_mesh = false;
                bool has_splats = false;
                for (const auto& [K_ID, K_CHUNK] : current->m_chunks) {
                    static_cast<void>(K_ID);
                    has_mesh = has_mesh || K_CHUNK->m_mesh.has_value();
                    has_baked = has_baked || (K_CHUNK->m_mesh && K_CHUNK->m_mesh->m_baked);
                    has_splats = has_splats || !K_CHUNK->m_splats.empty();
                }
                representation = has_baked && !has_splats ? SceneRepresentation::BAKED_APPEARANCE
                                 : has_mesh && !has_splats
                                     ? SceneRepresentation::OBSERVED_MESH
                                     : SceneRepresentation::CAPTURED_APPEARANCE;
                camera.m_up_axis = { 0, 1, 0 };
                camera.fit(world_bounds(*current));
                if (inspect_source) {
                    const auto K_INSPECTION = source_view(*current);
                    if (!K_INSPECTION) {
                        throw std::runtime_error(K_INSPECTION.error().m_message);
                    }
                    camera = K_INSPECTION->m_camera;
                    camera_fov = K_INSPECTION->m_vertical_fov_radians;
                }
                state.status = "Uploading validated scene...";
            } else {
                state.status = "Scene admission failed: " + admitted.error().m_message +
                               ". Previous visible scene retained.";
            }
        }
        const auto K_SCALING = viewer::display_scaling(SDL_GetWindowDisplayScale(window.get()),
                                                       SDL_GetWindowPixelDensity(window.get()));
        if (!K_SCALING) {
            throw std::runtime_error(K_SCALING.error().m_message);
        }
        if (scale != K_SCALING->layout_scale) {
            scale = K_SCALING->layout_scale;
            viewer::apply_theme(scale);
        }
        io.FontDefault = fonts[K_SCALING->font_index];
        io.FontGlobalScale = K_SCALING->font_scale;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        input_widgets = nlohmann::json::object();
        const auto K_DISPLAY = io.DisplaySize;
        const float K_TOP = 56 * scale, K_BOTTOM = 120 * scale;
        const bool K_COMPACT = K_DISPLAY.x < 1200 * scale;
        constexpr auto K_PANEL_FLAGS { static_cast<ImGuiWindowFlags>(
            static_cast<unsigned>(ImGuiWindowFlags_NoMove) | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings) };
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ K_DISPLAY.x, K_TOP });
        ImGui::Begin("Toolbar", nullptr, K_PANEL_FLAGS);
        ImGui::TextUnformatted(is_synthetic_scene(current) ? "SYNTHETIC SCENE" : "WORLD EXPLORER");
        ImGui::SameLine(210 * scale);
        if (ImGui::Button("Open scene")) {
            show_open = true;
            release_mouse();
        }
        K_TRACE_ITEM("open");
        ImGui::SameLine();
        if (ImGui::Button("Import video")) {
            show_import = true;
            release_mouse();
        }
        K_TRACE_ITEM("import");
        ImGui::SameLine();
        if (ImGui::Button("Fit") && current) {
            camera.fit(world_bounds(*current));
            camera_fov = CameraView {}.m_vertical_fov_radians;
        }
        K_TRACE_ITEM("fit");
        ImGui::SameLine();
        ImGui::BeginDisabled(!current || !current->m_provenance.contains("source_inspection_view"));
        if (ImGui::Button("Source view")) {
            const auto K_INSPECTION = source_view(*current);
            if (K_INSPECTION) {
                camera = K_INSPECTION->m_camera;
                camera_fov = K_INSPECTION->m_vertical_fov_radians;
            } else {
                std::lock_guard lock(state.mutex);
                state.status = K_INSPECTION.error().m_message;
            }
        }
        K_TRACE_ITEM("source");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Reset using the recorded capture orientation. Fly keeps this up "
                              "direction; gravity remains unmeasured. No camera following.");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Fly");
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Walk unavailable");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Walking requires independently measured scale and validated collision geometry.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Settings")) {
            show_settings = true;
            release_mouse();
        }
        K_TRACE_ITEM("settings");
        ImGui::End();
        if (settings.left && !K_COMPACT) {
            ImGui::SetNextWindowPos({ 0, K_TOP });
            ImGui::SetNextWindowSize({ 232 * scale, K_DISPLAY.y - K_TOP - K_BOTTOM });
            ImGui::Begin("Project", nullptr, K_PANEL_FLAGS);
            ImGui::TextUnformatted("PROJECT");
            ImGui::Separator();
            ImGui::TextWrapped("%s",
                               current ? (representation == SceneRepresentation::BAKED_APPEARANCE
                                              ? "Baked captured color"
                                          : representation == SceneRepresentation::OBSERVED_MESH
                                              ? "Partial observed geometry"
                                              : "Gaussian appearance")
                                       : "No scene open");
            if (current) {
                bool has_baked = false;
                bool has_portable = false;
                bool has_mesh = false;
                bool has_splats = false;
                for (const auto& [K_ID, K_CHUNK] : current->m_chunks) {
                    static_cast<void>(K_ID);
                    has_mesh = has_mesh || K_CHUNK->m_mesh.has_value();
                    has_baked = has_baked || (K_CHUNK->m_mesh && K_CHUNK->m_mesh->m_baked);
                    has_portable = has_portable ||
                                   (K_CHUNK->m_mesh && K_CHUNK->m_mesh->m_baked &&
                                    !K_CHUNK->m_mesh->m_baked->m_portable_parent_sha256.empty());
                    has_splats = has_splats || !K_CHUNK->m_splats.empty();
                }
                if (has_mesh) {
                    if (ImGui::RadioButton("Observed surfaces",
                                           representation == SceneRepresentation::OBSERVED_MESH)) {
                        representation = SceneRepresentation::OBSERVED_MESH;
                    }
                    if (has_baked && ImGui::RadioButton(
                                         "Baked captured color",
                                         representation == SceneRepresentation::BAKED_APPEARANCE)) {
                        representation = SceneRepresentation::BAKED_APPEARANCE;
                    }
                    if (has_splats &&
                        ImGui::RadioButton("Captured appearance",
                                           representation ==
                                               SceneRepresentation::CAPTURED_APPEARANCE)) {
                        representation = SceneRepresentation::CAPTURED_APPEARANCE;
                    }
                    if (representation == SceneRepresentation::BAKED_APPEARANCE) {
                        ImGui::TextWrapped(
                            "Captured unlit color. Unknown texels are masked. Not "
                            "relightable albedo. Surface accuracy and collision unverified.");
                    } else {
                        ImGui::TextWrapped("Normals diagnostic. Partial surfaces; gaps are "
                                           "unknown. Surface accuracy and collision unverified.");
                    }
                    if (has_portable && representation == SceneRepresentation::BAKED_APPEARANCE) {
                        ImGui::TextWrapped(
                            "Portable texture preview. Quantized color; conservative masking may "
                            "hide supported surface edges.");
                    }
                    if (current->m_provenance.contains("geometry_processing")) {
                        ImGui::TextWrapped("Simplified observed proxy. Raw source retained.");
                    }
                }
            }
            if (is_synthetic_scene(current)) {
                ImGui::TextWrapped("Synthetic renderer diagnostic. Not a reconstructed capture.");
            }
            if (current) {
                ImGui::Text("Revision %llu",
                            static_cast<unsigned long long>(current->m_revision.m_value));
                ImGui::Text("Chunks: %zu", current->m_chunks.size());
                const auto K_FRAME_LABEL = world_frame_label(*current);
                ImGui::TextWrapped(
                    "Scale/up: %.*s", static_cast<int>(K_FRAME_LABEL.size()), K_FRAME_LABEL.data());
                ImGui::TextWrapped(
                    current->m_world_frame
                        ? "Alignment changes the world frame; collision remains unavailable."
                        : "View up is a presentation reference; gravity unmeasured.");
                ImGui::TextWrapped("Coverage outside captured surfaces is unknown.");
                if (ImGui::Button("Save bookmark")) {
                    bookmark = SavedView { { current->m_id, current->m_epoch, camera }, camera_fov };
                }
                K_TRACE_ITEM("save_bookmark");
                if (ImGui::Button("Return to bookmark") && bookmark) {
                    auto restored =
                        restore_bookmark(bookmark->m_pose, current->m_id, current->m_epoch);
                    if (restored) {
                        camera = *restored;
                        camera_fov = bookmark->m_vertical_fov_radians;
                    }
                }
                K_TRACE_ITEM("restore_bookmark");
            }
            ImGui::Spacing();
            ImGui::TextWrapped("Capture overlapping views while moving around surfaces. Avoid "
                               "zoom, mirrors and moving subjects.");
            ImGui::End();
        }
        if (settings.right && !K_COMPACT) {
            ImGui::SetNextWindowPos({ K_DISPLAY.x - 248 * scale, K_TOP });
            ImGui::SetNextWindowSize({ 248 * scale, K_DISPLAY.y - K_TOP - K_BOTTOM });
            ImGui::Begin("Inspector", nullptr, K_PANEL_FLAGS);
            ImGui::TextUnformatted("INSPECTOR");
            ImGui::Separator();
            ImGui::TextWrapped("Navigation: %s",
                               captured ? "Mouse captured (Esc releases)"
                                        : "Click the viewport to fly");
            ImGui::Text("Speed: %.2f units/s", camera.m_speed);
            ImGui::Checkbox("Show controls", &settings.controls);
            ImGui::Checkbox("Diagnostics", &settings.diagnostics);
            if (settings.diagnostics) {
                const auto K_STATS = renderer->diagnostics();
                ImGui::TextWrapped("%s", renderer->capabilities().m_device_name.c_str());
                ImGui::Text("Splats: %zu", K_STATS.m_resident_splat_count);
                ImGui::Text("GPU allocated: %.1f MiB",
                            static_cast<double>(K_STATS.m_allocated_bytes) / 1048576);
                ImGui::Text("FP32 framebuffers: %.1f MiB",
                            static_cast<double>(K_STATS.m_framebuffer_bytes) / 1048576);
                ImGui::Text("CPU frame: %.2f ms", K_STATS.m_last_cpu_frame_ms);
                ImGui::Text("Validation errors: %u", K_STATS.m_validation_errors);
            }
            ImGui::End();
        }
        if (!current) {
            ImGui::SetNextWindowPos(
                { K_DISPLAY.x * 0.5F, K_DISPLAY.y * 0.45F }, ImGuiCond_Always, { 0.5F, 0.5F });
            ImGui::SetNextWindowSize({ 420 * scale, 0 });
            ImGui::Begin("Start",
                         nullptr,
                         static_cast<ImGuiWindowFlags>(static_cast<unsigned>(K_PANEL_FLAGS) |
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
        ImGui::SetNextWindowPos({ 0, K_DISPLAY.y - K_BOTTOM });
        ImGui::SetNextWindowSize({ K_DISPLAY.x, K_BOTTOM });
        ImGui::Begin("Job status", nullptr, K_PANEL_FLAGS);
        bool cancel_requested = false;
        {
            std::lock_guard lock(state.mutex);
            ImGui::TextWrapped("%s", state.status.c_str());
            if (state.busy) {
                ImGui::SameLine();
                ImGui::Text("Elapsed %.1fs",
                            std::chrono::duration<double>(K_NOW - state.started).count());
                ImGui::SameLine();
                cancel_requested = ImGui::Button("Cancel");
            }
            if (state.progress && state.progress->m_backend_elapsed_seconds) {
                ImGui::Text("Backend event elapsed %.1fs (ETA unknown)",
                            *state.progress->m_backend_elapsed_seconds);
            }
            if (ImGui::CollapsingHeader("Diagnostic log")) {
                ImGui::TextUnformatted(state.log.c_str());
            }
        }
        if (cancel_requested) {
            state.request_cancel();
        }
        if (settings.controls) {
            ImGui::TextWrapped("Click viewport: mouse look | %s/%s/%s/%s: move | %s/%s: vertical "
                               "| %s: accelerate | Wheel: speed | Esc: release. Remap in Settings.",
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[0])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[1])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[2])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[3])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[4])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[5])),
                               SDL_GetScancodeName(static_cast<SDL_Scancode>(settings.keys[6])));
        }
        ImGui::End();
        if (show_open) {
            ImGui::OpenPopup("Open scene");
        }
        if (ImGui::BeginPopupModal("Open scene", &show_open, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox("Observed geometry", &open_geometry);
            ImGui::TextWrapped("%s",
                               open_geometry ? "Choose a saved observed-geometry request JSON."
                                             : "Choose an unpacked .wexplore directory.");
            ImGui::SetNextItemWidth(560 * scale);
            ImGui::InputText("Path", path.data(), path.size());
            ImGui::BeginDisabled(state.busy);
            if (ImGui::Button("Open")) {
                if (open_geometry) {
                    state.load_fusion(path.data());
                } else {
                    state.load(path.data());
                }
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
        if (show_import) {
            ImGui::OpenPopup("Import video");
        }
        const ImVec2 K_IMPORT_LIMIT { K_DISPLAY.x - 32 * scale, K_DISPLAY.y - 32 * scale };
        ImGui::SetNextWindowSizeConstraints(
            { std::min(480 * scale, K_IMPORT_LIMIT.x), std::min(360 * scale, K_IMPORT_LIMIT.y) },
            K_IMPORT_LIMIT);
        ImGui::SetNextWindowPos(
            { K_DISPLAY.x * 0.5F, K_DISPLAY.y * 0.5F }, ImGuiCond_Always, { 0.5F, 0.5F });
        ImGui::SetNextWindowSize({ std::min(820 * scale, K_DISPLAY.x - 32 * scale),
                                  std::min(680 * scale, K_DISPLAY.y - 32 * scale) },
                                 ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Import video", &show_import, ImGuiWindowFlags_NoMove)) {
            ImGui::TextWrapped("Review a local reconstruction request before running. "
                               "This development recipe has unqualified quality.");
            ImGui::TextUnformatted("Reconstruction request");
            ImGui::BeginDisabled(state.busy);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText(
                    "##Reconstruction request", request_path.data(), request_path.size())) {
                state.clear_request_review();
            }
            ImGui::BeginDisabled(!request_path[0]);
            if (ImGui::Button("Review request")) {
                state.review_request(request_path.data());
            }
            K_TRACE_ITEM("review");
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
                { 0, std::max(120 * scale, ImGui::GetContentRegionAvail().y - 88 * scale) },
                ImGuiChildFlags_Borders);
            if (!review_error.empty()) {
                ImGui::TextWrapped("Cannot run: %s", review_error.c_str());
            }
            if (summary) {
                const auto K_ROWS = [&](const std::vector<viewer::request_summary_row>& values,
                                        const char* table) {
                    if (ImGui::BeginTable(table, 2, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableSetupColumn(
                            "Field", ImGuiTableColumnFlags_WidthFixed, 170 * scale);
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
                K_ROWS(summary->rows, "Request summary");
                ImGui::TextWrapped("Review checks passed. Current storage is a snapshot, not a "
                                   "reservation. Inputs, storage and backend capabilities are "
                                   "checked when running. ETA unknown.");
                if (ImGui::CollapsingHeader("Advanced request details")) {
                    K_ROWS(summary->advanced, "Backend details");
                }
            } else if (review_error.empty()) {
                ImGui::TextWrapped("Select a request and choose Review request to see its "
                                   "actions and limits. Reviewing keeps scene viewing available.");
            }
            if (ImGui::CollapsingHeader("Build observed geometry")) {
                ImGui::TextWrapped(
                    "Run the selected local depth recipe or reuse a checked depth cache. "
                    "Unknown gaps stay open; scale is arbitrary and walking is unavailable. "
                    "An unchanged request can resume the same output folder.");
                ImGui::BeginDisabled(state.has_pending_work() || pending_ticket.has_value());
                ImGui::InputText("Geometry request", geometry_path.data(), geometry_path.size());
                ImGui::InputText("Request SHA-256", geometry_pin.data(), geometry_pin.size());
                ImGui::InputText(
                    "Geometry output", geometry_destination.data(), geometry_destination.size());
                ImGui::BeginDisabled(!geometry_path[0] || !geometry_pin[0] ||
                                     !geometry_destination[0]);
                if (ImGui::Button("Build partial geometry")) {
                    state.geometry(
                        geometry_path.data(), geometry_pin.data(), geometry_destination.data());
                    show_import = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("Advanced local worker commands")) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("Video", source.data(), source.size());
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("Inspection output", output.data(), output.size());
                ImGui::TextWrapped(
                    "These fields apply to the separate worker commands below. "
                    "The reviewed request selects the reconstruction source and output.");
                ImGui::BeginDisabled(state.has_pending_work() || pending_ticket.has_value());
                if (ImGui::Button("Inspect video")) {
                    state.worker(
                        worker, { "inspect", "--source", source.data(), "--job-dir", output.data() });
                }
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText(
                    "Pose-worker request", pose_request_path.data(), pose_request_path.size());
                ImGui::BeginDisabled(!pose_request_path[0]);
                if (ImGui::Button("Run configured pose job")) {
                    state.worker(worker, { "run", "--request", pose_request_path.data() });
                }
                ImGui::EndDisabled();
                if (ImGui::Button("Check worker capabilities")) {
                    state.worker(worker, { "capabilities" });
                }
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
            K_TRACE_ITEM("close_import");
            ImGui::TextWrapped("Running pauses scene rendering and retains the previous scene. "
                               "Changed requests require another review.");
            ImGui::EndPopup();
        }
        if (show_settings) {
            ImGui::OpenPopup("Settings");
        }
        if (ImGui::BeginPopupModal("Settings", &show_settings, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox("Project panel", &settings.left);
            ImGui::Checkbox("Inspector panel", &settings.right);
            if (ImGui::Button("Reset layout and bindings")) {
                settings = preferences {};
            }
            K_TRACE_ITEM("reset_settings");
            constexpr std::array K_NAMES {
                "Forward", "Backward", "Left", "Right", "Up", "Down", "Accelerate" };
            for (std::size_t index { 0uz }; index < settings.keys.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextUnformatted(K_NAMES[index]);
                ImGui::SameLine(140 * scale);
                if (ImGui::Button(remapping == static_cast<int>(index)
                                      ? "Press a key..."
                                      : SDL_GetScancodeName(
                                            static_cast<SDL_Scancode>(settings.keys[index])))) {
                    remapping = static_cast<int>(index);
                }
                if (index == 0) {
                    K_TRACE_ITEM("forward_binding");
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Close")) {
                show_settings = false;
                remapping = -1;
                ImGui::CloseCurrentPopup();
            }
            K_TRACE_ITEM("close_settings");
            ImGui::EndPopup();
        }
        ImGui::Render();
#if defined(WE_VULKAN_TEST_FAULTS)
        const bool K_SCREENSHOT_AFTER_INJECTED_LOSS =
            !inject_device_loss_frame || device_recoveries != 0;
#else
        constexpr bool K_SCREENSHOT_AFTER_INJECTED_LOSS { true };
#endif
        if (!screenshot.empty() && !screenshot_requested && K_SCREENSHOT_AFTER_INJECTED_LOSS &&
            frames >= 8 && !state.has_pending_work() && reconstruction_request.empty() &&
            review_request.empty() && geometry_request.empty() &&
            (!(K_REQUIRE_RECONSTRUCTION_SUCCESS || !depth_space.m_directory.empty() ||
               !scene_path.empty() || !fusion_request.empty()) ||
             (current && !pending_ticket))) {
            if (K_REQUIRE_RECONSTRUCTION_SUCCESS) {
                std::lock_guard lock(state.mutex);
                if (!state.reconstruction_error.empty()) {
                    throw std::runtime_error(state.reconstruction_error);
                }
            }
            if (K_REQUIRE_REVIEW_SUCCESS) {
                std::lock_guard lock(state.mutex);
                if (!state.reviewed_request) {
                    throw std::runtime_error(state.review_error);
                }
            }
            const auto K_REQUESTED = renderer->request_screenshot(screenshot);
            if (!K_REQUESTED) {
                throw std::runtime_error(K_REQUESTED.error().m_message);
            }
            screenshot_requested = true;
        }
#if defined(WE_VULKAN_TEST_FAULTS)
        if (inject_device_loss_frame && frames + 1 >= inject_device_loss_frame && current &&
            !pending_ticket) {
            detail::fail_next_frame_with_device_loss();
            inject_device_loss_frame = 0;
        }
#endif
        const auto K_DRAWN = renderer->draw(
            { camera.m_position, camera.right(), camera.down(), camera.forward(), camera_fov },
            !state.processing.load(),
            representation);
        if (!K_DRAWN && K_DRAWN.error().m_code == ErrorCode::DEVICE_LOST) {
            if (device_recoveries++) {
                gui.m_device_lost = true;
                throw std::runtime_error("Vulkan device loss repeated after renderer recreation: " +
                                         K_DRAWN.error().m_message);
            }
            // The last admitted immutable snapshot survives even if its upload was in flight.
            // The new renderer establishes its own revision chain via normal full-snapshot
            // admission.
            const auto K_REPLAY = current ? current : renderer->updates().visible_snapshot();
            renderer->set_overlay_callback({});
            gui.m_device_lost = true;
            if (gui.m_vulkan) {
                ImGui_ImplVulkan_Shutdown();
                gui.m_vulkan = false;
            }
            gui.m_renderer = nullptr;
            renderer.reset();
            renderer_result = VulkanRenderer::create(window.get(), options);
            if (!renderer_result) {
                throw std::runtime_error("Renderer recreation after device loss failed: " +
                                         renderer_result.error().m_message);
            }
            renderer = std::move(*renderer_result);
            gui.m_renderer = renderer.get();
            gui.m_device_lost = false;
            K_INITIALIZE_VULKAN_OVERLAY();
            pending_ticket.reset();
            if (K_REPLAY) {
                auto batch = make_snapshot_batch(K_REPLAY, OperationId { next_operation++ });
                if (!batch) {
                    throw std::runtime_error("Scene replay after device loss failed: " +
                                             batch.error().m_message);
                }
                auto admitted = renderer->updates().enqueue(*batch);
                if (!admitted) {
                    throw std::runtime_error("Scene replay after device loss was rejected: " +
                                             admitted.error().m_message);
                }
                pending_ticket = *admitted;
            }
            screenshot_requested = false;
            {
                std::lock_guard lock(state.mutex);
                state.status = K_REPLAY ? "GPU device recreated; restoring the last scene..."
                                        : "GPU device recreated; ready for a scene.";
            }
            continue;
        }
        if (!K_DRAWN) {
            throw std::runtime_error(K_DRAWN.error().m_message);
        }
        if (pending_ticket) {
            const auto K_PROGRESS = renderer->updates().query_progress(*pending_ticket);
            if (K_PROGRESS && K_PROGRESS->m_state == UpdateState::FAILED) {
                current = renderer->updates().visible_snapshot();
                representation = previous_representation;
                pending_ticket.reset();
                const auto K_MESSAGE = K_PROGRESS->m_failure ? K_PROGRESS->m_failure->m_message
                                                             : "Unknown renderer upload failure";
                std::lock_guard lock(state.mutex);
                state.status =
                    "Scene upload failed: " + K_MESSAGE + ". Previous visible scene retained.";
                if (frame_limit) {
                    throw std::runtime_error(state.status);
                }
            } else if (K_PROGRESS && K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE) {
                std::lock_guard lock(state.mutex);
                state.status =
                    current && current->m_provenance.contains("depth_space")
                        ? current->m_provenance.at("depth_space")
                    : current && is_synthetic_scene(current) &&
                            current->m_provenance.contains("collision_support")
                        ? current->m_provenance.at("collision_support")
                    : current && current->m_world_frame
                        ? "World frame: " + std::string(world_frame_label(*current)) +
                              ". Walking unavailable."
                    : is_synthetic_scene(current) ? "Ready: synthetic diagnostic fixture."
                    : representation == SceneRepresentation::BAKED_APPEARANCE
                        ? "Baked captured color ready. Unknown texels masked; walking unavailable."
                    : representation == SceneRepresentation::OBSERVED_MESH
                        ? "Partial observed surfaces ready. Scale unmeasured; walking unavailable."
                        : "Appearance ready. Geometry and walking unavailable.";
                pending_ticket.reset();
            }
        }
        ++frames;
        if (input_trace.is_open() && frames <= 4096) {
            bool reviewed {};
            {
                std::lock_guard lock(state.mutex);
                reviewed = state.reviewed_request != nullptr;
            }
            input_trace
                << nlohmann::json { { "frame", frames },
                                   { "position",
                                    { camera.m_position.m_x,
                                     camera.m_position.m_y,
                                     camera.m_position.m_z } },
                                   { "yaw", camera.m_yaw },
                                   { "fov", camera_fov },
                                   { "pitch", camera.m_pitch },
                                   { "speed", camera.m_speed },
                                   { "up",
                                    { camera.m_up_axis.m_x,
                                     camera.m_up_axis.m_y,
                                     camera.m_up_axis.m_z } },
                                   { "captured", captured },
                                   { "ui_mouse", io.WantCaptureMouse },
                                   { "mouse", { io.MousePos.x, io.MousePos.y } },
                                   { "focused", movement.m_focused },
                                   { "forward", movement.m_forward },
                                   { "text_input", movement.m_text_input },
                                   { "import", show_import },
                                   { "settings", show_settings },
                                   { "remapping", remapping },
                                   { "keys", settings.keys },
                                   { "busy", state.busy.load() },
                                   { "device_recoveries", device_recoveries },
                                   { "reviewed", reviewed },
                                   { "visible", current && !pending_ticket },
                                   { "logical", { K_DISPLAY.x, K_DISPLAY.y } },
                                   { "display_scale", SDL_GetWindowDisplayScale(window.get()) },
                                   { "pixel_density", SDL_GetWindowPixelDensity(window.get()) },
                                   { "layout_scale", scale },
                                   { "widgets", input_widgets } }
                       .dump()
                << '\n';
            input_trace.flush();
            if (!input_trace) {
                throw std::runtime_error("Input trace write failed");
            }
        }
        if (frame_limit && frames >= frame_limit && !state.has_pending_work() && !pending_ticket &&
            reconstruction_request.empty() && review_request.empty() && geometry_request.empty()) {
            if (K_REQUIRE_RECONSTRUCTION_SUCCESS) {
                std::lock_guard lock(state.mutex);
                if (!state.reconstruction_error.empty()) {
                    throw std::runtime_error(state.reconstruction_error);
                }
            }
            if (K_REQUIRE_REVIEW_SUCCESS) {
                std::lock_guard lock(state.mutex);
                if (!state.reviewed_request) {
                    throw std::runtime_error(state.review_error);
                }
            }
            if ((!scene_path.empty() || !fusion_request.empty() ||
                 !depth_space.m_directory.empty()) &&
                !current) {
                std::lock_guard lock(state.mutex);
                throw std::runtime_error(state.status);
            }
            // Allow a requested scene to become visible before capturing it.
            if (screenshot.empty() || screenshot_requested) {
                running = false;
            }
        }
        if (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(20);
        }
    }
    release_mouse();
    state.job.request_stop();
    if (state.job.joinable()) {
        state.job.join();
    }
    if (const auto K_COMPLETED = renderer->wait_for_idle(); !K_COMPLETED) {
        throw std::runtime_error(K_COMPLETED.error().m_message);
    }
    if (const auto K_SAVED = save_preferences(settings_path, settings); !K_SAVED) {
        std::cerr << "Settings not saved: " << K_SAVED.error().m_message << '\n';
    }
    const auto K_STATS = renderer->diagnostics();
    if (input_latency) {
        input_latency->write({ .video_driver = SDL_GetCurrentVideoDriver(),
                              .device = renderer->capabilities().m_device_name.c_str(),
                              .resident_splats = K_STATS.m_resident_splat_count,
                              .validation_errors = K_STATS.m_validation_errors });
    }
    std::cout << nlohmann::json { { "device", renderer->capabilities().m_device_name },
                                 { "frames", K_STATS.m_submitted_frames },
                                 { "resident_splats", K_STATS.m_resident_splat_count },
                                 { "resident_mesh_vertices", K_STATS.m_resident_mesh_vertices },
                                 { "resident_mesh_triangles", K_STATS.m_resident_mesh_triangles },
                                 { "allocated_bytes", K_STATS.m_allocated_bytes },
                                 { "framebuffer_bytes", K_STATS.m_framebuffer_bytes },
                                 { "fp32_compositing", renderer->capabilities().m_fp32_compositing },
                                 { "validation_errors", K_STATS.m_validation_errors },
                                 { "fixture",
                                  is_synthetic_scene(current) ? "synthetic" : "user-selected" } }
                     .dump()
              << '\n';
    return K_STATS.m_validation_errors ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return viewer_main(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Viewer failed: " << error.what() << '\n';
        return 2;
    }
}
