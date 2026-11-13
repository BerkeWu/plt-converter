// Small reusable controls matching the design draft (teal pill switch, colour swatch, segmented buttons).
#pragma once
#include <QAbstractButton>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSlider>
#include <QStyle>
#include <QWheelEvent>
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

// Number box that cannot be changed by accident: it only accepts a value after the user CLICKS it. The mouse wheel never
// changes it (so scrolling the side panel past it is harmless); it locks again when editing finishes or focus is lost.
class ClickSpinBox : public QDoubleSpinBox {
public:
    explicit ClickSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setKeyboardTracking(false);
        setFocusPolicy(Qt::ClickFocus);
        lineEdit()->installEventFilter(this);
        installEventFilter(this);
        connect(this, &QAbstractSpinBox::editingFinished, this, [this] { lock(); });
        lock();
    }
    bool locked() const { return locked_; }
    QLineEdit* editor() const { return lineEdit(); }
    void lock() {
        locked_ = true;
        setReadOnly(true);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        lineEdit()->deselect();
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("点击后修改"));
        repolish();
    }
    void unlock() {
        locked_ = false;
        setReadOnly(false);
        setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        setCursor(Qt::IBeamCursor);
        setToolTip(QString());
        repolish();
        setFocus(Qt::MouseFocusReason);
        lineEdit()->selectAll();
    }

protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }          // never changes the value; the panel scrolls instead
    void stepBy(int n) override { if (!locked_) QDoubleSpinBox::stepBy(n); }
    void keyPressEvent(QKeyEvent* e) override {
        if (locked_) { e->ignore(); return; }
        if (e->key() == Qt::Key_Escape) { lock(); clearFocus(); return; }
        QDoubleSpinBox::keyPressEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        QDoubleSpinBox::focusOutEvent(e);
        if (!locked_) lock();
    }
    bool eventFilter(QObject* o, QEvent* e) override {
        if (locked_ && e->type() == QEvent::MouseButtonPress && (o == lineEdit() || o == this)) { unlock(); return true; }
        if (e->type() == QEvent::Wheel) { e->ignore(); return true; }
        return QDoubleSpinBox::eventFilter(o, e);
    }

private:
    void repolish() { setProperty("locked", locked_); style()->unpolish(this); style()->polish(this); update(); }
    bool locked_ = true;
};

// Slider that ignores the mouse wheel (dragging / clicking the groove still works).
class NoWheelSlider : public QSlider {
public:
    using QSlider::QSlider;
    explicit NoWheelSlider(Qt::Orientation o, QWidget* parent = nullptr) : QSlider(o, parent) { setFocusPolicy(Qt::ClickFocus); }

protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};
