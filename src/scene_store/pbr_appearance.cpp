#include "world_explorer/scene_store/pbr_appearance.hpp"

#include "../core/json_records.hpp"
#include "gltf_internal.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>
#include <set>
#include <span>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_BYTES = 16uz * 1024uz * 1024uz;

struct Writer {
    std::vector<std::byte> m_data;

    template <std::size_t Count = 8uz> void word(std::uint64_t value) {
        static_assert(Count == 4uz || Count == 8uz);
        require(Count <= K_BYTES - m_data.size(),
                "PBR binary budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        for (std::size_t index { 0uz }; index < Count; ++index) {
            m_data.push_back(static_cast<std::byte>((value >> (8uz * index)) & 255U));
        }
    }

    void text(const std::string& text) {
        require(text.size() <= 1024uz * 1024uz && text.size() + 4uz <= K_BYTES - m_data.size(),
                "PBR text budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        word<4uz>(text.size());
        const auto K_SPAN = std::as_bytes(std::span(text));
        m_data.insert(m_data.end(), K_SPAN.begin(), K_SPAN.end());
    }

    void real(double value) {
        word(std::bit_cast<std::uint64_t>(value));
    }

    void transform(const SimilarityTransform& value) {
        for (auto x : { value.m_rotation.m_w,
                       value.m_rotation.m_x,
                       value.m_rotation.m_y,
                       value.m_rotation.m_z,
                       value.m_translation.m_x,
                       value.m_translation.m_y,
                       value.m_translation.m_z,
                       value.m_scale }) {
            real(x);
        }
    }
};

struct Reader {
    std::span<const std::byte> m_data;
    std::size_t m_at {};

    std::uint64_t word(std::size_t count = 8uz) {
        require(count <= m_data.size() - m_at, "Truncated PBR binary");
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < count; ++i) {
            value |= std::to_integer<std::uint64_t>(m_data[m_at++]) << (8uz * i);
        }
        return value;
    }

    std::size_t count(std::size_t maximum, std::size_t width) {
        const auto K_COUNT = word();
        require(K_COUNT <= maximum && K_COUNT <= (m_data.size() - m_at) / width,
                "PBR array count exceeds bytes/budget");
        return static_cast<std::size_t>(K_COUNT);
    }

    std::string text() {
        const auto K_COUNT = word(4uz);
        require(K_COUNT <= 1024uz * 1024uz && K_COUNT <= m_data.size() - m_at,
                "Invalid PBR text length");
        std::string text(reinterpret_cast<const char*>(m_data.data() + m_at),
                         static_cast<std::size_t>(K_COUNT));
        m_at += static_cast<std::size_t>(K_COUNT);
        return text;
    }

    double real() {
        return std::bit_cast<double>(word());
    }

    bool flag() {
        const auto K_FLAG = word(4uz);
        require(K_FLAG <= 1, "Invalid PBR flag");
        return K_FLAG == 1;
    }

    SimilarityTransform transform() {
        return { { real(), real(), real(), real() }, { real(), real(), real() }, real() };
    }
};

void digest(const std::string& value) {
    digest_text(value);
}

Vec3d vector(const std::array<float, 3>& value) {
    return { value[0uz], value[1uz], value[2uz] };
}

bool within(Vec3d value, const Bounds3d& bounds) {
    return finite(value) && value.m_x >= bounds.m_minimum.m_x &&
           value.m_x <= bounds.m_maximum.m_x && value.m_y >= bounds.m_minimum.m_y &&
           value.m_y <= bounds.m_maximum.m_y && value.m_z >= bounds.m_minimum.m_z &&
           value.m_z <= bounds.m_maximum.m_z;
}

}  // namespace

Result<std::vector<std::byte>> encode_pbr_appearance(const PbrAppearance& value) {
    try {
        require(value.m_uv.size() <= 8192uz && value.m_tangents.size() <= 8192uz &&
                    value.m_triangle_materials.size() <= 8192uz &&
                    value.m_materials.size() <= 4uz && value.m_instances.size() <= 64uz,
                "PBR count budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        Writer out;
        out.text("WEXPBR01");
        out.word(value.m_definition.m_asset.m_value);
        out.word(value.m_definition.m_revision);
        out.text(value.m_definition.m_sha256);
        out.word(value.m_epoch.m_value);
        out.word(value.m_pose_revision.m_value);
        out.text(value.m_export_manifest);
        out.text(value.m_export_manifest_sha256);
        out.text(value.m_geometry_sha256);
        out.text(value.m_asset_document);
        out.text(value.m_scene_document);
        out.word<4uz>(value.m_synthetic);
        out.word<4uz>(value.m_manual_ancestor);
        out.word(value.m_uv.size());
        for (const auto& uv : value.m_uv) {
            for (auto x : uv) {
                out.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
        }
        out.word(value.m_tangents.size());
        for (const auto& tangent : value.m_tangents) {
            for (auto x : tangent) {
                out.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
        }
        out.word(value.m_triangle_materials.size());
        for (auto x : value.m_triangle_materials) {
            out.word<4uz>(x);
        }
        out.word(value.m_materials.size());
        for (const auto& material : value.m_materials) {
            require(material.m_width && material.m_height && material.m_width <= 256 &&
                        material.m_height <= 256,
                    "PBR texture extent exceeds budget");
            out.word(material.m_variant.m_value);
            out.word(material.m_product.m_product.m_value);
            out.word(material.m_product.m_revision);
            out.text(material.m_product.m_content_sha256);
            out.word<4uz>(static_cast<unsigned>(material.m_lighting));
            out.word<4uz>(material.m_width);
            out.word<4uz>(material.m_height);
            out.word<4uz>(material.m_linear_filter);
            out.word<4uz>(material.m_repeat);
            for (std::size_t i { 0uz }; i < 4uz; ++i) {
                const auto& image = material.m_images[i];
                require(image.size() ==
                            static_cast<std::size_t>(material.m_width) * material.m_height * 4uz,
                        "PBR texture bytes differ");
                out.text(material.m_png_sha256[i]);
                out.word(image.size());
                require(image.size() <= K_BYTES - out.m_data.size(), "PBR image budget exceeded");
                out.m_data.insert(out.m_data.end(), image.begin(), image.end());
            }
        }
        out.word(value.m_instances.size());
        for (const auto& instance : value.m_instances) {
            out.word(instance.m_instance.m_value);
            out.transform(instance.m_chunk_from_asset);
        }
        return std::move(out.m_data);
    } catch (const RecordFailure& e) {
        return fail(e.m_code, e.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "PBR encoding allocation failed");
    }
}

Result<PbrAppearance> decode_pbr_appearance(std::span<const std::byte> bytes) {
    try {
        require(
            bytes.size() <= K_BYTES, "PBR binary exceeds budget", ErrorCode::RESOURCE_EXHAUSTED);
        Reader in { bytes };
        require(in.text() == "WEXPBR01", "Unsupported PBR encoding");
        PbrAppearance value;
        value.m_definition = { { in.word() }, in.word(), in.text() };
        value.m_epoch = { in.word() };
        value.m_pose_revision = { in.word() };
        value.m_export_manifest = in.text();
        value.m_export_manifest_sha256 = in.text();
        value.m_geometry_sha256 = in.text();
        value.m_asset_document = in.text();
        value.m_scene_document = in.text();
        value.m_synthetic = in.flag();
        value.m_manual_ancestor = in.flag();
        value.m_uv.resize(in.count(8192uz, 8uz));
        for (auto& uv : value.m_uv) {
            for (auto& x : uv) {
                x = std::bit_cast<float>(static_cast<std::uint32_t>(in.word(4uz)));
            }
        }
        value.m_tangents.resize(in.count(8192uz, 16uz));
        for (auto& tangent : value.m_tangents) {
            for (auto& x : tangent) {
                x = std::bit_cast<float>(static_cast<std::uint32_t>(in.word(4uz)));
            }
        }
        value.m_triangle_materials.resize(in.count(8192uz, 4uz));
        for (auto& x : value.m_triangle_materials) {
            x = static_cast<std::uint32_t>(in.word(4uz));
        }
        value.m_materials.resize(in.count(4uz, 48uz));
        for (auto& material : value.m_materials) {
            material.m_variant = { in.word() };
            material.m_product = { { in.word() }, in.word(), in.text() };
            const auto K_LIGHTING = in.word(4uz);
            require(K_LIGHTING == static_cast<unsigned>(AssetLightingMode::ESTIMATED_PBR) ||
                        K_LIGHTING == static_cast<unsigned>(AssetLightingMode::GENERATED_PBR),
                    "Unsupported PBR lighting encoding");
            material.m_lighting = static_cast<AssetLightingMode>(K_LIGHTING);
            material.m_width = static_cast<std::uint32_t>(in.word(4uz));
            material.m_height = static_cast<std::uint32_t>(in.word(4uz));
            material.m_linear_filter = in.flag();
            material.m_repeat = in.flag();
            require(material.m_width && material.m_height && material.m_width <= 256 &&
                        material.m_height <= 256,
                    "PBR texture extent exceeds budget");
            for (std::size_t i { 0uz }; i < 4uz; ++i) {
                material.m_png_sha256[i] = in.text();
                const auto K_COUNT = in.count(256uz * 256uz * 4uz, 1uz);
                require(K_COUNT ==
                            static_cast<std::size_t>(material.m_width) * material.m_height * 4uz,
                        "PBR image size differs");
                const auto K_DATA = in.m_data.subspan(in.m_at, K_COUNT);
                material.m_images[i].assign(K_DATA.begin(), K_DATA.end());
                in.m_at += K_COUNT;
            }
        }
        value.m_instances.resize(in.count(64uz, 72uz));
        for (auto& instance : value.m_instances) {
            instance.m_instance = { in.word() };
            instance.m_chunk_from_asset = in.transform();
        }
        require(in.m_at == bytes.size(), "Trailing PBR binary bytes");
        return value;
    } catch (const RecordFailure& e) {
        return fail(e.m_code, e.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "PBR decoding allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, e.what());
    }
}

Result<std::size_t> validate_pbr_appearance(const SceneChunk& chunk, const SceneLimits& limits) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_pbr) {
        return 0uz;
    }
    try {
        const auto& mesh = *chunk.m_mesh;
        const auto& value = *mesh.m_pbr;
        require(!mesh.m_observed && !mesh.m_baked && chunk.m_splats.empty(),
                "PBR is exclusive with captured/observed payloads");
        require(value.m_definition.m_asset.m_value && value.m_definition.m_revision &&
                    value.m_epoch.m_value && value.m_pose_revision.m_value,
                "Missing PBR identity/context");
        digest(value.m_definition.m_sha256);
        digest(value.m_geometry_sha256);
        digest(value.m_export_manifest_sha256);
        require(!chunk.m_geometry_valid ||
                    chunk.m_pose_revision.m_value == value.m_pose_revision.m_value,
                "PBR source pose differs",
                ErrorCode::RESYNC_REQUIRED);
        require(value.m_uv.size() == mesh.m_vertices.size() &&
                    value.m_tangents.size() == mesh.m_vertices.size() &&
                    value.m_triangle_materials.size() == mesh.m_triangles.size() &&
                    !value.m_materials.empty() && !value.m_instances.empty(),
                "Incomplete PBR arrays");
        const auto K_ENCODED = encode_pbr_appearance(value);
        if (!K_ENCODED) {
            return std::unexpected(K_ENCODED.error());
        }
        require(K_ENCODED->size() <= limits.m_max_payload_bytes,
                "PBR payload budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(sha256(std::as_bytes(std::span(value.m_export_manifest))) ==
                    value.m_export_manifest_sha256,
                "PBR manifest checksum differs");
        const auto K_DOC =
            parse(std::as_bytes(std::span(value.m_export_manifest)), 1024uz * 1024uz, {});
        const auto K_PROFILE = K_DOC.at("profile").get<std::string>();
        const bool K_LOD = K_PROFILE == "WE_NATIVE_LOD_GLTF_V1";
        const bool K_STRUCTURED =
            K_PROFILE == "WE_NATIVE_STRUCTURED_GLTF_V1" || (K_LOD && K_DOC.contains("structure"));
        require(((K_PROFILE == "WE_NATIVE_TEXTURED_GLTF_V1" &&
                  integer(K_DOC.at("schema_version")) == 1 && !K_DOC.contains("structure") &&
                  !K_DOC.contains("lod_selection")) ||
                 (K_PROFILE == "WE_NATIVE_STRUCTURED_GLTF_V1" &&
                  integer(K_DOC.at("schema_version")) == 2 && !K_DOC.contains("lod_selection")) ||
                 (K_LOD && integer(K_DOC.at("schema_version")) == 3)) &&
                    K_DOC.at("qualification").get<std::string>() == "UNQUALIFIED_EXPORT" &&
                    K_DOC.at("synthetic_diagnostic").get<bool>() == value.m_synthetic &&
                    K_DOC.at("manual_ancestor").get<bool>() == value.m_manual_ancestor &&
                    K_DOC.at("world_epoch").get<std::string>() ==
                        std::to_string(value.m_epoch.m_value) &&
                    K_DOC.at("pose_revision").get<std::string>() ==
                        std::to_string(value.m_pose_revision.m_value),
                "PBR manifest context/labels differ");
        const auto& assets = K_DOC.at("assets");
        require(assets.is_array() && assets.size() <= 2uz, "PBR source asset budget exceeded");
        const auto K_ASSET = std::ranges::find_if(assets, [&](const auto& asset) {
            return asset.at("definition").at("asset_id").template get<std::string>() ==
                   std::to_string(value.m_definition.m_asset.m_value);
        });
        require(K_ASSET != assets.end() &&
                    K_ASSET->at("definition").at("sha256").get<std::string>() ==
                        value.m_definition.m_sha256 &&
                    K_ASSET->at("definition").at("revision").get<std::string>() ==
                        std::to_string(value.m_definition.m_revision) &&
                    K_ASSET->at("geometry_sha256").get<std::string>() == value.m_geometry_sha256,
                "PBR definition/source differs");
        const auto K_ASSET_PATH =
            "assets/" + std::to_string(value.m_definition.m_asset.m_value) + "/asset.gltf";
        const auto& inventory = K_DOC.at("files");
        require(inventory.is_array() && inventory.size() <= 64uz, "PBR inventory budget exceeded");
        if (K_STRUCTURED) {
            const auto& structure = K_DOC.at("structure");
            keys(structure, { "path", "sha256", "scope" });
            digest_text(structure.at("sha256").get<std::string>());
            require(
                structure.at("path").get<std::string>() == "structure/bindings.json" &&
                    structure.at("scope").get<std::string>() == "DECLARED_PARTIAL_PATCHES" &&
                    std::ranges::any_of(inventory,
                                        [&](const auto& row) {
                                            return row.at("path").template get<std::string>() ==
                                                       "structure/bindings.json" &&
                                                   row.at("sha256").template get<std::string>() ==
                                                       structure.at("sha256").get<std::string>();
                                        }),
                "PBR structure binding pin differs");
        }
        const auto K_PIN_FILE = [&](const std::string& path, const std::string& text) {
            const auto K_ROW = std::ranges::find_if(inventory, [&](const auto& item) {
                return item.at("path").template get<std::string>() == path;
            });
            require(K_ROW != inventory.end() && K_ROW->at("sha256").template get<std::string>() ==
                                                    sha256(std::as_bytes(std::span(text))),
                    "PBR source document hash differs");
        };
        K_PIN_FILE(K_ASSET_PATH, value.m_asset_document);
        K_PIN_FILE("scene.json", value.m_scene_document);
        const auto K_ASSET_DOC =
            parse(std::as_bytes(std::span(value.m_asset_document)), 1024uz * 1024uz, {});
        if (K_LOD) {
            const auto& lod = K_DOC.at("lod_selection");
            keys(lod, { "path", "sha256", "scope", "structure_geometry_scope" });
            digest_text(lod.at("sha256").get<std::string>());
            const auto K_LEVEL = integer(K_ASSET->at("level"));
            require(
                lod.at("path").get<std::string>() == "lods.json" &&
                    lod.at("scope").get<std::string>() == "EXPLICIT_PER_DEFINITION_ALL_INSTANCES" &&
                    lod.at("structure_geometry_scope").get<std::string>() == "LOD0" &&
                    K_LEVEL <= 8 && integer(K_ASSET_DOC.at("extras").at("lod_level")) == K_LEVEL &&
                    K_ASSET_DOC.at("extras").at("profile").get<std::string>() == K_PROFILE &&
                    K_ASSET->at("geometry_path").get<std::string>() ==
                        "assets/" + std::to_string(value.m_definition.m_asset.m_value) +
                            "/geometry-lod" + std::to_string(K_LEVEL) + ".bin" &&
                    std::ranges::any_of(inventory,
                                        [&](const auto& row) {
                                            return row.at("path").template get<std::string>() ==
                                                       "lods.json" &&
                                                   row.at("sha256").template get<std::string>() ==
                                                       lod.at("sha256").get<std::string>();
                                        }),
                "PBR LOD selection or companion pin differs");
            const auto& certificate = K_ASSET->at("certificate");
            if (K_LEVEL == 0) {
                require(certificate.is_null(), "PBR LOD0 cannot claim a reduction certificate");
            } else {
                keys(certificate, { "product", "path", "sha256" });
                digest_text(certificate.at("sha256").get<std::string>());
                const auto K_PATH = "lods/" + std::to_string(value.m_definition.m_asset.m_value) +
                                    "-" + std::to_string(K_LEVEL) + ".json";
                require(certificate.at("path").get<std::string>() == K_PATH &&
                            certificate.at("product").at("sha256") == certificate.at("sha256") &&
                            std::ranges::any_of(
                                inventory,
                                [&](const auto& row) {
                                    return row.at("path").template get<std::string>() == K_PATH &&
                                           row.at("sha256") == certificate.at("sha256");
                                }),
                        "PBR LOD certificate pin differs");
            }
        }
        const auto K_SCENE_DOC =
            parse(std::as_bytes(std::span(value.m_scene_document)), 1024uz * 1024uz, {});
        require(sha256(std::as_bytes(std::span(value.m_scene_document))) ==
                    K_DOC.at("source_instances_sha256").get<std::string>(),
                "PBR source instance hash differs");
        Writer geometry;
        std::set<AssetMaterialVariantId> variants;
        for (std::size_t i { 0uz }; i < mesh.m_vertices.size(); ++i) {
            const auto& vertex = mesh.m_vertices[i];
            const auto K_N = vector(vertex.m_normal);
            const auto& tangent = value.m_tangents[i];
            const Vec3d K_T { tangent[0uz], tangent[1uz], tangent[2uz] };
            require(std::abs(dot(K_N, K_N) - 1) <= 1e-4 && finite(K_T) &&
                        std::abs(dot(K_T, K_T) - 1) <= 1e-4 && std::abs(dot(K_N, K_T)) <= 1e-4 &&
                        std::abs(tangent[3uz]) == 1 && std::isfinite(value.m_uv[i][0uz]) &&
                        std::isfinite(value.m_uv[i][1uz]),
                    "Invalid PBR tangent frame/UV");
            for (auto x : vertex.m_position) {
                geometry.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
            for (auto x : vertex.m_normal) {
                geometry.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
            for (auto x : tangent) {
                geometry.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
            for (auto x : value.m_uv[i]) {
                geometry.word<4uz>(std::bit_cast<std::uint32_t>(x));
            }
        }
        std::vector<std::size_t> used(value.m_materials.size());
        for (std::size_t i { 0uz }; i < mesh.m_triangles.size(); ++i) {
            const auto K_M = value.m_triangle_materials[i];
            require(K_M < used.size(), "PBR triangle material out of range");
            ++used[K_M];
            const auto& face = mesh.m_triangles[i];
            const auto K_POINT = [&](std::uint32_t index) {
                return vector(mesh.m_vertices.at(index).m_position);
            };
            const auto K_CROSS = cross(K_POINT(face[1uz]) - K_POINT(face[0uz]),
                                       K_POINT(face[2uz]) - K_POINT(face[0uz]));
            const auto& a = value.m_uv[face[0uz]];
            const auto& b = value.m_uv[face[1uz]];
            const auto& c = value.m_uv[face[2uz]];
            const double K_AREA =
                (static_cast<double>(b[0uz]) - a[0uz]) * (static_cast<double>(c[1uz]) - a[1uz]) -
                (static_cast<double>(b[1uz]) - a[1uz]) * (static_cast<double>(c[0uz]) - a[0uz]);
            require(finite(K_CROSS) && dot(K_CROSS, K_CROSS) > 0 && K_AREA != 0 &&
                        std::isfinite(K_AREA),
                    "Degenerate PBR face/UV");
            for (auto index : face) {
                require(dot(K_CROSS, vector(mesh.m_vertices[index].m_normal)) > 0 &&
                            value.m_tangents[index][3uz] == value.m_tangents[face[0uz]][3uz],
                        "PBR winding/handedness differs");
            }

            for (auto x : mesh.m_triangles[i]) {
                geometry.word<4uz>(x);
            }
        }
        const auto& source_groups = K_ASSET->at("groups");
        require(source_groups.is_array() && source_groups.size() == value.m_materials.size(),
                "PBR group count differs");
        std::vector<bool> source_faces(mesh.m_triangles.size());
        std::size_t face_offset {};
        for (std::size_t group { 0uz }; group < source_groups.size(); ++group) {
            const auto& faces = source_groups[group].at("source_faces");
            require(faces.is_array() && faces.size() == used[group],
                    "PBR group face count differs");
            for (const auto& face : faces) {
                const auto K_FACE = integer(face);
                require(K_FACE < source_faces.size() &&
                            !source_faces[static_cast<std::size_t>(K_FACE)] &&
                            value.m_triangle_materials[face_offset++] == group,
                        "PBR source face/group partition differs");
                source_faces[static_cast<std::size_t>(K_FACE)] = true;
            }
        }
        require(sha256(geometry.m_data) == value.m_geometry_sha256,
                "PBR geometry differs from exported buffers");
        const auto& materials = K_ASSET->at("materials");
        require(materials.is_array() && materials.size() == value.m_materials.size(),
                "PBR source material count differs");
        std::size_t texels {};
        for (std::size_t i { 0uz }; i < value.m_materials.size(); ++i) {
            const auto& material = value.m_materials[i];
            const auto& pin = materials[i];
            require(used[i] && variants.insert(material.m_variant).second &&
                        material.m_product.m_product.m_value && material.m_product.m_revision &&
                        (material.m_lighting == AssetLightingMode::ESTIMATED_PBR ||
                         material.m_lighting == AssetLightingMode::GENERATED_PBR),
                    "Invalid/unused PBR material");
            digest(material.m_product.m_content_sha256);
            require(pin.at("variant_id").get<std::string>() ==
                            std::to_string(material.m_variant.m_value) &&
                        pin.at("product").at("sha256").get<std::string>() ==
                            material.m_product.m_content_sha256 &&
                        pin.at("product").at("product_id").get<std::string>() ==
                            std::to_string(material.m_product.m_product.m_value) &&
                        pin.at("product").at("revision").get<std::string>() ==
                            std::to_string(material.m_product.m_revision) &&
                        pin.at("lighting").get<std::string>() ==
                            (material.m_lighting == AssetLightingMode::ESTIMATED_PBR
                                 ? "ESTIMATED_PBR"
                                 : "GENERATED_PBR"),
                    "PBR source material differs");
            const auto K_TEXTURE = K_ASSET_DOC.at("materials")
                                       .at(i)
                                       .at("pbrMetallicRoughness")
                                       .at("baseColorTexture")
                                       .at("index")
                                       .get<std::size_t>();
            const auto K_SAMPLER =
                K_ASSET_DOC.at("textures").at(K_TEXTURE).at("sampler").get<std::size_t>();
            const auto& sampler = K_ASSET_DOC.at("samplers").at(K_SAMPLER);
            require(
                sampler.at("magFilter").get<unsigned>() ==
                        (material.m_linear_filter ? 9729U : 9728U) &&
                    sampler.at("minFilter").get<unsigned>() ==
                        (material.m_linear_filter ? 9729U : 9728U) &&
                    sampler.at("wrapS").get<unsigned>() == (material.m_repeat ? 10497U : 33071U) &&
                    sampler.at("wrapT").get<unsigned>() == (material.m_repeat ? 10497U : 33071U),
                "PBR sampler differs from portable source");
            const auto K_COUNT = static_cast<std::size_t>(material.m_width) * material.m_height;
            require(K_COUNT <= (limits.m_max_texture_texels - texels) / 4uz,
                    "PBR texel budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            texels += K_COUNT * 4uz;
            const auto& images = pin.at("images");
            require(images.is_array() && images.size() == 4uz, "PBR source image count differs");
            for (std::size_t image { 0uz }; image < 4uz; ++image) {
                digest(material.m_png_sha256[image]);
                require(images[image].at("sha256").get<std::string>() ==
                                material.m_png_sha256[image] &&
                            images[image].at("width").get<std::uint32_t>() == material.m_width &&
                            images[image].at("height").get<std::uint32_t>() == material.m_height,
                        "PBR source image differs");
                const auto K_PNG = gltf_detail::encode_png(
                    { material.m_width, material.m_height, material.m_images[image] }, {});
                require(sha256(K_PNG) == material.m_png_sha256[image],
                        "PBR pixels differ from portable images");
                for (std::size_t pixel { 0uz }; pixel < K_COUNT; ++pixel) {
                    if (image == 2uz) {
                        require(std::to_integer<unsigned>(
                                    material.m_images[image][pixel * 4uz + 2uz]) >= 128U,
                                "PBR normal map Z must remain positive");
                    }
                    require(material.m_images[image][pixel * 4uz + 3uz] == std::byte { 255 },
                            "PBR texture alpha is not opaque");
                }
            }
        }
        std::set<AssetInstanceId> ids;
        for (const auto& instance : value.m_instances) {
            require(instance.m_instance.m_value && ids.insert(instance.m_instance).second &&
                        valid(instance.m_chunk_from_asset),
                    "Invalid PBR instance");
            const auto& rows = K_SCENE_DOC.at("instances");
            require(rows.is_array() && rows.size() <= 64uz, "PBR source instance count exceeded");
            const auto K_ROW = std::ranges::find_if(rows, [&](const auto& row) {
                return row.at("instance_id").template get<std::uint64_t>() ==
                       instance.m_instance.m_value;
            });
            require(K_ROW != rows.end() &&
                        K_ROW->at("definition").at("asset_id").template get<std::uint64_t>() ==
                            value.m_definition.m_asset.m_value,
                    "PBR instance source identity differs");
            const auto K_SOURCE = transform(K_ROW->at("world_from_asset"));
            for (auto point : { Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 } }) {
                const auto K_EXPECTED = transform_point(K_SOURCE, point);
                const auto K_ACTUAL = transform_point(
                    chunk.m_world_from_chunk, transform_point(instance.m_chunk_from_asset, point));
                const auto K_LIMIT = 1e-9 * std::max({ 1.0,
                                                      std::abs(K_EXPECTED.m_x),
                                                      std::abs(K_EXPECTED.m_y),
                                                      std::abs(K_EXPECTED.m_z) });
                const auto K_DELTA = K_ACTUAL - K_EXPECTED;
                require(!chunk.m_geometry_valid ||
                            (finite(K_ACTUAL) && std::abs(K_DELTA.m_x) <= K_LIMIT &&
                             std::abs(K_DELTA.m_y) <= K_LIMIT && std::abs(K_DELTA.m_z) <= K_LIMIT),
                        "PBR original instance transform differs");
            }
            for (const auto& vertex : mesh.m_vertices) {
                require(
                    within(transform_point(instance.m_chunk_from_asset, vector(vertex.m_position)),
                           chunk.m_bounds),
                    "PBR instance outside chunk bounds");
            }
        }
        return K_ENCODED->size();
    } catch (const RecordFailure& e) {
        return fail(e.m_code, e.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "PBR validation allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, e.what());
    }
}

}  // namespace world_explorer
