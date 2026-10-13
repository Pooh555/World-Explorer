cmake_minimum_required(VERSION 3.30)
project(world_explorer VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

# -----------------------------------------------------------------------------
# C++ Standard & Configuration
# -----------------------------------------------------------------------------
set(WE_CXX_STANDARD 26 CACHE STRING "Explicit project standard: 26 or compatibility 23")

if(NOT WE_CXX_STANDARD MATCHES "^(23|26)$")
  message(FATAL_ERROR "WE_CXX_STANDARD must be 23 or 26")
endif()

set(CMAKE_CXX_STANDARD ${WE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WE_BUILD_VIEWER "Build the native SDL/Vulkan viewer" ON)
option(WE_SYSTEM_DEVELOPMENT_DEPS "Use measured local system libraries (not release qualification)" OFF)

set(WE_SANITIZER "" CACHE STRING "GNU/Clang sanitizer set, thread is separate from address")

find_package(Threads REQUIRED)
message(STATUS "Required C++${WE_CXX_STANDARD} APIs: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER})")
try_compile(WE_REQUIRED_FEATURES
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/feature_probes/required.cpp"
  CXX_STANDARD ${WE_CXX_STANDARD}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  LINK_LIBRARIES Threads::Threads
  OUTPUT_VARIABLE WE_FEATURE_LOG
)

if(NOT WE_REQUIRED_FEATURES)
  message(FATAL_ERROR "The selected compiler AND standard library must provide std::expected, std::jthread, std::stop_token and std::span. Select a supported toolchain; C++23 mode alone cannot add missing library APIs.\n${WE_FEATURE_LOG}")
endif()

# -----------------------------------------------------------------------------
# Dependencies & Helper Functions
# -----------------------------------------------------------------------------
include(cmake/dependencies.cmake)

function(we_target target)
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_compile_features(${target} PUBLIC cxx_std_${WE_CXX_STANDARD})

  if(WIN32)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

    if(WE_SANITIZER)
      target_compile_options(${target} PUBLIC -fsanitize=${WE_SANITIZER} -fno-omit-frame-pointer)
      target_link_options(${target} PUBLIC -fsanitize=${WE_SANITIZER})
    endif()
  endif()
endfunction()

# -----------------------------------------------------------------------------
# Core Libraries & Application Targets
# -----------------------------------------------------------------------------
add_library(core src/core/hash.cpp src/core/file_lock.cpp)
we_target(core)
target_link_libraries(core PRIVATE OpenSSL::Crypto PUBLIC Threads::Threads)

add_library(capture INTERFACE)
target_link_libraries(capture INTERFACE core)

add_library(reconstruction_api INTERFACE)
target_link_libraries(reconstruction_api INTERFACE core)

set(WE_WIRE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/worker")
file(MAKE_DIRECTORY "${WE_WIRE_DIRECTORY}/python")
file(COPY "${WE_FLATBUFFERS_PYTHON}" DESTINATION "${WE_WIRE_DIRECTORY}/python")
add_custom_command(OUTPUT "${WE_WIRE_DIRECTORY}/events_generated.h" "${WE_WIRE_DIRECTORY}/commands_generated.h"
  BYPRODUCTS "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/envelope.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/handshake.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/progress.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/completed.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/failure.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/artifact_reference.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/payload.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/command_acknowledged.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/command_kind.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/control/command_envelope.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/control/start_command.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/control/cancel_command.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/control/command_payload.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/control/__init__.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/__init__.py"
    "${WE_WIRE_DIRECTORY}/python/world_explorer/wire/__init__.py"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${WE_WIRE_DIRECTORY}"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --cpp --scoped-enums -o "${WE_WIRE_DIRECTORY}" "${PROJECT_SOURCE_DIR}/schemas/worker/events.fbs"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --python -o "${WE_WIRE_DIRECTORY}/python" "${PROJECT_SOURCE_DIR}/schemas/worker/events.fbs"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --cpp --scoped-enums -o "${WE_WIRE_DIRECTORY}" "${PROJECT_SOURCE_DIR}/schemas/worker/commands.fbs"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --python -o "${WE_WIRE_DIRECTORY}/python" "${PROJECT_SOURCE_DIR}/schemas/worker/commands.fbs"
  DEPENDS ${WE_FLATC} schemas/worker/events.fbs schemas/worker/commands.fbs
  VERBATIM)
add_library(reconstruction_adapters src/reconstruction_adapters/process.cpp
  src/reconstruction_adapters/worker_protocol.cpp src/reconstruction_adapters/decoded_frame_source.cpp
  src/reconstruction_adapters/worker_artifacts.cpp
  src/reconstruction_adapters/training_camera.cpp
  src/reconstruction_adapters/completed_poses.cpp
  src/reconstruction_adapters/completed_pose_replay.cpp
  src/reconstruction_adapters/batch_poses.cpp
  "${WE_WIRE_DIRECTORY}/events_generated.h" "${WE_WIRE_DIRECTORY}/commands_generated.h")
we_target(reconstruction_adapters)
target_include_directories(reconstruction_adapters SYSTEM PRIVATE "${WE_WIRE_DIRECTORY}" "${WE_FLATBUFFERS_INCLUDE}")
target_compile_definitions(reconstruction_adapters PRIVATE WE_WIRE_BINDINGS="${WE_WIRE_DIRECTORY}/python")
target_link_libraries(reconstruction_adapters PUBLIC reconstruction_api PRIVATE nlohmann_json::nlohmann_json)

set(WE_POSE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/map")
add_custom_command(OUTPUT "${WE_POSE_DIRECTORY}/poses_generated.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${WE_POSE_DIRECTORY}"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --cpp --scoped-enums --gen-mutable -o "${WE_POSE_DIRECTORY}" "${PROJECT_SOURCE_DIR}/schemas/map/poses.fbs"
  DEPENDS ${WE_FLATC} schemas/map/poses.fbs
  VERBATIM)
add_custom_command(OUTPUT "${WE_POSE_DIRECTORY}/checkpoint_generated.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${WE_POSE_DIRECTORY}"
  COMMAND $<TARGET_FILE:${WE_FLATC}> --cpp --scoped-enums --gen-mutable -o "${WE_POSE_DIRECTORY}" "${PROJECT_SOURCE_DIR}/schemas/map/checkpoint.fbs"
  DEPENDS ${WE_FLATC} schemas/map/checkpoint.fbs schemas/map/poses.fbs
  VERBATIM)
add_library(map_store src/map_store/map_store.cpp src/map_store/pose_asset.cpp
  src/map_store/map_codec.cpp src/map_store/map_files.cpp src/map_store/map_persistence.cpp
  "${WE_POSE_DIRECTORY}/poses_generated.h" "${WE_POSE_DIRECTORY}/checkpoint_generated.h")
we_target(map_store)
target_include_directories(map_store SYSTEM PRIVATE "${WE_POSE_DIRECTORY}" "${WE_FLATBUFFERS_INCLUDE}")
target_link_libraries(map_store PUBLIC reconstruction_api)
target_link_libraries(map_store PRIVATE nlohmann_json::nlohmann_json)

add_library(scene_store src/scene_store/scene.cpp src/scene_store/scene_store.cpp src/scene_store/bundle.cpp)
we_target(scene_store)
target_link_libraries(scene_store PUBLIC core PRIVATE nlohmann_json::nlohmann_json)

add_library(navigation src/navigation/fly_camera.cpp)
we_target(navigation)
target_link_libraries(navigation PUBLIC core)

add_library(application src/application/commands.cpp src/application/scenes.cpp
  src/application/reconstruction_progress.cpp
  src/application/scene_import.cpp src/application/source_view.cpp
  src/application/reconstruction_job.cpp src/application/job_validation.cpp src/application/pose_replay.cpp
  src/application/pose_pipeline.cpp src/application/pose_solve.cpp src/application/pose_export.cpp
  src/application/map_commands.cpp src/application/pose_preparation.cpp src/application/video_preflight.cpp
  src/application/video_inspection.cpp src/application/owned_job.cpp src/application/video_decode.cpp
  src/application/pose_job.cpp)
we_target(application)
target_link_libraries(application PUBLIC scene_store navigation reconstruction_adapters map_store PRIVATE nlohmann_json::nlohmann_json)

add_executable(we_process apps/we_process/main.cpp)
we_target(we_process)
target_link_libraries(we_process PRIVATE application)
target_compile_definitions(we_process PRIVATE WE_WORKER_SCRIPT="${PROJECT_SOURCE_DIR}/workers/worker.py")

add_library(render_core src/render_core/update_sink.cpp src/render_core/camera.cpp src/render_core/tile_plan.cpp)
we_target(render_core)
target_link_libraries(render_core PUBLIC scene_store)

# -----------------------------------------------------------------------------
# Viewer & Vulkan Components
# -----------------------------------------------------------------------------
if(WE_BUILD_VIEWER)
  add_library(render_vulkan
    src/render_vulkan/renderer.cpp
    src/render_vulkan/vulkan_device.cpp
    src/render_vulkan/vulkan_swapchain.cpp)
  we_target(render_vulkan)
  target_link_libraries(render_vulkan PUBLIC render_core PRIVATE SDL3::SDL3 Vulkan::Vulkan)

  set(WE_SHADER_OUTPUTS)
  foreach(shader project.comp radix_histogram.comp radix_scan.comp radix_offsets.comp radix_scatter.comp tile_count.comp tile_scan.comp tile_emit.comp tile_ranges.comp composite.comp composite.frag present.vert)
    set(output "${PROJECT_BINARY_DIR}/shaders/${shader}.spv")

    add_custom_command(OUTPUT "${output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders"
      COMMAND Vulkan::glslc --target-env=vulkan1.3 --target-spv=spv1.3 -I "${PROJECT_SOURCE_DIR}/shaders" "${PROJECT_SOURCE_DIR}/shaders/${shader}" -o "${output}"
      DEPENDS "shaders/${shader}" shaders/common.glsl
      VERBATIM
    )

    list(APPEND WE_SHADER_OUTPUTS "${output}")
  endforeach()

  add_custom_target(we_shaders DEPENDS ${WE_SHADER_OUTPUTS})

  add_executable(we_viewer apps/we_viewer/main.cpp apps/we_viewer/job_state.cpp
    apps/we_viewer/preferences.cpp)
  we_target(we_viewer)
  target_link_libraries(we_viewer PRIVATE application render_vulkan we_imgui nlohmann_json::nlohmann_json)
  target_compile_definitions(we_viewer PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders" WE_WORKER_SCRIPT="${PROJECT_SOURCE_DIR}/workers/worker.py")
  add_dependencies(we_viewer we_shaders)

  add_executable(we_gpu_smoke tests/gpu/smoke.cpp)
  we_target(we_gpu_smoke)
  target_link_libraries(we_gpu_smoke PRIVATE application render_vulkan SDL3::SDL3)
  target_compile_definitions(we_gpu_smoke PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
  add_dependencies(we_gpu_smoke we_shaders)

  add_executable(we_gpu_reference tests/gpu/reference.cpp)
  we_target(we_gpu_reference)
  target_link_libraries(we_gpu_reference PRIVATE render_vulkan SDL3::SDL3 nlohmann_json::nlohmann_json)
  target_compile_definitions(we_gpu_reference PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
  add_dependencies(we_gpu_reference we_shaders)

  add_executable(we_gpu_tile_plans tests/gpu/tile_plans.cpp)
  we_target(we_gpu_tile_plans)
  target_link_libraries(we_gpu_tile_plans PRIVATE render_vulkan SDL3::SDL3 nlohmann_json::nlohmann_json)
  target_compile_definitions(we_gpu_tile_plans PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
  add_dependencies(we_gpu_tile_plans we_shaders)

  add_executable(we_gpu_viewer_regression tests/gpu/viewer_regression.cpp)
  we_target(we_gpu_viewer_regression)
  target_link_libraries(we_gpu_viewer_regression PRIVATE application render_vulkan SDL3::SDL3 nlohmann_json::nlohmann_json)
  target_compile_definitions(we_gpu_viewer_regression PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
  add_dependencies(we_gpu_viewer_regression we_shaders)

  add_executable(we_gpu_reconstruction_handoff tests/gpu/reconstruction_handoff.cpp
    apps/we_viewer/job_state.cpp)
  we_target(we_gpu_reconstruction_handoff)
  target_include_directories(we_gpu_reconstruction_handoff PRIVATE "${PROJECT_SOURCE_DIR}/apps/we_viewer")
  target_link_libraries(we_gpu_reconstruction_handoff PRIVATE application render_vulkan SDL3::SDL3 nlohmann_json::nlohmann_json)
  target_compile_definitions(we_gpu_reconstruction_handoff PRIVATE WE_SHADER_DIRECTORY="${PROJECT_BINARY_DIR}/shaders")
  add_dependencies(we_gpu_reconstruction_handoff we_shaders)

  install(TARGETS we_viewer RUNTIME DESTINATION bin)
  install(FILES ${WE_SHADER_OUTPUTS} DESTINATION share/world-explorer/shaders)
endif()

# -----------------------------------------------------------------------------
# Installation & Testing
# -----------------------------------------------------------------------------
install(TARGETS we_process RUNTIME DESTINATION bin)
install(DIRECTORY workers/ DESTINATION share/world-explorer/workers PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY "${WE_WIRE_DIRECTORY}/python/" DESTINATION share/world-explorer/workers/wire_bindings PATTERN "__pycache__" EXCLUDE)

if(BUILD_TESTING)
  include(Catch)
  file(GLOB WE_CORE_TESTS CONFIGURE_DEPENDS tests/unit/*_tests.cpp)

  add_executable(we_core_tests ${WE_CORE_TESTS})
  we_target(we_core_tests)
  target_link_libraries(we_core_tests PRIVATE application map_store render_core Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
  target_include_directories(we_core_tests SYSTEM PRIVATE "${WE_WIRE_DIRECTORY}" "${WE_POSE_DIRECTORY}" "${WE_FLATBUFFERS_INCLUDE}")
  target_compile_definitions(we_core_tests PRIVATE WE_TEST_WIRE_BINDINGS="${WE_WIRE_DIRECTORY}/python")
  catch_discover_tests(we_core_tests PROPERTIES LABELS "cpu")

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  target_compile_definitions(we_core_tests PRIVATE WE_TEST_PYTHON="${Python3_EXECUTABLE}" WE_TEST_PROTOCOL_MODULE="${PROJECT_SOURCE_DIR}/workers")

  if(WE_BUILD_VIEWER)
    add_executable(we_viewer_tests tests/ui/preferences_tests.cpp tests/ui/job_state_tests.cpp
      apps/we_viewer/preferences.cpp apps/we_viewer/job_state.cpp)
    we_target(we_viewer_tests)
    target_compile_definitions(we_viewer_tests PRIVATE WE_TEST_PYTHON="${Python3_EXECUTABLE}"
      WE_TEST_PROTOCOL_MODULE="${PROJECT_SOURCE_DIR}/workers")
    target_include_directories(we_viewer_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/we_viewer")
    target_link_libraries(we_viewer_tests PRIVATE application SDL3::SDL3 we_imgui Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
    catch_discover_tests(we_viewer_tests PROPERTIES LABELS "cpu;viewer")
  endif()

  add_test(NAME worker_integration COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/integration -p "worker*_test.py" -v)
  set_tests_properties(worker_integration PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS "cpu;worker")
endif()
