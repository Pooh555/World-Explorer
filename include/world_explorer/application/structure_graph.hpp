#pragma once

#include "world_explorer/application/geometry_product.hpp"

namespace world_explorer {

using StructureSurfaceId = TypedId<struct structure_surface_tag>;
using StructureOpeningId = TypedId<struct structure_opening_tag>;
using StructureRoomId = TypedId<struct structure_room_tag>;
enum class StructureSurfaceKind : std::uint8_t { WALL, FLOOR, CEILING, OTHER };
enum class StructureThicknessSource : std::uint8_t {
    UNKNOWN,
    DECLARED_INFERRED,
    DECLARED_GENERATED
};
enum class StructureRelationKind : std::uint8_t { CONTACT, SUPPORT };

struct StructurePlane {
    Vec3d m_normal;
    double m_offset_units {}, m_residual_bound_units {}, m_actual_residual_units {};
};

struct StructureSurface {
    StructureSurfaceId m_id;
    StructureSurfaceKind m_kind;
    std::vector<std::uint32_t> m_faces;
    std::vector<std::vector<std::uint32_t>> m_loops;
    std::optional<StructurePlane> m_plane;
    StructureThicknessSource m_thickness_source;
    std::optional<double> m_thickness_units;
    std::array<std::uint64_t, 5> m_coverage {};
};

struct StructureOpening {
    StructureOpeningId m_id;
    StructureSurfaceId m_surface;
    std::uint32_t m_loop {};
    StructureRoomId m_first_room;
    std::optional<StructureRoomId> m_second_room;
    AssetEvidenceClass m_class;
    std::optional<double> m_confidence;
};

struct StructureContainment {
    StructureSurfaceId m_surface;
    double m_residual_bound_units {}, m_actual_residual_units {};
};

struct StructureRoom {
    StructureRoomId m_id;
    std::vector<StructureSurfaceId> m_boundaries;
    // Declared AABB containment only, never a closed-room occupancy certificate.
    std::vector<StructureContainment> m_contains;
};

struct StructureRelation {
    StructureRelationKind m_kind;
    StructureSurfaceId m_source, m_target;
    std::uint32_t m_source_vertex {}, m_target_vertex {};
    double m_residual_bound_units {}, m_actual_residual_units {};
};

struct StructureGraphSelection {
    AssetRevisionRef m_product;
    double m_max_plane_residual_units {}, m_max_relation_residual_units {};
};

struct StructureGraphRequest {
    std::optional<StructureGraphSelection> m_selection;
    std::optional<AdmittedGeometryProduct> m_geometry;
    std::optional<AdmittedCaptureSelection> m_capture;
    std::optional<AdmittedAssetReceipt> m_receipt;
    AssetDependencyContext m_expected;
};

// Owned partial-patch topology and declared spatial relations; no reconstruction,
// measured scale/gravity, physical opening/support or collision approval.
class AdmittedStructureGraph {
  public:
    AdmittedStructureGraph(const AdmittedStructureGraph&) = default;
    AdmittedStructureGraph& operator=(const AdmittedStructureGraph&) = default;
    const StructureGraphSelection& selection() const;
    const AssetRevisionRef& geometry_product() const;
    const std::vector<StructureSurface>& surfaces() const;
    const std::vector<StructureOpening>& openings() const;
    const std::vector<StructureRoom>& rooms() const;
    const std::vector<StructureRelation>& relations() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool synthetic_source() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedStructureGraph(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedStructureGraph> inspect_structure_graph(const StructureGraphRequest&,
                                                                  const std::stop_token&);
    friend Result<RecordCommit> publish_structure_graph(const AdmittedStructureGraph&,
                                                        const AdmittedAssetReceipt&,
                                                        const std::filesystem::path&,
                                                        OperationId,
                                                        std::uint64_t,
                                                        const AssetDependencyContext&,
                                                        const std::stop_token&,
                                                        const std::function<void(std::size_t)>&);
};

// <=256 KiB; 1..32 surfaces, <=32 rooms, <=64 openings, <=128 relations;
// <=8192 distinct selected mesh faces and <=1024 total boundary vertices.
Result<AdmittedStructureGraph> inspect_structure_graph(const StructureGraphRequest& request,
                                                       const std::stop_token& stop = {});
Result<RecordCommit> publish_structure_graph(const AdmittedStructureGraph& structure,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop = {},
                                             const std::function<void(std::size_t)>& observed = {});
Result<AdmittedStructureGraph> read_structure_graph(const std::filesystem::path& directory,
                                                    const RecordCommit& commit,
                                                    const StructureGraphRequest& request,
                                                    const std::stop_token& stop = {});

}  // namespace world_explorer
