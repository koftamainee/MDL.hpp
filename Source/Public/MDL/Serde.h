#pragma once

#include <MDL/Detail/Fields.h>
#include <MDL/Detail/Names.h>
#include <MDL/Document.h>
#include <MDL/Error.h>
#include <MDL/Value.h>
#include <MDL/Writer.h>

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>


namespace MDL {

enum class UnknownKeys : u8 {
    Error, // a key that matches no field is an error (catches typos)
    Ignore,
};

enum class MissingKeys : u8 {
    KeepDefault, // field keeps its default member initializer
    Error,       // every non-optional field must be present
};

struct ReadOptions final {
    UnknownKeys Unknown = UnknownKeys::Error;
    MissingKeys Missing = MissingKeys::KeepDefault;
};


// Serialization customization point. Specialize it for your own types:
//
//   template <>
//   struct MDL::Serde<glm::vec3> {
//       static std::expected<void, MDL::Error> Read(MDL::Value v, glm::vec3 &out, const MDL::ReadOptions &) {
//           ...
//       }
//       static void Write(MDL::Writer &w, const glm::vec3 &v) {
//           w.BeginList(MDL::Layout::Inline).Item(v.x).Item(v.y).Item(v.z).EndList();
//       }
//   };
//
// Optional members:
//   static bool ShouldWrite(const T &)   - field is skipped when false
//   static constexpr bool Flat = true;   - scalar-like, lets containers of it stay on one line
template <class T>
struct Serde;


namespace Detail {

template <class T>
struct IsBasicString : std::false_type {};

template <class Traits, class Alloc>
struct IsBasicString<std::basic_string<char, Traits, Alloc>> : std::true_type {};

template <class T>
concept StringLike = IsBasicString<T>::value;

template <class T>
concept ListLike = !StringLike<T> && requires(T &t, const T &ct) {
    typename T::value_type;
    t.emplace_back();
    t.clear();
    ct.begin();
    ct.end();
    ct.size();
};

template <class T>
concept MapLike = requires(T &t, const T &ct, std::string_view key) {
    typename T::key_type;
    typename T::mapped_type;
    t.try_emplace(typename T::key_type(key));
    ct.begin();
    ct.end();
} && std::is_constructible_v<std::string_view, const typename T::key_type &>;

template <class T>
struct IsOptional : std::false_type {};

template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

// Scalar-like types: their containers stay on one line.
template <class T>
consteval bool IsFlat() {
    if constexpr (requires { Serde<T>::Flat; }) {
        return Serde<T>::Flat;
    } else if constexpr (IsOptional<T>::value) {
        return IsFlat<typename T::value_type>();
    } else {
        return std::is_arithmetic_v<T> || std::is_enum_v<T> || StringLike<T> || std::is_same_v<T, std::string_view>;
    }
}

template <class T, usize... I>
consteval bool AllFieldsFlat(std::index_sequence<I...>) {
    return (IsFlat<FieldType<T, I>>() && ...);
}

template <class T>
consteval bool IsFlatObject() {
    if constexpr (Aggregate<T>) {
        return AllFieldsFlat<T>(std::make_index_sequence<FieldCount<T>> {});
    } else {
        return false;
    }
}

template <class T>
bool ShouldWrite(const T &value) {
    if constexpr (requires { Serde<T>::ShouldWrite(value); }) {
        return Serde<T>::ShouldWrite(value);
    } else {
        return true;
    }
}

inline std::unexpected<Error> Fail(const Value &value, ErrorCode code, std::string_view context = {}) {
    Error error = value.MakeError(code);
    error.Context = context;
    return std::unexpected(error);
}

} // namespace Detail


// ---------------------------------------------------------------------------
// entry points

template <class T>
std::expected<void, Error> ReadInto(Value value, T &out, const ReadOptions &options = {}) {
    return Serde<std::remove_cv_t<T>>::Read(value, out, options);
}

template <class T>
    requires std::is_default_constructible_v<T>
std::expected<T, Error> Read(Value value, const ReadOptions &options = {}) {
    T    result {};
    auto status = Serde<T>::Read(value, result, options);
    if (!status) {
        return std::unexpected(status.error());
    }
    return result;
}

// Parse + Read in one go. T must not hold std::string_view / MDL::Value
// members: the temporary document is gone when this returns.
template <class T>
    requires std::is_default_constructible_v<T>
std::expected<T, Error> Load(std::string_view source, const ParseOptions &parse = {}, const ReadOptions &read = {}) {
    auto doc = Parse(source, parse);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    return Read<T>(doc->Root(), read);
}

// Writes an object-like value (reflected struct, map, MDL::Value object) as a
// whole file.
template <class T>
std::expected<void, Error> Write(const T &value, Output output, const WriteOptions &options = {}) {
    Writer writer(output, options);
    Serde<T>::Write(writer, value);
    return writer.Finish();
}

template <class T>
std::expected<std::string, Error> WriteToString(const T &value, const WriteOptions &options = {}) {
    std::string text;
    auto        status = Write(value, Output::ToString(text), options);
    if (!status) {
        return std::unexpected(status.error());
    }
    return text;
}


template <class T>
Writer &Writer::Field(std::string_view key, const T &value) {
    if (Detail::ShouldWrite(value)) {
        Key(key);
        Serde<T>::Write(*this, value);
    }
    return *this;
}

template <class T>
Writer &Writer::Item(const T &value) {
    Serde<T>::Write(*this, value);
    return *this;
}


// ---------------------------------------------------------------------------
// scalars

template <class T>
    requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
struct Serde<T> {
    static std::expected<void, Error> Read(Value value, T &out, const ReadOptions &) {
        auto result = value.As<T>();
        if (!result) {
            return std::unexpected(result.error());
        }
        out = *result;
        return {};
    }

    static void Write(Writer &writer, const T &value) {
        if constexpr (std::is_enum_v<T>) {
            std::string_view name = EnumToName(value);
            if (name.empty()) {
                // no name for this value: still a valid file, but not readable back as T
                writer.Number(static_cast<std::underlying_type_t<T>>(value));
            } else {
                writer.String(name);
            }
        } else {
            writer.Number(value);
        }
    }
};

template <Detail::StringLike T>
struct Serde<T> {
    static std::expected<void, Error> Read(Value value, T &out, const ReadOptions &) {
        auto text = value.AsString();
        if (!text) {
            return std::unexpected(text.error());
        }
        out.assign(text->data(), text->size());
        return {};
    }

    static void Write(Writer &writer, const T &value) { writer.String(value); }
};

// Points into the document: valid only while the Document is alive.
template <>
struct Serde<std::string_view> {
    static std::expected<void, Error> Read(Value value, std::string_view &out, const ReadOptions &) {
        auto text = value.AsString();
        if (!text) {
            return std::unexpected(text.error());
        }
        out = *text;
        return {};
    }

    static void Write(Writer &writer, std::string_view value) { writer.String(value); }
};

template <usize N>
struct Serde<char[N]> {
    static void Write(Writer &writer, const char (&value)[N]) {
        writer.String(std::string_view(value, value[N - 1] == '\0' ? N - 1 : N));
    }
};

template <>
struct Serde<const char *> {
    static void Write(Writer &writer, const char *value) { writer.String(value); }
};


// ---------------------------------------------------------------------------
// containers

template <class T>
struct Serde<std::optional<T>> {
    static std::expected<void, Error> Read(Value value, std::optional<T> &out, const ReadOptions &options) {
        out.emplace();
        return Serde<T>::Read(value, *out, options);
    }

    static bool ShouldWrite(const std::optional<T> &value) { return value.has_value(); }

    static void Write(Writer &writer, const std::optional<T> &value) {
        if (value) {
            Serde<T>::Write(writer, *value);
        }
    }
};

template <Detail::ListLike T>
struct Serde<T> {
    using Item = typename T::value_type;

    static std::expected<void, Error> Read(Value value, T &out, const ReadOptions &options) {
        if (!value.IsList()) {
            return Detail::Fail(value, value.IsValid() ? ErrorCode::TypeMismatch : ErrorCode::MissingKey);
        }
        out.clear();
        if constexpr (requires { out.reserve(value.Size()); }) {
            out.reserve(value.Size());
        }
        for (Value item : value.Items()) {
            auto status = Serde<Item>::Read(item, out.emplace_back(), options);
            if (!status) {
                return status;
            }
        }
        return {};
    }

    static void Write(Writer &writer, const T &value) {
        writer.BeginList(Detail::IsFlat<Item>() ? Layout::Inline : Layout::Block);
        for (const auto &item : value) {
            Serde<Item>::Write(writer, item);
        }
        writer.EndList();
    }
};

template <class T, usize N>
struct Serde<std::array<T, N>> {
    static std::expected<void, Error> Read(Value value, std::array<T, N> &out, const ReadOptions &options) {
        if (!value.IsList()) {
            return Detail::Fail(value, value.IsValid() ? ErrorCode::TypeMismatch : ErrorCode::MissingKey);
        }
        if (value.Size() != N) {
            return Detail::Fail(value, ErrorCode::SizeMismatch);
        }
        for (usize i = 0; i < N; ++i) {
            auto status = Serde<T>::Read(value.At(i), out[i], options);
            if (!status) {
                return status;
            }
        }
        return {};
    }

    static void Write(Writer &writer, const std::array<T, N> &value) {
        writer.BeginList(Detail::IsFlat<T>() ? Layout::Inline : Layout::Block);
        for (const auto &item : value) {
            Serde<T>::Write(writer, item);
        }
        writer.EndList();
    }
};

template <Detail::MapLike T>
struct Serde<T> {
    using Mapped = typename T::mapped_type;
    using KeyType = typename T::key_type;

    static std::expected<void, Error> Read(Value value, T &out, const ReadOptions &options) {
        if (!value.IsObject()) {
            return Detail::Fail(value, value.IsValid() ? ErrorCode::TypeMismatch : ErrorCode::MissingKey);
        }
        out.clear();
        for (auto [key, item] : value.Members()) {
            auto [it, inserted] = out.try_emplace(KeyType(key));
            auto status = Serde<Mapped>::Read(item, it->second, options);
            if (!status) {
                return status;
            }
        }
        return {};
    }

    static void Write(Writer &writer, const T &value) {
        writer.BeginObject();
        for (const auto &[key, item] : value) {
            writer.Field(std::string_view(key), item);
        }
        writer.EndObject();
    }
};


// ---------------------------------------------------------------------------
// DOM

// Reading keeps a handle into the document; writing copies the subtree.
template <>
struct Serde<Value> {
    static std::expected<void, Error> Read(Value value, Value &out, const ReadOptions &) {
        out = value;
        return {};
    }

    static void Write(Writer &writer, const Value &value) {
        switch (value.GetType()) {
            case Type::Object: {
                bool flat = true;
                for (auto [key, item] : value.Members()) {
                    flat = flat && item.IsScalar();
                }
                bool inlined = flat && writer.InList() && value.Size() <= writer.Options().MaxInlineMembers;
                writer.BeginObject(inlined ? Layout::Inline : Layout::Block);
                for (auto [key, item] : value.Members()) {
                    writer.Key(key);
                    Write(writer, item);
                }
                writer.EndObject();
                break;
            }
            case Type::List: {
                bool flat = true;
                for (Value item : value.Items()) {
                    flat = flat && item.IsScalar();
                }
                writer.BeginList(flat ? Layout::Inline : Layout::Block);
                for (Value item : value.Items()) {
                    Write(writer, item);
                }
                writer.EndList();
                break;
            }
            case Type::Number:
            case Type::Bool:
                writer.Literal(*value.AsString());
                break;
            case Type::String:
                writer.String(*value.AsString());
                break;
            case Type::None:
                writer.String("");
                break;
        }
    }
};

template <>
struct Serde<Document> {
    static void Write(Writer &writer, const Document &doc) { Serde<Value>::Write(writer, doc.Root()); }
};


// ---------------------------------------------------------------------------
// reflected aggregates

namespace Detail {

template <class T>
using FieldReadFn = std::expected<void, Error> (*)(Value, T &, const ReadOptions &);

template <class T, usize I>
std::expected<void, Error> ReadField(Value value, T &out, const ReadOptions &options) {
    auto fields = TieFields(out);
    return Serde<FieldType<T, I>>::Read(value, std::get<I>(fields), options);
}

template <class T, usize... I>
consteval auto MakeFieldReaders(std::index_sequence<I...>) {
    return std::array<FieldReadFn<T>, sizeof...(I)> {&ReadField<T, I>...};
}

template <class T, usize... I>
consteval auto MakeOptionalMask(std::index_sequence<I...>) {
    return std::array<bool, sizeof...(I)> {IsOptional<FieldType<T, I>>::value...};
}

} // namespace Detail

template <Detail::Aggregate T>
struct Serde<T> {
    static constexpr usize Count = Detail::FieldCount<T>;

    static std::expected<void, Error> Read(Value value, T &out, const ReadOptions &options) {
        if (!value.IsObject()) {
            return Detail::Fail(value, value.IsValid() ? ErrorCode::TypeMismatch : ErrorCode::MissingKey);
        }

        static constexpr auto &s_keys = Detail::FieldKeys<T>;
        static constexpr auto  readers = Detail::MakeFieldReaders<T>(std::make_index_sequence<Count> {});

        std::array<bool, Count> seen {};

        for (const Detail::Member &member : Detail::Access::MembersOf(value)) {
            std::string_view key(member.Key, member.KeySize);

            usize index = Count;
            for (usize i = 0; i < Count; ++i) {
                if (s_keys[i] == key) {
                    index = i;
                    break;
                }
            }

            if (index == Count) {
                if (options.Unknown == UnknownKeys::Error) {
                    return std::unexpected(Error {
                        .Code = ErrorCode::UnknownKey,
                        .Pos = {.Line = member.KeyLine, .Column = member.KeyColumn},
                    });
                }
                continue;
            }

            seen[index] = true;
            auto status = readers[index](Detail::Access::Make(&member.Val), out, options);
            if (!status) {
                return status;
            }
        }

        if (options.Missing == MissingKeys::Error) {
            static constexpr auto optionalMask = Detail::MakeOptionalMask<T>(std::make_index_sequence<Count> {});
            for (usize i = 0; i < Count; ++i) {
                if (!seen[i] && !optionalMask[i]) {
                    return Detail::Fail(value, ErrorCode::MissingKey, s_keys[i]);
                }
            }
        }

        return {};
    }

    static void Write(Writer &writer, const T &value) {
        bool inlined = Detail::IsFlatObject<T>() && writer.InList() && Count <= writer.Options().MaxInlineMembers;
        writer.BeginObject(inlined ? Layout::Inline : Layout::Block);

        auto fields = Detail::TieFields(value);
        [&]<usize... I>(std::index_sequence<I...>) {
            (writer.Field(Detail::FieldKeys<T>[I], std::get<I>(fields)), ...);
        }(std::make_index_sequence<Count> {});

        writer.EndObject();
    }
};

} // namespace MDL
