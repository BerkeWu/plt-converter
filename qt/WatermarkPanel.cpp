#include "WatermarkPanel.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <map>

#include "DrawView.h"

namespace {
const char* kKind[] = {"独立叠加对象", "斜铺文字", "页眉页脚/页码", "整页位图", "同层店铺信息栏"};
QString u8(const std::string& s) { return QString::fromUtf8(s.c_str(), int(s.size())); }
std::string s8(const QString& s) { const QByteArray b = s.toUtf8(); return std::string(b.constData(), size_t(b.size())); }
std::filesystem::path fsPath(const QString& s) { return wm::fromU8(s8(s)); }

QPushButton* mkBtn(const QString& t, bool primary = false) {
    auto* b = new QPushButton(t);
    if (primary) b->setProperty("pri", true);
    return b;
}
QLabel* note(const QString& t) {
    auto* l = new QLabel(t);
    l->setProperty("note", true);
    l->setWordWrap(true);
    return l;
}
QString detectedText(const wm::Report& r) {
    if (!r.ok) return QObject::tr("读取失败");
    QStringList parts;
    for (auto& f : r.findings) {
        if (f.tier == wm::Tier::High) { QString s = QString::fromUtf8(kKind[int(f.kind)]); if (f.contactText) s += QObject::tr("·含联系方式"); if (!parts.contains(s)) parts << s; }
    }
    if (!parts.isEmpty()) return parts.join(" + ");
    for (auto& f : r.findings) if (f.tier == wm::Tier::Unsupported) return QObject::tr("图片水印（已合成）");
    for (auto& f : r.findings) if (f.tier == wm::Tier::Confirm) return QObject::tr("仅有待确认项");
    return QObject::tr("未发现水印");
}
void clearLayout(QLayout* l) {
    while (QLayoutItem* it = l->takeAt(0)) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
}
}  // namespace

// ============================================================================ construction

WatermarkPanel::WatermarkPanel(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTop());
    pages_ = new QStackedWidget;
    pages_->addWidget(buildAddPage());
    pages_->addWidget(buildReviewPage());
    pages_->addWidget(buildRunPage());
    root->addWidget(pages_, 1);
    root->addWidget(buildBottom());
    outDir_.clear();
    syncChrome();
}

WatermarkPanel::~WatermarkPanel() {
    cancel_ = true;
    QThreadPool::globalInstance()->waitForDone(20000);
}

QWidget* WatermarkPanel::buildTop() {
    auto* top = new QWidget;
    top->setObjectName("top");
    top->setFixedHeight(52);
    auto* l = new QHBoxLayout(top);
    l->setContentsMargins(20, 0, 20, 0);
    auto* logo = new QLabel(tr("PLT 排版转换器"));
    logo->setObjectName("logo");
    l->addWidget(logo);
    l->addStretch(1);
    const QString names[3] = {tr("1 添加文件"), tr("2 审阅与配方"), tr("3 运行与报告")};
    for (int i = 0; i < 3; ++i) {
        stepBtn_[i] = new QPushButton(names[i]);
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
    auto* single = new QPushButton(tr("单文件"));
    auto* batch = new QPushButton(tr("批量转换"));
    auto* wmBtn = new QPushButton(tr("水印处理"));
    wmBtn->setProperty("on", true);
    connect(single, &QPushButton::clicked, this, [this] { emit leaveRequested(false); });
    connect(batch, &QPushButton::clicked, this, [this] { emit leaveRequested(true); });
    ml->addWidget(single);
    ml->addWidget(batch);
    ml->addWidget(wmBtn);
    l->addWidget(mode);
    return top;
}

QWidget* WatermarkPanel::buildBottom() {
    auto* bar = new QWidget;
    bar->setObjectName("bottomBar");
    bar->setFixedHeight(60);
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(24, 0, 24, 0);
    info_ = new QLabel;
    l->addWidget(info_, 1);
    back_ = mkBtn(tr("← 上一步"));
    next_ = mkBtn(tr("下一步 →"), true);
    connect(back_, &QPushButton::clicked, this, [this] { goStep(step() - 1); });
    connect(next_, &QPushButton::clicked, this, [this] {
        if (step() == 0) goStep(1);
        else if (step() == 1) startRun();
        else openOutput();
    });
    l->addWidget(back_);
    l->addWidget(next_);
    return bar;
}

QWidget* WatermarkPanel::buildAddPage() {
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(20, 16, 0, 16);
    h->setSpacing(16);
    auto* left = new QVBoxLayout;
    left->setSpacing(12);
    auto* zone = new QFrame;
    zone->setObjectName("tip");
    zone->setFixedHeight(84);
    auto* zl = new QHBoxLayout(zone);
    zl->setContentsMargins(18, 8, 18, 8);
    auto* zt = new QLabel(tr("<b style='font-size:15px'>把同一类水印的多个 PDF 拖到这里</b><br><span style='color:#556666'>也可以拖入整个文件夹 · 一次最多 %1 个 · 文件只在本机处理，原件不会被改动</span>").arg(wm::kMaxBatchFiles));
    zt->setTextFormat(Qt::RichText);
    zl->addWidget(zt, 1);
    auto* addF = mkBtn(tr("选择文件…"));
    auto* addD = mkBtn(tr("选择文件夹…"));
    connect(addF, &QPushButton::clicked, this, [this] {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("选择 PDF"), QString(), tr("PDF (*.pdf)"));
        if (!f.isEmpty()) addFiles(f);
    });
    connect(addD, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
        if (!d.isEmpty()) addFiles({d});
    });
    zl->addWidget(addF);
    zl->addWidget(addD);
    left->addWidget(zone);
    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels({"", tr("文件名"), tr("页数"), tr("检测到的水印"), tr("分组"), tr("状态")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->setColumnWidth(0, 34); table_->setColumnWidth(2, 60); table_->setColumnWidth(4, 70); table_->setColumnWidth(5, 100);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (it->column() != 0) return;
        const int i = it->row();
        if (i >= 0 && i < files_.size()) { files_[i].selected = it->checkState() == Qt::Checked; syncChrome(); }
    });
    left->addWidget(table_, 1);
    limitNote_ = note({});
    left->addWidget(limitNote_);
    h->addLayout(left, 1);

    auto* right = new QWidget;
    right->setObjectName("side");
    right->setProperty("right", true);
    right->setFixedWidth(360);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(16, 0, 16, 0);
    groupsHost_ = new QWidget;
    groupsLay_ = new QVBoxLayout(groupsHost_);
    groupsLay_->setContentsMargins(0, 0, 0, 0);
    groupsLay_->setSpacing(10);
    rl->addWidget(groupsHost_);
    rl->addStretch(1);
    h->addWidget(right);
    return page;
}

QWidget* WatermarkPanel::buildReviewPage() {
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto* left = new QWidget;
    left->setObjectName("side");
    left->setFixedWidth(270);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(14, 14, 14, 14);
    ll->setSpacing(10);
    auto* t = new QLabel(tr("分组与样本"));
    t->setStyleSheet("font-weight:700;font-size:15px");
    ll->addWidget(t);
    groupBtnHost_ = new QWidget;
    groupBtnLay_ = new QVBoxLayout(groupBtnHost_);
    groupBtnLay_->setContentsMargins(0, 0, 0, 0);
    groupBtnLay_->setSpacing(6);
    ll->addWidget(groupBtnHost_);
    auto* st = new QLabel(tr("本组文件（点击切换样本）"));
    st->setProperty("lb", true);
    ll->addWidget(st);
    sampleList_ = new QListWidget;
    connect(sampleList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        const int fi = sampleList_->item(row)->data(Qt::UserRole).toInt();
        if (fi != sample_) setSample(fi);
    });
    ll->addWidget(sampleList_, 1);
    ll->addWidget(note(tr("吻合度 = 该文件的水印与配方吻合的程度；低于 85 的文件处理后会标为「需确认」。")));
    h->addWidget(left);

    auto* mid = new QWidget;
    auto* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    auto* bar = new QWidget;
    bar->setObjectName("bottomBar");
    bar->setFixedHeight(46);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 0, 16, 0);
    prevPage_ = mkBtn("‹"); nextPage_ = mkBtn("›");
    prevPage_->setFixedWidth(34); nextPage_->setFixedWidth(34);
    connect(prevPage_, &QPushButton::clicked, this, [this] { setPage(page_ - 1); });
    connect(nextPage_, &QPushButton::clicked, this, [this] { setPage(page_ + 1); });
    pageLbl_ = new QLabel;
    removedLbl_ = new QLabel;
    removedLbl_->setProperty("note", true);
    bl->addWidget(prevPage_); bl->addWidget(pageLbl_); bl->addWidget(nextPage_);
    bl->addSpacing(12);
    bl->addWidget(removedLbl_, 1);
    modeOrig_ = mkBtn(tr("原图（橙色=将去除）"));
    modeRem_ = mkBtn(tr("去除后"));
    for (auto* b : {modeOrig_, modeRem_}) { b->setCheckable(true); b->setProperty("seg", true); }
    modeOrig_->setChecked(true);
    connect(modeOrig_, &QPushButton::clicked, this, [this] { modeOrig_->setChecked(true); modeRem_->setChecked(false); view_->setMode(DrawView::Original); });
    connect(modeRem_, &QPushButton::clicked, this, [this] { modeRem_->setChecked(true); modeOrig_->setChecked(false); view_->setMode(DrawView::Removed); });
    bl->addWidget(modeOrig_); bl->addWidget(modeRem_);
    ml->addWidget(bar);
    view_ = new DrawView;
    connect(view_, &DrawView::boxed, this, [this](double a, double b, double c, double d) { boxShape(a, b, c, d); });
    ml->addWidget(view_, 1);
    h->addWidget(mid, 1);

    auto* right = new QWidget;
    right->setObjectName("side");
    right->setProperty("right", true);
    right->setFixedWidth(400);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(14, 14, 14, 14);
    rl->setSpacing(10);
    auto* rt = new QLabel(tr("本组配方"));
    rt->setStyleSheet("font-weight:700;font-size:15px");
    rl->addWidget(rt);
    rl->addWidget(note(tr("在样本上调好，同组全部文件套用。")));
    tabs_ = new QTabWidget;
    // --- auto detected
    auto* t1 = new QWidget;
    auto* l1 = new QVBoxLayout(t1);
    autoChk_ = new QCheckBox(tr("去除自动检测到的水印（把握度 ≥ 75）"));
    connect(autoChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (curGroup_ < 0 || curGroup_ >= recipes_.size() || recipes_[curGroup_].removeDetected == on) return;
        recipes_[curGroup_].removeDetected = on; refreshPreview(); refreshTables(); syncChrome();
    });
    l1->addWidget(autoChk_);
    findings_ = new QListWidget;
    connect(findings_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (sample_ < 0 || !findings_->property("busy").isNull()) return;
        std::vector<int> v;
        for (int i = 0; i < findings_->count(); ++i) {
            auto* it = findings_->item(i);
            if (it->data(Qt::UserRole + 1).toBool() && it->checkState() == Qt::Checked) v.push_back(it->data(Qt::UserRole).toInt());
        }
        extra_[sample_] = v;
        refreshPreview();
    });
    l1->addWidget(findings_, 1);
    autoNote_ = note({});
    l1->addWidget(autoNote_);
    tabs_->addTab(t1, tr("自动检测"));
    // --- text
    auto* t2 = new QWidget;
    auto* l2 = new QVBoxLayout(t2);
    auto* hl = new QHBoxLayout;
    needleEdit_ = new QLineEdit;
    needleEdit_->setPlaceholderText(tr("输入要去除的字（汉字、字母、数字），回车添加"));
    auto* addN = mkBtn(tr("添加"));
    connect(needleEdit_, &QLineEdit::returnPressed, this, [this] { addNeedle(needleEdit_->text()); needleEdit_->clear(); });
    connect(addN, &QPushButton::clicked, this, [this] { addNeedle(needleEdit_->text()); needleEdit_->clear(); });
    hl->addWidget(needleEdit_, 1); hl->addWidget(addN);
    l2->addLayout(hl);
    needleList_ = new QListWidget;
    needleList_->setMaximumHeight(110);
    connect(needleList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { removeNeedle(needleList_->row(it)); });
    l2->addWidget(note(tr("已加入配方的文字（双击移除）")));
    l2->addWidget(needleList_);
    auto* dt = new QLabel(tr("或：从样本文档里挑（双击添加）"));
    dt->setProperty("lb", true);
    l2->addWidget(dt);
    docTexts_ = new QListWidget;
    connect(docTexts_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { addNeedle(it->data(Qt::UserRole).toString()); });
    l2->addWidget(docTexts_, 1);
    needleNote_ = note(tr("搜不到想去的字？它可能是用线条画出来的（常见于 CAD 店标）。请到「框选图形」页签圈住它。"));
    l2->addWidget(needleNote_);
    tabs_->addTab(t2, tr("指定文字"));
    // --- shapes
    auto* t3 = new QWidget;
    auto* l3 = new QVBoxLayout(t3);
    shapeHint_ = note(tr("在中间的页面上拖出一个框，圈住要去除的字或标志（完整圈住每一笔）。系统会在全部页面找出形状相同的图形（含旋转、镜像、缩放）。"));
    l3->addWidget(shapeHint_);
    shapeLbl_ = new QLabel(tr("尚未框选"));
    shapeLbl_->setWordWrap(true);
    shapeLbl_->setObjectName("shapeStatus");
    l3->addWidget(shapeLbl_);
    shapeAdd_ = mkBtn(tr("加入配方"), true);
    shapeAdd_->setEnabled(false);
    connect(shapeAdd_, &QPushButton::clicked, this, [this] { commitShape(tr("图形 %1").arg(recipes_.isEmpty() ? 1 : recipes_[curGroup_].shapes.size() + 1)); });
    l3->addWidget(shapeAdd_);
    l3->addWidget(note(tr("已加入配方的图形（双击移除）")));
    shapeList_ = new QListWidget;
    connect(shapeList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int r = shapeList_->row(it);
        if (curGroup_ >= 0 && curGroup_ < recipes_.size() && r >= 0 && r < int(recipes_[curGroup_].shapes.size())) {
            recipes_[curGroup_].shapes.erase(recipes_[curGroup_].shapes.begin() + r);
            refreshRecipePanel(); refreshPreview(); refreshTables(); syncChrome();
        }
    });
    l3->addWidget(shapeList_, 1);
    tabs_->addTab(t3, tr("框选图形"));
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) { view_->setSelectEnabled(i == 2); });
    rl->addWidget(tabs_, 1);

    auto* ob = new QHBoxLayout;
    outLbl_ = new QLabel;
    outLbl_->setProperty("note", true);
    outLbl_->setWordWrap(true);
    auto* ch = mkBtn(tr("更改…"));
    connect(ch, &QPushButton::clicked, this, [this] { chooseOutput(); });
    ob->addWidget(outLbl_, 1); ob->addWidget(ch);
    rl->addLayout(ob);
    trial_ = mkBtn(tr("试跑 3 个文件"));
    connect(trial_, &QPushButton::clicked, this, [this] { startRun(3); });
    rl->addWidget(trial_);
    h->addWidget(right);
    return page;
}

QWidget* WatermarkPanel::buildRunPage() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(10);
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* cl = new QVBoxLayout(card);
    runInfo_ = new QLabel;
    runInfo_->setStyleSheet("font-weight:700;font-size:15px");
    runBar_ = new QProgressBar;
    runBar_->setFixedHeight(10);
    runBar_->setTextVisible(false);
    cl->addWidget(runInfo_);
    cl->addWidget(runBar_);
    v->addWidget(card);
    runTable_ = new QTableWidget(0, 6);
    runTable_->setHorizontalHeaderLabels({tr("文件名"), tr("组"), tr("状态"), tr("去除项"), tr("线条总数校验"), tr("说明")});
    runTable_->verticalHeader()->setVisible(false);
    runTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    runTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    runTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    runTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    runTable_->setColumnWidth(1, 50); runTable_->setColumnWidth(2, 120); runTable_->setColumnWidth(3, 70); runTable_->setColumnWidth(4, 170);
    v->addWidget(runTable_, 1);
    auto* br = new QHBoxLayout;
    runStart_ = mkBtn(tr("开始处理"), true);
    runCancel_ = mkBtn(tr("取消"));
    runCsv_ = mkBtn(tr("导出报告（CSV）…"));
    runOpen_ = mkBtn(tr("打开输出文件夹"));
    connect(runStart_, &QPushButton::clicked, this, [this] { startRun(); });
    connect(runCancel_, &QPushButton::clicked, this, [this] { cancel_ = true; });
    connect(runCsv_, &QPushButton::clicked, this, [this] { exportReport(); });
    connect(runOpen_, &QPushButton::clicked, this, [this] { openOutput(); });
    br->addWidget(runStart_); br->addWidget(runCancel_); br->addStretch(1); br->addWidget(runCsv_); br->addWidget(runOpen_);
    v->addLayout(br);
    return page;
}

// ============================================================================ helpers

void WatermarkPanel::async(std::function<void()> work, std::function<void()> done) {
    ++jobs_;
    auto* w = new QFutureWatcher<void>(this);
    connect(w, &QFutureWatcher<void>::finished, this, [this, w, done] {
        --jobs_;
        if (done) done();
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(std::move(work)));
}

QString WatermarkPanel::letter(int g) const {
    if (g == manual_) return tr("手动");
    return QString(QChar('A' + g % 26));
}
QColor WatermarkPanel::colorOf(int g) const {
    static const char* c[] = {"#C2410C", "#8A5A00", "#1D4ED8", "#0F766E", "#7C3AED", "#BE185D"};
    if (g == manual_) return QColor("#5B6B6B");
    return QColor(c[g % 6]);
}
bool WatermarkPanel::recipeHasContent(const wm::Recipe& r) const { return r.removeDetected || !r.texts.empty() || !r.shapes.empty(); }
int WatermarkPanel::step() const { return pages_->currentIndex(); }
std::vector<int> WatermarkPanel::extraFor(int fileIndex) const {
    auto it = extra_.constFind(fileIndex);
    return it == extra_.constEnd() ? std::vector<int>() : it.value();
}
QString WatermarkPanel::shapeStatus() const { return shapeStatus_; }

QString WatermarkPanel::statusText(const WmFile& f) const {
    switch (f.status) {
    case WmFile::Pending: return f.analyzed ? (f.group >= 0 && f.selected ? tr("可处理") : tr("将跳过")) : tr("分析中…");
    case WmFile::Waiting: return tr("排队中");
    case WmFile::Running: return tr("处理中…");
    case WmFile::Done: return tr("已完成");
    case WmFile::NeedsCheck: return tr("已完成 · 需确认");
    case WmFile::Failed: return tr("失败");
    case WmFile::Skipped: return tr("已跳过");
    }
    return {};
}

void WatermarkPanel::setOutputDir(const QString& d) { outDir_ = d; syncChrome(); }

void WatermarkPanel::chooseOutput() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("选择输出文件夹"), outDir_);
    if (!d.isEmpty()) setOutputDir(d);
}
void WatermarkPanel::openOutput() { if (!outDir_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(outDir_)); }

// ============================================================================ adding / analysing

void WatermarkPanel::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void WatermarkPanel::dropEvent(QDropEvent* e) {
    QStringList p;
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) p << u.toLocalFile();
    if (!p.isEmpty()) { goStep(0); addFiles(p); }
}

int WatermarkPanel::addFiles(const QStringList& in) {
    QStringList found;
    for (const QString& p : in) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, {"*.pdf", "*.PDF"}, QDir::Files, QDirIterator::Subdirectories);
            QStringList sub;
            while (it.hasNext()) sub << it.next();
            sub.sort(Qt::CaseInsensitive);
            found << sub;
        } else if (fi.suffix().compare("pdf", Qt::CaseInsensitive) == 0 && fi.exists()) found << fi.absoluteFilePath();
    }
    QSet<QString> have;
    for (auto& f : files_) have.insert(QFileInfo(f.path).canonicalFilePath());
    int added = 0, over = 0;
    const int first = files_.size();
    for (const QString& p : found) {
        const QString canon = QFileInfo(p).canonicalFilePath();
        if (have.contains(canon)) continue;
        if (files_.size() >= wm::kMaxBatchFiles) { ++over; continue; }
        have.insert(canon);
        WmFile f; f.path = QFileInfo(p).absoluteFilePath(); f.name = QFileInfo(p).fileName();
        files_.push_back(f);
        ++added;
    }
    limitNote_->setText(over ? tr("已达到上限 %1 个，另有 %2 个文件没有添加。可分批处理。").arg(wm::kMaxBatchFiles).arg(over) : QString());
    if (outDir_.isEmpty() && !files_.isEmpty()) outDir_ = QFileInfo(files_.first().path).absolutePath() + "/去水印输出";
    for (int i = first; i < files_.size(); ++i) {
        ++pendingAnalysis_;
        auto rep = std::make_shared<wm::Report>();
        const QString path = files_[i].path;
        async([rep, path] { *rep = wm::analyze(fsPath(path)); }, [this, i, rep] { onAnalyzed(i, *rep); });
    }
    refreshTables();
    syncChrome();
    return added;
}

void WatermarkPanel::onAnalyzed(int i, const wm::Report& r) {
    if (i < 0 || i >= files_.size()) return;
    files_[i].rep = r;
    files_[i].analyzed = true;
    if (--pendingAnalysis_ <= 0) {
        pendingAnalysis_ = 0;
        regroup();
        emit filesAnalyzed();
    }
}

void WatermarkPanel::regroup() {
    std::vector<wm::Report> reps;
    for (auto& f : files_) reps.push_back(f.rep);
    std::vector<std::string> sigs;
    auto g = wm::groupReports(reps, &sigs);
    // remember recipes of groups that still exist
    std::map<std::string, wm::Recipe> oldBySig;
    for (int k = 0; k < recipes_.size(); ++k) if (k != manual_ && k < int(sigs_.size())) oldBySig[sigs_[k]] = recipes_[k];
    wm::Recipe oldManual; bool hadManual = manual_ >= 0 && manual_ < recipes_.size();
    if (hadManual) oldManual = recipes_[manual_];
    sigs_ = sigs;
    const int nAuto = int(sigs.size());
    bool anyManual = false;
    for (int x : g) if (x == -1) anyManual = true;
    manual_ = anyManual ? nAuto : -1;
    nGroups_ = nAuto + (anyManual ? 1 : 0);
    recipes_.clear();
    for (int k = 0; k < nAuto; ++k) {
        auto it = oldBySig.find(sigs[k]);
        if (it != oldBySig.end()) recipes_.push_back(it->second);
        else { wm::Recipe r; r.name = s8(tr("组 %1").arg(letter(k))); r.signature = sigs[k]; r.removeDetected = true; recipes_.push_back(r); }
    }
    if (anyManual) {
        if (hadManual) recipes_.push_back(oldManual);
        else { wm::Recipe r; r.name = s8(tr("手动")); r.removeDetected = false; recipes_.push_back(r); }
    }
    for (int i = 0; i < files_.size(); ++i) {
        auto& f = files_[i];
        const bool first = f.group == -9;
        f.group = g[i] == -1 ? manual_ : g[i];
        if (first) f.selected = g[i] >= 0;
        f.conformity = f.group >= 0 ? wm::conformity(f.rep, recipes_[f.group]) : 0;
        if (f.status == WmFile::Pending && f.group == -2) f.note = tr("图片水印，无法安全去除");
    }
    if (curGroup_ >= nGroups_) curGroup_ = 0;
    if (sample_ < 0 || sample_ >= files_.size() || files_[sample_].group != curGroup_) {
        sample_ = -1;
        for (int i = 0; i < files_.size(); ++i) if (files_[i].group == curGroup_) { sample_ = i; break; }
    }
    refreshTables();
    refreshGroups();
    if (step() == 1) refreshReview();
    syncChrome();
}

// ============================================================================ step 1 views

void WatermarkPanel::refreshTables() {
    if (!table_) return;
    table_->blockSignals(true);
    table_->setRowCount(files_.size());
    for (int i = 0; i < files_.size(); ++i) {
        auto& f = files_[i];
        auto setTxt = [&](int c, const QString& s, const QColor& col = QColor()) {
            auto* it = new QTableWidgetItem(s);
            if (col.isValid()) it->setForeground(col);
            table_->setItem(i, c, it);
        };
        auto* chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(f.selected && f.group != -2 && f.group != -3 ? Qt::Checked : Qt::Unchecked);
        if (f.analyzed && (f.group == -2 || f.group == -3)) chk->setFlags(Qt::ItemIsEnabled);
        table_->setItem(i, 0, chk);
        setTxt(1, f.name);
        setTxt(2, f.analyzed ? QString::number(f.rep.pageCount) : QString());
        setTxt(3, f.analyzed ? detectedText(f.rep) : tr("分析中…"));
        QString gt; QColor gc;
        if (!f.analyzed) gt = "—";
        else if (f.group >= 0) { gt = letter(f.group); gc = colorOf(f.group); }
        else gt = "—";
        setTxt(4, gt, gc);
        QColor sc;
        if (f.analyzed && (f.group == -2 || f.group == -3)) sc = QColor("#8A5A00");
        setTxt(5, f.analyzed && f.group == -2 ? tr("无法处理") : f.analyzed && f.group == -3 ? tr("读取失败") : statusText(f), sc);
    }
    table_->blockSignals(false);
}

void WatermarkPanel::refreshGroups() {
    clearLayout(groupsLay_);
    if (pendingAnalysis_ > 0) {
        groupsLay_->addWidget(note(tr("正在分析 %1 个文件…").arg(pendingAnalysis_)));
        return;
    }
    if (files_.isEmpty()) {
        groupsLay_->addWidget(note(tr("添加文件后，系统会按水印的类型自动分组，同组文件共用一套设置。")));
        return;
    }
    int autoGroups = nGroups_ - (manual_ >= 0 ? 1 : 0);
    auto* title = new QLabel(autoGroups > 0 ? tr("已自动分成 %1 组").arg(autoGroups) : tr("没有发现可自动去除的水印"));
    title->setStyleSheet("font-weight:700;font-size:15px");
    groupsLay_->addWidget(title);
    groupsLay_->addWidget(note(tr("按水印的类型、位置和样式比对；同组文件共用一套设置。")));
    for (int g = 0; g < nGroups_; ++g) {
        int n = 0, pages = 0, sel = 0, confSum = 0;
        for (auto& f : files_) if (f.group == g) { ++n; pages += f.rep.pageCount; if (f.selected) ++sel; confSum += f.conformity; }
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* cl = new QVBoxLayout(card);
        cl->setSpacing(4);
        const bool man = g == manual_;
        auto* hd = new QLabel(QString("<span style='background:%1;color:#fff;border-radius:4px;padding:2px 8px;font-weight:700'>%2</span> &nbsp;<b>%3</b>")
                                  .arg(colorOf(g).name(), letter(g).toHtmlEscaped(), man ? tr("未发现水印（可手动指定文字 / 图形）") : u8(sigs_[g]).toHtmlEscaped()));
        hd->setTextFormat(Qt::RichText);
        hd->setWordWrap(true);
        cl->addWidget(hd);
        cl->addWidget(note(man ? tr("%1 个文件 · 共 %2 页 · 默认不处理，勾选后到「审阅与配方」里指定要去除的内容").arg(n).arg(pages)
                               : tr("%1 个文件 · 共 %2 页 · 平均吻合度 %3").arg(n).arg(pages).arg(n ? confSum / n : 0)));
        groupsLay_->addWidget(card);
    }
    int bm = 0, bad = 0;
    for (auto& f : files_) { if (f.group == -2) ++bm; else if (f.group == -3) ++bad; }
    if (bm) groupsLay_->addWidget(note(tr("图片水印，无法安全去除：%1 个（水印已和线条合成为图片，硬去除会损伤纸样）").arg(bm)));
    if (bad) groupsLay_->addWidget(note(tr("读取失败：%1 个").arg(bad)));
}

void WatermarkPanel::syncChrome() {
    const int s = pages_ ? step() : 0;
    for (int i = 0; i < 3; ++i) {
        stepBtn_[i]->setProperty("state", i == s ? "on" : (i < s ? "ok" : ""));
        stepBtn_[i]->style()->unpolish(stepBtn_[i]); stepBtn_[i]->style()->polish(stepBtn_[i]);
        stepBtn_[i]->setEnabled(i == 0 || (nGroups_ > 0 && pendingAnalysis_ == 0));
    }
    int sel = 0, usable = 0;
    for (auto& f : files_) { if (f.selected && f.group >= 0) ++sel; if (f.group >= 0) ++usable; }
    back_->setEnabled(s > 0);
    back_->setVisible(s < 2);
    if (s == 0) {
        next_->setText(tr("下一步：审阅与配方 →"));
        next_->setEnabled(nGroups_ > 0 && pendingAnalysis_ == 0 && usable > 0);
        info_->setText(files_.isEmpty() ? tr("还没有文件") : tr("已选 %1 个文件 · 分 %2 组").arg(sel).arg(nGroups_));
    } else if (s == 1) {
        next_->setText(tr("开始处理（%1 个文件）").arg(sel));
        next_->setEnabled(sel > 0 && !running_);
        int active = 0;
        for (auto& r : recipes_) if (recipeHasContent(r)) ++active;
        info_->setText(tr("%1 组中有 %2 组已设置去除内容 · 输出到 %3").arg(nGroups_).arg(active).arg(outDir_));
    } else {
        next_->setText(tr("打开输出文件夹"));
        next_->setEnabled(!running_ && !outDir_.isEmpty());
        info_->setText(summaryText());
    }
    if (outLbl_) outLbl_->setText(tr("输出到：%1\n文件名：{原名}_去水印.pdf，不覆盖原件").arg(outDir_));
    if (trial_) trial_->setEnabled(!running_ && sel > 0);
}

// ============================================================================ step 2: review

void WatermarkPanel::goStep(int s) {
    s = std::max(0, std::min(2, s));
    if (s > 0 && (nGroups_ == 0 || pendingAnalysis_ > 0)) return;
    pages_->setCurrentIndex(s);
    if (s == 1) refreshReview();
    if (s == 2) refreshRun();
    syncChrome();
}

void WatermarkPanel::selectGroup(int g) {
    if (g < 0 || g >= nGroups_) return;
    curGroup_ = g;
    sample_ = -1;
    for (int i = 0; i < files_.size(); ++i) if (files_[i].group == g) { sample_ = i; break; }
    page_ = 1;
    haveShape_ = false; shapeStatus_.clear();
    refreshReview();
}

void WatermarkPanel::setFileSelected(int i, bool on) {
    if (i < 0 || i >= files_.size() || files_[i].group < 0) return;
    files_[i].selected = on;
    refreshTables(); refreshGroups(); syncChrome();
}

void WatermarkPanel::setSample(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= files_.size()) return;
    sample_ = fileIndex;
    page_ = 1;
    haveShape_ = false; shapeStatus_.clear();
    view_->setBox({});
    refreshRecipePanel();
    refreshPreview();
}

void WatermarkPanel::setPage(int p) {
    if (sample_ < 0) return;
    p = std::max(1, std::min(files_[sample_].rep.pageCount, p));
    if (p == page_) return;
    page_ = p;
    view_->setBox({});
    refreshPreview();
}

void WatermarkPanel::refreshReview() {
    if (nGroups_ == 0) return;
    clearLayout(groupBtnLay_);
    for (int g = 0; g < nGroups_; ++g) {
        int n = 0; for (auto& f : files_) if (f.group == g) ++n;
        auto* b = new QPushButton(tr("%1 组 · %2 个文件").arg(letter(g)).arg(n));
        if (g == manual_) b->setText(tr("手动 · %1 个文件").arg(n));
        b->setCheckable(true);
        b->setChecked(g == curGroup_);
        b->setProperty("seg", true);
        b->setStyleSheet(QString("QPushButton{border-left:5px solid %1;text-align:left;padding-left:10px}").arg(colorOf(g).name()));
        connect(b, &QPushButton::clicked, this, [this, g] { selectGroup(g); });
        groupBtnLay_->addWidget(b);
    }
    sampleList_->blockSignals(true);
    sampleList_->clear();
    int row = 0, cur = 0;
    for (int i = 0; i < files_.size(); ++i) {
        if (files_[i].group != curGroup_) continue;
        auto* it = new QListWidgetItem(tr("%1　吻合 %2").arg(files_[i].name).arg(files_[i].conformity));
        it->setData(Qt::UserRole, i);
        if (files_[i].conformity < 85 && curGroup_ != manual_) it->setForeground(QColor("#8A5A00"));
        sampleList_->addItem(it);
        if (i == sample_) cur = row;
        ++row;
    }
    sampleList_->setCurrentRow(cur);
    sampleList_->blockSignals(false);
    if (sample_ < 0) for (int i = 0; i < files_.size(); ++i) if (files_[i].group == curGroup_) { sample_ = i; break; }
    refreshRecipePanel();
    refreshPreview();
}

void WatermarkPanel::refreshRecipePanel() {
    if (curGroup_ < 0 || curGroup_ >= recipes_.size()) return;
    auto& rc = recipes_[curGroup_];
    autoChk_->blockSignals(true);
    autoChk_->setChecked(rc.removeDetected);
    autoChk_->setEnabled(curGroup_ != manual_);
    autoChk_->blockSignals(false);
    findings_->setProperty("busy", true);
    findings_->clear();
    int groupSize = 0; for (auto& f : files_) if (f.group == curGroup_) ++groupSize;
    if (sample_ >= 0) {
        const auto ex = extraFor(sample_);
        const auto& rep = files_[sample_].rep;
        for (int i = 0; i < int(rep.findings.size()); ++i) {
            const auto& f = rep.findings[i];
            const char* tier = f.tier == wm::Tier::High ? "默认去除" : f.tier == wm::Tier::Confirm ? "需确认" : f.tier == wm::Tier::Keep ? "默认保留" : "无法处理";
            auto* it = new QListWidgetItem(tr("%1 · %2\n得分 %3 · %4 页 · %5").arg(kKind[int(f.kind)], tier).arg(f.score).arg(f.pages).arg(u8(f.label)));
            it->setData(Qt::UserRole, i);
            const bool confirmable = f.tier == wm::Tier::Confirm && groupSize == 1;
            it->setData(Qt::UserRole + 1, confirmable);
            if (f.tier == wm::Tier::High) { it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled); it->setCheckState(rc.removeDetected ? Qt::Checked : Qt::Unchecked); }
            else if (confirmable) { it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled); it->setCheckState(std::find(ex.begin(), ex.end(), i) != ex.end() ? Qt::Checked : Qt::Unchecked); }
            else it->setFlags(Qt::ItemIsEnabled);
            findings_->addItem(it);
        }
        if (rep.findings.empty()) findings_->addItem(tr("样本里没有发现水印"));
    }
    findings_->setProperty("busy", QVariant());
    autoNote_->setText(groupSize > 1 ? tr("批量时只自动去除「默认去除」的项；「需确认」的项请在单个文件里处理。") : tr("单个文件时，可勾选「需确认」的项一并去除。"));
    needleList_->clear();
    for (auto& t : rc.texts) needleList_->addItem(u8(t));
    shapeList_->clear();
    for (auto& s : rc.shapes) shapeList_->addItem(tr("%1（%2 条线）").arg(u8(s.name)).arg(s.strokes.size()));
    shapeLbl_->setText(haveShape_ ? shapeStatus_ : (shapeStatus_.isEmpty() ? tr("尚未框选") : shapeStatus_));
    shapeAdd_->setEnabled(haveShape_);
    // text found in the sample (decoded)
    docTexts_->clear();
    if (sample_ >= 0) {
        const QString path = files_[sample_].path;
        const int sm = sample_;
        auto fill = [this](const std::vector<wm::TextItem>& items) {
            docTexts_->clear();
            for (auto& t : items) {
                if (docTexts_->count() >= 200) break;
                auto* it = new QListWidgetItem(tr("%1　（%2 处 · %3 页）").arg(u8(t.text)).arg(t.count).arg(t.pages));
                it->setData(Qt::UserRole, u8(t.text));
                docTexts_->addItem(it);
            }
            if (items.empty()) docTexts_->addItem(tr("这个文件里没有真文字"));
        };
        if (auto c = textCache_.value(path)) fill(*c);
        else {
            auto items = std::make_shared<std::vector<wm::TextItem>>();
            async([items, path] { *items = wm::listText(fsPath(path)); }, [this, items, sm, path, fill] {
                textCache_[path] = items;
                if (sm == sample_) fill(*items);
            });
        }
    }
    syncChrome();
}

void WatermarkPanel::addNeedle(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty() || curGroup_ < 0 || curGroup_ >= recipes_.size()) return;
    auto& v = recipes_[curGroup_].texts;
    const std::string s = s8(t);
    if (std::find(v.begin(), v.end(), s) != v.end()) return;
    v.push_back(s);
    refreshRecipePanel(); refreshPreview(); refreshTables(); syncChrome();
}
void WatermarkPanel::removeNeedle(int i) {
    if (curGroup_ < 0 || curGroup_ >= recipes_.size()) return;
    auto& v = recipes_[curGroup_].texts;
    if (i < 0 || i >= int(v.size())) return;
    v.erase(v.begin() + i);
    refreshRecipePanel(); refreshPreview(); refreshTables(); syncChrome();
}

void WatermarkPanel::refreshPreview() {
    if (!view_) return;
    if (sample_ < 0 || sample_ >= files_.size() || !files_[sample_].analyzed || !files_[sample_].rep.ok) {
        view_->clear(); pageLbl_->clear(); removedLbl_->clear();
        return;
    }
    const auto& f = files_[sample_];
    page_ = std::max(1, std::min(f.rep.pageCount, page_));
    pageLbl_->setText(tr("第 %1 / %2 页").arg(page_).arg(f.rep.pageCount));
    prevPage_->setEnabled(page_ > 1);
    nextPage_->setEnabled(page_ < f.rep.pageCount);
    const quint64 gen = ++previewGen_;
    const QString key = f.path + "#" + QString::number(page_);
    wm::Recipe rc = recipes_[curGroup_];
    rc.verify = false;
    const auto also = extraFor(sample_);
    const QString out = tmp_.filePath(QString("pv%1.pdf").arg(++tmpCounter_));
    const QString path = f.path;
    const int page = page_;
    auto before = beforeCache_.value(key);
    auto res = std::make_shared<std::pair<std::shared_ptr<wm::PageDrawing>, std::shared_ptr<wm::PageDrawing>>>();
    const bool has = recipeHasContent(rc) || !also.empty();
    async([=] {
        res->first = before ? before : std::make_shared<wm::PageDrawing>(wm::drawPage(fsPath(path), page));
        if (has) {
            auto r = wm::applyRecipe(fsPath(path), fsPath(out), rc, also);
            if (r.ok) res->second = std::make_shared<wm::PageDrawing>(wm::drawPage(fsPath(out), page));
            QFile::remove(out);
        }
        if (!res->second) res->second = res->first;
    }, [=] {
        beforeCache_[key] = res->first;
        if (gen != previewGen_) return;
        view_->setDrawings(res->first, res->second);
        removedLbl_->setText(view_->removedCount() ? tr("本页将去除 %1 个图元").arg(view_->removedCount()) : tr("本页没有要去除的内容"));
    });
}

// ============================================================================ boxed shapes

void WatermarkPanel::boxShape(double x0, double y0, double x1, double y1) {
    if (sample_ < 0) return;
    haveShape_ = false; shapeCopies_ = 0;
    shapeStatus_ = tr("正在查找相同的图形…");
    shapeLbl_->setText(shapeStatus_);
    shapeAdd_->setEnabled(false);
    view_->setBox(QRectF(QPointF(std::min(x0, x1), std::min(y0, y1)), QPointF(std::max(x0, x1), std::max(y0, y1))));
    struct R { wm::ShapeTemplate t; wm::ShapeResult sel, all; };
    auto r = std::make_shared<R>();
    const QString path = files_[sample_].path;
    const int page = page_;
    const wm::Rect rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
    const QString out = tmp_.filePath(QString("sh%1.pdf").arg(++tmpCounter_));
    const int sm = sample_;
    async([=] {
        r->sel = wm::extractShape(fsPath(path), page, rect, r->t);
        if (r->sel.ok) { r->all = wm::removeShapeTemplate(fsPath(path), fsPath(out), r->t); QFile::remove(out); }
    }, [=] {
        if (sm != sample_) return;
        if (!r->sel.ok) { shapeStatus_ = u8(r->sel.error); haveShape_ = false; }
        else if (!r->all.ok) { shapeStatus_ = tr("框选了 %1 条线，但没有找到可去除的相同图形").arg(r->sel.selected); haveShape_ = false; }
        else {
            shape_ = r->t; haveShape_ = true; shapeCopies_ = r->all.copies;
            shapeStatus_ = tr("框选 %1 条线 · 在 %2 页找到 %3 处相同（共 %4 条线）").arg(r->sel.selected).arg(r->all.pagesTouched.size()).arg(r->all.copies).arg(r->all.removed);
        }
        shapeLbl_->setText(shapeStatus_);
        shapeAdd_->setEnabled(haveShape_);
    });
}

bool WatermarkPanel::commitShape(const QString& name) {
    if (!haveShape_ || curGroup_ < 0 || curGroup_ >= recipes_.size()) return false;
    shape_.name = s8(name);
    recipes_[curGroup_].shapes.push_back(shape_);
    haveShape_ = false;
    shapeStatus_.clear();
    view_->setBox({});
    refreshRecipePanel(); refreshPreview(); refreshTables(); syncChrome();
    return true;
}

// ============================================================================ step 3: run

void WatermarkPanel::startRun(int maxFiles) {
    if (running_ || files_.isEmpty() || pendingAnalysis_ > 0) return;
    struct Job { int i; QString in, out; wm::Recipe rc; std::vector<int> also; };
    auto jobs = std::make_shared<std::vector<Job>>();
    QSet<QString> used;
    QDir().mkpath(outDir_);
    int taken = 0;
    for (int i = 0; i < files_.size(); ++i) {
        auto& f = files_[i];
        if (maxFiles > 0 && !(f.group == curGroup_ && f.selected && taken < maxFiles)) { continue; }
        f.res = wm::FileResult();
        f.note.clear();
        if (!f.selected) { f.status = WmFile::Skipped; f.note = tr("未选择"); continue; }
        if (f.group == -2) { f.status = WmFile::Skipped; f.note = tr("图片水印，无法安全去除"); continue; }
        if (f.group < 0) { f.status = WmFile::Failed; f.note = tr("无法读取文件"); continue; }
        const auto also = extraFor(i);
        if (!recipeHasContent(recipes_[f.group]) && also.empty()) { f.status = WmFile::Skipped; f.note = tr("配方里没有要去除的内容"); continue; }
        if (maxFiles > 0) ++taken;
        Job j; j.i = i; j.in = f.path; j.rc = recipes_[f.group]; j.also = also;
        QString out = outDir_ + "/" + QFileInfo(f.path).completeBaseName() + QString::fromUtf8("_去水印.pdf");
        for (int k = 2; QFile::exists(out) || used.contains(out); ++k) out = outDir_ + "/" + QFileInfo(f.path).completeBaseName() + QString::fromUtf8("_去水印(%1).pdf").arg(k);
        used.insert(out);
        j.out = out;
        f.status = WmFile::Waiting;
        jobs->push_back(std::move(j));
    }
    running_ = true;
    cancel_ = false;
    goStep(2);
    refreshRun();
    if (jobs->empty()) { running_ = false; refreshRun(); syncChrome(); emit runFinished(); return; }
    QPointer<WatermarkPanel> self(this);
    async([this, self, jobs] {
        for (auto& j : *jobs) {
            if (cancel_) {
                QMetaObject::invokeMethod(self.data(), [self, i = j.i] { if (self) { self->files_[i].status = WmFile::Skipped; self->files_[i].note = tr("已取消"); self->refreshRun(); } }, Qt::QueuedConnection);
                continue;
            }
            QMetaObject::invokeMethod(self.data(), [self, i = j.i] { if (self) { self->files_[i].status = WmFile::Running; self->refreshRun(); } }, Qt::QueuedConnection);
            auto r = wm::applyRecipe(fsPath(j.in), fsPath(j.out), j.rc, j.also);
            QMetaObject::invokeMethod(self.data(), [self, i = j.i, r] {
                if (!self) return;
                auto& f = self->files_[i];
                f.res = r;
                if (r.ok) { f.status = f.conformity < 85 && f.group != self->manual_ ? WmFile::NeedsCheck : WmFile::Done; f.note = f.status == WmFile::NeedsCheck ? tr("与配方不完全一致，建议抽检") : QString(); }
                else if (r.nothingToDo) { f.status = WmFile::Skipped; f.note = tr("没有找到配方要去除的内容"); }
                else { f.status = WmFile::Failed; f.note = u8(r.error); }
                self->refreshRun();
            }, Qt::QueuedConnection);
        }
    }, [this] {
        running_ = false;
        cancel_ = false;
        refreshRun();
        syncChrome();
        QDir().mkpath(outDir_);
        int ok = 0; for (auto& f : files_) if (f.status == WmFile::Done || f.status == WmFile::NeedsCheck) ++ok;
        if (ok) writeReport(outDir_ + QString::fromUtf8("/去水印报告.csv"));
        emit runFinished();
    });
}

QString WatermarkPanel::summaryText() const {
    int ok = 0, chk = 0, bad = 0, skip = 0, left = 0;
    for (auto& f : files_) {
        switch (f.status) {
        case WmFile::Done: ++ok; break;
        case WmFile::NeedsCheck: ++chk; break;
        case WmFile::Failed: ++bad; break;
        case WmFile::Skipped: ++skip; break;
        case WmFile::Waiting: case WmFile::Running: ++left; break;
        default: break;
        }
    }
    return tr("已完成 %1 · 需确认 %2 · 失败 %3 · 跳过 %4%5").arg(ok).arg(chk).arg(bad).arg(skip).arg(left ? tr(" · 进行中 %1").arg(left) : QString());
}

void WatermarkPanel::refreshRun() {
    if (!runTable_) return;
    runTable_->setRowCount(files_.size());
    int total = 0, finished = 0;
    for (int i = 0; i < files_.size(); ++i) {
        auto& f = files_[i];
        auto setTxt = [&](int c, const QString& s, const QColor& col = QColor()) {
            auto* it = new QTableWidgetItem(s);
            if (col.isValid()) it->setForeground(col);
            runTable_->setItem(i, c, it);
        };
        const bool counted = f.status != WmFile::Pending && !(f.status == WmFile::Skipped && f.res.removedTotal() == 0 && f.note == tr("未选择"));
        if (counted) { ++total; if (f.status != WmFile::Waiting && f.status != WmFile::Running) ++finished; }
        setTxt(0, f.name);
        setTxt(1, f.group >= 0 ? letter(f.group) : "—", f.group >= 0 ? colorOf(f.group) : QColor());
        QColor col;
        if (f.status == WmFile::Done) col = QColor("#0B5C55");
        else if (f.status == WmFile::NeedsCheck) col = QColor("#8A5A00");
        else if (f.status == WmFile::Failed) col = QColor("#9A3412");
        setTxt(2, statusText(f), col);
        const bool done = f.status == WmFile::Done || f.status == WmFile::NeedsCheck;
        setTxt(3, done ? QString::number(f.res.removedTotal()) : "—");
        setTxt(4, done ? tr("%1 → %2 ✓").arg(f.res.strokesBefore).arg(f.res.strokesAfter) : (f.status == WmFile::Failed && f.res.strokesBefore ? tr("%1 → %2 ✗").arg(f.res.strokesBefore).arg(f.res.strokesAfter) : QString("—")), done ? QColor("#0B5C55") : QColor());
        setTxt(5, f.note);
    }
    runBar_->setMaximum(std::max(1, total));
    runBar_->setValue(finished);
    runInfo_->setText(running_ ? tr("正在处理 %1 / %2").arg(finished).arg(total) : (total ? tr("处理完成：%1 个文件").arg(total) : tr("还没有开始处理")));
    runStart_->setEnabled(!running_);
    runCancel_->setEnabled(running_);
    runCsv_->setEnabled(!running_ && total > 0);
    runOpen_->setEnabled(!outDir_.isEmpty());
    syncChrome();
}

bool WatermarkPanel::writeReport(const QString& csvPath) const {
    QFile f(csvPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    auto q = [](QString s) { s.replace('"', "\"\""); return "\"" + s + "\""; };
    QByteArray out = "\xEF\xBB\xBF";
    out += QString::fromUtf8("文件,状态,吻合度,去除项,线条数(前),线条数(后),说明\n").toUtf8();
    for (auto& w : files_) {
        if (w.status == WmFile::Pending) continue;
        const bool done = w.status == WmFile::Done || w.status == WmFile::NeedsCheck;
        out += (q(w.name) + "," + statusText(w) + "," + QString::number(w.conformity) + "," + QString::number(done ? w.res.removedTotal() : 0) + "," +
                (w.res.strokesBefore ? QString::number(w.res.strokesBefore) : QString()) + "," + (w.res.strokesAfter ? QString::number(w.res.strokesAfter) : QString()) + "," + q(w.note) + "\n").toUtf8();
    }
    return f.write(out) == out.size();
}

void WatermarkPanel::exportReport() {
    const QString p = QFileDialog::getSaveFileName(this, tr("导出报告"), outDir_ + QString::fromUtf8("/去水印报告.csv"), "CSV (*.csv)");
    if (!p.isEmpty()) writeReport(p);
}
