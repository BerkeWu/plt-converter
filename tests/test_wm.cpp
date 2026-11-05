#include "wm/wm.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// operator histogram of all page contents, and the set of resource names (XObject / ExtGState) still present
struct Hist { std::map<std::string, int> ops; std::set<std::string> res; int pages = 0; };
class Cb : public QPDFObjectHandle::ParserCallbacks {
public:
    std::map<std::string, int>* m;
    void handleObject(QPDFObjectHandle o) override { if (o.isOperator()) (*m)[o.getOperatorValue()]++; }
    void handleEOF() override {}
};
static Hist hist(const std::filesystem::path& p) {
    Hist h; QPDF q; q.processFile(wm::toU8(p).c_str());
    QPDFPageDocumentHelper d(q); d.pushInheritedAttributesToPage();
    for (auto& pg : d.getAllPages()) {
        ++h.pages; Cb cb; cb.m = &h.ops; pg.parseContents(&cb);
        auto r = pg.getObjectHandle().getKey("/Resources");
        for (const char* k : {"/XObject", "/ExtGState"}) { auto x = r.getKey(k); if (x.isDictionary()) for (auto& n : x.getKeys()) h.res.insert(n); }
    }
    return h;
}
static std::filesystem::path tmp(const char* n) { return std::filesystem::temp_directory_path() / n; }

int main(int argc, char** argv) {
    std::filesystem::path dir = argc > 1 ? argv[1] : "tests/data_wm";

    // A: stand-alone stamped object
    {
        auto r = wm::analyze(dir / "A_artifact.pdf");
        CHECK(r.ok); CHECK(r.pageCount == 10);
        CHECK(!r.findings.empty());
        if (!r.findings.empty()) {
            auto& f = r.findings[0];
            CHECK(f.kind == wm::Kind::ArtifactObject); CHECK(f.tier == wm::Tier::High); CHECK(f.score >= 85);
            CHECK(f.pages == 10); CHECK(f.objects == 10); CHECK(f.contactText);
        }
        auto o = tmp("wm_A_out.pdf");
        auto rr = wm::remove(dir / "A_artifact.pdf", o);
        CHECK(rr.ok); CHECK(rr.formsRemoved == 10);
        auto h = hist(o), c = hist(dir / "A_clean.pdf");
        for (const char* k : {"m", "l", "c", "re", "S", "f", "Do", "BDC", "EMC", "gs"}) CHECK(h.ops[k] == c.ops[k]);
        for (auto& n : h.res) CHECK(n.rfind("/KSP", 0) != 0);          // watermark form / graphics state are gone from the resources
        auto again = wm::analyze(o);
        CHECK(!again.hasRemovable());
    }
    // G: Acrobat-style background/watermark form (tilt + opacity live inside the form; Apple Preview does not draw it, WPS does)
    {
        auto r = wm::analyze(dir / "G_acrobat_bg.pdf");
        CHECK(r.ok); CHECK(r.pageCount == 6);
        const wm::Finding* art = nullptr;
        for (auto& f : r.findings) if (f.kind == wm::Kind::ArtifactObject) art = &f;
        CHECK(art != nullptr);
        if (art) { CHECK(art->tier == wm::Tier::High); CHECK(art->score >= 85); CHECK(art->pages == 5); CHECK(art->contactText); }
        auto o = tmp("wm_G_out.pdf");
        auto rr = wm::remove(dir / "G_acrobat_bg.pdf", o);
        CHECK(rr.ok); CHECK(rr.formsRemoved == 5);
        auto h = hist(o), b = hist(dir / "G_acrobat_bg.pdf");
        for (const char* k : {"m", "l", "c", "re", "S", "Tj"}) CHECK(h.ops[k] == b.ops[k]);   // pattern lines and the page number are untouched
        CHECK(!wm::analyze(o).hasRemovable());
    }
    // B: shop banner drawn in the pattern's own layer (contact text on a coloured block + icons + QR code)
    {
        auto r = wm::analyze(dir / "B_block.pdf");
        CHECK(r.ok); CHECK(r.pageCount == 8);
        const wm::Finding* blk = nullptr;
        for (auto& f : r.findings) if (f.kind == wm::Kind::LayerBlock) blk = &f;
        CHECK(blk != nullptr);
        if (blk) { CHECK(blk->tier == wm::Tier::High); CHECK(blk->score >= 75); CHECK(blk->pages == 8); CHECK(blk->contactText); }
        auto o = tmp("wm_B_out.pdf");
        auto rr = wm::remove(dir / "B_block.pdf", o);
        CHECK(rr.ok); CHECK(rr.blockItemsRemoved > 100);
        auto h = hist(o), b = hist(dir / "B_block.pdf");
        CHECK(h.ops["S"] == b.ops["S"]);                                     // every stroked pattern line is still there
        CHECK(h.ops["l"] > 0.8 * b.ops["l"]);                                // (only the icons' filled outlines went)
        CHECK(h.ops["Tj"] < b.ops["Tj"] && h.ops["Tj"] >= 8);               // contact text gone, one page number per page kept
        CHECK(h.ops["Do"] < b.ops["Do"]);                                    // QR image and the grey backing form are gone
        auto again = wm::analyze(o);
        for (auto& f : again.findings) CHECK(f.kind != wm::Kind::LayerBlock);
    }
    // H: characters drawn as line strokes ("小红叶501" in a CAD single-stroke font): box one instance, remove every copy
    {
        const auto src = dir / "H_vector_text.pdf";
        CHECK(wm::analyze(src).ok);
        auto o = tmp("wm_H_out.pdf");
        auto bad = wm::removeShape(src, o, 2, {315, 590, 365, 712});
        CHECK(bad.ok);
        CHECK(bad.selected >= 12 && bad.selected <= 14);
        CHECK(bad.copies >= 6);                       // copies of different size, turned by 180 degrees, and cut by the page edge
        CHECK(bad.removed >= 90 && bad.removed <= 130);
        CHECK(bad.pagesTouched.size() >= 7);
        auto h = hist(o), b = hist(src);
        CHECK(h.ops["S"] == b.ops["S"] - bad.removed + 0 || h.ops["S"] < b.ops["S"]);
        CHECK(h.ops["S"] >= b.ops["S"] - bad.removed - 2);                   // nothing else was dropped
        auto none = wm::removeShape(src, o, 2, {0, 0, 5, 5});
        CHECK(!none.ok);                              // empty box -> clear error, no output
        auto only = wm::removeShape(src, o, 2, {315, 590, 365, 712}, false);
        CHECK(only.ok && only.removed == only.selected && only.copies == 0);
    }
    // real text by content (decoded through ToUnicode)
    {
        std::string err;
        auto items = wm::listText(dir / "B_block.pdf", &err);
        bool haveHandle = false, haveWx = false;
        for (auto& t : items) { if (t.text == "Snowaishougong") haveHandle = true; if (t.text == "微信") haveWx = true; }
        CHECK(haveHandle && haveWx);
        auto o = tmp("wm_T_out.pdf");
        auto rr = wm::removeText(dir / "B_block.pdf", o, {"Snowaishougong", "992762650"});
        CHECK(rr.ok); CHECK(rr.removed >= 16);
        bool left = false;
        for (auto& t : wm::listText(o)) if (t.text.find("Snowaishougong") != std::string::npos || t.text.find("992762650") != std::string::npos) left = true;
        CHECK(!left);
        CHECK(!wm::removeText(dir / "B_block.pdf", o, {"不存在的内容"}).ok);
        CHECK(!wm::removeText(dir / "H_vector_text.pdf", o, {"小红叶"}).ok);   // strokes are not text: told to use the box instead
    }
    // C, D: flattened diagonal text
    for (const char* name : {"C_flat.pdf", "D_flat.pdf"}) {
        auto r = wm::analyze(dir / name);
        CHECK(r.ok);
        const wm::Finding* flat = nullptr; const wm::Finding* stamp = nullptr;
        for (auto& f : r.findings) { if (f.kind == wm::Kind::FlatText) flat = &f; if (f.kind == wm::Kind::PageStamp) stamp = &f; }
        CHECK(flat != nullptr);
        if (flat) { CHECK(flat->tier == wm::Tier::High); CHECK(flat->score >= 85); CHECK(!flat->restore.empty()); CHECK(flat->alpha > 0.1 && flat->alpha < 0.4); }
        if (std::string(name) == "C_flat.pdf") CHECK(stamp != nullptr && stamp->tier == wm::Tier::Keep);   // "第 1/6 页" must not be removed
        auto o = tmp("wm_flat_out.pdf");
        auto rr = wm::remove(dir / name, o);
        CHECK(rr.ok); CHECK(rr.textBlocksRemoved > 100); CHECK(rr.fillsRemoved > 100); CHECK(rr.coloursRestored > 100);
        auto again = wm::analyze(o);
        CHECK(again.ok);
        for (auto& f : again.findings) CHECK(f.kind != wm::Kind::FlatText);      // nothing left to find
        auto before = hist(dir / name), after = hist(o);
        CHECK(after.pages == before.pages);
        CHECK(after.ops["l"] == before.ops["l"] && after.ops["c"] == before.ops["c"]);   // geometry untouched
        if (std::string(name) == "C_flat.pdf") CHECK(after.res.count("/KSPX1") == 1);       // page stamp kept
    }
    // E: bitmap pages
    {
        auto r = wm::analyze(dir / "E_bitmap.pdf");
        CHECK(r.ok);
        bool bm = false; for (auto& f : r.findings) if (f.kind == wm::Kind::Bitmap && f.tier == wm::Tier::Unsupported) bm = true;
        CHECK(bm); CHECK(!r.hasRemovable());
        auto rr = wm::remove(dir / "E_bitmap.pdf", tmp("wm_E_out.pdf"));
        CHECK(!rr.ok);
    }
    // batch: grouping, recipes, integrity check
    {
        std::vector<wm::Report> reps;
        for (const char* n : {"C_flat.pdf", "D_flat.pdf", "B_block.pdf", "A_clean.pdf", "E_bitmap.pdf", "nope.pdf"}) reps.push_back(wm::analyze(dir / n));
        auto g = wm::groupReports(reps);
        CHECK(g[0] == g[1] && g[0] >= 0);              // two flattened-text files: one group
        CHECK(g[2] >= 0 && g[2] != g[0]);              // banner: another group
        CHECK(g[3] == -1 && g[4] == -2 && g[5] == -3); // clean, bitmap, unreadable
        // recipe round trip
        wm::Recipe rc; rc.name = "测试配方"; rc.signature = wm::signatureOf(reps[0]); rc.texts = {"小红叶", "SAMPLE"};
        wm::ShapeTemplate t; t.name = "图形"; t.strokes = {{{1.5, 2.25}, {3.125, 4.5}}, {{0, 0}, {10, 10}, {20, 5}}};
        rc.shapes.push_back(t);
        auto rf = tmp("wm_recipe.wmrecipe");
        CHECK(wm::saveRecipe(rf, rc));
        wm::Recipe back; CHECK(wm::loadRecipe(rf, back));
        CHECK(back.name == rc.name && back.signature == rc.signature && back.texts == rc.texts && back.shapes.size() == 1 && back.shapes[0].strokes.size() == 2);
        CHECK(back.shapes[0].strokes[1].size() == 3 && std::fabs(back.shapes[0].strokes[0][1].first - 3.125) < 1e-6);
        CHECK(!wm::loadRecipe(dir / "A_clean.pdf", back));
        // conformity: same type scores high, different type scores low
        CHECK(wm::conformity(reps[1], rc) >= 85);
        CHECK(wm::conformity(reps[2], rc) < 70);
        // flattened text recipe: strokes unchanged, verified
        wm::Recipe flat; flat.signature = wm::signatureOf(reps[0]);
        auto o = tmp("wm_batch_flat.pdf");
        auto fr = wm::applyRecipe(dir / "D_flat.pdf", o, flat);
        CHECK(fr.ok && fr.verified && fr.strokesBefore == fr.strokesAfter && fr.removedTotal() > 100);
        CHECK(!wm::applyRecipe(dir / "D_flat.pdf", dir / "D_flat.pdf", flat).ok);        // never overwrite the original
        auto nothing = wm::applyRecipe(dir / "A_clean.pdf", tmp("wm_batch_none.pdf"), flat);
        CHECK(!nothing.ok && nothing.nothingToDo);
        CHECK(!std::filesystem::exists(tmp("wm_batch_none.pdf")));
        // stroke-drawn label template taken from one file, applied as a recipe: exact stroke accounting
        wm::Recipe sr; sr.removeDetected = false;
        wm::ShapeTemplate st; auto ex = wm::extractShape(dir / "H_vector_text.pdf", 2, {315, 590, 365, 712}, st);
        CHECK(ex.ok && ex.selected >= 12 && st.strokes.size() == (size_t)ex.selected);
        sr.shapes.push_back(st);
        auto so = tmp("wm_batch_shape.pdf");
        auto sres = wm::applyRecipe(dir / "H_vector_text.pdf", so, sr);
        CHECK(sres.ok && sres.verified && sres.shapesRemoved >= 90 && sres.strokesAfter == sres.strokesBefore - sres.shapesRemoved);
        CHECK(sres.strokesAfter == wm::countStrokes(so));
        // a template that is not in the file: nothing to do, no output
        auto none = wm::applyRecipe(dir / "C_flat.pdf", tmp("wm_batch_none2.pdf"), sr);
        CHECK(!none.ok && none.nothingToDo);
        // text + detected together on the banner file
        wm::Recipe both; both.signature = wm::signatureOf(reps[2]); both.texts = {"微信"};
        auto bo = tmp("wm_batch_both.pdf");
        auto br = wm::applyRecipe(dir / "B_block.pdf", bo, both);
        CHECK(br.ok && br.verified && br.textsRemoved > 0 && br.detected.ok);
    }
    // missing file
    CHECK(!wm::analyze(dir / "nope.pdf").ok);
    if (!fails) std::printf("test_wm: all passed\n");
    return fails ? 1 : 0;
}
