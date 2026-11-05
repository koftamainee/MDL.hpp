#pragma once

#include <MDL/Config.h>
#include <MDL/Error.h>
#include <MDL/Types.h>

#include <array>
#include <concepts>
#include <expected>
#include <string_view>
#include <type_traits>


namespace MDL {

// Where written text goes. The writer buffers internally and calls WriteFn
// with large chunks, so this can be a VFS file, a socket, a string, ...
struct Output final {
    void *User = nullptr;
    bool (*WriteFn)(void *user, const char *data, usize size) = nullptr;


    bool Write(const char *data, usize size) const { return WriteFn(User, data, size); }

    // Appends to std::string / std::pmr::string / anything with append(ptr, size).
    template <class S>
        requires requires(S &s, const char *p, usize n) { s.append(p, n); }
    static Output ToString(S &str) {
        return Output {
            .User = &str,
            .WriteFn = [](void *user, const char *data, usize size) {
                static_cast<S *>(user)->append(data, size);
                return true;
            },
        };
    }

    // callable(std::string_view chunk) returning bool (false = failure) or void.
    template <class F>
        requires std::invocable<F &, std::string_view>
    static Output From(F &callable) {
        return Output {
            .User = &callable,
            .WriteFn = [](void *user, const char *data, usize size) {
                F &fn = *static_cast<F *>(user);
                if constexpr (std::is_void_v<std::invoke_result_t<F &, std::string_view>>) {
                    fn(std::string_view(data, size));
                    return true;
                } else {
                    return static_cast<bool>(fn(std::string_view(data, size)));
                }
            },
        };
    }
};


enum class Layout : u8 {
    Auto,   // objects: one member per line; lists: inline for scalars, otherwise one item per line
    Block,  // one entry per line
    Inline, // everything on one line: { a 1 b 2 } / [a b c]
};

struct WriteOptions final {
    u32  IndentWidth = 2;
    bool UseTabs = false;

    // Reflection/DOM writing puts objects of plain scalars that are list
    // items on one line if they have at most this many members.
    u32 MaxInlineMembers = 8;
};


// Streaming writer. The top level is an object without braces:
//
//   MDL::Writer w(MDL::Output::ToString(text));
//   w.Field("shader", "pbr");
//   w.BeginObject("pipeline");
//   w.Field("depth_test", true);
//   w.EndObject();
//   w.BeginList("tags").Item("sword").Item("shield").EndList();
//   auto result = w.Finish();
//
// Calls made in a wrong order put the writer into an error state, which is
// reported by Finish(). After an error every call is a no-op.
class Writer final {
  public:
    explicit Writer(Output output, const WriteOptions &options = {});
    ~Writer();

    Writer(const Writer &) = delete;
    Writer(Writer &&) = delete;
    Writer &operator=(const Writer &) = delete;
    Writer &operator=(Writer &&) = delete;

    Writer &Key(std::string_view key);

    Writer &String(std::string_view value);
    Writer &Bool(bool value);
    Writer &Int(i64 value);
    Writer &UInt(u64 value);
    Writer &Float(f64 value);
    Writer &Float(f32 value);

    template <class T>
        requires std::is_arithmetic_v<T>
    Writer &Number(T value) {
        if constexpr (std::is_same_v<T, bool>) {
            return Bool(value);
        } else if constexpr (std::is_same_v<T, f32>) {
            return Float(value);
        } else if constexpr (std::is_floating_point_v<T>) {
            return Float(static_cast<f64>(value));
        } else if constexpr (std::is_signed_v<T>) {
            return Int(static_cast<i64>(value));
        } else {
            return UInt(static_cast<u64>(value));
        }
    }

    Writer &BeginObject(Layout layout = Layout::Auto);
    Writer &BeginObject(std::string_view key, Layout layout = Layout::Auto) { return Key(key).BeginObject(layout); }
    Writer &EndObject();

    Writer &BeginList(Layout layout = Layout::Auto);
    Writer &BeginList(std::string_view key, Layout layout = Layout::Auto) { return Key(key).BeginList(layout); }
    Writer &EndList();

    // Key + value. T is anything MDL::Serde supports (scalars, strings, enums,
    // containers, reflected structs, MDL::Value). Needs <MDL/Serde.h>.
    template <class T>
    Writer &Field(std::string_view key, const T &value);

    // List item, same types as Field.
    template <class T>
    Writer &Item(const T &value);

    // Writes number/bool text as is. The text must be a valid MDL number or bool.
    Writer &Literal(std::string_view text);

    std::expected<void, Error> Finish();

    bool                HasError() const { return m_Failed; }
    bool                InList() const { return m_Frames[m_Depth].Kind == FrameKind::List; }
    bool                InInline() const { return m_Frames[m_Depth].Mode == Layout::Inline; }
    const WriteOptions &Options() const { return m_Options; }

  private:
    enum class FrameKind : u8 {
        Root,
        Object,
        List,
    };

    struct Frame final {
        FrameKind Kind = FrameKind::Root;
        Layout    Mode = Layout::Block;
        bool      Braceless = false;
        bool      AwaitingValue = false;
        u32       Indent = 0;
        u32       Count = 0;
    };

    bool BeforeValue(bool container);
    bool Push(FrameKind kind, Layout mode, bool braceless);
    void Separator(Frame &frame);
    void Newline(u32 indent);
    void WriteString(std::string_view text, bool key);
    void Emit(std::string_view text);
    void Emit(char c);
    void Flush();
    void Fail(ErrorCode code);

    Output       m_Output;
    WriteOptions m_Options;

    std::array<Frame, MDL_MAX_DEPTH + 2> m_Frames {};
    u32                                  m_Depth = 0;

    std::array<char, 4096> m_Buffer {};
    usize                  m_Size = 0;

    bool  m_Written = false;
    bool  m_Failed = false;
    bool  m_Finished = false;
    Error m_Error {};
};

} // namespace MDL
