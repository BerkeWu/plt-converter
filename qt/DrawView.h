// Vector preview of one PDF page (strokes / fills / text from wm::drawPage), with the parts a removal would take away
// highlighted, and an optional rubber-band box for "框选图形".
#pragma once
#include <QWidget>
#include <memory>
#include <vector>

#include "wm/wm.h"

class DrawView : public QWidget {
    Q_OBJECT
public:
    enum Mode { Original, Removed };
    explicit DrawView(QWidget* parent = nullptr);
    // before / after drawings of the same page; items of `before` that have no counterpart in `after` get highlighted
    void setDrawings(std::shared_ptr<const wm::PageDrawing> before, std::shared_ptr<const wm::PageDrawing> after);
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    void setHighlight(bool on);
    void setSelectEnabled(bool on);
    void setBox(const QRectF& pagePts);            // show a box (page points, top-left origin); empty = hide
    int removedCount() const { return removedCount_; }
    void clear();
    QSize sizeHint() const override { return {520, 600}; }
signals:
    void boxed(double x0, double y0, double x1, double y1);   // user dragged a box (page points, top-left origin)
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF pageRect() const;
    QPointF toPage(const QPointF& px) const;
    void paintItems(QPainter&, const wm::PageDrawing&, const QRectF& r, double sc, const std::vector<char>* only, bool hl);
    std::shared_ptr<const wm::PageDrawing> before_, after_;
    std::vector<char> removed_;                      // per item of before_
    int removedCount_ = 0;
    Mode mode_ = Original;
    bool highlight_ = true, select_ = false, dragging_ = false;
    QPointF a_, b_;
    QRectF box_;
};
