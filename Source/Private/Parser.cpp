#include <MDL/Document.h>

#include <MDL/Assert.h>

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>


namespace MDL {
namespace {

using Detail::Member;
using Detail::Node;

enum CharClass : u8 {
    ClassSpace = 1u << 0,
    ClassStop = 1u << 1,       // ends a bare string
    ClassKeyStart = 1u << 2,   // [A-Za-z_]
    ClassKey = 1u << 3,        // [A-Za-z0-9_-]
    ClassBareEscape = 1u << 4, // may follow '\' in a bare string
    ClassQuoted = 1u << 5,     // special inside "..."
    ClassDigit = 1u << 6,
};

constexpr std::array<u8, 256> MakeCharTable() {
    std::array<u8, 256> table {};
    auto                add = [&](std::string_view chars, u8 cls) {
        for (char c : chars) {
            table[static_cast<u8>(c)] |= cls;
        }
    };

    add(" \t\r\n\v\f", ClassSpace | ClassStop);
    add("{}[]\"`#", ClassStop);
    add("#{}[]\"` \t", ClassBareEscape);
    add("\"\\\n", ClassQuoted);
    add("0123456789", ClassDigit | ClassKey);
    add("-", ClassKey);
    for (char c = 'a'; c <= 'z'; ++c) {
        table[static_cast<u8>(c)] |= ClassKeyStart | ClassKey;
        table[static_cast<u8>(c - 'a' + 'A')] |= ClassKeyStart | ClassKey;
    }
    table['_'] |= ClassKeyStart | ClassKey;
    return table;
}

constexpr std::array<u8, 256> CharTable = MakeCharTable();

inline bool Has(char c, u8 cls) {
    return (CharTable[static_cast<u8>(c)] & cls) != 0;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// -?\d+(\.\d+)?([eE][+-]?\d+)?
bool IsNumber(const char *p, const char *end, bool &isFloat) {
    auto digits = [&]() {
        const char *start = p;
        while (p < end && Has(*p, ClassDigit)) {
            ++p;
        }
        return p != start;
    };

    isFloat = false;
    if (p < end && *p == '-') {
        ++p;
    }
    if (!digits()) {
        return false;
    }
    if (p < end && *p == '.') {
        ++p;
        if (!digits()) {
            return false;
        }
        isFloat = true;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) {
            ++p;
        }
        if (!digits()) {
            return false;
        }
        isFloat = true;
    }
    return p == end;
}

Position PositionAt(std::string_view source, usize offset) {
    Position pos {.Line = 1, .Column = 1};
    for (usize i = 0; i < offset; ++i) {
        if (source[i] == '\n') {
            ++pos.Line;
            pos.Column = 1;
        } else {
            ++pos.Column;
        }
    }
    return pos;
}


// Stack with inline storage, spills to the allocator. T must be trivially copyable.
template <class T, usize InlineCapacity>
class ScratchStack final {
  public:
    explicit ScratchStack(Allocator alloc) : m_Alloc(alloc) {}

    ~ScratchStack() {
        if (m_Data != InlineData()) {
            m_Alloc.Free(m_Data, m_Capacity * sizeof(T), alignof(T));
        }
    }

    ScratchStack(const ScratchStack &) = delete;
    ScratchStack(ScratchStack &&) = delete;
    ScratchStack &operator=(const ScratchStack &) = delete;
    ScratchStack &operator=(ScratchStack &&) = delete;

    [[nodiscard]] bool Push(const T &value) {
        if (m_Size == m_Capacity && !Grow()) {
            return false;
        }
        m_Data[m_Size++] = value;
        return true;
    }

    T    *Data() { return m_Data; }
    usize Size() const { return m_Size; }
    void  Resize(usize size) { m_Size = size; }

  private:
    T *InlineData() { return reinterpret_cast<T *>(m_Inline); }

    bool Grow() {
        usize capacity = m_Capacity * 2;
        auto *data = static_cast<T *>(m_Alloc.Alloc(capacity * sizeof(T), alignof(T)));
        if (data == nullptr) {
            return false;
        }
        std::memcpy(data, m_Data, m_Size * sizeof(T));
        if (m_Data != InlineData()) {
            m_Alloc.Free(m_Data, m_Capacity * sizeof(T), alignof(T));
        }
        m_Data = data;
        m_Capacity = capacity;
        return true;
    }

    Allocator m_Alloc;
    alignas(T) std::byte m_Inline[InlineCapacity * sizeof(T)];
    T    *m_Data = InlineData();
    usize m_Size = 0;
    usize m_Capacity = InlineCapacity;
};


struct Text final {
    const char *Ptr = nullptr;
    u32         Size = 0;
    bool        Decoded = false;
};


class Parser final {
  public:
    Parser(const char *begin, const char *end, Detail::BlockArena &arena, const ParseOptions &options) :
        m_Cur(begin),
        m_End(end),
        m_LineStart(begin),
        m_MaxDepth(options.MaxDepth),
        m_Arena(arena),
        m_Members(options.Alloc),
        m_Items(options.Alloc),
        m_Sorted(options.Alloc) {}

    bool ParseDocument(Node &root) {
        root.Kind = Type::Object;
        root.Line = 1;
        root.Column = 1;
        return ParseObjectBody(root, true, 1, 1);
    }

    const Error &GetError() const { return m_Error; }

  private:
    u32 Column() const { return static_cast<u32>(m_Cur - m_LineStart) + 1; }

    bool Fail(ErrorCode code, u32 line, u32 column) {
        m_Error = Error {.Code = code, .Pos = {.Line = line, .Column = column}};
        return false;
    }

    bool FailHere(ErrorCode code) { return Fail(code, m_Line, Column()); }


    void SkipTrivia() {
        while (m_Cur < m_End) {
            char c = *m_Cur;
            if (c == '\n') {
                ++m_Cur;
                ++m_Line;
                m_LineStart = m_Cur;
            } else if (Has(c, ClassSpace)) {
                ++m_Cur;
            } else if (c == '#') {
                const void *newline = std::memchr(m_Cur, '\n', static_cast<usize>(m_End - m_Cur));
                m_Cur = newline != nullptr ? static_cast<const char *>(newline) : m_End;
            } else {
                return;
            }
        }
    }


    bool ParseObjectBody(Node &out, bool topLevel, u32 openLine, u32 openColumn) {
        usize base = m_Members.Size();

        while (true) {
            SkipTrivia();
            if (m_Cur == m_End) {
                if (topLevel) {
                    break;
                }
                return Fail(ErrorCode::UnclosedObject, openLine, openColumn);
            }

            if (*m_Cur == '}') {
                if (topLevel) {
                    return FailHere(ErrorCode::UnexpectedCharacter);
                }
                ++m_Cur;
                break;
            }

            Member member;
            if (!ParseKey(member)) {
                return false;
            }

            SkipTrivia();
            if (m_Cur == m_End || *m_Cur == '}' || *m_Cur == ']') {
                return FailHere(ErrorCode::ExpectedValue);
            }
            if (!ParseValue(member.Val)) {
                return false;
            }
            if (!m_Members.Push(member)) {
                return FailHere(ErrorCode::OutOfMemory);
            }
        }

        usize count = m_Members.Size() - base;
        if (!CheckDuplicates(m_Members.Data() + base, count)) {
            return false;
        }

        Member *stored = nullptr;
        if (count > 0) {
            stored = m_Arena.Push<Member>(count);
            if (stored == nullptr) {
                return FailHere(ErrorCode::OutOfMemory);
            }
            std::memcpy(stored, m_Members.Data() + base, count * sizeof(Member));
        }

        out.Data = stored;
        out.Size = static_cast<u32>(count);
        m_Members.Resize(base);
        return true;
    }

    bool ParseList(Node &out) {
        u32 line = m_Line;
        u32 column = Column();
        ++m_Cur;

        usize base = m_Items.Size();
        while (true) {
            SkipTrivia();
            if (m_Cur == m_End) {
                return Fail(ErrorCode::UnclosedList, line, column);
            }
            if (*m_Cur == ']') {
                ++m_Cur;
                break;
            }
            if (*m_Cur == '}') {
                return FailHere(ErrorCode::UnexpectedCharacter);
            }

            Node item;
            if (!ParseValue(item)) {
                return false;
            }
            if (!m_Items.Push(item)) {
                return FailHere(ErrorCode::OutOfMemory);
            }
        }

        usize count = m_Items.Size() - base;
        Node *stored = nullptr;
        if (count > 0) {
            stored = m_Arena.Push<Node>(count);
            if (stored == nullptr) {
                return FailHere(ErrorCode::OutOfMemory);
            }
            std::memcpy(stored, m_Items.Data() + base, count * sizeof(Node));
        }

        out.Data = stored;
        out.Size = static_cast<u32>(count);
        m_Items.Resize(base);
        return true;
    }

    bool ParseValue(Node &out) {
        out.Line = m_Line;
        out.Column = Column();

        switch (*m_Cur) {
            case '{': {
                if (m_Depth >= m_MaxDepth) {
                    return FailHere(ErrorCode::NestingTooDeep);
                }
                ++m_Depth;
                ++m_Cur;
                out.Kind = Type::Object;
                bool ok = ParseObjectBody(out, false, out.Line, out.Column);
                --m_Depth;
                return ok;
            }
            case '[': {
                if (m_Depth >= m_MaxDepth) {
                    return FailHere(ErrorCode::NestingTooDeep);
                }
                ++m_Depth;
                out.Kind = Type::List;
                bool ok = ParseList(out);
                --m_Depth;
                return ok;
            }
            case '"':
            case '`': {
                bool quoted = *m_Cur == '"';
                Text text;
                if (!(quoted ? ParseQuoted(text) : ParseRaw(text))) {
                    return false;
                }
                out.Kind = Type::String;
                out.Data = text.Ptr;
                out.Size = text.Size;
                out.Flags = static_cast<u8>(quoted ? StringStyle::Quoted : StringStyle::Raw);
                if (text.Decoded) {
                    out.Flags |= Detail::NodeFlagDecoded;
                }
                return true;
            }
            default:
                return ParseBare(out);
        }
    }

    bool ParseKey(Member &out) {
        out.KeyLine = m_Line;
        out.KeyColumn = Column();

        char c = *m_Cur;
        if (c == '"' || c == '`') {
            Text text;
            if (!(c == '"' ? ParseQuoted(text) : ParseRaw(text))) {
                return false;
            }
            out.Key = text.Ptr;
            out.KeySize = text.Size;
            return true;
        }

        if (Has(c, ClassKeyStart)) {
            const char *start = m_Cur++;
            while (m_Cur < m_End && Has(*m_Cur, ClassKey)) {
                ++m_Cur;
            }
            if (m_Cur < m_End && !Has(*m_Cur, ClassStop)) {
                return Fail(ErrorCode::InvalidKey, out.KeyLine, out.KeyColumn);
            }
            out.Key = start;
            out.KeySize = static_cast<u32>(m_Cur - start);
            return true;
        }

        if (c == '{' || c == '[' || c == ']') {
            return FailHere(ErrorCode::UnexpectedCharacter);
        }
        return FailHere(ErrorCode::InvalidKey);
    }


    bool ParseQuoted(Text &out) {
        u32 line = m_Line;
        u32 column = Column();
        ++m_Cur;

        const char *start = m_Cur;
        bool        escaped = false;
        while (true) {
            while (m_Cur < m_End && !Has(*m_Cur, ClassQuoted)) {
                ++m_Cur;
            }
            if (m_Cur == m_End || *m_Cur == '\n') {
                return Fail(ErrorCode::UnterminatedString, line, column);
            }
            if (*m_Cur == '"') {
                break;
            }

            // backslash: skip it and the escaped character
            escaped = true;
            ++m_Cur;
            if (m_Cur == m_End || *m_Cur == '\n') {
                return Fail(ErrorCode::UnterminatedString, line, column);
            }
            ++m_Cur;
        }

        const char *stop = m_Cur++;
        if (!escaped) {
            out = Text {.Ptr = start, .Size = static_cast<u32>(stop - start)};
            return true;
        }
        return DecodeQuoted(start, stop, out);
    }

    bool DecodeQuoted(const char *start, const char *stop, Text &out) {
        auto  capacity = static_cast<usize>(stop - start);
        auto *buffer = static_cast<char *>(m_Arena.Push(capacity, 1));
        if (buffer == nullptr) {
            return FailHere(ErrorCode::OutOfMemory);
        }

        char *w = buffer;
        for (const char *p = start; p < stop;) {
            if (*p != '\\') {
                *w++ = *p++;
                continue;
            }

            // the scanner guarantees a character after every backslash
            const char *escape = p;
            char        e = p[1];
            p += 2;

            auto fail = [&]() {
                return Fail(ErrorCode::InvalidEscape, m_Line, static_cast<u32>(escape - m_LineStart) + 1);
            };

            switch (e) {
                case 'a':
                    *w++ = '\a';
                    break;
                case 'b':
                    *w++ = '\b';
                    break;
                case 'f':
                    *w++ = '\f';
                    break;
                case 'n':
                    *w++ = '\n';
                    break;
                case 'r':
                    *w++ = '\r';
                    break;
                case 't':
                    *w++ = '\t';
                    break;
                case 'v':
                    *w++ = '\v';
                    break;
                case '\\':
                    *w++ = '\\';
                    break;
                case '\'':
                    *w++ = '\'';
                    break;
                case '"':
                    *w++ = '"';
                    break;
                case '?':
                    *w++ = '?';
                    break;

                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7': {
                    // up to 3 octal digits
                    u32 value = static_cast<u32>(e - '0');
                    for (int i = 0; i < 2 && p < stop && *p >= '0' && *p <= '7'; ++i, ++p) {
                        value = value * 8 + static_cast<u32>(*p - '0');
                    }
                    if (value > 0xFF) {
                        return fail();
                    }
                    *w++ = static_cast<char>(value);
                    break;
                }

                case 'x': {
                    // 1 or 2 hex digits
                    u32 value = 0;
                    int digits = 0;
                    for (; digits < 2 && p < stop && HexValue(*p) >= 0; ++digits, ++p) {
                        value = value * 16 + static_cast<u32>(HexValue(*p));
                    }
                    if (digits == 0) {
                        return fail();
                    }
                    *w++ = static_cast<char>(value);
                    break;
                }

                case 'u':
                case 'U': {
                    int count = e == 'u' ? 4 : 8;
                    if (stop - p < count) {
                        return fail();
                    }
                    u32 cp = 0;
                    for (int i = 0; i < count; ++i, ++p) {
                        int digit = HexValue(*p);
                        if (digit < 0) {
                            return fail();
                        }
                        cp = cp * 16 + static_cast<u32>(digit);
                    }
                    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                        return fail();
                    }
                    w += Detail::EncodeUtf8(cp, w);
                    break;
                }

                default:
                    // unknown escape: kept as is
                    *w++ = '\\';
                    *w++ = e;
                    break;
            }
        }

        auto size = static_cast<usize>(w - buffer);
        m_Arena.ShrinkLast(buffer, capacity, size);
        out = Text {.Ptr = buffer, .Size = static_cast<u32>(size), .Decoded = true};
        return true;
    }

    bool ParseRaw(Text &out) {
        u32 line = m_Line;
        u32 column = Column();
        ++m_Cur;

        const char *start = m_Cur;
        bool        escaped = false;
        while (true) {
            if (m_Cur == m_End) {
                return Fail(ErrorCode::UnterminatedString, line, column);
            }
            char c = *m_Cur;
            if (c == '`') {
                break;
            }
            if (c == '\\' && m_Cur + 1 < m_End && m_Cur[1] == '`') {
                escaped = true;
                m_Cur += 2;
                continue;
            }
            if (c == '\n') {
                ++m_Line;
                m_LineStart = m_Cur + 1;
            }
            ++m_Cur;
        }

        const char *stop = m_Cur++;
        auto        size = static_cast<usize>(stop - start);
        if (!escaped) {
            out = Text {.Ptr = start, .Size = static_cast<u32>(size)};
            return true;
        }

        auto *buffer = static_cast<char *>(m_Arena.Push(size, 1));
        if (buffer == nullptr) {
            return FailHere(ErrorCode::OutOfMemory);
        }
        char *w = buffer;
        for (const char *p = start; p < stop; ++p) {
            if (*p == '\\' && p + 1 < stop && p[1] == '`') {
                ++p;
            }
            *w++ = *p;
        }
        m_Arena.ShrinkLast(buffer, size, static_cast<usize>(w - buffer));
        out = Text {.Ptr = buffer, .Size = static_cast<u32>(w - buffer), .Decoded = true};
        return true;
    }

    bool ParseBare(Node &out) {
        const char *start = m_Cur;
        bool        escaped = false;
        while (m_Cur < m_End) {
            char c = *m_Cur;
            if (c == '\\' && m_Cur + 1 < m_End && Has(m_Cur[1], ClassBareEscape)) {
                escaped = true;
                m_Cur += 2;
                continue;
            }
            if (Has(c, ClassStop)) {
                break;
            }
            ++m_Cur;
        }

        auto size = static_cast<usize>(m_Cur - start);
        out.Flags = static_cast<u8>(StringStyle::Bare);

        if (!escaped) {
            std::string_view text(start, size);
            bool             isFloat = false;
            if (text == "true" || text == "false") {
                out.Kind = Type::Bool;
                if (text.size() == 4) {
                    out.Flags |= Detail::NodeFlagTrue;
                }
            } else if (IsNumber(start, m_Cur, isFloat)) {
                out.Kind = Type::Number;
                if (isFloat) {
                    out.Flags |= Detail::NodeFlagFloat;
                }
            } else {
                out.Kind = Type::String;
            }
            out.Data = start;
            out.Size = static_cast<u32>(size);
            return true;
        }

        auto *buffer = static_cast<char *>(m_Arena.Push(size, 1));
        if (buffer == nullptr) {
            return FailHere(ErrorCode::OutOfMemory);
        }
        char *w = buffer;
        for (const char *p = start; p < m_Cur; ++p) {
            if (*p == '\\' && p + 1 < m_Cur && Has(p[1], ClassBareEscape)) {
                ++p;
            }
            *w++ = *p;
        }
        m_Arena.ShrinkLast(buffer, size, static_cast<usize>(w - buffer));

        out.Kind = Type::String;
        out.Data = buffer;
        out.Size = static_cast<u32>(w - buffer);
        out.Flags |= Detail::NodeFlagDecoded;
        return true;
    }


    static bool SameKey(const Member *a, const Member *b) {
        return a->KeySize == b->KeySize && std::memcmp(a->Key, b->Key, a->KeySize) == 0;
    }

    bool CheckDuplicates(const Member *members, usize count) {
        if (count < 2) {
            return true;
        }

        if (count <= 16) {
            for (usize i = 1; i < count; ++i) {
                for (usize j = 0; j < i; ++j) {
                    if (SameKey(&members[i], &members[j])) {
                        return Fail(ErrorCode::DuplicateKey, members[i].KeyLine, members[i].KeyColumn);
                    }
                }
            }
            return true;
        }

        m_Sorted.Resize(0);
        for (usize i = 0; i < count; ++i) {
            if (!m_Sorted.Push(&members[i])) {
                return FailHere(ErrorCode::OutOfMemory);
            }
        }

        const Member **sorted = m_Sorted.Data();
        std::sort(sorted, sorted + count, [](const Member *a, const Member *b) {
            if (a->KeySize != b->KeySize) {
                return a->KeySize < b->KeySize;
            }
            int cmp = std::memcmp(a->Key, b->Key, a->KeySize);
            return cmp != 0 ? cmp < 0 : a < b;
        });

        // report the duplicate that comes first in the file
        const Member *found = nullptr;
        for (usize i = 1; i < count; ++i) {
            if (SameKey(sorted[i - 1], sorted[i]) && (found == nullptr || sorted[i] < found)) {
                found = sorted[i];
            }
        }
        if (found != nullptr) {
            return Fail(ErrorCode::DuplicateKey, found->KeyLine, found->KeyColumn);
        }
        return true;
    }


    const char *m_Cur;
    const char *m_End;
    const char *m_LineStart;
    u32         m_Line = 1;
    u32         m_Depth = 0;
    u32         m_MaxDepth;

    Detail::BlockArena &m_Arena;

    ScratchStack<Member, 64>         m_Members;
    ScratchStack<Node, 128>          m_Items;
    ScratchStack<const Member *, 64> m_Sorted;

    Error m_Error {};
};

} // namespace


std::expected<Document, Error> Parse(std::string_view source, const ParseOptions &options) {
    if (source.size() >= std::numeric_limits<u32>::max()) {
        return std::unexpected(Error {.Code = ErrorCode::SourceTooLarge});
    }

    if (source.starts_with("\xEF\xBB\xBF")) {
        source.remove_prefix(3);
    }

    if (options.ValidateUtf8) {
        usize bad = Detail::ValidateUtf8(source);
        if (bad != source.size()) {
            return std::unexpected(Error {.Code = ErrorCode::InvalidUtf8, .Pos = PositionAt(source, bad)});
        }
    }

    Document doc;
    // dense asset files take ~4.5 bytes of nodes per source byte
    usize estimate = (source.size() * 5) + (options.CopySource ? source.size() : 0) + 512;
    doc.m_Arena.Init(options.Alloc, options.Buffer, estimate);

    const char *begin = source.data();
    if (options.CopySource && !source.empty()) {
        auto *copy = static_cast<char *>(doc.m_Arena.Push(source.size(), 1));
        if (copy == nullptr) {
            return std::unexpected(Error {.Code = ErrorCode::OutOfMemory});
        }
        std::memcpy(copy, source.data(), source.size());
        begin = copy;
    }

    auto *root = doc.m_Arena.Push<Node>();
    if (root == nullptr) {
        return std::unexpected(Error {.Code = ErrorCode::OutOfMemory});
    }
    new (root) Node {};

    Parser parser(begin, begin + source.size(), doc.m_Arena, options);
    if (!parser.ParseDocument(*root)) {
        return std::unexpected(parser.GetError());
    }

    doc.m_Root = root;
    return doc;
}

} // namespace MDL
