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
