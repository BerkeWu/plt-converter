// Output file naming: {stem}_{paper}{orient}.pdf, auto-numbered _2, _3 ... when the name exists. Never overwrites.
#pragma once
#include <filesystem>
#include <functional>
#include <string>

#include "layout.h"

namespace naming {

inline std::string sanitize(std::string s) {
    for (auto& c : s) if (std::string("<>:\"/\\|?*").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) c = '_';
    return s;
}

// `exists` lets callers (and tests) inject the collision check; `reserved` supports batch pre-allocation.
// {stem}_{tag}.pdf with _2, _3 ... on collision (tag e.g. "A4横" or "单页").
inline std::string uniquePdfNameTagged(const std::string& stem, const std::string& tag,
                                       const std::function<bool(const std::string&)>& exists) {
    std::string base = sanitize(stem) + "_" + tag;
    std::string name = base + ".pdf";
    for (int n = 2; exists(name); ++n) name = base + "_" + std::to_string(n) + ".pdf";
    return name;
}

inline std::string uniquePdfName(const std::string& stem, const std::string& paper, layout::Orient o,
                                 const std::function<bool(const std::string&)>& exists) {
    return uniquePdfNameTagged(stem, paper + layout::orientChar(o), exists);
}

// stem / paper are UTF-8; the result is a path object (never round-trips through the ANSI code page).
inline std::filesystem::path uniquePdfPath(const std::filesystem::path& dir, const std::string& stem, const std::string& paper,
                                           layout::Orient o) {
    auto name = uniquePdfName(stem, paper, o, [&](const std::string& n) { return std::filesystem::exists(dir / std::filesystem::u8path(n)); });
    return dir / std::filesystem::u8path(name);
}

}  // namespace naming
