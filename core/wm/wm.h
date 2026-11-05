// PDF watermark detection / removal (vector watermarks only). Depends on QPDF, not on Qt.
// Scope of this version: (1) stand-alone "artifact" objects (a Form XObject stamped on every page),
// (2) flattened diagonal text watermarks whose overlap with pattern lines was baked into mixed colours,
// (3) shop information banners drawn in the pattern's own layer (contact text on a coloured block, icons, QR code).
// Bitmap pages are reported but never touched.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace wm {

// UTF-8 <-> path helpers that compile in C++17 and C++20 (QPDF 12 headers need C++20, where u8string() returns char8_t).
inline std::string toU8(const std::filesystem::path& p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }
inline std::filesystem::path fromU8(const std::string& s) {
#if (defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}

enum class Kind { ArtifactObject, FlatText, PageStamp, Bitmap, LayerBlock };   // LayerBlock: shop banner drawn in the pattern's own layer
enum class Tier { High, Confirm, Keep, Unsupported };   // High = removed by default

struct Finding {
    Kind kind{};
    Tier tier = Tier::Keep;
    int score = 0;                 // 0..100
    std::string label;             // UTF-8, for the UI / report
    int pages = 0;                 // pages on which it occurs
    int objects = 0;               // drawing objects (form instances / text blocks)
    bool contactText = false;      // watermark text looks like contact info (reported as a flag only, never the text itself)
    std::vector<std::string> evidence;   // "reason +points" lines, UTF-8
    // internal plan data
    std::string formKey;           // ArtifactObject: group key
    double alpha = 0;              // FlatText: fitted mixing ratio
    std::vector<double> lightColour;                       // FlatText: colour of the watermark glyphs over white
    std::map<std::string, std::vector<double>> restore;    // FlatText: blended colour key -> original colour
};

struct Report {
    int pageCount = 0;
    bool ok = false;
    std::string error;
    std::vector<Finding> findings;
    bool hasRemovable() const;
};

struct RemoveResult {
    bool ok = false;
    std::string error;
    int formsRemoved = 0, textBlocksRemoved = 0, fillsRemoved = 0, coloursRestored = 0, blockItemsRemoved = 0;
};

Report analyze(const std::filesystem::path& in);
// Removes every finding with tier High, plus the ones whose index is listed in `alsoRemove` (user confirmed).
RemoveResult remove(const std::filesystem::path& in, const std::filesystem::path& out,
                    const std::vector<int>& alsoRemove = {});

std::string toJson(const Report&);

// ---- user-specified removal (the pattern layer itself) ----
// Selection rectangle in PDF points as a viewer shows the page: origin at the top-left corner, y pointing down.
struct Rect { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct ShapeResult {
    bool ok = false;
    std::string error;
    int selected = 0;      // strokes inside the selection on the chosen page
    int removed = 0;       // strokes removed in total (selection + copies)
    int copies = 0;        // extra copies found on other places / pages
    std::vector<int> pagesTouched;   // 1-based
};
// Characters drawn as line strokes (CAD "single-stroke" fonts) are not text.  The user boxes one instance; every stroke
// inside the box on `page` is removed, and so is every identical copy of that stroke group elsewhere (same shape and
// size, upright or turned by 90/180/270 degrees) unless `everywhere` is false.
ShapeResult removeShape(const std::filesystem::path& in, const std::filesystem::path& out, int page, const Rect& sel, bool everywhere = true);

// Real text (Tj/TJ).  `listText` gives the distinct strings (decoded to UTF-8 via ToUnicode where the font has one).
struct TextItem { std::string text; int pages = 0, count = 0; };
std::vector<TextItem> listText(const std::filesystem::path& in, std::string* error = nullptr);
struct TextResult { bool ok = false; std::string error; int removed = 0; std::vector<int> pagesTouched; };
// Removes every text-show whose decoded string contains one of `needles` (case-sensitive substring).
TextResult removeText(const std::filesystem::path& in, const std::filesystem::path& out, const std::vector<std::string>& needles);

}  // namespace wm
