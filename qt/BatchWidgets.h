// Queue table with drag-to-reorder (PRD 4.1 "拖拽排序").
#pragma once
#include <QDropEvent>
#include <QTreeWidget>

class QueueTree : public QTreeWidget {
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
signals:
    void reordered();

protected:
    void dropEvent(QDropEvent* e) override {
        QTreeWidget::dropEvent(e);
        emit reordered();
    }
};
