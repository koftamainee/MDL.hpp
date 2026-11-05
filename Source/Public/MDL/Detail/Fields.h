#pragma once

#include <MDL/Detail/Names.h>
#include <MDL/Detail/Tie.h>
#include <MDL/Types.h>

#include <array>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


// Compile-time reflection of aggregates in plain C++23:
//   - field count: probing aggregate initialization with "convert to anything"
//   - field access: structured bindings (Tie.h)
//   - field names: pretty function name of a pointer to the field of an
//     extern object that is never defined (only its address is taken)
namespace MDL::Detail {

template <class T>
struct IsStdArray : std::false_type {};

template <class T, usize N>
struct IsStdArray<std::array<T, N>> : std::true_type {};

template <class T>
concept Aggregate = std::is_class_v<T> && std::is_aggregate_v<T> && !IsStdArray<T>::value;


struct AnyField final {
    template <class T>
    operator T &() const && noexcept; // NOLINT(google-explicit-constructor)

    template <class T>
    operator T &&() const && noexcept; // NOLINT(google-explicit-constructor)
};

template <class T, usize... I>
consteval bool ConstructibleWith(std::index_sequence<I...>) {
    return requires { T {(void(I), AnyField {})...}; };
}

template <class T, usize N = 0>
consteval usize CountFields() {
    if constexpr (N > MaxReflectedFields || !ConstructibleWith<T>(std::make_index_sequence<N + 1> {})) {
        return N;
    } else {
        return CountFields<T, N + 1>();
    }
}

template <Aggregate T>
inline constexpr usize FieldCount = CountFields<T>();


template <class T>
constexpr auto TieFields(T &obj) {
    return Tie<FieldCount<std::remove_cv_t<T>>>(obj);
}

template <class T, usize I>
using FieldType = std::remove_cvref_t<std::tuple_element_t<I, decltype(TieFields(std::declval<T &>()))>>;


#if defined(_MSC_VER) && !defined(__clang__)

// MSVC: an extern object that is never defined, only its address is used.
template <class T>
extern const T FakeInstance;

template <class T>
constexpr const T &FakeObject() {
    return FakeInstance<T>;
}

#else

// clang/gcc reject an undefined extern object of a type without linkage
// (anonymous namespace, local types), so use an inactive union member of a
// constexpr object instead. T does not have to be default constructible or a
// literal type.
template <class T>
struct FakeStorage final {
    union {
        T    Value;
        char Dummy;
    };

    constexpr FakeStorage() : Dummy() {}
    constexpr ~FakeStorage() {} // NOLINT(modernize-use-equals-default)
};

template <class T>
inline constexpr FakeStorage<T> FakeInstance {};

template <class T>
constexpr const T &FakeObject() {
    return FakeInstance<T>.Value;
}

#endif

template <class T>
struct FieldPtr final {
    const T *Ptr;
};

template <class T, usize I>
consteval auto MakeFieldPtr() {
    auto  fields = TieFields(FakeObject<T>());
    auto &field = std::get<I>(fields);
    return FieldPtr<std::remove_cvref_t<decltype(field)>> {&field};
}

struct FieldProbe final {
    int MdlFieldProbeValue;
};

inline constexpr usize FieldSuffix = SuffixLength(PrettyName<MakeFieldPtr<FieldProbe, 0>()>(), "MdlFieldProbeValue");

template <class T, usize I>
consteval std::string_view RawFieldName() {
    return ExtractName(PrettyName<MakeFieldPtr<T, I>()>(), FieldSuffix);
}

template <class T, usize I>
struct FieldKeyStorage final {
    static constexpr std::string_view Raw = RawFieldName<T, I>();
    static constexpr auto             Value = MakeConverted<ConvertedSize(Raw, NamingOf<T>())>(Raw, NamingOf<T>());
};

template <class T, usize... I>
consteval auto MakeFieldKeys(std::index_sequence<I...>) {
    return std::array<std::string_view, sizeof...(I)> {FieldKeyStorage<T, I>::Value.View()...};
}

// Keys of T's fields in the file, after naming conversion.
template <Aggregate T>
inline constexpr auto FieldKeys = MakeFieldKeys<T>(std::make_index_sequence<FieldCount<T>> {});

} // namespace MDL::Detail
