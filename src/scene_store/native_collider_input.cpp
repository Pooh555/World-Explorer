#include "world_explorer/scene_store/native_collider_input.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>

namespace world_explorer {
namespace {

using namespace json_record_detail;

struct Reader {
    std::span<const std::byte> m_bytes;
    std::size_t m_at {};

    std::span<const std::byte> read(std::size_t count) {
        require(count <= m_bytes.size() - m_at, "Truncated native collider input");
        const auto K_PART = m_bytes.subspan(m_at, count);
        m_at += count;
        return K_PART;
    }

    std::uint64_t word() {
        const auto K_BYTES = read(8uz);
        std::uint64_t value {};
        for (std::size_t byte { 0uz }; byte < 8uz; ++byte) {
            value |= std::to_integer<std::uint64_t>(K_BYTES[byte]) << (8uz * byte);
        }
        return value;
    }

    bool text(std::string_view expected) {
        const auto K_BYTES = read(expected.size());
        return same_bytes(K_BYTES, expected);
    }

    double real() {
        const auto K_BITS = word();
        require(K_BITS != (std::uint64_t { 1 } << 63u), "Noncanonical collider signed zero");
        const auto K_VALUE = std::bit_cast<double>(K_BITS);
        require(std::isfinite(K_VALUE) && std::abs(K_VALUE) <= 1e6,
                "Collider coordinate outside bounded range");
        return K_VALUE;
    }

    std::uint32_t index(std::size_t count) {
        const auto K_INDEX = word();
        require(K_INDEX < count, "Native collider input index outside arrays");
        return static_cast<std::uint32_t>(K_INDEX);
    }
};

bool same_binding(const ColliderInputBinding& a, const ColliderInputBinding& b) {
    return a.m_context.m_world_epoch == b.m_context.m_world_epoch &&
           a.m_context.m_pose_revision == b.m_context.m_pose_revision &&
           a.m_collider == b.m_collider && a.m_base == b.m_base &&
           a.m_receipt_sha256 == b.m_receipt_sha256 && a.m_canonical_sha256 == b.m_canonical_sha256;
}

}  // namespace

struct AdmittedColliderInput::Implementation {
    ColliderInputBinding m_binding;
    NativeColliderMesh m_mesh;
    std::vector<std::byte> m_bytes;
};

AdmittedColliderInput::AdmittedColliderInput(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const ColliderInputBinding& AdmittedColliderInput::binding() const {
    return m_state->m_binding;
}

const NativeColliderMesh& AdmittedColliderInput::mesh() const {
    return m_state->m_mesh;
}

std::span<const std::byte> AdmittedColliderInput::canonical_bytes() const {
    return m_state->m_bytes;
}

Result<void> AdmittedColliderInput::validate_binding(const ColliderInputBinding& current,
                                                     const std::stop_token& stop) const {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Collider binding check cancelled");
    }
    if (!same_binding(current, binding())) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Native collider source binding differs");
    }
    return {};
}

Result<AdmittedColliderInput> decode_native_collider_input(std::span<const std::byte> bytes,
                                                           const ColliderInputBinding& selected,
                                                           const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(bytes.size() <= 1024uz * 1024uz,
                "Collider input byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        for (const auto& hash : { selected.m_collider.m_content_sha256,
                                 selected.m_base.m_content_sha256,
                                 selected.m_receipt_sha256,
                                 selected.m_canonical_sha256 }) {
            digest_text(hash);
        }
        require(selected.m_context.m_world_epoch.m_value &&
                    selected.m_context.m_pose_revision.m_value &&
                    selected.m_collider.m_product.m_value && selected.m_collider.m_revision &&
                    selected.m_base.m_product.m_value && selected.m_base.m_revision &&
                    selected.m_collider.m_product != selected.m_base.m_product,
                "Independent collider input identities missing",
                ErrorCode::INVALID_ARGUMENT);
        require(sha256(bytes) == selected.m_canonical_sha256,
                "Collider input checksum differs",
                ErrorCode::RESYNC_REQUIRED);
        Reader in { bytes };
        require(in.text("WE_NATIVE_COLLIDER_TRIANGLES_V1") && in.word() == 1,
                "Unsupported collider input version",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(in.word() == selected.m_context.m_world_epoch.m_value &&
                    in.word() == selected.m_context.m_pose_revision.m_value,
                "Collider input context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(in.word() == 0 && in.word() == 0,
                "Unsupported collider weld/sidedness policy",
                ErrorCode::UNSUPPORTED_OPERATION);
        const auto K_MAX_VERTICES = in.word(), K_MAX_FACES = in.word();
        require(K_MAX_VERTICES >= 3 && K_MAX_VERTICES <= 8192 && K_MAX_FACES >= 1 &&
                    K_MAX_FACES <= 8192,
                "Collider input policy exceeds budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(in.word() == selected.m_collider.m_product.m_value &&
                    in.word() == selected.m_collider.m_revision &&
                    in.word() == selected.m_base.m_product.m_value &&
                    in.word() == selected.m_base.m_revision &&
                    in.text(selected.m_collider.m_content_sha256) &&
                    in.text(selected.m_base.m_content_sha256) && in.text(selected.m_receipt_sha256),
                "Collider input selected source differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_VERTICES = in.word(), K_FACES = in.word();
        require(K_VERTICES >= 3 && K_VERTICES <= K_MAX_VERTICES && K_FACES >= 1 &&
                    K_FACES <= K_MAX_FACES,
                "Collider input counts exceed selected limits",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(bytes.size() - in.m_at == K_VERTICES * 24 + K_FACES * 32,
                "Collider input length/count mismatch");
        NativeColliderMesh mesh;
        for (std::size_t vertex { 0uz }; vertex < K_VERTICES; ++vertex) {
            check_stop(stop);
            mesh.m_positions.push_back({ in.real(), in.real(), in.real() });
        }
        std::vector<bool> used(mesh.m_positions.size()), seen(static_cast<std::size_t>(K_FACES));
        std::set<std::array<std::uint32_t, 3>> unique;
        for (std::size_t face { 0uz }; face < K_FACES; ++face) {
            check_stop(stop);
            const auto K_SOURCE = in.index(static_cast<std::size_t>(K_FACES));
            require(!seen[K_SOURCE], "Collider input source face duplicated");
            seen[K_SOURCE] = true;
            const std::array K_TRIANGLE { in.index(mesh.m_positions.size()),
                                         in.index(mesh.m_positions.size()),
                                         in.index(mesh.m_positions.size()) };
            auto sorted = K_TRIANGLE;
            std::ranges::sort(sorted);
            const auto K_CROSS =
                cross(mesh.m_positions[K_TRIANGLE[1uz]] - mesh.m_positions[K_TRIANGLE[0uz]],
                      mesh.m_positions[K_TRIANGLE[2uz]] - mesh.m_positions[K_TRIANGLE[0uz]]);
            require(sorted[0uz] != sorted[1uz] && sorted[1uz] != sorted[2uz] &&
                        unique.insert(sorted).second && finite(K_CROSS) &&
                        dot(K_CROSS, K_CROSS) > 0,
                    "Degenerate or duplicate collider input face");
            for (auto vertex : K_TRIANGLE) {
                used[vertex] = true;
            }
            mesh.m_triangles.push_back(K_TRIANGLE);
            mesh.m_source_faces.push_back(K_SOURCE);
        }
        require(std::ranges::all_of(used,
                                    [](bool value) {
                                        return value;
                                    }),
                "Unused collider input vertex");
        check_stop(stop);
        return AdmittedColliderInput(std::make_shared<const AdmittedColliderInput::Implementation>(
            AdmittedColliderInput::Implementation {
                selected, std::move(mesh), { bytes.begin(), bytes.end() } }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collider input allocation failed");
    }
}

}  // namespace world_explorer
