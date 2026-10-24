#include "../fixtures/asset_component_fixture.hpp"
#include "world_explorer/asset_catalog/installed_components.hpp"

#include <catch2/generators/catch_generators.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace world_explorer;
using namespace world_explorer::asset_recipe_test;
using namespace world_explorer::asset_component_test;

namespace {

struct InstalledFixture {
    EvidenceFixture m_evidence;
    std::pair<AdmittedAssetReceipt, AdmittedAssetReceipt> m_receipts;
    std::vector<AssetComponentRoot> m_roots;

    std::pair<AdmittedAssetReceipt, AdmittedAssetReceipt>
    prepare(const std::function<void(EvidenceFixture&)>& change) {
        if (change) {
            change(m_evidence);
        }
        return m_evidence.freeze();
    }

    explicit InstalledFixture(const std::function<void(EvidenceFixture&)>& change = {})
        : m_receipts(prepare(change)) {
        const auto K_RESULT = m_evidence.inspect(m_receipts);
        REQUIRE(K_RESULT);
        for (const auto& component : K_RESULT->m_components) {
            if (component.m_role == AssetRecipeRole::INPUT) {
                continue;
            }
            const auto K_ROOT =
                m_evidence.m_recipe.m_root / ("installed_" + std::to_string(m_roots.size()));
            REQUIRE(std::filesystem::create_directory(K_ROOT));
            m_roots.push_back({ component.m_role, component.m_component_sha256, K_ROOT });
            for (const auto& file : component.m_files) {
                const auto K_PATH = K_ROOT / file.m_path;
                std::filesystem::create_directories(K_PATH.parent_path());
                write(K_PATH, file.m_bytes ? m_evidence.m_payloads.at(file.m_sha256) : "");
            }
        }
    }

    Result<AssetInstalledInspection> inspect(const std::stop_token& stop = {}) const {
        return inspect_asset_installed_components(m_receipts.first,
                                                  { 1000 },
                                                  m_receipts.second,
                                                  m_evidence.m_recipe.m_context,
                                                  m_evidence.m_recipe.m_host,
                                                  m_evidence.m_selected,
                                                  m_roots,
                                                  stop);
    }
};

}  // namespace

TEST_CASE("installed components reconcile exact trees without changing caller files",
          "[asset-installed]") {
    InstalledFixture fixture;
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    const auto K_EVIDENCE_BEFORE = tree(fixture.m_evidence.m_evidence.m_root);
    const auto K_FIRST = fixture.inspect();
    REQUIRE(K_FIRST);
    CHECK(K_FIRST->m_roots.size() == 2uz);
    CHECK(K_FIRST->m_file_count == 2uz);
    CHECK(K_FIRST->m_installed_bytes == 47);
    CHECK(!blocked(K_FIRST->m_evidence, AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED));
    CHECK(blocked(K_FIRST->m_evidence, AssetRecipeBlocker::RUNTIME_CLOSURE_UNVERIFIED));
    CHECK(blocked(K_FIRST->m_evidence, AssetRecipeBlocker::WORKER_LIMITS_UNVERIFIED));
    CHECK(blocked(K_FIRST->m_evidence, AssetRecipeBlocker::CONTRACTS_ONLY_HOST));
    CHECK(blocked(K_FIRST->m_evidence, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    std::ranges::reverse(fixture.m_roots);
    const auto K_REPLAY = fixture.inspect();
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_roots[0uz].m_directory == K_FIRST->m_roots[0uz].m_directory);
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
    CHECK(tree(fixture.m_evidence.m_evidence.m_root) == K_EVIDENCE_BEFORE);
    write(K_FIRST->m_roots[0uz].m_directory / "src/code.txt", "changed after observation");
    const auto K_CHANGED = fixture.inspect();
    REQUIRE(!K_CHANGED);
    CHECK(K_FIRST->m_installed_bytes == 47);  // Old results remain observations only.
}

TEST_CASE("installed components reject missing changed extra and case-aliased entries",
          "[asset-installed]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6);
    InstalledFixture fixture;
    const auto K_ROOT = fixture.m_roots[0uz].m_directory;
    const auto K_FILE = K_ROOT / "src/code.txt";
    if (K_VARIANT == 0) {
        REQUIRE(std::filesystem::remove(K_FILE));
    } else if (K_VARIANT == 1) {
        write(K_FILE, std::string(20uz, 'x'));  // Same size, wrong digest.
    } else if (K_VARIANT == 2) {
        write(K_FILE, "truncated");
    } else if (K_VARIANT == 3) {
        write(K_ROOT / "unlisted.txt", "preserve unlisted bytes");
    } else if (K_VARIANT == 4) {
        REQUIRE(std::filesystem::create_directory(K_ROOT / "extra_empty"));
    } else if (K_VARIANT == 5) {
        std::filesystem::rename(K_FILE, K_ROOT / "src/Code.txt");
    } else {
        REQUIRE(std::filesystem::remove(K_FILE));
        REQUIRE(std::filesystem::create_directory(K_FILE));
    }
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
}

TEST_CASE("installed components require exact independent disjoint root selections",
          "[asset-installed]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    InstalledFixture fixture;
    if (K_VARIANT == 0) {
        fixture.m_roots.pop_back();
    } else if (K_VARIANT == 1) {
        fixture.m_roots.push_back(fixture.m_roots.front());
    } else if (K_VARIANT == 2) {
        fixture.m_roots[1uz].m_directory = fixture.m_roots[0uz].m_directory;
    } else if (K_VARIANT == 3) {
        fixture.m_roots[1uz].m_directory = fixture.m_roots[0uz].m_directory / "src";
    } else if (K_VARIANT == 4) {
        fixture.m_roots[0uz].m_component_sha256 = std::string(64uz, 'a');
    } else if (K_VARIANT == 5) {
        fixture.m_roots[0uz].m_role = AssetRecipeRole::INPUT;
    } else if (K_VARIANT == 6) {
        fixture.m_roots[0uz].m_directory = "relative_root";
    } else if (K_VARIANT == 7) {
        fixture.m_roots[0uz].m_directory /= "missing_root";
    } else {
        fixture.m_roots.resize(19uz, fixture.m_roots.front());
    }
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
}

TEST_CASE("installed component checks preserve cancellation stale context and rights blockers",
          "[asset-installed]") {
    const auto K_RESTRICTED = GENERATE(false, true);
    InstalledFixture fixture([&](EvidenceFixture& evidence) {
        if (K_RESTRICTED) {
            evidence.m_local_use = "RESTRICTED";
        }
    });
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED = fixture.inspect(cancellation.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    fixture.m_evidence.m_recipe.m_context.m_pose_revision = { 6 };
    const auto K_STALE = fixture.inspect();
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    fixture.m_evidence.m_recipe.m_context.m_pose_revision = { 5 };
    fixture.m_evidence.m_selected.clear();
    const auto K_UNSELECTED = fixture.inspect();
    REQUIRE(K_UNSELECTED);
    CHECK(blocked(K_UNSELECTED->m_evidence, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
    CHECK(blocked(K_UNSELECTED->m_evidence, AssetRecipeBlocker::RESTRICTED_LOCAL_RIGHTS) ==
          K_RESTRICTED);
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
}

#ifndef _WIN32
TEST_CASE("installed components reject symbolic roots ancestors files and hard links",
          "[asset-installed]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5);
    InstalledFixture fixture;
    const auto K_ROOT = fixture.m_roots[0uz].m_directory;
    const auto K_FILE = K_ROOT / "src/code.txt";
    const auto K_OUTSIDE = fixture.m_evidence.m_recipe.m_root / "outside.txt";
    write(K_OUTSIDE, "synthetic code bytes");
    const auto K_OUTSIDE_HASH = sha256_file(K_OUTSIDE, 1024uz);
    REQUIRE(K_OUTSIDE_HASH);
    if (K_VARIANT <= 1) {
        const auto K_LINK = fixture.m_evidence.m_recipe.m_root / "linked";
        std::filesystem::create_directory_symlink(K_VARIANT == 0 ? K_ROOT : K_ROOT.parent_path(),
                                                  K_LINK);
        fixture.m_roots[0uz].m_directory = K_VARIANT == 0 ? K_LINK : K_LINK / K_ROOT.filename();
    } else {
        REQUIRE(std::filesystem::remove(K_FILE));
        if (K_VARIANT == 2) {
            std::filesystem::create_symlink(K_OUTSIDE, K_FILE);
        } else if (K_VARIANT == 3) {
            std::filesystem::create_symlink(K_ROOT / "missing", K_FILE);
        } else if (K_VARIANT == 4) {
            std::filesystem::create_hard_link(K_OUTSIDE, K_FILE);
        } else {
            REQUIRE(::mkfifo(K_FILE.c_str(), 0600) == 0);
        }
    }
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    const auto K_OUTSIDE_AFTER = sha256_file(K_OUTSIDE, 1024uz);
    REQUIRE(K_OUTSIDE_AFTER);
    CHECK(*K_OUTSIDE_AFTER == *K_OUTSIDE_HASH);
    if (K_VARIANT >= 2) {
        CHECK(std::filesystem::exists(std::filesystem::symlink_status(K_FILE)));
    }
}
#endif

TEST_CASE("installed components reconcile models empty files and exact path depth",
          "[asset-installed]") {
    const auto K_EXCESS = GENERATE(false, true);
    InstalledFixture fixture([&](EvidenceFixture& evidence) {
        auto model = evidence.m_manifests[1uz];
        model["role"] = "MODEL";
        std::string path;
        for (std::size_t index { 0uz }; index < (K_EXCESS ? 16uz : 15uz); ++index) {
            path += "d/";
        }
        path += "model.bin";
        model["files"][0uz]["path"] = path;
        model["files"].push_back({ { "path", "Empty.marker" }, { "sha256", hash("") }, { "bytes", 0 } });
        evidence.m_manifests.push_back(model);
    });
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    const auto K_RESULT = fixture.inspect();
    if (K_EXCESS) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->m_roots.size() == 3uz);
        CHECK(K_RESULT->m_file_count == 4uz);
        CHECK(K_RESULT->m_installed_bytes == 74);
    }
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
}

TEST_CASE("installed components enforce the exact aggregate read budget including shared payloads",
          "[asset-installed]") {
    const auto K_EXCESS = GENERATE(false, true);
    InstalledFixture fixture([&](EvidenceFixture& evidence) {
        auto& manifest = evidence.m_manifests[0uz];
        const auto K_ENV_BYTES = evidence.m_manifests[1uz]["installed_bytes"].get<std::size_t>();
        manifest["files"] = Json::array();
        for (std::size_t index { 0uz }; index < 4uz; ++index) {
            const auto K_BYTES = 16uz * 1024uz * 1024uz - (index == 3uz ? K_ENV_BYTES : 0uz) +
                                 (K_EXCESS && index == 3uz ? 1uz : 0uz);
            const auto K_SHA = evidence.payload(std::string(K_BYTES, 'p'));
            manifest["files"].push_back({ { "path", "payload_" + std::to_string(index) },
                                         { "sha256", K_SHA },
                                         { "bytes", K_BYTES } });
        }
        manifest["installed_bytes"] = 64uz * 1024uz * 1024uz - K_ENV_BYTES + (K_EXCESS ? 1uz : 0uz);
    });
    const auto K_RESULT = fixture.inspect();
    if (K_EXCESS) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->m_installed_bytes == 64uz * 1024uz * 1024uz);
        CHECK(K_RESULT->m_file_count == 5uz);
    }
}

TEST_CASE("installed components bound directory entries independently from the file budget",
          "[asset-installed]") {
    const auto K_EXCESS = GENERATE(false, true);
    InstalledFixture fixture([&](EvidenceFixture& evidence) {
        for (std::size_t model { 0uz }; model < 2uz; ++model) {
            auto manifest = evidence.m_manifests[1uz];
            manifest["role"] = "MODEL";
            evidence.m_manifests.push_back(manifest);
        }
        for (std::size_t component_index { 0uz }; component_index < 4uz; ++component_index) {
            auto& manifest = evidence.m_manifests[component_index];
            const auto K_PAYLOAD = manifest["files"][0uz];
            manifest["files"] = Json::array();
            const auto K_COUNT = component_index == 3uz ? 1023uz : 1024uz;
            for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
                auto file = K_PAYLOAD;
                const std::string K_PREFIX =
                    component_index == 0uz ? (K_EXCESS ? "a/b/c/" : "a/b/") : "";
                file["path"] = K_PREFIX + "d" + std::to_string(component_index) + "_" +
                               std::to_string(index) + "/file";
                manifest["files"].push_back(file);
            }
            manifest["installed_bytes"] = K_COUNT * K_PAYLOAD["bytes"].get<std::size_t>();
        }
    });
    const auto K_RESULT = fixture.inspect();
    if (K_EXCESS) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->m_file_count == 4095uz);
    }
}
