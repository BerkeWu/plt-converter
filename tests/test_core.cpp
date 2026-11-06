#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

#include "../core/docops.h"
#include "../core/layout.h"
#include "../core/pagerange.h"
#include "../core/pltedit.h"
#include "../core/pdf_min.h"
#include "../core/naming.h"
#include "../core/plt.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::cout << "  FAIL: " << msg << "\n"; } } while (0)


int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    // 1. synthetic parser + tiling checks
    {
        plt::Document d = plt::parseString("IN;SP1;PU0,0;PD400,0,400,400,0,400,0,0;PU;PR;PU4000,4000;PD40,40;");
        CHECK(d.lines.size() == 2, "synthetic: 2 polylines");
        { plt::Document lb = plt::parseString("IN;PU0,0;PD400,0;PU100,100;LB abc\x03;PU0,0;PD0,400;"); CHECK(lb.unsupported["LB"] == 1 && lb.lines.size() == 2, "LB skipped and counted"); }
        CHECK(std::fabs(d.bounds.w() - 101.0) < 1e-6 && std::fabs(d.bounds.h() - 101.0) < 1e-6, "synthetic: bounds 101x101mm");
        CHECK(layout::tilesNeeded(190, 200, 10) == 1 && layout::tilesNeeded(200, 200, 10) == 1, "tiles: fits in 1");
        CHECK(layout::tilesNeeded(560, 200, 10) == 3 && layout::tilesNeeded(201, 200, 10) == 2, "tiles: 560 on 200 -> 3, 201 -> 2");
        // PRD example: 560x380 on A4 -> portrait 6 pages, landscape 4 pages
        plt::Document big = plt::parseString("IN;PU0,0;PD22400,0,22400,15200,0,15200,0,0;");  // 560x380 mm rectangle
        layout::Params p; p.calibBox = false;
        auto oc = layout::chooseOrientation(big, p);
        CHECK(oc.portraitPages == 6 && oc.landscapePages == 4 && oc.best == layout::Orient::Landscape, "PRD example 6 vs 4 pages, landscape wins");
        std::set<std::string> taken = {"a_A4横.pdf", "a_A4横_2.pdf"};
        CHECK(naming::uniquePdfName("a", "A4", layout::Orient::Landscape, [&](const std::string& n) { return taken.count(n) > 0; }) == "a_A4横_3.pdf", "naming: _3 when _2 taken");
        CHECK(naming::uniquePdfName("x:y", "A3", layout::Orient::Portrait, [](const std::string&) { return false; }) == "x_y_A3竖.pdf", "naming: sanitize + first name");
    }

    // 1b. docops / single page / page range / auto offset / shift hint / line colour
    {
        plt::Document d = plt::parseString("IN;PU0,0;PD4000,0,4000,2000,0,2000,0,0;");  // 100 x 50 mm
        plt::Document r = d; docops::transform(r, 1, false);
        CHECK(std::fabs(r.bounds.w() - 50) < 1e-6 && std::fabs(r.bounds.h() - 100) < 1e-6, "rotate 90: 50x100");
        plt::Document m = d; docops::transform(m, 0, true);
        CHECK(std::fabs(m.bounds.w() - 100) < 1e-6 && std::fabs(m.bounds.minX + 100) < 1e-6, "mirror: x negated");
        plt::Document four = d; docops::transform(four, 4, false);
        CHECK(std::fabs(four.bounds.w() - 100) < 1e-6, "rotate x4 = identity");
        plt::Document c1 = docops::crop(d, 0, 0, 50, 50, true);
        CHECK(std::fabs(c1.bounds.w() - 50) < 1e-6 && std::fabs(c1.bounds.h() - 50) < 1e-6 && !c1.lines.empty(), "crop clip: 50x50");
        plt::Document c2 = docops::crop(d, 40, 10, 60, 30, false);
        CHECK(c2.lines.empty(), "crop whole: rectangle outline not touched by an interior window");
        plt::Document c3 = docops::crop(d, 90, 10, 110, 30, false);
        CHECK(c3.lines.size() == 1 && std::fabs(c3.bounds.w() - 100) < 1e-6, "crop whole: touching line kept uncut");
        layout::Params sp; sp.singlePage = true; sp.margin = 5; sp.calibBox = false;
        layout::Plan spl = layout::buildPlan(d, sp);
        CHECK(spl.pages.size() == 1 && std::fabs(spl.pageW - 110) < 1e-9 && std::fabs(spl.pageH - 60) < 1e-9, "single page = drawing + 2*margin");
        double L = 0; for (auto& pa : spl.pages[0].paths) for (size_t i = 1; i < pa.pts.size(); ++i) L += std::hypot(pa.pts[i].x - pa.pts[i - 1].x, pa.pts[i].y - pa.pts[i - 1].y);
        CHECK(std::fabs(L - 300) < 1e-3, "single page keeps full line length");
        plt::Document huge = plt::parseString("IN;PU0,0;PD400000,0,400000,400000;");  // 10000 x 10000 mm
        bool threw = false; try { layout::buildPlan(huge, sp); } catch (const std::exception&) { threw = true; }
        CHECK(threw, "single page > 5000mm rejected");
        auto pr = pagerange::parse("1-3, 5，7", 10);
        CHECK((pr == std::vector<int>{1, 2, 3, 5, 7}), "page range parse");
        std::string e; CHECK(pagerange::parse("0-2", 10, &e).empty() && !e.empty(), "page range 0 rejected");
        CHECK(pagerange::parse("3-2", 10, &e).empty() && pagerange::parse("11", 10, &e).empty() && pagerange::parse("1,,2", 10, &e).empty(), "page range invalid forms");
        layout::Params cp; cp.lineR = 1; cp.lineG = 0; cp.lineB = 0; cp.calibBox = false; cp.alignMarks = false;
        layout::Plan cpl = layout::buildPlan(d, cp);
        CHECK(!cpl.pages.empty() && cpl.pages[0].paths[0].style.r == 1, "line colour applied to pattern");
        // 170 x 100 mm on A4 portrait (Wp = 200): fits; landscape fits. Use 230 x 100: portrait needs 2 columns when offset 0 -> shift hint
        plt::Document w = plt::parseString("IN;PU0,0;PD7200,0,7200,4000,0,4000,0,0;");  // 180 x 100
        layout::Params hp; hp.orient = layout::Orient::Portrait; hp.calibBox = false; hp.offX = 30;
        layout::ShiftHint sh = layout::shiftHint(w, hp);
        CHECK(sh.found && sh.dx < 0 && sh.from > sh.to, "shift hint: move left reduces pages");
        layout::Params ap; ap.orient = layout::Orient::Portrait; ap.calibBox = false;
        layout::OffsetChoice oc = layout::autoOffset(w, ap);
        CHECK(oc.pages == layout::buildPlan(w, [&] { auto q = ap; q.offX = oc.x; q.offY = oc.y; q.calibBox = false; return q; }()).effectivePages, "autoOffset reports its own page count");
    }

    // 1c. non-ASCII paths: read, write, auto-numbered name (the Windows ANSI code page problem)
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / fs::u8path(u8"plt测试 目录");
        fs::create_directories(dir);
        fs::path in = dir / fs::u8path(u8"纸样 上衣.PLT");
        { std::ofstream f(in, std::ios::binary); f << "IN;PU0,0;PD4000,0,4000,2000,0,2000,0,0;"; }
        plt::Document d;
        try { d = plt::parseFile(in); } catch (std::exception& ex) { CHECK(false, std::string("parseFile with Chinese path: ") + ex.what()); }
        CHECK(d.lines.size() == 1, "parseFile with Chinese path + space + upper-case extension");
        layout::Params p; p.calibBox = false;
        layout::Plan plan = layout::buildPlan(d, p);
        fs::path out = naming::uniquePdfPath(dir, in.stem().u8string(), "A4", p.orient);
        CHECK(out.filename().u8string() == u8"纸样 上衣_A4横.pdf", "output name keeps Chinese characters");
        try { pdfmin::write(plan, out); } catch (std::exception& ex) { CHECK(false, std::string("pdfmin::write with Chinese path: ") + ex.what()); }
        CHECK(fs::exists(out) && !fs::exists(fs::path(out).concat(".part")), "PDF written to Chinese path, temp file gone");
        fs::path out2 = naming::uniquePdfPath(dir, in.stem().u8string(), "A4", p.orient);
        CHECK(out2.filename().u8string() == u8"纸样 上衣_A4横_2.pdf", "second export is numbered _2, never overwrites");
        fs::remove_all(dir);
    }

    // 1c'. pages the user set to blank by hand: lines gone, sheet (number + marks) still output, even when empty pages are skipped
    {
        plt::Document d = plt::parseString("IN;PU0,0;PD12000,0,12000,8000,0,8000,0,0;");  // 300 x 200 mm -> two landscape A4 pages
        layout::Params p; p.orient = layout::Orient::Landscape; p.calibBox = false; p.overview = false; p.margin = 0; p.overlap = 10;
        layout::Plan a = layout::buildPlan(d, p);
        CHECK(a.pages.size() == 2 && a.effectivePages == 2, "blank-page test base: two pages with pattern");
        p.blankLabels = {"1-2"};
        layout::Plan b = layout::buildPlan(d, p);
        CHECK(b.pages.size() == 2 && b.effectivePages == 1 && b.blankPages == 1, "hand-set blank page is still output but not counted as effective");
        const layout::Page* blank = nullptr;
        for (auto& pg : b.pages) if (pg.label == "1-2") blank = &pg;
        CHECK(blank && blank->manual && blank->empty, "page flagged as manual blank");
        bool anyPattern = false;
        if (blank) for (auto& pa : blank->paths) if (!(pa.style.b > 0.8 && pa.style.r < 0.3)) anyPattern = true;   // only blue alignment marks remain
        CHECK(blank && !anyPattern, "no pattern line is printed on the blank page");
        bool number = false;
        if (blank) for (auto& t : blank->texts) if (t.utf8 == "1-2") number = true;
        CHECK(number, "blank page keeps its page number");
        p.singlePage = true;
        CHECK(layout::buildPlan(d, p).pages.size() == 1, "blank labels are ignored in single-page output");
    }

    // 1d. page number style + alignment marks (dashed, blue by default)
    {
        plt::Document d = plt::parseString("IN;PU0,0;PD12000,0,12000,8000,0,8000,0,0;");  // 300 x 200 mm
        plt::Document d1 = plt::parseString("IN;PU0,0;PD4000,0,4000,2000,0,2000,0,0;");   // 100 x 50 mm: one page, no neighbours
        for (auto o : {layout::Orient::Landscape, layout::Orient::Portrait}) {
            layout::Params p; p.orient = o; p.calibBox = false;
            layout::Plan pl = layout::buildPlan(d1, p);
            const layout::Page& pg = pl.pages.front();
            const layout::Text* num = nullptr;
            for (auto& t : pg.texts) if (t.utf8 == pg.label) num = &t;
            CHECK(num && num->sizePt == 30 && num->bold && num->boxed && std::fabs(num->alpha - 0.8) < 1e-9, "page number: 30pt (3x), bold, boxed, 80% opacity");
            int dashed = 0, solidBlue = 0;
            for (auto& pa : pg.paths) {
                const bool blue = pa.style.b > 0.8 && pa.style.r < 0.3;
                if (pa.style.dashed && blue && pa.style.dashMm > 0) ++dashed;
                else if (!pa.style.dashed && blue) ++solidBlue;
            }
            CHECK(dashed == 4 && solidBlue == 8, "alignment marks: 8 blue '+' arms joined by 4 dashed blue lines");
        }
        {   // registration frame (拼接框): dashed lines edge to edge, ONE "+" per corner, pattern also printed outside the frame
            plt::Document d2 = plt::parseString("IN;PU0,0;PD12000,0;PU11360,0;PD11360,7200;");  // 300 mm baseline + a vertical line at u = 284 mm
            layout::Params q; q.orient = layout::Orient::Landscape; q.calibBox = false; q.overview = false; q.margin = 5; q.overlap = 10;
            layout::Plan pl = layout::buildPlan(d2, q);
            CHECK(pl.nx == 2 && pl.ny == 1 && pl.pages.size() == 2, "300 mm wide on landscape A4 = 2 pages");
            CHECK(pl.inset == 10 && pl.strip == 5 && std::fabs(pl.Wp - 277) < 1e-9, "frame sits 10 mm inside the sheet edge, 5 mm of pattern printed outside it, frame 277 mm wide");
            auto isBlue = [](const layout::Path& pa) { return pa.style.b > 0.8 && pa.style.r < 0.3; };
            std::set<long> uLines[2];
            for (int i = 0; i < 2; ++i) {
                const layout::Page& pg = pl.pages[i];
                int arms = 0, dashed = 0;
                for (auto& pa : pg.paths) {
                    if (!isBlue(pa)) continue;
                    if (pa.style.dashed) { ++dashed; if (pa.pts[0].x == pa.pts[1].x) uLines[i].insert(std::lround((pa.pts[0].x - q.margin + i * pl.Wp) * 1000)); }
                    else ++arms;
                }
                CHECK(arms == 8 && dashed == 4, "one '+' per frame corner (8 arms) and 4 dashed frame lines per page, nothing else");
            }
            int common = 0;
            for (long u : uLines[0]) if (uLines[1].count(u)) ++common;
            CHECK(common == 1 && uLines[0].count(282000), "page 1's right frame line and page 2's left frame line are the same line (u = 282 mm)");
            auto hasVertical = [&](const layout::Page& pg, double x) {
                for (auto& pa : pg.paths) if (!isBlue(pa) && pa.pts.size() == 2 && std::fabs(pa.pts[0].x - x) < 1e-6 && std::fabs(pa.pts[1].x - x) < 1e-6) return true;
                return false;
            };
            CHECK(hasVertical(pl.pages[0], 289) && hasVertical(pl.pages[1], 12), "a line across the seam is printed on BOTH sheets (page 1 outside its frame, page 2 inside it)");
            CHECK(std::fabs((289 - 12) - pl.Wp) < 1e-9, "…and the two copies are exactly one frame width apart, so overlaid sheets coincide");
        }
        layout::Params p; p.alignR = 1; p.alignG = 0; p.alignB = 0; p.calibBox = false;
        bool red = false;
        for (auto& pa : layout::buildPlan(d, p).pages.front().paths) if (pa.style.dashed && pa.style.r == 1 && pa.style.b == 0) red = true;
        CHECK(red, "alignment mark colour is configurable");
        // calibration box never lands on the (now large) page-number box when another corner is free
        layout::Params c; c.orient = layout::Orient::Landscape;
        layout::Plan cp = layout::buildPlan(d, c);
        CHECK(!cp.calibCollision, "calibration box placed without collision");
    }

    // 1e. arcs, rectangles, pen colours
    {
        auto near = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
        const double PI = 3.14159265358979323846;
        plt::Document q = plt::parseString("IN;PU0,0;PD;AA0,1000,90;");  // quarter circle, centre (0,25mm), r 25 mm, CCW
        CHECK(near(q.bounds.w(), 25, 1e-6) && near(q.bounds.h(), 25, 1e-6), "AA: quarter circle bounds 25x25 mm");
        CHECK(near(q.totalLengthMm, PI / 2 * 25, 0.05), "AA: arc length = pi/2 * r");
        CHECK(q.unsupported.empty(), "AA is no longer reported as unsupported");
        plt::Document r = plt::parseString("IN;PU500,500;PD;AR0,500,180;");  // relative centre, half circle through (25,25)
        CHECK(near(r.bounds.maxX, 25, 1e-6) && near(r.bounds.w(), 12.5, 1e-6) && near(r.bounds.h(), 25, 1e-6), "AR: relative centre, 180 deg");
        plt::Document cw = plt::parseString("IN;PU0,1000;PD;AA0,0,-90;");  // negative sweep = clockwise
        CHECK(near(cw.totalLengthMm, PI / 2 * 25, 0.05) && cw.bounds.maxX > 24.9, "AA: negative sweep goes clockwise");
        plt::Document t = plt::parseString("IN;PU0,0;PD;AT1000,1000,2000,0;");  // semicircle over the top
        CHECK(near(t.bounds.w(), 50, 1e-6) && near(t.bounds.h(), 25, 1e-3) && near(t.totalLengthMm, PI * 25, 0.1), "AT: three-point semicircle");
        plt::Document rt = plt::parseString("IN;PU1000,0;PD;RT1000,1000,2000,0;");
        CHECK(near(rt.bounds.w(), 50, 1e-6) && near(rt.bounds.h(), 25, 1e-3), "RT: relative three-point arc");
        plt::Document col = plt::parseString("IN;PU0,0;PD;AT500,0,1000,0;");
        CHECK(near(col.totalLengthMm, 25, 1e-6), "AT with collinear points falls back to a straight line");
        plt::Document ea = plt::parseString("IN;PU0,0;EA400,200;PU0,0;PD800,0;");
        CHECK(ea.lines.size() == 2 && near(ea.bounds.w(), 20, 1e-6) && near(ea.bounds.h(), 5, 1e-6) && ea.unsupported.empty(), "EA: rectangle outline, pen position unchanged");
        plt::Document er = plt::parseString("IN;PU400,400;ER400,200;");
        CHECK(er.lines.size() == 1 && near(er.bounds.minX, 10, 1e-6) && near(er.bounds.maxX, 20, 1e-6) && near(er.bounds.maxY, 15, 1e-6), "ER: relative rectangle");

        plt::Document pc = plt::parseString("IN;PC2,255,0,0;PC3,0,255,0;PC6,255,255,255;SP2;PU0,0;PD400,0;SP3;PU0,400;PD400,400;SP1;PU0,800;PD400,800;SP6;PU0,1200;PD400,1200;");
        CHECK(pc.penColors.size() == 3 && pc.penColors[2].r == 1 && pc.penColors[2].g == 0, "PC defines pen colours (0..255 default range)");
        CHECK(pc.lines.size() == 4 && pc.lines[0].pen == 2 && pc.lines[2].pen == 1, "polylines keep their pen numbers");
        layout::Params cp; cp.calibBox = false; cp.alignMarks = false; cp.pageNumbers = false;
        auto colours = [&](bool orig) {
            cp.useOriginalColors = orig;
            std::vector<std::array<double, 3>> v;
            for (auto& pa : layout::buildPlan(pc, cp).pages.front().paths) v.push_back({pa.style.r, pa.style.g, pa.style.b});
            return v;
        };
        auto on = colours(true), off = colours(false);
        auto has = [](const std::vector<std::array<double, 3>>& v, double r, double g, double b) { for (auto& c : v) if (c[0] == r && c[1] == g && c[2] == b) return true; return false; };
        CHECK(has(on, 1, 0, 0) && has(on, 0, 1, 0) && has(on, 0, 0, 0), "original colours: red pen, green pen, undefined pen stays black");
        CHECK(has(on, 0.75, 0.75, 0.75) && !has(on, 1, 1, 1), "white pen prints light grey (would vanish on white paper)");
        CHECK(!has(off, 1, 0, 0) && !has(off, 0, 1, 0), "original colours off: everything uses the chosen line colour");
        plt::Document cr = plt::parseString("IN;CR0,1,0,1,0,1;PC5,0.5,0.25,1;SP5;PU0,0;PD400,0;");
        CHECK(cr.penColors.size() == 1 && cr.penColors[5].r == 0.5 && cr.penColors[5].g == 0.25 && cr.penColors[5].b == 1, "CR changes the colour range");
        plt::Document rs = plt::parseString("IN;PC2,255,0,0;PC;PU0,0;PD400,0;");
        CHECK(rs.penColors.empty(), "PC; resets pen colours (the 'PC;' in the sample files carries no colour)");
    }

    // 1f. PRD 4.9: automatic orientation == exhaustive optimum on 30 constructed drawings (3 papers x 10 sizes)
    {
        const int sz[10][2] = {{560, 380}, {380, 560}, {210, 297}, {297, 210}, {100, 100}, {900, 150}, {150, 900}, {1200, 800}, {640, 640}, {330, 480}};
        int bad = 0, total = 0;
        for (const char* paper : {"A4", "A3", "A0"})
            for (auto& d : sz) {
                const int W = d[0] * 40, H = d[1] * 40;
                plt::Document doc = plt::parseString("IN;PU0,0;PD" + std::to_string(W) + ",0," + std::to_string(W) + "," + std::to_string(H) + ",0," + std::to_string(H) + ",0,0;PU0,0;PD" + std::to_string(W) + "," + std::to_string(H) + ";");
                layout::Params p; p.paper = layout::paperByName(paper); p.calibBox = false;
                p.orient = layout::Orient::Portrait;  const int pp = layout::buildPlan(doc, p).effectivePages;
                p.orient = layout::Orient::Landscape; const int pl = layout::buildPlan(doc, p).effectivePages;
                const layout::OrientChoice oc = layout::chooseOrientation(doc, p);
                const int chosen = oc.best == layout::Orient::Portrait ? pp : pl;
                ++total;
                if (chosen != std::min(pp, pl)) ++bad;
            }
        CHECK(total == 30 && bad == 0, "auto orientation matches the exhaustive optimum on all 30 cases");
    }
    // 2. real files
    if (argc > 1) {
        for (auto& e : fs::directory_iterator(argv[1])) {
            if (e.path().extension() != ".plt") continue;
            std::cout << e.path().filename().string() << "\n";
            plt::Document doc;
            try { doc = plt::parseFile(e.path()); } catch (std::exception& ex) { CHECK(false, ex.what()); continue; }
            std::cout << "  " << doc.bounds.w() << " x " << doc.bounds.h() << " mm, " << doc.lines.size() << " polylines, length "
                      << doc.totalLengthMm / 1000 << " m\n";
            for (auto& u : doc.unsupported) std::cout << "  unsupported cmd " << u.first << " x" << u.second << "\n";
            for (const char* pn : {"A4", "A3", "A0"}) {
                layout::Params p; p.paper = layout::paperByName(pn);
                p.calibBox = false; p.alignMarks = false; p.pageNumbers = false; p.margin = 0; p.overlap = 0.0001; p.skipEmpty = true;
                for (auto o : {layout::Orient::Portrait, layout::Orient::Landscape}) {
                    p.orient = o;
                    layout::Plan pl = layout::buildPlan(doc, p);
                    double L = 0;
                    for (const auto& pg : pl.pages) for (const auto& pa : pg.paths) for (size_t i = 1; i < pa.pts.size(); ++i) L += std::hypot(pa.pts[i].x - pa.pts[i - 1].x, pa.pts[i].y - pa.pts[i - 1].y);
                    double err = std::fabs(L - doc.totalLengthMm) / doc.totalLengthMm;
                    CHECK(err < 1e-4, std::string(pn) + " length conservation, err=" + std::to_string(err));
                    CHECK(pl.effectivePages + pl.blankPages == pl.gridPages, "page accounting");
                }
                layout::Params q; q.paper = p.paper;
                auto oc = layout::chooseOrientation(doc, q);
                std::cout << "  " << pn << ": portrait " << oc.portraitPages << " / landscape " << oc.landscapePages << " pages -> "
                          << (oc.best == layout::Orient::Landscape ? "landscape" : "portrait") << "\n";
            }
            {
                plt::Document f = doc;
                bool isDZ = e.path().filename().string().rfind("DZ", 0) == 0;
                int removed = plt::stripFrame(f);
                bool had = removed > 0;
                CHECK(had == isDZ, "frame detection only on DZ files");
                if (had) {
                    CHECK(removed == 2 && f.lines.size() == doc.lines.size() - 2, "BOTH copies of the frame are removed (CAD draws it twice), others kept");
                    CHECK(plt::findFrame(f) < 0, "no frame left after stripping");
                    CHECK(f.bounds.w() < doc.bounds.w() + 1e-9, "bounds recomputed");
                    std::cout << "  frame removed: " << doc.bounds.w() << "x" << doc.bounds.h() << " -> " << f.bounds.w() << "x" << f.bounds.h() << " mm\n";
                }
            }
            layout::Params ov; ov.overview = true; ov.orient = layout::Orient::Portrait;
            layout::Plan pl = layout::buildPlan(doc, ov);
            CHECK(pl.pages.front().overview && pl.pages.front().label.empty(), "overview page first, no page number label");
            bool hasNumber = false;
            for (auto& t : pl.pages.front().texts) if (t.boxed) hasNumber = true;
            CHECK(!hasNumber, "overview page has no page number");
            CHECK(pl.pages[1].label == "1-1" || pl.pages[1].row == 0, "first grid page after overview");
        }
    }
    // PLT stroke-label removal: a label drawn with line strokes ("小红叶" style), copies of different size / mirrored / turned
    {
        auto fmt = [](double mm) { return std::to_string((long)std::lround(mm * 40)); };
        struct Pt2 { double x, y; };
        const std::vector<std::vector<Pt2>> glyph = {{{0, 0}, {10, 0}, {10, 6}}, {{0, 8}, {4, 12}, {9, 9}, {12, 14}}, {{14, 0}, {14, 14}}, {{18, 2}, {22, 2}}};
        auto draw = [&](double ox, double oy, double sc, bool mir, int rot, bool relative) {
            std::string o;
            for (auto& st : glyph) {
                std::vector<Pt2> q;
                for (auto p : st) { double x = (mir ? -p.x : p.x) * sc, y = p.y * sc; for (int k = 0; k < rot; ++k) { double nx = -y, ny = x; x = nx; y = ny; } q.push_back({x + ox, y + oy}); }
                o += "PU" + fmt(q[0].x) + "," + fmt(q[0].y) + ";";
                if (relative) {
                    o += "PR;PD";
                    for (size_t k = 1; k < q.size(); ++k) o += (k > 1 ? "," : "") + std::to_string(std::lround((q[k].x - q[k - 1].x) * 40)) + "," + std::to_string(std::lround((q[k].y - q[k - 1].y) * 40));
                    o += ";PU;PA;";
                } else {
                    o += "PD";
                    for (size_t k = 1; k < q.size(); ++k) o += (k > 1 ? "," : "") + fmt(q[k].x) + "," + fmt(q[k].y);
                    o += ";";
                }
            }
            return o;
        };
        std::string data = "IN;SP1;PU0,0;PD0,16000,16000,16000,16000,0,0,0;PU;PU1000,1000;PD1000,3000;";   // a pattern outline + a line
        data += draw(100, 100, 1.0, false, 0, false);
        data += draw(300, 120, 1.5, false, 0, true);       // bigger, drawn with relative moves
        data += draw(200, 300, 1.0, true, 0, false);       // mirrored
        data += draw(400, 400, 1.0, false, 2, false);      // turned by 180
        data += "PU2000,2000;PD2500,2500;PU;";               // something after the labels
        plt::Document d = plt::parseString(data);
        const size_t nGlyph = glyph.size(), nOther = 3;
        CHECK(d.lines.size() == nOther + 4 * nGlyph, "plt label: parsed " << d.lines.size());
        plt::Bounds box; box.add({99, 99}); box.add({125, 118});
        auto r = plt::findShapeCopies(d, box, true);
        CHECK(r.ok && r.selected == (int)nGlyph && r.copies == 3 && r.lines.size() == 4 * nGlyph, "plt label: found 4 instances");
        std::string out = plt::removeLines(data, d, r.lines);
        plt::Document d2 = plt::parseString(out);
        CHECK(d2.lines.size() == nOther, "plt label: only the outline and the other lines remain (" << d2.lines.size() << ")");
        std::set<size_t> gone(r.lines.begin(), r.lines.end());
        std::vector<size_t> kept; for (size_t i = 0; i < d.lines.size(); ++i) if (!gone.count(i)) kept.push_back(i);
        bool same = d2.lines.size() == kept.size();
        for (size_t i = 0; same && i < d2.lines.size(); ++i) { auto& a = d2.lines[i].pts; auto& b = d.lines[kept[i]].pts; same = a.size() == b.size(); for (size_t k = 0; same && k < a.size(); ++k) same = std::fabs(a[k].x - b[k].x) < 1e-9 && std::fabs(a[k].y - b[k].y) < 1e-9; }
        CHECK(same, "plt label: remaining lines unchanged (also after relative moves)");
        CHECK(out.find("IN;SP1;PU0,0;PD0,16000") == 0, "plt label: file header untouched");
        auto only = plt::findShapeCopies(d, box, false);
        CHECK(only.ok && only.lines.size() == nGlyph && only.copies == 0, "plt label: boxed instance only");
        plt::Bounds none; none.add({0, 0}); none.add({1, 1});
        CHECK(!plt::findShapeCopies(d, none, true).ok, "plt label: empty box -> error");
        // template taken once, reused on another drawing
        std::vector<plt::Stroke> tpl; CHECK(plt::extractTemplate(d, box, tpl), "plt label: template");
        plt::Document other = plt::parseString("IN;PU0,0;PD400,0;PU;" + draw(50, 60, 0.8, false, 1, false));
        auto tr = plt::findTemplateCopies(other, tpl);
        CHECK(tr.ok && tr.lines.size() == nGlyph, "plt label: template applied to another file (scaled, turned)");
        // arcs are not editable (cannot be switched off without moving the pen)
        plt::Document arcs = plt::parseString("IN;PU0,0;PD400,0;AA400,400,90;PU;");
        bool anyNon = false; for (auto& l : arcs.lines) if (!l.editable) anyNon = true;
        CHECK(anyNon, "plt label: arc lines are marked not editable");
    }
    std::cout << (fails ? "FAILED: " : "ALL PASSED") << (fails ? std::to_string(fails) : "") << "\n";
    return fails ? 1 : 0;
}
