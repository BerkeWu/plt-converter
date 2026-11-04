#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QSpinBox>
#include <QTreeWidget>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QThread>
#include <QToolButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <cmath>

#include "BatchWidgets.h"
#include "PageRender.h"
#include "BatchCore.h"
#include "PdfExporterQt.h"
#include "Widgets.h"
#include "../core/docops.h"
#include "../core/naming.h"
#include "../core/pagerange.h"

namespace {

const QColor kPresets[5] = {QColor("#111111"), QColor("#D33333"), QColor("#2563EB"), QColor("#16A34A"), QColor("#7C3AED")};
const char* kPresetNames[5] = {"黑色", "红色", "蓝色", "绿色", "紫色"};
const QColor kAlignPresets[5] = {QColor("#2563EB"), QColor("#111111"), QColor("#D33333"), QColor("#16A34A"), QColor("#7C3AED")};
const char* kAlignNames[5] = {"蓝色（默认）", "黑色", "红色", "绿色", "紫色"};
const QStringList kPapers = {"A0", "A3", "A4"};
const double kMargins[4] = {0, 3, 5, 10};

QString humanSize(qint64 b) {
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
}

std::shared_ptr<DrawingView> makeView(const plt::Document& d) {
    auto v = std::make_shared<DrawingView>();
    v->w = d.bounds.w();
    v->h = d.bounds.h();
    v->segments = int(std::min<size_t>(d.segmentCount, 1 << 30));
    const double bx = d.bounds.minX, by = d.bounds.maxY;
    v->polys.reserve(int(d.lines.size()));
    v->boxes.reserve(int(d.lines.size()));
    for (const auto& pl : d.lines) {
        QPolygonF poly;
        poly.reserve(int(pl.pts.size()));
        for (const auto& p : pl.pts) poly << QPointF(p.x - bx, by - p.y);
        v->boxes.push_back(poly.boundingRect());
        QColor pc;  // pen colour from the PLT (PC), near-white pens print light grey (same rule as the engine)
        auto it = d.penColors.find(pl.pen);
        if (it != d.penColors.end()) {
            double r = it->second.r, g = it->second.g, b = it->second.b;
            if (0.299 * r + 0.587 * g + 0.114 * b > 0.9) r = g = b = 0.75;
            pc = QColor::fromRgbF(r, g, b);
        }
        v->colors.push_back(pc);
        v->polys.push_back(std::move(poly));
    }
    return v;
}

// Grid geometry for the current state (same numbers the engine uses).
bool gridGeom(const State& s, const plt::Document& d, double& Wp, double& Hp, int& nx, int& ny, double& gw, double& gh, double* strip = nullptr) {
    const double w = d.bounds.w(), h = d.bounds.h();
    if (strip) *strip = 0;
    if (s.singlePage) { Wp = std::max(w, 20.0 - 2 * s.margin); Hp = std::max(h, 20.0 - 2 * s.margin); nx = ny = 1; gw = Wp; gh = Hp; return true; }
    const layout::TileGeom g = layout::tileGeometry(toParams(s), w, h);   // same numbers the engine uses
    if (!g.ok) return false;
    Wp = g.fw; Hp = g.fh; nx = g.nx; ny = g.ny;
    gw = nx * g.fw + 2 * g.strip; gh = ny * g.fh + 2 * g.strip;
    if (strip) *strip = g.strip;
    return true;
}

PlanResult computePlan(quint64 gen, std::shared_ptr<const plt::Document> doc, layout::Params p, bool autoOrient, bool skipLarger) {
    PlanResult r;
    r.gen = gen;
    try {
        if (autoOrient && !p.singlePage) {
            r.oc = layout::chooseOrientation(*doc, p);
            r.orientEvaluated = true;
            p.orient = r.oc.best;
        }
        r.landscape = p.orient == layout::Orient::Landscape;
        layout::Plan plan = layout::buildPlan(*doc, p);
        r.nx = plan.nx; r.ny = plan.ny; r.effective = plan.effectivePages; r.blank = plan.blankPages;
        r.gridPages = plan.gridPages; r.calibCollision = plan.calibCollision;
        r.pageW = plan.pageW; r.pageH = plan.pageH;
        const int n = int(plan.pages.size());
        for (int i = 0; i < n; ++i) {
            const auto& pg = plan.pages[i];
            PageMeta m;
            m.row = pg.row; m.col = pg.col; m.overview = pg.overview; m.empty = pg.empty; m.w = pg.w; m.h = pg.h;
            m.label = QString::fromUtf8(pg.label.c_str());
            r.meta.push_back(m);
            if (i < 150) r.thumbs.push_back(renderPageThumb(pg, 300, plan.calibCollision && pg.label == "1-1"));
            else r.thumbs.push_back(QImage());
        }
        if (!p.singlePage && plan.gridPages >= 2) r.hint = layout::shiftHint(*doc, p);
        if (!p.singlePage && !skipLarger && plan.effectivePages >= 4) {
            const char* order[3] = {"A4", "A3", "A0"};
            int cur = p.paper.name == "A4" ? 0 : p.paper.name == "A3" ? 1 : 2;
            for (int k = cur + 1; k < 3; ++k) {
                layout::Params q = p;
                q.paper = layout::paperByName(order[k]);
                auto oc = layout::chooseOrientation(*doc, q);
                int best = std::min(oc.portraitPages, oc.landscapePages);
                if (best < plan.effectivePages) { r.largerPaper = order[k]; r.largerGain = plan.effectivePages - best; break; }
            }
        }
    } catch (const std::exception& e) {
        r.err = friendlyError(QString::fromUtf8(e.what()));
    }
    return r;
}

// Builds the plan and writes the chosen pages (0-based indices into plan.pages).
QString exportJob(std::shared_ptr<const plt::Document> doc, layout::Params p, QVector<int> pages, QString path,
                  std::function<bool(int, int)> progress) {
    try {
        layout::Plan plan = layout::buildPlan(*doc, p);
        layout::Plan sub;
        sub.pageW = plan.pageW; sub.pageH = plan.pageH;
        for (int i : pages)
            if (i >= 0 && i < int(plan.pages.size())) sub.pages.push_back(std::move(plan.pages[i]));
        if (sub.pages.empty()) return QObject::tr("没有可导出的页面");
        return PdfExporterQt::write(sub, path, progress);
    } catch (const std::exception& e) {
        return friendlyError(QString::fromUtf8(e.what()));
    }
}

}  // namespace

// ============================================================================ construction

MainWindow::MainWindow() {
    setWindowTitle(tr("PLT 排版转换器"));
    setMinimumSize(1100, 700);
    resize(1360, 820);
    setAcceptDrops(true);
    editClock_.start();

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    auto* main = new QHBoxLayout();
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    leftStack_ = new QStackedWidget;
    leftStack_->setFixedWidth(300);
    leftStack_->setObjectName("stack");
    // 0 import, 1 layout, 2 single export, 3 batch: add files, 4 batch: auto layout, 5 batch: export
    for (QWidget* panel : {buildImportPanel(), buildLayoutPanel(), buildExportPanel(), buildBatchAddPanel(), buildBatchAutoPanel(), buildBatchExportPanel()}) {
        auto* sc = new QScrollArea;
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sc->setWidget(panel);
        leftStack_->addWidget(sc);
    }
    // the parameter column lives in sideHolder_; in batch step 4 it moves to the right of the canvas (PRD 4.4)
    sideHolder_ = new QWidget;
    sideHolder_->setObjectName("side");
    auto* sh = new QVBoxLayout(sideHolder_);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(0);
    sh->addWidget(buildBatchToggleBar());
    sh->addWidget(leftStack_, 1);
    bu_.leftHost = new QWidget;
    auto* lh = new QVBoxLayout(bu_.leftHost);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->addWidget(sideHolder_);
    main->addWidget(buildBatchQueueBox());
    main->addWidget(bu_.leftHost);
    centerStack_ = new QStackedWidget;
    centerStack_->addWidget(buildCanvasPage());
    centerStack_->addWidget(buildPreviewPage());
    centerStack_->addWidget(buildBatchTablePage());
    main->addWidget(centerStack_, 1);
    main->addWidget(buildBatchRightHost());
    root->addLayout(main, 1);
    root->addWidget(buildBatchBottomBar());
    setCentralWidget(central);
    applyStyle();
    batchChrome();

    planTimer_.setSingleShot(true);
    planTimer_.setInterval(150);  // PRD 4.6: 150 ms debounce
    connect(&planTimer_, &QTimer::timeout, this, &MainWindow::runPlan);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(800);
    connect(&saveTimer_, &QTimer::timeout, this, &MainWindow::saveSettings);

    auto sc = [&](const QKeySequence& k, auto fn) { auto* a = new QShortcut(k, this); a->setContext(Qt::ApplicationShortcut); connect(a, &QShortcut::activated, this, fn); };
    sc(QKeySequence::Open, [this] { chooseFile(); });
    sc(QKeySequence::Undo, [this] { undo(); });
    sc(QKeySequence("Ctrl+Y"), [this] { redo(); });
    sc(QKeySequence("Ctrl+Shift+Z"), [this] { redo(); });
    sc(QKeySequence("Ctrl+Left"), [this] { if (batch_ && bstep_ == 3) stepItem(-1); });   // plain arrows nudge the drawing
    sc(QKeySequence("Ctrl+Right"), [this] { if (batch_ && bstep_ == 3) stepItem(+1); });
    sc(QKeySequence("PgUp"), [this] { if (batch_ && bstep_ == 3) stepItem(-1); });
    sc(QKeySequence("PgDown"), [this] { if (batch_ && bstep_ == 3) stepItem(+1); });

    loadSettings();
    tmpl_ = s_;
    scanPool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 4));  // PRD 4.6: parse concurrency
    autoPool_.setMaxThreadCount(2);
    loadPool_.setMaxThreadCount(1);
    sessionTimer_.setInterval(30000);  // PRD 4.6: auto-save the batch session every 30 s
    connect(&sessionTimer_, &QTimer::timeout, this, &MainWindow::saveSession);
    sessionTimer_.start();
    exportTick_.setInterval(400);
    connect(&exportTick_, &QTimer::timeout, this, &MainWindow::exportTick);
    loadTemplates();
    QTimer::singleShot(400, this, &MainWindow::offerRestore);
    canvas_->setEmptyHint(tr("把 .plt 文件拖到这里\n或点击左侧「选择 PLT 文件」（Ctrl+O）"));
    syncUi();
    goStep(0);
}

QWidget* MainWindow::buildTopBar() {
    auto* top = new QWidget;
    top->setObjectName("top");
    top->setFixedHeight(52);
    auto* l = new QHBoxLayout(top);
    l->setContentsMargins(20, 0, 20, 0);
    auto* logo = new QLabel(tr("PLT 排版转换器"));
    logo->setObjectName("logo");
    l->addWidget(logo);
    l->addStretch(1);
    for (int i = 0; i < 5; ++i) {
        stepBtn_[i] = new QPushButton;
        stepBtn_[i]->setObjectName("stepBtn");
        stepBtn_[i]->setCursor(Qt::PointingHandCursor);
        connect(stepBtn_[i], &QPushButton::clicked, this, [this, i] { goStep(i); });
        l->addWidget(stepBtn_[i]);
    }
    l->addStretch(1);
    auto* mode = new QFrame;
    mode->setObjectName("mode");
    auto* ml = new QHBoxLayout(mode);
    ml->setContentsMargins(3, 3, 3, 3);
    ml->setSpacing(0);
    bu_.modeSingle = new QPushButton(tr("单文件"));
    bu_.modeBatch = new QPushButton(tr("批量转换"));
    bu_.modeSingle->setProperty("on", true);
    bu_.modeBatch->setToolTip(tr("一次设置参数，批量转换最多 20 个文件"));
    connect(bu_.modeSingle, &QPushButton::clicked, this, [this] { setBatchMode(false); });
    connect(bu_.modeBatch, &QPushButton::clicked, this, [this] { setBatchMode(true); });
    ml->addWidget(bu_.modeSingle);
    ml->addWidget(bu_.modeBatch);
    l->addWidget(mode);
    return top;
}

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
QWidget* rowOf(QWidget* left, QWidget* right) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->addWidget(left, 1);
    l->addWidget(right, 0);
    return w;
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
QDoubleSpinBox* spin(double lo, double hi, double step, int dec, const QString& suffix) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}
}  // namespace

QWidget* MainWindow::buildImportPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(11);
    openBtn_ = primaryButton(tr("选择 PLT 文件…"));
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::chooseFile);
    l->addWidget(openBtn_);
    impDrag_ = noteLabel(tr("也可以把 .plt 文件拖到窗口任意位置。扩展名不区分大小写，支持中文和空格路径。"));
    l->addWidget(impDrag_);
    busy_ = new QProgressBar;
    busy_->setRange(0, 0);
    busy_->setTextVisible(false);
    busy_->setFixedHeight(6);
    busy_->hide();
    l->addWidget(busy_);
    impInfo_ = new QLabel;
    impInfo_->setTextFormat(Qt::RichText);
    impInfo_->setWordWrap(true);
    l->addWidget(impInfo_);
    impNotes_ = new QLabel;
    impNotes_->setTextFormat(Qt::RichText);
    impNotes_->setWordWrap(true);
    impNotes_->setProperty("note", true);
    l->addWidget(impNotes_);

    ignoreFrame_ = new QCheckBox(tr("忽略纸张边框（门幅边框）"));
    connect(ignoreFrame_, &QCheckBox::toggled, this, [this](bool on) {
        edit([on](State& s) { s.ignoreFrame = on; s.hasSel = false; s.offX = s.offY = 0; });
    });
    l->addWidget(ignoreFrame_);
    frameNote_ = noteLabel();
    l->addWidget(frameNote_);

    l->addWidget(sectionLabel(tr("图形变换")));
    auto* tr1 = new QWidget;
    auto* tl = new QHBoxLayout(tr1);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(6);
    for (int k = 1; k <= 3; ++k) {
        auto* b = plainButton(tr("%1°").arg(k * 90));
        b->setToolTip(tr("逆时针旋转 %1°").arg(k * 90));
        connect(b, &QPushButton::clicked, this, [this, k] { edit([k](State& s) { s.rot = (s.rot + k) % 4; s.hasSel = false; s.offX = s.offY = 0; }); });
        tl->addWidget(b);
    }
    auto* mir = plainButton(tr("水平镜像"));
    connect(mir, &QPushButton::clicked, this, [this] { edit([](State& s) { s.mirror = !s.mirror; s.hasSel = false; s.offX = s.offY = 0; }); });
    tl->addWidget(mir);
    l->addWidget(tr1);
    resetXformBtn_ = plainButton(tr("还原方向"));
    connect(resetXformBtn_, &QPushButton::clicked, this, [this] { edit([](State& s) { s.rot = 0; s.mirror = false; s.hasSel = false; s.offX = s.offY = 0; }); });
    l->addWidget(resetXformBtn_);
    xformNote_ = noteLabel(tr("图形方向已变更：旋转/镜像会改变布纹方向，请确认后再输出。"));
    xformNote_->setStyleSheet("color:#A15C00");
    l->addWidget(xformNote_);

    l->addWidget(sectionLabel(tr("框选区域（可选）")));
    selBtn_ = plainButton(tr("框选区域"));
    selBtn_->setCheckable(true);
    selBtn_->setToolTip(tr("在画布上拖拽绘制矩形，仅对选区内的线条排版"));
    connect(selBtn_, &QPushButton::toggled, this, [this](bool on) { canvas_->setSelectTool(on); });
    clearSelBtn_ = plainButton(tr("清除选区"));
    connect(clearSelBtn_, &QPushButton::clicked, this, [this] { edit([](State& s) { s.hasSel = false; s.offX = s.offY = 0; }); });
    l->addWidget(rowOf(selBtn_, clearSelBtn_));
    clipRadio_ = new QRadioButton(tr("按选区边界裁剪"));
    wholeRadio_ = new QRadioButton(tr("整条保留"));
    clipRadio_->setChecked(true);
    connect(clipRadio_, &QRadioButton::toggled, this, [this](bool on) { edit([on](State& s) { s.selClip = on; s.offX = s.offY = 0; }); });
    l->addWidget(clipRadio_);
    l->addWidget(wholeRadio_);
    selInfo_ = noteLabel();
    l->addWidget(selInfo_);
    l->addStretch(1);
    auto* toBatch = plainButton(tr("转为批量转换"));
    toBatch->setToolTip(tr("把当前文件和参数带入批量转换，继续添加更多文件"));
    connect(toBatch, &QPushButton::clicked, this, [this] { convertToBatch(); });
    toBatch->setObjectName("toBatchBtn");
    l->addWidget(toBatch);
    toLayoutBtn_ = primaryButton(tr("下一步：排版设置"));
    connect(toLayoutBtn_, &QPushButton::clicked, this, [this] { goStep(1); });
    l->addWidget(toLayoutBtn_);
    return w;
}

QWidget* MainWindow::buildLayoutPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(11);

    l->addWidget(sectionLabel(tr("纸张尺寸")));
    paperSeg_ = makeSegmented(kPapers, paperGrp_);
    l->addWidget(paperSeg_);
    paperNote_ = noteLabel();
    l->addWidget(paperNote_);
    connect(paperGrp_, &QButtonGroup::idClicked, this, [this](int id) { edit([id](State& s) { s.paper = kPapers[id]; }); });

    orientRow_ = new QWidget;
    auto* ol = new QVBoxLayout(orientRow_);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(6);
    ol->addWidget(sectionLabel(tr("纸张方向")));
    orientSeg_ = makeSegmented({tr("竖向"), tr("横向")}, orientGrp_);
    ol->addWidget(orientSeg_);
    autoOrient_ = new QCheckBox(tr("自动选择最省纸方向"));
    ol->addWidget(autoOrient_);
    orientNote_ = noteLabel();
    ol->addWidget(orientNote_);
    l->addWidget(orientRow_);
    connect(orientGrp_, &QButtonGroup::idClicked, this, [this](int id) { edit([id](State& s) { s.landscape = id == 1; s.autoOrient = false; }); });
    connect(autoOrient_, &QCheckBox::toggled, this, [this](bool on) { edit([on](State& s) { s.autoOrient = on; }); });

    overlapSlider_ = new QSlider(Qt::Horizontal);
    overlapSlider_->setRange(0, 30);
    overlapSpin_ = spin(0, 30, 1, 0, " mm");
    auto* ovRow = new QWidget;
    auto* ovl = new QVBoxLayout(ovRow);
    ovl->setContentsMargins(0, 0, 0, 0);
    ovl->setSpacing(4);
    ovl->addWidget(rowOf(sectionLabel(tr("重叠区（框外延伸）")), overlapSpin_));
    ovl->addWidget(overlapSlider_);
    l->addWidget(ovRow);
    l->addWidget(noteLabel(tr("蓝色虚线框是拼接框，框外仍印出纸样线条。相邻两页把虚线框叠合，框外那圈线条就与邻页刚好重合；每个框角只有一个「+」定位点。")));
    connect(overlapSlider_, &QSlider::valueChanged, this, [this](int v) { edit([v](State& s) { s.overlap = v; }, "overlap"); });
    connect(overlapSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.overlap = v; }, "overlap"); });

    l->addWidget(sectionLabel(tr("页边距")));
    QWidget* mseg = makeSegmented({"0", "3", "5", "10"}, marginGrp_);
    marginSpin_ = spin(0, 50, 1, 0, " mm");
    marginSpin_->setToolTip(tr("自定义页边距"));
    l->addWidget(rowOf(mseg, marginSpin_));
    l->addWidget(noteLabel(tr("0 mm 边距不代表物理零边距：部分打印机会强制留白，请用校准框校准。")));
    connect(marginGrp_, &QButtonGroup::idClicked, this, [this](int id) { edit([id](State& s) { s.margin = kMargins[id]; }); });
    connect(marginSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.margin = v; }, "margin"); });

    auto addSwitch = [&](const QString& text, ToggleSwitch*& sw, std::function<void(State&, bool)> setter) {
        sw = new ToggleSwitch;
        sw->setAccessibleName(text);
        auto* lb = new QLabel(text);
        lb->setProperty("lb", true);
        lb->setWordWrap(true);
        l->addWidget(rowOf(lb, sw));
        connect(sw, &QAbstractButton::toggled, this, [this, setter](bool on) { edit([&](State& s) { setter(s, on); }); });
    };
    addSwitch(tr("单页输出"), singleSw_, [](State& s, bool v) { s.singlePage = v; s.offX = s.offY = 0; });
    l->addWidget(noteLabel(tr("开启后整个图形输出为一页（页面 = 图形 + 2×边距），纸张与方向置灰；不输出页码、重叠区和对位标记。")));
    addSwitch(tr("页码与标识"), pageNoSw_, [](State& s, bool v) { s.pageNumbers = v; });
    l->addWidget(noteLabel(tr("页码为大号加粗、带边框、80% 透明度。横向纸张在每页左上角，竖向纸张在底部中间；顶部/底部固定水印始终输出。")));
    addSwitch(tr("拼接对位标记"), alignSw_, [](State& s, bool v) { s.alignMarks = v; });
    l->addWidget(noteLabel(tr("拼接框的四条虚线贯穿整页，每个框角一个「+」。颜色：")));
    auto* asw = new QWidget;
    auto* aswl = new QHBoxLayout(asw);
    aswl->setContentsMargins(0, 0, 0, 0);
    aswl->setSpacing(6);
    auto* aGrp = new QButtonGroup(asw);
    aGrp->setExclusive(true);
    for (int i = 0; i < 6; ++i) {
        Swatch* b = new Swatch(i < 5 ? kAlignPresets[i] : QColor("#999999"));
        b->setToolTip(i < 5 ? tr(kAlignNames[i]) : tr("自定义颜色…"));
        aGrp->addButton(b, i);
        alignSwatches_.push_back(b);
        aswl->addWidget(b);
    }
    aswl->addStretch(1);
    l->addWidget(asw);
    connect(aGrp, &QButtonGroup::idClicked, this, [this](int id) {
        QColor c;
        if (id < 5) c = kAlignPresets[id];
        else {
            c = QColorDialog::getColor(s_.alignColor, this, tr("自定义对位标记颜色"));
            if (!c.isValid()) { syncUi(); return; }
        }
        edit([c](State& s) { s.alignColor = c; });
    });

    // line colour
    l->addWidget(sectionLabel(tr("纸样线条颜色")));
    auto* sw = new QWidget;
    auto* swl = new QHBoxLayout(sw);
    swl->setContentsMargins(0, 0, 0, 0);
    swl->setSpacing(6);
    auto* swGrp = new QButtonGroup(sw);
    swGrp->setExclusive(true);
    for (int i = 0; i < 7; ++i) {
        Swatch* b = new Swatch(i == 0 ? QColor(Qt::white) : i <= 5 ? kPresets[i - 1] : QColor("#999999"));
        b->setToolTip(i == 0 ? tr("原色：按 PLT 文件里各笔号定义的颜色输出；没有颜色信息的笔为黑色") : i <= 5 ? tr(kPresetNames[i - 1]) : tr("自定义颜色…"));
        swGrp->addButton(b, i);
        swatches_.push_back(b);
        swl->addWidget(b);
    }
    swl->addStretch(1);
    l->addWidget(sw);
    connect(swGrp, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 0) { edit([](State& s) { s.origColor = true; }); return; }
        QColor c;
        if (id <= 5) c = kPresets[id - 1];
        else {
            c = QColorDialog::getColor(s_.color, this, tr("自定义线条颜色"));
            if (!c.isValid()) { syncUi(); return; }
        }
        edit([c](State& s) { s.origColor = false; s.color = c; });
    });
    colorNote_ = noteLabel();
    l->addWidget(colorNote_);
    lineWidthSpin_ = spin(0.05, 2.0, 0.05, 2, " mm");
    l->addWidget(rowOf(sectionLabel(tr("线宽")), lineWidthSpin_));
    connect(lineWidthSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.lineWidth = v; }, "lw"); });

    addSwitch(tr("校准框（仅第 1 页）"), calibSw_, [](State& s, bool v) { s.calibBox = v; });
    calibSpin_ = spin(1, 20, 0.5, 1, " cm");
    l->addWidget(rowOf(noteLabel(tr("校准框边长（默认 5 cm）")), calibSpin_));
    connect(calibSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.calibCm = v; }, "calib"); });
    addSwitch(tr("添加拼接效果图（PDF 第 1 页）"), overviewSw_, [](State& s, bool v) { s.overview = v; });
    addSwitch(tr("输出空白页"), blankSw_, [](State& s, bool v) { s.keepBlank = v; });

    l->addWidget(sectionLabel(tr("图形位置（mm）")));
    xSpin_ = spin(0, 100000, 1, 1, "");
    ySpin_ = spin(0, 100000, 1, 1, "");
    xSpin_->setPrefix("X ");
    ySpin_->setPrefix("Y ");
    auto* pos = new QWidget;
    auto* pl = new QHBoxLayout(pos);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(xSpin_);
    pl->addWidget(ySpin_);
    l->addWidget(pos);
    connect(xSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.offX = v; }, "pos"); });
    connect(ySpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) { edit([v](State& s) { s.offY = v; }, "pos"); });
    alignTL_ = plainButton(tr("左上对齐"));
    center_ = plainButton(tr("居中"));
    autoPos_ = plainButton(tr("自动优化"));
    auto* pb = new QWidget;
    auto* pbl = new QHBoxLayout(pb);
    pbl->setContentsMargins(0, 0, 0, 0);
    pbl->setSpacing(6);
    for (auto* b : {alignTL_, center_, autoPos_}) pbl->addWidget(b);
    l->addWidget(pb);
    l->addWidget(noteLabel(tr("拖拽画布上的图形、方向键微调（1 mm，Shift 为 10 mm）。")));
    connect(alignTL_, &QPushButton::clicked, this, [this] { edit([](State& s) { s.offX = s.offY = 0; }); });
    connect(center_, &QPushButton::clicked, this, [this] {
        if (!work_) return;
        double Wp, Hp, gw, gh; int nx, ny;
        State z = s_; z.offX = z.offY = 0;
        if (!gridGeom(z, *work_, Wp, Hp, nx, ny, gw, gh)) return;
        const double ox = std::max(0.0, (gw - work_->bounds.w()) / 2), oy = std::max(0.0, (gh - work_->bounds.h()) / 2);
        edit([ox, oy](State& s) { s.offX = ox; s.offY = oy; });
    });
    connect(autoPos_, &QPushButton::clicked, this, [this] {
        if (!work_) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        layout::Params p = currentParams();
        p.orient = last_.landscape && s_.autoOrient ? layout::Orient::Landscape : p.orient;
        layout::OffsetChoice oc = layout::autoOffset(*work_, p);
        QApplication::restoreOverrideCursor();
        edit([oc](State& s) { s.offX = oc.x; s.offY = oc.y; });
    });

    l->insertWidget(0, buildBatchOptsBox());   // batch-only options: top of the panel, visible in step 2
    l->addStretch(1);
    auto* nav = new QWidget;
    auto* nl = new QHBoxLayout(nav);
    nl->setContentsMargins(0, 0, 0, 0);
    auto* back = plainButton(tr("上一步"));
    connect(back, &QPushButton::clicked, this, [this] { if (batch_) goBatchStep(bstep_ - 1); else goStep(0); });
    auto* next = primaryButton(tr("下一步：预览导出"));
    connect(next, &QPushButton::clicked, this, [this] { if (batch_) goBatchStep(bstep_ + 1); else goStep(2); });
    bu_.layoutBack = back;
    bu_.layoutNext = next;
    nl->addWidget(back);
    nl->addWidget(next, 1);
    l->addWidget(nav);
    return w;
}

QWidget* MainWindow::buildExportPanel() {
    auto* w = new QWidget;
    w->setObjectName("panel");
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(11);
    l->addWidget(sectionLabel(tr("导出摘要")));
    summary_ = new QLabel;
    summary_->setTextFormat(Qt::RichText);
    summary_->setWordWrap(true);
    l->addWidget(summary_);
    l->addWidget(sectionLabel(tr("导出页码范围")));
    rangeEdit_ = new QLineEdit;
    rangeEdit_->setPlaceholderText(tr("如 1-3,5（留空 = 全部）"));
    auto* ap = plainButton(tr("应用"));
    l->addWidget(rowOf(rangeEdit_, ap));
    rangeErr_ = noteLabel();
    rangeErr_->setStyleSheet("color:#A12B2B");
    rangeErr_->hide();
    l->addWidget(rangeErr_);
    auto* all = plainButton(tr("全选"));
    auto* none = plainButton(tr("全不选"));
    l->addWidget(rowOf(all, none));
    selCount_ = noteLabel();
    l->addWidget(selCount_);
    connect(ap, &QPushButton::clicked, this, &MainWindow::applyPageRange);
    connect(rangeEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyPageRange);
    connect(all, &QPushButton::clicked, this, [this] { rangeEdit_->clear(); rangeErr_->clear(); for (int i = 0; i < grid_->count(); ++i) grid_->item(i)->setCheckState(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [this] { for (int i = 0; i < grid_->count(); ++i) grid_->item(i)->setCheckState(Qt::Unchecked); });
    l->addWidget(sectionLabel(tr("输出文件")));
    nameLbl_ = noteLabel();
    nameLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(nameLbl_);
    l->addWidget(noteLabel(tr("PDF 为 1:1 实际尺寸，打印时请选择【实际大小 / 100%】，切勿选择【适应页面】。")));
    l->addStretch(1);
    exportBtn_ = primaryButton(tr("导出 PDF"));
    connect(exportBtn_, &QPushButton::clicked, this, &MainWindow::onExport);
    l->addWidget(exportBtn_);
    auto* back = plainButton(tr("返回修改"));
    connect(back, &QPushButton::clicked, this, [this] { goStep(1); });
    l->addWidget(back);
    return w;
}

QWidget* MainWindow::buildCanvasPage() {
    auto* page = new QWidget;
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    auto* bar = new QWidget;
    bar->setFixedHeight(42);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 0, 16, 0);
    fileInfo_ = new QLabel;
    cursorInfo_ = new QLabel;
    cursorInfo_->setMinimumWidth(120);
    fileInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    cvHint_ = new QLabel;
    cvHint_->setProperty("note", true);
    auto* fit = plainButton(tr("适应窗口"));
    auto* actual = plainButton(tr("100%"));
    actual->setToolTip(tr("实际大小（按屏幕 DPI 1:1 显示）"));
    fit->setFlat(true); actual->setFlat(true);
    bl->addWidget(fileInfo_, 1);
    bl->addWidget(cursorInfo_);
    bl->addWidget(cvHint_);
    bl->addWidget(fit);
    bl->addWidget(actual);
    l->addWidget(bar);
    canvas_ = new CanvasWidget;
    auto* holder = new QWidget;
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(16, 0, 16, 12);
    hl->addWidget(canvas_);
    canvas_->setObjectName("canvas");
    l->addWidget(holder, 1);
    connect(fit, &QPushButton::clicked, canvas_, &CanvasWidget::fitWindow);
    connect(actual, &QPushButton::clicked, canvas_, &CanvasWidget::actualSize);

    thumbStripBox_ = new QWidget;
    thumbStripBox_->setObjectName("thumbs");
    thumbStripBox_->setFixedHeight(164);
    auto* tl = new QHBoxLayout(thumbStripBox_);
    tl->setContentsMargins(16, 8, 16, 8);
    strip_ = new QListWidget;
    strip_->setViewMode(QListView::IconMode);
    strip_->setFlow(QListView::LeftToRight);
    strip_->setWrapping(false);
    strip_->setMovement(QListView::Static);
    strip_->setIconSize(QSize(118, 84));
    strip_->setSpacing(8);
    strip_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    strip_->setFrameShape(QFrame::NoFrame);
    tl->addWidget(strip_, 1);
    auto* printTip = noteLabel(tr("打印时请选择「实际大小 / 100%」，勿选「适应页面」。"));
    printTip->setMaximumWidth(190);
    tl->addWidget(printTip);
    l->addWidget(thumbStripBox_);
    l->addSpacing(0);

    connect(strip_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= meta_.size()) return;
        const PageMeta& m = meta_[row];
        if (m.row >= 0) canvas_->focusCell(m.row, m.col); else canvas_->fitWindow();
    });
    connect(canvas_, &CanvasWidget::cursorMm, this, [this](QPointF uv, bool inside) {
        if (!inside || !full_) { cursorInfo_->clear(); return; }
        const plt::Document& d = *(step_ == 0 ? full_ : work_);
        cursorInfo_->setText(tr("X %1  Y %2 mm").arg(uv.x() + d.bounds.minX, 0, 'f', 1).arg(d.bounds.maxY - uv.y(), 0, 'f', 1));
    });
    connect(canvas_, &CanvasWidget::dragBegan, this, [this] { dragBase_ = s_; dragPushed_ = false; });
    connect(canvas_, &CanvasWidget::offsetEdited, this, [this](QPointF mm, bool final) {
        if (!dragPushed_ && (mm.x() != dragBase_.offX || mm.y() != dragBase_.offY)) {
            undo_.push_back(dragBase_);
            redo_.clear();
            lastKey_.clear();
            dragPushed_ = true;
        }
        s_.offX = mm.x();
        s_.offY = mm.y();
        syncUi();
        updateGridLocal();
        requestPlan();
        saveTimer_.start();
        Q_UNUSED(final);
    });
    connect(canvas_, &CanvasWidget::nudged, this, [this](double dx, double dy) {
        edit([dx, dy](State& s) { s.offX = std::max(0.0, s.offX + dx); s.offY = std::max(0.0, s.offY + dy); }, "nudge");
    });
    connect(canvas_, &CanvasWidget::selectionEdited, this, [this](bool has, QRectF uv, bool) {
        edit([has, uv](State& s) { s.hasSel = has; s.sel = uv; s.offX = s.offY = 0; });
    });
    connect(canvas_, &CanvasWidget::tipApply, this, [this] {
        if (!last_.hint.found) return;
        const auto h = last_.hint;
        edit([h](State& s) { s.offX = std::max(0.0, s.offX + h.dx); s.offY = std::max(0.0, s.offY + h.dy); });
    });
    connect(canvas_, &CanvasWidget::tipDismissed, this, [this] { dismissLarger_ = true; updateTip(); });
    return page;
}

QWidget* MainWindow::buildPreviewPage() {
    auto* page = new QWidget;
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(16, 12, 16, 12);
    auto* head = new QLabel(tr("预览全部页面 — 勾选需要导出的页（双击缩略图可回到画布定位）"));
    head->setProperty("lb", true);
    l->addWidget(head);
    grid_ = new QListWidget;
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setIconSize(QSize(220, 160));
    grid_->setSpacing(12);
    grid_->setUniformItemSizes(true);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setObjectName("grid");
    l->addWidget(grid_, 1);
    connect(grid_, &QListWidget::itemChanged, this, [this] { updateSelCount(); });
    connect(grid_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int row = grid_->row(it);
        goStep(1);
        if (row >= 0 && row < meta_.size() && meta_[row].row >= 0) canvas_->focusCell(meta_[row].row, meta_[row].col);
    });
    return page;
}

void MainWindow::applyStyle() {
    qApp->setStyleSheet(R"(
QWidget { font-size: 13px; color:#1F2A2A; }
QMainWindow, QWidget#side, QScrollArea, QStackedWidget { background:#F3F6F6; }
QWidget#queueBox { background:#fff; border-right:1px solid #DDE3E3; }
QWidget#top { background:#fff; border-bottom:1px solid #DDE3E3; }
QLabel#logo { font-weight:700; color:#0B7A70; font-size:15px; }
QPushButton#stepBtn { border:0; padding:6px 16px; border-radius:16px; color:#556666; background:transparent; }
QPushButton#stepBtn[state="on"] { background:#0B7A70; color:#fff; font-weight:600; }
QPushButton#stepBtn[state="ok"] { color:#0B7A70; }
QFrame#mode { background:#E4ECEB; border-radius:8px; }
QFrame#mode QPushButton { border:0; padding:5px 12px; border-radius:6px; color:#556666; background:transparent; }
QFrame#mode QPushButton[on="true"] { background:#fff; color:#1F2A2A; font-weight:600; }
QWidget#panel { background:#fff; }
QWidget#side { border-right:1px solid #DDE3E3; }
QWidget#side[right="true"] { border-right:0; border-left:1px solid #DDE3E3; }
QFrame#card { background:#fff; border:1px solid #DDE3E3; border-radius:8px; }
QFrame#warn { background:#FFF6DD; border:1px solid #F0D58A; border-radius:8px; }
QWidget#bottomBar { background:#fff; border-top:1px solid #DDE3E3; }
QTreeWidget { background:#fff; border:1px solid #DDE3E3; border-radius:8px; alternate-background-color:#F7FAF9; }
QTreeWidget::item { padding:2px 4px; }
QHeaderView::section { background:#F3F6F6; border:0; border-bottom:1px solid #DDE3E3; padding:6px; font-weight:600; color:#556666; }
QComboBox { padding:5px 8px; border:1px solid #C3CECD; border-radius:6px; background:#fff; }
QLabel[lb="true"] { font-size:12px; color:#556666; font-weight:600; }
QLabel[note="true"] { font-size:12px; color:#556666; }
QPushButton { padding:7px 12px; border:1px solid #C3CECD; border-radius:8px; background:#fff; }
QPushButton:hover { border-color:#0B7A70; }
QPushButton:disabled { color:#9AA9A8; background:#F3F6F6; }
QPushButton[seg="true"] { padding:8px 0; }
QPushButton[seg="true"]:checked { border-color:#0B7A70; background:#E6F5F3; color:#0B7A70; font-weight:700; }
QPushButton[pri="true"] { background:#0B7A70; border-color:#0B7A70; color:#fff; font-weight:700; padding:10px 14px; }
QPushButton[pri="true"]:disabled { background:#9FC9C4; border-color:#9FC9C4; color:#fff; }
QPushButton:checked { border-color:#0B7A70; background:#E6F5F3; }
QPushButton[link="true"] { border:0; background:transparent; color:#0B6A61; font-weight:600; padding:2px 6px; }
QPushButton:flat { border:0; background:transparent; color:#0B6A61; }
QLineEdit { padding:5px 6px; border:1px solid #C3CECD; border-radius:6px; background:#fff; }
QDoubleSpinBox { padding:4px 4px; }
QSlider::groove:horizontal { height:4px; background:#C3CECD; border-radius:2px; }
QSlider::sub-page:horizontal { background:#0B7A70; border-radius:2px; }
QSlider::handle:horizontal { width:14px; margin:-6px 0; border-radius:7px; background:#0B7A70; }
QProgressBar { background:#E4ECEB; border:0; border-radius:3px; }
QProgressBar::chunk { background:#8FD0C7; }
QFrame#tip { background:#fff; border:1px solid #BFE3DE; border-radius:8px; }
QFrame#tip QLabel { color:#0B6A61; background:transparent; }
QWidget#thumbs { background:#fff; border-top:1px solid #DDE3E3; }
QListWidget { background:transparent; }
QListWidget::item { border:1px solid #C3CECD; border-radius:4px; background:#fff; color:#1F2A2A; padding:4px; }
QListWidget::item:selected { border:2px solid #0B7A70; color:#0B6A61; }
QListWidget#grid { background:#E6ECEB; border-radius:10px; }
QScrollArea QLabel { background:transparent; }
)");
    QFont f = qApp->font();
    f.setFamilies({"Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", "WenQuanYi Zen Hei", "sans-serif"});
    qApp->setFont(f);
}

// ============================================================================ state flow

void MainWindow::edit(const std::function<void(State&)>& fn, const QString& key) {
    State n = s_;
    fn(n);
    if (n == s_) return;
    const bool coalesce = !key.isEmpty() && key == lastKey_ && editClock_.elapsed() < 1000;
    if (!coalesce) {
        undo_.push_back(s_);
        if (undo_.size() > 100) undo_.pop_front();  // PRD: >= 20 steps
    }
    redo_.clear();
    lastKey_ = key;
    editClock_.restart();
    applyState(n);
}

void MainWindow::undo() {
    if (undo_.isEmpty()) return;
    redo_.push_back(s_);
    State n = undo_.takeLast();
    lastKey_.clear();
    applyState(n);
}

void MainWindow::redo() {
    if (redo_.isEmpty()) return;
    undo_.push_back(s_);
    State n = redo_.takeLast();
    lastKey_.clear();
    applyState(n);
}

void MainWindow::applyState(const State& n) {
    const State old = s_;
    s_ = n;
    if (raw_) {
        const bool g = !old.sameGeometryAs(n), sel = !old.sameSelectionAs(n);
        if (g) rebuildFull();
        if (g || sel) rebuildWork();
        if (g || sel) refreshCanvasView();
    }
    syncUi();
    updateGridLocal();
    requestPlan();
    if (batch_) onBatchEdited();
    saveTimer_.start();
}

void MainWindow::rebuildFull() {
    full_ = buildFullDoc(*raw_, s_, &frameRemoved_);
    fullView_ = makeView(*full_);
}

void MainWindow::rebuildWork() {
    if (!s_.hasSel) { work_ = full_; workView_ = fullView_; return; }
    auto w = cropWorkDoc(full_, s_);
    if (!w) {
        s_.hasSel = false;
        work_ = full_;
        workView_ = fullView_;
        if (!silent_) QTimer::singleShot(0, this, [this] { QMessageBox::information(this, tr("框选区域"), tr("选区内没有任何线条，已取消框选。")); });
        return;
    }
    work_ = w;
    workView_ = makeView(*work_);
}

void MainWindow::refreshCanvasView() {
    if (!full_) { canvas_->setView(nullptr); return; }
    canvas_->setMode(step_ == 0 ? CanvasWidget::Import : CanvasWidget::Layout);
    canvas_->setView(step_ == 0 ? fullView_ : workView_);
    canvas_->setSelection(step_ == 0 && s_.hasSel, s_.sel);
    canvas_->setOffset(QPointF(s_.offX, s_.offY));
    updateInfoLabels();
}

void MainWindow::updateGridLocal() {
    CanvasWidget::Grid g;
    double Wp, Hp, gw, gh;
    int nx, ny;
    double strip = 0;
    if (work_ && gridGeom(s_, *work_, Wp, Hp, nx, ny, gw, gh, &strip)) {
        g.valid = true; g.Wp = Wp; g.Hp = Hp; g.strip = strip; g.nx = nx; g.ny = ny; g.singlePage = s_.singlePage;
        g.allPresent = true;
        if (havePlan_ && last_.nx == nx && last_.ny == ny && !s_.singlePage) {
            g.allPresent = false;
            for (const auto& m : meta_) if (m.row >= 0) g.present.insert(m.row * 10000 + m.col);
        }
    }
    canvas_->setGrid(g);
    canvas_->setOffset(QPointF(s_.offX, s_.offY));
}

void MainWindow::syncUi() {
    auto setSpin = [](QDoubleSpinBox* b, double v) { QSignalBlocker x(b); b->setValue(v); };
    auto setSw = [](ToggleSwitch* b, bool v) { QSignalBlocker x(b); b->setChecked(v); };
    const bool single = s_.singlePage;
    const bool loadedDoc = raw_ != nullptr;

    // step bar
    {
        const int count = batch_ ? 5 : 3, cur = batch_ ? bstep_ : step_;
        for (int i = 0; i < 5; ++i) {
            stepBtn_[i]->setVisible(i < count);
            stepBtn_[i]->setProperty("state", i == cur ? "on" : (i < cur ? "ok" : ""));
            stepBtn_[i]->style()->unpolish(stepBtn_[i]);
            stepBtn_[i]->style()->polish(stepBtn_[i]);
            stepBtn_[i]->setEnabled(batch_ ? i <= maxBatchStep() : (i == 0 || loadedDoc));
        }
        bu_.modeSingle->setProperty("on", !batch_);
        bu_.modeBatch->setProperty("on", batch_);
        for (auto* b : {bu_.modeSingle, bu_.modeBatch}) { b->style()->unpolish(b); b->style()->polish(b); }
    }
    // import panel
    { QSignalBlocker b(ignoreFrame_); ignoreFrame_->setChecked(s_.ignoreFrame); }
    ignoreFrame_->setEnabled(loadedDoc);
    if (!loadedDoc || !s_.ignoreFrame) frameNote_->setText(hasFrame_ ? tr("已检测到纸张边框，勾选后移除。") : QString());
    else if (frameRemoved_ > 0) frameNote_->setText(tr("已移除 %1 条边框线，图形尺寸已按剩余内容重算。").arg(frameRemoved_));
    else frameNote_->setText(tr("没有识别到可移除的纸张边框（只识别与图形外框重合的矩形线）。"));
    { QSignalBlocker b1(clipRadio_), b2(wholeRadio_); clipRadio_->setChecked(s_.selClip); wholeRadio_->setChecked(!s_.selClip); }
    xformNote_->setVisible(s_.rot != 0 || s_.mirror);
    resetXformBtn_->setEnabled(s_.rot != 0 || s_.mirror);
    clearSelBtn_->setEnabled(s_.hasSel);
    selBtn_->setEnabled(loadedDoc);
    selInfo_->setText(s_.hasSel ? tr("选区 %1 × %2 mm").arg(s_.sel.width(), 0, 'f', 1).arg(s_.sel.height(), 0, 'f', 1)
                                : (loadedDoc ? tr("未框选：将排版整个图形。") : QString()));
    toLayoutBtn_->setEnabled(loadedDoc);
    toLayoutBtn_->setVisible(!batch_);
    openBtn_->setVisible(!batch_);
    // in the batch per-file preview the file info is in the header and files are managed in the queue,
    // so the panel only keeps the graphic controls and fits on one screen
    impDrag_->setVisible(!batch_);
    impInfo_->setVisible(!batch_);
    if (auto* tb = leftStack_->findChild<QPushButton*>("toBatchBtn")) tb->setVisible(!batch_);

    // layout panel
    const int pid = kPapers.indexOf(s_.paper);
    if (pid >= 0) paperGrp_->button(pid)->setChecked(true);
    orientGrp_->button(s_.landscape ? 1 : 0)->setChecked(true);
    { QSignalBlocker b(autoOrient_); autoOrient_->setChecked(s_.autoOrient); }
    paperSeg_->setEnabled(!single);
    orientRow_->setEnabled(!single);
    if (single) {
        paperNote_->setText(tr("单页输出：页面 = 图形尺寸 + 2×边距（上限约 5000 mm）。"));
    } else {
        const auto pa = layout::paperByName(s_.paper.toStdString());
        paperNote_->setText(tr("%1 · %2×%3 mm，1:1 实际尺寸输出").arg(s_.paper).arg(pa.w).arg(pa.h));
    }
    if (last_.orientEvaluated && s_.autoOrient && havePlan_)
        orientNote_->setText(tr("竖向 %1 页 / 横向 %2 页").arg(last_.oc.portraitPages).arg(last_.oc.landscapePages));
    else
        orientNote_->clear();
    { QSignalBlocker b(overlapSlider_); overlapSlider_->setValue(int(s_.overlap + 0.5)); }
    setSpin(overlapSpin_, s_.overlap);
    overlapSpin_->setEnabled(!single);
    overlapSlider_->setEnabled(!single);
    setSpin(marginSpin_, s_.margin);
    {
        int mid = -1;
        for (int i = 0; i < 4; ++i) if (s_.margin == kMargins[i]) mid = i;
        if (mid >= 0) marginGrp_->button(mid)->setChecked(true);
        else if (auto* c = marginGrp_->checkedButton()) { marginGrp_->setExclusive(false); c->setChecked(false); marginGrp_->setExclusive(true); }
    }
    setSw(singleSw_, s_.singlePage);
    setSw(pageNoSw_, s_.pageNumbers); pageNoSw_->setEnabled(!single);
    setSw(alignSw_, s_.alignMarks); alignSw_->setEnabled(!single);
    {
        int idx = 5;
        for (int i = 0; i < 5; ++i) if (s_.alignColor == kAlignPresets[i]) idx = i;
        if (idx == 5) alignSwatches_[5]->setColor(s_.alignColor);
        alignSwatches_[idx]->setChecked(true);
        for (auto* b : alignSwatches_) b->setEnabled(s_.alignMarks && !single);
    }
    setSw(calibSw_, s_.calibBox);
    setSw(overviewSw_, s_.overview && !single); overviewSw_->setEnabled(!single);
    setSw(blankSw_, s_.keepBlank); blankSw_->setEnabled(!single);
    calibSpin_->setEnabled(s_.calibBox);
    setSpin(calibSpin_, s_.calibCm);
    setSpin(lineWidthSpin_, s_.lineWidth);
    {
        int idx = 6;
        if (s_.origColor) idx = 0;
        else for (int i = 0; i < 5; ++i) if (s_.color == kPresets[i]) idx = i + 1;
        if (idx == 6) swatches_[6]->setColor(s_.color);
        swatches_[idx]->setChecked(true);
    }
    if (!raw_) colorNote_->clear();
    else if (!s_.origColor) colorNote_->setText(tr("所有线条统一使用所选颜色。"));
    else if (raw_->penColors.empty()) colorNote_->setText(tr("此文件没有笔色信息，「原色」按黑色输出。"));
    else colorNote_->setText(tr("此文件定义了 %1 种笔色，按原色输出（近白色会印成浅灰）。").arg(raw_->penColors.size()));
    setSpin(xSpin_, s_.offX);
    setSpin(ySpin_, s_.offY);
    for (QWidget* b : std::initializer_list<QWidget*>{xSpin_, ySpin_, alignTL_, center_, autoPos_}) b->setEnabled(!single && loadedDoc);
    canvas_->setLineStyle(s_.origColor, s_.color, s_.lineWidth);
    updateInfoLabels();
}

void MainWindow::updateInfoLabels() {
    if (!full_) { fileInfo_->setText(tr("尚未导入文件")); cvHint_->clear(); return; }
    const plt::Document& d = *(step_ == 0 ? full_ : work_);
    fileInfo_->setText(QStringLiteral("<b>%1</b> · %2×%3 mm · %4").arg(QFileInfo(path_).fileName().toHtmlEscaped()).arg(d.bounds.w(), 0, 'f', 1).arg(d.bounds.h(), 0, 'f', 1).arg(tr("线条 %1 条").arg(d.lines.size())));
    fileInfo_->setTextFormat(Qt::RichText);
    if (batch_) { cvHint_->clear(); return; }   // the batch column is narrow: no room for the hint
    cvHint_->setText(step_ == 0 ? tr("滚轮缩放 · 右键平移")
                                : tr("拖拽移动图形 · 方向键微调"));
}

// ---------------------------------------------------------------- planning

void MainWindow::requestPlan() {
    if (!work_) return;
    planTimer_.start();
}

void MainWindow::runPlan() {
    if (!work_) return;
    ++gen_;
    layout::Params p;
    try { p = currentParams(); } catch (...) { return; }
    auto* w = new QFutureWatcher<PlanResult>(this);
    connect(w, &QFutureWatcher<PlanResult>::finished, this, [this, w] {
        PlanResult r = w->result();
        w->deleteLater();
        onPlanResult(r);
    });
    w->setFuture(QtConcurrent::run(computePlan, gen_, std::shared_ptr<const plt::Document>(work_), p, s_.autoOrient, dismissLarger_));
}

void MainWindow::onPlanResult(const PlanResult& r) {
    if (r.gen != gen_) return;  // stale: the user changed something while this ran
    last_ = r;
    havePlan_ = r.err.isEmpty();
    meta_ = r.meta;
    thumbs_ = r.thumbs;
    if (havePlan_ && s_.autoOrient && !s_.singlePage) s_.landscape = r.landscape;
    updateGridLocal();
    syncUi();
    updateTip();
    rebuildThumbLists();
    updateSummary();
    if (batch_ && cur_ >= 0 && cur_ < items_.size()) {
        BatchItem& it = *items_[cur_];
        if (havePlan_) { it.pages = meta_.size(); it.hasHint = last_.hint.found; }
        it.state = s_;
        refreshStatus(it);
        updateBatchInfo();
        refreshRow(cur_);
        refreshBatchSummary();
    }
    emit planReady();
}

void MainWindow::updateTip() {
    if (step_ != 1 || !work_) { canvas_->setTip({}, false, false); return; }
    if (!last_.err.isEmpty()) { canvas_->setTip(last_.err, false, false); return; }
    if (!havePlan_) { canvas_->setTip(tr("正在计算分页…"), false, false); return; }
    QString t;
    if (s_.singlePage) {
        t = tr("单页输出 · 页面 %1 × %2 mm").arg(last_.pageW, 0, 'f', 1).arg(last_.pageH, 0, 'f', 1);
    } else {
        t = tr("共 %1 页（%2×%3）").arg(last_.effective).arg(last_.nx).arg(last_.ny);
        if (last_.blank > 0 && !s_.keepBlank) t += tr(" · 空白 %1 页不输出").arg(last_.blank);
        if (s_.overview) t += tr(" · 另含拼接效果图 1 页");
        if (last_.orientEvaluated && last_.oc.portraitPages != last_.oc.landscapePages) {
            const bool landscapeBetter = last_.oc.landscapePages < last_.oc.portraitPages;
            t += landscapeBetter ? tr(" · 提示：竖向需 %1 页，当前横向更省纸").arg(last_.oc.portraitPages)
                                 : tr(" · 提示：横向需 %1 页，当前竖向更省纸").arg(last_.oc.landscapePages);
        }
    }
    bool apply = false, dismiss = false;
    if (last_.calibCollision && s_.calibBox) {
        t += tr("\n⚠ 校准框四个角均与图形冲突，已画在右下角，请留意。");
    } else if (last_.hint.found) {
        const auto& h = last_.hint;
        QString dir;
        if (h.dx < 0) dir += tr("向左移动 %1 mm").arg(-h.dx);
        else if (h.dx > 0) dir += tr("向右移动 %1 mm").arg(h.dx);
        if (h.dy < 0) dir += (dir.isEmpty() ? "" : "、") + tr("向上移动 %1 mm").arg(-h.dy);
        else if (h.dy > 0) dir += (dir.isEmpty() ? "" : "、") + tr("向下移动 %1 mm").arg(h.dy);
        t += tr("\n💡 %1 可由 %2 页减至 %3 页").arg(dir).arg(h.from).arg(h.to);
        apply = true;
    } else if (!last_.largerPaper.isEmpty() && !dismissLarger_) {
        t += tr("\n💡 切换至 %1 可减少 %2 页").arg(last_.largerPaper).arg(last_.largerGain);
        dismiss = true;
    }
    canvas_->setTip(t, apply, dismiss);
}

void MainWindow::updateSummary() {
    if (!work_) { summary_->clear(); return; }
    QString t;
    if (!havePlan_) { summary_->setText(last_.err.toHtmlEscaped()); exportBtn_->setEnabled(false); return; }
    if (s_.singlePage) {
        t = tr("单页输出<br>页面尺寸 %1 × %2 mm").arg(last_.pageW, 0, 'f', 1).arg(last_.pageH, 0, 'f', 1);
    } else {
        t = tr("纸张 <b>%1 %2</b><br>网格 %3 × %4 · 有效页 <b>%5</b> 页").arg(s_.paper, s_.landscape ? tr("横向") : tr("竖向")).arg(last_.nx).arg(last_.ny).arg(last_.effective);
        if (last_.blank > 0) t += tr("<br>空白页 %1 页（%2）").arg(last_.blank).arg(s_.keepBlank ? tr("已包含在 PDF 中，可在预览取消勾选") : tr("不输出"));
        if (s_.overview) t += tr("<br>含拼接效果图 1 页");
        t += tr("<br>PDF 共 <b>%1</b> 页").arg(meta_.size());
    }
    summary_->setText(t);
    updateSelCount();
}

void MainWindow::rebuildThumbLists() {
    QSignalBlocker b1(strip_), b2(grid_);
    const int keepRow = strip_->currentRow();
    strip_->clear();
    grid_->clear();
    for (int i = 0; i < meta_.size(); ++i) {
        const PageMeta& m = meta_[i];
        QString cap = m.overview ? tr("拼接效果图") : tr("%1 · 第 %2 页").arg(m.label).arg(i + 1);
        if (m.empty && !m.overview) cap += tr("（空白）");
        if (last_.calibCollision && s_.calibBox && m.label == "1-1") cap += tr(" ⚠校准框冲突");
        QIcon icon;
        if (i < thumbs_.size() && !thumbs_[i].isNull()) icon = QIcon(QPixmap::fromImage(thumbs_[i]));
        auto* a = new QListWidgetItem(icon, cap, strip_);
        a->setSizeHint(QSize(140, 122));
        a->setTextAlignment(Qt::AlignHCenter);
        auto* g = new QListWidgetItem(icon, cap, grid_);
        g->setSizeHint(QSize(240, 210));
        g->setTextAlignment(Qt::AlignHCenter);
        g->setFlags(g->flags() | Qt::ItemIsUserCheckable);
        g->setCheckState(Qt::Checked);  // blank pages only appear when 「输出空白页」 is on -> they are exported by default
        if (m.empty && !m.overview) { a->setForeground(QColor("#8A9796")); g->setForeground(QColor("#8A9796")); }
    }
    if (keepRow >= 0 && keepRow < strip_->count()) strip_->setCurrentRow(keepRow);
    updateSelCount();
}

QVector<int> MainWindow::checkedPages() const {
    QVector<int> v;
    for (int i = 0; i < grid_->count(); ++i)
        if (grid_->item(i)->checkState() == Qt::Checked) v.push_back(i);
    return v;
}

void MainWindow::updateSelCount() {
    const int n = checkedPages().size(), total = grid_->count();
    selCount_->setText(total ? tr("已选 %1 / %2 页").arg(n).arg(total) : QString());
    exportBtn_->setEnabled(havePlan_ && n > 0);
    bool trunc = false;
    const QString name = path_.isEmpty() ? QString() : defaultOutputName(&trunc);
    nameLbl_->setText(name.isEmpty() ? QString() : name + (trunc ? tr("\n（完整路径超过 240 字符，已截断原文件名）") : QString()));
}

void MainWindow::applyPageRange() {
    const QString txt = rangeEdit_->text().trimmed();
    rangeErr_->clear();
    rangeErr_->hide();
    const int total = grid_->count();
    if (txt.isEmpty()) { for (int i = 0; i < total; ++i) grid_->item(i)->setCheckState(Qt::Checked); return; }
    std::string err;
    const auto pages = pagerange::parse(txt.toUtf8().toStdString(), total, &err);
    if (pages.empty()) {
        rangeErr_->setText(err == "out of range" ? tr("页码超出范围（1–%1）").arg(total) : tr("页码范围格式不正确，示例：1-3,5"));
        rangeErr_->show();
        return;
    }
    QSet<int> want(pages.begin(), pages.end());
    for (int i = 0; i < total; ++i) grid_->item(i)->setCheckState(want.contains(i + 1) ? Qt::Checked : Qt::Unchecked);
}

// ---------------------------------------------------------------- steps

void MainWindow::goStep(int step) {
    if (batch_) { goBatchStep(step); return; }
    if (step > 0 && !raw_) {
        QMessageBox::information(this, tr("提示"), tr("请先导入 PLT 文件。"));
        return;
    }
    step_ = step;
    leftStack_->setCurrentIndex(step);
    centerStack_->setCurrentIndex(step == 2 ? 1 : 0);
    thumbStripBox_->setVisible(step == 1);
    canvas_->setSelectTool(step == 0 && selBtn_->isChecked());
    if (raw_) {
        refreshCanvasView();
        updateGridLocal();
    } else {
        canvas_->setView(nullptr);
    }
    syncUi();
    updateTip();
    if (raw_) requestPlan();
}

// ---------------------------------------------------------------- import

void MainWindow::chooseFile() {
    if (batch_) { batchAddDialog(false); return; }
    const QString f = QFileDialog::getOpenFileName(this, tr("选择 PLT 文件"), path_.isEmpty() ? QString() : QFileInfo(path_).absolutePath(), tr("PLT 文件 (*.plt *.PLT);;所有文件 (*)"));
    if (!f.isEmpty()) loadFile(f);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    if (batch_) {
        QStringList all;
        for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) all << u.toLocalFile();
        addBatchFiles(all);
        return;
    }
    QStringList plts;
    int other = 0;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString f = u.toLocalFile();
        if (f.endsWith(".plt", Qt::CaseInsensitive)) plts << f; else ++other;
    }
    if (plts.isEmpty()) { QMessageBox::warning(this, tr("无法导入"), tr("仅支持 .plt 文件（已忽略 %1 个其他文件）。").arg(other)); return; }
    if (plts.size() > 1) QMessageBox::information(this, tr("提示"), tr("单文件模式一次只处理一个文件，已导入第一个：%1。\n多个文件请使用「批量转换」（开发中）。").arg(QFileInfo(plts[0]).fileName()));
    loadFile(plts[0]);
}

void MainWindow::loadFile(const QString& path) {
    if (parsing_) return;
    if (!path.endsWith(".plt", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("无法导入"), tr("仅支持 .plt 文件。"));
        emit loaded(false);
        return;
    }
    parsing_ = true;
    busy_->show();
    openBtn_->setEnabled(false);
    impInfo_->setText(tr("正在解析 %1 …").arg(QFileInfo(path).fileName().toHtmlEscaped()));
    auto* w = new QFutureWatcher<ParseResult>(this);
    connect(w, &QFutureWatcher<ParseResult>::finished, this, [this, w, path] {
        ParseResult r = w->result();
        w->deleteLater();
        onParsed(r, path);
    });
    w->setFuture(QtConcurrent::run(parseJob, path));
}

void MainWindow::onParsed(const ParseResult& r, const QString& path) {
    parsing_ = false;
    busy_->hide();
    openBtn_->setEnabled(true);
    if (!r.doc) {
        impInfo_->clear();
        QMessageBox::warning(this, tr("无法导入"), tr("%1\n\n%2").arg(QFileInfo(path).fileName(), r.err.isEmpty() ? tr("文件无法解析。") : r.err));
        updateInfoLabels();
        emit loaded(false);
        return;
    }
    raw_ = r.doc;
    path_ = path;
    fileSize_ = r.size;
    hasFrame_ = r.frameIdx >= 0;
    // keep the user's layout preferences, reset everything tied to the drawing
    State n = s_;
    n.ignoreFrame = false; n.rot = 0; n.mirror = false; n.hasSel = false; n.offX = n.offY = 0; n.autoOrient = true;
    s_ = n;
    undo_.clear(); redo_.clear(); lastKey_.clear();
    dismissLarger_ = false;
    havePlan_ = false;
    last_ = PlanResult();
    meta_.clear(); thumbs_.clear();
    rebuildFull();
    rebuildWork();

    // info + notices (PRD 1.A / 第7章)
    const plt::Document& d = *raw_;
    impInfo_->setText(tr("<b>%1</b><br>大小 %2<br>物理尺寸 %3 × %4 mm<br>线条 %5 条 · 线段 %6 · 总长 %7 m")
                          .arg(QFileInfo(path).fileName().toHtmlEscaped(), humanSize(fileSize_)).arg(d.bounds.w(), 0, 'f', 1).arg(d.bounds.h(), 0, 'f', 1)
                          .arg(d.lines.size()).arg(d.segmentCount).arg(d.totalLengthMm / 1000, 0, 'f', 2));
    notices_.clear();
    if (hasFrame_) notices_ << tr("检测到纸张边框（门幅边框）。可勾选「忽略纸张边框」移除并重算尺寸。");
    if (d.unsupported.count("LB")) notices_ << tr("检测到 %1 处文字标注（LB）未渲染，其余内容不受影响。").arg(d.unsupported.at("LB"));
    int skipped = 0;
    QStringList names;
    for (const auto& u : d.unsupported) if (u.first != "LB") { skipped += u.second; names << QStringLiteral("%1×%2").arg(QString::fromStdString(u.first)).arg(u.second); }
    if (skipped > 0) notices_ << tr("已跳过 %1 条暂不支持的指令（%2）。").arg(skipped).arg(names.join("、"));
    for (const auto& wmsg : d.warnings)
        if (wmsg.rfind("IP/SC", 0) == 0) notices_ << tr("检测到 IP/SC 缩放指令，当前按默认 40 单位/mm 换算，请核对尺寸。");
    if (d.bounds.w() < 1 && d.bounds.h() < 1) notices_ << tr("图形尺寸过小（< 1 mm），仍可输出。");
    const double far = std::max({std::fabs(d.bounds.minX), std::fabs(d.bounds.maxX), std::fabs(d.bounds.minY), std::fabs(d.bounds.maxY)});
    if (far > 10000) notices_ << tr("图形远离原点或含极端坐标（> 10 m），可能是单位识别错误。");
    if (d.segmentCount > 500000) notices_ << tr("大文件（线段 > 50 万），渲染与导出可能较慢。");
    QString html;
    for (const auto& s : notices_) html += QStringLiteral("• %1<br>").arg(s.toHtmlEscaped());
    impNotes_->setText(html);
    impNotes_->setVisible(!html.isEmpty());

    canvas_->setView(nullptr);  // force re-fit for the new drawing
    selBtn_->setChecked(false);
    goStep(0);
    refreshCanvasView();
    canvas_->fitWindow();
    syncUi();
    requestPlan();
    emit loaded(true);
}

// ---------------------------------------------------------------- export

QString MainWindow::defaultOutputName(bool* truncated) const {
    const QFileInfo fi(path_);
    QString stem = fi.completeBaseName();
    QDir dir(fi.absolutePath());
    auto exists = [&](const std::string& n) { return QFileInfo::exists(dir.filePath(QString::fromUtf8(n.c_str()))); };
    auto make = [&](const QString& st) {
        const std::string u = st.toUtf8().toStdString();
        return s_.singlePage ? naming::uniquePdfNameTagged(u, "单页", exists)
                             : naming::uniquePdfName(u, s_.paper.toStdString(), s_.landscape ? layout::Orient::Landscape : layout::Orient::Portrait, exists);
    };
    QString name = QString::fromUtf8(make(stem).c_str());
    if (truncated) *truncated = false;
    if (dir.filePath(name).size() > 240) {  // PRD 4.5.1: cut the original name, keep the suffix
        int keep = std::max(8, int(stem.size() - (dir.filePath(name).size() - 240)));
        stem = stem.left(keep);
        name = QString::fromUtf8(make(stem).c_str());
        if (truncated) *truncated = true;
    }
    return name;
}

QString MainWindow::exportBlocking(const QString& path) {
    QVector<int> pages = checkedPages();
    if (pages.isEmpty()) for (int i = 0; i < meta_.size(); ++i) pages.push_back(i);
    return exportJob(work_, currentParams(), pages, path, {});
}

QString MainWindow::exportWithProgress(const QString& out, const QVector<int>& pages) {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    QProgressDialog dlg(tr("正在导出 PDF…"), tr("取消"), 0, 0, this);
    dlg.setWindowTitle(tr("导出"));
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    connect(&dlg, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    dlg.show();
    QPointer<QProgressDialog> pd(&dlg);
    auto progress = [cancel, pd](int i, int n) {
        QMetaObject::invokeMethod(qApp, [pd, i, n] { if (pd) { pd->setMaximum(n); pd->setValue(i); } }, Qt::QueuedConnection);
        return !cancel->load();
    };
    QFutureWatcher<QString> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(exportJob, std::shared_ptr<const plt::Document>(work_), currentParams(), pages, out, std::function<bool(int, int)>(progress)));
    loop.exec();
    dlg.close();
    return watcher.result();
}

void MainWindow::onExport() {
    if (!havePlan_ || !work_) return;
    const QVector<int> pages = checkedPages();
    if (pages.isEmpty()) { QMessageBox::information(this, tr("导出"), tr("没有选中任何页面。")); return; }
    if (pages.size() >= 20 && QMessageBox::question(this, tr("导出前确认"), tr("将生成 %1 页 PDF，是否继续？").arg(pages.size())) != QMessageBox::Yes) return;
    QString def = QDir(QFileInfo(path_).absolutePath()).filePath(defaultOutputName());
    for (;;) {
        QString out = QFileDialog::getSaveFileName(this, tr("导出 PDF"), def, tr("PDF 文件 (*.pdf)"));
        if (out.isEmpty()) return;
        if (!out.endsWith(".pdf", Qt::CaseInsensitive)) out += ".pdf";
        const QString err = exportWithProgress(out, pages);
        if (err.isEmpty()) { showExportDone(out); updateSelCount(); return; }
        if (err == QStringLiteral("已取消")) { QMessageBox::information(this, tr("导出"), tr("已取消导出，未留下任何文件。")); return; }
        if (QMessageBox::question(this, tr("导出失败"), tr("%1\n\n是否更换保存位置后重试？").arg(err)) != QMessageBox::Yes) return;
        def = out;
    }
}

void MainWindow::showExportDone(const QString& out) {
    QMessageBox box(QMessageBox::Information, tr("导出完成"),
                    tr("已生成：%1\n\nPDF 为 1:1 实际尺寸，打印时请选择【实际大小】或【100%】，切勿选择【适应页面】。").arg(QDir::toNativeSeparators(out)), QMessageBox::NoButton, this);
    auto* openFile = box.addButton(tr("打开文件"), QMessageBox::ActionRole);
    auto* openDir = box.addButton(tr("打开所在文件夹"), QMessageBox::ActionRole);
    box.addButton(tr("关闭"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openFile) QDesktopServices::openUrl(QUrl::fromLocalFile(out));
    else if (box.clickedButton() == openDir) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(out).absolutePath()));
}

// ---------------------------------------------------------------- settings / lifecycle

void MainWindow::loadSettings() {
    QSettings q("HaoyangPattern", "PltConverter");
    State n;
    n.paper = q.value("paper", n.paper).toString();
    if (!kPapers.contains(n.paper)) n.paper = "A4";
    n.margin = q.value("margin", n.margin).toDouble();
    n.overlap = std::clamp(q.value("overlap", n.overlap).toDouble(), 0.0, 30.0);
    n.pageNumbers = q.value("pageNumbers", n.pageNumbers).toBool();
    n.calibBox = q.value("calibBox", n.calibBox).toBool();
    n.alignMarks = q.value("alignMarks", n.alignMarks).toBool();
    const bool v2 = q.value("settingsVersion", 1).toInt() >= 2;   // v1 stored 「拼接效果图」 = off as a side effect; the default is now on
    if (v2) n.overview = q.value("overview", n.overview).toBool();
    n.calibCm = q.value("calibCm", n.calibCm).toDouble();
    n.lineWidth = q.value("lineWidth", n.lineWidth).toDouble();
    n.origColor = q.value("origColor", n.origColor).toBool();
    n.alignColor = QColor(q.value("alignColor", n.alignColor.name()).toString());
    if (!n.alignColor.isValid()) n.alignColor = QColor("#2563EB");
    n.color = QColor(q.value("color", n.color.name()).toString());
    if (!n.color.isValid()) n.color = Qt::black;
    s_ = n;
}

void MainWindow::saveSettings() {
    if (batch_ && bstep_ != 1) return;  // per-file parameters are not the user's defaults
    QSettings q("HaoyangPattern", "PltConverter");
    q.setValue("settingsVersion", 2);
    q.setValue("paper", s_.paper);
    q.setValue("alignColor", s_.alignColor.name());
    q.setValue("margin", s_.margin);
    q.setValue("overlap", s_.overlap);
    q.setValue("pageNumbers", s_.pageNumbers);
    q.setValue("calibBox", s_.calibBox);
    q.setValue("alignMarks", s_.alignMarks);
    q.setValue("overview", s_.overview);
    q.setValue("calibCm", s_.calibCm);
    q.setValue("lineWidth", s_.lineWidth);
    q.setValue("origColor", s_.origColor);
    q.setValue("color", s_.color.name());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (exporting_) {
        if (!ask(tr("仍有任务运行"), tr("批量导出仍在进行。确认后将安全中止并清理未完成的临时文件。\n\n是否中止并退出？"), true)) { e->ignore(); return; }
        batchCancel();
        QEventLoop loop;
        QTimer t;
        t.setInterval(100);
        connect(&t, &QTimer::timeout, &loop, [&] { if (!exporting_) loop.quit(); });
        t.start();
        QTimer::singleShot(8000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    saveSession();
    saveSettings();
    QMainWindow::closeEvent(e);
}
