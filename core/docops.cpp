#include "docops.h"

#include <algorithm>
#include <cmath>

#include "layout.h"

namespace docops {

void recompute(plt::Document& d) {
    d.bounds = plt::Bounds();
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

void transform(plt::Document& d, int rot, bool mirrorX) {
    rot = ((rot % 4) + 4) % 4;
    if (rot == 0 && !mirrorX) return;
    for (auto& pl : d.lines)
        for (auto& p : pl.pts) {
            double x = mirrorX ? -p.x : p.x, y = p.y;
            for (int i = 0; i < rot; ++i) { double nx = -y, ny = x; x = nx; y = ny; }
            p.x = x; p.y = y;
        }
    recompute(d);
}

plt::Document crop(const plt::Document& d, double x0, double y0, double x1, double y1, bool clip) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    plt::Document out = d;
    out.lines.clear();
    for (const auto& pl : d.lines) {
        if (!clip) {
            bool hit = false;
            for (size_t i = 1; i < pl.pts.size() && !hit; ++i) {
                double a = pl.pts[i - 1].x, b = pl.pts[i - 1].y, c = pl.pts[i].x, e = pl.pts[i].y;
                hit = layout::clipSegment(a, b, c, e, x0, y0, x1, y1);
            }
            if (hit) out.lines.push_back(pl);
            continue;
        }
        plt::Polyline cur;
        cur.pen = pl.pen;
        auto flush = [&] { if (cur.pts.size() >= 2) out.lines.push_back(cur); cur.pts.clear(); };
        for (size_t i = 1; i < pl.pts.size(); ++i) {
            double a = pl.pts[i - 1].x, b = pl.pts[i - 1].y, c = pl.pts[i].x, e = pl.pts[i].y;
            if (!layout::clipSegment(a, b, c, e, x0, y0, x1, y1)) { flush(); continue; }
            if (std::hypot(c - a, e - b) < 1e-6) continue;
            if (!cur.pts.empty() && (std::fabs(cur.pts.back().x - a) > 1e-7 || std::fabs(cur.pts.back().y - b) > 1e-7)) flush();
            if (cur.pts.empty()) cur.pts.push_back({a, b});
            cur.pts.push_back({c, e});
        }
        flush();
    }
    recompute(out);
    return out;
}

}  // namespace docops
