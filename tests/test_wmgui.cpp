// Offscreen test of the 水印处理 panel: add -> analyse/group -> recipe (text + boxed shape) -> run -> report.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

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
            QVERIFY(QFileInfo::exists(out + "/" + QFileInfo(names[i]).completeBaseName() + QString::fromUtf8("_去水印.pdf")));
        }
        QVERIFY(p.files()[2].res.textsRemoved > 0);
        QVERIFY(QFileInfo::exists(out + QString::fromUtf8("/去水印报告.csv")));
        QVERIFY(QFileInfo(td.filePath(names[0])).size() > 0);   // originals untouched
        p.grab().save("/tmp/claude-0/shots/wm4_run.png");
        // a second run never overwrites: names get a counter
        QSignalSpy done2(&p, &WatermarkPanel::runFinished);
        p.startRun();
        QVERIFY(done2.wait(120000));
        QVERIFY(QFileInfo::exists(out + QString::fromUtf8("/C_flat_去水印(2).pdf")));
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
            QVERIFY(c.endsWith("_去水印.plt"));
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
