// Document-level operations used by the GUI: rotate / mirror (PRD 1.B) and region selection (PRD 模块一 框选).
#pragma once
#include "plt.h"

namespace docops {

// Recomputes bounds / segment count / total length from doc.lines.
void recompute(plt::Document& d);

// Mirror (x -> -x) is applied first, then rotation by rot90ccw * 90 degrees counter-clockwise.
void transform(plt::Document& d, int rot90ccw, bool mirrorX);

// Keeps the part of the drawing inside the rectangle (document coordinates, mm, Y up).
// clip = true: lines crossing the border are cut at the border; false: every line touching the rectangle is kept whole.
// The result may have no lines.
plt::Document crop(const plt::Document& d, double x0, double y0, double x1, double y1, bool clip);

}  // namespace docops
