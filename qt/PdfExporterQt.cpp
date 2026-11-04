#include "PdfExporterQt.h"

#include <QFile>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <algorithm>

namespace {

// Printing pipelines (Acrobat/GDI/browser) may rasterise a whole page at low resolution when it contains transparency, which
// makes every vector line look blurry on paper.  So the PDF never uses transparency: "80% opacity" is pre-blended with the
// white paper instead, which looks identical on a white sheet.
QColor onWhite(const QColor& c, double alpha) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    return QColor::fromRgbF(1 - alpha * (1 - c.redF()), 1 - alpha * (1 - c.greenF()), 1 - alpha * (1 - c.blueF()));
}

// Appends an incremental update that adds /ViewerPreferences << /PrintScaling /None >> to the catalog.  Acrobat, Foxit and
// Chromium-based viewers then default to printing at 100% instead of "fit to page" (which shrinks every sheet and makes
// neighbouring sheets impossible to join).  Anything unexpected in the file -> leave it untouched (returns false).
bool addNoScalingHint(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadWrite)) return false;
    const QByteArray d = f.readAll();
    static const QRegularExpression tailRe(QStringLiteral("trailer\\s*<<(.*?)>>\\s*startxref\\s*(\\d+)\\s*%%EOF\\s*$"), QRegularExpression::DotMatchesEverythingOption);
    const QString tail = QString::fromLatin1(d.right(2048));
    const auto tm = tailRe.match(tail);
    if (!tm.hasMatch()) return false;
    const QString trailer = tm.captured(1);
    const QString prev = tm.captured(2);
    static const QRegularExpression rootRe(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R")), sizeRe(QStringLiteral("/Size\\s+(\\d+)")),
        infoRe(QStringLiteral("/Info\\s+\\d+\\s+\\d+\\s+R")), idRe(QStringLiteral("/ID\\s*\\[[^\\]]*\\]"));
    const auto rm = rootRe.match(trailer);
    const auto sm = sizeRe.match(trailer);
    if (!rm.hasMatch() || !sm.hasMatch()) return false;
    const QString rootNum = rm.captured(1);
    const QString body = QString::fromLatin1(d);
    const QRegularExpression catRe(QStringLiteral("(?:^|\\n)") + rootNum + QStringLiteral("\\s+0\\s+obj\\s*<<([^<>]*)>>\\s*endobj"));
    const auto cm = catRe.match(body);
    if (!cm.hasMatch() || cm.captured(1).contains(QStringLiteral("/ViewerPreferences")) || !cm.captured(1).contains(QStringLiteral("/Type /Catalog"))) return false;
    QByteArray add;
    if (!d.endsWith('\n')) add += '\n';
    const qint64 objOff = d.size() + add.size();
    add += rootNum.toLatin1() + " 0 obj\n<<" + cm.captured(1).trimmed().toLatin1() + " /ViewerPreferences << /PrintScaling /None >> >>\nendobj\n";
    const qint64 xrefOff = d.size() + add.size();
    add += "xref\n" + rootNum.toLatin1() + " 1\n" + QByteArray::number(static_cast<long long>(objOff)).rightJustified(10, '0') + " 00000 n \n";
    add += "trailer\n<< /Size " + sm.captured(1).toLatin1() + " /Root " + rootNum.toLatin1() + " 0 R";
    const auto im = infoRe.match(trailer);
    if (im.hasMatch()) add += " " + im.captured(0).toLatin1();
    const auto dm = idRe.match(trailer);
    if (dm.hasMatch()) add += " " + dm.captured(0).toLatin1();
    add += " /Prev " + prev.toLatin1() + " >>\nstartxref\n" + QByteArray::number(static_cast<long long>(xrefOff)) + "\n%%EOF\n";
    f.seek(d.size());
    return f.write(add) == add.size();
}

}  // namespace

QString PdfExporterQt::write(const layout::Plan& plan, const QString& path, const std::function<bool(int, int)>& progress) {
    const QString tmp = path + ".part";
    {
        QPdfWriter w(tmp);
        const int res = 1200;                       // dpi: 0.02 mm resolution, coordinates stay vector
        w.setResolution(res);
        w.setPdfVersion(QPagedPaintDevice::PdfVersion_1_6);
        w.setPageMargins(QMarginsF(0, 0, 0, 0));
        const double k = res / 25.4;                // painter units = millimetres
        QPainter p;
        const int n = static_cast<int>(plan.pages.size());
        for (int i = 0; i < n; ++i) {
            const auto& pg = plan.pages[i];
            w.setPageSize(QPageSize(QSizeF(pg.w, pg.h), QPageSize::Millimeter));
            if (i == 0) { if (!p.begin(&w)) return QStringLiteral("无法写入文件"); } else w.newPage();
            p.resetTransform();
            p.scale(k, k);
            for (const auto& path2 : pg.paths) {
                QPen pen(QColor::fromRgbF(path2.style.r, path2.style.g, path2.style.b));
                pen.setWidthF(path2.style.widthMm);
                pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
                if (path2.style.dashed && path2.style.dashMm > 0 && path2.style.widthMm > 0) {
                    pen.setCapStyle(Qt::FlatCap);  // round caps would fill the gaps
                    pen.setDashPattern({path2.style.dashMm / path2.style.widthMm, path2.style.gapMm / path2.style.widthMm});  // units = pen widths
                } else if (path2.style.dashed) pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                QPolygonF poly;
                for (const auto& pt : path2.pts) poly << QPointF(pt.x, pt.y);
                p.drawPolyline(poly);
            }
            for (const auto& t : pg.texts) {
                QFont f(QStringLiteral("Microsoft YaHei"));
                f.setPointSizeF(t.sizePt / k);       // font sizes are device-relative; compensate for the painter scale
                f.setBold(t.bold);
                p.setFont(f);
                const QColor col = onWhite(QColor::fromRgbF(t.r, t.g, t.b), t.alpha);
                const QString s = QString::fromUtf8(t.utf8.c_str());
                // Measure at a large pixel size and scale down: metrics of a 0.6 pt font (the painter is scaled by k) are unreliable.
                QFont mf = f;
                mf.setPixelSize(1000);
                const double wMm = QFontMetricsF(mf).horizontalAdvance(s) / 1000.0 * (t.sizePt * layout::kPtMm);
                const double x0 = t.center ? t.x - wMm / 2 : t.x;
                p.setPen(col);
                p.drawText(QPointF(x0, t.y), s);
                if (t.boxed) {
                    const double capMm = layout::kCapRatio * t.sizePt * layout::kPtMm, pad = layout::kBoxPadMm;
                    QPen bp(col);
                    bp.setWidthF(0.35);
                    bp.setJoinStyle(Qt::MiterJoin);
                    p.setPen(bp);
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(QRectF(x0 - pad, t.y - capMm - pad, wMm + 2 * pad, capMm + 2 * pad));
                }
            }
            if (progress && !progress(i + 1, n)) { p.end(); QFile::remove(tmp); return QStringLiteral("已取消"); }
        }
        p.end();
    }
    addNoScalingHint(tmp);
    QFile::remove(path);
    return QFile::rename(tmp, path) ? QString() : QStringLiteral("无法保存到目标位置");
}
