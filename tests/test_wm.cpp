#include "wm/wm.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
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
    Hist h; QPDF q; q.processFile(p.u8string().c_str());
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
    // missing file
    CHECK(!wm::analyze(dir / "nope.pdf").ok);
    if (!fails) std::printf("test_wm: all passed\n");
    return fails ? 1 : 0;
}
