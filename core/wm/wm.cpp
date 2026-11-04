#include "wm.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>

namespace wm {
namespace {

constexpr double kPi = 3.14159265358979323846;
using Mat = std::array<double, 6>;
using Colour = std::vector<double>;

Mat mul(const Mat& m, const Mat& n) {   // row-vector convention: m applied first, then n
    return {m[0] * n[0] + m[1] * n[2], m[0] * n[1] + m[1] * n[3], m[2] * n[0] + m[3] * n[2],
            m[2] * n[1] + m[3] * n[3], m[4] * n[0] + m[5] * n[2] + n[4], m[4] * n[1] + m[5] * n[3] + n[5]};
}

std::string ckey(const Colour& c) {
    std::string s;
    char b[32];
    for (double v : c) { std::snprintf(b, sizeof b, "%.2f,", v); s += b; }
    return s;
}
bool isLight(const Colour& c) {
    if (c.empty() || c.size() == 4) return false;
    for (double v : c) if (v < 0.8) return false;
    return true;
}
bool axisAligned(double deg) {
    double r = std::fmod(std::fabs(deg), 90.0);
    return std::min(r, 90.0 - r) < 5.0;
}

// ---------------- content stream <-> instruction list ----------------
struct Instr {
    std::vector<QPDFObjectHandle> a;
    std::string op;
    QPDFObjectHandle inl;
    bool isInline = false;
};
class Collect : public QPDFObjectHandle::ParserCallbacks {
public:
    std::vector<Instr> out;
    std::vector<QPDFObjectHandle> pend;
    void handleObject(QPDFObjectHandle o) override {
        if (o.isOperator()) {
            Instr i; i.a = pend; i.op = o.getOperatorValue();
            out.push_back(std::move(i)); pend.clear();
        } else if (o.isInlineImage()) {
            Instr i; i.isInline = true; i.inl = o; i.op = "BI";
            out.push_back(std::move(i)); pend.clear();
        } else pend.push_back(o);
    }
    void handleEOF() override {}
};
std::string serialize(std::vector<Instr>& v) {
    std::string s;
    for (auto& i : v) {
        if (i.isInline) { s += i.inl.unparse(); s += '\n'; continue; }
        for (auto& o : i.a) { s += o.unparse(); s += ' '; }
        s += i.op; s += '\n';
    }
    return s;
}
bool numeric(Instr& i, Colour& c) {
    c.clear();
    for (auto& o : i.a) { if (!o.isNumber()) return false; c.push_back(o.getNumericValue()); }
    return !c.empty();
}
bool isFillColourOp(const std::string& op) { return op == "g" || op == "rg" || op == "k" || op == "sc" || op == "scn"; }
Mat numMat(Instr& i) {
    Mat m{1, 0, 0, 1, 0, 0};
    if (i.a.size() == 6) for (int k = 0; k < 6; ++k) m[k] = i.a[k].getNumericValue();
    return m;
}

// ---------------- graphics-state walker ----------------
struct GState { Mat ctm{1, 0, 0, 1, 0, 0}; Colour fill{0}; double alpha = 1; };

struct TextBlock { int angle = 0; double size = 0; Colour col; };
struct DoRec {
    std::string name; Mat m{}; double alpha = 1; bool inArtifact = false;
    bool isForm = false, isImage = false;
    std::string key; bool textOnly = false, contact = false; std::string stream;
};
struct PageInfo {
    std::vector<TextBlock> tbs;
    std::map<std::string, int> fills;
    std::map<std::string, Colour> fillCols;
    std::vector<DoRec> dos;
    double imgArea = 0, pageArea = 1;
    int pathOps = 0;
};

bool looksLikeContact(const std::string& s) {
    int digits = 0, run = 0; bool hasAlpha = false;
    for (unsigned char ch : s) {
        if (std::isdigit(ch)) { ++digits; ++run; }
        else if (std::isalpha(ch) && ch < 128) { hasAlpha = true; ++run; }
        else run = 0;
        if (digits >= 8) return true;
        if (run >= 6 && hasAlpha) return true;
        if (!std::isdigit(ch) && !(std::isalpha(ch) && ch < 128)) { digits = 0; hasAlpha = false; }
    }
    return false;
}
std::string collectStrings(std::vector<Instr>& v) {
    std::string s;
    for (auto& i : v) {
        if (i.op != "Tj" && i.op != "TJ" && i.op != "'" && i.op != "\"") continue;
        for (auto& o : i.a) {
            if (o.isString()) { s += o.getStringValue(); s += ' '; }
            else if (o.isArray()) for (int k = 0; k < o.getArrayNItems(); ++k) { auto e = o.getArrayItem(k); if (e.isString()) s += e.getStringValue(); }
        }
    }
    return s;
}

std::vector<Instr> parsePage(QPDFPageObjectHelper& p) {
    Collect cb; p.parseContents(&cb); return std::move(cb.out);
}

double extAlpha(QPDFObjectHandle res, const std::string& name) {
    auto g = res.getKey("/ExtGState");
    if (!g.isDictionary()) return 1;
    auto e = g.getKey(name);
    if (!e.isDictionary()) return 1;
    auto ca = e.getKey("/ca");
    return ca.isNumber() ? ca.getNumericValue() : 1;
}

PageInfo tracePage(QPDFPageObjectHelper& page, std::vector<Instr>& ins, std::map<std::string, DoRec>& formCache) {
    PageInfo pi;
    auto mb = page.getMediaBox().getArrayAsRectangle();
    pi.pageArea = std::max(1.0, std::fabs((mb.urx - mb.llx) * (mb.ury - mb.lly)));
    QPDFObjectHandle res = page.getObjectHandle().getKey("/Resources");
    QPDFObjectHandle xo = res.isDictionary() ? res.getKey("/XObject") : QPDFObjectHandle::newNull();
    GState g; std::vector<GState> st; std::vector<bool> marks;
    Mat tm{1, 0, 0, 1, 0, 0}; double fsize = 1;
    bool inBT = false, shown = false; int artDepth = 0;
    for (auto& i : ins) {
        const std::string& op = i.op;
        if (op == "q") st.push_back(g);
        else if (op == "Q") { if (!st.empty()) { g = st.back(); st.pop_back(); } }
        else if (op == "cm") g.ctm = mul(numMat(i), g.ctm);
        else if (op == "gs" && !i.a.empty() && i.a[0].isName()) g.alpha = extAlpha(res, i.a[0].getName());
        else if (isFillColourOp(op)) { Colour c; if (numeric(i, c)) g.fill = c; }
        else if (op == "BDC" || op == "BMC") {
            bool art = !i.a.empty() && i.a[0].isName() && i.a[0].getName() == "/Artifact";
            marks.push_back(art); if (art) ++artDepth;
        } else if (op == "EMC") { if (!marks.empty()) { if (marks.back()) --artDepth; marks.pop_back(); } }
        else if (op == "BT") { inBT = true; shown = false; tm = {1, 0, 0, 1, 0, 0}; }
        else if (op == "ET") inBT = false;
        else if (op == "Tm" && i.a.size() == 6) tm = numMat(i);
        else if (op == "Tf" && i.a.size() == 2) fsize = i.a[1].getNumericValue();
        else if ((op == "Tj" || op == "TJ") && inBT && !shown) {
            shown = true;
            Mat m = mul(tm, g.ctm);
            TextBlock tb; tb.angle = (int)std::lround(std::atan2(m[1], m[0]) * 180 / kPi);
            tb.angle = ((tb.angle % 360) + 360) % 360;
            tb.size = std::round(fsize * std::hypot(m[0], m[1]) * 10) / 10;
            tb.col = g.fill; pi.tbs.push_back(tb);
        } else if (op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*") {
            std::string k = ckey(g.fill); pi.fills[k]++; pi.fillCols[k] = g.fill; ++pi.pathOps;
        } else if (op == "S" || op == "s") ++pi.pathOps;
        else if (op == "Do" && !i.a.empty() && i.a[0].isName()) {
            DoRec d; d.name = i.a[0].getName(); d.m = g.ctm; d.alpha = g.alpha; d.inArtifact = artDepth > 0;
            QPDFObjectHandle x = xo.isDictionary() ? xo.getKey(d.name) : QPDFObjectHandle::newNull();
            if (x.isStream()) {
                auto sub = x.getDict().getKey("/Subtype");
                if (sub.isName() && sub.getName() == "/Form") {
                    d.isForm = true;
                    auto ck = std::to_string(x.getObjectID()) + "/" + std::to_string(x.getGeneration());
                    auto it = formCache.find(ck);
                    if (it == formCache.end()) {
                        DoRec f;
                        try {
                            auto buf = x.getStreamData(qpdf_dl_generalized);
                            f.stream.assign((const char*)buf->getBuffer(), buf->getSize());
                            Collect cb; x.parseAsContents(&cb);
                            bool text = false, other = false;
                            for (auto& fi : cb.out) {
                                if (fi.op == "Tj" || fi.op == "TJ") text = true;
                                else if (fi.op == "S" || fi.op == "f" || fi.op == "f*" || fi.op == "B" || fi.op == "Do" || fi.op == "BI") other = true;
                            }
                            f.textOnly = text && !other;
                            f.contact = looksLikeContact(collectStrings(cb.out));
                        } catch (...) {}
                        f.key = std::to_string(std::hash<std::string>{}(f.stream));
                        it = formCache.emplace(ck, f).first;
                    }
                    d.key = it->second.key; d.textOnly = it->second.textOnly; d.contact = it->second.contact;
                } else if (sub.isName() && sub.getName() == "/Image") {
                    d.isImage = true;
                    pi.imgArea += std::fabs(g.ctm[0] * g.ctm[3] - g.ctm[1] * g.ctm[2]);
                }
            }
            pi.dos.push_back(d);
        }
    }
    return pi;
}

std::string groupKey(const DoRec& d) {
    char b[200];
    std::snprintf(b, sizeof b, "%s|%.0f,%.0f,%.0f,%.0f,%.0f,%.0f|%.2f", d.key.c_str(), d.m[0] * 100, d.m[1] * 100, d.m[2] * 100,
                  d.m[3] * 100, d.m[4], d.m[5], d.alpha);
    return b;
}
double angleOf(const Mat& m) { return std::atan2(m[1], m[0]) * 180 / kPi; }

Tier tierFor(int score) { return score >= 75 ? Tier::High : (score >= 45 ? Tier::Confirm : Tier::Keep); }

struct Analysis {
    Report rep;
    std::vector<std::vector<Instr>> ins;
    std::vector<PageInfo> pages;
};

// fit  blended = (1-a)*orig + (G-(1-a))  over candidate original colours that exist in the file
double fitAlpha(const Colour& G, const std::vector<Colour>& blended, const std::vector<Colour>& origs, double& errOut) {
    double best = 1e9, ba = 0.2;
    for (int k = 5; k <= 60; ++k) {
        double al = k / 100.0, err = 0;
        for (auto& b : blended) {
            double m = 9;
            for (auto& o : origs) {
                if (o.size() != b.size()) continue;
                double e = 0;
                for (size_t j = 0; j < std::min<size_t>(3, b.size()); ++j) e = std::max(e, std::fabs(b[j] - ((1 - al) * o[j] + G[j] - (1 - al))));
                m = std::min(m, e);
            }
            err += m;
        }
        if (err < best) { best = err; ba = al; }
    }
    errOut = best;
    return ba;
}

void detectFlat(Analysis& A) {
    int n = (int)A.pages.size();
    std::map<std::pair<int, double>, int> runs;
    std::map<std::pair<int, double>, int> runPages;
    for (auto& p : A.pages) {
        std::set<std::pair<int, double>> seen;
        for (auto& t : p.tbs) { auto k = std::make_pair(t.angle, t.size); runs[k]++; seen.insert(k); }
        for (auto& k : seen) runPages[k]++;
    }
    if (runs.empty()) return;
    auto dom = std::max_element(runs.begin(), runs.end(), [](auto& a, auto& b) { return a.second < b.second; });
    auto key = dom->first;
    if (axisAligned(key.first) || dom->second < 20) return;
    std::map<std::string, int> cols; std::map<std::string, Colour> colv;
    int total = 0;
    for (auto& p : A.pages) for (auto& t : p.tbs) if (std::make_pair(t.angle, t.size) == key) { auto k = ckey(t.col); cols[k]++; colv[k] = t.col; ++total; }
    std::string gk; int gb = 0;
    for (auto& [k, c] : cols) if (isLight(colv[k]) && c > gb) { gb = c; gk = k; }
    if (gk.empty()) return;
    Colour G = colv[gk];
    std::vector<Colour> blended, origs; std::map<std::string, int> fillCount;
    for (auto& [k, c] : cols) if (k != gk) blended.push_back(colv[k]);
    for (auto& p : A.pages) for (auto& [k, c] : p.fills) { fillCount[k] += c; }
    std::map<std::string, Colour> fcv;
    for (auto& p : A.pages) for (auto& [k, c] : p.fillCols) fcv[k] = c;
    for (auto& [k, c] : fillCount) if (!cols.count(k) && k != gk && c >= 3) origs.push_back(fcv[k]);
    Finding f; f.kind = Kind::FlatText; f.lightColour = G;
    double err = 0; f.alpha = blended.empty() ? 0 : fitAlpha(G, blended, origs, err);
    int fills = 0; auto git = fillCount.find(gk); if (git != fillCount.end()) fills = git->second;
    bool fitOk = true;
    for (auto& b : blended) {
        double al = f.alpha;
        double bestE = 9; Colour bestO; int bestCnt = -1;
        for (auto& o : origs) {
            if (o.size() != b.size()) continue;
            double e = 0;
            for (size_t j = 0; j < std::min<size_t>(3, b.size()); ++j) e = std::max(e, std::fabs(b[j] - ((1 - al) * o[j] + G[j] - (1 - al))));
            if (e < 0.06 && fillCount[ckey(o)] > bestCnt) { bestCnt = fillCount[ckey(o)]; bestO = o; bestE = e; }
            else if (bestCnt < 0 && e < bestE) { bestE = e; bestO = o; }
        }
        Colour r;
        if (bestE < 0.06 && !bestO.empty()) r = bestO;
        else {   // analytic inverse
            for (size_t j = 0; j < b.size(); ++j) r.push_back(std::round(std::min(1.0, std::max(0.0, (b[j] - (G[j] - (1 - al))) / (1 - al))) * 100) / 100);
            if (bestE > 0.12) fitOk = false;
        }
        f.restore[ckey(b)] = r;
    }
    int score = 0;
    f.evidence.push_back("倾斜 " + std::to_string(key.first) + "°（纸样为 0°/90°） +25"); score += 25;
    f.evidence.push_back("浅色文字（接近白底上的浅灰） +20"); score += 20;
    int pg = runPages[key];
    if (n >= 2 && pg >= 0.8 * n) { f.evidence.push_back("在 " + std::to_string(pg) + "/" + std::to_string(n) + " 页重复出现 +20"); score += 20; }
    else if (n == 1) { f.evidence.push_back("单页文件，无法跨页比对 +10"); score += 10; }
    f.evidence.push_back("同一字号 " + std::to_string((int)key.second) + "pt 的文字 " + std::to_string(total) + " 个 +15"); score += 15;
    if (!blended.empty() && fitOk) { f.evidence.push_back("线条被混色，已估出混合比例 " + std::to_string((int)std::lround(f.alpha * 100)) + "% +15"); score += 15; }
    else if (blended.empty()) { f.evidence.push_back("未发现被混色的线条（未被压平） +5"); score += 5; }
    f.score = score; f.tier = tierFor(score);
    f.label = "斜铺文字水印" + std::string(blended.empty() ? "" : "（已压平，去除时还原线条色）");
    f.pages = pg; f.objects = gb + fills;
    A.rep.findings.push_back(f);
}

void detectArtifacts(Analysis& A) {
    int n = (int)A.pages.size();
    struct Grp { std::vector<DoRec> recs; std::set<int> pages; };
    std::map<std::string, Grp> groups;
    int stampPages = 0, stampObjs = 0; std::set<int> stampPageSet;
    for (int pi = 0; pi < n; ++pi)
        for (auto& d : A.pages[pi].dos) if (d.isForm) { auto& g = groups[groupKey(d)]; g.recs.push_back(d); g.pages.insert(pi); }
    for (auto& [k, g] : groups) {
        const DoRec& d = g.recs[0];
        bool allArt = true; for (auto& r : g.recs) if (!r.inArtifact) allArt = false;
        bool ksp = d.name.rfind("/KSPX", 0) == 0;
        int score = 0; Finding f; f.kind = Kind::ArtifactObject; f.formKey = k;
        double ang = angleOf(d.m);
        if (!axisAligned(ang)) { score += 25; f.evidence.push_back("倾斜 " + std::to_string((int)std::lround(ang)) + "° +25"); }
        if (d.alpha <= 0.5) { score += 20; f.evidence.push_back("半透明（不透明度 " + std::to_string((int)std::lround(d.alpha * 100)) + "%） +20"); }
        else if (d.alpha < 0.95) { score += 10; f.evidence.push_back("略透明 +10"); }
        if (n >= 3 && (int)g.pages.size() >= 0.8 * n) { score += 20; f.evidence.push_back("内容和位置在 " + std::to_string(g.pages.size()) + "/" + std::to_string(n) + " 页完全相同 +20"); }
        if (allArt) { score += 10; f.evidence.push_back("标记为页面装饰（Artifact） +10"); }
        if (ksp) { score += 10; f.evidence.push_back("对象名带编辑软件的叠加记号（KSPX） +10"); }
        if (d.textOnly) { score += 10; f.evidence.push_back("只含文字 +10"); }
        f.score = score; f.tier = tierFor(score); f.pages = (int)g.pages.size(); f.objects = (int)g.recs.size(); f.contactText = d.contact;
        if (f.tier == Tier::Keep) {
            if (d.textOnly || ksp) { stampObjs += (int)g.recs.size(); for (int p : g.pages) stampPageSet.insert(p); }
            continue;
        }
        f.label = d.contact ? "叠加文字（含联系方式文字）" : "叠加文字对象";
        A.rep.findings.push_back(f);
    }
    if (stampObjs > 0) {
        Finding f; f.kind = Kind::PageStamp; f.tier = Tier::Keep; f.score = 30;
        f.label = "页眉页脚 / 页码类叠加（默认保留）"; f.pages = (int)stampPageSet.size(); f.objects = stampObjs;
        f.evidence.push_back("正向、不透明或各页内容不同，不像水印");
        A.rep.findings.push_back(f);
    }
}

void detectBitmap(Analysis& A) {
    int n = (int)A.pages.size(), big = 0;
    for (auto& p : A.pages) if (p.imgArea / p.pageArea >= 0.6) ++big;
    if (n == 0 || big < 0.5 * n) return;
    Finding f; f.kind = Kind::Bitmap; f.tier = Tier::Unsupported; f.score = 100; f.pages = big;
    f.label = "整页位图：水印已和纸样合成在图片里，无法安全去除";
    f.evidence.push_back(std::to_string(big) + "/" + std::to_string(n) + " 页被图片铺满");
    A.rep.findings.push_back(f);
}

bool load(QPDF& pdf, const std::filesystem::path& in, std::string& err) {
    try { pdf.processFile(in.u8string().c_str()); return true; }
    catch (std::exception& e) { err = e.what(); return false; }
}

Analysis run(QPDF& pdf, const std::filesystem::path& in) {
    Analysis A;
    std::string err;
    if (!load(pdf, in, err)) { A.rep.error = err; return A; }
    try {
        QPDFPageDocumentHelper doc(pdf);
        doc.pushInheritedAttributesToPage();
        auto pages = doc.getAllPages();
        A.rep.pageCount = (int)pages.size();
        std::map<std::string, DoRec> cache;
        for (auto& p : pages) {
            A.ins.push_back(parsePage(p));
            A.pages.push_back(tracePage(p, A.ins.back(), cache));
        }
        detectArtifacts(A);
        detectFlat(A);
        detectBitmap(A);
        std::stable_sort(A.rep.findings.begin(), A.rep.findings.end(), [](const Finding& a, const Finding& b) {
            if (a.tier != b.tier) return (int)a.tier < (int)b.tier;
            return a.score > b.score;
        });
        A.rep.ok = true;
    } catch (std::exception& e) { A.rep.error = e.what(); }
    return A;
}

std::string esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; }
    return o;
}

}  // namespace

bool Report::hasRemovable() const {
    for (auto& f : findings) if (f.tier == Tier::High) return true;
    return false;
}

Report analyze(const std::filesystem::path& in) {
    QPDF pdf; return run(pdf, in).rep;
}

RemoveResult remove(const std::filesystem::path& in, const std::filesystem::path& out, const std::vector<int>& alsoRemove) {
    RemoveResult R;
    QPDF pdf;
    Analysis A = run(pdf, in);
    if (!A.rep.ok) { R.error = A.rep.error; return R; }
    std::set<int> pick(alsoRemove.begin(), alsoRemove.end());
    std::set<std::string> dropForms; const Finding* flat = nullptr;
    for (int i = 0; i < (int)A.rep.findings.size(); ++i) {
        auto& f = A.rep.findings[i];
        if (f.tier == Tier::Unsupported) continue;
        if (f.tier != Tier::High && !pick.count(i)) continue;
        if (f.kind == Kind::ArtifactObject) dropForms.insert(f.formKey);
        if (f.kind == Kind::FlatText) flat = &f;
    }
    if (dropForms.empty() && !flat) { R.error = "没有可去除的水印"; return R; }
    try {
        QPDFPageDocumentHelper doc(pdf);
        auto pages = doc.getAllPages();
        std::map<std::string, DoRec> cache;
        const Colour G = flat ? flat->lightColour : Colour{};
        const std::string Gk = flat ? ckey(G) : "";
        for (size_t pi = 0; pi < pages.size(); ++pi) {
            auto& page = pages[pi];
            std::vector<Instr> ins = A.ins[pi];
            std::vector<bool> kill(ins.size(), false);
            std::set<std::string> touchedNames;
            // --- artifact objects: find Do of a dropped form, remove its marker block / q..Q wrapper
            if (!dropForms.empty()) {
                QPDFObjectHandle res = page.getObjectHandle().getKey("/Resources");
                QPDFObjectHandle xo = res.isDictionary() ? res.getKey("/XObject") : QPDFObjectHandle::newNull();
                GState g; std::vector<GState> st; std::vector<size_t> markStart;
                for (size_t k = 0; k < ins.size(); ++k) {
                    auto& i = ins[k];
                    if (i.op == "q") st.push_back(g);
                    else if (i.op == "Q") { if (!st.empty()) { g = st.back(); st.pop_back(); } }
                    else if (i.op == "cm") g.ctm = mul(numMat(i), g.ctm);
                    else if (i.op == "gs" && !i.a.empty() && i.a[0].isName()) g.alpha = extAlpha(res, i.a[0].getName());
                    else if (i.op == "BDC" || i.op == "BMC") markStart.push_back(k);
                    else if (i.op == "EMC") { if (!markStart.empty()) markStart.pop_back(); }
                    else if (i.op == "Do" && !i.a.empty() && i.a[0].isName() && xo.isDictionary()) {
                        QPDFObjectHandle x = xo.getKey(i.a[0].getName());
                        if (!x.isStream()) continue;
                        auto sub = x.getDict().getKey("/Subtype");
                        if (!(sub.isName() && sub.getName() == "/Form")) continue;
                        DoRec d; d.name = i.a[0].getName(); d.m = g.ctm; d.alpha = g.alpha;
                        auto ck = std::to_string(x.getObjectID()) + "/" + std::to_string(x.getGeneration());
                        auto it = cache.find(ck);
                        if (it == cache.end()) {
                            auto buf = x.getStreamData(qpdf_dl_generalized);
                            DoRec f; f.stream.assign((const char*)buf->getBuffer(), buf->getSize());
                            f.key = std::to_string(std::hash<std::string>{}(f.stream));
                            it = cache.emplace(ck, f).first;
                        }
                        d.key = it->second.key;
                        if (!dropForms.count(groupKey(d))) continue;
                        size_t s = k, e = k;
                        if (!markStart.empty()) {   // whole marker block, if it only wraps this stamp
                            size_t ms = markStart.back(), me = k;
                            int depth = 0; bool simple = true;
                            for (size_t t = ms + 1; t < ins.size(); ++t) {
                                if (ins[t].op == "EMC" && depth == 0) { me = t; break; }
                                if (ins[t].op == "BDC" || ins[t].op == "BMC") ++depth;
                                if (ins[t].op == "EMC") --depth;
                            }
                            for (size_t t = ms + 1; t < me; ++t) {
                                const std::string& o = ins[t].op;
                                if (o != "gs" && o != "Do" && o != "cm" && o != "q" && o != "Q") simple = false;
                            }
                            if (simple && me > k) { s = ms; e = me; }
                        }
                        if (s == k) {   // not in a marker block: take preceding gs
                            while (s > 0 && ins[s - 1].op == "gs") --s;
                        }
                        if (s >= 2 && e + 1 < ins.size() && ins[s - 1].op == "cm" && ins[s - 2].op == "q" && ins[e + 1].op == "Q") { s -= 2; ++e; }
                        for (size_t t = s; t <= e; ++t) {
                            kill[t] = true;
                            if (ins[t].op == "Do" || ins[t].op == "gs") if (!ins[t].a.empty() && ins[t].a[0].isName()) touchedNames.insert(ins[t].op + ins[t].a[0].getName());
                        }
                        ++R.formsRemoved;
                    }
                }
            }
            // --- flattened text
            if (flat) {
                GState g; std::vector<GState> st; bool inBT = false, shown = false; size_t btStart = 0; Colour btCol;
                std::vector<std::pair<size_t, size_t>> btRanges; std::vector<std::string> btColKey;
                for (size_t k = 0; k < ins.size(); ++k) {
                    if (kill[k]) continue;
                    auto& i = ins[k];
                    const std::string& op = i.op;
                    if (op == "q") st.push_back(g);
                    else if (op == "Q") { if (!st.empty()) { g = st.back(); st.pop_back(); } }
                    else if (isFillColourOp(op)) {
                        Colour c;
                        if (numeric(i, c)) {
                            auto it = flat->restore.find(ckey(c));
                            if (it != flat->restore.end() && it->second.size() == c.size()) {
                                for (size_t j = 0; j < c.size(); ++j) i.a[j] = QPDFObjectHandle::newReal(it->second[j], 3);
                                c = it->second; ++R.coloursRestored;
                            }
                            g.fill = c;
                        }
                    } else if (op == "BT") { inBT = true; shown = false; btStart = k; }
                    else if ((op == "Tj" || op == "TJ") && inBT && !shown) { shown = true; btCol = g.fill; }
                    else if (op == "ET") {
                        inBT = false;
                        if (shown && ckey(btCol) == Gk) { for (size_t t = btStart; t <= k; ++t) kill[t] = true; ++R.textBlocksRemoved; }
                    } else if ((op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*") && ckey(g.fill) == Gk) {
                        std::string nop = (op == "B" || op == "B*") ? "S" : (op == "b" || op == "b*") ? "s" : "n";
                        i.op = nop; ++R.fillsRemoved;
                    }
                }
            }
            std::vector<Instr> outIns;
            for (size_t k = 0; k < ins.size(); ++k) if (!kill[k]) outIns.push_back(ins[k]);
            std::string data = serialize(outIns);
            page.getObjectHandle().replaceKey("/Contents", QPDFObjectHandle::newStream(&pdf, data));
            // drop resource entries that no longer have a user, so the watermark text does not stay in the file
            if (!touchedNames.empty()) {
                QPDFObjectHandle res = page.getObjectHandle().getKey("/Resources");
                if (res.isDictionary()) {
                    QPDFObjectHandle nres = res.shallowCopy();
                    page.getObjectHandle().replaceKey("/Resources", nres);
                    for (auto& tn : touchedNames) {
                        bool isDo = tn.rfind("Do", 0) == 0; std::string name = tn.substr(isDo ? 2 : 2);
                        bool used = false;
                        for (auto& i : outIns) if ((i.op == "Do" || i.op == "gs") && !i.a.empty() && i.a[0].isName() && i.a[0].getName() == name && i.op == (isDo ? "Do" : "gs")) used = true;
                        if (used) continue;
                        const char* dk = isDo ? "/XObject" : "/ExtGState";
                        QPDFObjectHandle d = nres.getKey(dk);
                        if (d.isDictionary()) { QPDFObjectHandle nd = d.shallowCopy(); nd.removeKey(name); nres.replaceKey(dk, nd); }
                    }
                }
            }
        }
        QPDFWriter w(pdf, out.u8string().c_str());
        w.setCompressStreams(true);
        w.setDecodeLevel(qpdf_dl_none);
        w.write();
        R.ok = true;
    } catch (std::exception& e) { R.error = e.what(); }
    return R;
}

std::string toJson(const Report& r) {
    std::ostringstream o;
    o << "{\"ok\":" << (r.ok ? "true" : "false") << ",\"pages\":" << r.pageCount << ",\"error\":\"" << esc(r.error) << "\",\"findings\":[";
    for (size_t i = 0; i < r.findings.size(); ++i) {
        auto& f = r.findings[i];
        const char* t[] = {"high", "confirm", "keep", "unsupported"};
        const char* k[] = {"artifact", "flat_text", "page_stamp", "bitmap"};
        o << (i ? "," : "") << "{\"index\":" << i << ",\"kind\":\"" << k[(int)f.kind] << "\",\"tier\":\"" << t[(int)f.tier] << "\",\"score\":" << f.score
          << ",\"label\":\"" << esc(f.label) << "\",\"pages\":" << f.pages << ",\"objects\":" << f.objects
          << ",\"contactText\":" << (f.contactText ? "true" : "false") << ",\"evidence\":[";
        for (size_t j = 0; j < f.evidence.size(); ++j) o << (j ? "," : "") << "\"" << esc(f.evidence[j]) << "\"";
        o << "]}";
    }
    o << "]}";
    return o.str();
}

}  // namespace wm
