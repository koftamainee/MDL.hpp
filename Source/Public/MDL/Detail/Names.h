#pragma once

#include <MDL/Config.h>
#include <MDL/Types.h>

#include <array>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>


namespace MDL {

enum class Naming : u8 {
    AsIs,
    SnakeCase, // DepthTest -> depth_test, HTTPServer -> http_server
};

// Customization point, specialize for your types. Every member is optional:
//
//   template <>
//   struct MDL::Reflect<MyEnum> {
//       static constexpr MDL::Naming Names = MDL::Naming::AsIs;
//       static constexpr MDL::i64 EnumMin = 0;    // enums only
//       static constexpr MDL::i64 EnumMax = 300;  // enums only
//   };
template <class T>
struct Reflect {};

namespace Detail {

#if defined(__clang__) || defined(__GNUC__)
#define MDL_PRETTY_FUNCTION __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define MDL_PRETTY_FUNCTION __FUNCSIG__
#else
#error "MDL: unsupported compiler"
#endif

template <auto V>
consteval std::string_view PrettyName() {
    return MDL_PRETTY_FUNCTION;
}

constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }
constexpr bool IsLower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool IsIdentChar(char c) { return IsDigit(c) || IsLower(c) || IsUpper(c) || c == '_'; }


// Everything after the name inside the pretty function string does not
// depend on the reflected type, so it is measured once on a probe.
consteval usize SuffixLength(std::string_view pretty, std::string_view probe) {
    return pretty.size() - (pretty.rfind(probe) + probe.size());
}

consteval std::string_view ExtractName(std::string_view pretty, usize suffix) {
    usize end = pretty.size() - suffix;
    usize begin = end;
    while (begin > 0 && IsIdentChar(pretty[begin - 1])) {
        --begin;
    }
    return pretty.substr(begin, end - begin);
}


template <class T>
consteval Naming NamingOf() {
    if constexpr (requires { Reflect<T>::Names; }) {
        return Reflect<T>::Names;
    } else {
        return MDL_SNAKE_CASE_NAMES ? Naming::SnakeCase : Naming::AsIs;
    }
}


// Calls emit(char) for every output character.
template <class Emit>
constexpr void ConvertName(std::string_view name, Naming naming, Emit &&emit) {
    if (naming == Naming::AsIs) {
        for (char c : name) {
            emit(c);
        }
        return;
    }

    for (usize i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (IsUpper(c)) {
            if (i > 0) {
                char prev = name[i - 1];
                bool nextLower = i + 1 < name.size() && IsLower(name[i + 1]);
                if (IsLower(prev) || (IsUpper(prev) && nextLower)) {
                    emit('_');
                }
            }
            emit(static_cast<char>(c - 'A' + 'a'));
        } else {
            emit(c);
        }
    }
}

consteval usize ConvertedSize(std::string_view name, Naming naming) {
    usize size = 0;
    ConvertName(name, naming, [&](char) { ++size; });
    return size;
}

template <usize N>
struct FixedString final {
    std::array<char, N + 1> Data {};

    constexpr std::string_view View() const { return {Data.data(), N}; }
};

template <usize N>
consteval FixedString<N> MakeConverted(std::string_view name, Naming naming) {
    FixedString<N> result;
    usize          i = 0;
    ConvertName(name, naming, [&](char c) { result.Data[i++] = c; });
    return result;
}


// ---------------------------------------------------------------------------
// enums

enum class EnumProbe : u8 {
    MdlEnumProbeValue
};

inline constexpr usize EnumSuffix = SuffixLength(PrettyName<EnumProbe::MdlEnumProbeValue>(), "MdlEnumProbeValue");

// Empty if V is not a named enumerator. Invalid values are printed as a cast,
// "(Ns::E)5" on clang/gcc and "(enum Ns::E)0x5" on MSVC.
template <auto V>
consteval std::string_view RawEnumName() {
    std::string_view pretty = PrettyName<V>();
    std::string_view name = ExtractName(pretty, EnumSuffix);
    if (name.empty() || IsDigit(name.front())) {
        return {};
    }
    usize begin = pretty.size() - EnumSuffix - name.size();
    if (begin > 0 && pretty[begin - 1] == ')') {
        return {};
    }
    return name;
}

template <class E>
consteval i64 EnumMin() {
    using U = std::underlying_type_t<E>;
    i64 value = MDL_ENUM_MIN;
    if constexpr (requires { Reflect<E>::EnumMin; }) {
        value = Reflect<E>::EnumMin;
    }
    if constexpr (std::is_unsigned_v<U>) {
        return value < 0 ? 0 : value;
    } else {
        constexpr i64 lowest = static_cast<i64>(std::numeric_limits<U>::min());
        return value < lowest ? lowest : value;
    }
}

template <class E>
consteval i64 EnumMax() {
    using U = std::underlying_type_t<E>;
    i64 value = MDL_ENUM_MAX;
    if constexpr (requires { Reflect<E>::EnumMax; }) {
        value = Reflect<E>::EnumMax;
    }
    constexpr u64 highest = static_cast<u64>(std::numeric_limits<U>::max());
    return static_cast<u64>(value) > highest ? static_cast<i64>(highest) : value;
}

template <class E, i64 Min, usize... I>
consteval auto EnumValidMask(std::index_sequence<I...>) {
    using U = std::underlying_type_t<E>;
    return std::array<bool, sizeof...(I)> {
        !RawEnumName<static_cast<E>(static_cast<U>(Min + static_cast<i64>(I)))>().empty()...};
}

template <class E>
inline constexpr auto EnumMask =
    EnumValidMask<E, EnumMin<E>()>(std::make_index_sequence<static_cast<usize>(EnumMax<E>() - EnumMin<E>() + 1)> {});

template <class E>
consteval usize EnumCount() {
    usize count = 0;
    for (bool valid : EnumMask<E>) {
        count += valid ? 1 : 0;
    }
    return count;
}

template <class E>
inline constexpr auto EnumValues = [] {
    using U = std::underlying_type_t<E>;
    std::array<E, EnumCount<E>()> values {};
    usize                         n = 0;
    for (usize i = 0; i < EnumMask<E>.size(); ++i) {
        if (EnumMask<E>[i]) {
            values[n++] = static_cast<E>(static_cast<U>(EnumMin<E>() + static_cast<i64>(i)));
        }
    }
    return values;
}();

template <class E, usize I>
struct EnumNameStorage final {
    static constexpr std::string_view Raw = RawEnumName<EnumValues<E>[I]>();
    static constexpr auto             Value = MakeConverted<ConvertedSize(Raw, NamingOf<E>())>(Raw, NamingOf<E>());
};

template <class E, usize... I>
consteval auto MakeEnumNames(std::index_sequence<I...>) {
    return std::array<std::string_view, sizeof...(I)> {EnumNameStorage<E, I>::Value.View()...};
}

template <class E>
inline constexpr auto EnumNames = MakeEnumNames<E>(std::make_index_sequence<EnumCount<E>()> {});

} // namespace Detail


// Name of an enumerator after naming conversion, empty if the value has no name.
template <class E>
    requires std::is_enum_v<E>
constexpr std::string_view EnumToName(E value) {
    constexpr auto &values = Detail::EnumValues<E>;
    for (usize i = 0; i < values.size(); ++i) {
        if (values[i] == value) {
            return Detail::EnumNames<E>[i];
        }
    }
    return {};
}

template <class E>
    requires std::is_enum_v<E>
constexpr bool EnumFromName(std::string_view name, E &out) {
    constexpr auto &names = Detail::EnumNames<E>;
    for (usize i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            out = Detail::EnumValues<E>[i];
            return true;
        }
    }
    return false;
}

} // namespace MDL
