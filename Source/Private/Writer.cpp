#include <MDL/Writer.h>

#include <MDL/Assert.h>

#include <charconv>
#include <cmath>
#include <cstring>


namespace MDL {
namespace {

bool IsStop(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\v':
        case '\f':
        case '{':
        case '}':
        case '[':
        case ']':
        case '"':
        case '`':
        case '#':
            return true;
        default:
            return false;
    }
}

bool IsControl(char c) {
    auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7F;
}

bool IsKeyStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsKeyChar(char c) {
    return IsKeyStart(c) || (c >= '0' && c <= '9') || c == '-';
}

bool IsIdentifierKey(std::string_view text) {
    if (text.empty() || !IsKeyStart(text.front())) {
        return false;
    }
    for (char c : text) {
        if (!IsKeyChar(c)) {
            return false;
        }
    }
    return true;
}

// Same grammar as the parser: -?\d+(\.\d+)?([eE][+-]?\d+)?
bool LooksLikeNumber(std::string_view text) {
    usize i = 0;
    auto  digits = [&]() {
        usize start = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
        }
        return i != start;
    };

    if (i < text.size() && text[i] == '-') {
        ++i;
    }
    if (!digits()) {
        return false;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        if (!digits()) {
            return false;
        }
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        if (!digits()) {
            return false;
        }
    }
    return i == text.size();
}

bool CanBeBare(std::string_view text) {
    if (text.empty() || text.back() == '\\' || text == "true" || text == "false" || LooksLikeNumber(text)) {
        return false;
    }
    for (char c : text) {
        if (IsStop(c) || IsControl(c)) {
            return false;
        }
    }
    return true;
}

// Raw strings read better for paths with backslashes.
bool PrefersRaw(std::string_view text) {
    if (text.empty() || text.back() == '\\' || text.find('\\') == std::string_view::npos) {
        return false;
    }
    for (char c : text) {
        if (c == '`' || IsControl(c)) {
            return false;
        }
    }
    return true;
}

} // namespace


Writer::Writer(Output output, const WriteOptions &options) :
    m_Output(output),
    m_Options(options) {
    MDL_CHECK(output.WriteFn != nullptr);
    m_Frames[0] = Frame {.Kind = FrameKind::Root, .Mode = Layout::Block};
}

Writer::~Writer() {
    if (!m_Finished && !m_Failed) {
        Flush();
    }
}

void Writer::Fail(ErrorCode code) {
    if (!m_Failed) {
        m_Failed = true;
        m_Error = Error {.Code = code};
    }
}

void Writer::Flush() {
    if (m_Size == 0 || m_Failed) {
        m_Size = 0;
        return;
    }
    if (!m_Output.Write(m_Buffer.data(), m_Size)) {
        Fail(ErrorCode::OutputFailed);
    }
    m_Size = 0;
}

void Writer::Emit(std::string_view text) {
    m_Written = true;
    while (!text.empty()) {
        if (m_Size == m_Buffer.size()) {
            Flush();
        }
        usize chunk = std::min(text.size(), m_Buffer.size() - m_Size);
        std::memcpy(m_Buffer.data() + m_Size, text.data(), chunk);
        m_Size += chunk;
        text.remove_prefix(chunk);
    }
}

void Writer::Emit(char c) {
    m_Written = true;
    if (m_Size == m_Buffer.size()) {
        Flush();
    }
    m_Buffer[m_Size++] = c;
}

void Writer::Newline(u32 indent) {
    Emit('\n');
    if (m_Options.UseTabs) {
        for (u32 i = 0; i < indent; ++i) {
            Emit('\t');
        }
    } else {
        for (u32 i = 0; i < indent * m_Options.IndentWidth; ++i) {
            Emit(' ');
        }
    }
}

// Whitespace before a key / list item.
void Writer::Separator(Frame &frame) {
    if (frame.Mode == Layout::Inline) {
        if (frame.Kind == FrameKind::Object || frame.Count > 0) {
            Emit(' ');
        }
    } else if (m_Written) {
        Newline(frame.Indent);
    }
}

bool Writer::BeforeValue(bool container) {
    if (m_Failed) {
        return false;
    }

    Frame &frame = m_Frames[m_Depth];
    if (frame.Kind == FrameKind::List) {
        if (frame.Mode == Layout::Auto) {
            frame.Mode = container ? Layout::Block : Layout::Inline;
        }
        Separator(frame);
    } else {
        if (!frame.AwaitingValue) {
            Fail(ErrorCode::InvalidWriterState);
            return false;
        }
        frame.AwaitingValue = false;
        Emit(' ');
    }
    ++frame.Count;
    return true;
}

bool Writer::Push(FrameKind kind, Layout mode, bool braceless) {
    if (m_Depth + 1 >= m_Frames.size()) {
        Fail(ErrorCode::NestingTooDeep);
        return false;
    }
    const Frame &parent = m_Frames[m_Depth];
    m_Frames[++m_Depth] = Frame {
        .Kind = kind,
        .Mode = mode,
        .Braceless = braceless,
        .Indent = braceless ? parent.Indent : parent.Indent + 1,
    };
    return true;
}


Writer &Writer::Key(std::string_view key) {
    if (m_Failed) {
        return *this;
    }

    Frame &frame = m_Frames[m_Depth];
    bool   valid = frame.Kind != FrameKind::List && !frame.AwaitingValue;
    if (!valid) {
        Fail(ErrorCode::InvalidWriterState);
        return *this;
    }

    Separator(frame);
    WriteString(key, true);
    frame.AwaitingValue = true;
    return *this;
}

Writer &Writer::String(std::string_view value) {
    if (BeforeValue(false)) {
        WriteString(value, false);
    }
    return *this;
}

Writer &Writer::Bool(bool value) {
    if (BeforeValue(false)) {
        Emit(value ? std::string_view("true") : std::string_view("false"));
    }
    return *this;
}

Writer &Writer::Literal(std::string_view text) {
    if (BeforeValue(false)) {
        Emit(text);
    }
    return *this;
}

Writer &Writer::Int(i64 value) {
    if (BeforeValue(false)) {
        std::array<char, 32> buffer {};
        auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        Emit(std::string_view(buffer.data(), static_cast<usize>(end - buffer.data())));
    }
    return *this;
}

Writer &Writer::UInt(u64 value) {
    if (BeforeValue(false)) {
        std::array<char, 32> buffer {};
        auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        Emit(std::string_view(buffer.data(), static_cast<usize>(end - buffer.data())));
    }
    return *this;
}

namespace {

// Shortest round-trip form, always with a '.' or an exponent so it reads
// back as a float-looking number.
template <class T>
std::string_view FormatFloat(T value, std::array<char, 64> &buffer) {
    auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size() - 2, value);
    std::string_view text(buffer.data(), static_cast<usize>(end - buffer.data()));
    if (text.find_first_of(".eE") == std::string_view::npos) {
        *end++ = '.';
        *end++ = '0';
    }
    return {buffer.data(), static_cast<usize>(end - buffer.data())};
}

} // namespace

Writer &Writer::Float(f64 value) {
    if (!std::isfinite(value)) {
        Fail(ErrorCode::NonFiniteNumber);
        return *this;
    }
    if (BeforeValue(false)) {
        std::array<char, 64> buffer {};
        Emit(FormatFloat(value, buffer));
    }
    return *this;
}

Writer &Writer::Float(f32 value) {
    if (!std::isfinite(value)) {
        Fail(ErrorCode::NonFiniteNumber);
        return *this;
    }
    if (BeforeValue(false)) {
        std::array<char, 64> buffer {};
        Emit(FormatFloat(value, buffer));
    }
    return *this;
}


Writer &Writer::BeginObject(Layout layout) {
    if (m_Failed) {
        return *this;
    }

    // The first keyless object at the top level is the root object itself.
    Frame &top = m_Frames[m_Depth];
    if (top.Kind == FrameKind::Root && !top.AwaitingValue && top.Count == 0) {
        top.Count = 1;
        Push(FrameKind::Object, Layout::Block, true);
        return *this;
    }

    Layout parentMode = top.Mode;
    if (!BeforeValue(true)) {
        return *this;
    }
    Layout mode = parentMode == Layout::Inline ? Layout::Inline : (layout == Layout::Inline ? Layout::Inline : Layout::Block);
    Emit('{');
    Push(FrameKind::Object, mode, false);
    return *this;
}

Writer &Writer::EndObject() {
    if (m_Failed) {
        return *this;
    }

    const Frame frame = m_Frames[m_Depth];
    bool        valid = frame.Kind == FrameKind::Object && !frame.AwaitingValue;
    if (!valid) {
        Fail(ErrorCode::InvalidWriterState);
        return *this;
    }
    --m_Depth;

    if (frame.Braceless) {
        return *this;
    }
    if (frame.Mode == Layout::Inline) {
        Emit(frame.Count > 0 ? std::string_view(" }") : std::string_view("}"));
    } else {
        if (frame.Count > 0) {
            Newline(frame.Indent - 1);
        }
        Emit('}');
    }
    return *this;
}

Writer &Writer::BeginList(Layout layout) {
    if (m_Failed) {
        return *this;
    }

    Layout parentMode = m_Frames[m_Depth].Mode;
    if (!BeforeValue(true)) {
        return *this;
    }
    Layout mode = parentMode == Layout::Inline ? Layout::Inline : layout;
    Emit('[');
    Push(FrameKind::List, mode, false);
    return *this;
}

Writer &Writer::EndList() {
    if (m_Failed) {
        return *this;
    }

    const Frame frame = m_Frames[m_Depth];
    if (frame.Kind != FrameKind::List) {
        Fail(ErrorCode::InvalidWriterState);
        return *this;
    }
    --m_Depth;

    if (frame.Mode == Layout::Block && frame.Count > 0) {
        Newline(frame.Indent - 1);
    }
    Emit(']');
    return *this;
}

std::expected<void, Error> Writer::Finish() {
    if (!m_Finished && !m_Failed) {
        const Frame &root = m_Frames[0];
        bool         valid = m_Depth == 0 && !root.AwaitingValue;
        if (!valid) {
            Fail(ErrorCode::InvalidWriterState);
        } else {
            if (m_Written) {
                Emit('\n');
            }
            Flush();
        }
    }
    m_Finished = true;

    if (m_Failed) {
        return std::unexpected(m_Error);
    }
    return {};
}


void Writer::WriteString(std::string_view text, bool key) {
    if (key ? IsIdentifierKey(text) : CanBeBare(text)) {
        Emit(text);
        return;
    }

    if (PrefersRaw(text)) {
        Emit('`');
        Emit(text);
        Emit('`');
        return;
    }

    static constexpr std::string_view hexDigits = "0123456789ABCDEF";

    Emit('"');
    usize plain = 0;
    for (usize i = 0; i < text.size(); ++i) {
        char                c = text[i];
        std::string_view    escape;
        std::array<char, 4> hex {};
        switch (c) {
            case '"':
                escape = "\\\"";
                break;
            case '\\':
                escape = "\\\\";
                break;
            case '\n':
                escape = "\\n";
                break;
            case '\t':
                escape = "\\t";
                break;
            case '\r':
                escape = "\\r";
                break;
            default:
                if (IsControl(c)) {
                    auto u = static_cast<unsigned char>(c);
                    hex = {'\\', 'x', hexDigits[u >> 4], hexDigits[u & 0xF]};
                    escape = std::string_view(hex.data(), hex.size());
                }
                break;
        }
        if (!escape.empty()) {
            Emit(text.substr(plain, i - plain));
            Emit(escape);
            plain = i + 1;
        }
    }
    Emit(text.substr(plain));
    Emit('"');
}

} // namespace MDL
