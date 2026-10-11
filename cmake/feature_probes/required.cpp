#include <concepts>
#include <expected>
#include <span>
#include <stop_token>
#include <thread>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error std::expected basic API is required
#endif

#if !defined(__cpp_lib_jthread) || __cpp_lib_jthread < 201911L
#error std::jthread and std::stop_token are required
#endif

int main() {
    std::expected<int, int> value{3};
    std::jthread worker([](std::stop_token) {});
    int values[]{1, 2};
    std::span<int> view(values);

    return value.value() == 3 && view.size() == 2 ? 0 : 1;
}
