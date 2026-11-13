#include "Stamp.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QtMath>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>

namespace {
std::filesystem::path fsPath(const QString& s) { return wm::fromU8(s.toUtf8().toStdString()); }

QString defaultFamily() {
    const QStringList fams = QFontDatabase::families();
    for (const char* f : {"Microsoft YaHei", "微软雅黑", "PingFang SC", "Noto Sans CJK SC", "Source Han Sans SC", "SimHei", "黑体", "Noto Sans CJK TC", "WenQuanYi Micro Hei"})
        if (fams.contains(QString::fromUtf8(f))) return QString::fromUtf8(f);
    return QApplication::font().family();
}

QFont stampFont(const StampSettings& s) {
    QFont f(s.family.isEmpty() ? defaultFamily() : s.family);
    f.setPixelSize(200);              // outlines are built at 200 units per em and scaled to the point size
    f.setBold(true);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// Qt path (y down, 200 units/em) -> stamp piece (points, y up, around the stamp centre)
wm::StampPath toPiece(const QPainterPath& p, double scale, QPointF centre) {
    wm::StampPath o;
    auto X = [&](double x) { return (x - centre.x()) * scale; };
    auto Y = [&](double y) { return -(y - centre.y()) * scale; };
    bool open = false;
    for (int i = 0; i < p.elementCount(); ++i) {
        const auto e = p.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            if (open) o.ops += 'Z';
            o.ops += 'M'; o.pts.push_back(X(e.x)); o.pts.push_back(Y(e.y)); open = true;
            break;
        case QPainterPath::LineToElement:
            o.ops += 'L'; o.pts.push_back(X(e.x)); o.pts.push_back(Y(e.y));
            break;
        case QPainterPath::CurveToElement: {
            if (i + 2 >= p.elementCount()) break;
            const auto c2 = p.elementAt(i + 1), c3 = p.elementAt(i + 2);
            o.ops += 'C';
            for (double v : {X(e.x), Y(e.y), X(c2.x), Y(c2.y), X(c3.x), Y(c3.y)}) o.pts.push_back(v);
            i += 2;
            break;
        }
        default: break;
        }
    }
    if (open) o.ops += 'Z';
    return o;
}

bool isPdfPath(const QString& p) { return p.endsWith(".pdf", Qt::CaseInsensitive); }
}  // namespace

StampSettings StampSettings::load() {
    QSettings q("HaoyangPattern", "PltConverter");
    StampSettings s;
    q.beginGroup("stamp");
    s.text = q.value("text", s.text).toString();
    s.family = q.value("family", s.family).toString();
    s.sizePt = q.value("sizePt", s.sizePt).toDouble();
    s.color = QColor(q.value("color", s.color.name()).toString());
    if (!s.color.isValid()) s.color = QColor(214, 64, 84);
    s.opacity = q.value("opacity", s.opacity).toInt();
    s.angle = q.value("angle", s.angle).toDouble();
    if (q.contains("gapXMm")) {
        s.gapXMm = q.value("gapXMm", s.gapXMm).toDouble();
        s.gapYMm = q.value("gapYMm", s.gapYMm).toDouble();
    } else if (q.contains("gapMm")) {
        // pre-existing single "间距" setting (round 20): carry it over as both axes once, then drop it
        s.gapXMm = s.gapYMm = q.value("gapMm").toDouble();
    }
    s.suffix = q.value("suffix", s.suffix).toString();
    s.askEveryTime = q.value("askEveryTime", s.askEveryTime).toBool();
    q.endGroup();
    return s;
}

void StampSettings::save() const {
    QSettings q("HaoyangPattern", "PltConverter");
    q.beginGroup("stamp");
    q.setValue("text", text); q.setValue("family", family); q.setValue("sizePt", sizePt); q.setValue("color", color.name());
    q.setValue("opacity", opacity); q.setValue("angle", angle); q.setValue("gapXMm", gapXMm); q.setValue("gapYMm", gapYMm);
    q.remove("gapMm");
    q.setValue("suffix", suffix);
    q.setValue("askEveryTime", askEveryTime);
    q.endGroup();
}

wm::StampSpec buildStampSpec(const StampSettings& s) {
    wm::StampSpec spec;
    const QFont f = stampFont(s);
    const QFontMetricsF fm(f);
    const QStringList lines = s.text.split('\n', Qt::SkipEmptyParts);
    const double lineH = fm.height() * 1.15;
    // whole-stamp box in Qt units (centre lines horizontally)
    double maxW = 0;
    for (auto& l : lines) maxW = std::max(maxW, fm.horizontalAdvance(l.trimmed()));
    const double totalH = lineH * lines.size();
    const QPointF centre(maxW / 2, totalH / 2);
    const double scale = s.sizePt / 200.0;
    for (int li = 0; li < lines.size(); ++li) {
        const QString line = lines[li].trimmed();
        double x = (maxW - fm.horizontalAdvance(line)) / 2;
        const double base = li * lineH + fm.ascent();
        // one piece per character: the stamp ends up as many small unrelated paths
        for (int k = 0; k < line.size();) {
            int n = (line[k].isHighSurrogate() && k + 1 < line.size()) ? 2 : 1;
            const QString ch = line.mid(k, n);
            QPainterPath p; p.addText(QPointF(x, base), f, ch);
            if (!p.isEmpty()) spec.pieces.push_back(toPiece(p, scale, centre));
            x += fm.horizontalAdvance(ch);
            k += n;
        }
    }
    spec.w = maxW * scale;
    spec.h = totalH * scale;
    spec.r = (float)s.color.redF(); spec.g = (float)s.color.greenF(); spec.b = (float)s.color.blueF();
    spec.opacity = std::clamp(s.opacity, 3, 100) / 100.0;
    spec.angleDeg = s.angle;
    spec.gapX = s.gapXMm * 72.0 / 25.4;
    spec.gapY = s.gapYMm * 72.0 / 25.4;
    return spec;
}

QString stampOutputPath(const QString& input, const StampSettings& s) {
    if (s.suffix.isEmpty()) return input;
    const QFileInfo fi(input);
    QString base = fi.completeBaseName();
    if (base.endsWith(s.suffix)) return input;              // already a stamped file: stamp it again in place
    return fi.dir().filePath(base + s.suffix + "." + (fi.suffix().isEmpty() ? QStringLiteral("pdf") : fi.suffix()));
}

// ---------------------------------------------------------------------------------------------------------- form

StampForm::StampForm(QWidget* parent) : QWidget(parent) {
    const StampSettings s = StampSettings::load();
    col_ = s.color;
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout;
    text_ = new QPlainTextEdit(s.text);
    text_->setPlaceholderText(tr("例如：好样纸样\n微信 xxxx"));
    text_->setFixedHeight(76);
    form->addRow(tr("水印文字"), text_);
    font_ = new QFontComboBox;
    font_->setWritingSystem(QFontDatabase::SimplifiedChinese);
    font_->setCurrentFont(QFont(s.family.isEmpty() ? defaultFamily() : s.family));
    form->addRow(tr("字体"), font_);
    size_ = new QDoubleSpinBox; size_->setRange(6, 120); size_->setSuffix(tr(" pt")); size_->setValue(s.sizePt);
    form->addRow(tr("字号"), size_);
    color_ = new QPushButton;
    form->addRow(tr("颜色"), color_);
    opacity_ = new QSpinBox; opacity_->setRange(3, 100); opacity_->setSuffix(" %"); opacity_->setValue(s.opacity);
    form->addRow(tr("不透明度"), opacity_);
    angle_ = new QDoubleSpinBox; angle_->setRange(-90, 90); angle_->setSuffix(QStringLiteral("°")); angle_->setValue(s.angle);
    form->addRow(tr("角度"), angle_);
    auto* gapPresetRow = new QWidget;
    auto* gapPresetL = new QHBoxLayout(gapPresetRow);
    gapPresetL->setContentsMargins(0, 0, 0, 0);
    gapSparse_ = new QPushButton(tr("稀")); gapMedium_ = new QPushButton(tr("中")); gapDense_ = new QPushButton(tr("密"));
    for (auto* b : {gapSparse_, gapMedium_, gapDense_}) { b->setFixedWidth(44); gapPresetL->addWidget(b); }
    gapPresetL->addStretch(1);
    form->addRow(tr("密度预设"), gapPresetRow);
    gapX_ = new QDoubleSpinBox; gapX_->setObjectName("gapX"); gapX_->setRange(0, 300); gapX_->setSuffix(" mm"); gapX_->setValue(s.gapXMm);
    form->addRow(tr("横向间距"), gapX_);
    gapY_ = new QDoubleSpinBox; gapY_->setObjectName("gapY"); gapY_->setRange(0, 300); gapY_->setSuffix(" mm"); gapY_->setValue(s.gapYMm);
    form->addRow(tr("纵向间距"), gapY_);
    suffix_ = new QLineEdit(s.suffix);
    suffix_->setPlaceholderText(tr("留空 = 直接覆盖原文件"));
    form->addRow(tr("输出文件名后缀"), suffix_);
    v->addLayout(form);
    preview_ = new QLabel;
    preview_->setFixedSize(420, 220);
    preview_->setFrameShape(QFrame::StyledPanel);
    v->addWidget(preview_, 0, Qt::AlignHCenter);
    connect(color_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(col_, this, tr("水印颜色"));
        if (c.isValid()) { col_ = c; updatePreview(); emit changed(); }
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); emit changed(); });
    connect(font_, &QFontComboBox::currentFontChanged, this, [this] { updatePreview(); emit changed(); });
    for (auto* sp : {size_, angle_, gapX_, gapY_}) connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); emit changed(); });
    connect(opacity_, &QSpinBox::valueChanged, this, [this] { updatePreview(); emit changed(); });
    connect(suffix_, &QLineEdit::textChanged, this, [this] { emit changed(); });
    connect(gapSparse_, &QPushButton::clicked, this, [this] { setGapPreset(StampGap::kSparseMm); });
    connect(gapMedium_, &QPushButton::clicked, this, [this] { setGapPreset(StampGap::kMediumMm); });
    connect(gapDense_, &QPushButton::clicked, this, [this] { setGapPreset(StampGap::kDenseMm); });
    updatePreview();
}

void StampForm::setGapPreset(double mm) {
    gapX_->setValue(mm);
    gapY_->setValue(mm);   // valueChanged already fires updatePreview()+changed() for each, no extra call needed
}

StampSettings StampForm::settings() const {
    StampSettings s;
    s.text = text_->toPlainText();
    s.family = font_->currentFont().family();
    s.sizePt = size_->value();
    s.color = col_;
    s.opacity = opacity_->value();
    s.angle = angle_->value();
    s.gapXMm = gapX_->value();
    s.gapYMm = gapY_->value();
    s.suffix = suffix_->text().trimmed();
    return s;
}

void StampForm::setSettings(const StampSettings& s) {
    text_->setPlainText(s.text);
    font_->setCurrentFont(QFont(s.family.isEmpty() ? defaultFamily() : s.family));
    size_->setValue(s.sizePt);
    col_ = s.color;
    opacity_->setValue(s.opacity);
    angle_->setValue(s.angle);
    gapX_->setValue(s.gapXMm);
    gapY_->setValue(s.gapYMm);
    suffix_->setText(s.suffix);
    updatePreview();
}

void StampForm::updatePreview() {
    color_->setStyleSheet(QString("background:%1; min-width:60px; min-height:18px; border:1px solid #999; border-radius:4px;").arg(col_.name()));
    // a strip of "paper" (scaled: 1 pt = 1 px * k) with a few pattern lines, the stamp tiled over it with Multiply
    QImage img(preview_->size(), QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double k = 0.9;                      // preview scale: px per point
    p.setPen(QPen(Qt::black, 1.2));
    p.drawLine(QPointF(20, 40), QPointF(400, 60));
    p.drawPath([] { QPainterPath a; a.moveTo(30, 200); a.cubicTo(120, 80, 260, 240, 400, 120); return a; }());
    p.setPen(QPen(Qt::black, 1, Qt::DashLine));
    p.drawLine(QPointF(210, 5), QPointF(210, 215));
    StampSettings s = settings();
    if (s.valid()) {
        const wm::StampSpec spec = buildStampSpec(s);
        QPainterPath stamp;
        for (auto& pc : spec.pieces) {
            size_t q = 0;
            for (char c : pc.ops) {
                if (c == 'M') { stamp.moveTo(pc.pts[q], -pc.pts[q + 1]); q += 2; }
                else if (c == 'L') { stamp.lineTo(pc.pts[q], -pc.pts[q + 1]); q += 2; }
                else if (c == 'C') { stamp.cubicTo(pc.pts[q], -pc.pts[q + 1], pc.pts[q + 2], -pc.pts[q + 3], pc.pts[q + 4], -pc.pts[q + 5]); q += 6; }
                else if (c == 'Z') stamp.closeSubpath();
            }
        }
        stamp.setFillRule(Qt::WindingFill);
        QColor c = s.color; c.setAlphaF(spec.opacity);
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.setPen(Qt::NoPen); p.setBrush(c);
        const double sx = (spec.w + spec.gapX) * k, sy = (spec.h + spec.gapY) * k;
        const double a = -s.angle * M_PI / 180.0;          // screen y is down
        for (int j = -4; j <= 4; ++j)
            for (int i = -4; i <= 4; ++i) {
                const double u = (i + ((j & 1) ? 0.5 : 0.0)) * sx, w = j * sy;
                const QPointF at(img.width() / 2 + u * std::cos(a) - w * std::sin(a), img.height() / 2 + u * std::sin(a) + w * std::cos(a));
                p.save();
                p.translate(at);
                p.rotate(-s.angle);
                p.scale(k, k);
                p.drawPath(stamp);
                p.restore();
            }
    }
    p.end();
    preview_->setPixmap(QPixmap::fromImage(img));
}

// ---------------------------------------------------------------------------------------------------------- dialog

StampDialog::StampDialog(int fileCount, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("添加水印"));
    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("给 %1 个 PDF 添加水印。水印以矢量轮廓直接写进页面内容（不是文字、不是水印注释/图层/独立对象），"
                               "不带 WPS / Acrobat 用来识别水印的任何标记，它们的「删除水印」找不到它。").arg(fileCount));
    hint->setWordWrap(true);
    v->addWidget(hint);
    form_ = new StampForm(this);
    v->addWidget(form_);
    direct_ = new QCheckBox(tr("以后点「添加水印」直接使用这些设置，不再弹出此窗口"));
    direct_->setChecked(!StampSettings::load().askEveryTime);
    v->addWidget(direct_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("添加水印"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (form_->settings().text.trimmed().isEmpty()) { QMessageBox::information(this, tr("添加水印"), tr("请先填写水印文字。")); return; }
        settings().save();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
}

StampSettings StampDialog::settings() const {
    StampSettings s = form_->settings();
    s.askEveryTime = !direct_->isChecked();
    return s;
}

// ---------------------------------------------------------------------------------------------------------- flow

QStringList runStampFlow(const QStringList& files, QWidget* parent, bool forceDialog) {
    if (files.isEmpty()) return {};
    StampSettings s = StampSettings::load();
    if (forceDialog || s.askEveryTime || !s.valid()) {
        StampDialog dlg(files.size(), parent);
        if (dlg.exec() != QDialog::Accepted) return {};
        s = dlg.settings();
    }
    const wm::StampSpec spec = buildStampSpec(s);
    struct Out { QStringList written; QStringList failed; };
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto done = std::make_shared<std::atomic_int>(0);
    QProgressDialog prog(QObject::tr("正在添加水印…"), QObject::tr("取消"), 0, files.size(), parent);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(300);
    QFutureWatcher<Out> w;
    QObject::connect(&prog, &QProgressDialog::canceled, [cancel] { *cancel = true; });
    w.setFuture(QtConcurrent::run([files, s, spec, cancel, done] {
        Out o;
        for (const QString& f : files) {
            if (*cancel) break;
            const QString target = stampOutputPath(f, s);
            // write next to the target first, then replace (the input may be the target itself)
            const QString tmp = target + ".stamp-tmp";
            const auto r = wm::addWatermark(fsPath(f), fsPath(tmp), spec);
            std::error_code ec;
            if (r.ok) {
                std::filesystem::rename(fsPath(tmp), fsPath(target), ec);
                if (ec) { std::filesystem::remove(fsPath(target), ec); std::filesystem::rename(fsPath(tmp), fsPath(target), ec); }
            }
            if (r.ok && !ec) o.written << target;
            else { std::filesystem::remove(fsPath(tmp), ec); o.failed << QFileInfo(f).fileName() + "：" + QString::fromUtf8(r.error.c_str()); }
            ++*done;
        }
        return o;
    }));
    QTimer t; t.setInterval(100);
    QObject::connect(&t, &QTimer::timeout, [&] { prog.setValue(*done); });
    t.start();
    QEventLoop loop;
    QObject::connect(&w, &QFutureWatcher<Out>::finished, &loop, &QEventLoop::quit);
    if (!w.isFinished()) loop.exec();
    t.stop();
    prog.setValue(files.size());
    const Out o = w.result();
    QString msg = QObject::tr("已添加水印：%1 个文件").arg(o.written.size());
    if (!o.written.isEmpty()) msg += "\n" + QFileInfo(o.written.first()).absolutePath();
    if (!o.failed.isEmpty()) msg += QObject::tr("\n\n失败 %1 个：\n").arg(o.failed.size()) + o.failed.join('\n');
    if (*cancel) msg += QObject::tr("\n（已取消剩余文件）");
    if (qEnvironmentVariableIsEmpty("PLT_NO_DIALOGS")) QMessageBox::information(parent, QObject::tr("添加水印"), msg);
    return o.written;
}

// ---------------------------------------------------------------------------------------------------------- panel (standalone "加水印" mode)

StampPanel::StampPanel(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    auto* h = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    auto* hint = new QLabel(tr("把 PDF 拖进来，或点「添加文件」。给每个文件加同一个水印，矢量轮廓写进页面内容，"
                               "WPS / Acrobat 的「删除水印」识别不到它（不是文字、不是水印注释/图层/独立对象）。"));
    hint->setWordWrap(true);
    left->addWidget(hint);
    list_ = new QListWidget;
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    left->addWidget(list_, 1);
    auto* fileBtns = new QHBoxLayout;
    addBtn_ = new QPushButton(tr("添加文件…"));
    removeBtn_ = new QPushButton(tr("移除选中"));
    clearBtn_ = new QPushButton(tr("清空"));
    fileBtns->addWidget(addBtn_); fileBtns->addWidget(removeBtn_); fileBtns->addWidget(clearBtn_); fileBtns->addStretch(1);
    left->addLayout(fileBtns);
    progress_ = new QProgressBar; progress_->setVisible(false);
    left->addWidget(progress_);
    status_ = new QLabel; status_->setWordWrap(true);
    left->addWidget(status_);
    runBtn_ = new QPushButton(tr("开始添加水印"));
    runBtn_->setProperty("pri", true);
    runBtn_->setEnabled(false);
    left->addWidget(runBtn_);
    h->addLayout(left, 1);
    form_ = new StampForm(this);
    h->addWidget(form_);

    connect(addBtn_, &QPushButton::clicked, this, [this] {
        const QStringList picked = QFileDialog::getOpenFileNames(this, tr("添加 PDF"), QString(), tr("PDF 文件 (*.pdf)"));
        if (!picked.isEmpty()) addFiles(picked);
    });
    connect(removeBtn_, &QPushButton::clicked, this, [this] {
        if (running_) return;
        QSet<QString> drop;
        for (auto* it : list_->selectedItems()) drop.insert(it->data(Qt::UserRole).toString());
        if (drop.isEmpty()) return;
        files_.removeIf([&](const QString& p) { return drop.contains(p); });
        refreshList();
    });
    connect(clearBtn_, &QPushButton::clicked, this, [this] { clearQueue(); });
    connect(runBtn_, &QPushButton::clicked, this, [this] { startRun(); });
    refreshList();
}

void StampPanel::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void StampPanel::dropEvent(QDropEvent* e) {
    QStringList p;
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) p << u.toLocalFile();
    if (!p.isEmpty()) addFiles(p);
}

int StampPanel::addFiles(const QStringList& in) {
    if (running_) return 0;
    QStringList found;
    for (const QString& p : in) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, {"*.pdf", "*.PDF"}, QDir::Files, QDirIterator::Subdirectories);
            QStringList sub;
            while (it.hasNext()) sub << it.next();
            sub.sort(Qt::CaseInsensitive);
            found << sub;
        } else if (isPdfPath(p) && fi.exists()) found << fi.absoluteFilePath();
    }
    QSet<QString> have;
    for (auto& f : files_) have.insert(QFileInfo(f).canonicalFilePath());
    int added = 0;
    for (const QString& p : found) {
        const QString canon = QFileInfo(p).canonicalFilePath();
        if (have.contains(canon)) continue;
        have.insert(canon);
        files_ << QFileInfo(p).absoluteFilePath();
        ++added;
    }
    refreshList();
    return added;
}

QStringList StampPanel::files() const { return files_; }

bool StampPanel::clearQueue() {
    if (running_) return false;
    files_.clear();
    status_->setText(QString());
    refreshList();
    return true;
}

void StampPanel::refreshList() {
    list_->clear();
    for (auto& p : files_) {
        auto* it = new QListWidgetItem(QFileInfo(p).fileName());
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        list_->addItem(it);
    }
    runBtn_->setEnabled(!files_.isEmpty() && !running_);
    removeBtn_->setEnabled(!files_.isEmpty() && !running_);
    clearBtn_->setEnabled(!files_.isEmpty() && !running_);
}

void StampPanel::startRun() {
    if (running_ || files_.isEmpty()) return;
    const StampSettings s = form_->settings();
    if (!s.valid()) { QMessageBox::information(this, tr("加水印"), tr("请先填写水印文字。")); return; }
    s.save();
    running_ = true;
    refreshList();
    progress_->setRange(0, files_.size());
    progress_->setValue(0);
    progress_->setVisible(true);
    status_->setText(tr("正在添加水印…"));
    const wm::StampSpec spec = buildStampSpec(s);
    const QStringList files = files_;
    auto doneCount = std::make_shared<std::atomic_int>(0);
    struct Out { QStringList written; QStringList failed; };
    auto* w = new QFutureWatcher<Out>(this);
    auto* t = new QTimer(this); t->setInterval(100);
    connect(t, &QTimer::timeout, this, [this, doneCount, t] { if (progress_->isVisible()) { const int d = *doneCount; progress_->setValue(std::min(d, progress_->maximum())); } });
    t->start();
    connect(w, &QFutureWatcher<Out>::finished, this, [this, w, t] {
        t->stop(); t->deleteLater();
        const Out o = w->result();
        w->deleteLater();
        running_ = false;
        progress_->setVisible(false);
        refreshList();
        QString msg = tr("已添加水印：%1 / %2 个文件").arg(o.written.size()).arg(files_.size());
        if (!o.failed.isEmpty()) msg += tr("\n失败：\n") + o.failed.join('\n');
        status_->setText(msg);
        emit done(o.written);
    });
    w->setFuture(QtConcurrent::run([files, s, spec, doneCount] {
        Out o;
        for (const QString& f : files) {
            const QString target = stampOutputPath(f, s);
            const QString tmp = target + ".stamp-tmp";
            const auto r = wm::addWatermark(fsPath(f), fsPath(tmp), spec);
            std::error_code ec;
            if (r.ok) {
                std::filesystem::rename(fsPath(tmp), fsPath(target), ec);
                if (ec) { std::filesystem::remove(fsPath(target), ec); std::filesystem::rename(fsPath(tmp), fsPath(target), ec); }
            }
            if (r.ok && !ec) o.written << target;
            else { std::filesystem::remove(fsPath(tmp), ec); o.failed << QFileInfo(f).fileName() + "：" + QString::fromUtf8(r.error.c_str()); }
            ++*doneCount;
        }
        return o;
    }));
}
