#include "pdf_min.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pdfmin {
namespace {
const double kPt = 72.0 / 25.4;  // mm -> pt

std::string num(double v) { char b[48]; std::snprintf(b, sizeof b, "%.3f", v); return b; }

std::string asciiOnly(const std::string& u) {
    std::string o;
    for (size_t i = 0; i < u.size();) {
        unsigned char c = static_cast<unsigned char>(u[i]);
        if (c < 0x80) { if (c == '(' || c == ')' || c == '\\') o += '\\'; o += static_cast<char>(c); ++i; }
        else { o += '?'; i += (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2; }
    }
    return o;
}
size_t glyphCount(const std::string& u) {
    size_t n = 0;
    for (unsigned char c : u) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string content(const layout::Page& pg) {
    std::ostringstream s;
    s << "1 J 1 j\n";
    for (const auto& p : pg.paths) {
        if (p.pts.size() < 2) continue;
        s << num(p.style.r) << ' ' << num(p.style.g) << ' ' << num(p.style.b) << " RG " << num(p.style.widthMm * kPt) << " w ";
        if (p.style.dashed && p.style.dashMm > 0) s << "[" << num(p.style.dashMm * kPt) << ' ' << num(p.style.gapMm * kPt) << "] 0 d\n";
        else s << (p.style.dashed ? "[3 3] 0 d\n" : "[] 0 d\n");
        for (size_t i = 0; i < p.pts.size(); ++i)
            s << num(p.pts[i].x * kPt) << ' ' << num((pg.h - p.pts[i].y) * kPt) << (i ? " l\n" : " m\n");
        s << "S\n";
    }
    for (const auto& t : pg.texts) {
        double wPt = glyphCount(t.utf8) * t.sizePt * 0.55;  // rough Helvetica width, dev writer only
        double x = t.x * kPt - (t.center ? wPt / 2 : 0);
        s << "BT /F1 " << num(t.sizePt) << " Tf " << num(t.r) << ' ' << num(t.g) << ' ' << num(t.b) << " rg "
          << num(x) << ' ' << num((pg.h - t.y) * kPt) << " Td (" << asciiOnly(t.utf8) << ") Tj ET\n";
    }
    return s.str();
}
}  // namespace

void write(const layout::Plan& plan, const std::filesystem::path& path) {
    const size_t N = plan.pages.size();
    if (N == 0) throw std::runtime_error("nothing to write");
    // objects: 1 catalog, 2 pages, 3 font, then per page: page obj, content obj
    std::vector<std::string> obj(3 + 2 * N + 1);
    std::string kids;
    for (size_t i = 0; i < N; ++i) {
        const auto& pg = plan.pages[i];
        size_t pid = 4 + 2 * i, cid = pid + 1;
        kids += std::to_string(pid) + " 0 R ";
        obj[pid] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(pg.w * kPt) + " " + num(pg.h * kPt) +
                   "] /Resources << /Font << /F1 3 0 R >> >> /Contents " + std::to_string(cid) + " 0 R >>";
        std::string c = content(pg);
        obj[cid] = "<< /Length " + std::to_string(c.size()) + " >>\nstream\n" + c + "endstream";
    }
    obj[1] = "<< /Type /Catalog /Pages 2 0 R >>";
    obj[2] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(N) + " >>";
    obj[3] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";

    std::filesystem::path tmp = path;
    tmp += ".part";  // atomic write: temp file then rename
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) throw std::runtime_error("cannot write " + tmp.u8string());
        std::string out = "%PDF-1.4\n";
        std::vector<size_t> off(obj.size(), 0);
        for (size_t i = 1; i + 1 < obj.size() + 0 && i < obj.size(); ++i) {
            if (obj[i].empty()) continue;
            off[i] = out.size();
            out += std::to_string(i) + " 0 obj\n" + obj[i] + "\nendobj\n";
        }
        size_t xref = out.size();
        out += "xref\n0 " + std::to_string(obj.size()) + "\n0000000000 65535 f \n";
        for (size_t i = 1; i < obj.size(); ++i) {
            char b[32]; std::snprintf(b, sizeof b, "%010zu 00000 n \n", off[i]); out += b;
        }
        out += "trailer\n<< /Size " + std::to_string(obj.size()) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!f) throw std::runtime_error("write failed (disk full?)");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) throw std::runtime_error("cannot finalize " + path.u8string());
}
}  // namespace pdfmin
