#include "world_explorer/asset_catalog/dependency_receipt.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    using namespace world_explorer;
    if (argc != 5) {
        return 64;
    }
    const AssetDependencyContext K_CONTEXT { { 3 }, { 5 } };
    const auto K_RECEIPT = read_asset_receipt({ argv[1], argv[2] }, K_CONTEXT);
    if (!K_RECEIPT) {
        std::cerr << K_RECEIPT.error().m_message << '\n';
        return 2;
    }
    const auto K_CRASH = std::string_view(argv[4]) == "crash";
    const auto K_RESULT =
        publish_asset_receipt(*K_RECEIPT, argv[3], K_CONTEXT, {}, [K_CRASH](std::size_t count) {
            if (K_CRASH && count == 1uz) {
                std::_Exit(77);
            }
        });
    if (!K_RESULT) {
        std::cerr << K_RESULT.error().m_message << '\n';
        return 2;
    }
    std::cout << K_RECEIPT->receipt_sha256() << '\n';
    return 0;
}
