// Central canvas: draws the drawing (and in layout mode the page grid), zoom / pan / drag / box-select.
#pragma once
#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPolygonF>
#include <QPushButton>
#include <QSet>
#include <QVector>
#include <QWidget>
#include <memory>

// Immutable geometry for the canvas: polylines in uv space (mm from the drawing's top-left, v pointing down).
struct DrawingView {
    QVector<QPolygonF> polys;
    QVector<QRectF> boxes;
    QVector<QColor> colors;   // per polyline: colour the PLT assigns to its pen (invalid = none defined)
    double w = 0, h = 0;
    int segments = 0;
};

class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { Import, Layout };
    struct Grid {
        bool valid = false;
        double Wp = 0, Hp = 0;      // registration frame size = tile step
        double strip = 0;           // pattern printed outside the frame on each side (frames abut, sheets overlap by 2*strip)
        int nx = 0, ny = 0;
        bool singlePage = false;
        QSet<int> present;          // r * 10000 + c of pages that will be output
        bool allPresent = true;
    };

    explicit CanvasWidget(QWidget* parent = nullptr);

    void setView(std::shared_ptr<const DrawingView> v);
    void setMode(Mode m);
    void setGrid(const Grid& g);
    void setOffset(const QPointF& mm);
    QPointF offset() const { return off_; }
    void setSelection(bool has, const QRectF& uv);
    void setSelectTool(bool on);
    void setHighlightCell(int r, int c);
    void focusCell(int r, int c);
    void setTip(const QString& text, bool applyBtn, bool dismissBtn);
    // What the page will look like on paper: the chosen line colour (or the pens' own colours) and line width.
    void setLineStyle(bool useOriginal, const QColor& color, double widthMm) { useOrig_ = useOriginal; lineColor_ = color; lineMm_ = widthMm; update(); }
    void setEmptyHint(const QString& s) { emptyHint_ = s; update(); }
    void setGhost(bool on, const QPointF& offsetMm = {}) { ghostOn_ = on; ghost_ = offsetMm; update(); }   // dashed outline = suggested position

public slots:
    void fitWindow();
    void actualSize();

signals:
    void dragBegan();
    void offsetEdited(QPointF mm, bool final);
    void nudged(double dx, double dy);
    void selectionEdited(bool has, QRectF uv, bool final);
    void cursorMm(QPointF uv, bool inside);
    void tipApply();
    void tipDismissed();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF sceneRect() const;
    QRectF gridRect() const;
    QPointF toWorld(const QPointF& screen) const { return (screen - origin_) / scale_; }
    QPointF toScreen(const QPointF& world) const { return world * scale_ + origin_; }
    void zoomAt(const QPointF& screenPos, double factor);
    void refit();
    void layoutTip();

    std::shared_ptr<const DrawingView> view_;
    Mode mode_ = Import;
    Grid grid_;
    QPointF off_;                 // drawing offset inside the grid (layout mode), mm
    double scale_ = 1;            // px per mm
    QPointF origin_;              // screen position of world (0,0)
    bool autoFit_ = true;

    bool hasSel_ = false, selectTool_ = false, selecting_ = false;
    QRectF sel_;
    QPointF selAnchor_;

    enum Drag { None, Pan, Move } drag_ = None;
    QPointF dragStartScreen_, dragStartOrigin_, dragStartOff_;
    bool space_ = false;
    int hiR_ = -1, hiC_ = -1;
    QString emptyHint_;
    bool useOrig_ = true;
    QColor lineColor_ = Qt::black;
    double lineMm_ = 0.25;
    bool ghostOn_ = false;
    QPointF ghost_;

    QFrame* tip_ = nullptr;
    QLabel* tipLabel_ = nullptr;
    QPushButton *tipApply_ = nullptr, *tipClose_ = nullptr;
};
