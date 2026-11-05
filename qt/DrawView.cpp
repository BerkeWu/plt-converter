#include "DrawView.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>

namespace {
std::string keyOf(const wm::DrawItem& d) {
    char b[160];
    const auto& f = d.pts.front();
    const auto& l = d.pts.back();
    std::snprintf(b, sizeof b, "%d|%zu|%.1f,%.1f|%.1f,%.1f|%d|", int(d.kind), d.pts.size(), f.first, f.second, l.first, l.second, d.glyphs);
    return std::string(b) + d.text;
}
}  // namespace

DrawView::DrawView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 360);
    setAutoFillBackground(false);
    auto mk = [this](const QString& t, const QString& tip, int w) {
        auto* b = new QPushButton(t, this);
        b->setFixedSize(w, 28);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::ArrowCursor);
        b->setStyleSheet("QPushButton{background:rgba(255,255,255,235);border:1px solid #B8C4C3;border-radius:6px;color:#16302E;font-weight:600;padding:0}"
                         "QPushButton:hover{background:#E6F2F1}");
        return b;
    };
    zOut_ = mk(QString::fromUtf8("－"), tr("缩小"), 30);
    zIn_ = mk(QString::fromUtf8("＋"), tr("放大"), 30);
    zFit_ = mk(tr("适合窗口"), tr("恢复为整页显示（双击预览也可以）"), 74);
    connect(zOut_, &QPushButton::clicked, this, [this] { zoomOut(); });
    connect(zIn_, &QPushButton::clicked, this, [this] { zoomIn(); });
    connect(zFit_, &QPushButton::clicked, this, [this] { zoomFit(); });
    layoutButtons();
}

void DrawView::layoutButtons() {
    const int y = 8;
    int x = width() - 8;
    x -= zFit_->width(); zFit_->move(x, y);
    x -= 4 + zIn_->width(); zIn_->move(x, y);
    x -= 4 + zOut_->width(); zOut_->move(x, y);
    zOut_->setEnabled(zoom_ > 0.26);
    zIn_->setEnabled(zoom_ < 31.9);
    zFit_->setText(zoom_ > 0.999 && zoom_ < 1.001 ? tr("适合窗口") : QString("%1%").arg(qRound(zoom_ * 100)));
}
void DrawView::resizeEvent(QResizeEvent*) { layoutButtons(); }

void DrawView::clear() {
    before_.reset(); after_.reset(); removed_.clear(); removedCount_ = 0; box_ = QRectF(); zoom_ = 1; pan_ = {}; layoutButtons(); update();
}

void DrawView::setDrawings(std::shared_ptr<const wm::PageDrawing> before, std::shared_ptr<const wm::PageDrawing> after) {
    const bool sameSize = before_ && before && std::abs(before_->w - before->w) < 0.01 && std::abs(before_->h - before->h) < 0.01;
    before_ = std::move(before);
    after_ = std::move(after);
    if (!sameSize) { zoom_ = 1; pan_ = {}; layoutButtons(); }
    removed_.assign(before_ ? before_->items.size() : 0, 0);
    removedCount_ = 0;
    if (before_ && after_ && before_->ok && after_->ok) {
        std::map<std::string, int> left;
        for (auto& d : after_->items) if (!d.pts.empty()) ++left[keyOf(d)];
        for (size_t i = 0; i < before_->items.size(); ++i) {
            const auto& d = before_->items[i];
            if (d.pts.empty()) continue;
            auto it = left.find(keyOf(d));
            if (it != left.end() && it->second > 0) --it->second;
            else { removed_[i] = 1; ++removedCount_; }
        }
    }
    update();
}

void DrawView::setMode(Mode m) { mode_ = m; update(); }
void DrawView::setHighlight(bool on) { highlight_ = on; update(); }
void DrawView::setSelectEnabled(bool on) { select_ = on; setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor); if (!on) { dragging_ = false; box_ = QRectF(); } update(); }
void DrawView::setBox(const QRectF& r) { box_ = r; update(); }

QRectF DrawView::fitRect() const {
    if (!before_ || before_->w <= 0 || before_->h <= 0) return {};
    const double m = 16;
    const double sc = std::min((width() - 2 * m) / before_->w, (height() - 2 * m) / before_->h);
    const double w = before_->w * sc, h = before_->h * sc;
    return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
}

QRectF DrawView::pageRect() const {
    const QRectF f = fitRect();
    if (f.isEmpty()) return {};
    const QPointF c = f.center() + pan_;
    const double w = f.width() * zoom_, h = f.height() * zoom_;
    return QRectF(c.x() - w / 2, c.y() - h / 2, w, h);
}

QPointF DrawView::pageToWidget(const QPointF& q) const {
    const QRectF r = pageRect();
    if (r.isEmpty() || !before_) return {};
    return {r.x() + q.x() / before_->w * r.width(), r.y() + q.y() / before_->h * r.height()};
}

void DrawView::applyZoom(double z, const QPointF& anchorPx) {
    const QRectF f = fitRect();
    if (f.isEmpty()) return;
    z = std::clamp(z, 0.25, 32.0);
    if (std::abs(z - zoom_) < 1e-9) return;
    // keep the page point under the anchor where it is
    const QRectF r = pageRect();
    const double u = (anchorPx.x() - r.x()) / r.width(), v = (anchorPx.y() - r.y()) / r.height();
    zoom_ = z;
    const double w = f.width() * zoom_, h = f.height() * zoom_;
    const QPointF topLeft(anchorPx.x() - u * w, anchorPx.y() - v * h);
    pan_ = QPointF(topLeft.x() + w / 2, topLeft.y() + h / 2) - f.center();
    layoutButtons();
    update();
    emit zoomChanged(zoom_);
}

void DrawView::setZoom(double z) { applyZoom(z, QPointF(width() / 2.0, height() / 2.0)); }

void DrawView::zoomFit() {
    zoom_ = 1; pan_ = {};
    layoutButtons();
    update();
    emit zoomChanged(zoom_);
}

QPointF DrawView::toPage(const QPointF& px) const {
    const QRectF r = pageRect();
    if (r.isEmpty() || !before_) return {};
    return {(px.x() - r.x()) / r.width() * before_->w, (px.y() - r.y()) / r.height() * before_->h};
}

void DrawView::paintItems(QPainter& p, const wm::PageDrawing& d, const QRectF& r, double sc, const std::vector<char>* only, bool hl) {
    auto X = [&](double x) { return r.x() + x * sc; };
    auto Y = [&](double y) { return r.y() + y * sc; };
    const QColor orange(0xC2, 0x41, 0x0C);
    for (size_t i = 0; i < d.items.size(); ++i) {
        const auto& it = d.items[i];
        if (it.pts.empty()) continue;
        const bool isRem = only && i < only->size() && (*only)[i];
        if (hl && !isRem) { /* normal item */ }
        QColor c = QColor::fromRgbF(std::clamp(it.r, 0.f, 1.f), std::clamp(it.g, 0.f, 1.f), std::clamp(it.b, 0.f, 1.f), std::clamp(it.a, 0.f, 1.f));
        if (hl && isRem) c = orange;
        switch (it.kind) {
        case wm::DrawItem::Stroke: {
            QPen pen(c);
            pen.setWidthF(std::max(hl && isRem ? 1.6 : 0.8, it.lineWidth * sc));
            pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen); p.setBrush(Qt::NoBrush);
            QPolygonF poly;
            for (auto& q : it.pts) poly << QPointF(X(q.first), Y(q.second));
            p.drawPolyline(poly);
            break;
        }
        case wm::DrawItem::Fill: {
            if (hl && isRem) c.setAlphaF(0.55);
            p.setPen(Qt::NoPen); p.setBrush(c);
            QPolygonF poly;
            for (auto& q : it.pts) poly << QPointF(X(q.first), Y(q.second));
            p.drawPolygon(poly);
            break;
        }
        case wm::DrawItem::Text: {
            const double px = it.size * sc;
            if (px < 1.5) break;
            p.save();
            p.translate(X(it.pts[0].first), Y(it.pts[0].second));
            p.rotate(-it.angle * 180.0 / 3.14159265358979);
            QFont f = p.font();
            f.setPixelSize(std::max(2, int(px)));
            p.setFont(f);
            if (hl && isRem) c.setAlphaF(std::max(0.55, double(c.alphaF())));
            p.setPen(c);
            if (!it.text.empty()) p.drawText(QPointF(0, 0), QString::fromUtf8(it.text.c_str()));
            else {
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(0, -px * 0.8, px * 0.6 * std::max(1, it.glyphs), px * 0.8));
            }
            p.restore();
            break;
        }
        case wm::DrawItem::ImageBox: {
            QPen pen(c); pen.setStyle(Qt::DashLine); p.setPen(pen); p.setBrush(Qt::NoBrush);
            QPolygonF poly;
            for (auto& q : it.pts) poly << QPointF(X(q.first), Y(q.second));
            p.drawPolyline(poly);
            break;
        }
        }
    }
}

void DrawView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#E4EAEA"));
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = pageRect();
    if (r.isEmpty()) {
        p.setPen(QColor("#5B6B6B"));
        p.drawText(rect(), Qt::AlignCenter, tr("选择左侧的文件查看预览"));
        return;
    }
    p.fillRect(r.translated(2, 3), QColor(0, 0, 0, 40));
    p.fillRect(r, Qt::white);
    p.save();
    p.setClipRect(r);
    const double sc = r.width() / before_->w;
    if (mode_ == Removed && after_ && after_->ok) paintItems(p, *after_, r, sc, nullptr, false);
    else paintItems(p, *before_, r, sc, &removed_, highlight_);
    p.restore();
    p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#B8C4C3"), 1)); p.drawRect(r);
    p.setPen(QColor("#5B6B6B"));
    QFont hf = p.font(); hf.setPixelSize(12); p.setFont(hf);
    p.drawText(QRectF(10, height() - 24, width() - 20, 18), Qt::AlignLeft | Qt::AlignVCenter, tr("滚轮缩放 · 右键或中键拖动平移 · 双击适合窗口"));
    QRectF bx = box_;
    if (dragging_) bx = QRectF(toPage(a_), toPage(b_)).normalized();
    if (!bx.isEmpty()) {
        const QRectF px(r.x() + bx.x() * sc, r.y() + bx.y() * sc, bx.width() * sc, bx.height() * sc);
        p.setPen(QPen(QColor("#C2410C"), 2, Qt::DashLine));
        p.setBrush(QColor(0xC2, 0x41, 0x0C, 28));
        p.drawRect(px);
    }
}

void DrawView::mousePressEvent(QMouseEvent* e) {
    if (pageRect().isEmpty()) return;
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && !select_)) {
        panning_ = true; panFrom_ = e->position(); setCursor(Qt::ClosedHandCursor); return;
    }
    if (!select_ || e->button() != Qt::LeftButton) return;
    dragging_ = true; a_ = b_ = e->position(); box_ = QRectF(); update();
}
void DrawView::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) { pan_ += e->position() - panFrom_; panFrom_ = e->position(); update(); return; }
    if (dragging_) { b_ = e->position(); update(); }
}
void DrawView::mouseDoubleClickEvent(QMouseEvent* e) { if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton) zoomFit(); }
void DrawView::wheelEvent(QWheelEvent* e) {
    if (pageRect().isEmpty()) return;
    const double d = e->angleDelta().y();
    if (d == 0) return;
    applyZoom(zoom_ * std::pow(1.0015, d), e->position());
    e->accept();
}
void DrawView::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_) { panning_ = false; setCursor(select_ ? Qt::CrossCursor : Qt::ArrowCursor); return; }
    if (!dragging_) return;
    dragging_ = false; b_ = e->position();
    const QRectF r = QRectF(toPage(a_), toPage(b_)).normalized();
    if (r.width() < 2 || r.height() < 2) { update(); return; }
    box_ = r;
    update();
    emit boxed(r.left(), r.top(), r.right(), r.bottom());
}
