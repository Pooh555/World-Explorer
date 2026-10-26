#pragma once

#include "geometry_product_fixture.hpp"
#include "world_explorer/application/material_product.hpp"

#include <bit>

namespace world_explorer::material_product_test {

using namespace geometry_product_test;
inline constexpr std::array K_CHANNELS {
    "CAPTURED_RGB", "BASE_COLOR", "METALLIC", "ROUGHNESS", "NORMAL", "OCCLUSION", "EMISSION" };
inline constexpr std::array K_LIGHTING { "CAPTURED_APPEARANCE", "ESTIMATED_PBR", "GENERATED_PBR" };
inline constexpr std::array K_FITS { "LIGHT", "EXPOSURE", "WHITE_BALANCE" };

inline void put(std::string& bytes, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t index { 0uz }; index < count; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8uz)) & 255);
    }
}

inline std::string channel_bytes(MaterialChannelRole role,
                                 std::uint64_t id,
                                 std::uint32_t width,
                                 std::uint32_t height) {
    const bool K_RGB = role == MaterialChannelRole::CAPTURED_RGB ||
                       role == MaterialChannelRole::BASE_COLOR ||
                       role == MaterialChannelRole::NORMAL || role == MaterialChannelRole::EMISSION;
    const auto K_COMPONENTS = K_RGB ? 3uz : 1uz;
    const auto K_COUNT = static_cast<std::size_t>(width) * height * K_COMPONENTS;
    std::string result(64uz + 4uz * K_COUNT, '\0');
    result.replace(0uz, 7uz, "WEMATX1");
    put(result, 8uz, 1, 4uz);
    put(result, 12uz, static_cast<std::uint64_t>(role), 4uz);
    put(result, 16uz, width, 4uz);
    put(result, 20uz, height, 4uz);
    put(result, 24uz, K_COMPONENTS, 4uz);
    put(result, 32uz, id, 8uz);
    put(result, 40uz, 1, 8uz);
    put(result, 48uz, 3, 8uz);
    put(result, 56uz, 5, 8uz);
    for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
        const float K_VALUE =
            role == MaterialChannelRole::NORMAL ? (index % 3uz == 2uz ? 1.0F : 0.0F) : .25F;
        put(result, 64uz + index * 4uz, std::bit_cast<std::uint32_t>(K_VALUE), 4uz);
    }
    return result;
}

struct MaterialFixtureOptions {
    AssetLightingMode m_lighting { AssetLightingMode::ESTIMATED_PBR };
    bool m_manual {}, m_white_balance {};
    std::uint32_t m_width { 2 }, m_height { 2 };
};

struct MaterialFixtureMutations {
    std::function<void(Json&, MaterialProductSelection&)> m_root;
    std::function<void(std::string&, MaterialChannelRole)> m_channel;
    std::function<void(Json&, MaterialChannelRole)> m_provenance;
    std::function<void(Json&, MaterialFitSelection&)> m_fit;
    std::function<void(AssetDependencyGraph&, const MaterialProductSelection&)> m_graph;
    std::function<void(std::string&)> m_root_bytes, m_fit_bytes;
};

struct MaterialFixture {
    GeometryFixture m_base;
    RecipeFixture m_output;
    MaterialProductSelection m_selected;
    Json m_record;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    AdmittedGeometryProduct m_geometry;

    explicit MaterialFixture(const MaterialFixtureOptions& options = {},
                             const MaterialFixtureMutations& changes = {},
                             const GeometryFixture::Mutation& geometry_change = {})
        : m_base(geometry_change), m_receipt(stage(options, changes)),
          m_capture(checked(m_base.m_views.inspect(m_receipt, false))), m_geometry(geometry()) { }

    AdmittedGeometryProduct geometry() const {
        auto request = m_base.request();
        request.m_capture = m_capture;
        request.m_receipt = m_receipt;
        return checked(inspect_geometry_product(request));
    }

    AdmittedAssetReceipt stage(const MaterialFixtureOptions& options,
                               const MaterialFixtureMutations& changes) {
        auto& region = m_base.m_views.m_object.m_region;
        const auto K_GEOMETRY = m_base.m_selected.m_product;
        const auto K_CAPTURE = m_base.m_capture.record().m_selection.m_product;
        const auto K_VIEW = m_base.m_selected.m_views[0uz];
        const std::string K_RECIPE { "synthetic material channel and fit import; inference-only" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        m_selected.m_variant = { 101 };
        m_selected.m_lighting = options.m_lighting;
        m_selected.m_width = options.m_width;
        m_selected.m_height = options.m_height;
        m_selected.m_filter = MaterialFilter::LINEAR;
        m_selected.m_wrap = MaterialWrap::CLAMP_TO_EDGE;
        const bool K_CAPTURED = options.m_lighting == AssetLightingMode::CAPTURED_APPEARANCE;
        std::map<AssetProductId, AssetRevisionRef> root_parents {
            { K_GEOMETRY.m_product, K_GEOMETRY } };
        auto fit_rows = Json::array();
        if (options.m_lighting == AssetLightingMode::ESTIMATED_PBR) {
            for (auto role : { MaterialFitRole::LIGHT,
                              MaterialFitRole::EXPOSURE,
                              MaterialFitRole::WHITE_BALANCE }) {
                if (role == MaterialFitRole::WHITE_BALANCE && !options.m_white_balance) {
                    continue;
                }
                MaterialFitSelection pin { role, {}, { K_VIEW } };
                Json parameters =
                    role == MaterialFitRole::EXPOSURE
                        ? Json { { "model", "SCALAR_LINEAR_GAIN" }, { "gain", 1.2 } }
                        : Json { { "model",
                                 role == MaterialFitRole::LIGHT ? "CONSTANT_ENVIRONMENT"
                                                                : "DIAGONAL_LINEAR_WHITE_BALANCE" },
                                { "linear_rgb", { .25, .5, 1 } } };
                Json record { { "schema_version", 1 },
                             { "kind", "MATERIAL_FIT_INPUT" },
                             { "world_epoch", 3 },
                             { "pose_revision", 5 },
                             { "recipe_sha256", hash(K_RECIPE) },
                             { "geometry", region_ref(K_GEOMETRY) },
                             { "capture", region_ref(K_CAPTURE) },
                             { "role", K_FITS.at(static_cast<std::size_t>(role)) },
                             { "views", Json::array({ object_view(K_VIEW) }) },
                             { "parameters", parameters },
                             { "manual_ancestor", false } };
                if (changes.m_fit) {
                    changes.m_fit(record, pin);
                }
                auto text = record.dump();
                if (changes.m_fit_bytes) {
                    changes.m_fit_bytes(text);
                }
                auto node = region.node(12000uz + static_cast<std::size_t>(role), text);
                node.m_recipe_sha256 = hash(K_RECIPE);
                node.m_dependencies = { K_GEOMETRY, K_VIEW.m_image, K_VIEW.m_pose_asset };
                pin.m_product = node.m_product;
                region.m_graph.m_products.push_back(node);
                root_parents.emplace(node.m_product.m_product, node.m_product);
                fit_rows.push_back({ { "role", K_FITS.at(static_cast<std::size_t>(role)) },
                                    { "product", region_ref(node.m_product) } });
                m_selected.m_fit_inputs.push_back(pin);
            }
        }
        const auto K_CHANNEL_PARENTS = root_parents;
        auto channel_rows = Json::object();
        for (std::size_t index { K_CAPTURED ? 0uz : 1uz }; index < (K_CAPTURED ? 1uz : 7uz);
             ++index) {
            const auto K_ROLE = static_cast<MaterialChannelRole>(index);
            const auto K_ID = 12100uz + 2uz * index;
            auto bytes = channel_bytes(K_ROLE, K_ID, options.m_width, options.m_height);
            if (changes.m_channel) {
                changes.m_channel(bytes, K_ROLE);
            }
            auto channel = region.node(K_ID, bytes);
            channel.m_recipe_sha256 = hash(K_RECIPE);
            for (const auto& [K_PARENT, K_REF] : K_CHANNEL_PARENTS) {
                channel.m_dependencies.push_back(K_REF);
            }
            if (options.m_manual) {
                const std::string K_ACTION { "synthetic declared material edit" };
                region.m_data[hash(K_ACTION)] = K_ACTION;
                channel.m_manual_action_sha256 = hash(K_ACTION);
                channel.m_manual_ancestor = true;
            }
            const AssetSourceViewRef K_SOURCE { { K_VIEW.m_capture, K_VIEW.m_frame },
                                               K_VIEW.m_calibration,
                                               K_VIEW.m_image,
                                               hash("synthetic calibration declaration"),
                                               K_VIEW.m_pose_asset.m_content_sha256,
                                               hash("synthetic source pixel grid") };
            const AssetRegionSelection K_REGION {
                channel.m_product,
                { AssetRegionKind::MATERIAL_TEXELS,
                 static_cast<std::uint64_t>(options.m_width) * options.m_height,
                 options.m_width,
                 options.m_height },
                { K_SOURCE } };
            const auto K_CLASS = options.m_manual ? "MANUALLY_EDITED"
                                 : K_CAPTURED     ? "OBSERVED"
                                 : options.m_lighting == AssetLightingMode::GENERATED_PBR
                                     ? "GENERATED"
                                     : "INFERRED";
            Json provenance {
                { "schema_version", 1 },
                { "world_epoch", 3 },
                { "pose_revision", 5 },
                { "target", region_ref(channel.m_product) },
                { "recipe_sha256", hash(K_RECIPE) },
                { "domain",
                 { { "kind", "MATERIAL_TEXELS" },
                  { "elements", K_REGION.m_domain.m_elements },
                  { "width", options.m_width },
                  { "height", options.m_height } } },
                { "sources", Json::array({ region_source(K_SOURCE) }) },
                { "spans",
                 Json::array({ { { "first", 0 },
                               { "count", K_REGION.m_domain.m_elements },
                               { "class", K_CLASS },
                               { "confidence", .5 },
                               { "parents", Json::array({ region_ref(K_GEOMETRY) }) },
                               { "support", Json::array({ source_view_id(K_SOURCE.m_view) }) },
                               { "manual_ancestor", options.m_manual } } }) },
                { "manual_ancestor", options.m_manual } };
            if (changes.m_provenance) {
                changes.m_provenance(provenance, K_ROLE);
            }
            auto sidecar = region.node(K_ID + 1uz, provenance.dump());
            sidecar.m_recipe_sha256 = hash(K_RECIPE);
            sidecar.m_dependencies = { channel.m_product };
            sidecar.m_manual_ancestor = options.m_manual;
            region.m_graph.m_products.insert(region.m_graph.m_products.end(), { channel, sidecar });
            root_parents.emplace(channel.m_product.m_product, channel.m_product);
            root_parents.emplace(sidecar.m_product.m_product, sidecar.m_product);
            m_selected.m_channels.emplace(
                K_ROLE, MaterialChannelSelection { channel.m_product, sidecar.m_product, K_REGION });
            const bool K_COLOR = K_ROLE == MaterialChannelRole::CAPTURED_RGB ||
                                 K_ROLE == MaterialChannelRole::BASE_COLOR ||
                                 K_ROLE == MaterialChannelRole::EMISSION;
            channel_rows[K_CHANNELS[index]] = {
                { "product", region_ref(channel.m_product) },
                { "provenance", region_ref(sidecar.m_product) },
                { "color_space", K_COLOR ? "LINEAR_SRGB" : "LINEAR_DATA" } };
        }
        m_record = {
            { "schema_version", 1 },
            { "kind", "MATERIAL_PRODUCT" },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "recipe_sha256", hash(K_RECIPE) },
            { "geometry", region_ref(K_GEOMETRY) },
            { "capture", region_ref(K_CAPTURE) },
            { "variant_id", 101 },
            { "lighting", K_LIGHTING.at(static_cast<std::size_t>(options.m_lighting)) },
            { "brdf", K_CAPTURED ? "CAPTURED_UNLIT" : "METALLIC_ROUGHNESS_GGX" },
            { "grid", { { "width", options.m_width }, { "height", options.m_height } } },
            { "uv_set", 0 },
            { "uv_origin", "TOP_LEFT" },
            { "sampler", { { "filter", "LINEAR" }, { "wrap", "CLAMP_TO_EDGE" }, { "mip", "NONE" } } },
            { "normal",
             K_CAPTURED ? Json(nullptr)
                        : Json { { "basis", "GEOMETRY_TANGENT_W_SIGN" }, { "y", "POSITIVE_BITANGENT" } } },
            { "relighting_scope", K_CAPTURED ? "CAPTURED_ONLY" : "UNQUALIFIED" },
            { "channels", channel_rows },
            { "fit_inputs", fit_rows },
            { "manual_ancestor", options.m_manual } };
        if (changes.m_root) {
            changes.m_root(m_record, m_selected);
        }
        auto text = m_record.dump();
        if (changes.m_root_bytes) {
            changes.m_root_bytes(text);
        }
        auto root = region.node(12200, text);
        root.m_recipe_sha256 = hash(K_RECIPE);
        root.m_manual_ancestor = options.m_manual;
        for (const auto& [K_ID, K_REF] : root_parents) {
            root.m_dependencies.push_back(K_REF);
        }
        m_selected.m_product = root.m_product;
        region.m_graph.m_products.push_back(root);
        if (changes.m_graph) {
            m_base.m_views.m_change_graph = [&](auto& graph) {
                changes.m_graph(graph, m_selected);
            };
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto result = m_base.m_views.freeze(false);
        m_base.m_views.m_change_graph = {};
        return result;
    }

    MaterialProductRequest request() const {
        return { m_selected, m_geometry, m_capture, m_receipt, m_receipt.graph().m_context };
    }

    Result<AdmittedMaterialProduct> inspect(const std::stop_token& stop = {}) const {
        return inspect_material_product(request(), stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "material";
    }
};

}  // namespace world_explorer::material_product_test
