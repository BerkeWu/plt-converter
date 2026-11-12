#include "Stamp.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
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
    s.gapMm = q.value("gapMm", s.gapMm).toDouble();
    s.suffix = q.value("suffix", s.suffix).toString();
    s.askEveryTime = q.value("askEveryTime", s.askEveryTime).toBool();
    q.endGroup();
    return s;
}

void StampSettings::save() const {
    QSettings q("HaoyangPattern", "PltConverter");
    q.beginGroup("stamp");
    q.setValue("text", text); q.setValue("family", family); q.setValue("sizePt", sizePt); q.setValue("color", color.name());
    q.setValue("opacity", opacity); q.setValue("angle", angle); q.setValue("gapMm", gapMm); q.setValue("suffix", suffix);
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
    spec.gapX = s.gapMm * 72.0 / 25.4;
    spec.gapY = s.gapMm * 72.0 / 25.4;
    return spec;
}

QString stampOutputPath(const QString& input, const StampSettings& s) {
    if (s.suffix.isEmpty()) return input;
    const QFileInfo fi(input);
    QString base = fi.completeBaseName();
    if (base.endsWith(s.suffix)) return input;              // already a stamped file: stamp it again in place
    return fi.dir().filePath(base + s.suffix + "." + (fi.suffix().isEmpty() ? QStringLiteral("pdf") : fi.suffix()));
}

// ---------------------------------------------------------------------------------------------------------- dialog

StampDialog::StampDialog(int fileCount, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("添加水印"));
    const StampSettings s = StampSettings::load();
    col_ = s.color;
    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("给 %1 个 PDF 添加水印。水印以矢量轮廓直接写进页面内容（不是文字、不是水印注释/图层/独立对象），"
                               "不带 WPS / Acrobat 用来识别水印的任何标记，它们的「删除水印」找不到它。").arg(fileCount));
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto* form = new QFormLayout;
    text_ = new QPlainTextEdit(s.text);
    text_->setPlaceholderText(tr("例如：浩洋纸样\n微信 xxxx"));
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
    gap_ = new QDoubleSpinBox; gap_->setRange(0, 300); gap_->setSuffix(" mm"); gap_->setValue(s.gapMm);
    form->addRow(tr("间距"), gap_);
    suffix_ = new QLineEdit(s.suffix);
    suffix_->setPlaceholderText(tr("留空 = 直接覆盖原文件"));
    form->addRow(tr("输出文件名后缀"), suffix_);
    v->addLayout(form);
    preview_ = new QLabel;
    preview_->setFixedSize(420, 220);
    preview_->setFrameShape(QFrame::StyledPanel);
    v->addWidget(preview_, 0, Qt::AlignHCenter);
    direct_ = new QCheckBox(tr("以后点「添加水印」直接使用这些设置，不再弹出此窗口"));
    direct_->setChecked(!s.askEveryTime);
    v->addWidget(direct_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("添加水印"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (text_->toPlainText().trimmed().isEmpty()) { QMessageBox::information(this, tr("添加水印"), tr("请先填写水印文字。")); return; }
        settings().save();
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    connect(color_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(col_, this, tr("水印颜色"));
        if (c.isValid()) { col_ = c; updatePreview(); }
    });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); });
    connect(font_, &QFontComboBox::currentFontChanged, this, [this] { updatePreview(); });
    for (auto* sp : {size_, angle_, gap_}) connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(opacity_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    updatePreview();
}

StampSettings StampDialog::settings() const {
    StampSettings s;
    s.text = text_->toPlainText();
    s.family = font_->currentFont().family();
    s.sizePt = size_->value();
    s.color = col_;
    s.opacity = opacity_->value();
    s.angle = angle_->value();
    s.gapMm = gap_->value();
    s.suffix = suffix_->text().trimmed();
    s.askEveryTime = !direct_->isChecked();
    return s;
}

void StampDialog::updatePreview() {
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
    const StampSettings s = settings();
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
