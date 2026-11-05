// Editing a PLT without regenerating it: find stroke-drawn labels (CAD single-stroke text such as "小红叶") by box + shape matching and
// switch their strokes off in the original file text.  Everything else in the file stays byte for byte as it was.
#pragma once
#include <string>
#include <vector>

#include "plt.h"

namespace plt {

using Stroke = std::vector<Pt>;   // millimetres, Y up (as in Document)

struct ShapeEdit {
    bool ok = false;
    std::string error;
    int selected = 0;               // strokes inside the box (or in the template)
    int copies = 0;                 // additional copies found elsewhere
    std::vector<size_t> lines;      // indices into Document::lines to remove (selection + copies)
};

// Strokes of `doc` that lie completely inside `box` (editable ones only).
std::vector<size_t> linesInBox(const Document& doc, const Bounds& box);
// Template = the strokes inside the box.
bool extractTemplate(const Document& doc, const Bounds& box, std::vector<Stroke>& tpl, std::string* error = nullptr);
// Every copy of the template (same shape; size 0.3x-3x, mirrored and/or turned by multiples of 90 degrees, either stroke direction).
ShapeEdit findTemplateCopies(const Document& doc, const std::vector<Stroke>& tpl, double tolMm = 0.12);
// Box one instance, find every copy (everywhere=false: just the boxed strokes).
ShapeEdit findShapeCopies(const Document& doc, const Bounds& box, bool everywhere = true, double tolMm = 0.12);

// `doc` must come from parseString(data) of the very same text (line indices refer to it).  Returns the edited text.
// Removed strokes become pen-up moves, so the pen position (and with it every relative command after it) is unchanged.
std::string removeLines(const std::string& data, const Document& doc, const std::vector<size_t>& lines);

}  // namespace plt
