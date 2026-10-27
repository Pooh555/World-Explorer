#pragma once

#include "world_explorer/application/native_assets.hpp"
#include "world_explorer/application/structure_graph.hpp"

namespace world_explorer {

struct NativeStructureRelation {
    StructureRelationKind m_kind;
    StructureSurfaceId m_source, m_target;
    auto operator<=>(const NativeStructureRelation&) const = default;
};

struct NativeStructureSelection {
    std::optional<AdmittedStructureGraph> m_graph;
    AssetDefinitionRef m_definition;
    // Explicit complete graph identity sets and all copies of this definition.
    std::vector<StructureSurfaceId> m_surfaces;
    std::vector<StructureOpeningId> m_openings;
    std::vector<StructureRoomId> m_rooms;
    std::vector<NativeStructureRelation> m_relations;
    std::vector<AssetInstanceId> m_instances;
};

struct NativeStructureRequest {
    std::optional<AdmittedNativeAssets> m_assets;
    std::optional<AdmittedAssetReceipt> m_receipt;
    std::vector<NativeStructureSelection> m_structures;
    AssetDependencyContext m_expected;
};

// Whole-definition partial graphs, not independent patch extraction, room
// closure, measured gravity, physical support, collision or AP qualification.
class AdmittedNativeStructure {
  public:
    AdmittedNativeStructure(const AdmittedNativeStructure&) = default;
    AdmittedNativeStructure& operator=(const AdmittedNativeStructure&) = default;
    const AdmittedNativeAssets& assets() const;
    const std::vector<NativeStructureSelection>& structures() const;
    const std::string& canonical_json() const;
    const std::string& sha256() const;
    bool manual_ancestor() const;
    // Exact graph roots plus the authoritative binding, without asset payload copies.
    const std::vector<ImmutableRecord>& records() const;

  private:
    struct Implementation;
    explicit AdmittedNativeStructure(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedNativeStructure> inspect_native_structure(const NativeStructureRequest&,
                                                                    const std::stop_token&);
};

// <=16 structural definitions/graphs, <=64 visible instances; no opaque room/
// support fields are promoted. Geometry products must match exactly.
Result<AdmittedNativeStructure> inspect_native_structure(const NativeStructureRequest& request,
                                                         const std::stop_token& stop = {});
Result<RecordCommit>
publish_native_structure(const AdmittedNativeStructure& structure,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});
Result<AdmittedNativeStructure> read_native_structure(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const NativeStructureRequest& request,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
