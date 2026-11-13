#include "layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace layout {

Paper paperByName(const std::string& n) {
    if (n == "A0") return {"A0", 841, 1189};
    if (n == "A3") return {"A3", 297, 420};
    if (n == "A4") return {"A4", 210, 297};
    throw std::runtime_error("unknown paper: " + n);
}

int tilesNeeded(double extent, double printable, double overlap) {
    if (extent <= printable + 1e-9) return 1;
    double step = printable - overlap;
    return 1 + static_cast<int>(std::ceil((extent - printable) / step - 1e-9));
}

TileGeom tileGeometry(const Params& p, double w, double h) {
    TileGeom g;
    const bool portrait = p.orient == Orient::Portrait;
    g.pageW = portrait ? p.paper.w : p.paper.h;
    g.pageH = portrait ? p.paper.h : p.paper.w;
    g.inset = std::max(p.overlap, p.margin);
    g.strip = g.inset - p.margin;
    g.fw = g.pageW - 2 * g.inset;
    g.fh = g.pageH - 2 * g.inset;
    g.ok = g.fw > 20 && g.fh > 20;
    if (g.ok) {
        // frame k covers u in [strip + k*fw, strip + (k+1)*fw]; the strips outside the first and last frame are printed too,
        // so n pages reach from u = 0 to n*fw + 2*strip
        g.nx = std::max(1, static_cast<int>(std::ceil((p.offX + w - 2 * g.strip) / g.fw - 1e-9)));
        g.ny = std::max(1, static_cast<int>(std::ceil((p.offY + h - 2 * g.strip) / g.fh - 1e-9)));
    }
    return g;
}

// Liang-Barsky. Returns false if the segment is fully outside; otherwise clips in place.
bool clipSegment(double& x1, double& y1, double& x2, double& y2, double xmin, double ymin, double xmax, double ymax) {
    double dx = x2 - x1, dy = y2 - y1, t0 = 0, t1 = 1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x1 - xmin, xmax - x1, y1 - ymin, ymax - y1};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) { if (q[i] < 0) return false; }
        else {
            double r = q[i] / p[i];
            if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
            else { if (r < t0) return false; if (r < t1) t1 = r; }
        }
    }
    double nx1 = x1 + t0 * dx, ny1 = y1 + t0 * dy, nx2 = x1 + t1 * dx, ny2 = y1 + t1 * dy;
    x1 = nx1; y1 = ny1; x2 = nx2; y2 = ny2;
    return true;
}

namespace {

const double kSafe = 4;  // mm: watermark / page-number inset when margin is smaller

// Colour of one pen. Near-white pen colours would vanish on white paper, so they print as light grey.
Style penStyleFor(const Params& p, const plt::Document& d, int pen);
Style penStyle(const Params& p) { Style s; s.widthMm = p.lineWidth; s.r = p.lineR; s.g = p.lineG; s.b = p.lineB; return s; }
Style penStyleFor(const Params& p, const plt::Document& d, int pen) {
    Style s = penStyle(p);
    if (!p.useOriginalColors) return s;
    auto it = d.penColors.find(pen);
    if (it == d.penColors.end()) return s;
    double r = it->second.r, g = it->second.g, b = it->second.b;
    if (0.299 * r + 0.587 * g + 0.114 * b > 0.9) r = g = b = 0.75;
    s.r = r; s.g = g; s.b = b;
    return s;
}
Style thin(double w, double g, bool dashed = false) { Style s; s.widthMm = w; s.r = s.g = s.b = g; s.dashed = dashed; return s; }

// Clip one polyline to a window; emits sub-paths in window-local coords shifted by (ox, oy).
void clipPolyline(const std::vector<Pt>& in, double xmin, double ymin, double xmax, double ymax, double ox, double oy,
                  const Style& st, std::vector<Path>& out) {
    Path cur;
    cur.style = st;
    auto flush = [&] { if (cur.pts.size() >= 2) out.push_back(cur); cur.pts.clear(); };
    for (size_t i = 1; i < in.size(); ++i) {
        double x1 = in[i - 1].x, y1 = in[i - 1].y, x2 = in[i].x, y2 = in[i].y;
        if (!clipSegment(x1, y1, x2, y2, xmin, ymin, xmax, ymax)) { flush(); continue; }
        if (std::hypot(x2 - x1, y2 - y1) < 1e-6) continue;  // avoid sub-0.001mm fragments
        Pt a{x1 + ox, y1 + oy}, b{x2 + ox, y2 + oy};
        if (!cur.pts.empty() && (std::fabs(cur.pts.back().x - a.x) > 1e-7 || std::fabs(cur.pts.back().y - a.y) > 1e-7)) flush();
        if (cur.pts.empty()) cur.pts.push_back(a);
        cur.pts.push_back(b);
    }
    flush();
}

bool pathsHitRect(const std::vector<Path>& paths, double x0, double y0, double x1, double y1) {
    for (const auto& pa : paths)
        for (size_t i = 1; i < pa.pts.size(); ++i) {
            double a = pa.pts[i - 1].x, b = pa.pts[i - 1].y, c = pa.pts[i].x, d = pa.pts[i].y;
            if (clipSegment(a, b, c, d, x0, y0, x1, y1)) return true;
        }
    return false;
}

void addText(Page& pg, const std::string& s, double x, double y, double size, bool center, double g = 0.35) {
    Text t; t.utf8 = s; t.x = x; t.y = y; t.sizePt = size; t.center = center; t.r = t.g = t.b = g;
    pg.texts.push_back(t);
}

// Page number: 3x the old 10 pt, bold, framed, 80% opacity.
const double kNumPt = 30, kNumAlpha = 0.8;
const double kLift = 1.5;  // bottom watermark / page number sit this far above the dashed alignment line
double numberBoxW(const std::string& label) { return label.size() * 0.62 * kNumPt * kPtMm + 2 * kBoxPadMm; }  // estimate, only for collision tests
double numberBoxH() { return kCapRatio * kNumPt * kPtMm + 2 * kBoxPadMm; }
// Box of the page number in page coordinates (landscape: top-left of the printable area, portrait: bottom centre).
void numberBox(const Page& pg, const Params& p, bool portrait, double& x0, double& y0, double& x1, double& y1) {
    const double ins = std::max({p.overlap, p.margin, kSafe}), w = numberBoxW(pg.label), h = numberBoxH();   // inside the registration frame
    if (portrait) { x0 = pg.w / 2 - w / 2; y1 = pg.h - ins - kLift - 4; y0 = y1 - h; x1 = x0 + w; }
    else { x0 = ins + 3.5; y0 = ins + 3.5; x1 = x0 + w; y1 = y0 + h; }  // 3.5 mm: clear of the corner mark
}
void addMarks(Page& pg, const Params& p, bool portrait, bool numberOnThisPage) {
    const double ins = std::max(p.margin, kSafe);
    addText(pg, p.topMark, pg.w / 2, ins + 4.2, 8, true);   // below the dashed alignment line
    double botBase = pg.h - ins - kLift;
    addText(pg, p.bottomMark, pg.w / 2, botBase, 7, true);
    if (numberOnThisPage && p.pageNumbers) {
        double x0, y0, x1, y1;
        numberBox(pg, p, portrait, x0, y0, x1, y1);
        Text t; t.utf8 = pg.label; t.sizePt = kNumPt; t.bold = t.boxed = true; t.alpha = kNumAlpha;
        t.center = portrait;
        t.x = portrait ? pg.w / 2 : x0 + kBoxPadMm;
        t.y = y1 - kBoxPadMm;  // baseline
        pg.texts.push_back(t);
        if (!p.fileTag.empty()) {  // batch mode: loose pages of different files must not get mixed up
            Text f; f.utf8 = p.fileTag; f.sizePt = 9; f.alpha = kNumAlpha; f.r = f.g = f.b = 0.2;
            if (portrait) { f.center = true; f.x = pg.w / 2; f.y = y0 - 1.5; }
            else { f.x = x1 + 2.5; f.y = y1 - kBoxPadMm; }
            pg.texts.push_back(f);
        }
    }
}
// Registration frame: four dashed lines running edge to edge (a "#") and ONE "+" at each frame corner.  Lay a sheet over its
// neighbour so that the dashed lines coincide: the strip printed outside the frame then matches the neighbour's pattern lines.
void addAlignMarks(Page& pg, const Params& p, double f) {
    const double m = p.margin, L = 3, wMm = 0.2;
    auto style = [&](bool dashed) {
        Style s = thin(wMm, 0, dashed);
        s.r = p.alignR; s.g = p.alignG; s.b = p.alignB;
        if (dashed) { s.dashMm = 2.0; s.gapMm = 1.5; }
        return s;
    };
    const double x0 = f, x1 = pg.w - f, y0 = f, y1 = pg.h - f;
    for (double y : {y0, y1}) { Path d; d.style = style(true); d.pts = {{m, y}, {pg.w - m, y}}; pg.paths.push_back(d); }
    for (double x : {x0, x1}) { Path d; d.style = style(true); d.pts = {{x, m}, {x, pg.h - m}}; pg.paths.push_back(d); }
    for (double x : {x0, x1})
        for (double y : {y0, y1}) {
            Path h; h.style = style(false); h.pts = {{x - L, y}, {x + L, y}};
            Path v; v.style = style(false); v.pts = {{x, y - L}, {x, y + L}};
            pg.paths.push_back(h); pg.paths.push_back(v);
        }
}

Page makeOverview(const plt::Document& doc, const Params& p, const Plan& plan) {
    Page pg; pg.w = plan.pageW; pg.h = plan.pageH; pg.overview = true;
    const double step_x = plan.Wp, step_y = plan.Hp;   // frames abut
    const double gridW = plan.nx * step_x + 2 * plan.strip, gridH = plan.ny * step_y + 2 * plan.strip;
    const double side = 10, top = 35, bottom = 16;
    const double s = std::min((pg.w - 2 * side) / gridW, (pg.h - top - bottom) / gridH);
    const double ox = (pg.w - gridW * s) / 2, oy = top + ((pg.h - top - bottom) - gridH * s) / 2;
    // page windows
    for (int r = 0; r < plan.ny; ++r)
        for (int c = 0; c < plan.nx; ++c) {
            Path rect; rect.style = thin(0.15, 0.55, true);
            double x0 = ox + (plan.strip + c * step_x) * s, y0 = oy + (plan.strip + r * step_y) * s, x1 = x0 + plan.Wp * s, y1 = y0 + plan.Hp * s;
            rect.pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
            pg.paths.push_back(rect);
            addText(pg, std::to_string(r + 1) + "-" + std::to_string(c + 1), (x0 + x1) / 2 - 0, y0 + 5, 7, true, 0.6);
        }
    // pattern (scaled)
    const double bx = doc.bounds.minX, by = doc.bounds.maxY;
    Style st = penStyle(p); st.widthMm = std::max(0.1, p.lineWidth * 0.6);
    for (const auto& pl : doc.lines) {
        Path pa; pa.style = penStyleFor(p, doc, pl.pen); pa.style.widthMm = st.widthMm;
        for (const auto& q : pl.pts) pa.pts.push_back({ox + (q.x - bx + p.offX) * s, oy + (by - q.y + p.offY) * s});
        pg.paths.push_back(pa);
    }
    addText(pg, p.overviewTitle, pg.w / 2, 19, 16, true, 0);
    addText(pg, p.overviewNote, pg.w / 2, 24.5, 8, true, 0.35);
    {   // like commercial pattern PDFs: tell the user which size the printed frame must measure
        char fr[160];
        std::snprintf(fr, sizeof fr, "拼接框 %g x %g mm：打印后量一下蓝色虚线框，与此一致再裁拼", std::round(plan.Wp * 10) / 10, std::round(plan.Hp * 10) / 10);
        addText(pg, fr, pg.w / 2, 29.5, 8, true, 0.15);
    }
    addMarks(pg, p, plan.pageW < plan.pageH, false);  // watermarks, no page number
    return pg;
}

}  // namespace

Plan buildPlan(const plt::Document& doc, const Params& p) {
    Plan plan;
    const bool single = p.singlePage;
    const double w = doc.bounds.w(), h = doc.bounds.h();
    const double offX = single ? 0 : p.offX, offY = single ? 0 : p.offY;
    bool portrait = p.orient == Orient::Portrait;
    if (single) {
        plan.pageW = std::max(w + 2 * p.margin, 20.0);
        plan.pageH = std::max(h + 2 * p.margin, 20.0);
        if (plan.pageW > 5000 || plan.pageH > 5000) throw std::runtime_error("单页尺寸超过 PDF 单页上限（约 5000mm），请改用分页输出");
        plan.Wp = plan.pageW - 2 * p.margin;
        plan.Hp = plan.pageH - 2 * p.margin;
        plan.inset = p.margin; plan.strip = 0;
        plan.nx = plan.ny = 1;
        portrait = plan.pageW < plan.pageH;
    } else {
        TileGeom g = tileGeometry(p, w, h);
        if (!g.ok) throw std::runtime_error("margin/overlap too large for this paper");
        plan.pageW = g.pageW; plan.pageH = g.pageH;
        plan.Wp = g.fw; plan.Hp = g.fh; plan.inset = g.inset; plan.strip = g.strip;
        plan.nx = g.nx; plan.ny = g.ny;
    }
    plan.gridPages = plan.nx * plan.ny;

    const double bx = doc.bounds.minX, by = doc.bounds.maxY;
    // pattern in layout space (u right, v down, mm) -- built once
    std::vector<std::vector<Pt>> L;
    std::vector<Style> LS;  // one style per polyline (pen colour)
    L.reserve(doc.lines.size());
    LS.reserve(doc.lines.size());
    for (const auto& pl : doc.lines) {
        std::vector<Pt> q; q.reserve(pl.pts.size());
        for (const auto& pt : pl.pts) q.push_back({pt.x - bx + offX, by - pt.y + offY});
        L.push_back(std::move(q));
        LS.push_back(penStyleFor(p, doc, pl.pen));
    }

    std::vector<Page> grid;
    for (int r = 0; r < plan.ny; ++r)
        for (int c = 0; c < plan.nx; ++c) {
            Page pg; pg.w = plan.pageW; pg.h = plan.pageH; pg.row = r; pg.col = c;
            pg.label = std::to_string(r + 1) + "-" + std::to_string(c + 1);
            // printed window of page (r,c) = [c*Wp, c*Wp + Wp + 2*strip]: the frame sits `strip` inside it (frames abut, windows overlap by 2*strip)
            const double u0 = c * plan.Wp, v0 = r * plan.Hp;
            const double eps = single ? 1e-6 : 0;
            for (size_t li = 0; li < L.size(); ++li) clipPolyline(L[li], u0, v0, u0 + plan.Wp + 2 * plan.strip + eps, v0 + plan.Hp + 2 * plan.strip + eps, p.margin - u0, p.margin - v0, LS[li], pg.paths);
            // a sheet is blank when its FRAME holds no line: lines of neighbours reaching into its strip do not make it necessary.
            // On the outer sides of the grid there is no neighbour, so the strip there belongs to this sheet.
            const double tx0 = c == 0 ? p.margin : plan.inset, tx1 = c == plan.nx - 1 ? plan.pageW - p.margin : plan.pageW - plan.inset;
            const double ty0 = r == 0 ? p.margin : plan.inset, ty1 = r == plan.ny - 1 ? plan.pageH - p.margin : plan.pageH - plan.inset;
            pg.empty = !pathsHitRect(pg.paths, tx0, ty0, tx1, ty1);
            if (!single && std::find(p.blankLabels.begin(), p.blankLabels.end(), pg.label) != p.blankLabels.end()) { pg.paths.clear(); pg.empty = true; pg.manual = true; }
            grid.push_back(std::move(pg));
        }

    for (auto& pg : grid) {
        const bool first = pg.row == 0 && pg.col == 0;
        if (pg.empty && !pg.manual && !(first && p.calibBox) && p.skipEmpty) { ++plan.blankPages; continue; }
        if (pg.empty) ++plan.blankPages; else ++plan.effectivePages;
        if (p.alignMarks && !single) addAlignMarks(pg, p, plan.inset);
        addMarks(pg, p, portrait, !single);
        if (first && p.calibBox) {
            const double S = p.calibSize, gap = 8, f = plan.inset;   // inside the frame corners
            const double xr = plan.pageW - f - gap - S, xl = f + gap, yb = plan.pageH - f - gap - S, yt = f + gap;
            const double cand[4][2] = {{xr, yb}, {xl, yb}, {xr, yt}, {xl, yt}};
            int pick = -1;
            double nx0 = 0, ny0 = 0, nx1 = 0, ny1 = 0;
            const bool hasNumber = p.pageNumbers && !single;
            if (hasNumber) numberBox(pg, p, portrait, nx0, ny0, nx1, ny1);
            for (int k = 0; k < 4 && pick < 0; ++k) {
                const double cx0 = cand[k][0] - 2, cy0 = cand[k][1] - 2, cx1 = cand[k][0] + S + 2, cy1 = cand[k][1] + S + 2;
                const bool onNumber = hasNumber && cx0 < nx1 && cx1 > nx0 && cy0 < ny1 && cy1 > ny0;
                if (!onNumber && !pathsHitRect(pg.paths, cx0, cy0, cx1, cy1)) pick = k;
            }
            if (pick < 0) { pick = 0; plan.calibCollision = true; }
            Path sq; sq.style = thin(0.3, 0);
            double x = cand[pick][0], y = cand[pick][1];
            sq.pts = {{x, y}, {x + S, y}, {x + S, y + S}, {x, y + S}, {x, y}};
            pg.paths.push_back(sq);
            char lab[48];
            std::snprintf(lab, sizeof lab, "%g x %g cm", S / 10, S / 10);
            addText(pg, lab, x + S / 2, y + S / 2 + 1.2, 9, true, 0);
        }
        plan.pages.push_back(std::move(pg));
    }
    if (p.overview && !single) plan.pages.insert(plan.pages.begin(), makeOverview(doc, p, plan));
    return plan;
}

OrientChoice chooseOrientation(const plt::Document& doc, Params p) {
    p.blankLabels.clear();   // hand-set blanks belong to the cells of the chosen layout, not to the comparison
    p.overview = false; p.calibBox = false;  // not relevant for counting
    p.orient = Orient::Portrait;  Plan a = buildPlan(doc, p);
    p.orient = Orient::Landscape; Plan b = buildPlan(doc, p);
    OrientChoice oc{Orient::Portrait, a.effectivePages, b.effectivePages};
    auto seams = [](const Plan& x) { return (x.nx - 1) + (x.ny - 1); };
    auto waste = [&](const Plan& x) { return x.effectivePages * x.pageW * x.pageH; };
    if (b.effectivePages != a.effectivePages) oc.best = b.effectivePages < a.effectivePages ? Orient::Landscape : Orient::Portrait;
    else if (seams(b) != seams(a)) oc.best = seams(b) < seams(a) ? Orient::Landscape : Orient::Portrait;
    else if (std::fabs(waste(b) - waste(a)) > 1e-6) oc.best = waste(b) < waste(a) ? Orient::Landscape : Orient::Portrait;
    return oc;
}

OffsetChoice autoOffset(const plt::Document& doc, Params p) {
    p.blankLabels.clear();
    OffsetChoice best;
    if (p.singlePage) return best;
    p.overview = false; p.calibBox = false;
    const double w = doc.bounds.w(), h = doc.bounds.h();
    p.offX = p.offY = 0;
    const TileGeom g = tileGeometry(p, w, h);
    if (!g.ok) return best;
    const double gridW = g.nx * g.fw + 2 * g.strip, gridH = g.ny * g.fh + 2 * g.strip;
    const double xs[3] = {0, std::max(0.0, (gridW - w) / 2), std::max(0.0, gridW - w)};
    const double ys[3] = {0, std::max(0.0, (gridH - h) / 2), std::max(0.0, gridH - h)};
    best.pages = -1;
    for (double y : ys)
        for (double x : xs) {
            p.offX = x; p.offY = y;
            int n = buildPlan(doc, p).effectivePages;
            if (best.pages < 0 || n < best.pages) { best.x = x; best.y = y; best.pages = n; }
        }
    return best;
}

ShiftHint shiftHint(const plt::Document& doc, const Params& p, double maxShift) {
    ShiftHint r;
    if (p.singlePage) return r;
    const double w = doc.bounds.w(), h = doc.bounds.h();
    const TileGeom g = tileGeometry(p, w, h);
    if (!g.ok) return r;
    auto cnt = [&](double off, double ext, double frame) { return std::max(1, static_cast<int>(std::ceil((off + ext - 2 * g.strip) / frame - 1e-9))); };
    auto pages = [&](double ox, double oy) { return cnt(ox, w, g.fw) * cnt(oy, h, g.fh); };
    const int from = pages(p.offX, p.offY);
    int best = from, bestCost = 1 << 30;
    const int m = static_cast<int>(maxShift);
    for (int ix = -m; ix <= m; ++ix)
        for (int iy = -m; iy <= m; ++iy) {
            double ox = p.offX + ix, oy = p.offY + iy;
            if (ox < 0 || oy < 0) continue;
            int n = pages(ox, oy), cost = std::abs(ix) + std::abs(iy);
            if (n < best || (n == best && n < from && cost < bestCost)) { best = n; bestCost = cost; r.dx = ix; r.dy = iy; }
        }
    if (best < from) { r.found = true; r.from = from; r.to = best; }
    return r;
}

}  // namespace layout
