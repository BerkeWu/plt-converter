// Page-range text such as "1-3,5" -> sorted unique page numbers (1-based). Accepts full-width commas and spaces.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace pagerange {

// Returns {} and fills *err when the text is invalid (or empty: err = "empty"). n = number of pages available.
inline std::vector<int> parse(std::string s, int n, std::string* err = nullptr) {
    auto fail = [&](const char* m) { if (err) *err = m; return std::vector<int>{}; };
    for (size_t pos; (pos = s.find("\xEF\xBC\x8C")) != std::string::npos;) s.replace(pos, 3, ",");  // "，"
    for (size_t pos; (pos = s.find("\xE3\x80\x81")) != std::string::npos;) s.replace(pos, 3, ",");  // "、"
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c == ' ' || c == '\t'; }), s.end());
    if (s.empty()) return fail("empty");
    std::vector<int> out;
    size_t i = 0;
    auto num = [&](int& v) {
        size_t st = i;
        long x = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { x = x * 10 + (s[i] - '0'); if (x > 100000) return false; ++i; }
        v = static_cast<int>(x);
        return i > st;
    };
    while (i < s.size()) {
        int a = 0, b = 0;
        if (!num(a)) return fail("invalid");
        b = a;
        if (i < s.size() && s[i] == '-') { ++i; if (!num(b)) return fail("invalid"); }
        if (a < 1 || b < a || b > n) return fail("out of range");
        for (int k = a; k <= b; ++k) out.push_back(k);
        if (i < s.size()) { if (s[i] != ',') return fail("invalid"); ++i; if (i == s.size()) return fail("invalid"); }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (err) err->clear();
    return out;
}

}  // namespace pagerange
