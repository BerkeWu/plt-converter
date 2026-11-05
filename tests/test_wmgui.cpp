// Offscreen test of the 水印处理 panel: add -> analyse/group -> recipe (text + boxed shape) -> run -> report.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include "CanvasWidget.h"
#include "BatchCore.h"
#include <QWheelEvent>
#include <cmath>
#include <sys/resource.h>

#include "DrawView.h"
#include "MainWindow.h"
#include "plt.h"
#include "WatermarkPanel.h"

static void waitIdle(WatermarkPanel& p, int ms = 60000) {
    QElapsedTimer t; t.start();
    QTest::qWait(50);
    while (p.busy() && t.elapsed() < ms) QTest::qWait(30);
    QTest::qWait(50);
}

class WmGuiTest : public QObject {
    Q_OBJECT
    QString data_;
private slots:
    void initTestCase() { data_ = QString::fromLocal8Bit(qgetenv("WM_DATA")); QVERIFY(QDir(data_).exists()); }

    void limitIsTwenty() {
        QTemporaryDir td;
        for (int i = 0; i < 25; ++i) QVERIFY(QFile::copy(data_ + "/C_flat.pdf", td.filePath(QString("f%1.pdf").arg(i, 2, 10, QChar('0')))));
        WatermarkPanel p;
        const int added = p.addFiles({td.path()});
        QCOMPARE(added, 20);
        QCOMPARE(p.files().size(), 20);
        waitIdle(p);
        QCOMPARE(p.addFiles({td.filePath("f21.pdf")}), 0);   // already full
        QCOMPARE(p.groupCount(), 1);                           // 20 identical files: one group
    }

    void fullFlow() {
        QTemporaryDir td;
        const QStringList names = {"C_flat.pdf", "D_flat.pdf", "B_block.pdf", "A_clean.pdf", "E_bitmap.pdf", "H_vector_text.pdf"};
        QStringList paths;
        for (auto& n : names) { QVERIFY(QFile::copy(data_ + "/" + n, td.filePath(n))); paths << td.filePath(n); }
        WatermarkPanel p;
        p.resize(1360, 820);
        p.show();
        QCOMPARE(p.addFiles(paths), 6);
        waitIdle(p);
        // groups: [C,D] flattened text, [B] banner, manual [A_clean, H]; bitmap file belongs to none
        QCOMPARE(p.groupCount(), 3);
        QVERIFY(p.hasManualGroup());
        int gC = p.files()[0].group, gD = p.files()[1].group, gB = p.files()[2].group, gA = p.files()[3].group, gE = p.files()[4].group, gH = p.files()[5].group;
        QCOMPARE(gC, gD);
        QVERIFY(gB >= 0 && gB != gC);
        QCOMPARE(gA, gH);
        QCOMPARE(gA, p.groupCount() - 1);
        QCOMPARE(gE, -2);
        QVERIFY(!p.files()[3].selected && !p.files()[5].selected);   // nothing found: off by default
        QVERIFY(p.files()[0].selected && p.files()[2].selected);
        p.grab().save("/tmp/claude-0/shots/wm1_add.png");

        // step 2: review the flattened-text group on a sample
        p.goStep(1);
        QCOMPARE(p.step(), 1);
        p.selectGroup(gC);
        waitIdle(p);
        p.view()->setMode(DrawView::Original);
        QVERIFY(p.view()->removedCount() > 100);          // the watermark is highlighted
        p.grab().save("/tmp/claude-0/shots/wm2_review.png");

        // manual group: stroke-drawn label by box + a text needle on another group
        p.selectGroup(gA);
        waitIdle(p);
        p.setSample(5);   // H_vector_text
        p.setPage(2);
        waitIdle(p);
        p.boxShape(315, 590, 365, 712);
        waitIdle(p);
        QVERIFY2(p.shapeStatus().contains(QString::fromUtf8("找到")), qPrintable(p.shapeStatus()));
        QVERIFY(p.commitShape("小红叶"));
        QCOMPARE(p.recipe(gA).shapes.size(), size_t(1));
        waitIdle(p);
        QVERIFY(p.view()->removedCount() > 0);
        p.grab().save("/tmp/claude-0/shots/wm3_shape.png");
        p.setFileSelected(5, true);
        p.selectGroup(gB);
        p.addNeedle("微信");
        QCOMPARE(p.recipe(gB).texts.size(), size_t(1));
        waitIdle(p);

        // step 3: run everything
        const QString out = td.filePath("out");
        p.setOutputDir(out);
        QSignalSpy done(&p, &WatermarkPanel::runFinished);
        p.startRun();
        QVERIFY(done.wait(120000));
        waitIdle(p);
        QCOMPARE(p.step(), 2);
        auto st = [&](int i) { return p.files()[i].status; };
        QCOMPARE(st(0), WmFile::Done);
        QCOMPARE(st(1), WmFile::Done);
        QCOMPARE(st(2), WmFile::Done);
        QCOMPARE(st(3), WmFile::Skipped);   // not selected
        QCOMPARE(st(4), WmFile::Skipped);   // bitmap
        QCOMPARE(st(5), WmFile::Done);
        for (int i : {0, 1, 2, 5}) {
            QVERIFY(p.files()[i].res.verified);
            QVERIFY(p.files()[i].res.removedTotal() > 0);
            QVERIFY(QFileInfo::exists(out + "/" + QFileInfo(names[i]).completeBaseName() + QString::fromUtf8("_haoyang.pdf")));
        }
        QVERIFY(p.files()[2].res.textsRemoved > 0);
        QVERIFY(QFileInfo::exists(out + QString::fromUtf8("/去水印报告.csv")));
        QVERIFY(QFileInfo(td.filePath(names[0])).size() > 0);   // originals untouched
        p.grab().save("/tmp/claude-0/shots/wm4_run.png");
        // a second run never overwrites: names get a counter
        QSignalSpy done2(&p, &WatermarkPanel::runFinished);
        p.startRun();
        QVERIFY(done2.wait(120000));
        QVERIFY(QFileInfo::exists(out + QString::fromUtf8("/C_flat_haoyang(2).pdf")));
    }

    void styledShots() {   // screenshots of the three steps inside the main window (with the application style)
        MainWindow w;
        w.resize(1360, 820);
        w.show();
        w.showWatermark(true);
        QTemporaryDir td;
        for (auto n : {"C_flat.pdf", "D_flat.pdf", "B_block.pdf", "A_clean.pdf", "E_bitmap.pdf", "H_vector_text.pdf"}) QFile::copy(data_ + "/" + n, td.filePath(n));
        auto* p = w.watermarkPanel();
        p->addFiles({td.path()});
        waitIdle(*p);
        w.grab().save("/tmp/claude-0/shots/s1_add.png");
        p->goStep(1); p->selectGroup(0); waitIdle(*p);
        w.grab().save("/tmp/claude-0/shots/s2_review.png");
        p->selectGroup(p->groupCount() - 1);
        for (int i = 0; i < p->files().size(); ++i) if (p->files()[i].name == "H_vector_text.pdf") p->setSample(i);
        p->setPage(2); waitIdle(*p);
        p->boxShape(315, 590, 365, 712); waitIdle(*p);
        p->commitShape("小红叶"); waitIdle(*p);
        w.grab().save("/tmp/claude-0/shots/s3_shape.png");
        for (int i = 0; i < p->files().size(); ++i) if (p->files()[i].name == "H_vector_text.pdf") p->setFileSelected(i, true);
        p->setOutputDir(td.filePath("out"));
        QSignalSpy done(p, &WatermarkPanel::runFinished);
        p->startRun();
        QVERIFY(done.wait(120000));
        waitIdle(*p);
        w.grab().save("/tmp/claude-0/shots/s4_run.png");
    }

    // PLT: stroke-drawn labels ("小红叶") are removed from the PLT itself, the output is a PLT, Ctrl+Enter goes on to PLT->PDF
    static QByteArray pltWithLabels(const std::vector<std::pair<double, double>>& at, double sc) {
        const std::vector<std::vector<std::pair<double, double>>> glyph = {
            {{0, 0}, {8, 0}, {8, 5}, {0, 5}, {0, 0}}, {{2, 1}, {6, 4}}, {{2, 4}, {6, 1}, {7, 6}}, {{1, 7}, {9, 7}}};
        auto u = [](double mm) { return QString::number(qRound(mm * 40)); };
        QString o = "IN;SP1;PU0,0;PD0,16000,16000,16000,16000,0,0,0;PU;PU1000,1000;PD1000,3000;PU;";
        for (auto& a : at)
            for (auto& g : glyph) {
                o += "PU" + u(a.first + g[0].first * sc) + "," + u(a.second + g[0].second * sc) + ";PD";
                for (size_t k = 1; k < g.size(); ++k) o += (k > 1 ? "," : "") + u(a.first + g[k].first * sc) + "," + u(a.second + g[k].second * sc);
                o += ";";
            }
        o += "PU;";
        return o.toLatin1();
    }

    void pltLabelFlow() {
        QTemporaryDir td;
        QStringList paths;
        const std::vector<std::vector<std::pair<double, double>>> pos = {{{100, 100}, {250, 200}}, {{120, 150}}, {{60, 60}, {300, 300}, {200, 120}}};
        for (size_t i = 0; i < pos.size(); ++i) {
            QFile f(td.filePath(QString("p%1.plt").arg(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(pltWithLabels(pos[i], i == 1 ? 1.5 : 1.0));
            f.close();
            paths << f.fileName();
        }
        MainWindow w;
        w.resize(1360, 820);
        w.show();
        w.showWatermark(true);
        auto* p = w.watermarkPanel();
        QCOMPARE(p->addFiles(paths), 3);
        waitIdle(*p);
        QVERIFY(p->pltGroup() >= 0);
        for (auto& f : p->files()) { QVERIFY(f.isPlt); QCOMPARE(f.group, p->pltGroup()); QVERIFY(f.pltDoc); }
        p->goStep(1); p->selectGroup(p->pltGroup());
        p->setSample(0); waitIdle(*p);
        // box the first label of p0 (preview coordinates: x = mm - ox, y = oy - mm)
        const auto& b = p->files()[0].pltDoc->bounds;
        const double ox = b.minX - 10, oy = b.maxY + 10;
        p->boxShape(98 - ox, oy - 108, 112 - ox, oy - 98); waitIdle(*p);
        QVERIFY2(!p->shapeStatus().isEmpty(), "shape status");
        QVERIFY(p->commitShape("小红叶"));
        waitIdle(*p);
        QVERIFY(p->view()->removedCount() > 0);
        w.grab().save("/tmp/claude-0/shots/plt1_review.png");
        p->setOutputDir(td.filePath("out"));
        QSignalSpy done(p, &WatermarkPanel::runFinished);
        p->startRun();
        QVERIFY(done.wait(60000));
        waitIdle(*p);
        w.grab().save("/tmp/claude-0/shots/plt2_run.png");
        const QStringList cleaned = p->cleanedPltFiles();
        QCOMPARE(cleaned.size(), 3);
        for (const QString& c : cleaned) {
            QVERIFY(c.endsWith("_haoyang.plt"));
            QFile f(c); QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray d = f.readAll();
            QVERIFY(d.startsWith("IN;SP1;PU0,0;PD0,16000"));
            const plt::Document doc = plt::parseString(d.toStdString());
            QCOMPARE((int)doc.lines.size(), 2);              // outline + the extra line; every label is gone
        }
        // the originals are untouched
        QFile o(paths[0]); QVERIFY(o.open(QIODevice::ReadOnly));
        QCOMPARE((int)plt::parseString(o.readAll().toStdString()).lines.size(), 2 + 8);
        // Ctrl+Enter -> 批量转换 with the cleaned files
        QTest::keyClick(p, Qt::Key_Return, Qt::ControlModifier);
        QTest::qWait(300);
        QVERIFY(!w.watermarkVisible());
    }

    void clearQueueAndZoom() {
        QTemporaryDir td;
        QVERIFY(QFile::copy(data_ + "/C_flat.pdf", td.filePath("a.pdf")));
        QVERIFY(QFile::copy(data_ + "/B_block.pdf", td.filePath("b.pdf")));
        WatermarkPanel p;
        p.setSilent(true);
        p.resize(1360, 820);
        p.show();
        QCOMPARE(p.addFiles({td.filePath("a.pdf"), td.filePath("b.pdf")}), 2);
        waitIdle(p);
        QCOMPARE(p.groupCount(), 2);
        p.goStep(1); p.selectGroup(0); waitIdle(p);
        // zoom: wheel around the cursor keeps the page point under the cursor; box dragging still maps to page points
        auto* v = p.view();
        QCOMPARE(v->zoom(), 1.0);
        const QPointF anchorPage(200, 300);
        const QPointF before = v->pageToWidget(anchorPage);
        QWheelEvent we(before, v->mapToGlobal(before), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &we);
        QVERIFY(v->zoom() > 1.3);
        const QPointF after = v->pageToWidget(anchorPage);
        QVERIFY(std::abs(after.x() - before.x()) < 0.5 && std::abs(after.y() - before.y()) < 0.5);
        v->zoomIn(); v->zoomIn();
        const double z = v->zoom();
        QVERIFY(z > 2.0);
        // refreshing the preview (e.g. after adding a text) must not reset the zoom
        p.addNeedle("zzz-not-there"); waitIdle(p);
        QCOMPARE(v->zoom(), z);
        // pan with the right button
        const QPointF a0 = v->pageToWidget({100, 100});
        QTest::mousePress(v, Qt::RightButton, Qt::NoModifier, QPoint(300, 300));
        QTest::mouseMove(v, QPoint(340, 330));
        QTest::mouseRelease(v, Qt::RightButton, Qt::NoModifier, QPoint(340, 330));
        const QPointF a1 = v->pageToWidget({100, 100});
        QVERIFY(std::abs((a1.x() - a0.x()) - 40) < 1 && std::abs((a1.y() - a0.y()) - 30) < 1);
        p.grab().save("/tmp/claude-0/shots/zoom1.png");
        v->setSelectEnabled(true);
        QSignalSpy boxed(v, &DrawView::boxed);
        const QPointF p0 = v->pageToWidget({120, 140}), p1 = v->pageToWidget({150, 180});
        QTest::mousePress(v, Qt::LeftButton, Qt::NoModifier, p0.toPoint());
        QTest::mouseMove(v, p1.toPoint());
        QTest::mouseRelease(v, Qt::LeftButton, Qt::NoModifier, p1.toPoint());
        QCOMPARE(boxed.size(), 1);
        QVERIFY(std::abs(boxed[0][0].toDouble() - 120) < 1.5 && std::abs(boxed[0][3].toDouble() - 180) < 1.5);
        v->zoomFit();
        QCOMPARE(v->zoom(), 1.0);
        // clear the queue
        QVERIFY(p.clearQueue());
        QCOMPARE(p.files().size(), 0);
        QCOMPARE(p.groupCount(), 0);
        QCOMPARE(p.step(), 0);
        QVERIFY(p.outputDir().isEmpty());
        // and start again
        QCOMPARE(p.addFiles({td.filePath("a.pdf")}), 1);
        waitIdle(p);
        QCOMPARE(p.groupCount(), 1);
    }

    // Real-world files (set WM_REAL to a ';'-separated list of paths): the whole flow must finish in reasonable time.
    void realFiles() {
        const QStringList paths = QString::fromLocal8Bit(qgetenv("WM_REAL")).split(';', Qt::SkipEmptyParts);
        if (paths.isEmpty()) QSKIP("WM_REAL not set");
        WatermarkPanel p;
        p.setSilent(true);
        p.resize(1360, 820);
        p.show();
        QElapsedTimer t; t.start();
        p.addFiles(paths);
        waitIdle(p, 120000);
        qInfo() << "analysed in" << t.elapsed() << "ms, groups" << p.groupCount();
        QVERIFY(!p.busy());
        for (auto& f : p.files()) qInfo() << f.name << "group" << f.group << "sel" << f.selected << "pages" << f.rep.pageCount << "findings" << (int)f.rep.findings.size();
        p.goStep(1);
        for (int g = 0; g < p.groupCount(); ++g) {
            t.restart();
            p.selectGroup(g);
            waitIdle(p, 120000);
            qInfo() << "review group" << g << t.elapsed() << "ms";
            QVERIFY(!p.busy());
        }
        QCOMPARE(p.step(), 1);
        // pick the manual group, box a shape, commit, tick the file, run
        if (p.hasManualGroup()) {
            const int g = p.groupCount() - 1;
            p.selectGroup(g); waitIdle(p, 120000);
            int fi = -1; for (int i = 0; i < p.files().size(); ++i) if (p.files()[i].group == g) { fi = i; break; }
            if (fi >= 0) {
                p.setSample(fi); p.setPage(2); waitIdle(p, 120000);
                t.restart();
                p.boxShape(315, 590, 365, 712); waitIdle(p, 120000);
                qInfo() << "box" << t.elapsed() << "ms" << p.shapeStatus();
                if (p.commitShape("x")) {
                    waitIdle(p, 120000);
                    qInfo() << "commit+preview" << t.elapsed() << "ms";
                    for (int pg = 1; pg <= 4; ++pg) { t.restart(); p.setPage(pg); waitIdle(p, 120000); qInfo() << "page" << pg << t.elapsed() << "ms"; }
                    p.setFileSelected(fi, true);
                    p.setOutputDir(QDir::tempPath() + "/wm_real_out");
                    QSignalSpy done(&p, &WatermarkPanel::runFinished);
                    t.restart();
                    p.startRun();
                    QVERIFY(done.wait(180000));
                    waitIdle(p, 120000);
                    qInfo() << "run" << t.elapsed() << "ms";
                    for (auto& f : p.files()) qInfo() << f.name << f.status << f.res.removedTotal() << f.note;
                }
            }
        }
    }

    // CPU must go quiet once the work is done (no busy loops, no piled-up jobs)
    void idleIsQuiet() {
        const QStringList paths = QString::fromLocal8Bit(qgetenv("WM_REAL")).split(';', Qt::SkipEmptyParts);
        if (paths.isEmpty()) QSKIP("WM_REAL not set");
        MainWindow w;
        w.resize(1360, 820);
        w.show();
        w.showWatermark(true);
        auto* p = w.watermarkPanel();
        p->addFiles(paths);
        waitIdle(*p, 120000);
        p->goStep(1);
        for (int g = 0; g < p->groupCount(); ++g) { p->selectGroup(g); waitIdle(*p, 120000); }
        auto cpu = [] { struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6; };
        const double c0 = cpu();
        QTest::qWait(4000);
        const double used = cpu() - c0;
        qInfo() << "idle cpu seconds in 4 s:" << used;
        QVERIFY2(used < 0.5, "UI keeps burning CPU while idle");
        // flipping pages quickly must not leave a backlog behind
        p->selectGroup(0); waitIdle(*p, 120000);
        const double c1 = cpu();
        for (int k = 0; k < 20; ++k) { p->setPage(1 + k % 5); QTest::qWait(10); }
        waitIdle(*p, 120000);
        qInfo() << "20 quick page flips cost cpu seconds:" << cpu() - c1;
    }

    // PLT -> PDF must stay responsive: no long main-thread stalls, no piled-up background work, quiet when idle.
    void pltToPdfStaysResponsive() {
        const QString pltDir = QString::fromLocal8Bit(qgetenv("PLT_DATA"));
        if (pltDir.isEmpty()) QSKIP("PLT_DATA not set");
        auto cpu = [] { struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6; };
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QTemporaryDir cfg; QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfg.path());
        QFile::remove(batchSessionPath());
        struct Cleanup { ~Cleanup() { QFile::remove(batchSessionPath()); } } cleanup;   // never leave a session behind (it would pop a restore dialog in the next test)
        // main-thread stall monitor
        QElapsedTimer clock; clock.start();
        qint64 last = 0, worst = 0;
        QTimer tick; tick.setInterval(10);
        QObject::connect(&tick, &QTimer::timeout, [&] { const qint64 n = clock.elapsed(); worst = std::max(worst, n - last - 10); last = n; });
        auto resetWorst = [&] { worst = 0; last = clock.elapsed(); };
        tick.start();
        auto settle = [&](MainWindow& w, int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QTest::qWait(20); (void)w; };

        MainWindow w;
        w.setSilent(true);
        w.resize(1360, 820);
        w.show();
        QSignalSpy loaded(&w, &MainWindow::loaded);
        double c0 = cpu(); resetWorst();
        QString biggest; qint64 bs = -1;
        for (auto& fi : QDir(pltDir).entryInfoList({"*.plt"}, QDir::Files)) if (fi.size() > bs) { bs = fi.size(); biggest = fi.absoluteFilePath(); }
        qInfo() << "largest file:" << biggest << bs / 1024 << "KB";
        w.loadFile(biggest);
        QVERIFY(loaded.wait(30000));
        QVERIFY(loaded.at(0).at(0).toBool());
        qInfo() << "load: cpu" << cpu() - c0 << "worst main-thread stall ms" << worst;
        QVERIFY2(worst < 400, "loading stalls the UI");
        w.goStep(1);
        QSignalSpy plan(&w, &MainWindow::planReady);
        QTest::qWait(800);
        // drag-like burst of edits
        c0 = cpu(); resetWorst();
        for (int k = 0; k < 60; ++k) { w.edit([k](State& s) { s.offX = k * 0.5; s.offY = (k % 7) * 0.5; }, "drag"); QTest::qWait(15); }
        settle(w, 3000);
        qInfo() << "60 quick edits: cpu" << cpu() - c0 << "worst stall ms" << worst << "plans" << plan.size();
        QVERIFY2(worst < 400, "layout edits stall the UI");
        QVERIFY2(cpu() - c0 < 25, "layout edits burn too much CPU");
        // zoom / pan on the canvas
        auto* cv = w.findChild<CanvasWidget*>();
        QVERIFY(cv);
        c0 = cpu(); resetWorst();
        for (int k = 0; k < 40; ++k) {
            QWheelEvent we(QPointF(300, 300), cv->mapToGlobal(QPointF(300, 300)), QPoint(), QPoint(0, k % 2 ? -120 : 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(cv, &we);
            QTest::qWait(10);
        }
        settle(w, 500);
        qInfo() << "40 wheel zooms: cpu" << cpu() - c0 << "worst stall ms" << worst;
        QVERIFY2(worst < 500, "zoom stalls the UI");
        // idle
        c0 = cpu(); QTest::qWait(3000);
        qInfo() << "idle cpu in 3 s:" << cpu() - c0;
        QVERIFY2(cpu() - c0 < 0.4, "single-file view burns CPU while idle");

        // export
        QTemporaryDir out;
        c0 = cpu(); resetWorst();
        const QString pdf = w.exportBlocking(out.filePath("single.pdf"));
        qInfo() << "export:" << pdf << "cpu" << cpu() - c0 << "worst stall ms" << worst;
        QVERIFY2(worst < 600, "export stalls the UI");

        // batch of 20
        QTemporaryDir bd;
        const QStringList src = QDir(pltDir).entryList({"*.plt"}, QDir::Files);
        QStringList files;
        for (int k = 0; k < 20; ++k) {
            const QString dst = bd.filePath(QString("f%1_%2").arg(k, 2, 10, QChar('0')).arg(src[k % src.size()]));
            QVERIFY(QFile::copy(pltDir + "/" + src[k % src.size()], dst));
            files << dst;
        }
        w.setBatchMode(true);
        c0 = cpu(); resetWorst();
        w.addBatchFiles(files);
        QElapsedTimer bt; bt.start();
        while (bt.elapsed() < 60000) { QTest::qWait(50); bool all = true; for (auto& it : w.batchItems()) if (it->status == ItemStatus::Scanning) all = false; if (w.batchItems().size() == 20 && all) break; }
        qInfo() << "batch add 20: cpu" << cpu() - c0 << "worst stall ms" << worst << "queued" << w.batchItems().size();
        QVERIFY2(worst < 600, "adding files stalls the UI");
        w.goBatchStep(2);
        c0 = cpu(); resetWorst();
        QSignalSpy autoDone(&w, &MainWindow::batchAutoFinished);
        w.batchStartAuto();
        if (autoDone.isEmpty()) QVERIFY(autoDone.wait(120000));
        qInfo() << "batch auto layout: cpu" << cpu() - c0 << "worst stall ms" << worst;
        QVERIFY2(worst < 600, "auto layout stalls the UI");
        w.batchConfirmAll();
        w.batchOptions().outDir = out.filePath("batch");
        w.batchOptions().concurrency = 2;
        c0 = cpu(); resetWorst();
        QSignalSpy expDone(&w, &MainWindow::batchExportFinished);
        w.batchStartExport();
        if (expDone.isEmpty()) QVERIFY(expDone.wait(300000));
        qInfo() << "batch export: cpu" << cpu() - c0 << "worst stall ms" << worst << "pdfs" << QDir(out.filePath("batch")).entryList({"*.pdf"}).size();
        QVERIFY2(worst < 800, "batch export stalls the UI");
        c0 = cpu(); QTest::qWait(3000);
        qInfo() << "idle cpu after batch in 3 s:" << cpu() - c0;
        QVERIFY2(cpu() - c0 < 0.4, "batch view burns CPU while idle");
    }

    // Regression: flipping pages / clicking around in the review step must not re-run the recipe over the whole file
    // for every click, and stale requests must not pile up (this once burnt minutes of CPU and made the UI look hung).
    void reviewDoesNotPileUpWork() {
        QTemporaryDir td;
        QVERIFY(QFile::copy(data_ + "/C_flat.pdf", td.filePath("a.pdf")));
        WatermarkPanel p;
        p.setSilent(true);
        p.resize(1360, 820);
        p.show();
        p.addFiles({td.filePath("a.pdf")});
        waitIdle(p);
        p.goStep(1); p.selectGroup(0); waitIdle(p);
        const int base = p.previewApplyCount();
        QVERIFY(base >= 1);
        const int pages = p.files()[0].rep.pageCount;
        for (int k = 0; k < 40; ++k) { p.setPage(1 + k % pages); QTest::qWait(3); }
        waitIdle(p);
        QCOMPARE(p.previewApplyCount(), base);                 // pages of the same file + recipe: drawing only
        p.addNeedle("zzz-not-there"); waitIdle(p);              // recipe changed: exactly one more pass
        QCOMPARE(p.previewApplyCount(), base + 1);
        for (int k = 0; k < 40; ++k) { p.setPage(1 + k % pages); QTest::qWait(3); }
        waitIdle(p);
        QCOMPARE(p.previewApplyCount(), base + 1);
        QVERIFY(!p.busy());
    }

    // The "开始处理" button must not stay grey after the user told the manual group what to remove.
    void manualGroupGetsTickedWhenGivenWork() {
        QTemporaryDir td;
        QVERIFY(QFile::copy(data_ + "/A_clean.pdf", td.filePath("a.pdf")));
        WatermarkPanel p;
        p.setSilent(true);
        p.resize(1360, 820);
        p.show();
        p.addFiles({td.filePath("a.pdf")});
        waitIdle(p);
        QVERIFY(p.hasManualGroup());
        QVERIFY(!p.files()[0].selected);
        p.goStep(1); p.selectGroup(0); waitIdle(p);
        p.addNeedle("something");
        waitIdle(p);
        QVERIFY(p.files()[0].selected);
    }

    void insideMainWindow() {
        MainWindow w;
        w.resize(1360, 820);
        w.show();
        QVERIFY(!w.watermarkVisible());
        w.showWatermark(true);
        QVERIFY(w.watermarkVisible());
        QTemporaryDir td;
        QFile::copy(data_ + "/G_acrobat_bg.pdf", td.filePath("G.pdf"));
        auto* p = w.watermarkPanel();
        QCOMPARE(p->addFiles({td.filePath("G.pdf")}), 1);
        waitIdle(*p);
        QCOMPARE(p->groupCount(), 1);
        w.grab().save("/tmp/claude-0/shots/wm5_window.png");
        w.showWatermark(false);
        QVERIFY(!w.watermarkVisible());
    }
};

QTEST_MAIN(WmGuiTest)
#include "test_wmgui.moc"
