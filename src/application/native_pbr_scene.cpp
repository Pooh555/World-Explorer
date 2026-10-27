#include "world_explorer/application/native_pbr_scene.hpp"

#include "../core/json_records.hpp"
#include "../scene_store/gltf_internal.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>
#include <charconv>
#include <span>

namespace world_explorer {
namespace {

using namespace json_record_detail;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return std::move(*result);
}

std::uint64_t id(const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    std::uint64_t result {};
    const auto K_PARSE = std::from_chars(K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), result);
    require(K_PARSE.ec == std::errc {} && K_PARSE.ptr == K_TEXT.data() + K_TEXT.size() && result,
            "Invalid native PBR identity");
    return result;
}

const std::vector<std::byte>& file(const AdmittedNativeExport& package, const std::string& path) {
    const auto K_FOUND = std::ranges::find(package.files(), path, &ExportPackageFile::m_path);
    require(K_FOUND != package.files().end(), "Native PBR package closure missing");
    return K_FOUND->m_bytes;
}

std::string text(const std::vector<std::byte>& data) {
    return { reinterpret_cast<const char*>(data.data()), data.size() };
}

std::uint32_t word(const std::vector<std::byte>& data, std::size_t at) {
    require(at <= data.size() && data.size() - at >= 4uz, "Truncated native PBR geometry");
    std::uint32_t value {};
    for (std::size_t i { 0uz }; i < 4uz; ++i) {
        value |= std::to_integer<std::uint32_t>(data[at + i]) << (8uz * i);
    }
    return value;
}

Quatd multiply(Quatd a, Quatd b) {
    return { a.m_w * b.m_w - a.m_x * b.m_x - a.m_y * b.m_y - a.m_z * b.m_z,
            a.m_w * b.m_x + a.m_x * b.m_w + a.m_y * b.m_z - a.m_z * b.m_y,
            a.m_w * b.m_y - a.m_x * b.m_z + a.m_y * b.m_w + a.m_z * b.m_x,
            a.m_w * b.m_z + a.m_x * b.m_y - a.m_y * b.m_x + a.m_z * b.m_w };
}

SimilarityTransform relative(const SimilarityTransform& base, const SimilarityTransform& value) {
    const auto& q = base.m_rotation;
    return { multiply({ q.m_w, -q.m_x, -q.m_y, -q.m_z }, value.m_rotation),
            inverse_transform_point(base, value.m_translation),
            value.m_scale / base.m_scale };
}

}  // namespace

Result<SceneSnapshot> compile_native_pbr_scene(const AdmittedNativeExport& package,
                                               const AssetDependencyContext& expected,
                                               const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(package.context().m_world_epoch == expected.m_world_epoch &&
                    package.context().m_pose_revision == expected.m_pose_revision,
                "Native PBR context differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_MANIFEST_TEXT = text(file(package, "manifest.json"));
        const auto K_MANIFEST =
            parse(std::as_bytes(std::span(K_MANIFEST_TEXT)), 1024uz * 1024uz, stop);
        require(sha256(std::as_bytes(std::span(K_MANIFEST_TEXT))) == package.manifest_sha256(),
                "Native PBR manifest differs");
        const auto K_SCENE_TEXT = text(file(package, "scene.json"));
        const auto K_SCENE = parse(std::as_bytes(std::span(K_SCENE_TEXT)), 1024uz * 1024uz, stop);
        auto scene = std::make_shared<SceneState>();
        scene->m_id = { id(K_MANIFEST.at("scene_id")) };
        scene->m_epoch = expected.m_world_epoch;
        scene->m_revision = { id(K_MANIFEST.at("scene_revision")) };
        const bool K_SYNTHETIC = K_MANIFEST.at("synthetic_diagnostic").get<bool>();
        scene->m_provenance = {
            { "source_kind", K_SYNTHETIC ? "synthetic_diagnostic" : "native_asset_draft" },
            { "profile", K_MANIFEST.at("profile").get<std::string>() },
            { "qualification", "UNQUALIFIED_NATIVE_PBR" } };
        for (const auto& source : K_MANIFEST.at("assets")) {
            check_stop(stop);
            auto chunk = std::make_shared<SceneChunk>();
            const auto& ref = source.at("definition");
            chunk->m_id = { id(ref.at("asset_id")) };
            chunk->m_generation = id(ref.at("revision"));
            chunk->m_pose_revision = { expected.m_pose_revision.m_value };
            chunk->m_geometry_valid = true;
            auto& mesh = chunk->m_mesh.emplace();
            auto& pbr = mesh.m_pbr.emplace();
            pbr.m_definition = {
                { chunk->m_id.m_value }, chunk->m_generation, ref.at("sha256").get<std::string>() };
            pbr.m_epoch = expected.m_world_epoch;
            pbr.m_pose_revision = expected.m_pose_revision;
            pbr.m_export_manifest = K_MANIFEST_TEXT;
            pbr.m_export_manifest_sha256 = package.manifest_sha256();
            pbr.m_geometry_sha256 = source.at("geometry_sha256").get<std::string>();
            pbr.m_synthetic = K_SYNTHETIC;
            pbr.m_manual_ancestor = K_MANIFEST.at("manual_ancestor").get<bool>();
            pbr.m_scene_document = K_SCENE_TEXT;
            pbr.m_asset_document = text(file(package, source.at("asset_path").get<std::string>()));
            const auto K_ASSET =
                parse(std::as_bytes(std::span(pbr.m_asset_document)), 1024uz * 1024uz, stop);
            const auto K_GEOMETRY_PATH = source.at("geometry_path").get<std::string>();
            const auto& geometry = file(package, K_GEOMETRY_PATH);
            require(sha256(geometry) == pbr.m_geometry_sha256, "Native PBR geometry differs");
            const auto K_VERTICES = K_ASSET.at("accessors").at(0uz).at("count").get<std::size_t>();
            require(K_VERTICES <= 8192uz && K_VERTICES <= geometry.size() / 48uz,
                    "Native PBR vertex count differs");
            mesh.m_vertices.resize(K_VERTICES);
            pbr.m_uv.resize(K_VERTICES);
            pbr.m_tangents.resize(K_VERTICES);
            for (std::size_t i { 0uz }; i < K_VERTICES; ++i) {
                check_stop(stop);
                std::array<float, 12> values {};
                for (std::size_t c { 0uz }; c < 12uz; ++c) {
                    values[c] = std::bit_cast<float>(word(geometry, i * 48uz + c * 4uz));
                }
                mesh.m_vertices[i] = { { values[0uz], values[1uz], values[2uz] },
                                      { values[3uz], values[4uz], values[5uz] } };
                pbr.m_tangents[i] = { values[6uz], values[7uz], values[8uz], values[9uz] };
                pbr.m_uv[i] = { values[10uz], values[11uz] };
            }
            const auto& source_materials = source.at("materials");
            for (std::size_t group { 0uz }; group < source_materials.size(); ++group) {
                const auto& material = source_materials[group];
                PbrMaterial out;
                out.m_variant = { id(material.at("variant_id")) };
                const auto& product = material.at("product");
                out.m_product = { { id(product.at("product_id")) },
                                 id(product.at("revision")),
                                 product.at("sha256").get<std::string>() };
                out.m_lighting = material.at("lighting").get<std::string>() == "ESTIMATED_PBR"
                                     ? AssetLightingMode::ESTIMATED_PBR
                                     : AssetLightingMode::GENERATED_PBR;
                const auto& sampler = K_ASSET.at("samplers").at(group);
                out.m_linear_filter = sampler.at("magFilter").get<unsigned>() == 9729;
                out.m_repeat = sampler.at("wrapS").get<unsigned>() == 10497;
                for (std::size_t image { 0uz }; image < 4uz; ++image) {
                    const auto& pin = material.at("images").at(image);
                    auto decoded = gltf_detail::decode_png(
                        file(package, pin.at("path").get<std::string>()), stop);
                    out.m_width = decoded.m_width;
                    out.m_height = decoded.m_height;
                    out.m_images[image] = std::move(decoded.m_rgba);
                    out.m_png_sha256[image] = pin.at("sha256").get<std::string>();
                }
                pbr.m_materials.push_back(std::move(out));
                const auto& accessor = K_ASSET.at("accessors").at(4uz + group);
                const auto K_COUNT = accessor.at("count").get<std::size_t>();
                const auto K_VIEW = accessor.at("bufferView").get<std::size_t>();
                const auto K_OFFSET =
                    K_ASSET.at("bufferViews").at(K_VIEW).at("byteOffset").get<std::size_t>();
                require(K_COUNT % 3uz == 0 && K_COUNT / 3uz <= 8192uz - mesh.m_triangles.size(),
                        "Native PBR triangle count exceeded");
                for (std::size_t triangle { 0uz }; triangle < K_COUNT / 3uz; ++triangle) {
                    mesh.m_triangles.push_back({ word(geometry, K_OFFSET + triangle * 12uz),
                                                word(geometry, K_OFFSET + triangle * 12uz + 4uz),
                                                word(geometry, K_OFFSET + triangle * 12uz + 8uz) });
                    pbr.m_triangle_materials.push_back(static_cast<std::uint32_t>(group));
                }
            }
            bool first = true;
            for (const auto& instance : K_SCENE.at("instances")) {
                if (instance.at("definition").at("asset_id").get<std::uint64_t>() !=
                    chunk->m_id.m_value) {
                    continue;
                }
                const auto K_WORLD = transform(instance.at("world_from_asset"));
                if (first) {
                    chunk->m_world_from_chunk = K_WORLD;
                    first = false;
                }
                const auto K_LOCAL = pbr.m_instances.empty()
                                         ? SimilarityTransform {}
                                         : relative(chunk->m_world_from_chunk, K_WORLD);
                pbr.m_instances.push_back({ { integer(instance.at("instance_id")) }, K_LOCAL });
            }
            require(!first, "Native PBR asset has no instances");
            bool bound = false;
            for (const auto& instance : pbr.m_instances) {
                for (const auto& vertex : mesh.m_vertices) {
                    const auto K_POINT = transform_point(
                        instance.m_chunk_from_asset,
                        { vertex.m_position[0uz], vertex.m_position[1uz], vertex.m_position[2uz] });
                    if (!bound) {
                        chunk->m_bounds = { K_POINT, K_POINT };
                        bound = true;
                    } else {
                        auto& b = chunk->m_bounds;
                        b.m_minimum = { std::min(b.m_minimum.m_x, K_POINT.m_x),
                                       std::min(b.m_minimum.m_y, K_POINT.m_y),
                                       std::min(b.m_minimum.m_z, K_POINT.m_z) };
                        b.m_maximum = { std::max(b.m_maximum.m_x, K_POINT.m_x),
                                       std::max(b.m_maximum.m_y, K_POINT.m_y),
                                       std::max(b.m_maximum.m_z, K_POINT.m_z) };
                    }
                }
            }
            scene->m_chunks.emplace(chunk->m_id, std::move(chunk));
        }
        (void)take(validate_scene(*scene));
        check_stop(stop);
        return SceneSnapshot(scene);
    } catch (const RecordFailure& e) {
        return fail(e.m_code, e.what());
    } catch (const gltf_detail::Cancelled&) {
        return fail(ErrorCode::CANCELLED, "Native PBR compilation cancelled");
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Native PBR compilation allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, e.what());
    }
}

}  // namespace world_explorer
