// Headless GUI flow test (QT_QPA_PLATFORM=offscreen): import, step flow, undo/redo, drag, box-select, rotate, single page, export.
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <iostream>

#include "../qt/CanvasWidget.h"
#include "../qt/MainWindow.h"
#include "../qt/Widgets.h"

static int fails = 0;
static void pump(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::cout << "  FAIL: " << msg << "\n"; } else std::cout << "  ok: " << msg << "\n"; } while (0)

static bool waitFor(MainWindow& w, void (MainWindow::*sig)(), int ms = 15000) {
    QEventLoop loop;
    QTimer t; t.setSingleShot(true);
    bool got = false;
    QObject::connect(&w, sig, &loop, [&] { got = true; loop.quit(); });
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(ms);
    loop.exec();
    return got;
}
static bool waitPlan(MainWindow& w) { return waitFor(w, &MainWindow::planReady); }
static bool load(MainWindow& w, const QString& f) {
    QEventLoop loop; bool ok = false;
    QObject::connect(&w, &MainWindow::loaded, &loop, [&](bool o) { ok = o; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    w.loadFile(f);
    loop.exec();
    return ok;
}
static QString pdfInfo(const QString& pdf, const QString& key) {
    QProcess p; p.start("pdfinfo", {pdf}); p.waitForFinished();
    for (auto& l : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) if (l.startsWith(key)) return l.mid(key.size() + 1).trimmed();
    return {};
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfgHome;                                  // settings live in a throw-away folder: never read or write the real ones
    qputenv("XDG_CONFIG_HOME", cfgHome.path().toLocal8Bit());
    QApplication app(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    const QString data = argc > 1 ? argv[1] : "tests/data";
    MainWindow w;
    w.show();
    auto* canvas = w.findChild<CanvasWidget*>();
    CHECK(canvas != nullptr, "canvas exists");

    // --- bad inputs never crash ---
    { QString bad = QDir(tmp.path()).filePath("empty.plt"); QFile f(bad); f.open(QIODevice::WriteOnly); f.close();
      QTimer::singleShot(300, [] { for (auto* wd : QApplication::topLevelWidgets()) if (auto* mb = qobject_cast<QMessageBox*>(wd)) mb->accept(); });
      CHECK(!load(w, bad), "empty file rejected with a message, no crash"); }
    { QString bad = QDir(tmp.path()).filePath("junk.plt"); QFile f(bad); f.open(QIODevice::WriteOnly); f.write("hello world, not hpgl"); f.close();
      QTimer::singleShot(300, [] { for (auto* wd : QApplication::topLevelWidgets()) if (auto* mb = qobject_cast<QMessageBox*>(wd)) mb->accept(); });
      CHECK(!load(w, bad), "non-HPGL content rejected"); }

    // --- import a Chinese-named file via a path with CJK + space ---
    const QString src = QDir(data).filePath("2401_儿童风衣外套-110cm.plt");
    QString cjk = QDir(tmp.path()).filePath("中文 目录/纸样 上衣.PLT");
    QDir().mkpath(QFileInfo(cjk).absolutePath());
    QFile::copy(src, cjk);
    CHECK(load(w, cjk), "import file with Chinese/space path and upper-case extension");
    CHECK(w.state().overview, "「添加拼接效果图」 is on by default");
    waitPlan(w);

    w.goStep(1);
    waitPlan(w);
    CHECK(w.planValid() && w.pageCount() > 1, "step 2: plan computed");
    const int p0 = w.pageCount();
    const State s0 = w.state();
    CHECK(s0.autoOrient, "auto orientation on by default");

    // --- undo / redo ---
    w.edit([](State& s) { s.paper = "A3"; });
    waitPlan(w);
    CHECK(w.pageCount() < p0, "A3 needs fewer pages than A4");
    w.undo(); waitPlan(w);
    CHECK(w.state().paper == "A4" && w.pageCount() == p0, "undo restores paper and page count");
    w.redo(); waitPlan(w);
    CHECK(w.state().paper == "A3", "redo");
    w.undo(); waitPlan(w);

    // --- drag the drawing with the mouse ---
    w.edit([](State& s) { s.margin = 5; });
    waitPlan(w);
    const double x0 = w.state().offX;
    QTest::mousePress(canvas, Qt::LeftButton, {}, QPoint(400, 300));
    QTest::mouseMove(canvas, QPoint(430, 320));
    QTest::mouseMove(canvas, QPoint(470, 340));
    QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(470, 340));
    CHECK(w.state().offX > x0 && w.state().offY > 0, "mouse drag moves the drawing (offset grows)");
    w.undo();
    CHECK(w.state().offX == x0 && w.state().offY == 0, "undo reverts the whole drag in one step");
    // keyboard nudges coalesce into one undo step
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
    CHECK(w.state().offX == 2 && w.state().offY == 10, "arrow keys nudge 1 mm, Shift 10 mm");
    w.undo();
    CHECK(w.state().offX == 0 && w.state().offY == 0, "nudges undone together");
    waitPlan(w);

    // --- auto position + centre + alignment never go negative ---
    w.edit([](State& s) { s.offX = 3; s.offY = 3; });
    waitPlan(w);

    // --- box select in step 1 ---
    w.goStep(0);
    waitPlan(w);
    const double fullW = w.lastPlan().nx;  // grid columns with the full drawing
    const int pBeforeSel = w.pageCount();
    auto* sel = w.findChild<QPushButton*>();  // not used; selection is driven by mouse on the canvas
    Q_UNUSED(sel);
    for (auto* b : w.findChildren<QPushButton*>()) if (b->text() == QObject::tr("框选区域")) b->setChecked(true);
    QTest::mousePress(canvas, Qt::LeftButton, {}, QPoint(380, 200));
    QTest::mouseMove(canvas, QPoint(600, 400));
    QTest::mouseRelease(canvas, Qt::LeftButton, {}, QPoint(700, 480));
    CHECK(w.state().hasSel && w.state().sel.width() > 10 && w.state().sel.height() > 10, "mouse box-select creates a selection");
    waitPlan(w);
    CHECK(w.lastPlan().nx < fullW || w.pageCount() < p0, "selection reduces the page count");
    const int pSel = w.pageCount();
    QString out1 = QDir(tmp.path()).filePath("sel.pdf");
    CHECK(w.exportBlocking(out1).isEmpty() && QFileInfo(out1).size() > 0, "export of the selected region");
    CHECK(pdfInfo(out1, "Pages").toInt() == pSel, "PDF page count equals preview page count");
    {
        QFile pf(out1); pf.open(QIODevice::ReadOnly);
        const QByteArray b = pf.readAll();
        CHECK(b.contains("/PrintScaling /None"), "PDF asks viewers not to scale when printing");
        CHECK(!b.contains("/ca 0") && !b.contains("/CA 0"), "PDF contains no transparency (printing never rasterises the page)");
        CHECK(b.trimmed().endsWith("%%EOF"), "PDF still ends with a valid trailer after the print-scaling update");
    }
    w.undo(); waitPlan(w);
    CHECK(!w.state().hasSel && w.pageCount() == pBeforeSel, "undo clears selection");

    // --- rotate / mirror ---
    w.edit([](State& s) { s.rot = 1; });
    waitPlan(w);
    CHECK(w.state().rot == 1 && w.planValid(), "rotate 90 recomputes the plan");
    w.edit([](State& s) { s.rot = 0; s.mirror = true; });
    waitPlan(w);
    CHECK(w.state().mirror && w.planValid(), "mirror recomputes the plan");
    w.edit([](State& s) { s.mirror = false; });

    // --- single page ---
    w.goStep(1);
    w.edit([](State& s) { s.singlePage = true; s.margin = 10; });
    waitPlan(w);
    CHECK(w.planValid() && w.pageCount() == 1, "single-page output = 1 page");
    QString out2 = QDir(tmp.path()).filePath("single.pdf");
    CHECK(w.exportBlocking(out2).isEmpty(), "single page export");
    const QString sz = pdfInfo(out2, "Page size");
    std::cout << "    single page size: " << sz.toStdString() << "\n";
    CHECK(pdfInfo(out2, "Pages") == "1", "single page PDF has one page");
    w.edit([](State& s) { s.singlePage = false; });
    waitPlan(w);

    // --- overview + line colour + page range filter ---
    w.edit([](State& s) { s.overview = true; s.origColor = false; s.color = QColor("#D33333"); });
    waitPlan(w);
    CHECK(w.lastPlan().meta.first().overview, "overview page is first");
    {   // the canvas shows the line colour that will be printed (was always teal)
        pump(300);
        const QImage img = canvas->grab().toImage();
        int red = 0, teal = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.red() > 170 && c.green() < 90 && c.blue() < 90) ++red;
                if (std::abs(c.red() - 11) <= 3 && std::abs(c.green() - 94) <= 3 && std::abs(c.blue() - 87) <= 3) ++teal;   // the old fixed drawing colour #0B5E57
            }
        CHECK(red > 200, "canvas draws the drawing in the selected line colour");
        CHECK(teal < 50, "canvas no longer draws the layout preview in the fixed teal");
    }
    w.goStep(2);
    QLineEdit* edit = nullptr;
    for (auto* e : w.findChildren<QLineEdit*>()) if (!e->placeholderText().isEmpty()) edit = e;
    CHECK(edit != nullptr, "page range field found");
    edit->setText("1-3,5");
    for (auto* b : w.findChildren<QPushButton*>()) if (b->text() == QObject::tr("应用") && b->parentWidget() && b->parentWidget()->isAncestorOf(edit)) { b->click(); break; }
    QString out3 = QDir(tmp.path()).filePath("range.pdf");
    CHECK(w.exportBlocking(out3).isEmpty() && pdfInfo(out3, "Pages") == "4", "page range 1-3,5 exports 4 pages");


    // --- blank pages: an L-shaped drawing leaves empty grid cells ---
    {
        QString l = QDir(tmp.path()).filePath("L形.plt");
        QFile f(l); f.open(QIODevice::WriteOnly); f.write("IN;SP1;PU0,0;PD24000,0;PU0,0;PD0,24000;"); f.close();   // 600 x 600 mm, two edges only
        CHECK(load(w, l), "import L-shaped drawing");
        w.edit([](State& s) { s.autoOrient = false; s.landscape = true; s.calibBox = false; s.overview = false; });
        waitPlan(w);
        const int grid = w.lastPlan().nx * w.lastPlan().ny;
        CHECK(w.lastPlan().blank > 0 && w.pageCount() < grid, "blank cells are skipped by default");
        w.edit([](State& s) { s.keepBlank = true; });
        waitPlan(w);
        std::cout << "    grid " << grid << ", pages now " << w.pageCount() << ", keepBlank " << w.state().keepBlank << ", blank " << w.lastPlan().blank << ", eff " << w.lastPlan().effective << "\n";
        CHECK(w.pageCount() == grid, "「输出空白页」 brings every grid cell back");
        w.goStep(2);
        QString out = QDir(tmp.path()).filePath("blank.pdf");
        CHECK(w.exportBlocking(out).isEmpty() && pdfInfo(out, "Pages").toInt() == grid, "all cells (incl. blank) are exported by default");
    }
    // --- frame detection on a DZ file ---
    const QString dz = QDir(data).filePath("DZ231101儿童中式背心__100码_纸宽门幅110cm_.plt");
    CHECK(load(w, dz), "import file with paper frame");
    waitPlan(w);
    CHECK(w.state().ignoreFrame && w.frameRemoved() == 2, "「忽略纸张边框」 is on by default and removes the frame");
    w.edit([](State& s) { s.ignoreFrame = false; });
    waitPlan(w);
    const int withFrame = w.pageCount();
    const int linesBefore = w.workLineCount();
    CHECK(w.state().alignColor == QColor("#2563EB"), "alignment marks default to blue");
    w.edit([](State& s) { s.ignoreFrame = true; });
    waitPlan(w);
    CHECK(w.frameRemoved() == 2 && w.workLineCount() == linesBefore - 2, "「忽略纸张边框」 removes both frame lines from the drawing");
    CHECK(w.pageCount() <= withFrame, "ignoring the paper frame never adds pages");

    // --- parameter defaults + click-to-edit boxes ---
    {
        State def;
        CHECK(def.overlap == 10 && def.margin == 0 && def.lineWidth == 0.1 && def.calibCm == 5, "defaults: 重叠区 10 mm, 页边距 0 mm, 线宽 0.1 mm, 校准框 5 cm");
        CHECK(def.ignoreFrame && !def.fileTag, "defaults: 忽略纸张边框 on, 页码旁附加文件名 off");
        MainWindow w2;
        w2.show();
        pump(50);
        CHECK(w2.state().margin == 0 && w2.state().ignoreFrame && !w2.state().fileTag, "new window starts with the new defaults");
        QList<ClickSpinBox*> spins;     // (no Q_OBJECT in the header-only widget: qobject_cast would match every QDoubleSpinBox)
        for (auto* d : w2.findChildren<QDoubleSpinBox*>()) if (auto* c = dynamic_cast<ClickSpinBox*>(d)) spins << c;
        CHECK(spins.size() == 4, "overlap / margin / line width / calibration boxes are click-to-edit");
        for (auto* sp : spins) {
            CHECK(sp->locked(), "box starts locked");
            const double before = sp->value();
            QWheelEvent we(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(sp, &we);
            QApplication::sendEvent(sp->editor(), &we);
            QTest::keyClick(sp, Qt::Key_Up);
            CHECK(sp->value() == before, "wheel / keys / stepping do not change a locked box");
            QTest::mouseClick(sp->editor(), Qt::LeftButton);
            CHECK(!sp->locked(), "a click unlocks the box for editing");
            sp->lock();
        }
        ClickSpinBox* ov = spins.isEmpty() ? nullptr : spins.first();
        if (ov) {
            QTest::mouseClick(ov->editor(), Qt::LeftButton);
            ov->editor()->selectAll();
            QTest::keyClicks(ov->editor(), "12");
            QTest::keyClick(ov->editor(), Qt::Key_Return);
            pump(50);
            CHECK(ov->locked(), "box locks again after editing");
        }
    }
    // --- hand-set blank pages ---
    {
        MainWindow w3;
        w3.show();
        CHECK(load(w3, dz), "import for blank pages");
        waitPlan(w3);
        w3.edit([](State& s) { s.paper = "A4"; s.autoOrient = false; s.landscape = true; s.overview = false; });
        waitPlan(w3);
        const int pages = w3.pageCount(), eff = w3.lastPlan().effective;
        int pick = -1;
        for (int i = 0; i < w3.lastPlan().meta.size(); ++i) if (!w3.lastPlan().meta[i].empty) { pick = i; break; }
        CHECK(pages >= 2 && pick >= 0, "multi-page drawing for the blank-page test");
        if (pick >= 0) {
            w3.blankPages({pick}, true);
            waitPlan(w3);
            CHECK(w3.state().blanked.size() == 1, "page recorded as hand-set blank");
            CHECK(w3.pageCount() == pages && w3.lastPlan().effective == eff - 1, "blank page stays in the PDF but is no longer an effective page");
            CHECK(w3.lastPlan().meta[pick].manual, "meta marks it as manual blank");
            w3.undo();
            waitPlan(w3);
            CHECK(w3.state().blanked.isEmpty() && w3.lastPlan().effective == eff, "undo restores the page");
            w3.blankPages({pick}, true);
            waitPlan(w3);
            w3.blankPages({pick}, false);
            waitPlan(w3);
            CHECK(w3.state().blanked.isEmpty() && w3.lastPlan().effective == eff, "「恢复内容」 brings the pattern back");
            w3.blankPages({pick}, true);
            waitPlan(w3);
            w3.edit([](State& s) { s.rot = 1; });
            waitPlan(w3);
            CHECK(w3.state().blanked.isEmpty(), "rotating the drawing clears hand-set blanks");
            const QByteArray shots = qgetenv("GUI_SHOTS");      // optional: screenshots for a visual check
            if (!shots.isEmpty()) {
                w3.resize(1400, 900);
                w3.edit([](State& s) { s.rot = 0; });
                waitPlan(w3);
                w3.goStep(1); waitPlan(w3); pump(300);
                w3.grab().save(QString::fromLocal8Bit(shots) + "/layout.png");
                w3.blankPages({pick}, true); waitPlan(w3);
                w3.goStep(2); pump(500);
                w3.grab().save(QString::fromLocal8Bit(shots) + "/preview.png");
            }
        }
    }

    std::cout << (fails ? "GUI TEST FAILED: " + std::to_string(fails) : std::string("GUI ALL PASSED")) << "\n";
    return fails ? 1 : 0;
}
