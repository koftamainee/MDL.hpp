#pragma once

#include <MDL/MDL.h>

#include <gtest/gtest.h>

#include <format>
#include <fstream>
#include <sstream>
#include <string>


namespace MDL::Tests {

inline std::string ReadAsset(std::string_view name) {
    std::ifstream     file(std::string(MDL_TEST_ASSETS) + "/" + std::string(name), std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline Document MustParse(std::string_view source, const ParseOptions &options = {}) {
    auto doc = Parse(source, options);
    if (!doc) {
        ADD_FAILURE() << "parse failed: " << std::format("{}", doc.error());
        return {};
    }
    return std::move(*doc);
}

inline Error MustFail(std::string_view source, const ParseOptions &options = {}) {
    auto doc = Parse(source, options);
    if (doc) {
        ADD_FAILURE() << "parse unexpectedly succeeded: " << source;
        return {};
    }
    return doc.error();
}

} // namespace MDL::Tests
