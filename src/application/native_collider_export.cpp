#include "native_export_internal.hpp"

#include <cmath>

namespace world_explorer::native_export_detail {
namespace {

SimilarityTransform compose(const SimilarityTransform& outer, const SimilarityTransform& inner) {
    const auto& a = outer.m_rotation;
    const auto& b = inner.m_rotation;
    return { { a.m_w * b.m_w - a.m_x * b.m_x - a.m_y * b.m_y - a.m_z * b.m_z,
             a.m_w * b.m_x + a.m_x * b.m_w + a.m_y * b.m_z - a.m_z * b.m_y,
             a.m_w * b.m_y - a.m_x * b.m_z + a.m_y * b.m_w + a.m_z * b.m_x,
             a.m_w * b.m_z + a.m_x * b.m_y - a.m_y * b.m_x + a.m_z * b.m_w },
            transform_point(outer, inner.m_translation),
            outer.m_scale * inner.m_scale };
}

Json placement(const SimilarityTransform& value) {
    const auto& q = value.m_rotation;
    return { { "rotation", { q.m_w, q.m_x, q.m_y, q.m_z } },
            { "translation", xyz(value.m_translation) },
            { "scale", value.m_scale } };
}

}  // namespace

Json compile_colliders(const NativeExportRequest& request,
                       Files& files,
                       const std::stop_token& stop) {
    const auto& assets = selected(request.m_assets);
    const auto& receipt = selected(request.m_receipt);
    const auto& policy = selected(request.m_colliders);
    require(std::isfinite(policy.m_world_position_units) && policy.m_world_position_units >= 0 &&
                policy.m_world_position_units <= 1,
            "Invalid independent collider world-position limit",
            ErrorCode::INVALID_ARGUMENT);
    auto entries = Json::array();
    for (const auto& definition : assets.instances().definitions()) {
        check_stop(stop);
        const auto K_SOURCE = std::ranges::find_if(assets.sources(), [&](const auto& item) {
            return item.m_definition.m_asset == definition.reference().m_asset;
        });
        require(K_SOURCE != assets.sources().end(), "Collider export definition missing");
        if (!K_SOURCE->m_collider) {
            continue;
        }
        const auto& collider = selected(K_SOURCE->m_collider);
        require(collider.receipt_sha256() == receipt.receipt_sha256() &&
                    collider.base().selection().m_product == definition.definition().m_geometry,
                "Collider export source differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_PREFIX =
            "colliders/" + std::to_string(definition.reference().m_asset.m_value) + "/";
        const auto& product = collider.selection().m_product;
        const auto& base = collider.base().selection().m_product;
        const auto K_PROVENANCE =
            std::ranges::find_if(K_SOURCE->m_provenance, [&](const auto& item) {
                return item.m_region.m_target == product;
            });
        require(K_PROVENANCE != K_SOURCE->m_provenance.end(), "Collider export provenance missing");
        const auto K_PROVENANCE_PATH =
            "provenance/" + std::to_string(K_PROVENANCE->m_product.m_product.m_value) + "-" +
            std::to_string(K_PROVENANCE->m_product.m_revision) + ".json";
        files.add(K_PREFIX + "root.json",
                  take(receipt.copy_blob(product.m_content_sha256, 1024uz * 1024uz, stop)));
        files.add(K_PREFIX + "base.json",
                  take(receipt.copy_blob(base.m_content_sha256, 1024uz * 1024uz, stop)));
        const auto K_BYTES = collider.canonical_bytes();
        files.add(K_PREFIX + "triangles.bin", { K_BYTES.begin(), K_BYTES.end() });
        const auto& pivot = definition.definition().m_pivot.m_asset_from_geometry;
        transform_profile(pivot);
        auto instances = Json::array();
        double maximum {};
        const auto& originals = assets.instances().instances().m_instances;
        for (std::size_t node { 0uz }; node < originals.size(); ++node) {
            const auto& instance = originals[node];
            if (instance.m_definition != definition.reference()) {
                continue;
            }
            check_stop(stop);
            transform_profile(instance.m_world_from_asset);
            const auto K_WORLD = compose(instance.m_world_from_asset, pivot);
            require(valid(K_WORLD), "Collider composed world frame invalid");
            const auto K_CHECK = [&](Vec3d point) {
                const auto K_ACTUAL = transform_point(K_WORLD, point);
                const auto K_EXPECTED =
                    transform_point(instance.m_world_from_asset, transform_point(pivot, point));
                const auto K_DELTA = K_ACTUAL - K_EXPECTED;
                const auto K_ERROR = std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z);
                require(finite(K_ACTUAL) && finite(K_EXPECTED) && std::isfinite(K_ERROR),
                        "Collider world-frame residual overflow");
                maximum = std::max(maximum, K_ERROR);
            };
            for (auto point : { Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 } }) {
                K_CHECK(point);
            }
            for (const auto& point : collider.mesh().m_positions) {
                check_stop(stop);
                K_CHECK(point);
            }
            instances.push_back({ { "instance_id", std::to_string(instance.m_instance.m_value) },
                                 { "node_index", node },
                                 { "world_from_collider", placement(K_WORLD) } });
        }
        require(!instances.empty() && maximum <= policy.m_world_position_units,
                "Collider placement exceeds independently selected world-position limit",
                ErrorCode::UNSUPPORTED_OPERATION);
        entries.push_back(
            { { "definition", reference(definition.reference()) },
             { "product", reference(product) },
             { "base_geometry", reference(base) },
             { "purpose", "SYNTHETIC_GAMEPLAY" },
             { "body", "STATIC_STRUCTURAL" },
             { "units", "ARBITRARY" },
             { "frame", "BASE_GEOMETRY_LOCAL" },
             { "backend_shape", "NOT_COOKED" },
             { "root", { { "path", K_PREFIX + "root.json" }, { "sha256", product.m_content_sha256 } } },
             { "base", { { "path", K_PREFIX + "base.json" }, { "sha256", base.m_content_sha256 } } },
             { "cook",
              { { "path", K_PREFIX + "triangles.bin" },
               { "sha256", collider.digest() },
               { "format", "WE_NATIVE_COLLIDER_TRIANGLES_V1" } } },
             { "provenance",
              { { "product", reference(K_PROVENANCE->m_product) },
               { "path", K_PROVENANCE_PATH },
               { "sha256", K_PROVENANCE->m_product.m_content_sha256 } } },
             { "vertex_count", collider.mesh().m_positions.size() },
             { "triangle_count", collider.mesh().m_triangles.size() },
             { "source_faces", collider.mesh().m_source_faces },
             { "asset_from_collider", placement(pivot) },
             { "maximum_world_position_error_units", maximum },
             { "instances", instances } });
    }
    require(!entries.empty(),
            "Collider companion requires at least one admitted collider",
            ErrorCode::INVALID_ARGUMENT);
    return { { "schema_version", 1 },
            { "kind", "NATIVE_COLLIDER_COMPANION" },
            { "source_receipt_sha256", receipt.receipt_sha256() },
            { "source_instances_sha256", assets.instances().sha256() },
            { "world_epoch", std::to_string(request.m_expected.m_world_epoch.m_value) },
            { "pose_revision", std::to_string(request.m_expected.m_pose_revision.m_value) },
            { "geometry_scope", "LOD0" },
            { "purpose", "SYNTHETIC_GAMEPLAY" },
            { "backend_shape", "NOT_COOKED" },
            { "walking_enabled", false },
            { "world_position_error_limit_units", policy.m_world_position_units },
            { "assets", entries } };
}

}  // namespace world_explorer::native_export_detail
