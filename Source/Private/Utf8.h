#pragma once

#include <MDL/Types.h>

#include <cstring>
#include <string_view>


namespace MDL::Detail {

// Returns the offset of the first invalid byte, or text.size() if the text is
// valid UTF-8 (no overlongs, no surrogates, nothing above U+10FFFF).
inline usize ValidateUtf8(std::string_view text) {
    const auto *data = reinterpret_cast<const u8 *>(text.data());
    usize       size = text.size();
    usize       i = 0;

    while (i < size) {
        // ASCII fast path, 8 bytes at a time
        while (i + 8 <= size) {
            u64 chunk = 0;
            std::memcpy(&chunk, data + i, sizeof(chunk));
            if ((chunk & 0x8080808080808080ULL) != 0) {
                break;
            }
            i += 8;
        }
        if (i >= size) {
            break;
        }

        u8 c = data[i];
        if (c < 0x80) {
            ++i;
            continue;
        }

        usize length = 0;
        u8    lo = 0x80;
        u8    hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) {
            length = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            length = 3;
            if (c == 0xE0) {
                lo = 0xA0; // overlong
            } else if (c == 0xED) {
                hi = 0x9F; // surrogates
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            length = 4;
            if (c == 0xF0) {
                lo = 0x90; // overlong
            } else if (c == 0xF4) {
                hi = 0x8F; // > U+10FFFF
            }
        } else {
            return i;
        }

        if (i + length > size) {
            return i;
        }
        if (data[i + 1] < lo || data[i + 1] > hi) {
            return i;
        }
        for (usize k = 2; k < length; ++k) {
            if ((data[i + k] & 0xC0) != 0x80) {
                return i;
            }
        }
        i += length;
    }

    return size;
}

// Appends the UTF-8 encoding of a valid code point, returns the byte count.
inline usize EncodeUtf8(u32 cp, char *out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

} // namespace MDL::Detail
