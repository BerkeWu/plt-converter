#pragma once
#include <functional>
#include <QString>
#include "../core/layout.h"

// Production PDF exporter (Qt6, module QtGui). Vector output via QPdfWriter, system CJK fonts are embedded.
// NOTE: written against the Qt6 API but NOT yet compiled in the dev sandbox (no Qt there) - first task of milestone 2.
class PdfExporterQt {
public:
    // Writes to "<path>.part" then renames, so a cancelled / failed export never leaves a broken PDF.
    // progress(i, n) may return false to cancel. Returns an empty string on success, else an error message.
    static QString write(const layout::Plan& plan, const QString& path,
                         const std::function<bool(int, int)>& progress = {});
};
