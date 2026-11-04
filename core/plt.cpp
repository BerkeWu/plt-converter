#include "plt.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace plt {

namespace {

const double kPi = 3.14159265358979323846;

// Mnemonics that carry no geometry we need; silently ignored.
const std::set<std::string> kIgnored = {"IN", "PS", "NP", "PC", "PW", "LT", "PG", "SP", "DF", "IW", "RO", "VS", "FS",
                                        "LA", "CT", "TL", "XT", "YT", "SI", "SR", "DI", "DR", "SL", "CS", "CC", "SA",
                                        "SS", "AP", "BP", "CP", "DC", "EC", "DP", "OP", "OI", "OS", "OE", "NR", "OH",
                                        "OW", "ES", "PB", "TD", "WU", "RF", "AF", "FT", "GC", "SM", "UC",
                                        "LO", "IP", "SC", "PE", "PT", "SV"};

struct State {
    bool penDown = false;
    bool relative = false;
    int pen = 1;
    Pt cur;
    Polyline open;
    bool hasOpen = false;
    std::vector<Polyline> out;

    void closeLine() {
        if (hasOpen && open.pts.size() >= 2) out.push_back(open);
        open.pts.clear();
        hasOpen = false;
    }
    void moveTo(Pt p) {
        if (penDown) {
            if (!hasOpen) {
                open.pen = pen;
                open.pts.clear();
                open.pts.push_back(cur);
                hasOpen = true;
            }
            const Pt& last = open.pts.back();
            if (last.x != p.x || last.y != p.y) open.pts.push_back(p);  // drop zero-length segments
        } else {
            closeLine();
        }
        cur = p;
    }
};

std::vector<double> parseNumbers(const std::string& s) {
    std::vector<double> v;
    const char* p = s.c_str();
    char* end = nullptr;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') ++p;
        if (!*p) break;
        double d = std::strtod(p, &end);
        if (end == p) { ++p; continue; }  // skip garbage char
        v.push_back(d);
        p = end;
    }
    return v;
}

}  // namespace

Document parseString(const std::string& data) {
    Document doc;
    State st;
    const size_t n = data.size();
    size_t i = 0;
    bool anyCmd = false;

    auto drawCircle = [&](double r) {
        Polyline c;
        c.pen = st.pen;
        const int steps = 72;
        for (int k = 0; k <= steps; ++k) {
            double a = 2 * kPi * k / steps;
            c.pts.push_back({st.cur.x + r * std::cos(a), st.cur.y + r * std::sin(a)});
        }
        st.closeLine();
        st.out.push_back(c);
    };

    // Arc around centre c from the current pen position; sweep in degrees (counter-clockwise positive).
    auto drawArc = [&](Pt c, double sweep, double chord) {
        const double r = std::hypot(st.cur.x - c.x, st.cur.y - c.y);
        if (r < 1e-9 || sweep == 0) return;
        const double a0 = std::atan2(st.cur.y - c.y, st.cur.x - c.x);
        const double stepDeg = chord > 0 ? std::min(chord, 2.0) : 2.0;  // never coarser than 2 degrees: smooth when printed 1:1
        const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / stepDeg)));
        for (int k = 1; k <= steps; ++k) {
            const double a = a0 + sweep * k / steps * kPi / 180.0;
            st.moveTo({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
    };
    // Arc through three points (current position, p1, p2).
    auto drawArc3 = [&](Pt p1, Pt p2, double chord) {
        const Pt p0 = st.cur;
        const double d = 2 * (p0.x * (p1.y - p2.y) + p1.x * (p2.y - p0.y) + p2.x * (p0.y - p1.y));
        if (std::fabs(d) < 1e-9) { st.moveTo(p1); st.moveTo(p2); return; }  // collinear: straight lines
        const double s0 = p0.x * p0.x + p0.y * p0.y, s1 = p1.x * p1.x + p1.y * p1.y, s2 = p2.x * p2.x + p2.y * p2.y;
        const Pt c{(s0 * (p1.y - p2.y) + s1 * (p2.y - p0.y) + s2 * (p0.y - p1.y)) / d,
                   (s0 * (p2.x - p1.x) + s1 * (p0.x - p2.x) + s2 * (p1.x - p0.x)) / d};
        const double a0 = std::atan2(p0.y - c.y, p0.x - c.x), a2 = std::atan2(p2.y - c.y, p2.x - c.x);
        const bool ccw = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x) > 0;  // p1 lies on the arc from p0 to p2
        double sweep = a2 - a0;
        if (ccw) { while (sweep <= 0) sweep += 2 * kPi; } else { while (sweep >= 0) sweep -= 2 * kPi; }
        drawArc(c, sweep * 180.0 / kPi, chord);
    };
    double crMin[3] = {0, 0, 0}, crMax[3] = {255, 255, 255};  // HP-GL/2 default colour range
    auto norm = [&](double v, int k) { double d = crMax[k] - crMin[k]; return d == 0 ? 0.0 : std::min(1.0, std::max(0.0, (v - crMin[k]) / d)); };

    while (i < n) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch == ';' || ch <= ' ') { ++i; continue; }
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= 'a' && ch <= 'z')) { ++i; continue; }  // stray byte
        if (i + 1 >= n) break;
        std::string mn;
        mn += static_cast<char>(std::toupper(ch));
        mn += static_cast<char>(std::toupper(static_cast<unsigned char>(data[i + 1])));
        i += 2;
        anyCmd = true;
        doc.commandCount[mn]++;

        if (mn == "LB") {  // label text up to ETX (0x03); text is not rendered in this version
            while (i < n && data[i] != 0x03) ++i;
            if (i < n) ++i;
            doc.unsupported["LB"]++;
            continue;
        }
        size_t j = i;
        while (j < n && data[j] != ';') ++j;
        std::string args = data.substr(i, j - i);
        i = (j < n) ? j + 1 : j;
        std::vector<double> a = parseNumbers(args);

        if (mn == "IN") {
            st.closeLine(); st.penDown = false; st.relative = false; st.cur = {0, 0};
        } else if (mn == "SP") {
            st.closeLine();
            st.pen = a.empty() ? 1 : static_cast<int>(a[0]);
            if (st.pen == 0) st.pen = 1;
        } else if (mn == "PU" || mn == "PD" || mn == "PA" || mn == "PR") {
            if (mn == "PU") { st.closeLine(); st.penDown = false; }
            if (mn == "PD") st.penDown = true;
            if (mn == "PA") st.relative = false;
            if (mn == "PR") st.relative = true;
            for (size_t k = 0; k + 1 < a.size(); k += 2) {
                Pt p{a[k], a[k + 1]};
                if (st.relative) { p.x += st.cur.x; p.y += st.cur.y; }
                st.moveTo(p);
            }
        } else if (mn == "CI") {
            if (!a.empty()) drawCircle(a[0]);
        } else if (mn == "AA" || mn == "AR") {  // arc by centre + sweep angle
            if (a.size() >= 3) {
                Pt c{a[0], a[1]};
                if (mn == "AR") { c.x += st.cur.x; c.y += st.cur.y; }
                drawArc(c, a[2], a.size() > 3 ? a[3] : 0);
            }
        } else if (mn == "AT" || mn == "RT") {  // arc through three points (RT: both points relative to the start)
            if (a.size() >= 4) {
                Pt p1{a[0], a[1]}, p2{a[2], a[3]};
                if (mn == "RT") { p1.x += st.cur.x; p1.y += st.cur.y; p2.x += st.cur.x; p2.y += st.cur.y; }
                drawArc3(p1, p2, a.size() > 4 ? a[4] : 0);
            }
        } else if (mn == "EA" || mn == "ER") {  // rectangle outline from the current position; pen position unchanged
            if (a.size() >= 2) {
                const Pt s0 = st.cur;
                Pt e{a[0], a[1]};
                if (mn == "ER") { e.x += s0.x; e.y += s0.y; }
                Polyline r;
                r.pen = st.pen;
                r.pts = {s0, {e.x, s0.y}, e, {s0.x, e.y}, s0};
                st.closeLine();
                st.out.push_back(r);
            }
        } else if (mn == "CR") {  // colour range for PC
            if (a.size() >= 6) { for (int k = 0; k < 3; ++k) { crMin[k] = a[2 * k]; crMax[k] = a[2 * k + 1]; } }
            else { for (int k = 0; k < 3; ++k) { crMin[k] = 0; crMax[k] = 255; } }
        } else if (mn == "PC") {  // PC; = reset, PC n = default colour for pen n, PC n,r,g,b = define
            if (a.empty()) doc.penColors.clear();
            else if (a.size() == 1) doc.penColors.erase(static_cast<int>(a[0]));
            else if (a.size() >= 4) doc.penColors[static_cast<int>(a[0])] = {norm(a[1], 0), norm(a[2], 1), norm(a[3], 2)};
        } else if (kIgnored.count(mn) == 0) {
            doc.unsupported[mn]++;  // possibly geometry (AA, AR, EA, ...) we do not handle yet
        }
        if (mn == "IP" || mn == "SC") doc.warnings.push_back("IP/SC scaling command found; default 40 units/mm assumed");
    }
    st.closeLine();
    if (!anyCmd) throw std::runtime_error("not an HP-GL/PLT file (no commands found)");

    const double u = doc.unitsPerMm;
    for (auto& pl : st.out) {
        for (auto& p : pl.pts) { p.x /= u; p.y /= u; doc.bounds.add(p); }
        for (size_t k = 1; k < pl.pts.size(); ++k) {
            doc.totalLengthMm += std::hypot(pl.pts[k].x - pl.pts[k - 1].x, pl.pts[k].y - pl.pts[k - 1].y);
            ++doc.segmentCount;
        }
    }
    doc.lines = std::move(st.out);
    if (doc.lines.empty()) throw std::runtime_error("file contains no drawable lines");
    if (!doc.unsupported.empty()) doc.warnings.push_back("some commands were skipped (see unsupported)");
    return doc;
}

namespace {
void recompute(Document& d) {
    d.bounds = Bounds();
    d.segmentCount = 0;
    d.totalLengthMm = 0;
    for (const auto& pl : d.lines) {
        for (const auto& p : pl.pts) d.bounds.add(p);
        for (size_t k = 1; k < pl.pts.size(); ++k) {
            d.totalLengthMm += std::hypot(pl.pts[k].x - pl.pts[k - 1].x, pl.pts[k].y - pl.pts[k - 1].y);
            ++d.segmentCount;
        }
    }
}
}  // namespace

int findFrame(const Document& d) {
    const double tol = 0.05, bboxArea = d.bounds.w() * d.bounds.h();
    int best = -1;
    double bestArea = 0;
    for (size_t i = 0; i < d.lines.size(); ++i) {
        const auto& pts = d.lines[i].pts;
        if (pts.size() < 5 || pts.size() > 6) continue;
        if (std::fabs(pts.front().x - pts.back().x) > tol || std::fabs(pts.front().y - pts.back().y) > tol) continue;  // closed
        Bounds b;
        bool axis = true;
        for (size_t k = 0; k < pts.size(); ++k) {
            b.add(pts[k]);
            if (k > 0 && std::fabs(pts[k].x - pts[k - 1].x) > tol && std::fabs(pts[k].y - pts[k - 1].y) > tol) axis = false;
        }
        if (!axis) continue;
        double area = b.w() * b.h();
        if (area >= 0.7 * bboxArea && area > bestArea) { best = static_cast<int>(i); bestArea = area; }
    }
    return best;
}

int stripFrame(Document& d) {
    const int idx = findFrame(d);
    if (idx < 0 || d.lines.size() < 2) return 0;
    Bounds fb;
    for (const auto& p : d.lines[idx].pts) fb.add(p);
    const double tol = 0.5;  // mm: duplicates may differ slightly
    auto isDuplicate = [&](const Polyline& l) {
        if (l.pts.size() < 5 || l.pts.size() > 6) return false;
        if (std::fabs(l.pts.front().x - l.pts.back().x) > 0.05 || std::fabs(l.pts.front().y - l.pts.back().y) > 0.05) return false;
        Bounds b;
        for (size_t k = 0; k < l.pts.size(); ++k) {
            b.add(l.pts[k]);
            if (k > 0 && std::fabs(l.pts[k].x - l.pts[k - 1].x) > 0.05 && std::fabs(l.pts[k].y - l.pts[k - 1].y) > 0.05) return false;
        }
        return std::fabs(b.minX - fb.minX) < tol && std::fabs(b.maxX - fb.maxX) < tol && std::fabs(b.minY - fb.minY) < tol && std::fabs(b.maxY - fb.maxY) < tol;
    };
    std::vector<Polyline> kept;
    kept.reserve(d.lines.size());
    for (auto& l : d.lines) if (!isDuplicate(l)) kept.push_back(std::move(l));
    if (kept.empty()) return 0;  // nothing but frames: keep the file as it was
    const int removed = static_cast<int>(d.lines.size() - kept.size());
    d.lines = std::move(kept);
    recompute(d);
    return removed;
}

Document parseFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path.u8string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseString(ss.str());
}

}  // namespace plt
