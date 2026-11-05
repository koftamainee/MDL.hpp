#pragma once

#include <MDL/Detail/Names.h>
#include <MDL/Detail/Node.h>
#include <MDL/Error.h>
#include <MDL/Types.h>

#include <expected>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>


namespace MDL {

class Value;
class Document;

namespace Detail {

struct Access;

enum class NumberStatus : u8 {
    Ok,
    OutOfRange,
};

NumberStatus ParseI64(std::string_view text, i64 &out);
NumberStatus ParseU64(std::string_view text, u64 &out);
NumberStatus ParseF64(std::string_view text, f64 &out);
NumberStatus ParseF32(std::string_view text, f32 &out);

template <class T>
std::expected<T, ErrorCode> NumberAs(const Node &node);

} // namespace Detail


struct MemberView;

class MemberIterator final {
  public:
    using value_type = MemberView;          // NOLINT(readability-identifier-naming): STL naming
    using difference_type = std::ptrdiff_t; // NOLINT(readability-identifier-naming)

    MemberIterator() = default;
    explicit MemberIterator(const Detail::Member *ptr) : m_Ptr(ptr) {}

    MemberView      operator*() const;
    MemberIterator &operator++() {
        ++m_Ptr;
        return *this;
    }
    MemberIterator operator++(int) {
        MemberIterator copy = *this;
        ++m_Ptr;
        return copy;
    }
    bool operator==(const MemberIterator &) const = default;

  private:
    const Detail::Member *m_Ptr = nullptr;
};

class ItemIterator final {
  public:
    using value_type = Value;               // NOLINT(readability-identifier-naming): STL naming
    using difference_type = std::ptrdiff_t; // NOLINT(readability-identifier-naming)

    ItemIterator() = default;
    explicit ItemIterator(const Detail::Node *ptr) : m_Ptr(ptr) {}

    Value         operator*() const;
    ItemIterator &operator++() {
        ++m_Ptr;
        return *this;
    }
    ItemIterator operator++(int) {
        ItemIterator copy = *this;
        ++m_Ptr;
        return copy;
    }
    bool operator==(const ItemIterator &) const = default;

  private:
    const Detail::Node *m_Ptr = nullptr;
};

// NOLINTBEGIN(readability-identifier-naming): range-for / STL naming
template <class It>
class Range final {
  public:
    Range() = default;
    Range(It begin, It end, usize size) : m_Begin(begin), m_End(end), m_Size(size) {}

    It    begin() const { return m_Begin; }
    It    end() const { return m_End; }
    usize size() const { return m_Size; }
    bool  empty() const { return m_Size == 0; }

  private:
    It    m_Begin {};
    It    m_End {};
    usize m_Size = 0;
};
// NOLINTEND(readability-identifier-naming)


// Lightweight read-only handle to a node of a Document. Copy it freely, it is
// valid while the Document is alive.
//
// Lookups never fail loudly: a missing key gives an invalid Value, and the
// error (with the position of the last valid parent) comes out of As*().
//
//   auto src = doc["stages"]["vertex"]["src"].AsString();
//   i32  hp  = doc["hp"].ValueOr(100);
class Value final {
  public:
    Value() = default;

    Type GetType() const { return m_Node != nullptr ? m_Node->Kind : Type::None; }
    bool IsValid() const { return m_Node != nullptr; }
    bool IsObject() const { return GetType() == Type::Object; }
    bool IsList() const { return GetType() == Type::List; }
    bool IsString() const { return GetType() == Type::String; }
    bool IsNumber() const { return GetType() == Type::Number; }
    bool IsBool() const { return GetType() == Type::Bool; }
    bool IsScalar() const;

    explicit operator bool() const { return IsValid(); }

    // Position in the source. For an invalid Value - position of the
    // container where the lookup failed.
    Position Pos() const;

    // StringStyle::Bare for non-strings
    StringStyle Style() const;

    // members of an object / items of a list, 0 for everything else
    usize Size() const;

    Value Find(std::string_view key) const;
    Value At(usize index) const;
    bool  Contains(std::string_view key) const { return Find(key).IsValid(); }

    Value operator[](std::string_view key) const { return Find(key); }
    Value operator[](usize index) const { return At(index); }

    // for (auto [key, value] : obj.Members())
    Range<MemberIterator> Members() const;
    // for (MDL::Value item : list.Items())
    Range<ItemIterator> Items() const;

    // Any scalar: number and bool text is returned as written.
    std::expected<std::string_view, Error> AsString() const;

    // bool, integers, floats, enums, std::string_view.
    // Floats read as integers are truncated toward zero.
    template <class T>
    std::expected<T, Error> As() const;

    template <class T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    T ValueOr(T fallback) const {
        auto result = As<T>();
        return result ? *result : fallback;
    }

    std::string_view ValueOr(std::string_view fallback) const {
        auto result = AsString();
        return result ? *result : fallback;
    }

    // Error describing why this value is unusable as T's kind.
    Error MakeError(ErrorCode code) const;

  private:
    friend class Document;
    friend class MemberIterator;
    friend class ItemIterator;
    friend struct Detail::Access;

    explicit Value(const Detail::Node *node) : m_Node(node) {}
    Value(const Detail::Node *parent, ErrorCode reason) : m_Parent(parent), m_Reason(reason) {}

    Error MissingError() const;

    const Detail::Node *m_Node = nullptr;
    const Detail::Node *m_Parent = nullptr;
    ErrorCode           m_Reason = ErrorCode::MissingKey;
};


struct MemberView final {
    std::string_view Key;
    Value            Val;
};

inline MemberView MemberIterator::operator*() const {
    return MemberView {
        .Key = std::string_view(m_Ptr->Key, m_Ptr->KeySize),
        .Val = Value(&m_Ptr->Val),
    };
}

inline Value ItemIterator::operator*() const { return Value(m_Ptr); }


namespace Detail {

// Internal access for the reflection layer.
struct Access final {
    static const Node *NodeOf(const Value &value) { return value.m_Node; }
    static Value       Make(const Node *node) { return Value(node); }

    static std::span<const Member> MembersOf(const Value &value) {
        return {static_cast<const Member *>(value.m_Node->Data), value.m_Node->Size};
    }
};

inline std::string_view TextOf(const Node &node) {
    return {static_cast<const char *>(node.Data), node.Size};
}

template <class T>
std::expected<T, ErrorCode> NumberAs(const Node &node) {
    std::string_view text = TextOf(node);

    if constexpr (std::is_floating_point_v<T>) {
        static_assert(std::is_same_v<T, f32> || std::is_same_v<T, f64>, "MDL: only float and double are supported");

        T            value {};
        NumberStatus status {};
        if constexpr (std::is_same_v<T, f32>) {
            status = ParseF32(text, value);
        } else {
            status = ParseF64(text, value);
        }
        if (status != NumberStatus::Ok) {
            return std::unexpected(ErrorCode::OutOfRange);
        }
        return value;
    } else {
        using Limits = std::numeric_limits<T>;

        if ((node.Flags & NodeFlagFloat) != 0) {
            f64 value = 0;
            if (ParseF64(text, value) != NumberStatus::Ok) {
                return std::unexpected(ErrorCode::OutOfRange);
            }
            // [lower, upper) are exact powers of two, so these compares are exact
            constexpr f64 upper = static_cast<f64>(u64 {1} << (Limits::digits - 1)) * 2.0;
            constexpr f64 lower = Limits::is_signed ? -upper : -1.0;
            bool          inRange = Limits::is_signed ? (value >= lower && value < upper) : (value > lower && value < upper);
            if (!inRange) {
                return std::unexpected(ErrorCode::OutOfRange);
            }
            return static_cast<T>(value);
        }

        if constexpr (Limits::is_signed) {
            i64 value = 0;
            if (ParseI64(text, value) != NumberStatus::Ok || value < static_cast<i64>(Limits::min()) ||
                value > static_cast<i64>(Limits::max())) {
                return std::unexpected(ErrorCode::OutOfRange);
            }
            return static_cast<T>(value);
        } else {
            u64 value = 0;
            if (ParseU64(text, value) != NumberStatus::Ok || value > static_cast<u64>(Limits::max())) {
                return std::unexpected(ErrorCode::OutOfRange);
            }
            return static_cast<T>(value);
        }
    }
}

} // namespace Detail


template <class T>
std::expected<T, Error> Value::As() const {
    using U = std::remove_cv_t<T>;

    if (m_Node == nullptr) {
        return std::unexpected(MissingError());
    }

    if constexpr (std::is_same_v<U, bool>) {
        if (m_Node->Kind != Type::Bool) {
            return std::unexpected(MakeError(ErrorCode::TypeMismatch));
        }
        return (m_Node->Flags & Detail::NodeFlagTrue) != 0;
    } else if constexpr (std::is_arithmetic_v<U>) {
        if (m_Node->Kind != Type::Number) {
            return std::unexpected(MakeError(ErrorCode::TypeMismatch));
        }
        auto result = Detail::NumberAs<U>(*m_Node);
        if (!result) {
            return std::unexpected(MakeError(result.error()));
        }
        return *result;
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return AsString();
    } else if constexpr (std::is_enum_v<U>) {
        if (m_Node->Kind != Type::String) {
            return std::unexpected(MakeError(ErrorCode::TypeMismatch));
        }
        U value {};
        if (!EnumFromName(Detail::TextOf(*m_Node), value)) {
            return std::unexpected(MakeError(ErrorCode::InvalidEnum));
        }
        return value;
    } else {
        static_assert(sizeof(U) == 0, "Value::As<T>: T must be a scalar, use MDL::Read<T> for composite types");
    }
}

} // namespace MDL
