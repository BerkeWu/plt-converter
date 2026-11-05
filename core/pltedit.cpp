#include "pltedit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace plt {

namespace {

Stroke rotated(const Stroke& p, int r) {
    Stroke o; o.reserve(p.size());
    for (auto q : p) {
        for (int k = 0; k < r; ++k) { const double nx = -q.y, ny = q.x; q.x = nx; q.y = ny; }
        o.push_back(q);
    }
    return o;
}
Stroke reversedS(const Stroke& p) { return Stroke(p.rbegin(), p.rend()); }
bool samePts(const Stroke& a, const Stroke& b, double tol) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::fabs(a[i].x - b[i].x) > tol || std::fabs(a[i].y - b[i].y) > tol) return false;
    return true;
}

// Candidate strokes with a grid index on both end points.
struct Index {
    const std::vector<Stroke>* C = nullptr;
    double cell = 1.0;
    std::map<std::pair<long, long>, std::vector<int>> grid;
    void build(const std::vector<Stroke>& c) {
        C = &c;
        for (int i = 0; i < (int)c.size(); ++i) {
            if (c[i].empty()) continue;
            grid[key(c[i].front())].push_back(i);
            grid[key(c[i].back())].push_back(i);
        }
    }
    std::pair<long, long> key(const Pt& p) const { return {(long)std::floor(p.x / cell), (long)std::floor(p.y / cell)}; }
    template <class F> void near(const Pt& p, F f) const {
        auto k = key(p);
        for (long dx = -1; dx <= 1; ++dx) for (long dy = -1; dy <= 1; ++dy) {
            auto it = grid.find({k.first + dx, k.second + dy});
            if (it != grid.end()) for (int i : it->second) f(i);
        }
    }
};

struct Pair { int k, d; bool rev; };

// Copies of template T among candidates C (strokes already claimed in `done` are skipped).  Appends the claimed candidate indices to `found`.
int matchCopies(const std::vector<Stroke>& T, const std::vector<Stroke>& C, std::vector<char>& done, double tol, std::vector<int>& found) {
    Index ix; ix.build(C);
    int copies = 0;
    std::vector<int> order(T.size());
    for (size_t i = 0; i < T.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return T[x].size() > T[y].size(); });
    std::vector<int> anchors;
    for (int k : order) if (T[k].size() >= 3 && anchors.size() < 6) anchors.push_back(k);
    if (anchors.empty()) anchors.push_back(order[0]);
    for (int ak : anchors) {
        const Stroke& ap = T[ak];
        auto base = [&](const Stroke& src, bool mir, int rot) {
            Stroke o; o.reserve(src.size());
            for (auto& q : src) o.push_back({(mir ? -1.0 : 1.0) * (q.x - ap[0].x), q.y - ap[0].y});
            return rotated(o, rot);
        };
        auto place = [&](const Stroke& b, double sc, double tx, double ty) { Stroke o = b; for (auto& q : o) { q.x = q.x * sc + tx; q.y = q.y * sc + ty; } return o; };
        auto matchAll = [&](bool mir, int rot, double sc, double tx, double ty, double tl, std::vector<Pair>& out) {
            out.clear(); int miss = 0;
            std::set<int> used;
            for (int k = 0; k < (int)T.size(); ++k) {
                Stroke sp = place(base(T[k], mir, rot), sc, tx, ty);
                int fnd = -1; bool rev = false;
                auto tryC = [&](int d) {
                    if (fnd >= 0 || C[d].size() != sp.size() || done[d] || used.count(d)) return;
                    if (samePts(sp, C[d], tl)) { fnd = d; rev = false; }
                    else if (samePts(sp, reversedS(C[d]), tl)) { fnd = d; rev = true; }
                };
                ix.near(sp.front(), tryC);
                if (fnd < 0) ix.near(sp.back(), tryC);
                if (fnd >= 0) { out.push_back({k, fnd, rev}); used.insert(fnd); continue; }
                if (++miss > (int)T.size() / 10) return false;
            }
            return !out.empty();
        };
        // candidates for the anchor: strokes with the anchor's point count, found through the grid by brute force over all (cheap filter first)
        for (int c = 0; c < (int)C.size(); ++c) {
            if (C[c].size() != ap.size() || done[c]) continue;
            bool hit = false;
            for (int mir = 0; mir < 2 && !hit; ++mir)
            for (int rot = 0; rot < 4 && !hit; ++rot)
            for (int dir = 0; dir < 2 && !hit; ++dir) {
                Stroke ab = base(ap, mir, rot);
                const Stroke& cp = C[c];
                Stroke cc = dir ? reversedS(cp) : cp;
                double num = 0, den = 0;
                for (size_t q = 1; q < ab.size(); ++q) {
                    const double ax = ab[q].x - ab[0].x, ay = ab[q].y - ab[0].y;
                    const double cx = cc[q].x - cc[0].x, cy = cc[q].y - cc[0].y;
                    num += ax * cx + ay * cy; den += ax * ax + ay * ay;
                }
                if (den < 1e-9) continue;
                double sc = num / den;
                if (sc < 0.3 || sc > 3.0) continue;
                double tx = cc[0].x - ab[0].x * sc, ty = cc[0].y - ab[0].y * sc;
                if (!samePts(place(ab, sc, tx, ty), cc, tol)) continue;
                std::vector<Pair> m;
                if (!matchAll(mir, rot, sc, tx, ty, tol * 1.5, m)) continue;
                // refit scale and shift on every matched stroke, then match again tighter
                double sx = 0, sy = 0, qx = 0, qy = 0, nn = 0, cross = 0, sq = 0;
                std::vector<std::pair<Stroke, Stroke>> prs;
                for (auto& pr : m) {
                    Stroke b = base(T[pr.k], mir, rot);
                    Stroke t = pr.rev ? reversedS(C[pr.d]) : C[pr.d];
                    prs.push_back({b, t});
                    for (size_t q = 0; q < b.size(); ++q) { sx += b[q].x; sy += b[q].y; qx += t[q].x; qy += t[q].y; ++nn; }
                }
                const double mbx = sx / nn, mby = sy / nn, mqx = qx / nn, mqy = qy / nn;
                for (auto& pr : prs) for (size_t q = 0; q < pr.first.size(); ++q) {
                    const double bx = pr.first[q].x - mbx, by = pr.first[q].y - mby;
                    cross += bx * (pr.second[q].x - mqx) + by * (pr.second[q].y - mqy); sq += bx * bx + by * by;
                }
                if (sq > 1e-9) { sc = cross / sq; tx = mqx - sc * mbx; ty = mqy - sc * mby; }
                std::vector<Pair> m2;
                if (!matchAll(mir, rot, sc, tx, ty, tol, m2)) m2 = m;
                int before = (int)found.size();
                for (auto& pr : m2) if (!done[pr.d]) { done[pr.d] = 1; found.push_back(pr.d); }
                if ((int)found.size() > before) { ++copies; hit = true; }
            }
        }
    }
    return copies;
}

}  // namespace

std::vector<size_t> linesInBox(const Document& doc, const Bounds& box) {
    std::vector<size_t> r;
    const double e = 0.05;
    for (size_t i = 0; i < doc.lines.size(); ++i) {
        const auto& l = doc.lines[i];
        if (!l.editable || l.pts.size() < 2) continue;
        bool in = true;
        for (auto& p : l.pts) if (p.x < box.minX - e || p.x > box.maxX + e || p.y < box.minY - e || p.y > box.maxY + e) { in = false; break; }
        if (in) r.push_back(i);
    }
    return r;
}

bool extractTemplate(const Document& doc, const Bounds& box, std::vector<Stroke>& tpl, std::string* error) {
    tpl.clear();
    for (size_t i : linesInBox(doc, box)) tpl.push_back(doc.lines[i].pts);
    if (tpl.empty()) { if (error) *error = "选框内没有完整的线条（请把要去除的字符完整框住）"; return false; }
    return true;
}

ShapeEdit findTemplateCopies(const Document& doc, const std::vector<Stroke>& tpl, double tol) {
    ShapeEdit R;
    if (tpl.empty()) { R.error = "图形模板是空的"; return R; }
    std::vector<Stroke> C; std::vector<size_t> map;
    for (size_t i = 0; i < doc.lines.size(); ++i) if (doc.lines[i].editable && doc.lines[i].pts.size() >= 2) { C.push_back(doc.lines[i].pts); map.push_back(i); }
    std::vector<char> done(C.size(), 0);
    std::vector<int> found;
    R.selected = (int)tpl.size();
    R.copies = matchCopies(tpl, C, done, tol, found);
    for (int d : found) R.lines.push_back(map[d]);
    std::sort(R.lines.begin(), R.lines.end());
    if (R.lines.empty()) { R.error = "没有找到与图形模板相同的线条"; return R; }
    R.ok = true;
    return R;
}

ShapeEdit findShapeCopies(const Document& doc, const Bounds& box, bool everywhere, double tol) {
    ShapeEdit R;
    std::vector<size_t> sel = linesInBox(doc, box);
    if (sel.empty()) { R.error = "选框内没有完整的线条（请把要去除的字符完整框住）"; return R; }
    R.selected = (int)sel.size();
    if (!everywhere) { R.lines = sel; R.ok = true; return R; }
    std::vector<Stroke> T;
    for (size_t i : sel) T.push_back(doc.lines[i].pts);
    ShapeEdit all = findTemplateCopies(doc, T, tol);
    // the boxed instance itself must always be part of the result
    std::set<size_t> s(all.lines.begin(), all.lines.end());
    for (size_t i : sel) s.insert(i);
    R.lines.assign(s.begin(), s.end());
    R.copies = std::max(0, all.copies - 1);   // the boxed instance matches itself
    R.ok = true;
    return R;
}

std::string removeLines(const std::string& data, const Document& doc, const std::vector<size_t>& lines) {
    std::map<size_t, Span> spans;   // by start offset
    for (size_t li : lines) {
        if (li >= doc.lines.size() || !doc.lines[li].editable) continue;
        for (auto& s : doc.lines[li].src) spans[s.a] = s;
    }
    std::string out;
    out.reserve(data.size() + spans.size() * 4);
    size_t pos = 0;
    for (auto& [a, s] : spans) {
        if (a < pos || s.b > data.size() || s.b <= a) continue;
        out.append(data, pos, a - pos);
        std::string cmd = data.substr(a, s.b - a);
        if (cmd.empty() || cmd.back() != ';') cmd += ';';
        if (s.kind == 'X') { /* circle / rectangle: self-contained, drop it */ }
        else if (s.kind == 'D') { cmd[0] = 'P'; cmd[1] = 'U'; out += cmd; out += "PD;"; }        // PD x,y.. -> PU x,y..; PD;
        else { out += "PU;"; out += cmd; out += "PD;"; }                                           // PA / PR with the pen down
        pos = s.b;
    }
    out.append(data, pos, std::string::npos);
    return out;
}

}  // namespace plt
