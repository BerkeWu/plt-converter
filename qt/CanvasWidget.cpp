#include "CanvasWidget.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    tip_ = new QFrame(this);
    tip_->setObjectName("tip");
    auto* l = new QHBoxLayout(tip_);
    l->setContentsMargins(12, 8, 8, 8);
    tipLabel_ = new QLabel(tip_);
    tipApply_ = new QPushButton(tr("应用"), tip_);
    tipClose_ = new QPushButton(tr("不再提示"), tip_);
    for (auto* b : {tipApply_, tipClose_}) { b->setFlat(true); b->setCursor(Qt::PointingHandCursor); b->setProperty("link", true); }
    l->addWidget(tipLabel_);
    l->addWidget(tipApply_);
    l->addWidget(tipClose_);
    tip_->hide();
    connect(tipApply_, &QPushButton::clicked, this, &CanvasWidget::tipApply);
    connect(tipClose_, &QPushButton::clicked, this, &CanvasWidget::tipDismissed);
}

void CanvasWidget::setView(std::shared_ptr<const DrawingView> v) {
    const bool sizeChanged = !view_ || !v || view_->w != v->w || view_->h != v->h;
    view_ = std::move(v);
    if (sizeChanged) autoFit_ = true;
    if (autoFit_) refit();
    update();
}

void CanvasWidget::setMode(Mode m) {
    if (mode_ == m) return;
    mode_ = m;
    autoFit_ = true;
    refit();
    update();
}

void CanvasWidget::setGrid(const Grid& g) {
    grid_ = g;
    if (autoFit_) refit();
    update();
}

void CanvasWidget::setOffset(const QPointF& mm) {
    off_ = mm;
    if (autoFit_ && mode_ == Layout) refit();
    update();
}

void CanvasWidget::setSelection(bool has, const QRectF& uv) { hasSel_ = has; sel_ = uv; update(); }

void CanvasWidget::setSelectTool(bool on) {
    selectTool_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasWidget::setHighlightCell(int r, int c) { hiR_ = r; hiC_ = c; update(); }

QRectF CanvasWidget::gridRect() const {
    if (!grid_.valid) return {};
    return QRectF(0, 0, grid_.nx * grid_.Wp + 2 * grid_.strip, grid_.ny * grid_.Hp + 2 * grid_.strip);
}

QRectF CanvasWidget::sceneRect() const {
    QRectF r;
    if (view_) r = QRectF(mode_ == Layout ? off_ : QPointF(0, 0), QSizeF(view_->w, view_->h));
    if (mode_ == Layout && grid_.valid) r = r.united(gridRect());
    if (r.width() < 1) r.setWidth(1);
    if (r.height() < 1) r.setHeight(1);
    return r;
}

void CanvasWidget::refit() {
    const QRectF sr = sceneRect();
    const double pad = 36;
    scale_ = std::max(0.01, std::min((width() - 2 * pad) / sr.width(), (height() - 2 * pad) / sr.height()));
    origin_ = QPointF(width() / 2.0, height() / 2.0) - sr.center() * scale_;
}

void CanvasWidget::fitWindow() { autoFit_ = true; refit(); update(); }

void CanvasWidget::actualSize() {
    autoFit_ = false;
    const QPointF c = toWorld(QPointF(width() / 2.0, height() / 2.0));
    scale_ = logicalDpiX() / 25.4;  // 1 mm on screen = 1 mm physical (as far as the OS dpi is right)
    origin_ = QPointF(width() / 2.0, height() / 2.0) - c * scale_;
    update();
}

void CanvasWidget::focusCell(int r, int c) {
    hiR_ = r; hiC_ = c;
    if (!grid_.valid || r < 0 || c < 0) { update(); return; }
    autoFit_ = false;
    // frame of the sheet plus the strip printed outside it
    const QRectF cell(c * grid_.Wp, r * grid_.Hp, grid_.Wp + 2 * grid_.strip, grid_.Hp + 2 * grid_.strip);
    const double pad = 40;
    scale_ = std::max(0.01, std::min((width() - 2 * pad) / cell.width(), (height() - 2 * pad) / cell.height()));
    origin_ = QPointF(width() / 2.0, height() / 2.0) - cell.center() * scale_;
    update();
}

void CanvasWidget::setTip(const QString& text, bool applyBtn, bool dismissBtn) {
    tipLabel_->setText(text);
    tipApply_->setVisible(applyBtn);
    tipClose_->setVisible(dismissBtn);
    tip_->setVisible(!text.isEmpty());
    layoutTip();
}

void CanvasWidget::layoutTip() {
    tip_->setMaximumWidth(std::max(200, width() - 28));
    tip_->adjustSize();
    tip_->move(14, height() - tip_->height() - 12);
    tip_->raise();
}

void CanvasWidget::resizeEvent(QResizeEvent*) {
    if (autoFit_) refit();
    layoutTip();
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#E6ECEB"));
    if (!view_) {
        p.setPen(QColor("#6B7B7A"));
        QFont f = font();
        f.setPixelSize(18);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, emptyHint_);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing, view_->segments < 150000);

    // ---- page grid (layout mode) ----
    if (mode_ == Layout && grid_.valid) {
        const double st = grid_.strip;
        p.save();
        p.translate(origin_);
        p.scale(scale_, scale_);
        // everything that is printed (frames + the strips outside them)
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 120));
        p.drawRect(gridRect());
        QPen dash(QColor("#2563EB"));   // the registration frame, like the blue dashed lines on the sheets
        dash.setCosmetic(true);
        dash.setStyle(Qt::DashLine);
        for (int r = 0; r < grid_.ny; ++r)
            for (int c = 0; c < grid_.nx; ++c) {
                const QRectF cell(st + c * grid_.Wp, st + r * grid_.Hp, grid_.Wp, grid_.Hp);
                const bool present = grid_.allPresent || grid_.present.contains(r * 10000 + c);
                p.setPen(dash);
                p.setBrush(present ? QColor(255, 255, 255, 110) : QColor(200, 208, 207, 170));
                p.drawRect(cell);
            }
        if (!grid_.singlePage && st > 0) {   // sheets overlap here: each seam carries 2 * strip of duplicated pattern
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(246, 183, 60, 90));
            const QRectF g = gridRect();
            for (int c = 1; c < grid_.nx; ++c) p.drawRect(QRectF(c * grid_.Wp, 0, 2 * st, g.height()));
            for (int r = 1; r < grid_.ny; ++r) p.drawRect(QRectF(0, r * grid_.Hp, g.width(), 2 * st));
        }
        p.restore();
    }

    // ---- drawing ----
    {
        p.save();
        const QPointF o = mode_ == Layout ? off_ : QPointF(0, 0);
        p.translate(origin_ + o * scale_);
        p.scale(scale_, scale_);
        // Import step: neutral teal.  Layout step: what will be printed (colour and line width of the settings).
        const bool paper = mode_ == Layout;
        const QColor base = paper ? (useOrig_ ? QColor(Qt::black) : lineColor_) : QColor("#0B5E57");
        QPen pen(base);
        pen.setCosmetic(true);
        pen.setWidthF(paper ? std::max(1.0, lineMm_ * scale_) : 1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QColor cur = base;
        const QRectF vis = QRectF(toWorld(QPointF(0, 0)), toWorld(QPointF(width(), height()))).translated(-o);
        const double minMm = 0.7 / scale_;
        for (int i = 0; i < view_->polys.size(); ++i) {
            const QRectF& b = view_->boxes[i];
            if (b.right() < vis.left() || b.left() > vis.right() || b.bottom() < vis.top() || b.top() > vis.bottom()) continue;
            if (b.width() < minMm && b.height() < minMm) continue;  // LOD: sub-pixel pieces are skipped
            QColor c = base;
            if (paper && useOrig_ && i < view_->colors.size() && view_->colors[i].isValid()) c = view_->colors[i];
            if (c != cur) { pen.setColor(c); p.setPen(pen); cur = c; }
            p.drawPolyline(view_->polys[i]);
        }
        p.restore();
        if (mode_ == Layout && ghostOn_) {
            p.setPen(QPen(QColor("#E58A00"), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(toScreen(ghost_), QSizeF(view_->w * scale_, view_->h * scale_)));
        }
        // drawing outline in layout mode
        if (mode_ == Layout) {
            p.setPen(QPen(QColor(11, 122, 112, 90), 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(toScreen(o), QSizeF(view_->w * scale_, view_->h * scale_)));
        }
    }

    // ---- page labels / highlight (screen space) ----
    if (mode_ == Layout && grid_.valid) {
        const double st = grid_.strip;
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(13);
        p.setFont(f);
        for (int r = 0; r < grid_.ny; ++r)
            for (int c = 0; c < grid_.nx; ++c) {
                const QRectF cell(toScreen(QPointF(st + c * grid_.Wp, st + r * grid_.Hp)), QSizeF(grid_.Wp * scale_, grid_.Hp * scale_));
                const bool present = grid_.allPresent || grid_.present.contains(r * 10000 + c);
                if (cell.width() > 46 && cell.height() > 24) {
                    p.setPen(present ? QColor("#334444") : QColor("#8A9796"));
                    p.drawText(cell.topLeft() + QPointF(6, 16), grid_.singlePage ? tr("单页") : QStringLiteral("%1-%2").arg(r + 1).arg(c + 1));
                    if (!present) { p.setPen(QColor("#8A9796")); p.drawText(cell, Qt::AlignCenter, tr("空白")); }
                }
                if (r == hiR_ && c == hiC_) { p.setPen(QPen(QColor("#0B7A70"), 2.5)); p.setBrush(Qt::NoBrush); p.drawRect(cell); }
            }
        // scale bar
        const double barMm = std::pow(10.0, std::floor(std::log10(120.0 / scale_)));
        const double barPx = barMm * scale_;
        p.setPen(QPen(QColor("#334444"), 2));
        const QPointF a(width() - 20 - barPx, height() - 18), b(width() - 20, height() - 18);
        p.drawLine(a, b);
        p.drawLine(a + QPointF(0, -4), a + QPointF(0, 4));
        p.drawLine(b + QPointF(0, -4), b + QPointF(0, 4));
        QFont f2 = font();
        f2.setPixelSize(11);
        p.setFont(f2);
        p.setPen(QColor("#334444"));
        p.drawText(QRectF(b.x() - 90, a.y() - 20, 90, 14), Qt::AlignRight, barMm >= 10 ? QStringLiteral("%1 mm").arg(barMm) : QStringLiteral("%1 mm").arg(barMm, 0, 'g', 2));
    }

    // ---- selection ----
    if ((hasSel_ || selecting_) && view_) {
        const QRectF s(toScreen(sel_.topLeft()), toScreen(sel_.bottomRight()));
        p.setPen(QPen(QColor("#0B7A70"), 1.5, Qt::DashLine));
        p.setBrush(QColor(11, 122, 112, 36));
        p.drawRect(s.normalized());
        p.setPen(QColor("#0B6A61"));
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(s.normalized().topLeft() + QPointF(4, -5), QStringLiteral("%1 × %2 mm").arg(sel_.width(), 0, 'f', 1).arg(sel_.height(), 0, 'f', 1));
    }
}

void CanvasWidget::zoomAt(const QPointF& sp, double factor) {
    autoFit_ = false;
    const QPointF w = toWorld(sp);
    scale_ = std::min(300.0, std::max(0.005, scale_ * factor));
    origin_ = sp - w * scale_;
    update();
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    zoomAt(e->position(), std::pow(1.15, e->angleDelta().y() / 120.0));
    e->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (!view_) return;
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && space_)) {
        drag_ = Pan;
        dragStartScreen_ = e->position();
        dragStartOrigin_ = origin_;
        setCursor(Qt::ClosedHandCursor);
        autoFit_ = false;
    } else if (e->button() == Qt::LeftButton) {
        if (mode_ == Import && selectTool_) {
            const QPointF w = toWorld(e->position());
            selAnchor_ = QPointF(std::clamp(w.x(), 0.0, view_->w), std::clamp(w.y(), 0.0, view_->h));
            sel_ = QRectF(selAnchor_, selAnchor_);
            selecting_ = true;
        } else if (mode_ == Layout) {
            drag_ = Move;
            dragStartScreen_ = e->position();
            dragStartOff_ = off_;
            setCursor(Qt::SizeAllCursor);
            emit dragBegan();
        }
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (view_) {
        const QPointF w = toWorld(e->position()) - (mode_ == Layout ? off_ : QPointF(0, 0));
        emit cursorMm(w, w.x() >= 0 && w.y() >= 0 && w.x() <= view_->w && w.y() <= view_->h);
    }
    if (drag_ == Pan) {
        origin_ = dragStartOrigin_ + (e->position() - dragStartScreen_);
        update();
    } else if (drag_ == Move) {
        QPointF d = (e->position() - dragStartScreen_) / scale_;
        off_ = QPointF(std::max(0.0, dragStartOff_.x() + d.x()), std::max(0.0, dragStartOff_.y() + d.y()));
        emit offsetEdited(off_, false);
        update();
    } else if (selecting_) {
        const QPointF w = toWorld(e->position());
        const QPointF c(std::clamp(w.x(), 0.0, view_->w), std::clamp(w.y(), 0.0, view_->h));
        sel_ = QRectF(selAnchor_, c).normalized();
        update();
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e);
    if (drag_ == Move) emit offsetEdited(off_, true);
    if (drag_ != None) setCursor(selectTool_ ? Qt::CrossCursor : Qt::ArrowCursor);
    drag_ = None;
    if (selecting_) {
        selecting_ = false;
        const bool ok = sel_.width() > 0.5 && sel_.height() > 0.5;  // ignore stray clicks
        hasSel_ = ok;
        emit selectionEdited(ok, sel_, true);
        update();
    }
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { space_ = true; setCursor(Qt::OpenHandCursor); return; }
    if (mode_ == Layout && view_) {
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        switch (e->key()) {
            case Qt::Key_Left: emit nudged(-step, 0); return;
            case Qt::Key_Right: emit nudged(step, 0); return;
            case Qt::Key_Up: emit nudged(0, -step); return;
            case Qt::Key_Down: emit nudged(0, step); return;
            default: break;
        }
    }
    QWidget::keyPressEvent(e);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { space_ = false; setCursor(selectTool_ ? Qt::CrossCursor : Qt::ArrowCursor); return; }
    QWidget::keyReleaseEvent(e);
}

void CanvasWidget::leaveEvent(QEvent*) { emit cursorMm({}, false); }
