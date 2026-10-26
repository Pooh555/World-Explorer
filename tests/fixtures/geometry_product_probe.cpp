#include "world_explorer/application/geometry_product.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <iostream>

int main(int argc, char** argv) {
    using namespace world_explorer;
    if (argc != 3) {
        return 64;
    }
    const BundleLimits K_LIMITS {
        { 64uz, 0uz, 32uz * 1024uz * 1024uz, 4096uz, 262144uz, 524288uz, 0uz } };
    const auto K_SOURCE = load_bundle(argv[1], K_LIMITS, argv[2]);
    if (!K_SOURCE) {
        std::cerr << K_SOURCE.error().m_message << '\n';
        return 3;
    }
    GeometryProductRequest request;
    request.m_source = *K_SOURCE;
    std::cout << "Native mesh source loaded read-only: " << scene_content_hash(**K_SOURCE) << '\n';
    const auto K_RESULT = inspect_geometry_product(request);
    if (!K_RESULT) {
        std::cerr << K_RESULT.error().m_message << '\n';
        return 2;
    }
    return 0;
}
