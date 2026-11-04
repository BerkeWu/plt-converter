// Renders a layout::Page to a raster thumbnail (thread-safe: QImage only).
#pragma once
#include <QImage>

#include "../core/layout.h"

// maxSide: longest edge in pixels. redBorder marks e.g. a calibration-box collision.
QImage renderPageThumb(const layout::Page& pg, int maxSide, bool redBorder = false);
