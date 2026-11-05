#pragma once

#include <MDL/Allocator.h>
#include <MDL/Config.h>
#include <MDL/Detail/Arena.h>
#include <MDL/Error.h>
#include <MDL/Value.h>

#include <expected>
#include <span>
#include <string_view>


namespace MDL {

struct ParseOptions final {
    // Where the document memory comes from.
    Allocator Alloc = Allocator::Default();

    // Optional memory used before anything is requested from Alloc (for
    // example a stack buffer). Must outlive the Document.
    std::span<std::byte> Buffer {};

    // true  - the source is copied into the document, so it can be freed right
    //         after parsing
    // false - strings point into the source, which must outlive the Document
    bool CopySource = true;

    bool ValidateUtf8 = true;

    u32 MaxDepth = MDL_MAX_DEPTH;
};


// Immutable parsed file. All nodes and decoded strings live in one arena that
// is released with the document. Safe to read from several threads at once.
class Document final {
  public:
    Document() = default;
    ~Document() = default;

    Document(const Document &) = delete;
    Document(Document &&other) noexcept;
    Document &operator=(const Document &) = delete;
    Document &operator=(Document &&other) noexcept;

    // Top level object. Values stay valid when the document is moved.
    Value Root() const { return Value(m_Root); }

    Value operator[](std::string_view key) const { return Root().Find(key); }

    // Bytes taken from the arena / reserved from the allocator and buffer.
    usize MemoryUsed() const { return m_Arena.BytesUsed(); }
    usize MemoryReserved() const { return m_Arena.BytesReserved(); }

  private:
    friend std::expected<Document, Error> Parse(std::string_view source, const ParseOptions &options);

    Detail::BlockArena  m_Arena;
    const Detail::Node *m_Root = nullptr;
};


[[nodiscard]] std::expected<Document, Error> Parse(std::string_view source, const ParseOptions &options = {});

} // namespace MDL
