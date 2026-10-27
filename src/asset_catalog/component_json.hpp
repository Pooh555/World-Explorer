#pragma once

#include "recipe_json.hpp"

namespace world_explorer::asset_component_detail {

using namespace asset_recipe_detail;

inline AssetRecipeRole role(const Json& value) {
    static const std::map<std::string, AssetRecipeRole> K_ROLES {
        { "CODE", AssetRecipeRole::CODE },
        { "ENVIRONMENT", AssetRecipeRole::ENVIRONMENT },
        { "MODEL", AssetRecipeRole::MODEL },
        { "INPUT", AssetRecipeRole::INPUT } };
    return named(value, K_ROLES);
}

inline std::string reviewer(const std::string& value) {
    require(!value.empty() && value.size() <= 128uz &&
                value.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:@-") ==
                    std::string::npos,
            "Invalid asset reviewer identifier");
    return value;
}

inline std::vector<std::byte> read(const AdmittedAssetReceipt& receipt,
                                   const std::string& sha,
                                   std::size_t limit,
                                   const std::stop_token& stop) {
    auto bytes = receipt.copy_blob(sha, limit, stop);
    if (!bytes) {
        throw RecipeFailure(bytes.error().m_code, bytes.error().m_message);
    }
    return std::move(*bytes);
}

inline std::string portable_path(const Json& value) {
    auto text = value.get<std::string>();
    require(!text.empty() && text.size() <= 240uz && text.front() != '/' && text.back() != '/' &&
                text.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/") ==
                    std::string::npos,
            "Invalid component inventory path");
    std::size_t offset {};
    while (offset < text.size()) {
        const auto K_END = text.find('/', offset);
        const auto K_PART =
            text.substr(offset, K_END == text.npos ? text.size() - offset : K_END - offset);
        require(!K_PART.empty() && K_PART != "." && K_PART != ".." && K_PART.back() != '.' &&
                    K_PART.size() <= 128uz,
                "Unsafe component inventory path segment");
        if (K_END == text.npos) {
            break;
        }
        offset = K_END + 1uz;
    }
    for (auto& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    offset = 0uz;
    while (offset < text.size()) {
        const auto K_END = text.find('/', offset);
        const auto K_PART =
            text.substr(offset, K_END == text.npos ? text.size() - offset : K_END - offset);
        const auto K_BASE = K_PART.substr(0uz, K_PART.find('.'));
        require(K_BASE != "con" && K_BASE != "prn" && K_BASE != "aux" && K_BASE != "nul" &&
                    !(K_BASE.size() == 4uz &&
                      (K_BASE.starts_with("com") || K_BASE.starts_with("lpt")) &&
                      K_BASE[3uz] >= '1' && K_BASE[3uz] <= '9'),
                "Reserved component inventory filename");
        if (K_END == text.npos) {
            break;
        }
        offset = K_END + 1uz;
    }
    return text;
}

}  // namespace world_explorer::asset_component_detail
