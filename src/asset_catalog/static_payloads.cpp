#include "world_explorer/asset_catalog/static_payloads.hpp"

#include "../core/json_records.hpp"

#include <algorithm>
#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;

Json document(const AdmittedAssetReceipt& receipt,
              const AssetRevisionRef& ref,
              const AssetDependencyContext& expected,
              const std::stop_token& stop) {
    const auto K_BYTES = receipt.copy_blob(ref.m_content_sha256, 64uz * 1024uz, stop);
    if (!K_BYTES) {
        throw RecordFailure(K_BYTES.error().m_code, K_BYTES.error().m_message);
    }
    const auto K_DOC = parse(*K_BYTES, 64uz * 1024uz, stop);
    require(integer(K_DOC.at("schema_version")) == 1,
            "Unsupported static payload schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(K_DOC.at("product_id")) == ref.m_product.m_value &&
                integer(K_DOC.at("revision")) == ref.m_revision &&
                integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value,
            "Static payload identity/context differs",
            ErrorCode::RESYNC_REQUIRED);
    return K_DOC;
}

double unit(const Json& value) {
    const auto K_VALUE = number(value);
    require(K_VALUE >= 0 && K_VALUE <= 1, "Static material channel outside unit range");
    return K_VALUE;
}

}  // namespace

Result<StaticAssetPayloads>
inspect_static_asset_payloads(const AdmittedAssetDefinition& admitted,
                              const AdmittedAssetReceipt& receipt,
                              const std::vector<AssetProvenanceSelection>& provenance,
                              const AssetDependencyContext& expected,
                              const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto& definition = admitted.definition();
        require(admitted.receipt_sha256() == receipt.receipt_sha256() &&
                    definition.m_context.m_world_epoch == expected.m_world_epoch &&
                    definition.m_context.m_pose_revision == expected.m_pose_revision,
                "Static definition receipt/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(definition.m_materials.size() == 1uz && definition.m_lods.empty() &&
                    !definition.m_collider.has_value() &&
                    definition.m_units.m_status == AssetUnitStatus::ARBITRARY &&
                    definition.m_materials.front().m_lighting == AssetLightingMode::GENERATED_PBR,
                "Unsupported static asset payload profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(provenance.size() == definition.m_provenance.size(),
                "Static provenance selection incomplete",
                ErrorCode::RESYNC_REQUIRED);
        std::set<AssetProductId> selected_regions;
        for (const auto& selected : provenance) {
            require(std::ranges::find(definition.m_provenance, selected.m_product) !=
                            definition.m_provenance.end() &&
                        selected_regions.insert(selected.m_product.m_product).second,
                    "Static provenance selection differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        for (const auto& node : receipt.graph().m_products) {
            if (node.m_product == definition.m_geometry ||
                node.m_product == definition.m_materials.front().m_product) {
                auto parents = node.m_dependencies;
                auto selected = node.m_product == definition.m_geometry
                                    ? definition.m_sources
                                    : std::vector { definition.m_geometry };
                std::ranges::sort(parents);
                std::ranges::sort(selected);
                require(parents == selected,
                        "Static payload dependency closure differs",
                        ErrorCode::RESYNC_REQUIRED);
            }
        }
        const auto K_GEOMETRY = document(receipt, definition.m_geometry, expected, stop);
        keys(K_GEOMETRY,
             { "schema_version",
              "kind",
              "product_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "frame",
              "surface",
              "variant_id",
              "positions",
              "normals",
              "uv",
              "triangles" });
        require(K_GEOMETRY.at("kind").get<std::string>() == "GENERATED_STATIC_MESH" &&
                    K_GEOMETRY.at("frame").get<std::string>() ==
                        "GEOMETRY_LOCAL_RIGHT_HANDED_Y_UP" &&
                    K_GEOMETRY.at("surface").get<std::string>() == "OPEN_SURFACE_ALLOWED",
                "Unsupported static mesh representation",
                ErrorCode::UNSUPPORTED_OPERATION);
        StaticAssetPayloads result;
        auto& mesh = result.m_mesh;
        mesh.m_variant = { integer(K_GEOMETRY.at("variant_id")) };
        require(mesh.m_variant == definition.m_materials.front().m_variant,
                "Mesh material variant differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto* name : { "positions", "normals", "uv", "triangles" }) {
            array(K_GEOMETRY.at(name), 512uz);
        }
        const auto& positions = K_GEOMETRY.at("positions");
        const auto& normals = K_GEOMETRY.at("normals");
        const auto& uv = K_GEOMETRY.at("uv");
        require(positions.size() >= 3uz && normals.size() == positions.size() &&
                    uv.size() == positions.size() && !K_GEOMETRY.at("triangles").empty(),
                "Incomplete static mesh arrays");
        Bounds3d bounds;
        for (std::size_t index { 0uz }; index < positions.size(); ++index) {
            check_stop(stop);
            const auto K_POSITION = vector3(positions[index]);
            const auto K_NORMAL = vector3(normals[index]);
            require(std::abs(dot(K_NORMAL, K_NORMAL) - 1) <= 1e-5, "Nonunit static mesh normal");
            array(uv[index], 2uz);
            require(uv[index].size() == 2uz, "Invalid static mesh UV");
            mesh.m_uv.push_back({ unit(uv[index][0uz]), unit(uv[index][1uz]) });
            mesh.m_positions.push_back(K_POSITION);
            mesh.m_normals.push_back(K_NORMAL);
            const auto K_POINT =
                transform_point(definition.m_pivot.m_asset_from_geometry, K_POSITION);
            require(finite(K_POINT), "Static asset bounds overflow");
            if (index == 0uz) {
                bounds = { K_POINT, K_POINT };
            } else {
                bounds.m_minimum = { std::min(bounds.m_minimum.m_x, K_POINT.m_x),
                                    std::min(bounds.m_minimum.m_y, K_POINT.m_y),
                                    std::min(bounds.m_minimum.m_z, K_POINT.m_z) };
                bounds.m_maximum = { std::max(bounds.m_maximum.m_x, K_POINT.m_x),
                                    std::max(bounds.m_maximum.m_y, K_POINT.m_y),
                                    std::max(bounds.m_maximum.m_z, K_POINT.m_z) };
            }
        }
        require(bounds.m_minimum == definition.m_bounds.m_minimum &&
                    bounds.m_maximum == definition.m_bounds.m_maximum,
                "Definition bounds differ from actual pivoted mesh",
                ErrorCode::RESYNC_REQUIRED);
        std::set<std::array<std::uint32_t, 3>> faces;
        std::set<std::uint32_t> used;
        for (const auto& row : K_GEOMETRY.at("triangles")) {
            check_stop(stop);
            array(row, 3uz);
            require(row.size() == 3uz, "Invalid static triangle");
            std::array<std::uint32_t, 3> face {};
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                const auto K_INDEX = integer(row[corner]);
                require(K_INDEX < positions.size(), "Static triangle index outside vertices");
                face[corner] = static_cast<std::uint32_t>(K_INDEX);
                used.insert(face[corner]);
            }
            auto sorted = face;
            std::ranges::sort(sorted);
            require(sorted[0uz] != sorted[1uz] && sorted[1uz] != sorted[2uz] &&
                        faces.insert(sorted).second,
                    "Duplicate/repeated static triangle vertices");
            const auto K_CROSS = cross(mesh.m_positions[face[1uz]] - mesh.m_positions[face[0uz]],
                                       mesh.m_positions[face[2uz]] - mesh.m_positions[face[0uz]]);
            require(finite(K_CROSS) && dot(K_CROSS, K_CROSS) > 0 &&
                        std::isfinite(dot(K_CROSS, K_CROSS)),
                    "Degenerate/overflowing static face");
            for (auto vertex : face) {
                require(dot(K_CROSS, mesh.m_normals[vertex]) > 0,
                        "Static face winding differs from vertex normals");
            }
            const auto& a = mesh.m_uv[face[0uz]];
            const auto& b = mesh.m_uv[face[1uz]];
            const auto& c = mesh.m_uv[face[2uz]];
            require((b[0uz] - a[0uz]) * (c[1uz] - a[1uz]) - (b[1uz] - a[1uz]) * (c[0uz] - a[0uz]) !=
                        0,
                    "Degenerate static face UV");
            mesh.m_triangles.push_back(face);
        }
        require(used.size() == positions.size(), "Unused static mesh vertices");
        const auto& binding = definition.m_materials.front();
        const auto K_MATERIAL = document(receipt, binding.m_product, expected, stop);
        keys(K_MATERIAL,
             { "schema_version",
              "kind",
              "product_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "geometry",
              "brdf",
              "color_space",
              "normal",
              "base_color",
              "metallic",
              "roughness",
              "emission",
              "occlusion" });
        require(K_MATERIAL.at("kind").get<std::string>() == "GENERATED_CONSTANT_PBR" &&
                    K_MATERIAL.at("brdf").get<std::string>() == "METALLIC_ROUGHNESS_GGX" &&
                    K_MATERIAL.at("color_space").get<std::string>() == "LINEAR_SRGB" &&
                    vector3(K_MATERIAL.at("normal")) == Vec3d { 0, 0, 1 },
                "Unsupported static material profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        const auto& geometry = K_MATERIAL.at("geometry");
        keys(geometry, { "product_id", "revision", "content_sha256" });
        require(integer(geometry.at("product_id")) == definition.m_geometry.m_product.m_value &&
                    integer(geometry.at("revision")) == definition.m_geometry.m_revision &&
                    digest(geometry.at("content_sha256")) == definition.m_geometry.m_content_sha256,
                "Material references stale geometry",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_COLOR = vector3(K_MATERIAL.at("base_color"));
        const auto K_EMISSION = vector3(K_MATERIAL.at("emission"));
        require(K_COLOR.m_x >= 0 && K_COLOR.m_x <= 1 && K_COLOR.m_y >= 0 && K_COLOR.m_y <= 1 &&
                    K_COLOR.m_z >= 0 && K_COLOR.m_z <= 1 && K_EMISSION.m_x >= 0 &&
                    K_EMISSION.m_x <= 64 && K_EMISSION.m_y >= 0 && K_EMISSION.m_y <= 64 &&
                    K_EMISSION.m_z >= 0 && K_EMISSION.m_z <= 64,
                "Invalid static material color/emission");
        result.m_material = { K_COLOR,
                             K_EMISSION,
                             unit(K_MATERIAL.at("metallic")),
                             unit(K_MATERIAL.at("roughness")),
                             unit(K_MATERIAL.at("occlusion")) };
        for (const auto& selection : provenance) {
            const auto K_REGION = inspect_asset_region_provenance(
                receipt, selection.m_product.m_product, selection.m_region, expected, stop);
            if (!K_REGION) {
                return std::unexpected(K_REGION.error());
            }
            const auto& domain = selection.m_region.m_domain;
            require(K_REGION->m_coverage[0uz] == 0 && K_REGION->m_coverage[1uz] == 0 &&
                        K_REGION->m_coverage[2uz] == 0,
                    "Static profile requires generated/edited regions");
            require((domain.m_kind == AssetRegionKind::GEOMETRY_FACES &&
                     domain.m_elements == mesh.m_triangles.size()) ||
                        (domain == AssetRegionDomain { AssetRegionKind::MATERIAL_TEXELS, 1, 1, 1 }),
                    "Static provenance domain differs from actual payload");
        }
        check_stop(stop);
        return result;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Static payload allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
