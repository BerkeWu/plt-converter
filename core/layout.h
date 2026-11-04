// Tiling / page layout engine. Produces renderer-independent page descriptions (all units mm,
// origin top-left of the page, Y pointing down). Renderers: pdf_min (dev) and the Qt exporter.
#pragma once
#include <string>
#include <vector>

#include "plt.h"

namespace layout {

using plt::Pt;

struct Paper { std::string name; double w, h; };  // portrait size in mm
Paper paperByName(const std::string& name);       // "A0" | "A3" | "A4" (throws on unknown)

enum class Orient { Portrait, Landscape };
inline const char* orientChar(Orient o) { return o == Orient::Landscape ? "横" : "竖"; }

struct Params {
    Paper paper{"A4", 210, 297};
    Orient orient = Orient::Landscape;
    double margin = 5;       // mm: nothing is printed closer than this to the sheet edge
    double overlap = 10;     // mm: width of the strip printed OUTSIDE the registration frame (frame inset = max(overlap, margin))
    double offX = 0, offY = 0;  // graph offset inside the page grid (mm, >= 0)
    bool pageNumbers = true;
    bool calibBox = true;    // 5x5 cm square, first page only
    double calibSize = 50;
    bool alignMarks = true;  // registration frame: 4 dashed lines edge to edge + one "+" per frame corner
    bool overview = false;   // 「添加拼接效果图」: scaled overview page placed first, no page number
    bool skipEmpty = true;   // do not output pages without any line
    double lineWidth = 0.25;
    double alignR = 0.145, alignG = 0.388, alignB = 0.922;  // alignment marks: blue (#2563EB) by default
    bool useOriginalColors = false;  // use the colours the PLT assigns to pens (PC); pens without a colour use lineR/G/B
    double lineR = 0, lineG = 0, lineB = 0;  // pattern line colour (helper lines / page numbers stay black)
    std::string fileTag;     // batch: short file name printed next to the page number (UTF-8, empty = off)
    bool singlePage = false;  // whole drawing on ONE page (page = drawing + 2*margin); no overlap/page number/align marks
    std::string topMark = "好样纸样plt转换器";
    std::string bottomMark = "打印时，打印比例请选择「按实际」或「100%」";
    std::string overviewTitle = "拼接效果图";
    std::string overviewNote = "本页为缩小示意图（非 1:1），无页码，仅用于查看拼接关系";
};

struct Style { double widthMm = 0.25; double r = 0, g = 0, b = 0; bool dashed = false; double dashMm = 0, gapMm = 0; /* 0 = renderer default */ };
struct Path { std::vector<Pt> pts; Style style; };
// Page-number box geometry shared by every renderer: the border is drawn kBoxPadMm outside the text, whose cap height is kCapRatio * font size.
constexpr double kBoxPadMm = 2.0, kCapRatio = 0.75, kPtMm = 0.3528;
struct Text {
    std::string utf8; double x = 0, y = 0, sizePt = 8; bool center = false; double r = 0, g = 0, b = 0;
    bool bold = false, boxed = false; double alpha = 1;
};

struct Page {
    double w = 0, h = 0;
    std::string label;          // "1-1" (row-col); empty for the overview page
    int row = -1, col = -1;
    bool overview = false;
    bool empty = false;         // contains no pattern line
    std::vector<Path> paths;    // pattern + helper lines
    std::vector<Text> texts;
};

struct Plan {
    double pageW = 0, pageH = 0;       // oriented paper size
    double Wp = 0, Hp = 0;             // registration frame size = tile step (frames of neighbouring pages abut)
    double inset = 0;                  // sheet edge -> frame line
    double strip = 0;                  // pattern printed outside the frame on each side (inset - margin)
    int nx = 0, ny = 0;                // grid size
    int gridPages = 0;                 // nx * ny
    int blankPages = 0;
    int effectivePages = 0;            // non-empty pages (what the user must print/assemble)
    bool calibCollision = false;
    std::vector<Page> pages;           // output order (overview first if enabled); skipEmpty applied
};

// Tiling geometry. The registration frame ("拼接框") sits max(overlap, margin) inside the sheet edge; frames of neighbouring
// pages abut (tile step = frame size).  Pattern lines are also printed in the strip outside the frame, up to `margin` from the
// edge, so laying a sheet over its neighbour with the dashed frame lines coincident shows continuous pattern lines.
// With the defaults (margin 5, overlap 10) the page counts equal the old "windows overlapping by 10 mm" model.
struct TileGeom {
    double pageW = 0, pageH = 0, inset = 0, strip = 0, fw = 0, fh = 0;
    int nx = 1, ny = 1;
    bool ok = false;   // false: margin / overlap leave no usable frame on this paper
};
TileGeom tileGeometry(const Params& p, double drawingW, double drawingH);  // uses p.offX / p.offY

Plan buildPlan(const plt::Document& doc, const Params& p);

struct OrientChoice { Orient best; int portraitPages, landscapePages; };
// "Most paper-saving" = fewer effective pages; ties -> fewer seams, less blank area, then portrait.
OrientChoice chooseOrientation(const plt::Document& doc, Params p);

struct OffsetChoice { double x = 0, y = 0; int pages = 0; };
// Tries {top-left, centre, bottom-right} anchoring per axis (9 combos) and returns the one with the fewest effective pages
// (ties keep the earlier = more top-left candidate).
OffsetChoice autoOffset(const plt::Document& doc, Params p);

struct ShiftHint { bool found = false; double dx = 0, dy = 0; int from = 0, to = 0; };
// Smallest translation (<= maxShift mm per axis, offsets stay >= 0) that lowers the grid page count; dx<0 means "move left".
ShiftHint shiftHint(const plt::Document& doc, const Params& p, double maxShift = 20);

// Pure helpers (also used by tests)
int tilesNeeded(double extent, double printable, double overlap);
bool clipSegment(double& x1, double& y1, double& x2, double& y2, double xmin, double ymin, double xmax, double ymax);

}  // namespace layout
