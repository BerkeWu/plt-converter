#include "wm.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <array>
#include <cctype>
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
    // facts read from inside the form (Acrobat-style stamps keep the tilt and the opacity in the form itself)
    bool hasInnerAngle = false; double innerAngle = 0; double innerAlpha = 1; bool acrobat = false;
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


struct FormFacts { bool text = false, other = false, hasAngle = false; double angle = 0, alpha = 1; };
bool isWhite(const Colour& c) {
    if (c.empty()) return false;
    if (c.size() == 4) { for (double v : c) if (v > 0.02) return false; return true; }   // CMYK 0,0,0,0
    for (double v : c) if (v < 0.98) return false;
    return true;
}
// Walks the content of a Form XObject: tilt of its first text, lowest opacity it sets, whether it draws anything
// besides text (white filler shapes, which Acrobat adds around a stamp, do not count).
FormFacts scanForm(std::vector<Instr>& v, QPDFObjectHandle formRes) {
    FormFacts F;
    GState g; std::vector<GState> st; Colour stroke{0};
    std::vector<Colour> strokeSt;
    Mat tm{1, 0, 0, 1, 0, 0}; bool inBT = false, gotAngle = false;
    for (auto& i : v) {
        const std::string& op = i.op;
        if (op == "q") { st.push_back(g); strokeSt.push_back(stroke); }
        else if (op == "Q") { if (!st.empty()) { g = st.back(); st.pop_back(); stroke = strokeSt.back(); strokeSt.pop_back(); } }
        else if (op == "cm") g.ctm = mul(numMat(i), g.ctm);
        else if (op == "gs" && !i.a.empty() && i.a[0].isName()) { g.alpha = extAlpha(formRes, i.a[0].getName()); F.alpha = std::min(F.alpha, g.alpha); }
        else if (isFillColourOp(op)) { Colour c; if (numeric(i, c)) g.fill = c; }
        else if (op == "G" || op == "RG" || op == "K" || op == "SC" || op == "SCN") { Colour c; if (numeric(i, c)) stroke = c; }
        else if (op == "BT") { inBT = true; tm = {1, 0, 0, 1, 0, 0}; }
        else if (op == "ET") inBT = false;
        else if (op == "Tm" && i.a.size() == 6) tm = numMat(i);
        else if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
            F.text = true;
            if (inBT && !gotAngle) {
                Mat m = mul(tm, g.ctm);
                F.angle = std::atan2(m[1], m[0]) * 180 / kPi; F.hasAngle = true; gotAngle = true;
            }
        }
        else if (op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*") {
            const bool strokes = (op[0] == 'B' || op[0] == 'b');
            if (!isWhite(g.fill) || (strokes && !isWhite(stroke))) F.other = true;
        }
        else if (op == "S" || op == "s") { if (!isWhite(stroke)) F.other = true; }
        else if (op == "Do" || op == "BI" || op == "sh") F.other = true;
    }
    return F;
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
                            QPDFObjectHandle fres = x.getDict().getKey("/Resources");
                            FormFacts ff = scanForm(cb.out, fres.isDictionary() ? fres : res);
                            f.textOnly = ff.text && !ff.other;
                            f.hasInnerAngle = ff.hasAngle; f.innerAngle = ff.angle; f.innerAlpha = ff.alpha;
                            f.contact = looksLikeContact(collectStrings(cb.out));
                            auto pinfo = x.getDict().getKey("/PieceInfo");
                            f.acrobat = pinfo.isDictionary() && pinfo.hasKey("/ADBE_CompoundType");
                        } catch (...) {}
                        f.key = std::to_string(std::hash<std::string>{}(f.stream));
                        it = formCache.emplace(ck, f).first;
                    }
                    d.key = it->second.key; d.textOnly = it->second.textOnly; d.contact = it->second.contact;
                    d.hasInnerAngle = it->second.hasInnerAngle; d.innerAngle = it->second.innerAngle; d.innerAlpha = it->second.innerAlpha; d.acrobat = it->second.acrobat;
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
    std::vector<QPDFPageObjectHelper> pageObjs;
    // layer block (B): instruction indices to drop per page, and the XObject names those instructions used
    std::vector<std::vector<size_t>> bKill;
    std::vector<std::set<std::string>> bNames;
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
        // tilt / opacity written inside the form only count when the form is a stamp on most pages (or carries the
        // Acrobat background marker); a one-off rotated label in an ordinary pattern page must not look like a watermark
        const bool innerOk = d.acrobat || (n >= 3 && (int)g.pages.size() >= 0.6 * n);
        if (innerOk && axisAligned(ang) && d.hasInnerAngle) ang += d.innerAngle;
        const double alpha = innerOk ? std::min(d.alpha, d.innerAlpha) : d.alpha;
        if (!axisAligned(ang)) { score += 25; f.evidence.push_back("倾斜 " + std::to_string((int)std::lround(ang)) + "° +25"); }
        if (alpha <= 0.5) { score += 20; f.evidence.push_back("半透明（不透明度 " + std::to_string((int)std::lround(alpha * 100)) + "%） +20"); }
        else if (alpha < 0.95) { score += 10; f.evidence.push_back("略透明 +10"); }
        if (n >= 3 && (int)g.pages.size() >= 0.8 * n) { score += 20; f.evidence.push_back("内容和位置在 " + std::to_string(g.pages.size()) + "/" + std::to_string(n) + " 页完全相同 +20"); }
        if (allArt) { score += 10; f.evidence.push_back("标记为页面装饰（Artifact） +10"); }
        if (ksp) { score += 10; f.evidence.push_back("对象名带编辑软件的叠加记号（KSPX） +10"); }
        if (d.textOnly) { score += 10; f.evidence.push_back("只含文字 +10"); }
        if (d.acrobat && !axisAligned(ang)) { score += 10; f.evidence.push_back("带编辑软件的背景/水印记号（PieceInfo） +10"); }
        score = std::min(score, 100);
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


// ---------------- B: shop banner drawn in the pattern's own layer ----------------
struct Box {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    bool valid() const { return x1 >= x0 && y1 >= y0; }
    void add(double x, double y) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    void add(const Box& b) { if (b.valid()) { add(b.x0, b.y0); add(b.x1, b.y1); } }
    double area() const { return valid() ? (x1 - x0) * (y1 - y0) : 0; }
    double cx() const { return (x0 + x1) / 2; }
    double cy() const { return (y0 + y1) / 2; }
    bool has(double x, double y, double pad = 0) const { return valid() && x >= x0 - pad && x <= x1 + pad && y >= y0 - pad && y <= y1 + pad; }
};
Box boxOfRect(double x0, double y0, double x1, double y1, const Mat& m) {
    Box b;
    const double xs[2] = {x0, x1}, ys[2] = {y0, y1};
    for (double x : xs) for (double y : ys) b.add(x * m[0] + y * m[2] + m[4], x * m[1] + y * m[3] + m[5]);
    return b;
}
struct Ev {
    enum Type { Text, Paint, DoImg, DoFillForm, DoOtherForm } type = Text;
    size_t first = 0, last = 0;      // instruction range (path construction .. paint op, or the single op)
    Box box;
    bool stroke = false, whiteFill = false, clipped = false;
    std::string str;                 // Text: shown bytes
    double x = 0, y = 0, size = 0;   // Text: origin
    std::string name;                // Do: resource name
    std::vector<std::pair<double, double>> pts;   // Paint: path points in page space (control points included)
};
bool fillOnlyForm(QPDFObjectHandle x) {
    try {
        Collect cb; x.parseAsContents(&cb);
        bool fill = false;
        for (auto& i : cb.out) {
            const std::string& o = i.op;
            if (o == "f" || o == "F" || o == "f*") fill = true;
            else if (o == "S" || o == "s" || o == "B" || o == "B*" || o == "b" || o == "b*" || o == "Tj" || o == "TJ" || o == "Do" || o == "BI" || o == "sh" || o == "'" || o == "\"") return false;
        }
        return fill;
    } catch (...) { return false; }
}
std::vector<Ev> traceEvents(QPDFPageObjectHelper& page, std::vector<Instr>& ins, std::map<std::string, bool>& fillFormCache) {
    std::vector<Ev> evs;
    QPDFObjectHandle res = page.getObjectHandle().getKey("/Resources");
    QPDFObjectHandle xo = res.isDictionary() ? res.getKey("/XObject") : QPDFObjectHandle::newNull();
    GState g; std::vector<GState> st;
    Mat tm{1, 0, 0, 1, 0, 0}, tlm{1, 0, 0, 1, 0, 0}; double fsize = 1, leading = 0;
    Box path; size_t pathFirst = SIZE_MAX; bool pathClip = false;
    std::vector<std::pair<double, double>> curPts;
    auto pt = [&](double x, double y) { const double X = x * g.ctm[0] + y * g.ctm[2] + g.ctm[4], Y = x * g.ctm[1] + y * g.ctm[3] + g.ctm[5]; path.add(X, Y); curPts.emplace_back(X, Y); };
    auto num = [](Instr& i, size_t k) { return k < i.a.size() && i.a[k].isNumber() ? i.a[k].getNumericValue() : 0.0; };
    auto showText = [&](size_t idx, Instr& i) {
        Ev e; e.type = Ev::Text; e.first = e.last = idx;
        Mat m = mul(tm, g.ctm);
        e.x = m[4]; e.y = m[5]; e.size = fsize * std::hypot(m[0], m[1]);
        for (auto& o : i.a) {
            if (o.isString()) e.str += o.getStringValue();
            else if (o.isArray()) for (int k = 0; k < o.getArrayNItems(); ++k) { auto t = o.getArrayItem(k); if (t.isString()) e.str += t.getStringValue(); }
        }
        evs.push_back(e);
    };
    for (size_t k = 0; k < ins.size(); ++k) {
        Instr& i = ins[k];
        const std::string& op = i.op;
        if (op == "q") st.push_back(g);
        else if (op == "Q") { if (!st.empty()) { g = st.back(); st.pop_back(); } }
        else if (op == "cm") g.ctm = mul(numMat(i), g.ctm);
        else if (isFillColourOp(op)) { Colour c; if (numeric(i, c)) g.fill = c; }
        else if (op == "m" || op == "l") { if (pathFirst == SIZE_MAX) pathFirst = k; pt(num(i, 0), num(i, 1)); }
        else if (op == "c") { if (pathFirst == SIZE_MAX) pathFirst = k; pt(num(i, 0), num(i, 1)); pt(num(i, 2), num(i, 3)); pt(num(i, 4), num(i, 5)); }
        else if (op == "v" || op == "y") { if (pathFirst == SIZE_MAX) pathFirst = k; pt(num(i, 0), num(i, 1)); pt(num(i, 2), num(i, 3)); }
        else if (op == "re") {
            if (pathFirst == SIZE_MAX) pathFirst = k;
            const double x = num(i, 0), y = num(i, 1), w = num(i, 2), h = num(i, 3);
            pt(x, y); pt(x + w, y); pt(x + w, y + h); pt(x, y + h);
        }
        else if (op == "h") { if (pathFirst == SIZE_MAX) pathFirst = k; }
        else if (op == "W" || op == "W*") pathClip = true;
        else if (op == "n") { path = Box(); pathFirst = SIZE_MAX; pathClip = false; curPts.clear(); }
        else if (op == "f" || op == "F" || op == "f*" || op == "S" || op == "s" || op == "B" || op == "B*" || op == "b" || op == "b*") {
            if (pathFirst != SIZE_MAX && path.valid()) {
                Ev e; e.type = Ev::Paint; e.first = pathFirst; e.last = k; e.box = path; e.clipped = pathClip;
                e.stroke = !(op == "f" || op == "F" || op == "f*");
                e.whiteFill = isWhite(g.fill);
                e.pts = curPts;
                evs.push_back(e);
            }
            path = Box(); pathFirst = SIZE_MAX; pathClip = false; curPts.clear();
        }
        else if (op == "BT") { tm = tlm = {1, 0, 0, 1, 0, 0}; }
        else if (op == "Tm" && i.a.size() == 6) tm = tlm = numMat(i);
        else if (op == "Td") { tlm = mul({1, 0, 0, 1, num(i, 0), num(i, 1)}, tlm); tm = tlm; }
        else if (op == "TD") { leading = -num(i, 1); tlm = mul({1, 0, 0, 1, num(i, 0), num(i, 1)}, tlm); tm = tlm; }
        else if (op == "TL") leading = num(i, 0);
        else if (op == "T*") { tlm = mul({1, 0, 0, 1, 0, -leading}, tlm); tm = tlm; }
        else if (op == "Tf" && i.a.size() == 2) fsize = i.a[1].getNumericValue();
        else if (op == "Tj" || op == "TJ") showText(k, i);
        else if (op == "'" || op == "\"") { tlm = mul({1, 0, 0, 1, 0, -leading}, tlm); tm = tlm; showText(k, i); }
        else if (op == "Do" && !i.a.empty() && i.a[0].isName()) {
            QPDFObjectHandle x = xo.isDictionary() ? xo.getKey(i.a[0].getName()) : QPDFObjectHandle::newNull();
            if (!x.isStream()) continue;
            auto sub = x.getDict().getKey("/Subtype");
            Ev e; e.first = e.last = k; e.name = i.a[0].getName();
            if (sub.isName() && sub.getName() == "/Image") { e.type = Ev::DoImg; e.box = boxOfRect(0, 0, 1, 1, g.ctm); }
            else if (sub.isName() && sub.getName() == "/Form") {
                auto bb = x.getDict().getKey("/BBox");
                Mat fm{1, 0, 0, 1, 0, 0};
                auto mt = x.getDict().getKey("/Matrix");
                if (mt.isArray() && mt.getArrayNItems() == 6) for (int q = 0; q < 6; ++q) fm[q] = mt.getArrayItem(q).getNumericValue();
                if (bb.isArray() && bb.getArrayNItems() == 4) e.box = boxOfRect(bb.getArrayItem(0).getNumericValue(), bb.getArrayItem(1).getNumericValue(), bb.getArrayItem(2).getNumericValue(), bb.getArrayItem(3).getNumericValue(), mul(fm, g.ctm));
                auto ck = std::to_string(x.getObjectID()) + "/" + std::to_string(x.getGeneration());
                auto it = fillFormCache.find(ck);
                if (it == fillFormCache.end()) it = fillFormCache.emplace(ck, fillOnlyForm(x)).first;
                e.type = it->second ? Ev::DoFillForm : Ev::DoOtherForm;
            } else continue;
            evs.push_back(e);
        }
    }
    return evs;
}

void detectBlocks(Analysis& A) {
    const int n = (int)A.pages.size();
    if (n < 1) return;
    std::vector<std::vector<Ev>> E(n);
    std::map<std::string, bool> ffc;
    for (int p = 0; p < n; ++p) E[p] = traceEvents(A.pageObjs[p], A.ins[p], ffc);
    // contact-like strings and how many pages carry them
    std::map<std::string, std::set<int>> seedPages, staticPages;
    struct Seen { int page; double x, y; };
    std::map<std::string, std::vector<Seen>> seen;   // where a string appears on each page (the banner may drift a few points)
    auto skey = [](const Ev& e) { return e.str; };
    for (int p = 0; p < n; ++p)
        for (auto& e : E[p]) if (e.type == Ev::Text && !e.str.empty()) {
            if (e.str.size() >= 6 && looksLikeContact(e.str)) seedPages[e.str].insert(p);
            seen[skey(e)].push_back({p, e.x, e.y});
        }
    // "same place on enough pages": pages where the same string sits within 40 pt of this one
    auto steady = [&](const Ev& e, int pagesNeeded) {
        std::set<int> ps;
        for (auto& s : seen[skey(e)]) if (std::fabs(s.x - e.x) <= 40 && std::fabs(s.y - e.y) <= 40) ps.insert(s.page);
        return (int)ps.size() >= pagesNeeded;
    };
    const int need = std::max(1, (int)std::ceil(0.6 * n));
    const int staticNeed = std::max(3, (int)std::ceil(0.8 * n));
    std::set<std::string> seedStr;
    for (auto& [s, ps] : seedPages) if ((int)ps.size() >= need) seedStr.insert(s);
    if (seedStr.empty()) return;

    A.bKill.assign(n, {}); A.bNames.assign(n, {});
    int pagesHit = 0, items = 0, onBlock = 0, sameSpot = 0, statics = 0, seedsAll = 0, pagesWithSeed = 0;
    double areaFrac = 0;
    for (int p = 0; p < n; ++p) {
        auto& ev = E[p];
        std::vector<size_t> seeds;
        for (size_t k = 0; k < ev.size(); ++k) if (ev[k].type == Ev::Text && seedStr.count(ev[k].str)) seeds.push_back(k);
        if (seeds.empty()) continue;
        ++pagesWithSeed;
        // one region per seed: the seed text plus the coloured block(s) that surround it
        std::vector<Box> regions; std::set<size_t> containers;
        const double pageArea = A.pages[p].pageArea;
        bool anyBlock = false;
        for (size_t sk : seeds) {
            const Ev& s = ev[sk];
            Box r; r.add(s.x, s.y - 0.25 * s.size); r.add(s.x + 0.55 * s.size * s.str.size(), s.y + 0.95 * s.size);
            for (auto& c : ev) {
                const bool fillBlock = (c.type == Ev::Paint && !c.stroke && !c.whiteFill && !c.clipped) || c.type == Ev::DoFillForm;
                if (fillBlock && c.box.area() < 0.3 * pageArea && c.box.has(s.x, s.y, 1.0)) { r.add(c.box); anyBlock = true; containers.insert((size_t)(&c - &ev[0])); }
            }
            regions.push_back(r);
        }
        if (anyBlock) ++onBlock;
        auto inside = [&](double x, double y) { for (auto& r : regions) if (r.has(x, y, 2.0)) return true; return false; };
        for (auto& r : regions) areaFrac = std::max(areaFrac, r.area() / pageArea);
        // contiguous run of events around the seeds that holds no pattern strokes and no other forms
        std::vector<bool> inSpan(ev.size(), false);
        for (size_t sk : seeds) {
            inSpan[sk] = true;
            for (size_t k = sk; k-- > 0;) { if ((ev[k].type == Ev::Paint && ev[k].stroke) || ev[k].type == Ev::DoOtherForm) break; inSpan[k] = true; }
            for (size_t k = sk + 1; k < ev.size(); ++k) { if ((ev[k].type == Ev::Paint && ev[k].stroke) || ev[k].type == Ev::DoOtherForm) break; inSpan[k] = true; }
        }
        for (size_t k = 0; k < ev.size(); ++k) {
            if (!inSpan[k] && !containers.count(k)) continue;
            Ev& e = ev[k];
            bool kill = false;
            if (e.type == Ev::Text) {
                if (seedStr.count(e.str)) { kill = true; ++seedsAll; if (steady(e, std::max(2, (int)std::ceil(0.8 * n)))) ++sameSpot; }
                else if (n >= 3 && inside(e.x, e.y) && steady(e, staticNeed)) { kill = true; ++statics; }
                if (kill) {   // the next text-show must not depend on this one's advance
                    const size_t nx = e.first + 1;
                    if (nx < A.ins[p].size()) { const std::string& o = A.ins[p][nx].op; if (o == "Tj" || o == "TJ" || o == "'" || o == "\"") kill = false; }
                }
            } else if (e.type == Ev::Paint) kill = !e.stroke && !e.clipped && inside(e.box.cx(), e.box.cy());
            else if (e.type == Ev::DoImg || e.type == Ev::DoFillForm) kill = inside(e.box.cx(), e.box.cy());
            if (!kill) continue;
            for (size_t t = e.first; t <= e.last; ++t) {
                const std::string& o = A.ins[p][t].op;
                if (e.type == Ev::Paint && !(o == "m" || o == "l" || o == "c" || o == "v" || o == "y" || o == "re" || o == "h" || o == "f" || o == "F" || o == "f*")) continue;
                A.bKill[p].push_back(t);
            }
            if (e.type == Ev::DoImg || e.type == Ev::DoFillForm) A.bNames[p].insert(e.name);
            ++items;
        }
        if (!A.bKill[p].empty()) ++pagesHit;
    }
    if (pagesHit == 0) return;
    Finding f; f.kind = Kind::LayerBlock; f.contactText = true; f.pages = pagesHit; f.objects = items;
    int score = 0;
    const int seedPg = (int)std::max_element(seedPages.begin(), seedPages.end(), [](auto& a, auto& b) { return a.second.size() < b.second.size(); })->second.size();
    if (seedPg >= std::ceil(0.8 * n)) { score += 40; f.evidence.push_back("联系方式类文字在 " + std::to_string(seedPg) + "/" + std::to_string(n) + " 页重复出现 +40"); }
    else { score += 25; f.evidence.push_back("联系方式类文字在 " + std::to_string(seedPg) + "/" + std::to_string(n) + " 页重复出现 +25"); }
    if (sameSpot >= 0.8 * seedsAll && seedsAll > 0) { score += 15; f.evidence.push_back("文字每页都在同一位置 +15"); }
    if (onBlock >= 0.8 * pagesWithSeed) { score += 15; f.evidence.push_back("文字压在一块有底色的矩形上（信息栏） +15"); }
    if (areaFrac <= 0.1) { score += 10; f.evidence.push_back("占页面面积不超过 10% +10"); }
    if (statics > 0) { score += 10; f.evidence.push_back("同一块内还有每页相同的品牌字/图标（页码等逐页变化的文字不动） +10"); }
    f.score = score; f.tier = tierFor(score);
    f.label = "店铺信息栏（与纸样同层，含联系方式文字）";
    A.rep.findings.push_back(f);
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
    try { pdf.processFile(toU8(in).c_str()); return true; }
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
            A.pageObjs.push_back(p);
            A.ins.push_back(parsePage(p));
            A.pages.push_back(tracePage(p, A.ins.back(), cache));
        }
        detectArtifacts(A);
        detectFlat(A);
        detectBlocks(A);
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
    std::set<std::string> dropForms; const Finding* flat = nullptr; bool dropBlock = false;
    for (int i = 0; i < (int)A.rep.findings.size(); ++i) {
        auto& f = A.rep.findings[i];
        if (f.tier == Tier::Unsupported) continue;
        if (f.tier != Tier::High && !pick.count(i)) continue;
        if (f.kind == Kind::ArtifactObject) dropForms.insert(f.formKey);
        if (f.kind == Kind::FlatText) flat = &f;
        if (f.kind == Kind::LayerBlock) dropBlock = true;
    }
    if (dropForms.empty() && !flat && !dropBlock) { R.error = "没有可去除的水印"; return R; }
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
            // --- same-layer shop banner (B): drop the planned instructions
            if (dropBlock && pi < A.bKill.size()) {
                for (size_t t : A.bKill[pi]) {
                    kill[t] = true;
                    if (ins[t].op == "Do" && !ins[t].a.empty() && ins[t].a[0].isName()) touchedNames.insert("Do" + ins[t].a[0].getName());
                    ++R.blockItemsRemoved;
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
        QPDFWriter w(pdf, toU8(out).c_str());
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
        const char* k[] = {"artifact", "flat_text", "page_stamp", "bitmap", "layer_block"};
        o << (i ? "," : "") << "{\"index\":" << i << ",\"kind\":\"" << k[(int)f.kind] << "\",\"tier\":\"" << t[(int)f.tier] << "\",\"score\":" << f.score
          << ",\"label\":\"" << esc(f.label) << "\",\"pages\":" << f.pages << ",\"objects\":" << f.objects
          << ",\"contactText\":" << (f.contactText ? "true" : "false") << ",\"evidence\":[";
        for (size_t j = 0; j < f.evidence.size(); ++j) o << (j ? "," : "") << "\"" << esc(f.evidence[j]) << "\"";
        o << "]}";
    }
    o << "]}";
    return o.str();
}


// ============================================================================ user-specified removal

namespace {
// ---- helpers shared by removeShape / removeText ----
struct PageGeom { double llx = 0, lly = 0, w = 0, h = 0; };
PageGeom geomOf(QPDFPageObjectHelper& pg) {
    auto mb = pg.getMediaBox().getArrayAsRectangle();
    PageGeom g; g.llx = std::min(mb.llx, mb.urx); g.lly = std::min(mb.lly, mb.ury);
    g.w = std::fabs(mb.urx - mb.llx); g.h = std::fabs(mb.ury - mb.lly);
    return g;
}
using Pts = std::vector<std::pair<double, double>>;
Pts rotated(const Pts& p, int r) {   // r quarter turns, about the origin
    Pts o; o.reserve(p.size());
    for (auto& q : p) {
        double x = q.first, y = q.second;
        for (int k = 0; k < r; ++k) { const double nx = -y, ny = x; x = nx; y = ny; }
        o.emplace_back(x, y);
    }
    return o;
}
bool samePts(const Pts& a, const Pts& b, double tol) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::fabs(a[i].first - b[i].first) > tol || std::fabs(a[i].second - b[i].second) > tol) return false;
    return true;
}
Pts translated(const Pts& p, double dx, double dy) { Pts o = p; for (auto& q : o) { q.first += dx; q.second += dy; } return o; }
Pts reversed(const Pts& p) { return Pts(p.rbegin(), p.rend()); }

void writePages(QPDF& pdf, std::vector<QPDFPageObjectHelper>& pages, std::vector<std::vector<Instr>>& ins, std::vector<std::vector<bool>>& kill,
                const std::filesystem::path& out) {
    for (size_t pi = 0; pi < pages.size(); ++pi) {
        bool any = false; for (bool b : kill[pi]) if (b) { any = true; break; }
        if (!any) continue;
        std::vector<Instr> o;
        for (size_t k = 0; k < ins[pi].size(); ++k) if (!kill[pi][k]) o.push_back(ins[pi][k]);
        pages[pi].getObjectHandle().replaceKey("/Contents", QPDFObjectHandle::newStream(&pdf, serialize(o)));
    }
    QPDFWriter w(pdf, toU8(out).c_str());
    w.setCompressStreams(true);
    w.setDecodeLevel(qpdf_dl_none);
    w.write();
}
}  // namespace

ShapeResult removeShape(const std::filesystem::path& in, const std::filesystem::path& out, int page, const Rect& sel, bool everywhere) {
    ShapeResult R;
    QPDF pdf;
    std::string err;
    if (!load(pdf, in, err)) { R.error = err; return R; }
    try {
        QPDFPageDocumentHelper doc(pdf);
        doc.pushInheritedAttributesToPage();
        auto pages = doc.getAllPages();
        const int n = (int)pages.size();
        if (page < 1 || page > n) { R.error = "页码超出范围"; return R; }
        std::vector<std::vector<Instr>> ins; std::vector<std::vector<Ev>> E; std::vector<std::vector<bool>> kill;
        std::map<std::string, bool> ffc;
        for (auto& p : pages) {
            ins.push_back(parsePage(p));
            auto ev = traceEvents(p, ins.back(), ffc);
            std::vector<Ev> strokes;
            for (auto& e : ev) if (e.type == Ev::Paint && e.stroke && !e.clipped && e.pts.size() >= 2) strokes.push_back(e);
            E.push_back(std::move(strokes));
            kill.emplace_back(ins.back().size(), false);
        }
        // selection rectangle -> PDF space of that page
        PageGeom g = geomOf(pages[page - 1]);
        Box sb;
        sb.add(g.llx + sel.x0, g.lly + g.h - sel.y0); sb.add(g.llx + sel.x1, g.lly + g.h - sel.y1);
        std::vector<int> S;   // indices of strokes in the selection
        for (int k = 0; k < (int)E[page - 1].size(); ++k) {
            const Box& b = E[page - 1][k].box;
            if (b.x0 >= sb.x0 - 0.3 && b.x1 <= sb.x1 + 0.3 && b.y0 >= sb.y0 - 0.3 && b.y1 <= sb.y1 + 0.3) S.push_back(k);
        }
        if (S.empty()) { R.error = "选框内没有完整的线条（请把要去除的字符完整框住）"; return R; }
        R.selected = (int)S.size();
        auto markStroke = [&](int pi, int k) {
            const Ev& e = E[pi][k];
            for (size_t t = e.first; t <= e.last; ++t) {
                const std::string& o = ins[pi][t].op;
                if (o == "m" || o == "l" || o == "c" || o == "v" || o == "y" || o == "re" || o == "h" || o == "S" || o == "s") kill[pi][t] = true;
            }
        };
        std::set<std::pair<int, int>> done;   // (page, stroke) already removed
        auto take = [&](int pi, int k) { if (done.insert({pi, k}).second) { markStroke(pi, k); ++R.removed; } };
        for (int k : S) take(page - 1, k);
        if (everywhere) {
            const double tol = 0.35;   // the source coordinates sit on a 0.16 pt grid, so two copies of one glyph differ by up to ~0.12 pt
            std::set<int> touched; touched.insert(page - 1);
            // anchors: the most detailed strokes of the selection (a copy cut by the page edge may lack some of them)
            std::vector<int> anchors = S;
            std::stable_sort(anchors.begin(), anchors.end(), [&](int x, int y) { return E[page - 1][x].pts.size() > E[page - 1][y].pts.size(); });
            std::vector<int> useAnchors;
            for (int k : anchors) if (E[page - 1][k].pts.size() >= 3 && useAnchors.size() < 6) useAnchors.push_back(k);
            if (useAnchors.empty()) useAnchors.push_back(anchors[0]);
            struct Pair { int k, d; bool rev; };
            for (int pi = 0; pi < n; ++pi) {
                PageGeom pg = geomOf(pages[pi]);
                Box pageBox; pageBox.add(pg.llx + 2, pg.lly + 2); pageBox.add(pg.llx + pg.w - 2, pg.lly + pg.h - 2);
                for (int ak : useAnchors) {
                    const Pts& ap = E[page - 1][ak].pts;
                    // similarity transform from selection space to this page: mirror (optional), quarter turns, uniform scale, shift.
                    // Labels of different pieces are printed at different sizes, and mirrored pieces carry mirrored labels.
                    auto base = [&](const Pts& src, bool mir, int rot) {
                        Pts o; o.reserve(src.size());
                        for (auto& q : src) o.emplace_back((mir ? -1.0 : 1.0) * (q.first - ap[0].first), q.second - ap[0].second);
                        return rotated(o, rot);
                    };
                    auto place = [&](const Pts& b, double sc, double tx, double ty) { Pts o = b; for (auto& q : o) { q.first = q.first * sc + tx; q.second = q.second * sc + ty; } return o; };
                    // match every selection stroke under the transform.  A stroke that is absent may be excused when its place lies on or
                    // beyond the page edge (the print split the drawing there).
                    auto matchAll = [&](bool mir, int rot, double sc, double tx, double ty, double tl, std::vector<Pair>& out) {
                        out.clear(); int miss = 0, excused = 0;
                        for (int k : S) {
                            Pts sp = place(base(E[page - 1][k].pts, mir, rot), sc, tx, ty);
                            int found = -1; bool rev = false;
                            for (int d = 0; d < (int)E[pi].size() && found < 0; ++d) {
                                if (E[pi][d].pts.size() != sp.size() || done.count({pi, d})) continue;
                                if (samePts(sp, E[pi][d].pts, tl)) found = d;
                                else if (samePts(sp, reversed(E[pi][d].pts), tl)) { found = d; rev = true; }
                            }
                            if (found >= 0) { out.push_back({k, found, rev}); continue; }
                            Box sb2; for (auto& q : sp) sb2.add(q.first, q.second);
                            const bool inside = sb2.x0 >= pageBox.x0 && sb2.x1 <= pageBox.x1 && sb2.y0 >= pageBox.y0 && sb2.y1 <= pageBox.y1;
                            if (!inside) ++excused; else if (++miss > (int)S.size() / 10) return false;
                        }
                        if (out.empty()) return false;
                        if (excused > 0 && (int)out.size() < std::max(3, (int)std::ceil(0.25 * S.size()))) return false;
                        return true;
                    };
                    for (int c = 0; c < (int)E[pi].size(); ++c) {
                        if (E[pi][c].pts.size() != ap.size() || done.count({pi, c})) continue;
                        bool hit = false;
                        for (int mir = 0; mir < 2 && !hit; ++mir)
                        for (int rot = 0; rot < 4 && !hit; ++rot)
                        for (int dir = 0; dir < 2 && !hit; ++dir) {
                            Pts ab = base(ap, mir, rot);
                            const Pts& cp = E[pi][c].pts;
                            Pts cc = dir ? reversed(cp) : cp;
                            double num = 0, den = 0;
                            for (size_t q = 1; q < ab.size(); ++q) {
                                const double ax = ab[q].first - ab[0].first, ay = ab[q].second - ab[0].second;
                                const double cx = cc[q].first - cc[0].first, cy = cc[q].second - cc[0].second;
                                num += ax * cx + ay * cy; den += ax * ax + ay * ay;
                            }
                            if (den < 1e-9) continue;
                            double sc = num / den;
                            if (sc < 0.3 || sc > 3.0) continue;
                            double tx = cc[0].first - ab[0].first * sc, ty = cc[0].second - ab[0].second * sc;
                            if (!samePts(place(ab, sc, tx, ty), cc, tol)) continue;
                            std::vector<Pair> m;
                            if (!matchAll(mir, rot, sc, tx, ty, tol * 1.5, m)) continue;
                            // refit scale and shift on every matched stroke (the anchor alone is a short baseline), then match again tighter
                            double sx = 0, sy = 0, qx = 0, qy = 0, nn = 0, cross = 0, sq = 0;
                            std::vector<std::pair<Pts, Pts>> prs;
                            for (auto& pr : m) {
                                Pts b = base(E[page - 1][pr.k].pts, mir, rot);
                                Pts t = pr.rev ? reversed(E[pi][pr.d].pts) : E[pi][pr.d].pts;
                                prs.push_back({b, t});
                                for (size_t q = 0; q < b.size(); ++q) { sx += b[q].first; sy += b[q].second; qx += t[q].first; qy += t[q].second; ++nn; }
                            }
                            const double mbx = sx / nn, mby = sy / nn, mqx = qx / nn, mqy = qy / nn;
                            for (auto& pr : prs) for (size_t q = 0; q < pr.first.size(); ++q) {
                                const double bx = pr.first[q].first - mbx, by = pr.first[q].second - mby;
                                cross += bx * (pr.second[q].first - mqx) + by * (pr.second[q].second - mqy); sq += bx * bx + by * by;
                            }
                            if (sq > 1e-9) { sc = cross / sq; tx = mqx - sc * mbx; ty = mqy - sc * mby; }
                            std::vector<Pair> m2;
                            if (!matchAll(mir, rot, sc, tx, ty, tol, m2)) m2 = m;
                            int before = R.removed;
                            for (auto& pr : m2) take(pi, pr.d);
                            if (R.removed > before) { ++R.copies; touched.insert(pi); hit = true; }
                        }
                    }
                }
            }
            for (int pi : touched) R.pagesTouched.push_back(pi + 1);
        } else R.pagesTouched.push_back(page);
        writePages(pdf, pages, ins, kill, out);
        R.ok = true;
    } catch (std::exception& e) { R.error = e.what(); }
    return R;
}

// ---- real text ----
namespace {
struct FontMap { int width = 1; bool has = false; std::map<unsigned, std::u16string> m; };
std::string u16ToU8(const std::u16string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size()) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); ++i; }
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    return o;
}
std::vector<std::string> hexTokens(const std::string& s) {   // every <...> group, as raw hex digits
    std::vector<std::string> t;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == '<' && (i + 1 >= s.size() || s[i + 1] != '<')) {
        size_t j = s.find('>', i);
        if (j == std::string::npos) break;
        std::string h; for (size_t k = i + 1; k < j; ++k) if (std::isxdigit((unsigned char)s[k])) h += s[k];
        t.push_back(h); i = j;
    }
    return t;
}
unsigned hexVal(const std::string& h) { return (unsigned)std::strtoul(h.c_str(), nullptr, 16); }
std::u16string hexU16(const std::string& h) { std::u16string u; for (size_t i = 0; i + 3 < h.size() + 0; i += 4) u += (char16_t)hexVal(h.substr(i, 4)); if (h.size() == 2) u += (char16_t)hexVal(h); return u; }
FontMap loadFont(QPDFObjectHandle f) {
    FontMap fm;
    if (!f.isDictionary()) return fm;
    auto st = f.getKey("/Subtype");
    if (st.isName() && st.getName() == "/Type0") fm.width = 2;
    auto tu = f.getKey("/ToUnicode");
    if (!tu.isStream()) return fm;
    try {
        auto buf = tu.getStreamData(qpdf_dl_generalized);
        std::string d((const char*)buf->getBuffer(), buf->getSize());
        size_t cs = d.find("begincodespacerange");
        if (cs != std::string::npos) { auto t = hexTokens(d.substr(cs, d.find("endcodespacerange", cs) - cs)); if (!t.empty()) fm.width = std::max<int>(1, (int)t[0].size() / 2); }
        size_t pos = 0;
        while ((pos = d.find("beginbfchar", pos)) != std::string::npos) {
            size_t e = d.find("endbfchar", pos); auto t = hexTokens(d.substr(pos, e - pos));
            for (size_t i = 0; i + 1 < t.size(); i += 2) fm.m[hexVal(t[i])] = hexU16(t[i + 1]);
            pos = e;
        }
        pos = 0;
        while ((pos = d.find("beginbfrange", pos)) != std::string::npos) {
            size_t e = d.find("endbfrange", pos); std::string blk = d.substr(pos, e - pos);
            // <lo> <hi> <dst>  or  <lo> <hi> [<d1> <d2> ...]
            size_t q = 0;
            while (q < blk.size()) {
                size_t a = blk.find('<', q); if (a == std::string::npos) break;
                size_t a2 = blk.find('>', a), b = blk.find('<', a2), b2 = blk.find('>', b);
                if (b == std::string::npos) break;
                size_t c = blk.find_first_not_of(" \r\n\t", b2 + 1);
                std::string lo, hi; for (size_t k = a + 1; k < a2; ++k) lo += blk[k]; for (size_t k = b + 1; k < b2; ++k) hi += blk[k];
                if (c != std::string::npos && blk[c] == '[') {
                    size_t endArr = blk.find(']', c); auto t = hexTokens(blk.substr(c, endArr - c));
                    for (size_t i = 0; i < t.size(); ++i) fm.m[hexVal(lo) + (unsigned)i] = hexU16(t[i]);
                    q = endArr + 1;
                } else if (c != std::string::npos && blk[c] == '<') {
                    size_t c2 = blk.find('>', c); std::string dst; for (size_t k = c + 1; k < c2; ++k) dst += blk[k];
                    std::u16string base = hexU16(dst);
                    for (unsigned code = hexVal(lo); code <= hexVal(hi) && code - hexVal(lo) < 65536; ++code) { std::u16string u = base; if (!u.empty()) u.back() = (char16_t)(u.back() + (code - hexVal(lo))); fm.m[code] = u; }
                    q = c2 + 1;
                } else break;
            }
            pos = e;
        }
        fm.has = !fm.m.empty();
    } catch (...) {}
    return fm;
}
std::string decodeShown(const std::string& raw, const FontMap& fm) {
    std::u16string u;
    const int w = fm.has ? fm.width : 1;
    for (size_t i = 0; i + w <= raw.size(); i += w) {
        unsigned code = 0; for (int k = 0; k < w; ++k) code = (code << 8) | (unsigned char)raw[i + k];
        auto it = fm.m.find(code);
        if (it != fm.m.end()) u += it->second;
        else if (!fm.has && code < 128) u += (char16_t)code;
        else if (!fm.has) u += (char16_t)code;   // Latin-1 fallback
    }
    return u16ToU8(u);
}
struct ShownText { size_t idx; std::string text; };
std::vector<ShownText> shownTexts(QPDFPageObjectHelper& page, std::vector<Instr>& ins) {
    std::vector<ShownText> out;
    QPDFObjectHandle res = page.getObjectHandle().getKey("/Resources");
    QPDFObjectHandle fonts = res.isDictionary() ? res.getKey("/Font") : QPDFObjectHandle::newNull();
    std::map<std::string, FontMap> cache; const FontMap* cur = nullptr; FontMap none;
    std::vector<const FontMap*> st;
    for (size_t k = 0; k < ins.size(); ++k) {
        Instr& i = ins[k];
        if (i.op == "Tf" && !i.a.empty() && i.a[0].isName()) {
            const std::string nm = i.a[0].getName();
            auto it = cache.find(nm);
            if (it == cache.end()) it = cache.emplace(nm, loadFont(fonts.isDictionary() ? fonts.getKey(nm) : QPDFObjectHandle::newNull())).first;
            cur = &it->second;
        } else if (i.op == "Tj" || i.op == "TJ" || i.op == "'" || i.op == "\"") {
            std::string raw;
            for (auto& o : i.a) {
                if (o.isString()) raw += o.getStringValue();
                else if (o.isArray()) for (int q = 0; q < o.getArrayNItems(); ++q) { auto e = o.getArrayItem(q); if (e.isString()) raw += e.getStringValue(); }
            }
            if (!raw.empty()) out.push_back({k, decodeShown(raw, cur ? *cur : none)});
        }
    }
    (void)st;
    return out;
}
}  // namespace

std::vector<TextItem> listText(const std::filesystem::path& in, std::string* error) {
    std::vector<TextItem> items;
    QPDF pdf; std::string err;
    if (!load(pdf, in, err)) { if (error) *error = err; return items; }
    try {
        QPDFPageDocumentHelper doc(pdf);
        doc.pushInheritedAttributesToPage();
        std::map<std::string, TextItem> m;
        int pi = 0;
        for (auto& p : doc.getAllPages()) {
            auto ins = parsePage(p);
            std::set<std::string> seenHere;
            for (auto& t : shownTexts(p, ins)) {
                if (t.text.empty()) continue;
                auto& it = m[t.text]; it.text = t.text; ++it.count;
                if (seenHere.insert(t.text).second) ++it.pages;
            }
            ++pi;
        }
        for (auto& [k, v] : m) items.push_back(v);
        std::sort(items.begin(), items.end(), [](const TextItem& a, const TextItem& b) { return a.pages != b.pages ? a.pages > b.pages : a.count > b.count; });
    } catch (std::exception& e) { if (error) *error = e.what(); }
    return items;
}

TextResult removeText(const std::filesystem::path& in, const std::filesystem::path& out, const std::vector<std::string>& needles) {
    TextResult R;
    QPDF pdf; std::string err;
    if (!load(pdf, in, err)) { R.error = err; return R; }
    if (needles.empty()) { R.error = "没有指定要去除的文字"; return R; }
    try {
        QPDFPageDocumentHelper doc(pdf);
        doc.pushInheritedAttributesToPage();
        auto pages = doc.getAllPages();
        std::vector<std::vector<Instr>> ins; std::vector<std::vector<bool>> kill;
        for (size_t pi = 0; pi < pages.size(); ++pi) {
            ins.push_back(parsePage(pages[pi]));
            kill.emplace_back(ins.back().size(), false);
            int hit = 0;
            for (auto& t : shownTexts(pages[pi], ins.back())) {
                bool m = false;
                for (auto& nd : needles) if (!nd.empty() && t.text.find(nd) != std::string::npos) m = true;
                if (m) { kill[pi][t.idx] = true; ++hit; ++R.removed; }
            }
            if (hit) R.pagesTouched.push_back((int)pi + 1);
        }
        if (R.removed == 0) { R.error = "没有找到包含指定内容的文字（线条画出来的字不是文字，请改用框选）"; return R; }
        writePages(pdf, pages, ins, kill, out);
        R.ok = true;
    } catch (std::exception& e) { R.error = e.what(); }
    return R;
}

}  // namespace wm
