// Batch conversion (PRD module 5): queue, common parameters, automatic layout, per-file preview, batch export.
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#include "BatchWidgets.h"
#include "MainWindow.h"
#include "Widgets.h"

namespace {

QLabel* sectionLabel(const QString& t) {
    auto* l = new QLabel(t);
    l->setProperty("lb", true);
    return l;
}
QLabel* noteLabel(const QString& t = {}) {
    auto* l = new QLabel(t);
    l->setProperty("note", true);
    l->setWordWrap(true);
    return l;
}
QPushButton* primaryButton(const QString& t) {
    auto* b = new QPushButton(t);
    b->setProperty("pri", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QPushButton* plainButton(const QString& t) {
    auto* b = new QPushButton(t);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QWidget* hbox(std::initializer_list<QWidget*> ws, int spacing = 6) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(spacing);
    for (auto* x : ws) l->addWidget(x, 1);
    return w;
}

// The layout parameters a template carries (everything the user can set per file except position / import-stage edits).
bool layoutEqual(const State& a, const State& b) {
    return a.paper == b.paper && a.margin == b.margin && a.overlap == b.overlap && a.singlePage == b.singlePage &&
           a.pageNumbers == b.pageNumbers && a.calibBox == b.calibBox && a.alignMarks == b.alignMarks && a.overview == b.overview &&
           a.keepBlank == b.keepBlank && a.calibCm == b.calibCm && a.lineWidth == b.lineWidth && a.origColor == b.origColor &&
           a.color == b.color && a.alignColor == b.alignColor && a.autoOrient == b.autoOrient;
}
void copyLayout(State& dst, const State& src) {
    dst.paper = src.paper; dst.margin = src.margin; dst.overlap = src.overlap; dst.singlePage = src.singlePage;
    dst.pageNumbers = src.pageNumbers; dst.calibBox = src.calibBox; dst.alignMarks = src.alignMarks; dst.overview = src.overview;
    dst.keepBlank = src.keepBlank; dst.calibCm = src.calibCm; dst.lineWidth = src.lineWidth; dst.origColor = src.origColor;
    dst.color = src.color; dst.alignColor = src.alignColor; dst.autoOrient = src.autoOrient; dst.landscape = src.landscape;
}

QString hintText(const layout::ShiftHint& h) {
    QString dir;
    if (h.dx < 0) dir += QObject::tr("向左移动 %1 mm").arg(-h.dx);
    else if (h.dx > 0) dir += QObject::tr("向右移动 %1 mm").arg(h.dx);
    if (h.dy < 0) dir += (dir.isEmpty() ? QString() : QStringLiteral("、")) + QObject::tr("向上移动 %1 mm").arg(-h.dy);
    else if (h.dy > 0) dir += (dir.isEmpty() ? QString() : QStringLiteral("、")) + QObject::tr("向下移动 %1 mm").arg(h.dy);
    return QObject::tr("%1，可由 %2 页减至 %3 页。").arg(dir).arg(h.from).arg(h.to);
}

QString orientText(const State& s) { return s.singlePage ? QObject::tr("单页") : (s.landscape ? QObject::tr("横向") : QObject::tr("竖向")); }

}  // namespace

// ============================================================================ widgets

QWidget* MainWindow::buildBatchToggleBar() {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(12, 8, 12, 4);
    bu_.tabLayout = plainButton(tr("排版"));
    bu_.tabShape = plainButton(tr("图形"));
    bu_.tabShape->setToolTip(tr("旋转 / 镜像 / 忽略纸张边框 / 框选区域"));
    for (auto* b : {bu_.tabLayout, bu_.tabShape}) { b->setCheckable(true); b->setProperty("seg", true); l->addWidget(b); }
    auto* g = new QButtonGroup(w);
    g->setExclusive(true);
    g->addButton(bu_.tabLayout, 0);
    g->addButton(bu_.tabShape, 1);
    bu_.tabLayout->setChecked(true);
    connect(g, &QButtonGroup::idClicked, this, [this](int id) { setBatchTab(id == 1); });
    w->hide();
    bu_.toggleBar = w;
    return w;
}

QWidget* MainWindow::buildBatchQueueBox() {
    auto* w = new QWidget;
    w->setObjectName("queueBox");
    w->setFixedWidth(250);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(8);
    auto* title = new QLabel;
    title->setObjectName("queueTitle");
    title->setProperty("lb", true);
    l->addWidget(title);
    bu_.filter = new QComboBox;
    bu_.filter->addItems({tr("全部文件"), tr("仅有优化提示的"), tr("按页数从多到少")});
    l->addWidget(bu_.filter);
    bu_.queueList = new QListWidget;
    bu_.queueList->setIconSize(QSize(64, 48));
    bu_.queueList->setSpacing(2);
    bu_.queueList->setFrameShape(QFrame::NoFrame);
    l->addWidget(bu_.queueList, 1);
    connect(bu_.filter, &QComboBox::currentIndexChanged, this, [this](int i) { viewFilter_ = i; refreshQueueList(); });
    connect(bu_.queueList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (refreshingList_ || row < 0) return;
        const QVector<int> order = viewOrder();
        if (row < order.size() && order[row] != cur_) activateItem(order[row]);
    });
    w->hide();
    bu_.queueBox = w;
    return w;
}

QWidget* MainWindow::buildBatchRightHost() {
    auto* host = new QWidget;
    host->setFixedWidth(300);
    auto* l = new QVBoxLayout(host);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    // --- info card (design: 自动方案 / 当前方案 / 优化提示) ---
    auto* info = new QWidget;
    auto* il = new QVBoxLayout(info);
    il->setContentsMargins(10, 10, 10, 6);
    il->setSpacing(6);
    bu_.prev = plainButton(tr("◀ 上一个"));
    bu_.next = plainButton(tr("下一个 ▶"));
    bu_.posLbl = new QLabel;
    bu_.posLbl->setAlignment(Qt::AlignCenter);
    auto* nav = new QWidget;
    auto* nl = new QHBoxLayout(nav);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->addWidget(bu_.prev);
    nl->addWidget(bu_.posLbl, 1);
    nl->addWidget(bu_.next);
    il->addWidget(nav);
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(10, 8, 10, 8);
    cl->setSpacing(4);
    bu_.compare = new QLabel;
    bu_.compare->setTextFormat(Qt::RichText);
    bu_.compare->setWordWrap(true);
    cl->addWidget(bu_.compare);
    bu_.hintLbl = new QLabel;
    bu_.hintLbl->setWordWrap(true);
    bu_.hintLbl->setProperty("note", true);
    cl->addWidget(bu_.hintLbl);
    bu_.applyHint = plainButton(tr("应用优化位置"));
    cl->addWidget(bu_.applyHint);
    il->addWidget(card);
    bu_.overrideNote = noteLabel();
    il->addWidget(bu_.overrideNote);
    bu_.infoBox = info;
    l->addWidget(info);
    // (index 1 is filled with sideHolder_ by batchChrome)
    // --- actions ---
    auto* act = new QWidget;
    auto* al = new QVBoxLayout(act);
    al->setContentsMargins(10, 6, 10, 10);
    al->setSpacing(6);
    bu_.confirm = primaryButton(tr("确认并下一个"));
    bu_.restoreAuto = plainButton(tr("还原为自动方案"));
    bu_.restoreTmpl = plainButton(tr("恢复通用设置"));
    bu_.applyFollow = plainButton(tr("应用到后续所有文件"));
    bu_.applyFollow->setToolTip(tr("把当前文件的纸张、方向、页边距、重叠区复制给排在后面的所有文件（不复制位置）"));
    al->addWidget(bu_.confirm);
    al->addWidget(hbox({bu_.restoreAuto, bu_.restoreTmpl}));
    al->addWidget(bu_.applyFollow);
    bu_.actionBox = act;
    l->addWidget(act);
    host->hide();
    bu_.rightHost = host;
    connect(bu_.prev, &QPushButton::clicked, this, [this] { stepItem(-1); });
    connect(bu_.next, &QPushButton::clicked, this, [this] { stepItem(+1); });
    connect(bu_.applyHint, &QPushButton::clicked, this, [this] {
        if (!last_.hint.found) return;
        const auto h = last_.hint;
        edit([h](State& s) { s.offX = std::max(0.0, s.offX + h.dx); s.offY = std::max(0.0, s.offY + h.dy); });
    });
    connect(bu_.confirm, &QPushButton::clicked, this, [this] { confirmCurrent(true); });
    connect(bu_.restoreAuto, &QPushButton::clicked, this, &MainWindow::restoreAutoPlan);
    connect(bu_.restoreTmpl, &QPushButton::clicked, this, &MainWindow::restoreTemplateParams);
    connect(bu_.applyFollow, &QPushButton::clicked, this, &MainWindow::applyToFollowing);
    return host;
}

QWidget* MainWindow::buildBatchBottomBar() {
    auto* bar = new QWidget;
    bar->setObjectName("bottomBar");
    bar->setFixedHeight(48);
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(16, 0, 16, 0);
    bu_.sumLbl = new QLabel;
    bu_.sumLbl->setTextFormat(Qt::RichText);
    bu_.confirmAll = plainButton(tr("全部按自动方案确认"));
    bu_.exportBtn = primaryButton(tr("批量导出"));
    l->addWidget(bu_.sumLbl, 1);
    l->addWidget(bu_.confirmAll);
    l->addWidget(bu_.exportBtn);
    bar->hide();
    bu_.bottomBar = bar;
    connect(bu_.confirmAll, &QPushButton::clicked, this, &MainWindow::confirmAllAuto);
    connect(bu_.exportBtn, &QPushButton::clicked, this, [this] { goBatchStep(4); });
    return bar;
}

QWidget* MainWindow::buildBatchTablePage() {
    auto* page = new QWidget;
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);
    bu_.tableNote = new QLabel;
    bu_.tableNote->setProperty("lb", true);
    bu_.tableNote->setWordWrap(true);
    l->addWidget(bu_.tableNote);
    bu_.table = new QueueTree;
    bu_.table->setColumnCount(8);
    bu_.table->setHeaderLabels({tr("#"), tr("缩略图"), tr("文件名"), tr("尺寸 (mm)"), tr("线条数"), tr("页数"), tr("方向"), tr("状态")});
    bu_.table->setRootIsDecorated(false);
    bu_.table->setAlternatingRowColors(true);
    bu_.table->setIconSize(QSize(64, 48));
    bu_.table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    bu_.table->setDragDropMode(QAbstractItemView::InternalMove);
    bu_.table->setDefaultDropAction(Qt::MoveAction);
    bu_.table->setUniformRowHeights(true);
    auto* hd = bu_.table->header();
    hd->setStretchLastSection(false);
    hd->setSectionResizeMode(0, QHeaderView::Fixed);
    hd->setSectionResizeMode(1, QHeaderView::Fixed);
    hd->setSectionResizeMode(2, QHeaderView::Stretch);
    for (int c : {3, 4, 5, 6}) hd->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    hd->setSectionResizeMode(7, QHeaderView::Interactive);
    bu_.table->setColumnWidth(0, 36);
    bu_.table->setColumnWidth(1, 80);
    bu_.table->setColumnWidth(7, 230);
    l->addWidget(bu_.table, 1);
    bu_.scanNote = noteLabel();
    l->addWidget(bu_.scanNote);
    connect(bu_.table, &QueueTree::reordered, this, [this] {
        QVector<std::shared_ptr<BatchItem>> n;
        for (int r = 0; r < bu_.table->topLevelItemCount(); ++r) {
            const int id = bu_.table->topLevelItem(r)->data(0, Qt::UserRole).toInt();
            for (auto& it : items_) if (it->id == id) { n.push_back(it); break; }
        }
        if (n.size() == items_.size()) items_ = n;
        refreshTable();
    });
    return page;
}

QWidget* MainWindow::buildBatchAddPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(10);
    l->addWidget(sectionLabel(tr("添加文件")));
    bu_.addFiles = primaryButton(tr("添加文件…"));
    bu_.addFolder = plainButton(tr("添加文件夹…"));
    bu_.subfolders = new QCheckBox(tr("包含子文件夹"));
    l->addWidget(bu_.addFiles);
    l->addWidget(bu_.addFolder);
    l->addWidget(bu_.subfolders);
    l->addWidget(noteLabel(tr("也可以把多个 .plt 文件或整个文件夹拖进窗口。单个队列最多 20 个文件；非 .plt 文件会被自动忽略。")));
    bu_.scanBar = new QProgressBar;
    bu_.scanBar->setTextVisible(false);
    bu_.scanBar->setFixedHeight(6);
    bu_.scanBar->hide();
    l->addWidget(bu_.scanBar);
    l->addWidget(sectionLabel(tr("队列操作")));
    bu_.selAll = plainButton(tr("全选"));
    bu_.invSel = plainButton(tr("反选"));
    bu_.removeSel = plainButton(tr("删除选中"));
    bu_.clearAll = plainButton(tr("清空队列"));
    bu_.moveUp = plainButton(tr("上移"));
    bu_.moveDown = plainButton(tr("下移"));
    l->addWidget(hbox({bu_.selAll, bu_.invSel}));
    l->addWidget(hbox({bu_.moveUp, bu_.moveDown}));
    l->addWidget(hbox({bu_.removeSel, bu_.clearAll}));
    l->addWidget(noteLabel(tr("也可以在表格里直接拖拽行来排序。队列顺序就是预览与导出的顺序。")));
    l->addStretch(1);
    bu_.toTemplate = primaryButton(tr("下一步：通用参数"));
    l->addWidget(bu_.toTemplate);
    connect(bu_.addFiles, &QPushButton::clicked, this, [this] { batchAddDialog(false); });
    connect(bu_.addFolder, &QPushButton::clicked, this, [this] { batchAddDialog(true); });
    connect(bu_.toTemplate, &QPushButton::clicked, this, [this] { goBatchStep(1); });
    connect(bu_.selAll, &QPushButton::clicked, this, [this] { bu_.table->selectAll(); });
    connect(bu_.invSel, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < bu_.table->topLevelItemCount(); ++r) { auto* it = bu_.table->topLevelItem(r); it->setSelected(!it->isSelected()); }
    });
    auto selectedIds = [this] {
        QSet<int> ids;
        for (auto* it : bu_.table->selectedItems()) ids.insert(it->data(0, Qt::UserRole).toInt());
        return ids;
    };
    connect(bu_.removeSel, &QPushButton::clicked, this, [this, selectedIds] {
        const QSet<int> ids = selectedIds();
        if (ids.isEmpty()) return;
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const std::shared_ptr<BatchItem>& i) { return ids.contains(i->id); }), items_.end());
        refreshTable(); refreshBatchSummary(); syncUi(); saveSession();
    });
    connect(bu_.clearAll, &QPushButton::clicked, this, [this] {
        if (items_.isEmpty()) return;
        if (!ask(tr("清空队列"), tr("确定清空队列中的 %1 个文件吗？").arg(items_.size()), true)) return;
        items_.clear();
        clearSession();
        refreshTable(); refreshBatchSummary(); syncUi();
    });
    auto move = [this, selectedIds](int d) {
        const QSet<int> ids = selectedIds();
        if (ids.isEmpty()) return;
        if (d < 0) { for (int i = 1; i < items_.size(); ++i) if (ids.contains(items_[i]->id) && !ids.contains(items_[i - 1]->id)) std::swap(items_[i], items_[i - 1]); }
        else { for (int i = items_.size() - 2; i >= 0; --i) if (ids.contains(items_[i]->id) && !ids.contains(items_[i + 1]->id)) std::swap(items_[i], items_[i + 1]); }
        refreshTable();
        for (int r = 0; r < bu_.table->topLevelItemCount(); ++r) bu_.table->topLevelItem(r)->setSelected(ids.contains(bu_.table->topLevelItem(r)->data(0, Qt::UserRole).toInt()));
    };
    connect(bu_.moveUp, &QPushButton::clicked, this, [move] { move(-1); });
    connect(bu_.moveDown, &QPushButton::clicked, this, [move] { move(+1); });
    return w;
}

QWidget* MainWindow::buildBatchAutoPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(10);
    l->addWidget(sectionLabel(tr("自动排版")));
    l->addWidget(noteLabel(tr("对每个文件分别计算横向 / 竖向所需页数，自动选择最省纸的方向；图形位置按通用参数中的初始策略放置。不会自动旋转图形（纸样有布纹方向）。")));
    bu_.autoBar = new QProgressBar;
    bu_.autoBar->setTextVisible(true);
    l->addWidget(bu_.autoBar);
    bu_.autoInfo = new QLabel;
    bu_.autoInfo->setWordWrap(true);
    bu_.autoInfo->setTextFormat(Qt::RichText);
    l->addWidget(bu_.autoInfo);
    bu_.autoRun = plainButton(tr("重新计算"));
    l->addWidget(bu_.autoRun);
    l->addStretch(1);
    bu_.autoNext = primaryButton(tr("下一步：逐一预览"));
    auto* back = plainButton(tr("上一步"));
    l->addWidget(hbox({back, bu_.autoNext}));
    connect(back, &QPushButton::clicked, this, [this] { goBatchStep(1); });
    connect(bu_.autoRun, &QPushButton::clicked, this, [this] { startAuto(true); });
    connect(bu_.autoNext, &QPushButton::clicked, this, [this] { goBatchStep(3); });
    return w;
}

QWidget* MainWindow::buildBatchExportPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(10);
    l->addWidget(sectionLabel(tr("输出位置")));
    bu_.outSource = new QRadioButton(tr("每个文件所在的文件夹"));
    bu_.outCustom = new QRadioButton(tr("指定文件夹"));
    bu_.outChoose = plainButton(tr("选择文件夹…"));
    bu_.outDirShow = noteLabel();
    l->addWidget(bu_.outSource);
    l->addWidget(bu_.outCustom);
    l->addWidget(bu_.outChoose);
    l->addWidget(bu_.outDirShow);
    l->addWidget(noteLabel(tr("每个文件输出一个 PDF，命名：原名_纸张方向.pdf；重名自动加 _2、_3，不会覆盖已有文件。")));
    l->addWidget(sectionLabel(tr("并发数")));
    bu_.concurrency = new QSpinBox;
    bu_.concurrency->setRange(1, std::max(1, std::min(4, QThread::idealThreadCount() - 1)));
    bu_.concurrency->setToolTip(tr("同时导出的文件数（1–4，不超过 CPU 核数 − 1）"));
    l->addWidget(bu_.concurrency);
    l->addWidget(sectionLabel(tr("导出摘要")));
    bu_.expSummary = new QLabel;
    bu_.expSummary->setWordWrap(true);
    bu_.expSummary->setTextFormat(Qt::RichText);
    l->addWidget(bu_.expSummary);
    bu_.expBar = new QProgressBar;
    bu_.expBar->setRange(0, 1000);
    bu_.expBar->setTextVisible(true);
    l->addWidget(bu_.expBar);
    bu_.expTime = noteLabel();
    l->addWidget(bu_.expTime);
    bu_.expStart = primaryButton(tr("开始导出"));
    bu_.expPause = plainButton(tr("暂停"));
    bu_.expCancel = plainButton(tr("取消"));
    l->addWidget(bu_.expStart);
    l->addWidget(hbox({bu_.expPause, bu_.expCancel}));
    bu_.expOpen = plainButton(tr("打开输出文件夹"));
    bu_.expCsv = plainButton(tr("导出报告（CSV）"));
    bu_.expRetry = plainButton(tr("仅重试失败项"));
    l->addWidget(bu_.expOpen);
    l->addWidget(bu_.expCsv);
    l->addWidget(bu_.expRetry);
    l->addWidget(noteLabel(tr("PDF 为 1:1 实际尺寸，打印时请选择【实际大小 / 100%】，切勿选择【适应页面】。")));
    l->addStretch(1);
    bu_.expBack = plainButton(tr("返回逐一预览"));
    l->addWidget(bu_.expBack);
    connect(bu_.outSource, &QRadioButton::toggled, this, [this](bool on) { if (on) { opts_.outDir.clear(); syncOptsUi(); } });
    connect(bu_.outCustom, &QRadioButton::toggled, this, [this](bool on) {
        if (on && opts_.outDir.isEmpty()) {
            const QString d = QFileDialog::getExistingDirectory(this, tr("选择输出文件夹"));
            if (d.isEmpty()) { bu_.outSource->setChecked(true); return; }
            opts_.outDir = d;
        }
        syncOptsUi();
    });
    connect(bu_.outChoose, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("选择输出文件夹"), opts_.outDir);
        if (!d.isEmpty()) { opts_.outDir = d; bu_.outCustom->setChecked(true); syncOptsUi(); }
    });
    connect(bu_.concurrency, &QSpinBox::valueChanged, this, [this](int v) { opts_.concurrency = v; });
    connect(bu_.expStart, &QPushButton::clicked, this, [this] { batchStartExport(false); });
    connect(bu_.expPause, &QPushButton::clicked, this, [this] { batchPause(!(ctl_ && ctl_->paused.load())); });
    connect(bu_.expCancel, &QPushButton::clicked, this, [this] { batchCancel(); });
    connect(bu_.expOpen, &QPushButton::clicked, this, [this] {
        QString dir = opts_.outDir;
        if (dir.isEmpty()) for (auto& it : items_) if (it->status == ItemStatus::Done) { dir = QFileInfo(it->outPath).absolutePath(); break; }
        if (dir.isEmpty() && !items_.isEmpty()) dir = QFileInfo(items_.first()->path).absolutePath();
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(bu_.expCsv, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("导出报告"), QDir::home().filePath(tr("批量导出报告.csv")), tr("CSV 文件 (*.csv)"));
        if (f.isEmpty()) return;
        QSaveFile out(f);
        if (out.open(QIODevice::WriteOnly)) { out.write(batchReportCsv().toUtf8()); out.commit(); }
        else notify(tr("导出报告"), tr("无法写入该文件。"));
    });
    connect(bu_.expRetry, &QPushButton::clicked, this, [this] { batchStartExport(true); });
    connect(bu_.expBack, &QPushButton::clicked, this, [this] { goBatchStep(3); });
    return w;
}

QWidget* MainWindow::buildBatchOptsBox() {
    auto* box = new QWidget;
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(0, 6, 0, 0);
    l->setSpacing(10);
    l->addWidget(sectionLabel(tr("批量选项（应用于全部文件）")));
    bu_.calibScope = new QComboBox;
    bu_.calibScope->addItems({tr("校准框：每个文件第一页"), tr("校准框：仅批量首个文件")});
    l->addWidget(bu_.calibScope);
    bu_.fileTag = new ToggleSwitch;
    auto* fl = new QLabel(tr("页码旁附加文件名（≤20 字）"));
    fl->setProperty("lb", true);
    fl->setWordWrap(true);
    auto* frow = new QWidget;
    auto* frl = new QHBoxLayout(frow);
    frl->setContentsMargins(0, 0, 0, 0);
    frl->addWidget(fl, 1);
    frl->addWidget(bu_.fileTag);
    l->addWidget(frow);
    bu_.startPos = new QComboBox;
    bu_.startPos->addItems({tr("初始位置：左上对齐"), tr("初始位置：自动优化位置")});
    l->addWidget(bu_.startPos);
    l->addWidget(sectionLabel(tr("参数模板")));
    bu_.templates = new QComboBox;
    bu_.tplSave = plainButton(tr("保存当前参数…"));
    bu_.tplDelete = plainButton(tr("删除模板"));
    l->addWidget(bu_.templates);
    l->addWidget(hbox({bu_.tplSave, bu_.tplDelete}));
    box->hide();
    bu_.optsBox = box;
    connect(bu_.calibScope, &QComboBox::activated, this, [this](int i) { opts_.calibFirstOnly = i == 1; });
    connect(bu_.fileTag, &QAbstractButton::toggled, this, [this](bool on) { opts_.fileTag = on; });
    connect(bu_.startPos, &QComboBox::activated, this, [this](int i) { opts_.autoPosition = i == 1; });
    connect(bu_.templates, &QComboBox::activated, this, [this](int i) {
        if (i <= 0) return;
        const QString name = bu_.templates->itemText(i);
        if (!namedTemplates_.contains(name)) return;
        const State t = namedTemplates_[name];
        edit([&](State& s) { copyLayout(s, t); });
    });
    connect(bu_.tplSave, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("保存参数模板"), tr("模板名称（如「A3 图文店标准」）："), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        namedTemplates_[name] = s_;
        saveTemplates();
        loadTemplates();
        bu_.templates->setCurrentText(name);
    });
    connect(bu_.tplDelete, &QPushButton::clicked, this, [this] {
        const QString name = bu_.templates->currentText();
        if (!namedTemplates_.contains(name)) return;
        namedTemplates_.remove(name);
        saveTemplates();
        loadTemplates();
    });
    return box;
}

// ============================================================================ helpers

bool MainWindow::ask(const QString& title, const QString& text, bool def) {
    if (silent_) return def;
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
}

void MainWindow::notify(const QString& title, const QString& text) {
    if (silent_) return;
    QMessageBox::information(this, title, text);
}

layout::Params MainWindow::currentParams() const {
    if (batch_ && cur_ >= 0 && cur_ < items_.size()) return batchParams(s_, opts_, cur_, items_[cur_]->path);
    layout::Params p = toParams(s_);
    if (s_.fileTag && !path_.isEmpty()) p.fileTag = fileTagFor(path_).toUtf8().toStdString();
    return p;
}

EditCtx MainWindow::grabCtx() const {
    EditCtx c;
    c.s = s_; c.undo = undo_; c.redo = redo_; c.lastKey = lastKey_;
    c.raw = raw_; c.full = full_; c.work = work_; c.fullView = fullView_; c.workView = workView_;
    c.path = path_; c.fileSize = fileSize_; c.hasFrame = hasFrame_; c.frameRemoved = frameRemoved_; c.notices = notices_;
    c.meta = meta_; c.thumbs = thumbs_; c.last = last_; c.havePlan = havePlan_; c.dismissLarger = dismissLarger_; c.step = step_;
    return c;
}

void MainWindow::putCtx(const EditCtx& c) {
    s_ = c.s; undo_ = c.undo; redo_ = c.redo; lastKey_ = c.lastKey;
    raw_ = c.raw; full_ = c.full; work_ = c.work; fullView_ = c.fullView; workView_ = c.workView;
    path_ = c.path; fileSize_ = c.fileSize; hasFrame_ = c.hasFrame; frameRemoved_ = c.frameRemoved; notices_ = c.notices;
    meta_ = c.meta; thumbs_ = c.thumbs; last_ = c.last; havePlan_ = c.havePlan; dismissLarger_ = c.dismissLarger; step_ = c.step;
    ++gen_;  // results of plans started for the previous file are stale now
}

void MainWindow::clearCtx() {
    EditCtx e;
    e.s = s_;
    e.step = step_;
    putCtx(e);
    canvas_->setGhost(false);
    canvas_->setView(nullptr);
    updateGridLocal();
    rebuildThumbLists();
}

// ============================================================================ mode / steps

void MainWindow::setBatchMode(bool on) {
    if (on == batch_ || parsing_) return;
    if (!on && exporting_) { notify(tr("提示"), tr("批量导出进行中，请先等待完成或取消。")); return; }
    if (on) {
        singleCtx_ = grabCtx();
        batch_ = true;
        s_ = tmpl_;
        cur_ = -1;
        clearCtx();
        const int target = items_.isEmpty() ? 0 : std::max(0, std::min(bstep_, maxBatchStep()));
        bstep_ = -1;  // force a full transition
        goBatchStep(target);
    } else {
        commitBatchEditing();
        batch_ = false;
        cur_ = -1;
        putCtx(singleCtx_);
        singleCtx_ = EditCtx();
        batchChrome();
        goStep(step_);
    }
}

void MainWindow::convertToBatch() {
    if (!raw_) { setBatchMode(true); return; }
    const QString path = path_;
    const State st = s_;
    setBatchMode(true);
    State t = tmpl_;
    copyLayout(t, st);
    opts_.fileTag = st.fileTag;
    t.fileTag = false;
    tmpl_ = t;
    tmplBefore_ = t;
    s_ = t;
    addBatchFiles({path});
    if (!items_.isEmpty()) {
        BatchItem& it = *items_.last();
        it.state = st;      // keep this file's own edits (rotation, selection, position ...)
        it.restoredState = true;
        it.adjusted = false;
    }
    syncUi();
}

int MainWindow::maxBatchStep() const {
    int valid = 0;
    bool allAuto = true;
    for (const auto& it : items_) {
        if (!it->valid()) continue;
        ++valid;
        if (!it->haveAuto) allAuto = false;
    }
    if (valid == 0) return 0;
    return (allAuto && autoPending_ == 0 && scanPending_ == 0) ? 4 : 2;
}

void MainWindow::commitCurrentItem() {
    if (cur_ < 0 || cur_ >= items_.size()) return;
    BatchItem& it = *items_[cur_];
    it.state = s_;
    it.undo = undo_;
    it.redo = redo_;
}

void MainWindow::commitBatchEditing() {
    if (!batch_) return;
    if (bstep_ == 1) {
        const State nw = s_;
        if (!layoutEqual(nw, tmplBefore_)) {
            int kept = 0;
            applyTemplateToItems(tmplBefore_, nw, &kept);
            tmpl_ = nw;
            tmplBefore_ = nw;
            if (kept > 0) notify(tr("通用参数已更新"), tr("%1 个已单独调整的文件保持原参数，其余文件将按新的通用参数重新自动排版。\n之后可在「逐一预览」里对它们点「恢复通用设置」。").arg(kept));
        } else {
            tmpl_ = nw;
        }
    } else if (bstep_ == 3) {
        commitCurrentItem();
    }
}

void MainWindow::goBatchStep(int step) {
    if (!batch_) return;
    step = std::clamp(step, 0, 4);
    if (exporting_ && step != 4) { notify(tr("提示"), tr("批量导出进行中。")); return; }
    const int maxStep = maxBatchStep();
    if (step > maxStep) {
        if (maxStep == 0) { notify(tr("请先添加文件"), tr("队列里还没有可用的文件。")); return; }
        if (scanPending_ > 0 || autoPending_ > 0) { notify(tr("请稍候"), tr("后台仍在处理文件，完成后即可继续。")); return; }
        step = 2;  // results of the automatic layout are missing: compute them first
    }
    commitBatchEditing();
    const int prev = bstep_;
    if (prev == 3) resumeCur_ = cur_;
    bstep_ = step;
    if (prev == 3 && step != 3) {
        cur_ = -1;
        State keep = tmpl_;
        clearCtx();
        s_ = keep;
        releaseDocs();
    }
    canvas_->setGhost(false);
    switch (step) {
        case 0:
            step_ = 0;
            leftStack_->setCurrentIndex(3);
            centerStack_->setCurrentIndex(2);
            break;
        case 1:
            step_ = 1;
            s_ = tmpl_;
            tmplBefore_ = tmpl_;
            leftStack_->setCurrentIndex(1);
            centerStack_->setCurrentIndex(2);
            break;
        case 2:
            step_ = 0;
            leftStack_->setCurrentIndex(4);
            centerStack_->setCurrentIndex(2);
            startAuto(false);
            break;
        case 3: {
            step_ = 1;
            leftStack_->setCurrentIndex(1);
            centerStack_->setCurrentIndex(0);
            bu_.tabLayout->setChecked(true);
            break;
        }
        case 4:
            step_ = 0;
            leftStack_->setCurrentIndex(5);
            centerStack_->setCurrentIndex(2);
            break;
    }
    thumbStripBox_->setVisible(step == 3);
    batchChrome();
    refreshTable();
    refreshBatchSummary();
    syncOptsUi();
    if (step == 3) {
        refreshQueueList();
        const QVector<int> order = viewOrder();
        int target = -1;
        if (resumeCur_ >= 0 && resumeCur_ < items_.size() && items_[resumeCur_]->valid() && order.contains(resumeCur_)) target = resumeCur_;
        for (int i : order) { if (target >= 0) break; if (items_[i]->valid() && !items_[i]->confirmed) target = i; }
        for (int i : order) { if (target >= 0) break; if (items_[i]->valid()) target = i; }
        if (target >= 0) activateItem(target);
    }
    syncUi();
    updateTip();
}

void MainWindow::batchChrome() {
    const QStringList single = {tr("1 导入"), tr("2 排版设置"), tr("3 预览导出")};
    const QStringList batch = {tr("1 添加文件"), tr("2 通用参数"), tr("3 自动排版"), tr("4 逐一预览"), tr("5 批量导出")};
    for (int i = 0; i < 5; ++i) stepBtn_[i]->setText(batch_ ? batch[i] : (i < 3 ? single[i] : QString()));
    const bool b3 = batch_ && bstep_ == 3;
    bu_.queueBox->setVisible(b3);
    bu_.rightHost->setVisible(b3);
    bu_.leftHost->setVisible(!b3);
    bu_.toggleBar->setVisible(b3);
    bu_.bottomBar->setVisible(b3);
    // park the parameter column on the right of the canvas in step 4, on the left otherwise
    QWidget* host = b3 ? bu_.rightHost : bu_.leftHost;
    if (sideHolder_->parentWidget() != host) {
        QLayout* old = sideHolder_->parentWidget() ? sideHolder_->parentWidget()->layout() : nullptr;
        if (old) old->removeWidget(sideHolder_);
        auto* lay = qobject_cast<QVBoxLayout*>(host->layout());
        if (b3) lay->insertWidget(1, sideHolder_, 1); else lay->addWidget(sideHolder_);
        sideHolder_->show();
    }
    sideHolder_->setProperty("right", b3);
    sideHolder_->style()->unpolish(sideHolder_);
    sideHolder_->style()->polish(sideHolder_);
    // wording of the layout panel's buttons
    if (batch_) {
        bu_.layoutNext->setText(bstep_ == 1 ? tr("下一步：自动排版") : tr("去批量导出"));
        bu_.layoutNext->setVisible(bstep_ == 1 || b3);
        bu_.layoutBack->setVisible(bstep_ == 1);
    } else {
        bu_.layoutNext->setText(tr("下一步：预览导出"));
        bu_.layoutNext->setVisible(true);
        bu_.layoutBack->setVisible(true);
    }
    bu_.optsBox->setVisible(batch_ && bstep_ == 1);
    const QStringList notes = {
        tr("拖入或添加 .plt 文件 / 文件夹。表格里可拖拽排序；队列顺序即预览与导出顺序。"),
        tr("通用参数将应用到下列全部文件；单个文件之后仍可在「逐一预览」中单独覆盖。"),
        tr("自动排版结果：每个文件的方向、页数。"),
        QString(),
        tr("批量导出：每个文件独立输出一个 PDF。")};
    if (batch_) bu_.tableNote->setText(notes[std::clamp(bstep_, 0, 4)]);
}

void MainWindow::setBatchTab(bool shape) {
    if (!batch_ || bstep_ != 3) return;
    bu_.tabShape->setChecked(shape);
    bu_.tabLayout->setChecked(!shape);
    leftStack_->setCurrentIndex(shape ? 0 : 1);
    step_ = shape ? 0 : 1;
    if (raw_) { refreshCanvasView(); updateGridLocal(); }
    canvas_->setSelectTool(shape && selBtn_->isChecked());
    thumbStripBox_->setVisible(!shape);
    syncUi();
    updateTip();
}

// ============================================================================ table / lists

QString MainWindow::statusText(const BatchItem& it) const {
    QString t;
    switch (it.status) {
        case ItemStatus::Scanning: t = tr("⏳ 解析中…"); break;
        case ItemStatus::Parsed: t = it.error.isEmpty() ? tr("● 已解析") : tr("⚠ 无法自动排版：%1").arg(it.error); break;
        case ItemStatus::ToConfirm: t = tr("○ 待确认"); break;
        case ItemStatus::Confirmed: t = tr("✔ 已确认"); break;
        case ItemStatus::Adjusted: t = tr("✎ 已调整"); break;
        case ItemStatus::Exporting: t = it.progress > 0 ? tr("▶ 导出中 %1%").arg(it.progress) : tr("▶ 排队中…"); break;
        case ItemStatus::Done: t = tr("✔ 完成"); break;
        case ItemStatus::Failed: t = tr("✖ 失败：%1").arg(it.error); break;
        case ItemStatus::Skipped: t = tr("⤼ 已跳过"); break;
    }
    if (it.status == ItemStatus::ToConfirm && it.adjusted) t += tr("（已调整）");
    if (it.suspectDup) t += tr(" ⚠疑似重复");
    return t;
}

void MainWindow::refreshStatus(BatchItem& it) {
    switch (it.status) {
        case ItemStatus::Scanning: case ItemStatus::Failed: case ItemStatus::Skipped: case ItemStatus::Exporting: case ItemStatus::Done: return;
        default: break;
    }
    if (!it.haveAuto) { it.status = ItemStatus::Parsed; return; }
    it.status = it.confirmed ? (it.adjusted ? ItemStatus::Adjusted : ItemStatus::Confirmed) : ItemStatus::ToConfirm;
}

static QColor statusColor(ItemStatus s) {
    switch (s) {
        case ItemStatus::Failed: return QColor("#B42318");
        case ItemStatus::Done: case ItemStatus::Confirmed: return QColor("#0B7A70");
        case ItemStatus::Adjusted: return QColor("#A15C00");
        case ItemStatus::Exporting: return QColor("#2563EB");
        default: return QColor("#556666");
    }
}

void MainWindow::refreshTable() {
    if (!bu_.table) return;
    QSet<int> sel;
    for (auto* it : bu_.table->selectedItems()) sel.insert(it->data(0, Qt::UserRole).toInt());
    const int scroll = bu_.table->verticalScrollBar()->value();
    QSignalBlocker blk(bu_.table);
    bu_.table->clear();
    for (int i = 0; i < items_.size(); ++i) {
        const BatchItem& it = *items_[i];
        auto* row = new QTreeWidgetItem(bu_.table);
        row->setData(0, Qt::UserRole, it.id);
        row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
        row->setText(0, QString::number(i + 1));
        if (!it.thumb.isNull()) row->setIcon(1, QIcon(QPixmap::fromImage(it.thumb)));
        const bool over = it.haveAuto && bstep_ >= 3 && isOverridden(it, tmpl_);
        row->setText(2, QFileInfo(it.path).fileName() + (over ? QStringLiteral("  ✎") : QString()));
        row->setToolTip(2, QDir::toNativeSeparators(it.path) + (over ? tr("\n已覆盖通用参数") : QString()));
        const bool scanned = it.w > 0 || it.h > 0 || it.lines > 0;
        row->setText(3, scanned ? QStringLiteral("%1 × %2").arg(it.w, 0, 'f', 0).arg(it.h, 0, 'f', 0) : QStringLiteral("—"));
        row->setText(4, it.lines ? QString::number(it.lines) : QStringLiteral("—"));
        if (it.haveAuto && it.valid()) row->setText(5, QString::number(it.pages));
        else if (scanned && it.valid()) row->setText(5, QStringLiteral("≈%1").arg(estimatePages(it.w, it.h, bstep_ == 1 ? s_ : it.state)));
        else row->setText(5, QStringLiteral("—"));
        row->setText(6, it.haveAuto && it.valid() ? orientText(it.state) : QStringLiteral("—"));
        row->setText(7, statusText(it));
        row->setForeground(7, statusColor(it.status));
        row->setSizeHint(0, QSize(0, 54));
        row->setSelected(sel.contains(it.id));
    }
    bu_.table->verticalScrollBar()->setValue(scroll);
}

void MainWindow::updateRowStatus(int id) {
    for (int r = 0; r < bu_.table->topLevelItemCount(); ++r) {
        auto* row = bu_.table->topLevelItem(r);
        if (row->data(0, Qt::UserRole).toInt() != id) continue;
        for (const auto& it : items_) if (it->id == id) { row->setText(7, statusText(*it)); row->setForeground(7, statusColor(it->status)); }
    }
}

void MainWindow::refreshRow(int) {
    refreshTable();
    if (batch_ && bstep_ == 3) refreshQueueList();
}

void MainWindow::refreshQueueList() {
    if (!bu_.queueList) return;
    refreshingList_ = true;
    bu_.queueList->clear();
    if (auto* t = bu_.queueBox->findChild<QLabel*>("queueTitle")) t->setText(tr("文件队列（%1 / 上限 %2）").arg(items_.size()).arg(kBatchLimit));
    const QVector<int> order = viewOrder();
    int curRow = -1;
    for (int r = 0; r < order.size(); ++r) {
        const BatchItem& it = *items_[order[r]];
        QString line2 = it.valid() && it.haveAuto ? tr("%1×%2 · %3 · %4 页").arg(it.w, 0, 'f', 0).arg(it.h, 0, 'f', 0).arg(orientText(it.state)).arg(it.pages) : QString();
        auto* li = new QListWidgetItem(it.thumb.isNull() ? QIcon() : QIcon(QPixmap::fromImage(it.thumb)), QFileInfo(it.path).fileName() + "\n" + line2 + "\n" + statusText(it), bu_.queueList);
        li->setSizeHint(QSize(220, 66));
        li->setForeground(statusColor(it.status));
        if (order[r] == cur_) curRow = r;
    }
    if (curRow >= 0) bu_.queueList->setCurrentRow(curRow);
    refreshingList_ = false;
}

void MainWindow::refreshBatchSummary() {
    int total = items_.size(), confirmed = 0, toConfirm = 0, failed = 0, pages = 0, valid = 0;
    double area = 0;
    for (const auto& it : items_) {
        if (it->status == ItemStatus::Failed) { ++failed; continue; }
        if (!it->valid()) continue;
        ++valid;
        if (it->confirmed) ++confirmed; else ++toConfirm;
        const int p = it->haveAuto ? it->pages : estimatePages(it->w, it->h, it->state);
        pages += p;
        const layout::Paper pa = layout::paperByName(it->state.paper.toStdString());
        area += p * pa.w * pa.h / 1e6;
    }
    const int secs = int(std::ceil(pages * 0.15 / std::max(1, opts_.concurrency)));
    if (bu_.sumLbl) {
        bu_.sumLbl->setText(tr("共 <b>%1</b> 个文件　已确认 <b style='color:#0B7A70'>%2</b>　待确认 <b>%3</b>　<b style='color:#B42318'>失败 %4</b><br>"
                               "<span style='color:#556666'>预计总页数 %5 · 纸张约 %6 ㎡ · 预计导出约 %7 秒 · 每个文件导出一个 PDF · 命名：原名_纸张方向（重名自动 _2、_3）</span>")
                                .arg(total).arg(confirmed).arg(toConfirm).arg(failed).arg(pages).arg(area, 0, 'f', 2).arg(secs));
    }
    if (bu_.scanNote) {
        QString n = tr("共 %1 个文件（上限 %2）").arg(total).arg(kBatchLimit);
        if (scanPending_ > 0) n += tr(" · 正在解析 %1 个…").arg(scanPending_);
        if (failed > 0) n += tr(" · %1 个失败（可删除或忽略，导出时会跳过）").arg(failed);
        bu_.scanNote->setText(n);
    }
    if (bu_.scanBar) {
        bu_.scanBar->setVisible(scanPending_ > 0);
        bu_.scanBar->setRange(0, 0);
    }
    if (bu_.toTemplate) bu_.toTemplate->setEnabled(valid > 0 && scanPending_ == 0);
    if (bu_.expSummary && !exporting_ && !haveExportResult_) {
        bu_.expSummary->setText(tr("共 %1 个文件 · %2 页<br>纸张约 %3 ㎡ · 预计约 %4 秒").arg(valid).arg(pages).arg(area, 0, 'f', 2).arg(secs));
    }
    if (bu_.exportBtn) bu_.exportBtn->setEnabled(valid > 0 && autoPending_ == 0);
}

QVector<int> MainWindow::viewOrder() const {
    QVector<int> v;
    for (int i = 0; i < items_.size(); ++i) {
        const BatchItem& it = *items_[i];
        if (viewFilter_ == 1 && !(it.valid() && it.hasHint)) continue;
        if (viewFilter_ == 2 && !it.valid()) continue;
        v.push_back(i);
    }
    if (viewFilter_ == 2) std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return items_[a]->pages > items_[b]->pages; });
    return v;
}

// ============================================================================ adding files / scanning

void MainWindow::batchAddDialog(bool folder) {
    if (exporting_) return;
    if (folder) {
        const QString d = QFileDialog::getExistingDirectory(this, tr("选择包含 PLT 文件的文件夹"));
        if (!d.isEmpty()) addBatchFiles({d});
    } else {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("选择 PLT 文件（可多选）"), items_.isEmpty() ? QString() : QFileInfo(items_.last()->path).absolutePath(), tr("PLT 文件 (*.plt *.PLT);;所有文件 (*)"));
        if (!f.isEmpty()) addBatchFiles(f);
    }
}

void MainWindow::addBatchFiles(const QStringList& in) {
    if (!batch_ || exporting_) return;
    QStringList files;
    int ignored = 0;
    const bool sub = bu_.subfolders && bu_.subfolders->isChecked();
    for (const QString& p : in) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            QStringList found;
            QDirIterator dit(p, QDir::Files | QDir::Readable, sub ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (dit.hasNext()) {
                const QString f = dit.next();
                if (f.endsWith(".plt", Qt::CaseInsensitive)) found << f; else ++ignored;
            }
            found.sort(Qt::CaseInsensitive);
            files += found;
        } else if (fi.isFile()) {
            if (p.endsWith(".plt", Qt::CaseInsensitive)) files << p; else ++ignored;
        }
    }
    QSet<QString> have;
    for (const auto& it : items_) have.insert(QFileInfo(it->path).canonicalFilePath().toLower());
    int dup = 0, over = 0, added = 0;
    for (const QString& f : files) {
        const QFileInfo fi(f);
        const QString key = (fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath()).toLower();
        if (have.contains(key)) { ++dup; continue; }
        if (items_.size() >= kBatchLimit) { ++over; continue; }
        auto it = std::make_shared<BatchItem>();
        it->id = nextId_++;
        it->path = fi.absoluteFilePath();
        it->size = fi.size();
        it->state = tmpl_;
        for (const auto& o : items_) if (QFileInfo(o->path).fileName() == fi.fileName() && o->size == it->size) { it->suspectDup = true; o->suspectDup = true; }
        items_.push_back(it);
        have.insert(key);
        ++added;
        startScan(it);
    }
    refreshTable();
    refreshBatchSummary();
    syncUi();
    QStringList msg;
    if (ignored) msg << tr("已忽略 %1 个非 .plt 文件").arg(ignored);
    if (dup) msg << tr("%1 个文件已在队列中（重复，未添加）").arg(dup);
    if (over) msg << tr("已达上限 %1 个，请分批添加；另有 %2 个文件未添加").arg(kBatchLimit).arg(over);
    if (!msg.isEmpty()) notify(tr("添加文件"), tr("已添加 %1 个文件。\n\n").arg(added) + msg.join('\n'));
    if (added == 0 && scanPending_ == 0) emit batchScanFinished();
}

void MainWindow::startScan(const std::shared_ptr<BatchItem>& it) {
    ++scanPending_;
    auto* w = new QFutureWatcher<ScanResult>(this);
    connect(w, &QFutureWatcher<ScanResult>::finished, this, [this, w] {
        const ScanResult r = w->result();
        w->deleteLater();
        onScanned(r);
    });
    w->setFuture(QtConcurrent::run(&scanPool_, scanFile, it->id, it->path));
}

void MainWindow::onScanned(const ScanResult& r) {
    --scanPending_;
    for (auto& it : items_) {
        if (it->id != r.id) continue;
        it->size = r.size ? r.size : it->size;
        if (!r.err.isEmpty()) {
            it->status = ItemStatus::Failed;
            it->error = r.err;
        } else {
            it->w = r.w; it->h = r.h; it->lines = r.lines; it->segments = r.segments; it->frameIdx = r.frameIdx; it->thumb = r.thumb;
            it->status = ItemStatus::Parsed;
            refreshStatus(*it);
        }
        break;
    }
    refreshTable();
    refreshBatchSummary();
    syncUi();
    if (scanPending_ == 0) emit batchScanFinished();
}

// ============================================================================ common template -> files

bool MainWindow::isOverridden(const BatchItem& it, const State& base) const { return !layoutEqual(it.state, base); }

State MainWindow::deriveState(const BatchItem& it, const State& base) const {
    State n = base;
    n.ignoreFrame = it.state.ignoreFrame; n.rot = it.state.rot; n.mirror = it.state.mirror;
    n.hasSel = it.state.hasSel; n.sel = it.state.sel; n.selClip = it.state.selClip;
    n.blanked = it.state.blanked;     // pages this file's user set to blank by hand
    n.offX = n.offY = 0;
    return n;
}

void MainWindow::applyTemplateToItems(const State& oldT, const State& newT, int* kept) {
    for (auto& it : items_) {
        if (!it->valid()) continue;
        if (it->adjusted || isOverridden(*it, oldT)) { if (kept) ++*kept; continue; }
        it->state = deriveState(*it, newT);
        it->haveAuto = false;
        it->confirmed = false;
        it->adjusted = false;
        it->status = ItemStatus::Parsed;
        it->error.clear();
    }
}

void MainWindow::batchEditTemplate(const std::function<void(State&)>& fn) {
    if (!batch_) return;
    if (bstep_ != 1) goBatchStep(1);
    edit(fn);
}

void MainWindow::startAuto(bool all) {
    QVector<int> targets;
    for (int i = 0; i < items_.size(); ++i) {
        BatchItem& it = *items_[i];
        if (!it.valid()) continue;
        const bool custom = it.adjusted || isOverridden(it, tmpl_);
        if (all && !custom) { it.state = deriveState(it, tmpl_); targets.push_back(i); }
        else if (!it.haveAuto) targets.push_back(i);
    }
    for (int i : targets) { items_[i]->haveAuto = false; items_[i]->error.clear(); items_[i]->status = ItemStatus::Parsed; }
    autoTotal_ = targets.size();
    autoPending_ += targets.size();
    if (bu_.autoBar) { bu_.autoBar->setRange(0, std::max(1, autoTotal_)); bu_.autoBar->setValue(0); bu_.autoBar->setFormat(tr("%v / %m 个文件")); }
    for (int i : targets) {
        const BatchItem& it = *items_[i];
        auto* w = new QFutureWatcher<AutoResult>(this);
        connect(w, &QFutureWatcher<AutoResult>::finished, this, [this, w] {
            const AutoResult r = w->result();
            w->deleteLater();
            onAutoResult(r);
        });
        w->setFuture(QtConcurrent::run(&autoPool_, computeAuto, it.id, it.path, it.state, opts_, i));
    }
    refreshTable();
    refreshBatchSummary();
    if (targets.isEmpty()) onAutoResult(AutoResult{-1, {}, {}, 0, false});  // nothing to compute: just refresh the panel
}

void MainWindow::onAutoResult(const AutoResult& r) {
    if (r.id >= 0) --autoPending_;
    autoPending_ = std::max(0, autoPending_);
    for (auto& it : items_) {
        if (it->id != r.id) continue;
        if (!r.err.isEmpty()) { it->error = r.err; it->haveAuto = false; it->status = ItemStatus::Parsed; break; }
        it->autoState = r.state;
        it->autoPages = r.pages;
        it->hasHint = r.hint;
        const bool replace = !it->adjusted;   // untouched by the user: the automatic plan becomes the current plan
        if (replace) { it->state = r.state; it->pages = r.pages; }
        it->haveAuto = true;
        it->adjusted = it->state != it->autoState;
        it->error.clear();
        refreshStatus(*it);
        break;
    }
    if (bu_.autoBar) bu_.autoBar->setValue(autoTotal_ - autoPending_);
    refreshTable();
    refreshBatchSummary();
    if (autoPending_ == 0) {
        int land = 0, port = 0, hints = 0, bad = 0, singles = 0;
        QString reason;
        for (const auto& it : items_) {
            if (!it->valid()) continue;
            if (!it->haveAuto) { ++bad; if (reason.isEmpty()) reason = it->error; continue; }
            if (it->state.singlePage) ++singles; else if (it->state.landscape) ++land; else ++port;
            if (it->hasHint) ++hints;
        }
        QString t = tr("已完成自动排版：横向 <b>%1</b> 个 · 竖向 <b>%2</b> 个").arg(land).arg(port);
        if (singles) t += tr(" · 单页 %1 个").arg(singles);
        if (hints) t += tr("<br>其中 <b>%1</b> 个文件有优化提示，可在逐一预览中处理。").arg(hints);
        if (bad) t += tr("<br><span style='color:#B42318'>%1 个文件无法自动排版：%2</span>").arg(bad).arg(reason.toHtmlEscaped());
        if (bu_.autoInfo) bu_.autoInfo->setText(t);
        if (bu_.autoNext) bu_.autoNext->setEnabled(bad == 0 && maxBatchStep() >= 4);
        syncUi();
        emit batchAutoFinished();
    } else if (bu_.autoInfo) {
        bu_.autoInfo->setText(tr("正在自动排版… 剩余 %1 个").arg(autoPending_));
        if (bu_.autoNext) bu_.autoNext->setEnabled(false);
    }
}

// ============================================================================ step 4: one file at a time

void MainWindow::ensureDoc(std::shared_ptr<BatchItem> it, std::function<void()> cb) {
    if (it->doc) { if (cb) cb(); return; }
    auto* w = new QFutureWatcher<ParseResult>(this);
    connect(w, &QFutureWatcher<ParseResult>::finished, this, [this, w, it, cb] {
        ParseResult r = w->result();
        w->deleteLater();
        if (r.doc) { it->doc = r.doc; it->frameIdx = r.frameIdx; }
        else { it->status = ItemStatus::Failed; it->error = r.err; it->exportFailed = false; }
        if (cb) cb();
    });
    w->setFuture(QtConcurrent::run(&loadPool_, parseJob, it->path));
}

void MainWindow::releaseDocs() {
    // PRD 4.6: full geometry only for the current file and the prefetched next one
    const QVector<int> order = viewOrder();
    const int pos = order.indexOf(cur_);
    const int next = (pos >= 0 && pos + 1 < order.size()) ? order[pos + 1] : -1;
    for (int i = 0; i < items_.size(); ++i)
        if (i != cur_ && i != next) items_[i]->doc.reset();
}

void MainWindow::activateItem(int idx) {
    if (!batch_ || idx < 0 || idx >= items_.size()) return;
    if (cur_ >= 0 && cur_ < items_.size() && cur_ != idx) commitCurrentItem();
    cur_ = idx;
    const std::shared_ptr<BatchItem> it = items_[idx];
    clearCtx();            // never show / edit the previous file while this one loads
    refreshQueueList();
    if (!it->valid()) {
        canvas_->setEmptyHint(tr("该文件无法使用：%1").arg(it->error));
        updateBatchInfo();
        return;
    }
    canvas_->setEmptyHint(tr("正在加载 %1 …").arg(QFileInfo(it->path).fileName()));
    ensureDoc(it, [this, idx] { finishActivate(idx); });
}

void MainWindow::finishActivate(int idx) {
    if (cur_ != idx || idx < 0 || idx >= items_.size()) return;
    BatchItem& it = *items_[idx];
    if (!it.doc) {
        clearCtx();
        canvas_->setEmptyHint(tr("无法加载：%1").arg(it.error));
        refreshQueueList();
        refreshTable();
        updateBatchInfo();
        return;
    }
    EditCtx c;
    c.s = it.state; c.undo = it.undo; c.redo = it.redo;
    c.raw = it.doc; c.path = it.path; c.fileSize = it.size; c.hasFrame = it.frameIdx >= 0;
    c.step = step_;
    putCtx(c);
    rebuildFull();
    rebuildWork();
    const plt::Document& d = *raw_;
    impInfo_->setText(tr("<b>%1</b><br>大小 %2<br>物理尺寸 %3 × %4 mm<br>线条 %5 条")
                          .arg(QFileInfo(it.path).fileName().toHtmlEscaped(), QString::number(it.size / 1024.0, 'f', 1) + " KB").arg(d.bounds.w(), 0, 'f', 1).arg(d.bounds.h(), 0, 'f', 1).arg(d.lines.size()));
    impNotes_->clear();
    impNotes_->hide();
    canvas_->setHighlightCell(-1, -1);
    refreshCanvasView();
    canvas_->fitWindow();
    updateGridLocal();
    syncUi();
    requestPlan();
    updateBatchInfo();
    refreshQueueList();
    const QVector<int> order = viewOrder();
    const int pos = order.indexOf(idx);
    if (pos >= 0 && pos + 1 < order.size() && items_[order[pos + 1]]->valid()) ensureDoc(items_[order[pos + 1]], nullptr);
    releaseDocs();
}

void MainWindow::onBatchEdited() {
    if (bstep_ == 1) {
        tmpl_ = s_;
        refreshTable();       // page estimates follow the template while it is being edited
        refreshBatchSummary();
        return;
    }
    if (bstep_ == 3 && cur_ >= 0 && cur_ < items_.size()) {
        BatchItem& it = *items_[cur_];
        it.state = s_;
        it.userEdited = true;
        it.adjusted = it.haveAuto && s_ != it.autoState;
        refreshStatus(it);
        updateBatchInfo();
        refreshRow(cur_);
        refreshBatchSummary();
    }
}

void MainWindow::updateBatchInfo() {
    if (!batch_ || bstep_ != 3) return;
    const QVector<int> order = viewOrder();
    const int pos = order.indexOf(cur_);
    bu_.posLbl->setText(pos >= 0 ? tr("%1 / %2").arg(pos + 1).arg(order.size()) : tr("— / %1").arg(order.size()));
    bu_.prev->setEnabled(pos > 0);
    bu_.next->setEnabled(pos >= 0 && pos + 1 < order.size());
    if (cur_ < 0 || cur_ >= items_.size() || !items_[cur_]->valid()) {
        bu_.compare->clear(); bu_.hintLbl->clear(); bu_.applyHint->hide(); bu_.confirm->setEnabled(false);
        bu_.restoreAuto->setEnabled(false); bu_.restoreTmpl->setEnabled(false); bu_.applyFollow->setEnabled(false);
        canvas_->setGhost(false);
        return;
    }
    const BatchItem& it = *items_[cur_];
    const int now = havePlan_ ? meta_.size() : it.pages;
    const bool worse = it.haveAuto && now > it.autoPages;
    bu_.compare->setText(tr("自动方案 <b>%1</b> 页 → 当前方案 <b style='color:%2'>%3</b> 页%4")
                             .arg(it.autoPages).arg(worse ? "#A15C00" : "#0B7A70").arg(now).arg(worse ? tr("　⚠ 比自动方案多 %1 页").arg(now - it.autoPages) : QString()));
    if (havePlan_ && last_.hint.found) {
        bu_.hintLbl->setText(hintText(last_.hint));
        bu_.applyHint->show();
        canvas_->setGhost(true, QPointF(std::max(0.0, s_.offX + last_.hint.dx), std::max(0.0, s_.offY + last_.hint.dy)));
    } else {
        bu_.hintLbl->setText(havePlan_ ? tr("当前位置已无明显优化空间。") : tr("正在计算…"));
        bu_.applyHint->hide();
        canvas_->setGhost(false);
    }
    const bool over = isOverridden(it, tmpl_);
    bu_.overrideNote->setText(over ? tr("✎ 本文件已覆盖通用参数。") : tr("本文件参数继承通用设置：%1 · 边距 %2 · 重叠 %3 · 页码%4。").arg(tmpl_.paper).arg(tmpl_.margin).arg(tmpl_.overlap).arg(tmpl_.pageNumbers ? tr("开启") : tr("关闭")));
    bu_.restoreTmpl->setEnabled(over);
    bu_.restoreAuto->setEnabled(it.haveAuto && s_ != it.autoState);
    bu_.applyFollow->setEnabled(pos >= 0 && pos + 1 < order.size());
    bu_.confirm->setEnabled(true);
    bu_.confirm->setText(it.confirmed ? tr("已确认 · 下一个") : tr("确认并下一个"));
}

void MainWindow::stepItem(int delta) {
    const QVector<int> order = viewOrder();
    if (order.isEmpty()) return;
    int n = order.indexOf(cur_);
    for (;;) {
        n += delta;
        if (n < 0 || n >= order.size()) return;
        if (items_[order[n]]->valid()) { activateItem(order[n]); return; }
    }
}

void MainWindow::confirmCurrent(bool andNext) {
    if (cur_ < 0 || cur_ >= items_.size() || !items_[cur_]->valid()) return;
    commitCurrentItem();
    BatchItem& it = *items_[cur_];
    it.confirmed = true;
    refreshStatus(it);
    refreshRow(cur_);
    refreshBatchSummary();
    updateBatchInfo();
    if (andNext) stepItem(+1);
}

void MainWindow::confirmAllAuto() {
    if (!batch_) return;
    if (bstep_ == 3) commitCurrentItem();
    for (auto& it : items_) {
        if (!it->valid() || !it->haveAuto) continue;
        it->confirmed = true;
        refreshStatus(*it);
    }
    refreshTable();
    if (bstep_ == 3) { refreshQueueList(); updateBatchInfo(); }
    refreshBatchSummary();
}

void MainWindow::restoreAutoPlan() {
    if (cur_ < 0 || cur_ >= items_.size()) return;
    const State a = items_[cur_]->autoState;
    edit([&](State& s) { s = a; });
}

void MainWindow::restoreTemplateParams() {
    if (cur_ < 0 || cur_ >= items_.size() || !work_) return;
    BatchItem& it = *items_[cur_];
    BatchItem probe = it;
    probe.state = s_;
    const State nd = deriveState(probe, tmpl_);   // keeps rotation / selection / frame choice, resets layout parameters
    const AutoResult r = autoFromDoc(it.id, *work_, nd, opts_, cur_, it.path);
    if (!r.err.isEmpty()) { notify(tr("恢复通用设置"), r.err); return; }
    it.autoState = r.state;
    it.autoPages = r.pages;
    it.hasHint = r.hint;
    it.haveAuto = true;
    edit([&](State& s) { s = r.state; });
}

void MainWindow::applyToFollowing() {
    const QVector<int> order = viewOrder();
    const int pos = order.indexOf(cur_);
    if (pos < 0) return;
    commitCurrentItem();
    const State src = s_;
    int n = 0;
    for (int k = pos + 1; k < order.size(); ++k) {
        BatchItem& it = *items_[order[k]];
        if (!it.valid()) continue;
        it.state.paper = src.paper; it.state.margin = src.margin; it.state.overlap = src.overlap;
        it.state.autoOrient = src.autoOrient;
        if (!src.autoOrient) it.state.landscape = src.landscape;
        it.state.offX = it.state.offY = 0;      // position is never copied (PRD 4.4)
        it.haveAuto = false; it.confirmed = false; it.adjusted = false;
        it.status = ItemStatus::Parsed;
        ++n;
    }
    if (n == 0) return;
    startAuto(false);
    refreshQueueList();
}

// ============================================================================ step 5: export

void MainWindow::syncOptsUi() {
    if (!bu_.calibScope) return;
    { QSignalBlocker b(bu_.calibScope); bu_.calibScope->setCurrentIndex(opts_.calibFirstOnly ? 1 : 0); }
    { QSignalBlocker b(bu_.startPos); bu_.startPos->setCurrentIndex(opts_.autoPosition ? 1 : 0); }
    { QSignalBlocker b(bu_.fileTag); bu_.fileTag->setChecked(opts_.fileTag); }
    { QSignalBlocker b1(bu_.outSource), b2(bu_.outCustom); bu_.outSource->setChecked(opts_.outDir.isEmpty()); bu_.outCustom->setChecked(!opts_.outDir.isEmpty()); }
    bu_.outDirShow->setText(opts_.outDir.isEmpty() ? tr("输出到各文件所在的文件夹") : QDir::toNativeSeparators(opts_.outDir));
    bu_.outChoose->setEnabled(!exporting_);
    bu_.outSource->setEnabled(!exporting_);
    bu_.outCustom->setEnabled(!exporting_);
    { QSignalBlocker b(bu_.concurrency); bu_.concurrency->setValue(std::clamp(opts_.concurrency, bu_.concurrency->minimum(), bu_.concurrency->maximum())); }
    bu_.concurrency->setEnabled(!exporting_);
    bool anyFailed = false, anyDone = false;
    for (const auto& it : items_) { if (it->exportFailed) anyFailed = true; if (it->status == ItemStatus::Done) anyDone = true; }
    bu_.expStart->setEnabled(!exporting_ && maxBatchStep() >= 3);
    bu_.expPause->setEnabled(exporting_);
    bu_.expCancel->setEnabled(exporting_);
    bu_.expOpen->setEnabled(!exporting_ && anyDone);
    bu_.expCsv->setEnabled(!exporting_ && (anyDone || anyFailed));
    bu_.expRetry->setEnabled(!exporting_ && anyFailed);
    bu_.expBack->setEnabled(!exporting_);
}

void MainWindow::batchStartExport(bool retryOnly) {
    if (!batch_ || exporting_) return;
    if (bstep_ != 4) goBatchStep(4);
    if (bstep_ != 4) return;
    if (autoPending_ > 0 || scanPending_ > 0) { notify(tr("请稍候"), tr("后台仍在处理文件。")); return; }
    QVector<int> targets;
    QStringList badScan;
    bool anyDone = false;
    for (int i = 0; i < items_.size(); ++i) {
        const BatchItem& it = *items_[i];
        if (it.status == ItemStatus::Done) anyDone = true;
        if (retryOnly) { if (it.exportFailed) targets.push_back(i); continue; }
        if (it.status == ItemStatus::Failed && !it.exportFailed) badScan << QFileInfo(it.path).fileName();
        else if ((it.valid() || it.exportFailed) && it.haveAuto) targets.push_back(i);
    }
    if (targets.isEmpty()) { notify(tr("批量导出"), tr("没有可导出的文件。")); return; }
    if (!retryOnly && anyDone && !ask(tr("再次导出"), tr("部分文件已经导出过。再次导出会生成带序号的新文件（如 _2），不会覆盖旧文件。\n\n是否继续？"), true)) return;
    if (!badScan.isEmpty() && !ask(tr("存在无法解析的文件"), tr("有 %1 个文件无法解析，将被跳过：\n%2\n\n是否跳过后继续？").arg(badScan.size()).arg(badScan.mid(0, 8).join('\n') + (badScan.size() > 8 ? "\n…" : "")), true)) return;
    // estimates
    double pages = 0;
    QStringList big;
    QHash<QString, double> perDir;
    for (int i : targets) {
        const BatchItem& it = *items_[i];
        const int p = it.pages > 0 ? it.pages : estimatePages(it.w, it.h, it.state);
        pages += p;
        if (it.big()) big << QFileInfo(it.path).fileName();
        perDir[opts_.outDir.isEmpty() ? QFileInfo(it.path).absolutePath() : opts_.outDir] += p;
    }
    if (!big.isEmpty() && !ask(tr("大文件"), tr("以下文件较大（线段 > 50 万或页数 > 100），将单独（单线程）导出，耗时较长：\n%1\n\n是否继续？").arg(big.join('\n')), true)) return;
    for (auto itd = perDir.begin(); itd != perDir.end(); ++itd) {   // PRD 4.7: estimate the space first (~100 KB per page, conservative)
        QStorageInfo si(itd.key());
        if (si.isValid() && si.bytesAvailable() > 0 && si.bytesAvailable() < qint64(itd.value() * 100 * 1024) &&
            !ask(tr("磁盘空间可能不足"), tr("%1\n预计需要约 %2 MB，可用 %3 MB。是否仍要继续？").arg(QDir::toNativeSeparators(itd.key())).arg(itd.value() * 0.1, 0, 'f', 0).arg(si.bytesAvailable() / 1048576), false))
            return;
    }
    const int secs = int(std::ceil(pages * 0.15 / std::max(1, opts_.concurrency)));
    if (!ask(tr("批量导出"), tr("共 %1 个文件、%2 页，预计耗时约 %3 秒。\n输出：%4\n\n是否开始导出？").arg(targets.size()).arg(int(pages)).arg(secs)
                            .arg(opts_.outDir.isEmpty() ? tr("各文件所在文件夹") : QDir::toNativeSeparators(opts_.outDir)), true)) return;
    // pre-assign every output name before anything runs (PRD 4.5.1) so parallel jobs can never collide
    QSet<QString> taken;
    exportIds_.clear();
    retryMode_ = retryOnly;
    if (!retryOnly) results_.clear();
    for (int i : targets) {
        BatchItem& it = *items_[i];
        const QString dir = opts_.outDir.isEmpty() ? QFileInfo(it.path).absolutePath() : opts_.outDir;
        QDir().mkpath(dir);
        it.exportFailed = false;
        it.error.clear();
        it.progress = 0;
        if (!QFileInfo(dir).isDir() || !QFileInfo(dir).isWritable()) {
            it.status = ItemStatus::Failed; it.exportFailed = true; it.error = tr("输出目录不存在或没有写入权限");
            continue;
        }
        it.outPath = allocateOutputPath(taken, dir, QFileInfo(it.path).completeBaseName(), it.state);
        it.status = ItemStatus::Exporting;
        exportIds_.push_back(it.id);
    }
    pagesTotal_ = pages;
    haveExportResult_ = false;
    ctl_ = std::make_shared<ExportControl>();
    exporting_ = true;
    exportPending_ = exportIds_.size();
    exportPool_.setMaxThreadCount(std::clamp(opts_.concurrency, 1, 4));
    exportClock_.start();
    exportTick_.start();
    bu_.expPause->setText(tr("暂停"));
    syncOptsUi();
    refreshTable();
    exportTick();
    if (exportPending_ == 0) { finishExport(); return; }
    QPointer<MainWindow> self(this);
    for (int i : targets) {
        BatchItem& it = *items_[i];
        if (it.status != ItemStatus::Exporting) continue;
        const int id = it.id;
        const QString path = it.path, out = it.outPath;
        const State st = it.state;
        const layout::Params p = batchParams(st, opts_, i, path);
        const bool big = it.big();
        const std::shared_ptr<ExportControl> ctl = ctl_;
        auto* w = new QFutureWatcher<ExportResult>(this);
        connect(w, &QFutureWatcher<ExportResult>::finished, this, [this, w] {
            const ExportResult r = w->result();
            w->deleteLater();
            onExportResult(r);
        });
        w->setFuture(QtConcurrent::run(&exportPool_, [=]() {
            return exportOne(id, path, st, p, out, big, *ctl, [self, id](int pi, int pn) {
                QMetaObject::invokeMethod(qApp, [self, id, pi, pn] { if (self) self->setItemProgress(id, pi, pn); }, Qt::QueuedConnection);
            });
        }));
    }
}

void MainWindow::setItemProgress(int id, int i, int n) {
    for (auto& it : items_) {
        if (it->id != id || it->status != ItemStatus::Exporting) continue;
        it->progress = n > 0 ? std::clamp(int(100.0 * i / n), 0, 99) : 0;
        updateRowStatus(id);
    }
}

void MainWindow::onExportResult(const ExportResult& r) {
    --exportPending_;
    for (auto& it : items_) {
        if (it->id != r.id) continue;
        if (r.cancelled) { it->status = ItemStatus::Parsed; it->progress = 0; refreshStatus(*it); }
        else if (!r.err.isEmpty()) { it->status = ItemStatus::Failed; it->exportFailed = true; it->error = r.err; it->progress = 0; }
        else { it->status = ItemStatus::Done; it->exportFailed = false; it->error.clear(); it->progress = 100; it->outPath = r.outPath; results_.push_back(r); }
        break;
    }
    refreshTable();
    if (exportPending_ <= 0) finishExport();
}

void MainWindow::exportTick() {
    if (!exporting_) return;
    double done = 0;
    for (const auto& it : items_) {
        if (!exportIds_.contains(it->id)) continue;
        const int p = it->pages > 0 ? it->pages : 1;
        if (it->status == ItemStatus::Done) done += p;
        else if (it->status == ItemStatus::Exporting) done += p * it->progress / 100.0;
    }
    const double frac = pagesTotal_ > 0 ? std::min(1.0, done / pagesTotal_) : 0;
    bu_.expBar->setValue(int(frac * 1000));
    bu_.expBar->setFormat(tr("%1%").arg(int(frac * 100)));
    const double el = exportClock_.elapsed() / 1000.0;
    QString t = tr("已用 %1 秒").arg(int(el));
    if (frac > 0.02) t += tr(" · 预计剩余约 %1 秒").arg(int(el * (1 - frac) / frac));
    if (ctl_ && ctl_->paused.load()) t += tr(" · 已暂停");
    if (ctl_ && ctl_->cancel.load()) t += tr(" · 正在取消…");
    bu_.expTime->setText(t);
}

void MainWindow::batchPause(bool on) {
    if (!exporting_ || !ctl_) return;
    ctl_->paused = on;
    bu_.expPause->setText(on ? tr("继续") : tr("暂停"));
    exportTick();
}

void MainWindow::batchCancel() {
    if (!exporting_ || !ctl_) return;
    ctl_->cancel = true;
    ctl_->paused = false;
    exportTick();
}

void MainWindow::finishExport() {
    exporting_ = false;
    exportTick_.stop();
    exportElapsedMs_ = exportClock_.elapsed();
    int ok = 0, failed = 0, cancelled = 0, pages = 0;
    double area = 0;
    for (const auto& it : items_) {
        if (!exportIds_.contains(it->id)) continue;
        if (it->status == ItemStatus::Done) ++ok; else if (it->exportFailed) ++failed; else ++cancelled;
    }
    for (const auto& r : results_) { pages += r.pages; area += r.areaM2; }
    int skipped = 0;
    for (const auto& it : items_) if (it->status == ItemStatus::Failed && !it->exportFailed) ++skipped;
    QString t = tr("成功 <b style='color:#0B7A70'>%1</b> · 失败 <b style='color:#B42318'>%2</b> · 跳过 %3").arg(ok).arg(failed).arg(skipped);
    if (cancelled) t += tr(" · 已取消 %1").arg(cancelled);
    t += tr("<br>共 %1 页 · 纸张约 %2 ㎡ · 用时 %3 秒").arg(pages).arg(area, 0, 'f', 2).arg(exportElapsedMs_ / 1000.0, 0, 'f', 1);
    bu_.expSummary->setText(t);
    haveExportResult_ = true;
    bu_.expBar->setValue(cancelled ? bu_.expBar->value() : 1000);
    bu_.expBar->setFormat(cancelled ? tr("已取消") : tr("完成"));
    bu_.expTime->setText(cancelled ? tr("已取消，已完成的文件已保留。") : tr("已完成，用时 %1 秒。").arg(exportElapsedMs_ / 1000.0, 0, 'f', 1));
    syncOptsUi();
    refreshTable();
    refreshBatchSummary();
    if (failed == 0 && cancelled == 0 && !items_.isEmpty()) clearSession(); else saveSession();
    if (!silent_) {
        QString msg = tr("成功 %1 个，失败 %2 个，跳过 %3 个。\n共 %4 页，用时 %5 秒。").arg(ok).arg(failed).arg(skipped).arg(pages).arg(exportElapsedMs_ / 1000.0, 0, 'f', 1);
        if (failed) msg += tr("\n\n失败的文件可点「仅重试失败项」，必要时先更换输出文件夹。");
        msg += tr("\n\nPDF 为 1:1 实际尺寸，请选择【实际大小 / 100%】，切勿选择【适应页面】。");
        QMessageBox::information(this, tr("批量导出完成"), msg);
    }
    emit batchExportFinished();
}

QString MainWindow::batchReportCsv() const {
    QVector<ReportRow> rows;
    for (const auto& it : items_) {
        ReportRow r;
        r.name = QFileInfo(it->path).fileName();
        r.paper = it->state.singlePage ? tr("单页") : it->state.paper;
        r.orient = orientText(it->state);
        r.pages = it->pages;
        switch (it->status) {
            case ItemStatus::Done: r.status = tr("成功"); r.output = QDir::toNativeSeparators(it->outPath); break;
            case ItemStatus::Failed: r.status = tr("失败"); r.reason = it->error; break;
            case ItemStatus::Skipped: r.status = tr("跳过"); break;
            default: r.status = tr("未导出"); break;
        }
        rows.push_back(r);
    }
    return reportCsv(rows);
}

// ============================================================================ templates / session

void MainWindow::loadTemplates() {
    QSettings q("HaoyangPattern", "PltConverter");
    namedTemplates_.clear();
    q.beginGroup("batchTemplates");
    for (const QString& k : q.childKeys()) {
        const QJsonDocument d = QJsonDocument::fromJson(q.value(k).toString().toUtf8());
        if (d.isObject()) namedTemplates_[k] = stateFromJson(d.object());
    }
    q.endGroup();
    if (!bu_.templates) return;
    QSignalBlocker b(bu_.templates);
    bu_.templates->clear();
    bu_.templates->addItem(tr("载入模板…"));
    QStringList names = namedTemplates_.keys();
    names.sort(Qt::CaseInsensitive);
    bu_.templates->addItems(names);
}

void MainWindow::saveTemplates() {
    QSettings q("HaoyangPattern", "PltConverter");
    q.beginGroup("batchTemplates");
    for (const QString& k : q.childKeys()) q.remove(k);
    for (auto it = namedTemplates_.begin(); it != namedTemplates_.end(); ++it)
        q.setValue(it.key(), QString::fromUtf8(QJsonDocument(stateToJson(it.value())).toJson(QJsonDocument::Compact)));
    q.endGroup();
}

void MainWindow::saveSession() {
    if (items_.isEmpty()) return;
    if (batch_ && bstep_ == 3) commitCurrentItem();
    QSaveFile f(batchSessionPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(sessionToJson(batch_ && bstep_ == 1 ? s_ : tmpl_, opts_, items_));
    f.commit();
}

void MainWindow::clearSession() { QFile::remove(batchSessionPath()); }

void MainWindow::offerRestore() {
    if (silent_ || exporting_ || !items_.isEmpty() || batch_) return;
    QFile f(batchSessionPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    State t;
    BatchOptions o;
    QVector<std::shared_ptr<BatchItem>> its;
    if (!sessionFromJson(f.readAll(), t, o, its)) { f.close(); clearSession(); return; }
    f.close();
    if (!ask(tr("恢复上次批量任务"), tr("检测到上次未完成的批量任务（%1 个文件）。\n是否恢复队列、参数和确认状态？").arg(its.size()), true)) { clearSession(); return; }
    tmpl_ = t;
    tmplBefore_ = t;
    opts_ = o;
    items_ = its;
    nextId_ = items_.size() + 1;
    setBatchMode(true);
    for (const auto& it : items_) {
        if (!QFileInfo::exists(it->path)) { it->status = ItemStatus::Failed; it->error = tr("源文件不可访问（已被移动或删除）"); continue; }
        startScan(it);
    }
    refreshTable();
    refreshBatchSummary();
}
