#pragma once

#include <MDL/Types.h>

#include <format>
#include <string_view>


namespace MDL {

enum class ErrorCode : u8 {
    // parsing
    InvalidUtf8,
    SourceTooLarge,
    UnexpectedCharacter,
    InvalidKey,
    ExpectedValue,
    UnterminatedString,
    InvalidEscape,
    DuplicateKey,
    UnclosedObject,
    UnclosedList,
    NestingTooDeep,
    OutOfMemory,

    // reading values
    MissingKey,
    IndexOutOfRange,
    TypeMismatch,
    OutOfRange,
    InvalidEnum,
    UnknownKey,
    SizeMismatch,

    // writing
    NonFiniteNumber,
    InvalidWriterState,
    OutputFailed,
};

struct Error final {
    ErrorCode Code {};
    Position  Pos {};

    // NOTE(koftamainee): only ever points to static storage (reflected field
    // names), so it is safe to keep the error after the document is gone.
    std::string_view Context {};
};

std::string_view ToString(ErrorCode code);

} // namespace MDL


template <>
struct std::formatter<MDL::Error> : std::formatter<std::string_view> {
    template <class FormatContext>
    auto format(const MDL::Error &error, FormatContext &ctx) const {
        auto out = ctx.out();
        if (error.Pos.Line != 0) {
            out = std::format_to(out, "{}:{}: ", error.Pos.Line, error.Pos.Column);
        }
        out = std::format_to(out, "{}", MDL::ToString(error.Code));
        if (!error.Context.empty()) {
            out = std::format_to(out, " '{}'", error.Context);
        }
        return out;
    }
};
