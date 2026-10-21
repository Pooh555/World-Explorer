#include "collision_inputs.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <set>

namespace world_explorer::collision_inputs {

void fields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size()) {
        throw std::runtime_error("Unexpected collision inspection fields");
    }
    for (const auto* name : names) {
        if (!value.contains(name)) {
            throw std::runtime_error("Missing collision inspection field");
        }
    }
}

double real(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        throw std::runtime_error("Collision probe scalars must be finite numeric data");
    }
    return value.get<double>();
}

Vec3d vector(const Json& value) {
    if (!value.is_array() || value.size() != 3uz) {
        throw std::runtime_error("Collision probe vectors must have three components");
    }
    return { real(value.at(0uz)), real(value.at(1uz)), real(value.at(2uz)) };
}

Result<Json> read_request(const std::filesystem::path& path,
                          std::string_view expected_sha256,
                          const std::stop_token& stop) {
    try {
        const auto K_BYTES = asset_files::read_bytes(path, 1024uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        if (sha256(*K_BYTES) != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "Collision inspection request checksum differs");
        }
        const std::string_view K_TEXT(reinterpret_cast<const char*>(K_BYTES->data()),
                                      K_BYTES->size());
        if (K_TEXT.find('\0') != K_TEXT.npos) {
            throw std::runtime_error("Raw NUL in collision inspection request");
        }
        std::vector<std::set<std::string>> keys;
        auto document = Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 8) {
                throw std::runtime_error("Collision inspection nesting exceeds budget");
            }
            const auto K_DEPTH = static_cast<std::size_t>(depth);
            if (event == Json::parse_event_t::object_start) {
                keys.resize(K_DEPTH + 1uz);
                keys[K_DEPTH].clear();
            }
            if (event == Json::parse_event_t::key &&
                !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second) {
                throw std::runtime_error("Duplicate collision inspection key");
            }
            return true;
        });
        return document;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collision input allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<Inputs>
load(const std::filesystem::path& request, const Json& document, const std::stop_token& stop) {
    try {
        const auto K_BASE = std::filesystem::canonical(request).parent_path();
        const auto K_PATH = [&](const char* name) {
            const auto K_NAME = document.at(name).get<std::string>();
            if (K_NAME.empty() || K_NAME.size() > 4096uz || K_NAME.find('\0') != K_NAME.npos) {
                throw std::runtime_error("Invalid collision inspection input path");
            }
            return std::filesystem::canonical(K_BASE / K_NAME);
        };
        const auto K_SOURCE = K_PATH("source_bundle");
        const auto K_MANIFEST_HASH = document.at("source_manifest_sha256").get<std::string>();
        if (K_MANIFEST_HASH.size() != 64uz) {
            throw std::runtime_error(
                "Collision source manifest pin must have 64 hexadecimal characters");
        }
        const auto K_SCENE = load_bundle(K_SOURCE, {}, K_MANIFEST_HASH);
        if (!K_SCENE) {
            return std::unexpected(K_SCENE.error());
        }
        const auto K_SUPPORT_BYTES =
            asset_files::read_bytes(K_PATH("support"), K_COLLISION_SUPPORT_DOCUMENT_BYTES, stop);
        if (!K_SUPPORT_BYTES) {
            return std::unexpected(K_SUPPORT_BYTES.error());
        }
        const auto K_SUPPORT = admit_collision_support(
            **K_SCENE,
            std::string_view(reinterpret_cast<const char*>(K_SUPPORT_BYTES->data()),
                             K_SUPPORT_BYTES->size()),
            document.at("support_sha256").get<std::string>(),
            stop);
        if (!K_SUPPORT) {
            return std::unexpected(K_SUPPORT.error());
        }
        return Inputs { K_SOURCE, *K_SCENE, *K_SUPPORT };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collision input allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer::collision_inputs
