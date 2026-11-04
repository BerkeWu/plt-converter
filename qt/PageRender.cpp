#include "PageRender.h"

#include <QFont>
#include <algorithm>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

QImage renderPageThumb(const layout::Page& pg, int maxSide, bool redBorder) {
    const double scale = maxSide / std::max(pg.w, pg.h);
    QImage img(std::max(8, int(pg.w * scale + 0.5)), std::max(8, int(pg.h * scale + 0.5)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto& pa : pg.paths) {
        if (pa.pts.size() < 2) continue;
        QPen pen(QColor::fromRgbF(pa.style.r, pa.style.g, pa.style.b));
        pen.setCosmetic(true);
        pen.setWidthF(std::max(1.0, pa.style.widthMm * scale));
        if (pa.style.dashed && pa.style.dashMm > 0) {
            const double wpx = std::max(1.0, pa.style.widthMm * scale);
            pen.setDashPattern({std::max(1.0, pa.style.dashMm * scale / wpx), std::max(1.0, pa.style.gapMm * scale / wpx)});
        } else if (pa.style.dashed) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        QPolygonF poly;
        poly.reserve(int(pa.pts.size()));
        for (const auto& q : pa.pts) poly << QPointF(q.x * scale, q.y * scale);
        p.drawPolyline(poly);
    }
    for (const auto& t : pg.texts) {
        const double px = t.sizePt * layout::kPtMm * scale;  // pt -> mm -> px
        if (px < 5) continue;
        QFont f;
        f.setPixelSize(std::max(1, int(px)));
        f.setBold(t.bold);
        p.setFont(f);
        const double a = std::clamp(t.alpha, 0.0, 1.0);
        const QColor col = QColor::fromRgbF(1 - a * (1 - t.r), 1 - a * (1 - t.g), 1 - a * (1 - t.b));
        const QString s = QString::fromUtf8(t.utf8.c_str());
        const double w = QFontMetricsF(f).horizontalAdvance(s);
        const double x0 = t.center ? t.x * scale - w / 2 : t.x * scale;
        p.setPen(col);
        p.drawText(QPointF(x0, t.y * scale), s);
        if (t.boxed) {
            const double cap = layout::kCapRatio * px, pad = layout::kBoxPadMm * scale;
            QPen bp(col);
            bp.setWidthF(std::max(1.0, 0.35 * scale));
            p.setPen(bp);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(x0 - pad, t.y * scale - cap - pad, w + 2 * pad, cap + 2 * pad));
        }
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(redBorder ? QColor("#D33") : QColor("#B8C4C3"), redBorder ? 3 : 1));
    p.drawRect(img.rect().adjusted(0, 0, -1, -1));
    return img;
}
