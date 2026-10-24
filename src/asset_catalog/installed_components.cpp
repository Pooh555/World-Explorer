#include "world_explorer/asset_catalog/installed_components.hpp"

#include "recipe_json.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <new>

namespace world_explorer {
namespace {

using namespace asset_recipe_detail;
namespace fs = std::filesystem;
using ComponentKey = std::pair<AssetRecipeRole, std::string>;
constexpr std::uint64_t K_BYTE_LIMIT { 64uz * 1024uz * 1024uz };
constexpr std::size_t K_ENTRY_LIMIT { 8192uz };
constexpr std::size_t K_DEPTH_LIMIT { 16uz };

struct Tree {
    fs::path m_root;
    std::map<std::string, AssetComponentFile> m_files;
    std::set<std::string> m_directories;
};

void directories(const fs::path& path, const std::stop_token& stop) {
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        check_stop(stop);
        const auto K_STATUS = fs::symlink_status(current);
        require(fs::is_directory(K_STATUS) && !fs::is_symlink(K_STATUS),
                "Installed root/ancestor is missing, linked or not a directory");
        if (current == current.parent_path()) {
            break;
        }
    }
}

struct RootRelation {
    const fs::path& m_parent;
    const fs::path& m_child;
};

bool contains(const RootRelation& relation, const std::stop_token& stop) {
    for (auto current = relation.m_child; !current.empty(); current = current.parent_path()) {
        check_stop(stop);
        if (fs::equivalent(relation.m_parent, current)) {
            return true;
        }
        if (current == current.parent_path()) {
            break;
        }
    }
    return false;
}

using Observation = std::map<fs::path, fs::file_time_type>;

Observation scan(const std::vector<Tree>& trees, const std::stop_token& stop) {
    Observation result;
    std::size_t entries {};
    std::uint64_t bytes {};
    for (const auto& tree : trees) {
        directories(tree.m_root, stop);
        result.emplace(tree.m_root, fs::last_write_time(tree.m_root));
        std::vector<fs::path> pending { tree.m_root };
        std::size_t files {};
        std::size_t subdirectories {};
        while (!pending.empty()) {
            check_stop(stop);
            const auto K_DIRECTORY = pending.back();
            pending.pop_back();
            directories(K_DIRECTORY, stop);
            for (const auto& entry : fs::directory_iterator(K_DIRECTORY)) {
                check_stop(stop);
                require(++entries <= K_ENTRY_LIMIT,
                        "Installed tree entry budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                const auto& path = entry.path();
                const auto K_NAME = path.lexically_relative(tree.m_root).generic_string();
                const auto K_STATUS = fs::symlink_status(path);
                require(!fs::is_symlink(K_STATUS), "Linked installed entry");
                const auto K_TIME = fs::last_write_time(path);
                result.emplace(path, K_TIME);
                if (fs::is_directory(K_STATUS)) {
                    require(tree.m_directories.contains(K_NAME), "Extra installed directory");
                    pending.push_back(path);
                    ++subdirectories;
                    continue;
                }
                require(fs::is_regular_file(K_STATUS), "Nonregular installed entry");
                const auto K_FOUND = tree.m_files.find(K_NAME);
                require(K_FOUND != tree.m_files.end(), "Extra or case-aliased installed file");
                const auto& file = K_FOUND->second;
                require(fs::hard_link_count(path) == 1 && fs::file_size(path) == file.m_bytes,
                        "Installed file size/link count differs");
                require(file.m_bytes <= K_BYTE_LIMIT - bytes,
                        "Installed byte budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                bytes += file.m_bytes;
                directories(path.parent_path(), stop);
                const auto K_HASH = sha256_file(path, static_cast<std::size_t>(file.m_bytes), stop);
                if (!K_HASH) {
                    throw RecipeFailure(K_HASH.error().m_code, K_HASH.error().m_message);
                }
                require(*K_HASH == file.m_sha256, "Installed file content differs");
                directories(path.parent_path(), stop);
                require(fs::is_regular_file(fs::symlink_status(path)) &&
                            fs::hard_link_count(path) == 1 && fs::file_size(path) == file.m_bytes &&
                            fs::last_write_time(path) == K_TIME,
                        "Installed file changed during verification",
                        ErrorCode::RESYNC_REQUIRED);
                ++files;
            }
        }
        require(files == tree.m_files.size() && subdirectories == tree.m_directories.size(),
                "Installed inventory has missing entries");
    }
    return result;
}

}  // namespace

Result<AssetInstalledInspection>
inspect_asset_installed_components(const AdmittedAssetReceipt& recipe_receipt,
                                   AssetProductId product,
                                   const AdmittedAssetReceipt& evidence_receipt,
                                   const AssetDependencyContext& expected,
                                   const AssetRecipeHost& host,
                                   const std::vector<AssetReviewSelection>& selected_reviews,
                                   const std::vector<AssetComponentRoot>& selected_roots,
                                   const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(selected_roots.size() <= 18uz,
                "Too many selected installed roots",
                ErrorCode::INVALID_ARGUMENT);
        auto evidence = inspect_asset_recipe_evidence(
            recipe_receipt, product, evidence_receipt, expected, host, selected_reviews, stop);
        if (!evidence) {
            return std::unexpected(evidence.error());
        }
        std::map<ComponentKey, fs::path> roots;
        for (const auto& root : selected_roots) {
            check_stop(stop);
            require(root.m_directory.is_absolute(),
                    "Installed root must be explicitly absolute",
                    ErrorCode::INVALID_ARGUMENT);
            directories(root.m_directory, stop);
            const auto K_PATH = root.m_directory.lexically_normal();
            require(fs::canonical(K_PATH) == K_PATH, "Installed root has an alias");
            require(
                roots.emplace(ComponentKey { root.m_role, root.m_component_sha256 }, K_PATH).second,
                "Duplicate installed root selection",
                ErrorCode::INVALID_ARGUMENT);
            for (const auto& [K_KEY, K_EXISTING] : roots) {
                if (K_KEY != ComponentKey { root.m_role, root.m_component_sha256 }) {
                    require(!contains({ .m_parent = K_PATH, .m_child = K_EXISTING }, stop) &&
                                !contains({ .m_parent = K_EXISTING, .m_child = K_PATH }, stop),
                            "Installed roots overlap",
                            ErrorCode::INVALID_ARGUMENT);
                }
            }
        }
        AssetInstalledInspection result { std::move(*evidence), {}, 0, 0uz };
        std::vector<Tree> trees;
        for (const auto& component : result.m_evidence.m_components) {
            check_stop(stop);
            if (component.m_role == AssetRecipeRole::INPUT) {
                continue;
            }
            const ComponentKey K_KEY { component.m_role, component.m_component_sha256 };
            const auto K_FOUND = roots.find(K_KEY);
            require(K_FOUND != roots.end(),
                    "Missing installed root selection",
                    ErrorCode::INVALID_ARGUMENT);
            Tree tree { K_FOUND->second, {}, {} };
            for (const auto& file : component.m_files) {
                check_stop(stop);
                tree.m_files.emplace(file.m_path, file);
                const auto K_DEPTH =
                    static_cast<std::size_t>(std::ranges::count(file.m_path, '/')) + 1uz;
                require(K_DEPTH <= K_DEPTH_LIMIT,
                        "Installed path depth exceeds budget",
                        ErrorCode::RESOURCE_EXHAUSTED);
                for (auto slash = file.m_path.find('/'); slash != file.m_path.npos;
                     slash = file.m_path.find('/', slash + 1uz)) {
                    tree.m_directories.insert(file.m_path.substr(0uz, slash));
                }
            }
            result.m_file_count += component.m_file_count;
            result.m_installed_bytes = add(result.m_installed_bytes, component.m_installed_bytes);
            require(result.m_installed_bytes <= K_BYTE_LIMIT,
                    "Installed byte budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            result.m_roots.push_back({ component.m_role, component.m_component_sha256, tree.m_root });
            roots.erase(K_FOUND);
            trees.push_back(std::move(tree));
        }
        require(roots.empty(), "Unrelated installed root selection", ErrorCode::INVALID_ARGUMENT);
        const auto K_FIRST = scan(trees, stop);
        const auto K_SECOND = scan(trees, stop);
        require(K_FIRST == K_SECOND,
                "Installed tree changed between verification scans",
                ErrorCode::RESYNC_REQUIRED);
        auto& blockers = result.m_evidence.m_recipe.m_blockers;
        std::erase(blockers, AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED);
        blockers.push_back(AssetRecipeBlocker::RUNTIME_CLOSURE_UNVERIFIED);
        blockers.push_back(AssetRecipeBlocker::WORKER_LIMITS_UNVERIFIED);
        std::ranges::sort(blockers);
        check_stop(stop);
        return result;
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Installed inspection allocation failed");
    } catch (const fs::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
