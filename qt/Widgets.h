// Small reusable controls matching the design draft (teal pill switch, colour swatch, segmented buttons).
#pragma once
#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>

class ToggleSwitch : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }
    QSize sizeHint() const override { return {36, 22}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? (isChecked() ? QColor("#0B7A70") : QColor("#9AA9A8")) : QColor("#D5DCDB"));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        const double d = r.height() - 6;
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(isChecked() ? r.right() - d - 3 : r.left() + 3, r.top() + 3, d, d));
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#0B7A70"), 1, Qt::DotLine)); p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 11, 11); }
    }
};

class Swatch : public QAbstractButton {
public:
    explicit Swatch(const QColor& c, QWidget* parent = nullptr) : QAbstractButton(parent), color_(c) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
    }
    void setColor(const QColor& c) { color_ = c; update(); }
    QColor color() const { return color_; }
    QSize sizeHint() const override { return {24, 24}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        p.setBrush(color_);
        p.setPen(QPen(isChecked() ? QColor("#0B7A70") : QColor("#C3CECD"), isChecked() ? 2.5 : 1));
        p.drawEllipse(r);
    }

private:
    QColor color_;
};

// Segmented button row; exactly one button checked. `prop` lets the style sheet pick it up.
inline QWidget* makeSegmented(const QStringList& labels, QButtonGroup*& group, QWidget* parent = nullptr) {
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    group = new QButtonGroup(w);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new QPushButton(labels[i], w);
        b->setCheckable(true);
        b->setProperty("seg", true);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b, i);
        l->addWidget(b);
    }
    return w;
}
