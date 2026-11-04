// PLT (HP-GL) parser. Output geometry is in millimetres, Y axis pointing up (as in the file).
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace plt {

struct Pt { double x = 0, y = 0; };

struct Bounds {
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    bool valid() const { return minX <= maxX && minY <= maxY; }
    double w() const { return valid() ? maxX - minX : 0; }
    double h() const { return valid() ? maxY - minY : 0; }
    void add(const Pt& p) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
    }
};

struct Rgb { double r = 0, g = 0, b = 0; };  // 0..1

struct Polyline {
    int pen = 1;
    std::vector<Pt> pts;  // >= 2 points
};

struct Document {
    std::vector<Polyline> lines;
    Bounds bounds;
    double unitsPerMm = 40.0;                // HP-GL default: 1 unit = 0.025 mm
    size_t segmentCount = 0;
    double totalLengthMm = 0;
    std::map<int, Rgb> penColors;             // pen number -> colour defined by PC (HP-GL/2); pens not listed have no colour info
    std::map<std::string, int> commandCount;  // every mnemonic seen
    std::map<std::string, int> unsupported;   // mnemonics that may carry geometry but are not handled
    std::vector<std::string> warnings;
};

// Throws std::runtime_error on unreadable / empty / non-HPGL input.
Document parseString(const std::string& data);
// Takes a path object so non-ASCII (e.g. Chinese) paths work on Windows. Build it from UTF-8 text with std::filesystem::u8path().
Document parseFile(const std::filesystem::path& path);

// Paper-frame ("门幅边框") handling. A frame is the largest closed, axis-aligned rectangle polyline whose
// area is >= 70% of the drawing's bounding box. Returns its index in doc.lines, or -1.
int findFrame(const Document& doc);
// Removes the frame (if any) and recomputes bounds/length/segments. Returns true when a frame was removed.
// Removes every polyline that duplicates the frame (CADs often draw it twice). Returns how many lines were removed (0 = none found).
int stripFrame(Document& doc);

}  // namespace plt
