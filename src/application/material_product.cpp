#include "world_explorer/application/material_product.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <bit>
#include <set>
#include <span>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::array K_CHANNELS {
    "CAPTURED_RGB", "BASE_COLOR", "METALLIC", "ROUGHNESS", "NORMAL", "OCCLUSION", "EMISSION" };
constexpr std::array K_LIGHTING { "CAPTURED_APPEARANCE", "ESTIMATED_PBR", "GENERATED_PBR" };
constexpr std::array K_FITS { "LIGHT", "EXPOSURE", "WHITE_BALANCE" };
constexpr std::array K_FILTERS { "NEAREST", "LINEAR" };
constexpr std::array K_WRAPS { "CLAMP_TO_EDGE", "REPEAT" };
constexpr auto K_JSON_BYTES = 64uz * 1024uz;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return *result;
}

template <class Enum, std::size_t N>
const char* name(Enum value, const std::array<const char*, N>& names) {
    const auto K_INDEX = static_cast<std::size_t>(value);
    require(K_INDEX < names.size(),
            "Unsupported material selection enum",
            ErrorCode::UNSUPPORTED_OPERATION);
    return names[K_INDEX];
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

ObjectViewSelection view(const Json& value) {
    keys(
        value,
        { "capture_id", "frame_id", "calibration_id", "frame_pose_revision", "image", "pose_asset" });
    return { { integer(value.at("capture_id")) },
            { integer(value.at("frame_id")) },
            { integer(value.at("calibration_id")) },
            { integer(value.at("frame_pose_revision")) },
            reference(value.at("image")),
            reference(value.at("pose_asset")) };
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    require(ref.m_product.m_value && ref.m_revision, "Unselected material product");
    const auto K_FOUND =
        std::ranges::find(receipt.graph().m_products, ref, &AssetDependencyNode::m_product);
    require(K_FOUND != receipt.graph().m_products.end(),
            "Material product differs from selected receipt",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

using Parents = std::map<AssetProductId, AssetRevisionRef>;

void parents(const AssetDependencyNode& node, const Parents& expected) {
    std::vector<AssetRevisionRef> wanted;
    wanted.reserve(expected.size());
    for (const auto& [K_ID, K_REF] : expected) {
        wanted.push_back(K_REF);
    }
    auto actual = node.m_dependencies;
    std::ranges::sort(actual);
    std::ranges::sort(wanted);
    require(actual == wanted, "Material dependency set differs");
}

void header(const Json& value,
            const AssetDependencyNode& node,
            const AssetDependencyContext& expected) {
    require(integer(value.at("schema_version")) == 1,
            "Unsupported material schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(value.at("recipe_sha256")) == node.m_recipe_sha256,
            "Material context/recipe differs",
            ErrorCode::RESYNC_REQUIRED);
    require(value.at("manual_ancestor").is_boolean() &&
                value.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Material manual ancestry differs");
}

std::string key(const char* prefix, const AssetRevisionRef& ref) {
    return std::string(prefix) + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

void record(std::vector<ImmutableRecord>& records,
            std::string record_key,
            std::vector<std::byte> bytes) {
    require(bytes.size() <= 8uz * 1024uz * 1024uz,
            "Material record budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::size_t total = bytes.size();
    for (const auto& item : records) {
        require(item.m_bytes.size() <= 8uz * 1024uz * 1024uz - total,
                "Material publication byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        total += item.m_bytes.size();
    }
    require(total <= 8uz * 1024uz * 1024uz,
            "Material publication byte budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    require(std::ranges::none_of(records,
                                 [&](const auto& item) {
                                     return item.m_key == record_key;
                                 }),
            "Repeated material record");
    records.push_back({ std::move(record_key), std::move(bytes) });
}

template <std::size_t N>
std::uint64_t little(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t result {};
    for (std::size_t index { 0uz }; index < N; ++index) {
        result |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << (index * 8uz);
    }
    return result;
}

std::uint32_t components(MaterialChannelRole role) {
    return role == MaterialChannelRole::CAPTURED_RGB || role == MaterialChannelRole::BASE_COLOR ||
                   role == MaterialChannelRole::NORMAL || role == MaterialChannelRole::EMISSION
               ? 3
               : 1;
}

std::vector<float> pixels(std::span<const std::byte> bytes,
                          MaterialChannelRole role,
                          const MaterialChannelSelection& selected,
                          const MaterialProductSelection& material,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop) {
    constexpr std::array K_MAGIC { std::byte { 'W' },
                                  std::byte { 'E' },
                                  std::byte { 'M' },
                                  std::byte { 'A' },
                                  std::byte { 'T' },
                                  std::byte { 'X' },
                                  std::byte { '1' },
                                  std::byte {} };
    require(bytes.size() >= 64uz && std::equal(K_MAGIC.begin(), K_MAGIC.end(), bytes.begin()),
            "Invalid material channel magic/header");
    const auto K_COMPONENTS = components(role);
    const auto K_COUNT =
        static_cast<std::size_t>(material.m_width) * material.m_height * K_COMPONENTS;
    require(bytes.size() == 64uz + 4uz * K_COUNT && little<4uz>(bytes, 8uz) == 1 &&
                little<4uz>(bytes, 12uz) == static_cast<std::uint64_t>(role) &&
                little<4uz>(bytes, 16uz) == material.m_width &&
                little<4uz>(bytes, 20uz) == material.m_height &&
                little<4uz>(bytes, 24uz) == K_COMPONENTS && little<4uz>(bytes, 28uz) == 0 &&
                little<8uz>(bytes, 32uz) == selected.m_product.m_product.m_value &&
                little<8uz>(bytes, 40uz) == selected.m_product.m_revision &&
                little<8uz>(bytes, 48uz) == expected.m_world_epoch.m_value &&
                little<8uz>(bytes, 56uz) == expected.m_pose_revision.m_value,
            "Material channel length/role/grid/identity/context differs",
            ErrorCode::RESYNC_REQUIRED);
    std::vector<float> result;
    result.reserve(K_COUNT);
    for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
        check_stop(stop);
        const auto K_VALUE = std::bit_cast<float>(
            static_cast<std::uint32_t>(little<4uz>(bytes, 64uz + index * 4uz)));
        const float K_MINIMUM = role == MaterialChannelRole::NORMAL ? -1.0F : 0.0F;
        const float K_MAXIMUM =
            role == MaterialChannelRole::CAPTURED_RGB || role == MaterialChannelRole::EMISSION
                ? 64.0F
                : 1.0F;
        require(std::isfinite(K_VALUE) && K_VALUE >= K_MINIMUM && K_VALUE <= K_MAXIMUM,
                "Invalid material channel value");
        result.push_back(K_VALUE);
    }
    if (role == MaterialChannelRole::NORMAL) {
        for (std::size_t index { 0uz }; index < result.size(); index += 3uz) {
            const Vec3d K_NORMAL { result[index], result[index + 1uz], result[index + 2uz] };
            require(std::abs(dot(K_NORMAL, K_NORMAL) - 1) <= 1e-4,
                    "Nonunit material tangent normal");
        }
    }
    return result;
}

MaterialFitInput fit(const MaterialFitSelection& selected,
                     const MaterialProductRequest& request,
                     std::vector<ImmutableRecord>& records,
                     const std::stop_token& stop) {
    if (!request.m_geometry || !request.m_receipt || !request.m_capture) {
        throw RecordFailure(ErrorCode::INVALID_ARGUMENT, "Material fit prerequisites are missing");
    }
    const auto& geometry = request.m_geometry.value();
    const auto& receipt = request.m_receipt.value();
    const auto& capture = request.m_capture.value();
    require(!selected.m_views.empty() && selected.m_views.size() <= 16uz,
            "Material fit view budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    Parents dependencies {
        { geometry.selection().m_product.m_product, geometry.selection().m_product } };
    std::set<ObjectViewSelection> seen;
    for (const auto& pin : selected.m_views) {
        require(std::ranges::find(geometry.selection().m_views, pin) !=
                        geometry.selection().m_views.end() &&
                    seen.insert(pin).second,
                "Material fit view is duplicated or outside selected inference geometry",
                ErrorCode::RESYNC_REQUIRED);
        dependencies.emplace(pin.m_image.m_product, pin.m_image);
        dependencies.emplace(pin.m_pose_asset.m_product, pin.m_pose_asset);
    }
    const auto& node = bind(selected.m_product, receipt);
    parents(node, dependencies);
    auto bytes = take(receipt.copy_blob(selected.m_product.m_content_sha256, K_JSON_BYTES, stop));
    const auto K_DOC = parse(bytes, K_JSON_BYTES, stop);
    keys(K_DOC,
         { "schema_version",
          "kind",
          "world_epoch",
          "pose_revision",
          "recipe_sha256",
          "geometry",
          "capture",
          "role",
          "views",
          "parameters",
          "manual_ancestor" });
    header(K_DOC, node, request.m_expected);
    require(K_DOC.at("kind").get<std::string>() == "MATERIAL_FIT_INPUT" &&
                K_DOC.at("role").get<std::string>() == name(selected.m_role, K_FITS) &&
                reference(K_DOC.at("geometry")) == geometry.selection().m_product &&
                reference(K_DOC.at("capture")) == capture.record().m_selection.m_product,
            "Material fit identity differs",
            ErrorCode::RESYNC_REQUIRED);
    array(K_DOC.at("views"), 16uz);
    std::vector<ObjectViewSelection> actual;
    for (const auto& row : K_DOC.at("views")) {
        actual.push_back(view(row));
    }
    auto wanted = selected.m_views;
    std::ranges::sort(actual);
    std::ranges::sort(wanted);
    require(actual == wanted,
            "Material fit independently selected views differ",
            ErrorCode::RESYNC_REQUIRED);
    const auto& parameters = K_DOC.at("parameters");
    MaterialFitInput result { selected, {} };
    if (selected.m_role == MaterialFitRole::EXPOSURE) {
        keys(parameters, { "model", "gain" });
        require(parameters.at("model").get<std::string>() == "SCALAR_LINEAR_GAIN",
                "Unsupported material exposure model");
        const auto K_GAIN = number(parameters.at("gain"));
        require(K_GAIN > 0 && K_GAIN <= 64, "Invalid material exposure gain");
        result.m_values.push_back(K_GAIN);
    } else {
        keys(parameters, { "model", "linear_rgb" });
        require(parameters.at("model").get<std::string>() ==
                    (selected.m_role == MaterialFitRole::LIGHT ? "CONSTANT_ENVIRONMENT"
                                                               : "DIAGONAL_LINEAR_WHITE_BALANCE"),
                "Unsupported material RGB fit model");
        const auto K_RGB = vector3(parameters.at("linear_rgb"));
        for (auto value : { K_RGB.m_x, K_RGB.m_y, K_RGB.m_z }) {
            require(value >= 0 && value <= 64 &&
                        (selected.m_role != MaterialFitRole::WHITE_BALANCE || value > 0),
                    "Invalid material linear RGB fit value");
            result.m_values.push_back(value);
        }
    }
    record(records, key("material-fit-", selected.m_product), std::move(bytes));
    std::ranges::sort(result.m_selection.m_views);
    return result;
}

}  // namespace

struct AdmittedMaterialProduct::Implementation {
    MaterialProductSelection m_selection;
    AssetRevisionRef m_geometry;
    std::map<MaterialChannelRole, MaterialChannel> m_channels;
    std::vector<MaterialFitInput> m_fits;
    std::string m_receipt;
    AssetDependencyContext m_context;
    bool m_synthetic {}, m_manual {};
    std::vector<ImmutableRecord> m_records;
};

AdmittedMaterialProduct::AdmittedMaterialProduct(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const MaterialProductSelection& AdmittedMaterialProduct::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedMaterialProduct::geometry_product() const {
    return m_state->m_geometry;
}

const std::map<MaterialChannelRole, MaterialChannel>& AdmittedMaterialProduct::channels() const {
    return m_state->m_channels;
}

const std::vector<MaterialFitInput>& AdmittedMaterialProduct::fit_inputs() const {
    return m_state->m_fits;
}

const std::string& AdmittedMaterialProduct::receipt_sha256() const {
    return m_state->m_receipt;
}

const AssetDependencyContext& AdmittedMaterialProduct::context() const {
    return m_state->m_context;
}

bool AdmittedMaterialProduct::synthetic_source() const {
    return m_state->m_synthetic;
}

bool AdmittedMaterialProduct::manual_ancestor() const {
    return m_state->m_manual;
}

Result<AdmittedMaterialProduct> inspect_material_product(const MaterialProductRequest& request,
                                                         const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_geometry || !request.m_capture ||
            !request.m_receipt) {
            return fail(
                ErrorCode::INVALID_ARGUMENT,
                "Independent material selection, geometry, capture and receipt are required");
        }
        const auto& selected = request.m_selection.value();
        const auto& geometry = request.m_geometry.value();
        const auto& capture = request.m_capture.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        (void)name(selected.m_lighting, K_LIGHTING);
        (void)name(selected.m_filter, K_FILTERS);
        (void)name(selected.m_wrap, K_WRAPS);
        const bool K_CAPTURED = selected.m_lighting == AssetLightingMode::CAPTURED_APPEARANCE;
        require(selected.m_width && selected.m_width <= 256 && selected.m_height &&
                    selected.m_height <= 256 &&
                    selected.m_channels.size() == (K_CAPTURED ? 1uz : 6uz) &&
                    selected.m_fit_inputs.size() <= 3uz,
                "Material grid/channel/fit budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(selected.m_variant.m_value &&
                    std::ranges::find(geometry.mesh().m_groups, selected.m_variant) !=
                        geometry.mesh().m_groups.end() &&
                    !geometry.mesh().m_uv.empty() && !geometry.mesh().m_tangents.empty(),
                "Material variant or UV/tangent geometry is unavailable");
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        require(geometry.receipt_sha256() == receipt.receipt_sha256() &&
                    capture.receipt_sha256() == receipt.receipt_sha256() &&
                    geometry.context().m_world_epoch == expected.m_world_epoch &&
                    geometry.context().m_pose_revision == expected.m_pose_revision &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision &&
                    geometry.capture_product() == capture.record().m_selection.m_product,
                "Material geometry/capture/receipt context differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FIREWALL =
            capture.validate_inference_dependency(receipt, selected.m_product, expected, stop);
        if (!K_FIREWALL) {
            return std::unexpected(K_FIREWALL.error());
        }
        const auto& node = bind(selected.m_product, receipt);
        Parents dependencies {
            { geometry.selection().m_product.m_product, geometry.selection().m_product } };
        std::vector<ImmutableRecord> records;
        std::vector<MaterialFitInput> fits;
        std::set<MaterialFitRole> fit_roles;
        Parents channel_parents = dependencies;
        for (const auto& pin : selected.m_fit_inputs) {
            (void)name(pin.m_role, K_FITS);
            require(fit_roles.insert(pin.m_role).second, "Repeated material fit role");
            fits.push_back(fit(pin, request, records, stop));
            dependencies.emplace(pin.m_product.m_product, pin.m_product);
            channel_parents.emplace(pin.m_product.m_product, pin.m_product);
        }
        require(!K_CAPTURED || fits.empty(),
                "Captured appearance cannot declare inverse-fit inputs");
        require(selected.m_lighting != AssetLightingMode::ESTIMATED_PBR ||
                    (fit_roles.contains(MaterialFitRole::LIGHT) &&
                     fit_roles.contains(MaterialFitRole::EXPOSURE)),
                "Estimated PBR requires explicit light and exposure inputs");
        auto root_bytes =
            take(receipt.copy_blob(selected.m_product.m_content_sha256, K_JSON_BYTES, stop));
        const auto K_DOC = parse(root_bytes, K_JSON_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "geometry",
              "capture",
              "variant_id",
              "lighting",
              "brdf",
              "grid",
              "uv_set",
              "uv_origin",
              "sampler",
              "normal",
              "relighting_scope",
              "channels",
              "fit_inputs",
              "manual_ancestor" });
        header(K_DOC, node, expected);
        require(K_DOC.at("kind").get<std::string>() == "MATERIAL_PRODUCT" &&
                    reference(K_DOC.at("geometry")) == geometry.selection().m_product &&
                    reference(K_DOC.at("capture")) == geometry.capture_product() &&
                    integer(K_DOC.at("variant_id")) == selected.m_variant.m_value &&
                    K_DOC.at("lighting").get<std::string>() ==
                        name(selected.m_lighting, K_LIGHTING) &&
                    K_DOC.at("brdf").get<std::string>() ==
                        (K_CAPTURED ? "CAPTURED_UNLIT" : "METALLIC_ROUGHNESS_GGX") &&
                    integer(K_DOC.at("uv_set")) == 0 &&
                    K_DOC.at("uv_origin").get<std::string>() == "TOP_LEFT" &&
                    K_DOC.at("relighting_scope").get<std::string>() ==
                        (K_CAPTURED ? "CAPTURED_ONLY" : "UNQUALIFIED"),
                "Material root profile differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto& grid = K_DOC.at("grid");
        keys(grid, { "width", "height" });
        require(integer(grid.at("width")) == selected.m_width &&
                    integer(grid.at("height")) == selected.m_height,
                "Material independently selected grid differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto& sampler = K_DOC.at("sampler");
        keys(sampler, { "filter", "wrap", "mip" });
        require(sampler.at("filter").get<std::string>() == name(selected.m_filter, K_FILTERS) &&
                    sampler.at("wrap").get<std::string>() == name(selected.m_wrap, K_WRAPS) &&
                    sampler.at("mip").get<std::string>() == "NONE",
                "Material sampler differs");
        if (K_CAPTURED) {
            require(K_DOC.at("normal").is_null(), "Captured appearance cannot claim a normal map");
        } else {
            keys(K_DOC.at("normal"), { "basis", "y" });
            require(K_DOC.at("normal").at("basis").get<std::string>() ==
                            "GEOMETRY_TANGENT_W_SIGN" &&
                        K_DOC.at("normal").at("y").get<std::string>() == "POSITIVE_BITANGENT",
                    "Unsupported material normal basis");
        }
        array(K_DOC.at("fit_inputs"), 3uz);
        require(K_DOC.at("fit_inputs").size() == fits.size(), "Material fit pin table differs");
        std::set<MaterialFitRole> parsed_fits;
        for (const auto& row : K_DOC.at("fit_inputs")) {
            keys(row, { "role", "product" });
            const auto K_FOUND = std::ranges::find_if(selected.m_fit_inputs, [&](const auto& pin) {
                return row.at("role").get<std::string>() == name(pin.m_role, K_FITS) &&
                       reference(row.at("product")) == pin.m_product;
            });
            require(K_FOUND != selected.m_fit_inputs.end() &&
                        parsed_fits.insert(K_FOUND->m_role).second,
                    "Material fit pin differs",
                    ErrorCode::RESYNC_REQUIRED);
        }
        const auto& channel_rows = K_DOC.at("channels");
        require(channel_rows.is_object() && channel_rows.size() == selected.m_channels.size(),
                "Material channel table differs");
        std::map<MaterialChannelRole, MaterialChannel> channels;
        for (const auto& [K_ROLE, K_PIN] : selected.m_channels) {
            check_stop(stop);
            const auto K_NAME = name(K_ROLE, K_CHANNELS);
            require((K_CAPTURED && K_ROLE == MaterialChannelRole::CAPTURED_RGB) ||
                        (!K_CAPTURED && K_ROLE != MaterialChannelRole::CAPTURED_RGB),
                    "Material channel role differs from lighting mode");
            const auto& row = channel_rows.at(K_NAME);
            keys(row, { "product", "provenance", "color_space" });
            const bool K_COLOR = K_ROLE == MaterialChannelRole::CAPTURED_RGB ||
                                 K_ROLE == MaterialChannelRole::BASE_COLOR ||
                                 K_ROLE == MaterialChannelRole::EMISSION;
            require(reference(row.at("product")) == K_PIN.m_product &&
                        reference(row.at("provenance")) == K_PIN.m_provenance &&
                        row.at("color_space").get<std::string>() ==
                            (K_COLOR ? "LINEAR_SRGB" : "LINEAR_DATA"),
                    "Material channel binding/color space differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& channel_node = bind(K_PIN.m_product, receipt);
            (void)bind(K_PIN.m_provenance, receipt);
            parents(channel_node, channel_parents);
            const AssetRegionDomain K_DOMAIN { AssetRegionKind::MATERIAL_TEXELS,
                                              static_cast<std::uint64_t>(selected.m_width) *
                                                  selected.m_height,
                                              selected.m_width,
                                              selected.m_height };
            require(K_PIN.m_region.m_target == K_PIN.m_product &&
                        K_PIN.m_region.m_domain == K_DOMAIN &&
                        K_PIN.m_region.m_sources.size() <= 16uz,
                    "Material provenance target/grid differs",
                    ErrorCode::RESYNC_REQUIRED);
            for (const auto& source : K_PIN.m_region.m_sources) {
                const auto K_VIEW =
                    std::ranges::find_if(geometry.selection().m_views, [&](const auto& pin) {
                        return source.m_view == AssetSourceViewId { pin.m_capture, pin.m_frame };
                    });
                require(K_VIEW != geometry.selection().m_views.end() &&
                            source.m_calibration == K_VIEW->m_calibration &&
                            source.m_image == K_VIEW->m_image &&
                            source.m_pose_sha256 == K_VIEW->m_pose_asset.m_content_sha256,
                        "Material provenance is not selected inference geometry",
                        ErrorCode::RESYNC_REQUIRED);
            }
            auto bytes = take(receipt.copy_blob(K_PIN.m_product.m_content_sha256, 786496uz, stop));
            auto provenance_bytes =
                take(receipt.copy_blob(K_PIN.m_provenance.m_content_sha256, 256uz * 1024uz, stop));
            auto values = pixels(bytes, K_ROLE, K_PIN, selected, expected, stop);
            auto provenance = take(inspect_asset_region_provenance(
                receipt, K_PIN.m_provenance.m_product, K_PIN.m_region, expected, stop));
            const auto& coverage = provenance.m_coverage;
            require(!K_CAPTURED || (coverage[2uz] == 0 && coverage[3uz] == 0 && coverage[4uz] == 0),
                    "Captured appearance includes inferred/generated/edited texels");
            require(selected.m_lighting != AssetLightingMode::ESTIMATED_PBR ||
                        (coverage[3uz] == 0 && coverage[4uz] == 0),
                    "Estimated PBR includes generated/edited texels");
            channels.emplace(
                K_ROLE,
                MaterialChannel { components(K_ROLE), std::move(values), std::move(provenance) });
            record(records, key("material-channel-", K_PIN.m_product), std::move(bytes));
            record(
                records, key("material-region-", K_PIN.m_provenance), std::move(provenance_bytes));
            dependencies.emplace(K_PIN.m_product.m_product, K_PIN.m_product);
            dependencies.emplace(K_PIN.m_provenance.m_product, K_PIN.m_provenance);
        }
        parents(node, dependencies);
        record(records, key("material-", selected.m_product), std::move(root_bytes));
        auto owned = selected;
        for (auto& pin : owned.m_fit_inputs) {
            std::ranges::sort(pin.m_views);
        }
        std::ranges::sort(owned.m_fit_inputs, {}, &MaterialFitSelection::m_role);
        std::ranges::sort(fits, {}, [](const auto& input) {
            return input.m_selection.m_role;
        });
        check_stop(stop);
        return AdmittedMaterialProduct(
            std::make_shared<const AdmittedMaterialProduct::Implementation>(
                AdmittedMaterialProduct::Implementation { std::move(owned),
                                                         geometry.selection().m_product,
                                                         std::move(channels),
                                                         std::move(fits),
                                                         receipt.receipt_sha256(),
                                                         expected,
                                                         geometry.synthetic_source(),
                                                         node.m_manual_ancestor,
                                                         std::move(records) }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Material product allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_material_product(const AdmittedMaterialProduct& material,
                                              const AdmittedAssetReceipt& receipt,
                                              const std::filesystem::path& directory,
                                              OperationId operation,
                                              std::uint64_t base_generation,
                                              const AssetDependencyContext& expected,
                                              const std::stop_token& stop,
                                              const std::function<void(std::size_t)>& observed) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        return std::unexpected(K_GRAPH.error());
    }
    if (material.receipt_sha256() != receipt.receipt_sha256() ||
        material.context().m_world_epoch != expected.m_world_epoch ||
        material.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Material publication context differs");
    }
    try {
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         material.m_state->m_records,
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Material publication allocation failed");
    }
}

Result<AdmittedMaterialProduct> read_material_product(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const MaterialProductRequest& request,
                                                      const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(commit.m_manifest_sha256);
        auto material = take(inspect_material_product(request, stop));
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && K_SNAPSHOT.m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Material selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        // Full closure: a root alone cannot replace independently admitted channels/regions/fits.
        const auto& selection = material.selection();
        std::vector<std::pair<std::string, AssetRevisionRef>> pins {
            { key("material-", selection.m_product), selection.m_product } };
        for (const auto& [K_ROLE, K_PIN] : selection.m_channels) {
            pins.emplace_back(key("material-channel-", K_PIN.m_product), K_PIN.m_product);
            pins.emplace_back(key("material-region-", K_PIN.m_provenance), K_PIN.m_provenance);
        }
        for (const auto& pin : selection.m_fit_inputs) {
            pins.emplace_back(key("material-fit-", pin.m_product), pin.m_product);
        }
        for (const auto& [K_KEY, K_REF] : pins) {
            const auto K_FOUND = K_SNAPSHOT.m_records.find(K_KEY);
            require(K_FOUND != K_SNAPSHOT.m_records.end() &&
                        sha256(K_FOUND->second) == K_REF.m_content_sha256,
                    "Selected material closure is not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return material;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Material reload allocation failed");
    }
}

}  // namespace world_explorer
