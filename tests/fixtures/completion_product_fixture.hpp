#pragma once

#include "structure_graph_fixture.hpp"
#include "world_explorer/application/completion_product.hpp"

namespace world_explorer::completion_product_test {

using namespace structure_graph_test;

struct CompletionFixtureOptions {
    CompletionSubject m_subject { CompletionSubject::SHELL_GEOMETRY };
    CompletionMethod m_method { CompletionMethod::GENERATED };
    std::size_t m_candidates { 2uz };
    bool m_manual {}, m_inferred_source {}, m_distinct_libraries {};
};

struct CompletionFixtureMutations {
    std::function<void(Json&, CompletionProductSelection&)> m_root;
    std::function<void(Json&, std::size_t)> m_geometry;
    std::function<void(Json&, CompletionMethodSelection&)> m_configuration;
    std::function<void(std::string&)> m_root_bytes, m_configuration_bytes;
    std::function<void(std::string&, std::uint64_t)> m_input;
    std::function<void(AssetDependencyGraph&, const CompletionProductSelection&)> m_graph;
};

struct CompletionFixture {
    StructureFixture m_base;
    RecipeFixture m_output;
    CompletionProductSelection m_selected;
    Json m_record;
    std::vector<GeometryProductSelection> m_geometry_pins;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    AdmittedGeometryProduct m_source;
    AdmittedStructureGraph m_structure;
    std::vector<AdmittedGeometryProduct> m_candidates;

    explicit CompletionFixture(const CompletionFixtureOptions& options = {},
                               const CompletionFixtureMutations& changes = {})
        : m_base({ .m_record =
                      [&](Json& doc, auto&) {
                          if (options.m_inferred_source) {
                              doc["surfaces"][0uz]["coverage"] = { 0, 7, 1, 0, 0 };
                          }
                      },
                  .m_bytes = {},
                  .m_graph = {} },
                 false,
                 [&](Json& doc, auto&) {
                     if (options.m_inferred_source) {
                         doc["support"][7uz]["class"] = "INFERRED";
                     }
                 }),
          m_receipt(stage(options, changes)),
          m_capture(checked(m_base.m_base.m_views.inspect(m_receipt, false))), m_source(source()),
          m_structure(structure()), m_candidates(candidates()) { }

    AdmittedGeometryProduct source() const {
        auto request = m_base.m_base.request();
        request.m_receipt = m_receipt;
        request.m_capture = m_capture;
        return checked(inspect_geometry_product(request));
    }

    AdmittedStructureGraph structure() const {
        auto request = m_base.request();
        request.m_geometry = m_source;
        request.m_capture = m_capture;
        request.m_receipt = m_receipt;
        return checked(inspect_structure_graph(request));
    }

    std::vector<AdmittedGeometryProduct> candidates() const {
        std::vector<AdmittedGeometryProduct> result;
        for (const auto& pin : m_geometry_pins) {
            auto request = m_base.m_base.request();
            request.m_selection = pin;
            request.m_capture = m_capture;
            request.m_receipt = m_receipt;
            result.push_back(checked(inspect_geometry_product(request)));
        }
        return result;
    }

    AdmittedAssetReceipt stage(const CompletionFixtureOptions& options,
                               const CompletionFixtureMutations& changes) {
        auto& base = m_base.m_base;
        auto& region = base.m_views.m_object.m_region;
        const std::string K_RECIPE {
            "synthetic completion import; inference-only; no algorithm execution" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        const auto K_INPUT = [&](std::uint64_t id, const std::string& text) {
            auto bytes = text;
            if (changes.m_input) {
                changes.m_input(bytes, id);
            }
            auto node = region.node(id, bytes);
            node.m_recipe_sha256 = hash(K_RECIPE);
            region.m_graph.m_products.push_back(node);
            return node.m_product;
        };
        const auto K_ALGORITHM = K_INPUT(15200, "synthetic algorithm bytes; not executed");
        const auto K_RIGHTS =
            K_INPUT(15201, "synthetic rights declaration; no legal review or permission");
        const auto K_PROMPT =
            options.m_method == CompletionMethod::GENERATED
                ? std::optional { K_INPUT(15202,
                                         Json { { "schema_version", 1 },
                                               { "kind", "COMPLETION_PROMPT" },
                                               { "text", "synthetic fill diagnostic" } }
                                             .dump()) }
                : std::nullopt;
        const auto K_LIBRARY =
            options.m_method == CompletionMethod::LIBRARY && !options.m_distinct_libraries
                ? std::optional { K_INPUT(
                      15203, "synthetic opaque library bytes; no decoded library correspondence") }
                : std::nullopt;
        m_selected.m_subject = options.m_subject;
        m_selected.m_selected_candidate = 1;
        m_selected.m_max_region_extent_units = 10;
        if (options.m_subject == CompletionSubject::SHELL_GEOMETRY) {
            m_selected.m_surfaces = { { 11 } };
        }
        auto candidate_rows = Json::array();
        std::map<AssetProductId, AssetRevisionRef> root_parents {
            { base.m_selected.m_product.m_product, base.m_selected.m_product },
            { m_base.m_selected.m_product.m_product, m_base.m_selected.m_product } };
        for (std::size_t candidate_index { 0uz }; candidate_index < options.m_candidates;
             ++candidate_index) {
            const auto K_CANDIDATE_LIBRARY =
                options.m_method == CompletionMethod::LIBRARY && options.m_distinct_libraries
                    ? std::optional { K_INPUT(15400uz + candidate_index,
                                             "synthetic distinct library " +
                                                 std::to_string(candidate_index)) }
                    : K_LIBRARY;
            auto doc = base.m_record;
            doc["recipe_sha256"] = hash(K_RECIPE);
            doc["manual_ancestor"] = options.m_manual;
            doc["mesh"]["triangles"].push_back({ 4, 5, 6 });
            doc["mesh"]["triangles"].push_back({ 4, 6, 7 });
            doc["mesh"]["groups"].push_back(101);
            doc["mesh"]["groups"].push_back(101);
            for (const auto& face : { std::array { 4U, 5U, 6U }, std::array { 4U, 6U, 7U } }) {
                Json anchors = nullptr, views = Json::array();
                const auto K_CLASS = options.m_manual ? "MANUALLY_EDITED"
                                     : options.m_method == CompletionMethod::INFERRED ? "INFERRED"
                                                                                      : "GENERATED";
                if (options.m_method == CompletionMethod::INFERRED) {
                    anchors = Json::array();
                    views = doc["support"][0uz]["views"];
                    for (auto vertex : face) {
                        bool found {};
                        const auto K_FACES = ring_faces();
                        for (std::size_t source_face { 0uz }; source_face < K_FACES.size() && !found;
                             ++source_face) {
                            for (std::size_t corner { 0uz }; corner < 3uz && !found; ++corner) {
                                if (K_FACES[source_face][corner] == vertex) {
                                    std::array<double, 3> weights {};
                                    weights[corner] = 1;
                                    anchors.push_back(
                                        { { "triangle", source_face }, { "weights", weights } });
                                    found = true;
                                }
                            }
                        }
                    }
                }
                doc["support"].push_back({ { "class", K_CLASS },
                                          { "anchors", anchors },
                                          { "views", views },
                                          { "uncertainty_units", nullptr } });
            }
            if (changes.m_geometry) {
                changes.m_geometry(doc, candidate_index);
            }
            auto geometry_node = region.node(15000uz + candidate_index, doc.dump());
            geometry_node.m_recipe_sha256 = hash(K_RECIPE);
            const auto K_ORIGINAL = std::ranges::find(region.m_graph.m_products,
                                                      base.m_selected.m_product,
                                                      &AssetDependencyNode::m_product);
            if (K_ORIGINAL == region.m_graph.m_products.end()) {
                throw std::logic_error("Missing source geometry fixture node");
            }
            geometry_node.m_dependencies = K_ORIGINAL->m_dependencies;
            geometry_node.m_manual_ancestor = options.m_manual;
            if (options.m_manual) {
                const std::string K_ACTION { "synthetic declared completion edit" };
                region.m_data[hash(K_ACTION)] = K_ACTION;
                geometry_node.m_manual_action_sha256 = hash(K_ACTION);
            }
            region.m_graph.m_products.push_back(geometry_node);
            auto pin = base.m_selected;
            pin.m_product = geometry_node.m_product;
            m_geometry_pins.push_back(pin);
            CompletionMethodSelection method {
                options.m_method,
                options.m_method == CompletionMethod::LIBRARY ? 0 : 42uz + candidate_index,
                {},
                K_ALGORITHM,
                K_RIGHTS,
                K_PROMPT,
                K_CANDIDATE_LIBRARY };
            const std::array K_METHODS { "INFERRED", "GENERATED", "LIBRARY" };
            Json configuration {
                { "schema_version", 1 },
                { "kind", "COMPLETION_CONFIGURATION" },
                { "world_epoch", 3 },
                { "pose_revision", 5 },
                { "recipe_sha256", hash(K_RECIPE) },
                { "method", K_METHODS.at(static_cast<std::size_t>(options.m_method)) },
                { "seed", method.m_seed },
                { "algorithm", region_ref(K_ALGORITHM) },
                { "prompt", K_PROMPT ? region_ref(K_PROMPT.value()) : Json(nullptr) },
                { "library",
                 K_CANDIDATE_LIBRARY ? region_ref(K_CANDIDATE_LIBRARY.value()) : Json(nullptr) },
                { "rights", region_ref(K_RIGHTS) },
                { "manual_ancestor", false } };
            if (changes.m_configuration) {
                changes.m_configuration(configuration, method);
            }
            auto configuration_bytes = configuration.dump();
            if (changes.m_configuration_bytes) {
                changes.m_configuration_bytes(configuration_bytes);
            }
            auto configuration_node = region.node(15100uz + candidate_index, configuration_bytes);
            configuration_node.m_recipe_sha256 = hash(K_RECIPE);
            configuration_node.m_dependencies = { K_ALGORITHM, K_RIGHTS };
            if (K_PROMPT) {
                configuration_node.m_dependencies.push_back(K_PROMPT.value());
            }
            if (K_CANDIDATE_LIBRARY) {
                configuration_node.m_dependencies.push_back(K_CANDIDATE_LIBRARY.value());
            }
            method.m_configuration = configuration_node.m_product;
            region.m_graph.m_products.push_back(configuration_node);
            CompletionCandidateSelection selected {
                candidate_index + 1uz, geometry_node.m_product, method };
            m_selected.m_candidates.push_back(selected);
            auto protections = Json::array();
            for (std::size_t face { 0uz }; face < 8uz; ++face) {
                if (base.m_record["support"][face]["class"] == "OBSERVED") {
                    protections.push_back(
                        { { "source_face", face }, { "result_face", face }, { "rotation", 0 } });
                }
            }
            std::map<std::string, std::vector<std::size_t>> classes;
            for (std::size_t face { 0uz }; face < doc["support"].size(); ++face) {
                const auto K_CLASS = doc["support"][face]["class"].template get<std::string>();
                if (K_CLASS != "OBSERVED") {
                    classes[K_CLASS].push_back(face);
                }
            }
            auto regions = Json::array();
            std::size_t region_id {};
            for (const auto& [K_CLASS, K_FACES] : classes) {
                Vec3d minimum { 1e6, 1e6, 1e6 }, maximum { -1e6, -1e6, -1e6 };
                for (auto face : K_FACES) {
                    for (const auto& vertex : doc["mesh"]["triangles"][face]) {
                        const auto& point =
                            doc["mesh"]["positions"][vertex.template get<std::size_t>()];
                        const Vec3d K_POINT { point[0uz].template get<double>(),
                                             point[1uz].template get<double>(),
                                             point[2uz].template get<double>() };
                        minimum = { std::min(minimum.m_x, K_POINT.m_x),
                                   std::min(minimum.m_y, K_POINT.m_y),
                                   std::min(minimum.m_z, K_POINT.m_z) };
                        maximum = { std::max(maximum.m_x, K_POINT.m_x),
                                   std::max(maximum.m_y, K_POINT.m_y),
                                   std::max(maximum.m_z, K_POINT.m_z) };
                    }
                }
                regions.push_back(
                    { { "id", ++region_id },
                     { "faces", K_FACES },
                     { "class", K_CLASS },
                     { "confidence", K_CLASS == "UNKNOWN" ? Json(nullptr) : Json(.5) },
                     { "surfaces",
                      options.m_subject == CompletionSubject::SHELL_GEOMETRY ? Json::array({ 11 })
                                                                             : Json::array() },
                     { "bounds",
                      { { "minimum", { minimum.m_x, minimum.m_y, minimum.m_z } },
                       { "maximum", { maximum.m_x, maximum.m_y, maximum.m_z } } } } });
            }
            candidate_rows.push_back(
                { { "id", selected.m_id },
                 { "rank", candidate_index + 1uz },
                 { "geometry", region_ref(geometry_node.m_product) },
                 { "configuration", region_ref(configuration_node.m_product) },
                 { "rejection", candidate_index == 0uz ? "NONE" : "DECLARED_RANKED_ALTERNATIVE" },
                 { "protected", protections },
                 { "regions", regions },
                 { "checks",
                  { { "observed_anchors_preserved", true },
                   { "complete_region_partition", true },
                   { "region_extent_within_policy", true } } } });
            root_parents.emplace(geometry_node.m_product.m_product, geometry_node.m_product);
            root_parents.emplace(configuration_node.m_product.m_product,
                                 configuration_node.m_product);
        }
        const std::array K_SUBJECTS { "OBJECT_GEOMETRY", "SHELL_GEOMETRY" };
        m_record = { { "schema_version", 1 },
                    { "kind", "COMPLETION_PRODUCT" },
                    { "world_epoch", 3 },
                    { "pose_revision", 5 },
                    { "recipe_sha256", hash(K_RECIPE) },
                    { "source", region_ref(base.m_selected.m_product) },
                    { "structure", region_ref(m_base.m_selected.m_product) },
                    { "capture", region_ref(base.m_capture.record().m_selection.m_product) },
                    { "scope", "UNQUALIFIED_GEOMETRY_COMPLETION" },
                    { "subject",
                     { { "kind", K_SUBJECTS.at(static_cast<std::size_t>(options.m_subject)) },
                      { "surfaces",
                       options.m_subject == CompletionSubject::SHELL_GEOMETRY ? Json::array({ 11 })
                                                                              : Json::array() } } },
                    { "selected_candidate", 1 },
                    { "candidates", candidate_rows },
                    { "manual_ancestor", options.m_manual } };
        if (changes.m_root) {
            changes.m_root(m_record, m_selected);
        }
        auto root_bytes = m_record.dump();
        if (changes.m_root_bytes) {
            changes.m_root_bytes(root_bytes);
        }
        auto root = region.node(15300, root_bytes);
        root.m_recipe_sha256 = hash(K_RECIPE);
        root.m_manual_ancestor = options.m_manual;
        for (const auto& [K_ID, K_REF] : root_parents) {
            root.m_dependencies.push_back(K_REF);
        }
        m_selected.m_product = root.m_product;
        region.m_graph.m_products.push_back(root);
        if (changes.m_graph) {
            base.m_views.m_change_graph = [&](auto& graph) {
                changes.m_graph(graph, m_selected);
            };
        }
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto receipt = base.m_views.freeze(false);
        base.m_views.m_change_graph = {};
        return receipt;
    }

    CompletionProductRequest request() const {
        return { m_selected,
                m_source,
                m_structure,
                m_capture,
                m_receipt,
                m_candidates,
                m_receipt.graph().m_context };
    }

    Result<AdmittedCompletionProduct> inspect(const std::stop_token& stop = {}) const {
        return inspect_completion_product(request(), stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "completion";
    }
};

}  // namespace world_explorer::completion_product_test
