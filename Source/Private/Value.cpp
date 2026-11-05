#include <MDL/Value.h>

#include <MDL/Document.h>

#include <charconv>
#include <cstring>
#include <utility>


namespace MDL {

namespace Detail {
namespace {

template <class T>
NumberStatus FromChars(std::string_view text, T &out) {
    const char *end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, out);
    return (ec == std::errc {} && ptr == end) ? NumberStatus::Ok : NumberStatus::OutOfRange;
}

} // namespace

NumberStatus ParseI64(std::string_view text, i64 &out) { return FromChars(text, out); }

NumberStatus ParseU64(std::string_view text, u64 &out) {
    if (text == "-0") {
        out = 0;
        return NumberStatus::Ok;
    }
    return FromChars(text, out);
}

NumberStatus ParseF64(std::string_view text, f64 &out) { return FromChars(text, out); }
NumberStatus ParseF32(std::string_view text, f32 &out) { return FromChars(text, out); }

} // namespace Detail


bool Value::IsScalar() const {
    Type type = GetType();
    return type == Type::String || type == Type::Number || type == Type::Bool;
}

Position Value::Pos() const {
    const Detail::Node *node = m_Node != nullptr ? m_Node : m_Parent;
    if (node == nullptr) {
        return {};
    }
    return {.Line = node->Line, .Column = node->Column};
}

StringStyle Value::Style() const {
    if (m_Node == nullptr || m_Node->Kind != Type::String) {
        return StringStyle::Bare;
    }
    return static_cast<StringStyle>(m_Node->Flags & Detail::NodeFlagStyleMask);
}

usize Value::Size() const {
    if (m_Node == nullptr || (m_Node->Kind != Type::Object && m_Node->Kind != Type::List)) {
        return 0;
    }
    return m_Node->Size;
}

Value Value::Find(std::string_view key) const {
    if (m_Node == nullptr) {
        return *this;
    }
    if (m_Node->Kind != Type::Object) {
        return {m_Node, ErrorCode::TypeMismatch};
    }

    const auto *members = static_cast<const Detail::Member *>(m_Node->Data);
    for (u32 i = 0; i < m_Node->Size; ++i) {
        const Detail::Member &member = members[i];
        if (member.KeySize == key.size() && std::memcmp(member.Key, key.data(), key.size()) == 0) {
            return Value(&member.Val);
        }
    }
    return {m_Node, ErrorCode::MissingKey};
}

Value Value::At(usize index) const {
    if (m_Node == nullptr) {
        return *this;
    }
    if (m_Node->Kind != Type::List) {
        return {m_Node, ErrorCode::TypeMismatch};
    }
    if (index >= m_Node->Size) {
        return {m_Node, ErrorCode::IndexOutOfRange};
    }
    return Value(static_cast<const Detail::Node *>(m_Node->Data) + index);
}

Range<MemberIterator> Value::Members() const {
    if (!IsObject()) {
        return {};
    }
    const auto *members = static_cast<const Detail::Member *>(m_Node->Data);
    return {MemberIterator(members), MemberIterator(members + m_Node->Size), m_Node->Size};
}

Range<ItemIterator> Value::Items() const {
    if (!IsList()) {
        return {};
    }
    const auto *items = static_cast<const Detail::Node *>(m_Node->Data);
    return {ItemIterator(items), ItemIterator(items + m_Node->Size), m_Node->Size};
}

std::expected<std::string_view, Error> Value::AsString() const {
    if (m_Node == nullptr) {
        return std::unexpected(MissingError());
    }
    if (!IsScalar()) {
        return std::unexpected(MakeError(ErrorCode::TypeMismatch));
    }
    return Detail::TextOf(*m_Node);
}

Error Value::MakeError(ErrorCode code) const {
    return Error {.Code = code, .Pos = Pos()};
}

Error Value::MissingError() const {
    return Error {.Code = m_Reason, .Pos = Pos()};
}


Document::Document(Document &&other) noexcept :
    m_Arena(std::move(other.m_Arena)),
    m_Root(std::exchange(other.m_Root, nullptr)) {}

Document &Document::operator=(Document &&other) noexcept {
    if (this != &other) {
        m_Arena = std::move(other.m_Arena);
        m_Root = std::exchange(other.m_Root, nullptr);
    }
    return *this;
}

} // namespace MDL
