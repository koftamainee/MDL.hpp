#include <MDL/Error.h>

#include <MDL/Assert.h>

#include <cstdio>
#include <cstdlib>


namespace MDL {

std::string_view ToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidUtf8:
            return "invalid UTF-8";
        case ErrorCode::SourceTooLarge:
            return "source is too large";
        case ErrorCode::UnexpectedCharacter:
            return "unexpected character";
        case ErrorCode::InvalidKey:
            return "invalid key";
        case ErrorCode::ExpectedValue:
            return "expected a value";
        case ErrorCode::UnterminatedString:
            return "unterminated string";
        case ErrorCode::InvalidEscape:
            return "invalid escape sequence";
        case ErrorCode::DuplicateKey:
            return "duplicate key";
        case ErrorCode::UnclosedObject:
            return "unclosed '{'";
        case ErrorCode::UnclosedList:
            return "unclosed '['";
        case ErrorCode::NestingTooDeep:
            return "nesting is too deep";
        case ErrorCode::OutOfMemory:
            return "out of memory";
        case ErrorCode::MissingKey:
            return "missing key";
        case ErrorCode::IndexOutOfRange:
            return "index out of range";
        case ErrorCode::TypeMismatch:
            return "type mismatch";
        case ErrorCode::OutOfRange:
            return "number out of range";
        case ErrorCode::InvalidEnum:
            return "invalid enum value";
        case ErrorCode::UnknownKey:
            return "unknown key";
        case ErrorCode::SizeMismatch:
            return "wrong number of items";
        case ErrorCode::NonFiniteNumber:
            return "number is not finite";
        case ErrorCode::InvalidWriterState:
            return "invalid writer call order";
        case ErrorCode::OutputFailed:
            return "output failed";
    }
    return "unknown error";
}


namespace Detail {

void AssertFailed(const char *expr, const char *file, int line) {
    std::fprintf(stderr, "MDL assertion failed: %s\nat %s:%d\n", expr, file, line); // NOLINT
    std::abort();
}

} // namespace Detail
} // namespace MDL
