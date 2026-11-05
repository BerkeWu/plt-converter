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

// A group of strokes remembered for later reuse (stored in recipes).  Coordinates are PDF points of the page it was taken from;
// matching is shape-based (position, size, mirror and quarter turns do not matter).
struct ShapeTemplate { std::string name; std::vector<std::vector<std::pair<double, double>>> strokes; };
// Takes the strokes inside `sel` on `page` as a template (nothing is written).
ShapeResult extractShape(const std::filesystem::path& in, int page, const Rect& sel, ShapeTemplate& tpl);
// Removes every copy of the template found in the file.
ShapeResult removeShapeTemplate(const std::filesystem::path& in, const std::filesystem::path& out, const ShapeTemplate& tpl);

// Real text (Tj/TJ).  `listText` gives the distinct strings (decoded to UTF-8 via ToUnicode where the font has one).
struct TextItem { std::string text; int pages = 0, count = 0; };
std::vector<TextItem> listText(const std::filesystem::path& in, std::string* error = nullptr);
struct TextResult { bool ok = false; std::string error; int removed = 0; std::vector<int> pagesTouched; };
// Removes every text-show whose decoded string contains one of `needles` (case-sensitive substring).
TextResult removeText(const std::filesystem::path& in, const std::filesystem::path& out, const std::vector<std::string>& needles);

// ---- vector preview (no rasteriser needed: these PDFs consist of strokes, fills and text) ----
struct DrawItem {
    enum Kind { Stroke, Fill, Text, ImageBox } kind = Stroke;
    std::vector<std::pair<double, double>> pts;   // page points, origin top-left, y down (Text: pts[0] = baseline start)
    float r = 0, g = 0, b = 0, a = 1;
    double lineWidth = 0.5;
    std::string text;                              // Text, UTF-8 (empty when the font has no usable mapping)
    double size = 0, angle = 0;                    // Text: font size in points, rotation in radians (counter-clockwise on screen)
    int glyphs = 0;                                // Text: number of characters shown
};
struct PageDrawing { bool ok = false; std::string error; double w = 0, h = 0; int pageCount = 0; std::vector<DrawItem> items; };
PageDrawing drawPage(const std::filesystem::path& in, int page /*1-based*/);

// ---- batch: one recipe applied to many files of the same watermark type ----
constexpr int kMaxBatchFiles = 20;

struct Recipe {
    std::string name;
    bool removeDetected = true;                 // remove every finding of tier High
    std::vector<std::string> texts;             // real text to remove (substring)
    std::vector<ShapeTemplate> shapes;          // stroke-drawn text / logos to remove
    bool verify = true;                         // stroke count must come out as expected, otherwise no output file
    std::string signature;                      // what the recipe was made from (see signatureOf); empty = anything
};
// Short description of the High-tier findings, e.g. "斜铺文字+联系方式|同层店铺信息栏".  Files with the same signature form one group.
std::string signatureOf(const Report&);
// 0..100: how well a file's detections match the recipe (extra or missing findings lower it).
int conformity(const Report&, const Recipe&);
// Group index per report (0,1,2,... by first appearance); -1 = nothing found, -2 = only unsupported (bitmap) findings, -3 = unreadable.
std::vector<int> groupReports(const std::vector<Report>&, std::vector<std::string>* groupSignatures = nullptr);

bool saveRecipe(const std::filesystem::path& file, const Recipe&, std::string* error = nullptr);
bool loadRecipe(const std::filesystem::path& file, Recipe&, std::string* error = nullptr);

// Number of stroked paths on the pages (used for the integrity check).
long countStrokes(const std::filesystem::path& pdf);

struct FileResult {
    bool ok = false;               // output written and verified
    std::string error;
    RemoveResult detected;
    int textsRemoved = 0, shapesRemoved = 0;
    long strokesBefore = 0, strokesAfter = 0, strokesExpected = 0;
    bool verified = false;
    bool nothingToDo = false;      // the file simply contains nothing the recipe removes (not an error)
    int removedTotal() const { return detected.formsRemoved + detected.textBlocksRemoved + detected.fillsRemoved + detected.blockItemsRemoved + textsRemoved + shapesRemoved; }
};
// Applies the recipe to one file.  With recipe.verify the output is deleted again when the stroke count is not what it should be.
// `alsoRemove`: indices (into analyze(in).findings) of "需确认" findings the user ticked for this very file.
FileResult applyRecipe(const std::filesystem::path& in, const std::filesystem::path& out, const Recipe&, const std::vector<int>& alsoRemove = {});

}  // namespace wm
