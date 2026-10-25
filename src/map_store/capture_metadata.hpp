#pragma once

#include "../core/json_records.hpp"
#include "world_explorer/map_store/capture_selection.hpp"

#include <array>
#include <limits>

namespace world_explorer::capture_metadata_detail {

using namespace json_record_detail;

inline std::int64_t signed_integer(const Json& value) {
    require(value.is_number_integer(), "Expected signed metadata integer");
    require(!value.is_number_unsigned() ||
                value.get<std::uint64_t>() <=
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            "Metadata integer overflow");
    return value.get<std::int64_t>();
}

inline AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

inline ObjectViewSelection view(const Json& row) {
    keys(
        row,
        { "capture_id", "frame_id", "calibration_id", "frame_pose_revision", "image", "pose_asset" });
    const ObjectViewSelection K_VIEW { { integer(row.at("capture_id")) },
                                      { integer(row.at("frame_id")) },
                                      { integer(row.at("calibration_id")) },
                                      { integer(row.at("frame_pose_revision")) },
                                      reference(row.at("image")),
                                      reference(row.at("pose_asset")) };
    return K_VIEW;
}

inline void camera(const Json& value, const CaptureFrameInputs& inputs) {
    keys(value, { "view", "pts", "time_base", "capture_time_ns", "world_from_camera" });
    require(view(value.at("view")) == inputs.m_view,
            "Capture camera selection differs",
            ErrorCode::RESYNC_REQUIRED);
    const auto& time_base = value.at("time_base");
    array(time_base, 2uz);
    const auto& timestamp = inputs.m_observation.m_timestamp;
    require(time_base.size() == 2uz && signed_integer(value.at("pts")) == timestamp.m_pts &&
                signed_integer(time_base[0uz]) == timestamp.m_time_base_numerator &&
                signed_integer(time_base[1uz]) == timestamp.m_time_base_denominator &&
                signed_integer(value.at("capture_time_ns")) ==
                    inputs.m_observation.m_capture_time.count(),
            "Capture timestamps differ",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_POSE = transform(value.at("world_from_camera"));
    const auto& expected = inputs.m_world_from_camera;
    require(K_POSE.m_rotation.m_w == expected.m_rotation.m_w &&
                K_POSE.m_rotation.m_x == expected.m_rotation.m_x &&
                K_POSE.m_rotation.m_y == expected.m_rotation.m_y &&
                K_POSE.m_rotation.m_z == expected.m_rotation.m_z &&
                K_POSE.m_translation == expected.m_translation &&
                K_POSE.m_scale == expected.m_scale,
            "Capture decoded world pose differs",
            ErrorCode::RESYNC_REQUIRED);
}

inline void calibration(const Json& value, const CalibrationRecord& expected) {
    keys(value,
         { "calibration_id",
          "width",
          "height",
          "model",
          "intrinsics",
          "distortion",
          "image_transform" });
    require(integer(value.at("calibration_id")) == expected.m_id.m_value &&
                integer(value.at("width")) == expected.m_width &&
                integer(value.at("height")) == expected.m_height &&
                value.at("model").get<std::string>() ==
                    (expected.m_model == CalibrationModel::PINHOLE ? "PINHOLE" : "OPENCV"),
            "Capture calibration identity/model/dimensions differ",
            ErrorCode::RESYNC_REQUIRED);
    const std::array K_CAMERA { expected.m_camera.m_fx,
                               expected.m_camera.m_fy,
                               expected.m_camera.m_cx,
                               expected.m_camera.m_cy };
    const auto K_CHECK = [&](const char* field, const std::array<double, 4>& values) {
        const auto& row = value.at(field);
        array(row, 4uz);
        require(row.size() == 4uz, "Capture calibration vector size differs");
        for (std::size_t index { 0uz }; index < values.size(); ++index) {
            require(number(row[index]) == values[index],
                    "Capture calibration values differ",
                    ErrorCode::RESYNC_REQUIRED);
        }
    };
    K_CHECK("intrinsics", K_CAMERA);
    K_CHECK("distortion", expected.m_distortion);
    K_CHECK("image_transform", expected.m_image_transform);
}

}  // namespace world_explorer::capture_metadata_detail
