#include "bitmap.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFObjGen.hh>

#include <algorithm>
#include <cmath>
#include <set>

namespace wm::bmp {

namespace {
bool nameIs(QPDFObjectHandle o, const char* n) { return o.isName() && o.getName() == n; }

int components(QPDFObjectHandle cs) {
    if (nameIs(cs, "/DeviceRGB") || nameIs(cs, "/CalRGB")) return 3;
    if (nameIs(cs, "/DeviceGray") || nameIs(cs, "/CalGray")) return 1;
    if (cs.isArray() && cs.getArrayNItems() >= 1) {
        auto k = cs.getArrayItem(0);
        if (nameIs(k, "/ICCBased") && cs.getArrayNItems() >= 2) {
            auto s = cs.getArrayItem(1);
            if (s.isStream()) { auto n = s.getDict().getKey("/N"); if (n.isInteger()) { int v = (int)n.getIntValue(); if (v == 1 || v == 3) return v; } }
        }
        if (nameIs(k, "/CalRGB")) return 3;
        if (nameIs(k, "/CalGray")) return 1;
    }
    return 0;
}

// chroma of a pixel: p - mean(p)
inline void chroma(const uint8_t* p, double& m, double q[3]) {
    m = (p[0] + p[1] + p[2]) / 3.0;
    q[0] = p[0] - m; q[1] = p[1] - m; q[2] = p[2] - m;
}
inline double norm3(const double q[3]) { return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]); }

double median(std::vector<double>& v) {
    if (v.empty()) return 0;
    auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

void collect(QPDFObjectHandle res, int depth, std::set<QPDFObjGen>& seen, std::vector<QPDFObjectHandle>& out, long minPixels) {
    if (!res.isDictionary() || depth > 3) return;
    auto xo = res.getKey("/XObject");
    if (!xo.isDictionary()) return;
    for (auto& k : xo.getKeys()) {
        auto x = xo.getKey(k);
        if (!x.isStream()) continue;
        if (!seen.insert(x.getObjGen()).second) continue;
        auto d = x.getDict();
        auto sub = d.getKey("/Subtype");
        if (nameIs(sub, "/Image")) {
            auto w = d.getKey("/Width"), h = d.getKey("/Height");
            if (w.isInteger() && h.isInteger() && (long)w.getIntValue() * (long)h.getIntValue() >= minPixels) out.push_back(x);
        } else if (nameIs(sub, "/Form")) collect(d.getKey("/Resources"), depth + 1, seen, out, minPixels);
    }
}
}  // namespace

Model Model::unpack(const std::vector<double>& v) {
    Model m;
    if (v.size() < 5) return m;
    m.u[0] = v[0]; m.u[1] = v[1]; m.u[2] = v[2]; m.K = v[3]; m.mW = v[4];
    m.ok = m.K > 1;
    return m;
}

bool decode(QPDFObjectHandle img, Image& out, std::string* why) {
    auto fail = [&](const char* s) { if (why) *why = s; return false; };
    if (!img.isStream()) return fail("不是图片对象");
    auto d = img.getDict();
    if (d.getKey("/ImageMask").isBool() && d.getKey("/ImageMask").getBoolValue()) return fail("蒙版图片");
    auto bpc = d.getKey("/BitsPerComponent");
    if (!bpc.isInteger() || bpc.getIntValue() != 8) return fail("不是 8 位图片");
    const int nc = components(d.getKey("/ColorSpace"));
    if (nc == 0) return fail("颜色空间不支持（只支持 RGB / 灰度）");
    if (d.getKey("/Decode").isArray()) return fail("带 Decode 数组的图片");
    auto W = d.getKey("/Width"), H = d.getKey("/Height");
    if (!W.isInteger() || !H.isInteger()) return fail("图片尺寸缺失");
    out.w = (int)W.getIntValue(); out.h = (int)H.getIntValue();
    auto f = d.getKey("/Filter");
    out.lossy = false;
    auto lossyName = [](QPDFObjectHandle n) { return nameIs(n, "/DCTDecode") || nameIs(n, "/JPXDecode"); };
    if (lossyName(f)) out.lossy = true;
    if (f.isArray()) for (int i = 0; i < f.getArrayNItems(); ++i) if (lossyName(f.getArrayItem(i))) out.lossy = true;
    std::shared_ptr<Buffer> buf;
    try { buf = img.getStreamData(qpdf_dl_all); } catch (std::exception&) { return fail("图片数据无法解码"); }
    const size_t need = (size_t)out.w * out.h * nc;
    if (!buf || buf->getSize() < need) return fail("图片数据不完整");
    const uint8_t* s = buf->getBuffer();
    out.rgb.resize((size_t)out.w * out.h * 3);
    if (nc == 3) std::copy(s, s + need, out.rgb.begin());
    else for (size_t i = 0, n = (size_t)out.w * out.h; i < n; ++i) out.rgb[3 * i] = out.rgb[3 * i + 1] = out.rgb[3 * i + 2] = s[i];
    return true;
}

Model fit(const std::vector<const Image*>& imgs, int stride) {
    Model M;
    // 1. the dominant chroma direction (hue) of all clearly coloured pixels
    double acc[3] = {0, 0, 0};
    long total = 0, coloured = 0, vivid = 0;
    for (auto* im : imgs) {
        const size_t n = (size_t)im->w * im->h;
        for (size_t i = 0; i < n; i += stride) {
            ++total;
            double m, q[3]; chroma(&im->rgb[3 * i], m, q);
            const double L = norm3(q);
            if (L < 10) continue;
            ++coloured;
            if (L > 70) ++vivid;     // strongly saturated: opaque coloured lines / text, not a see-through stamp
            // fold into one half-space so opposite hues do not cancel
            const double s = (acc[0] * q[0] + acc[1] * q[1] + acc[2] * q[2]) < 0 ? -1 : 1;
            for (int j = 0; j < 3; ++j) acc[j] += s * q[j] / L;
        }
    }
    M.colourShare = total ? double(coloured) / total : 0;
    if (coloured < 200 || M.colourShare < 0.0005) { M.why = "图片里没有彩色水印：水印和纸样一样是黑白灰，无法按颜色区分"; return M; }
    // A translucent watermark only ever tints the paper (and the lines under it); its pixels never get far from grey.
    // Pattern lines or labels printed in colour are saturated - removing "the colour" would destroy them.
    if (double(vivid) / coloured > 0.05) { M.why = "彩色部分是纸样本身的彩色线条/文字（颜色饱和、不透明），水印不是唯一的彩色内容，无法按颜色分离"; return M; }
    const double an = norm3(acc);
    for (int j = 0; j < 3; ++j) M.u[j] = acc[j] / an;
    // orient u so that the watermark pixels over white have a positive projection
    // 2. share of coloured pixels that really have this hue (within ~25 degrees); fit data over white paper and over black lines
    long same = 0, pos = 0, neg = 0;
    std::vector<double> light, dark;
    for (auto* im : imgs) {
        const size_t n = (size_t)im->w * im->h;
        for (size_t i = 0; i < n; i += stride) {
            double m, q[3]; chroma(&im->rgb[3 * i], m, q);
            const double L = norm3(q);
            if (L < 10) continue;
            const double pr = q[0] * M.u[0] + q[1] * M.u[1] + q[2] * M.u[2];
            if (std::fabs(pr) >= 0.9 * L) { ++same; (pr > 0 ? pos : neg)++; }
        }
    }
    M.hueShare = double(same) / coloured;
    if (neg > pos) for (double& x : M.u) x = -x;
    if (M.hueShare < 0.93) { M.why = "图片里有不止一种颜色（纸样本身含彩色内容），无法安全地只去掉水印"; return M; }
    for (auto* im : imgs) {
        const size_t n = (size_t)im->w * im->h;
        for (size_t i = 0; i < n; i += stride) {
            double m, q[3]; chroma(&im->rgb[3 * i], m, q);
            const double pr = q[0] * M.u[0] + q[1] * M.u[1] + q[2] * M.u[2];
            if (pr < 12 || norm3(q) > 1.15 * pr + 4) continue;
            if (m > 120 && light.size() < 400000) light.push_back((255 - m) / pr);        // watermark over white:  255 - mean = pr * (255 - mW) / K
            else if (m < 70 && dark.size() < 400000) dark.push_back(m / pr);              // watermark over black:  mean = pr * mW / K
        }
    }
    M.lightSamples = (int)light.size(); M.darkSamples = (int)dark.size();
    if (light.size() < 100) { M.why = "彩色像素太少，无法可靠地估计水印颜色"; return M; }
    const double s1 = median(light);
    const double s2 = dark.size() >= 50 ? median(dark) : 0.0;
    if (!(s1 + s2 > 0)) { M.why = "无法估计水印颜色"; return M; }
    M.K = 255.0 / (s1 + s2);
    M.mW = s2 * M.K;
    M.ok = M.K > 5;
    if (!M.ok) M.why = "水印颜色太淡，无法估计";
    return M;
}

CleanStats clean(Image& img, const Model& M) {
    CleanStats S;
    const int w = img.w, h = img.h;
    std::vector<uint8_t> mask((size_t)w * h, 0);
    for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
        uint8_t* p = &img.rgb[3 * i];
        double m, q[3]; chroma(p, m, q);
        const double L = norm3(q);
        const int span = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
        if (span <= 4 || L <= 3) { if (m < 200) ++S.protectedDark; continue; }   // achromatic: the pattern itself, never touched
        const double pr = q[0] * M.u[0] + q[1] * M.u[1] + q[2] * M.u[2];
        if (pr <= 2 || L > 75) continue;                              // not our hue / a saturated (opaque) colour: not the watermark
        if (L > 1.5 * pr + 6) continue;                                 // a different colour: not ours
        const double a = std::min(0.95, pr / M.K);
        double c = (m - a * M.mW) / (1 - a);
        if (c >= 200) c = 255;
        const uint8_t v = (uint8_t)std::clamp(std::lround(c), 0L, 255L);
        if (p[0] != v || p[1] != v || p[2] != v) { p[0] = p[1] = p[2] = v; ++S.changed; }
        mask[i] = 1;
    }
    // soft fringe of the glyphs (anti-aliasing, resampling) next to removed pixels: back to paper white
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &img.rgb[3 * ((size_t)y * w + x)];
            const int mn = std::min({p[0], p[1], p[2]});
            if (mn >= 244) { if (p[0] != 255 || p[1] != 255 || p[2] != 255) { p[0] = p[1] = p[2] = 255; } continue; }   // paper white (also the faint opposite-hue fringe)
            if (mn < 215 || mask[(size_t)y * w + x]) continue;
            bool near = false;
            for (int dy = -2; dy <= 2 && !near; ++dy) {
                const int yy = y + dy; if (yy < 0 || yy >= h) continue;
                for (int dx = -2; dx <= 2; ++dx) { const int xx = x + dx; if (xx >= 0 && xx < w && mask[(size_t)yy * w + xx]) { near = true; break; } }
            }
            if (near) { p[0] = p[1] = p[2] = 255; ++S.changed; }
        }
    return S;
}

std::vector<QPDFObjectHandle> pageImages(QPDFObjectHandle page, long minPixels) {
    std::vector<QPDFObjectHandle> out;
    std::set<QPDFObjGen> seen;
    collect(page.getKey("/Resources"), 0, seen, out, minPixels);
    return out;
}

}  // namespace wm::bmp
