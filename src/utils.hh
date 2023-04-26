#ifndef LTS_HIGHLIGHT_CODE_UTILS_HH
#define LTS_HIGHLIGHT_CODE_UTILS_HH

#include <fmt/format.h>

using namespace std::literals;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usz = size_t;
using uptr = uintptr_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using isz = ptrdiff_t;
using iptr = intptr_t;

#define STR_(X) #X
#define STR(X) STR_(X)

#define CAT_(X, Y) X##Y
#define CAT(X, Y) CAT_(X, Y)

namespace rgs = std::ranges;

template <typename ...arguments>
[[noreturn]] void die(fmt::format_string<arguments...> fmt, arguments&& ...args) {
    fmt::print(stderr, fmt, std::forward<arguments>(args)...);
    fmt::print(stderr, "\n");
    std::exit(1);
}

#define defer auto CAT($$defer_type_, __COUNTER__) = $$defer_helper{} & [&]

template <typename T>
struct $$defer_type {
    T func;
    ~$$defer_type() {
        func();
    }
};

struct $$defer_helper {
    template <typename T>
    $$defer_type<T> operator&(T&& func) {
        return {std::forward<T>(func)};
    }
};

#endif // LTS_HIGHLIGHT_CODE_UTILS_HH
