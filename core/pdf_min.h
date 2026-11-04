// Minimal dependency-free vector PDF writer used by the CLI / tests (Helvetica only: non-ASCII
// glyphs are drawn as '?'). The production exporter uses QPdfWriter (qt/PdfExporterQt.cpp).
#pragma once
#include <filesystem>
#include <string>

#include "layout.h"

namespace pdfmin {
// Writes every page of the plan as a true vector PDF at 1:1 physical size. Throws on I/O error.
void write(const layout::Plan& plan, const std::filesystem::path& path);
}
