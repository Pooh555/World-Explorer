// =============================================================================
// File:        style_reference.cpp
// Purpose:     C++26 coding-style reference for developers and AI agents.
//              Every section demonstrates ONE area of the language and the
//              convention to use there. Copy the patterns, not the content.
// How to edit: Section 0 is the source of truth for naming, formatting and
//              whitespace. When you change a rule, change the example code
//              that demonstrates it in the same edit.
// Status:      Style reference. It illustrates conventions; it is not a
//              program. Features that are experimental or not yet available
//              in every supported toolchain are guarded by feature-test macros.
// =============================================================================
//
// TABLE OF CONTENTS
//   0. Global conventions (naming, formatting, vertical whitespace, comments)
//   1. File layout, includes, modules, preprocessor, macros
//   2. Namespaces
//   3. Constants, variables, initialization, types, casts
//   4. Enums, aggregates, unions, variants
//   5. Functions (params, returns, attributes, overloads, lambdas)
//   6. Classes (special members, access, inheritance, operators, deducing this)
//   7. Templates, concepts, constraints, variadics, specialization
//   8. Error handling (exceptions, expected, contracts, asserts)
//   9. Ownership, memory, allocators, RAII
//  10. Containers, ranges, algorithms, strings, formatting, I/O
//  11. Control flow (if/switch/loops/structured bindings)
//  12. Concurrency, coroutines, std::execution
//  13. Compile-time: constexpr, consteval, reflection, expansion statements
//  14. Interop, platform code, low-level (extern "C", bit ops, UB policy)
//  15. Tests
//
// -----------------------------------------------------------------------------
// 0. GLOBAL CONVENTIONS
// -----------------------------------------------------------------------------
// Naming:
//   Classes / structs / aliases / concepts / enums .............. PascalCase
//   Enumerators ................................................. SNAKE_CASE_UPPER
//   Template parameters ......................................... PascalCase
//   Functions / variables / parameters / namespaces / files ..... snake_case
//   Data members (private, protected and public fields) ......... m_snake_case
//   Constants (any named constexpr / const-initialized constant,
//     including static class constants and local constexpr) ..... K_SNAKE_CASE_UPPER
//   Mutable globals and thread-locals ........................... g_snake_case
//   Macros ...................................................... ACME_SNAKE_CASE_UPPER
//   Template type params: T, U (generic); Ts (packs); descriptive PascalCase
//     when meaning matters (e.g. Allocator, Predicate).
//   Names that must match the standard library keep the standard spelling:
//     member aliases (value_type, size_type), trait members (`value`),
//     `_v` / `_t` helpers, and customization points (std::formatter::parse).
//
// Formatting (enforce with clang-format; this file is the source of
//   truth, .clang-format must match):
//   - 4 spaces, no tabs.
//   - 100-column limit.
//   - Attached (K&R) braces. Every control-flow body is braced, including
//     one-line bodies, `if consteval`, and every arm of an if / else chain.
//     Enforce with clang-tidy readability-braces-around-statements and review.
//   - `T* p` / `T& r` (type-attached).
//   - West const: `const T&`.
//   - One declaration per line.
//   - `auto` per section 3.
//   - Brace initialization: spaces inside non-empty braces and `{}` when empty.
//     Use direct-list initialization for spelled types (`int count { 0 };`); no
//     space before the brace for a temporary (`std::string{ text }`). `auto`
//     declarations use `=` as shown in section 3.
//   - Trailing commas in multi-line initializer/enumerator lists.
//   - Line breaks: break after binary operators so a continued condition keeps
//     its operator at the end of the preceding line; align wrapped arguments
//     with the opening bracket, or use a hanging indent of 4 when impossible.
//   - Align consecutive enumerator values, one-line `case` returns and one-line
//     accessor groups in columns; never align across a blank line or a comment.
//
// Vertical whitespace (blank lines and line breaks; enforce with clang-format
//   where possible, the rest in review):
//   - LF line endings, exactly one newline at end of file, no trailing whitespace.
//   - Never more than one consecutive blank line.
//   - One blank line between top-level definitions, between member functions
//     whose definitions span more than one line, and between logical groups of
//     declarations. Consecutive one-line declarations of the same kind
//     (accessors, aliases, constants, using-declarations) stay together.
//   - One blank line after the opening brace and before the closing brace of a
//     namespace or `extern "C"` block. No blank line at the start or end of a
//     class, struct, enum, function, lambda or control-flow body.
//   - No blank line after an access specifier. One blank line before an access
//     specifier, except when it is the first line of the class.
//   - Inside functions: one blank line after a block of local declarations and
//     after the closing `}` of a compound statement when another statement
//     follows. Use blank lines to separate logical groups; do not add one between
//     each statement or between closely related operations. No blank line before
//     `else`, `catch` or a closing brace.
//   - A comment block sits directly above the declaration it documents, with one
//     blank line before the comment block. Free-standing comments have a blank
//     line before and after.
//   - Section banners (`// ----`) have one blank line before and after the banner.
//   - Preprocessor: one blank line around each include group and around an
//     `#if` ... `#endif` block, unless the block sits inside a declaration.
//   - One statement per line. A function or lambda body stays on one line
//     only when it is a single `return` and fits the column limit; every other
//     body breaks after `{`.
//   - When a call, template argument list or initializer list does not fit on
//     one line, put every element on its own line (no bin-packing). Break after
//     `|`, `.`, `?`, `:` and binary operators. `template <...>` and `requires`
//     clauses each get their own line.
//
// Comments:
//   - `//` everywhere; `/* */` only to disable code temporarily (never commit).
//   - Doc comments: `///` with Doxygen tags (@brief, @param, @return, @pre,
//     @post, @throws). Document WHY and contracts, not WHAT the code says.
//   - Task markers: TODO(owner): ..., FIXME(owner): ..., NOTE: ...
//     Every TODO must have an owner or ticket id, e.g. TODO(jdoe, PROJ-123).
//   - Comment sentences start with a capital letter and end with a period.
//     Trailing end-of-line comments and rule lists may be fragments.
//
// Language level policy: C++26 baseline. Anything experimental or
//   compiler-dependent must be behind a feature-test macro (see section 1).

// -----------------------------------------------------------------------------
// 1. FILE LAYOUT, INCLUDES, MODULES, PREPROCESSOR, MACROS
// -----------------------------------------------------------------------------

// Header guard: require exactly one `#pragma once` as the first line of headers.
// (In a real header, this would be the first line.)
// #pragma once

// File extensions: .hpp (headers), .cpp (sources), .cppm (module interface
//   units), .ipp (template implementation included by .hpp), _test.cpp (tests).

// Include order, blank line between groups, alphabetical in groups:
//   1. The file's own header (in .cpp files only)
//   2. Project headers            "..."  (full path from the project root)
//   3. Third-party libraries      <...>
//   4. C++ standard library       <...>  (no .h C headers; use <cstdint> etc.)
//   Include what you use. Never rely on transitive includes.
//   Prefer forward declarations only for pointer/reference use in headers.
// Modules: `import std;` is allowed in new code if the toolchain
//   supports it; the header-based form below is the fallback.
// (This file has no project or third-party includes, so only group 4 appears.)
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <generator>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <version>

#if defined(__cpp_impl_reflection)
#include <meta>
#endif

#if defined(__cpp_lib_senders)
#include <execution>
#endif

// Preprocessor: `#` at column 0; directives inside conditionals are
//   indented one space after `#` per nesting level.
#if defined(__linux__)
#define ACME_PLATFORM_NAME "linux"
#define ACME_EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#define ACME_PLATFORM_NAME "windows"
#define ACME_EXPORT __declspec(dllexport)
#else
#define ACME_PLATFORM_NAME "unknown"
#define ACME_EXPORT
#endif

// Macros: avoid. Allowed only for (a) include guards/platform
//   detection, (b) feature gating, (c) things the language cannot express
//   (assertion wrappers, logging hooks, export attributes). Always ACME_ prefixed
//   UPPER_CASE, always #undef'd if file-local.
//   Never macro constants, inline functions, or types.
//   Multi-statement macros use do { ... } while (false).
#if defined(__cpp_contracts)
#define ACME_ASSERT(condition) contract_assert(condition)
#else
#define ACME_ASSERT(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) std::terminate();                                                        \
    } while (false)
#endif

// Binary embedding: prefer `#embed` over xxd-generated arrays.
// constexpr unsigned char K_LOGO[] = {
// #embed "logo.png"
// };

// -----------------------------------------------------------------------------
// 2. NAMESPACES
// -----------------------------------------------------------------------------

// Top-level namespace: `acme`. Nested: `acme::net`. Use the C++17 nested form.
// Namespace aliases and using-directives (`using namespace ...`) are not allowed.
//   Using-declarations are allowed at function scope only (e.g. `using std::swap;`
//   to enable ADL).
// Specializations of standard templates for project types (std::formatter,
//   std::hash) go at global scope, after the project namespace is closed
//   (see the end of this file).
// Close comment: `}  // namespace acme::style` (two spaces before //).
namespace acme::style {

// Internal linkage: anonymous namespace (not `static`) for functions
//   and types; `static` only for class members and local statics.
//   Anonymous namespaces belong in .cpp files, never in headers.
namespace {

[[maybe_unused]] constexpr std::size_t K_INTERNAL_BUFFER_SIZE { 256uz };

}  // namespace

// Versioned APIs: `inline namespace v2 { }`. Keep it rare.
inline namespace v1 {

[[nodiscard]] constexpr int api_version() noexcept {
    return 1;
}

}  // namespace v1

// -----------------------------------------------------------------------------
// 3. CONSTANTS, VARIABLES, INITIALIZATION, TYPES, CASTS
// -----------------------------------------------------------------------------

// Constants: `constexpr` by default (global), `inline constexpr` in
//   headers. `constinit` for mutable-but-statically-initialized globals.
//   Avoid non-trivial globals; if unavoidable, use a function-local static.
inline constexpr int K_MAX_RETRIES { 3 };
inline constexpr double K_PI { 3.141592653589793 };
inline constexpr std::string_view K_DEFAULT_NAME { "unnamed" };
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz * 1024uz };
constinit thread_local int g_thread_counter { 0 };  // Mutable globals: g_ prefix.

// Fixed-width types for storage/serialization (std::int32_t);
//   `int` for local arithmetic where width does not matter;
//   `std::size_t` for sizes/indices from the standard library.
//   Avoid mixing signed and unsigned. Use std::ssize / std::cmp_less for
//   cross-sign comparisons. Use the `uz` / `z` literal suffixes.

// Initialization: brace-init `{}` by default (prevents narrowing) for every
//   declaration with a spelled type. `auto` declarations use `=`
//   (`auto it = values.begin();`) because `auto x { y }` reads like a braced list.
//   Parentheses only when a constructor semantic differs
//   (e.g. std::vector<int>(n, v)).
//   Always initialize at the point of declaration.
//   C++26 makes reading an uninitialized automatic an "erroneous behavior";
//   when intentionally leaving a buffer uninitialized, mark [[indeterminate]].
// Demo-only variables carry [[maybe_unused]].
inline void initialization_examples() {
    [[maybe_unused]] int count { 0 };
    [[maybe_unused]] double ratio { 0.5 };
    [[maybe_unused]] std::vector<int> values { 1, 2, 3 };
    [[maybe_unused]] std::vector<int> filled(10, 7);  // (count, value) ctor: parentheses.
    [[maybe_unused]] std::string name { "abc" };
    [[maybe_unused]] std::array<std::byte, 64> scratch [[indeterminate]];  // Intentional.

    // `auto`: use when the type is obvious from the right-hand side
    //   (make_*, casts, iterators, ranges, lambdas, structured bindings) or
    //   is unspellable. Spell the type otherwise. Never `auto` for scalars
    //   returned from non-obvious functions.
    [[maybe_unused]] auto owned = std::make_unique<int>(42);  // Never raw `new`.
    [[maybe_unused]] auto it = values.begin();
    [[maybe_unused]] auto lambda = [](int v) { return v * 2; };
    [[maybe_unused]] const std::size_t total { values.size() };  // Spelled: not obvious.

    // Placeholder `_` for intentionally unused names (C++26).
    [[maybe_unused]] auto [_, inserted] = std::map<int, int> {}.try_emplace(1, 2);
}

inline int sum_values(std::span<const int> values) {
    int sum { 0 };
    for (std::size_t index { 0uz }; index < values.size(); ++index) {
        sum += values[index];
    }
    return sum;
}

// Casts: never C-style `(T)x` or functional `T(x)` for conversions.
//   static_cast for value conversions; std::bit_cast for type punning;
//   reinterpret_cast only at hardware/FFI boundaries, with a comment;
//   const_cast banned unless interfacing with a const-incorrect C API;
//   dynamic_cast only in RTTI-permitting code, checked for null.
//   `auto(x)` / `auto{x}` for explicit decay-copy.
//   Narrowing: use a checked conversion helper, not a bare static_cast.
template <std::integral To, std::integral From>
[[nodiscard]] constexpr To checked_cast(From value) {
    if (!std::in_range<To>(value)) {
        throw std::out_of_range{ "checked_cast: value out of range" };
    }

    return static_cast<To>(value);
}

// Pointers: raw pointers are non-owning observers only; prefer
//   references for required params, std::optional<std::reference_wrapper<T>> or
//   T* for optional params. Always `nullptr`, never NULL/0.
// Booleans: no implicit conversion from pointers/integers in
//   conditions where clarity suffers; `if (ptr != nullptr)` is allowed but
//   `if (ptr)` is the default.

// -----------------------------------------------------------------------------
// 4. ENUMS, AGGREGATES, UNIONS, VARIANTS
// -----------------------------------------------------------------------------

// Enums: always `enum class`, with explicit underlying type.
//   Enumerators PascalCase, no prefix, trailing comma.
//   Bit-flag enums define their operators next to the enum (see Permission).
//   Convert with std::to_underlying; stringify with reflection (section 13)
//   or an explicit switch with no `default:` so the compiler warns on additions.
enum class Color : std::uint8_t {
    Red,
    Green,
    Blue,
};

enum class Permission : std::uint32_t {
    None = 0,
    Read = 1U << 0,
    Write = 1U << 1,
    Execute = 1U << 2,
};

[[nodiscard]] constexpr Permission operator|(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

[[nodiscard]] constexpr bool has_flag(Permission set, Permission flag) noexcept {
    return (std::to_underlying(set) & std::to_underlying(flag)) != 0;
}

[[nodiscard]] constexpr std::string_view to_string(Color color) noexcept {
    switch (color) {  // No default: -Wswitch catches new enumerators.
    case Color::Red:
        return "red";
    case Color::Green:
        return "green";
    case Color::Blue:
        return "blue";
    }

    std::unreachable();
}

// Aggregates ("plain data"): `struct`, public fields, no invariants,
//   no member functions beyond trivial helpers. Use default member
//   initializers and construct with designated initializers.
//   `class` is for anything with an invariant or private state.
struct ServerOptions {
    std::string m_host { "localhost" };
    std::uint16_t m_port { 8080 };
    std::chrono::milliseconds m_timeout { 5000 };
    bool m_use_tls { true };

    friend bool operator==(const ServerOptions&, const ServerOptions&) = default;
};

[[nodiscard]] inline ServerOptions make_default_options() {
    return ServerOptions{ .m_host = "example.org", .m_port = 443, .m_use_tls = true };
}

// Unions: raw `union` banned; use std::variant. Use std::visit with
//   an overload set, or `std::get_if` for single-alternative checks.
struct Circle {
    double m_radius {};
};

struct Square {
    double m_side {};
};

using Shape = std::variant<Circle, Square>;

template <typename... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};

[[nodiscard]] inline double area(const Shape& shape) {
    return std::visit(Overloaded {
                          [](const Circle& c) { return K_PI * c.m_radius * c.m_radius; },
                          [](const Square& s) { return s.m_side * s.m_side; },
                      },
                      shape);
}

// -----------------------------------------------------------------------------
// 5. FUNCTIONS
// -----------------------------------------------------------------------------

/// @brief Parse a port number.
/// @param text Decimal digits, no sign, no whitespace.
/// @return The port, or an error string describing the failure.
// Return style: leading return type for ordinary functions;
//   trailing `-> T` only when the return type depends on parameters
//   (decltype) or for lambdas that need an explicit type.
// Attributes: [[nodiscard]] on every non-void function whose result
//   matters (default: all pure queries and all factories). Put attributes on
//   their own preceding position, same line as the declaration if short.
//   Defaulted comparison operators carry no attribute.
// Fallible functions return std::expected<T, E>. E is a dedicated
//   error enum or small struct, never a bare string in library code (a string
//   is used here to keep the demo short).
[[nodiscard]] inline std::expected<std::uint16_t, std::string> parse_port(std::string_view text) {
    constexpr std::uint32_t K_MAX_PORT { 65535 };

    if (text.empty()) return std::unexpected{ "empty port" };

    std::uint32_t value { 0 };

    for (const char character : text) {
        if (character < '0' || character > '9') {
            return std::unexpected {std::format("invalid character '{}'", character)};
        }

        value = (value * 10) + static_cast<std::uint32_t>(character - '0');

        if (value > K_MAX_PORT) return std::unexpected{ "port out of range" };
    }

    return static_cast<std::uint16_t>(value);
}

// Parameter passing:
//   - Cheap to copy (<= 2 words, trivially copyable): by value.
//   - Read-only, larger: `const T&`.
//   - Strings: `std::string_view`; contiguous ranges: `std::span<const T>`.
//   - Sink (will store/consume): by value then std::move.
//   - Forwarding: `T&&` with std::forward (templates only).
//   - Out params: banned. Return a value, a struct, or std::expected.
//   - In/out (rare): non-const reference, name ends with `_inout` if unclear.
//   - Optional param: std::optional<T> by value, or an overload.
//   - Default arguments allowed only for trailing, cheap, constant values;
//     prefer overloads when behavior differs.
class Widget;  // Forward declaration: enough for references and pointers.

void draw(const Widget& widget);

[[nodiscard]] inline std::string join(std::span<const std::string> parts,
                                      std::string_view separator = ", ") {
    return parts | std::views::join_with(separator) | std::ranges::to<std::string>();
}

// noexcept: mark move ops, swap, destructors (implicit), and
//   trivial accessors `noexcept`. Do NOT mark functions that might throw
//   or call unmarked third-party code. Conditional noexcept via noexcept(expr).
// `= delete("reason")` (C++26) to forbid overloads with a message.
void accepts_only_int(int value);
void accepts_only_int(double) = delete ("implicit double->int conversion is lossy");

// constexpr: mark everything that can be constexpr. `consteval` only
//   for functions that must never run at runtime. `inline` is implicit for
//   constexpr functions and in-class definitions; do not write it redundantly.

// Lambdas: short, local, non-recursive.
//   - Capture explicitly; never `[=]`; `[&]` only for lambdas that do not
//     outlive the full-expression (algorithm calls).
//   - Lambdas stored/returned/escaping: explicit captures, by value or move.
//   - Longer than ~10 lines or reused: make it a named function.
//   - Generic lambdas use `auto&&` + std::forward when forwarding.
//   - Static call operators for stateless lambdas: `[](int x) static { ... }`.
[[nodiscard]] inline int lambda_examples(const std::vector<int>& data) {
    const int threshold { 10 };
    const auto count = std::ranges::count_if(data, [threshold](int v) { return v > threshold; });
    auto square = [](int v) static { return v * v; };

    return static_cast<int>(count) + square(2);
}

// Type-erased callables: std::function_ref (non-owning params),
//   std::move_only_function / std::copyable_function (owning storage).
//   Plain templates (`std::invocable F`) for hot paths.
inline void for_each_value(std::span<const int> data, std::function_ref<void(int)> callback) {
    for (const int value : data) {
        callback(value);
    }
}

// Overload resolution policy: no more than 3 overloads of one name;
//   use differently named functions for semantically different operations.

// Recursion: allowed when depth is bounded and obvious; otherwise
//   iterate. Document the maximum depth.

// Function length: soft limit 40 lines, cyclomatic complexity 10.

// -----------------------------------------------------------------------------
// 6. CLASSES
// -----------------------------------------------------------------------------

/// @brief Fixed-capacity ring buffer. Demonstrates class layout conventions.
// Member order inside a class (each group separated by a blank line,
//   with `public:` / `protected:` / `private:` labels outdented to class level):
//     1. using/typedef aliases
//     2. static constants
//     3. constructors, destructor, assignment (rule of 0/5 grouped together)
//     4. public member functions (queries first, then mutators)
//     5. protected members
//     6. private data members
//     7. private functions
// Rule of 0 by default. If any of the five special members is
//   declared, declare or `= delete` all five. Move operations are noexcept.
// Constructors: `explicit` on all single-argument and all
//   multi-argument constructors that could be brace-init-ed implicitly;
//   conversion constructors are deliberate and commented.
//   Initialize members via default member initializers or the init list,
//   in declaration order.
// Getters: no `get_` prefix: `size()`, `capacity()`. Setters:
//   `set_name(...)` only when an invariant needs enforcement; otherwise the
//   field should be public in an aggregate.
// Contracts: `pre` / `post` specifiers are guarded by __cpp_contracts; where the
//   check must survive without contracts, use ACME_ASSERT in the body (section 8).
template <typename T, std::size_t Capacity>
    requires(Capacity > 0 && std::movable<T> && std::default_initializable<T>)
class RingBuffer {
  public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type K_CAPACITY { Capacity };

    RingBuffer() = default;

    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] constexpr bool full() const noexcept { return m_size == Capacity; }

    [[nodiscard]] constexpr size_type size() const noexcept { return m_size; }

    // Deducing this (C++23) replaces const/non-const accessor pairs.
    //   Use for const/non-const duplication and CRTP replacement.
    [[nodiscard]] constexpr auto&& front(this auto&& self) {
        return std::forward<decltype(self)>(self).m_storage[self.m_head];
    }

    /// @pre !full()
    constexpr void push_back(T value)
#if defined(__cpp_contracts)
        pre(!full())
#endif
    {
        m_storage[(m_head + m_size) % Capacity] = std::move(value);
        ++m_size;
    }

    /// @pre !empty()
    [[nodiscard]] constexpr T pop_front()
#if defined(__cpp_contracts)
        pre(!empty())
#endif
    {
        T value { std::move(m_storage[m_head]) };
        m_head = (m_head + 1) % Capacity;
        --m_size;
        return value;
    }

  private:
    std::array<T, Capacity> m_storage {};
    size_type m_head { 0 };
    size_type m_size { 0 };
};

// Resource-owning class: rule of five spelled out; copy deleted.
//   Definitions live in file_handle.cpp.
class FileHandle final {
  public:
    explicit FileHandle(const std::filesystem::path& path);
    ~FileHandle();

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& other) noexcept;
    FileHandle& operator=(FileHandle&& other) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return m_handle != nullptr; }

  private:
    void* m_handle { nullptr };
};

// Inheritance:
//   - Prefer composition; inheritance for polymorphic interfaces only.
//   - Interfaces: pure-virtual abstract class with a plain PascalCase name
//     (e.g. `Logger`, not `ILogger`), protected default ctor, public virtual
//     dtor (or protected non-virtual if never deleted via base pointer).
//   - Every override: `override` (never repeat `virtual`); leaf classes
//     `final`. Mark non-virtual-by-design classes `final`.
//   - No multiple inheritance of implementations. No protected data.
//   - Value-semantic polymorphism: std::polymorphic<T> / std::indirect<T>
//     instead of raw owning pointers to a base.
//   - Virtual calls in ctor/dtor are banned.
class Logger {
  public:
    virtual ~Logger() = default;

    virtual void log(std::string_view message) = 0;

  protected:
    Logger() = default;
    Logger(const Logger&) = default;
    Logger& operator=(const Logger&) = default;
};

class StdoutLogger final : public Logger {
  public:
    void log(std::string_view message) override { std::println("{}", message); }
};

// Operators:
//   - Comparison: define `operator==` and `operator<=>` as `= default` when
//     memberwise is right; otherwise define <=> and ==. Make them friends
//     or non-members for symmetry.
//   - Arithmetic operators as non-member (hidden friend), mutating forms
//     (+=) as members. Never overload && || , or unary &.
//   - Stream/format support via std::formatter, not operator<<
//     (see the std::formatter<Version> specialization at the end of this file).
//   - User-defined literals: suffixes start with `_` and live in an
//     inline `literals` namespace.
struct Version {
    int m_major {};
    int m_minor {};
    int m_patch {};

    friend auto operator<=>(const Version&, const Version&) = default;
};

inline namespace literals {

[[nodiscard]] constexpr Version operator""_v(unsigned long long major_version) noexcept {
    return Version{ .m_major = static_cast<int>(major_version) };
}

}  // namespace literals

// `friend`: hidden friends for operators/swap; variadic friends
//   (C++26) allowed for policy-based designs; otherwise avoid.
// PIMPL: use `std::unique_ptr<Impl>` with out-of-line special
//   members, only when ABI stability or compile time demands it.
// `mutable`: only for caches/mutexes; comment why.
// Static members: `static constexpr` for constants,
//   `inline static` for mutable class state (discouraged).
// Friends of tests are banned; test through the public API.

// -----------------------------------------------------------------------------
// 7. TEMPLATES, CONCEPTS, CONSTRAINTS, VARIADICS, SPECIALIZATION
// -----------------------------------------------------------------------------

// Constrain every template. Concepts, not SFINAE / enable_if.
//   Concept names PascalCase. Prefer standard concepts first.
//   Constraint placement: type-constraint syntax for single simple
//   constraints (`std::integral T`), trailing `requires` for compound ones.
// Template declaration spacing: `template <typename T>` on its own
//   line, `requires` clause on its own line.
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <typename T>
concept Serializable = requires(const T& value, std::string& out) {
    { value.serialize(out) } -> std::same_as<void>;
};

template <Numeric T> [[nodiscard]] constexpr T clamp_to_unit(T value) noexcept {
    return std::clamp(value, T{ 0 }, T{ 1 });
}

template <typename Range, typename Predicate>
    requires std::ranges::input_range<Range> &&
             std::predicate<Predicate, std::ranges::range_reference_t<Range>>
[[nodiscard]] auto count_matching(Range&& range, Predicate predicate) {
    return std::ranges::count_if(std::forward<Range>(range), std::move(predicate));
}

// `typename` (not `class`) for type parameters.
// Variadics: fold expressions over recursion; pack indexing (C++26)
//   over std::tuple_element when selecting by index.
template <typename... Ts> [[nodiscard]] constexpr auto sum_all(Ts... values) {
    return (values + ... + 0);
}

#if defined(__cpp_pack_indexing)
template <typename... Ts>
    requires(sizeof...(Ts) > 0)
[[nodiscard]] constexpr auto&& first_of(Ts&&... values) {
    return values...[0];
}
#endif

// Non-type template params: `auto` NTTP; class-type NTTP allowed
//   for structural types (e.g. fixed strings).
template <auto Value> struct Constant {
    static constexpr auto value = Value;
};

// Specialization: prefer overloads and `if constexpr` to full
//   specialization; if specializing, use partial specialization of a trait
//   struct, and variable template `_v` / alias template `_t` helpers.
template <typename T> struct IsSmallType : std::bool_constant<(sizeof(T) <= 2 * sizeof(void*))> { };

template <typename T> inline constexpr bool is_small_type_v { IsSmallType<T>::value };

// `if constexpr` for type-dependent branches (not tag dispatch).
template <typename T> [[nodiscard]] std::string describe(const T& value) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::format("number({})", value);
    } else if constexpr (std::convertible_to<const T&, std::string_view>) {
        return std::format("text({})", std::string_view{ value });
    } else {
        return "opaque";
    }
}

// CTAD: allowed; add explicit deduction guides when the implicit
//   ones are surprising. Never rely on CTAD for `std::vector{a, b}` where a
//   single-element ambiguity is possible; spell the type.
// `static_assert` with a message, user-generated messages allowed:
static_assert(sizeof(std::uint32_t) == 4, "std::uint32_t must be 4 bytes");
static_assert(std::is_trivially_copyable_v<Version>, "Version must stay trivially copyable");
// static_assert(cond, std::format("size is {}", sizeof(T)));  // C++26

// Template definitions live in headers (or .ipp included at the
//   bottom). Explicit instantiation (`extern template`) only for proven
//   compile-time hot spots.

// -----------------------------------------------------------------------------
// 8. ERROR HANDLING
// -----------------------------------------------------------------------------

// Error-handling strategy (choose per layer and write it down):
//   - Programmer errors (violated preconditions): contracts (pre/post/
//     contract_assert) where supported; otherwise `ACME_ASSERT`.
//     Never throw for a bug.
//   - Expected, recoverable runtime failures (I/O, parse, network):
//     std::expected<T, E>.
//   - Exceptional/unrecoverable-at-this-level failures in constructors or
//     deep call stacks: exceptions derived from std::exception.
//   - Exceptions are allowed. If your project disables them (-fno-exceptions),
//     replace this bullet and the code below.
//   - Never use error codes via globals (errno) except at C boundaries.
//   - Never `catch (...)` without rethrow or a logged top-level boundary.
//   - Catch by const reference. Throw by value. No exception specs.
//   - Destructors never throw.
class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @pre value != INT_MIN
/// @post result >= 0
[[nodiscard]] inline int absolute_value(int value)
#if defined(__cpp_contracts)
    pre(value != std::numeric_limits<int>::min()) post(result : result >= 0)
#endif
{
    return value < 0 ? -value : value;
}

// ACME_ASSERT expands to contract_assert when contracts are available.
inline void assert_example(int index, std::size_t size) {
    ACME_ASSERT(index >= 0 && static_cast<std::size_t>(index) < size);
}

// Chaining expected: use .and_then / .transform / .or_else rather
//   than manual if-ladders when 2+ steps.
[[nodiscard]] inline std::expected<std::string, std::string> describe_port(std::string_view text) {
    return parse_port(text).transform(
        [](std::uint16_t port) { return std::format("port {}", port); });
}

// Logging/diagnostics: via project logger; never std::cout in
//   library code. Use std::print/std::println for CLI output.
// Assertions: `assert` is banned in favor of contract_assert /
//   ACME_ASSERT because NDEBUG semantics are unreliable.
// Hardened standard library mode: enabled in release builds.

// -----------------------------------------------------------------------------
// 9. OWNERSHIP, MEMORY, ALLOCATORS, RAII
// -----------------------------------------------------------------------------

// Ownership rules:
//   - Exactly one owner: std::unique_ptr (default) or value/member.
//   - Shared ownership: std::shared_ptr only with justification
//     (comment why); prefer std::make_shared; never pass by value unless
//     sharing ownership; never use enable_shared_from_this casually.
//   - weak_ptr for non-owning observers of shared data.
//   - Raw `new`/`delete`/`malloc`/`free`: banned outside allocators.
//   - Stack allocation by default; heap only for size/lifetime reasons.
//   - Factory functions return std::unique_ptr or value, never raw owners.
//   - Value types preferred: std::optional over nullable pointers for
//     "maybe a value"; std::inplace_vector for bounded no-alloc vectors.
// std::pmr: allowed in performance-sensitive subsystems;
//   pass `std::pmr::memory_resource*` as the last constructor parameter.
[[nodiscard]] inline int ownership_examples() {
    auto owned = std::make_unique<int>(42);
    std::pmr::monotonic_buffer_resource arena {};
    std::pmr::vector<int> pooled { &arena };
    pooled.push_back(*owned);

    std::optional<int> maybe { std::nullopt };

    if (!pooled.empty()) maybe = pooled.front();

    return maybe.value_or(0);
}

// RAII everywhere: locks, files, sockets, transactions.
//   Scope guards: a small `ScopeExit` utility; no goto-cleanup.
//   Use std::lock_guard / std::scoped_lock, never manual lock()/unlock().
// Lifetime pitfalls: never return string_view/span to a local or
//   temporary; never store string_view as a member without documenting the
//   owner's lifetime.

// -----------------------------------------------------------------------------
// 10. CONTAINERS, RANGES, ALGORITHMS, STRINGS, FORMATTING, I/O
// -----------------------------------------------------------------------------

// Container defaults: std::vector first; std::array for fixed size;
//   std::flat_map/flat_set for small, read-mostly maps; std::unordered_map
//   when hashing is cheap and iteration order is irrelevant; std::map only
//   when ordering matters; std::hive for stable-pointer high-churn sets;
//   std::mdspan for multi-dimensional views. Always reserve() when the final
//   size is known. Avoid std::list / std::deque unless required.
// Ranges and views over raw loops/iterator pairs for transformation
//   pipelines. Constrained algorithms (std::ranges::) over legacy ones.
//   Pipelines longer than ~4 stages go into named intermediate variables.
[[nodiscard]] inline std::vector<int> ranges_example(const std::vector<int>& input) {
    auto even_squares = input | std::views::filter([](int v) { return v % 2 == 0; }) |
                        std::views::transform([](int v) { return v * v; });

    return std::ranges::to<std::vector>(even_squares);
}

// Strings: std::string_view for non-owning params; std::string for
//   ownership; no C strings or char arrays except at C boundaries.
//   Formatting via std::format / std::print; no printf, no iostream
//   formatting. String literals: "..." (UTF-8 by default); use u8"" only
//   when explicit encoding matters. Raw strings R"(...)" for regex/JSON.
inline void string_examples(std::string_view name) {
    std::string greeting {std::format("hello, {}!", name)};
    constexpr auto K_TIMEOUT = std::chrono::milliseconds{ 250 };

    std::println("{} (timeout {} ms)", greeting, K_TIMEOUT.count());
}

// I/O: <print> and <format> for text; <fstream> / std::filesystem
//   for files; binary I/O via std::span<const std::byte>.
// Time: std::chrono only, strong types in APIs
//   (`std::chrono::milliseconds`, not `int timeout_ms`).
// Randomness: <random> engines (std::mt19937_64 seeded from
//   std::random_device) or std::philox; never rand().
// SIMD: std::simd for portable vectorization; intrinsics only
//   behind an abstraction.
// Linear algebra: std::linalg over hand-written kernels where
//   available.
// Generators: std::generator for lazy sequences.
[[nodiscard]] inline std::generator<int> iota_generator(int count) {
    for (int i { 0 }; i < count; ++i) {
        co_yield i;
    }
}

// -----------------------------------------------------------------------------
// 11. CONTROL FLOW
// -----------------------------------------------------------------------------

// Conditions: braces per the rule in section 0; `else` on the same line as
//   the closing brace; early return preferred to deep nesting; init-statement
//   in if/switch to limit variable scope; no assignment in conditions.
//   Keep logical operators at the end of a continued condition line:
//   if (!first_asset_present ||
//       !second_asset_present ||
//       !third_asset_present) {
//       return false;
//   }
// switch: braces around each case that declares variables;
//   `[[fallthrough]];` for intentional fallthrough; no `default:` over enums
//   (see section 4); `default:` otherwise, with explicit handling.
// Loops: range-for over index loops; structured bindings when
//   iterating maps; `std::views::enumerate` / `std::views::zip` rather than manual
//   counters; `for (;;)` for intentional infinite loops; no `goto`, no
//   `do...while` except in macros; avoid `break`/`continue` in nested loops by
//   extracting a function.
// Ternary: one level only, no side effects.
// Boolean expression wrapping: break before `&&` / `||`.
[[nodiscard]] inline std::string control_flow_examples(const std::map<std::string, int>& scores) {
    switch (const auto count = scores.size(); count) {
    case 0:
        return "no scores";
    case 1: {
        const auto& [name, score] = *scores.begin();

        return std::format("{}: {}", name, score);
    }
    default:
        break;
    }

    std::string out;

    for (const auto& [name, score] : scores) {
        if (const auto bonus = score / 10; bonus > 5 && score < 100) {
            out += std::format("{}: {} (+{})\n", name, score, bonus);
        } else if (score == 0) {
            continue;
        } else {
            out += std::format("{}: {}\n", name, score);
        }
    }

    return out;
}

// `if consteval` for compile-time vs run-time branches (always braced).
constexpr int compile_time_or_runtime(int x) {
    if consteval {
        return x * 2;
    } else {
        return x + x;
    }
}

// -----------------------------------------------------------------------------
// 12. CONCURRENCY, COROUTINES, std::execution
// -----------------------------------------------------------------------------

// Threads: std::jthread + std::stop_token (never detach, never raw
//   std::thread unless joined in a guard). Prefer tasks (std::execution /
//   async frameworks) over manual thread management.
// Shared state: protect with a mutex that lives next to the data;
//   lock via std::scoped_lock; document which mutex guards which field
//   (`// GUARDED_BY(m_mutex)`). Lock ordering documented; hold locks for the
//   shortest time; never call unknown code under a lock.
// Atomics: default memory_order_seq_cst (implicit); weaker orderings must be
//   written explicitly with a comment proving correctness. Atomics only for
//   single flags/counters; complex lock-free structures need review.
// Immutable data shared across threads: std::shared_ptr<const T> or
//   RCU / hazard pointers (<rcu>, <hazard_pointer>) in expert code.
class Counter {
  public:
    [[nodiscard]] int value() const {
        std::scoped_lock _ { m_mutex };

        return m_value;
    }

    void increment() {
        std::scoped_lock _ { m_mutex };  // `_` placeholder: lock held to scope end.
        ++m_value;
    }

  private:
    mutable std::mutex m_mutex;  // Mutable: locked inside const member functions.
    int m_value { 0 };             // GUARDED_BY(m_mutex)
};

class StopFlag {
  public:
    [[nodiscard]] bool requested() const noexcept { return m_requested.load(); }

    void request() noexcept { m_requested.store(true); }

  private:
    std::atomic<bool> m_requested { false };
};

inline void jthread_example() {
    auto task = [](std::stop_token token) {
        while (!token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        }
    };

    std::jthread worker { task };
    worker.request_stop();
}

// Coroutines: return std::generator for lazy sequences; custom task
//   types live in a dedicated header with documented lifetime rules.
//   Never capture references in coroutine lambdas; copy into the coroutine
//   frame. Coroutine parameters are by value unless the lifetime is proven.
// Structured async: std::execution senders/receivers.
#if defined(__cpp_lib_senders)
[[nodiscard]] inline int execution_example() {
    auto work = std::execution::just(20) | std::execution::then([](int v) { return v + 22; });
    auto [result] = std::this_thread::sync_wait(std::move(work)).value();

    return result;
}
#endif

// -----------------------------------------------------------------------------
// 13. COMPILE-TIME: CONSTEXPR, CONSTEVAL, REFLECTION, EXPANSION STATEMENTS
// -----------------------------------------------------------------------------

// Prefer constexpr/consteval computation over macros and
//   code generation. constexpr containers/strings/exceptions allowed.
//   `constinit` for static initialization guarantees.
consteval std::size_t compile_time_only(std::size_t n) {
    return n * n;
}

static_assert(compile_time_only(4) == 16);

// Reflection (C++26, P2996): use for enum<->string, serialization,
//   and boilerplate removal. Wrap every use in feature-test macros; put
//   reflection helpers in `acme::meta` so the surface stays obvious.
//   Splice syntax: `[: r :]`; reflect operator: `^^`.
//   Reflection code gets extra comments: what is reflected and why.
#if defined(__cpp_impl_reflection)
template <typename E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr std::string_view enum_name_of(E value) {
    // Walk the enumerators of E at compile time and match the runtime value.
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
        if (value == [:e:]) return std::meta::identifier_of(e);
    }

    return "<unknown>";
}
#endif

// Expansion statements (`template for`, C++26): preferred over
//   recursive templates or index_sequence tricks for iterating packs/tuples.
//   Always guard with __cpp_expansion_statements; fall back to a fold expression.
template <typename... Ts> inline void print_all(const Ts&... values) {
#if defined(__cpp_expansion_statements)
    template for (const auto& v : std::tie(values...)) {
        std::println("{}", v);
    }
#else
    (std::println("{}", values), ...);
#endif
}

// Structured binding packs (C++26): `auto [first, ...rest] = tuple;`
//   allowed in templates.

// -----------------------------------------------------------------------------
// 14. INTEROP, PLATFORM CODE, LOW-LEVEL
// -----------------------------------------------------------------------------

// C interop: `extern "C"` blocks in a dedicated header; C-visible
//   names use project_prefix_snake_case; ownership documented per function;
//   no exceptions cross the boundary (catch and convert to error codes).
extern "C" {

ACME_EXPORT int acme_style_version(void);
}

// Bit manipulation: <bit> (std::popcount, std::countl_zero,
//   std::bit_cast, std::byteswap, std::endian), never hand-rolled loops.
[[nodiscard]] constexpr std::uint32_t to_big_endian(std::uint32_t value) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(value);
    } else {
        return value;
    }
}

// Type punning: std::bit_cast; raw memory: std::start_lifetime_as
//   (C++23) over reinterpret_cast when treating bytes as objects.
//   Union punning and pointer-cast punning banned.
// Undefined behavior policy: zero tolerance. Build with
//   -Wall -Wextra -Wpedantic -Wconversion -Werror, plus ASan/UBSan/TSan in CI.
//   Compiler hints [[assume(expr)]] and std::unreachable() require a comment
//   with the proof and a debug-mode check.
// Alignment/size: static_assert on layouts crossing ABI boundaries;
//   use alignas explicitly; `[[no_unique_address]]` for empty-base
//   optimization of members.
// Compiler-specific code: isolated in `platform/` headers behind
//   feature-test macros; `__builtin_*` banned outside those headers.
// Deprecation: `[[deprecated("use X instead")]]` with a removal
//   version in the message.
[[deprecated("use parse_port(); removal planned for v3.0")]]
int legacy_parse_port(const char* text);

// `volatile`: only for memory-mapped I/O; never for threading.
// `register`, `auto_ptr`, `std::bind`, `std::rand`, `<codecvt>`,
//   `std::unary_function` and other removed/deprecated features: banned.
// Exceptions across threads / DLL boundaries: banned.

}  // namespace acme::style

// -----------------------------------------------------------------------------
// STANDARD TEMPLATE SPECIALIZATIONS (global scope, after the project namespace)
// -----------------------------------------------------------------------------

template <> struct std::formatter<acme::style::Version> {
    constexpr auto parse(std::format_parse_context& context) {
        auto it = context.begin();

        if (it != context.end() && *it != '}') {
            throw std::format_error{ "invalid format spec for Version" };
        }

        return it;
    }

    auto format(const acme::style::Version& version, std::format_context& context) const {
        return std::format_to(
            context.out(), "{}.{}.{}", version.m_major, version.m_minor, version.m_patch);
    }
};

// -----------------------------------------------------------------------------
// 15. TESTS
// -----------------------------------------------------------------------------
// Framework: GoogleTest / Catch2 / doctest (pick one).
//   File name: <unit>_test.cpp next to the unit or in tests/.
//   Test names: `SuiteName.DescribesBehavior_WhenCondition` or
//   `TEST(RingBuffer, PopFront_WhenEmpty_ViolatesContract)`.
//   Structure: Arrange / Act / Assert, one logical assertion per test;
//   no logic (loops/conditionals) in tests; no shared mutable fixtures;
//   deterministic (seed RNGs, fake clocks); tests are never `friend`s.
//   Mock only at architectural boundaries.
//   Compile-time behavior is tested with `static_assert` in the same file.
//
//   Example (pseudocode, framework-specific):
//   TEST(ParsePort, ReturnsError_WhenInputIsEmpty) {
//       const auto result = acme::style::parse_port("");
//       ASSERT_FALSE(result.has_value());
//   }

// -----------------------------------------------------------------------------
// ENTRY POINT
// -----------------------------------------------------------------------------
// `main` returns int, no `void main`, no args unless needed;
//   wrap body in try/catch at the top level and map errors to exit codes.
int main() {
    try {
        std::println("style reference compiled: platform={}", ACME_PLATFORM_NAME);
        return 0;
    } catch (const std::exception& error) {
        std::println(stderr, "fatal: {}", error.what());
        return 1;
    }
}
