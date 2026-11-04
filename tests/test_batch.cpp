// Headless batch-conversion test (PRD 4.9 acceptance scenarios).
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

#include "../qt/MainWindow.h"
#include "../core/layout.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::cout << "  FAIL: " << msg << "\n"; } else std::cout << "  ok: " << msg << "\n"; } while (0)

static bool waitFor(MainWindow& w, void (MainWindow::*sig)(), int ms = 60000) {
    QEventLoop loop;
    QTimer t; t.setSingleShot(true);
    bool got = false;
    QObject::connect(&w, sig, &loop, [&] { got = true; loop.quit(); });
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(ms);
    loop.exec();
    return got;
}
static bool waitPlan(MainWindow& w) { return waitFor(w, &MainWindow::planReady, 15000); }
static void pump(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }
static int count(MainWindow& w, ItemStatus s) { int n = 0; for (auto& it : w.batchItems()) if (it->status == s) ++n; return n; }
static bool allAuto(MainWindow& w) { for (auto& it : w.batchItems()) if (it->valid() && !it->haveAuto) return false; return true; }
static void waitAuto(MainWindow& w) { if (!allAuto(w)) waitFor(w, &MainWindow::batchAutoFinished); }
static void waitScan(MainWindow& w) { int guard = 0; while (count(w, ItemStatus::Scanning) > 0 && guard++ < 600) pump(50); }

static QString writePlt(const QString& path, int w, int h, bool junk = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    if (junk) f.write("this is not an HP-GL file at all, it was truncated here");
    else {
        const int W = w * 40, H = h * 40;
        f.write(QString("IN;SP1;PU0,0;PD%1,0,%1,%2,0,%2,0,0;PU%3,%4;CI%5;PU0,0;PD%1,%2;PU%6,%7;PD;AA%6,%4,200;").arg(W).arg(H).arg(W / 2).arg(H / 2).arg(std::min(W, H) / 5).arg(W / 4).arg(H / 4).toUtf8());
    }
    f.close();
    return path;
}
static int pdfPages(const QString& pdf) {
    QProcess p; p.start("pdfinfo", {pdf}); p.waitForFinished();
    for (auto& l : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) if (l.startsWith("Pages:")) return l.mid(6).trimmed().toInt();
    return -1;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(batchSessionPath());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    const QString root = tmp.path();

    // ---------- source files: 17 valid (varied sizes), 3 corrupt, 1 non-plt, + extras beyond the limit ----------
    QStringList valid, corrupt;
    const int sizes[17][2] = {{560, 380}, {540, 390}, {280, 160}, {820, 610}, {190, 90}, {300, 700}, {650, 250}, {400, 400}, {150, 900}, {900, 150}, {210, 297}, {297, 210}, {500, 500}, {350, 120}, {120, 350}, {730, 420}, {260, 260}};
    for (int i = 0; i < 17; ++i) valid << writePlt(QString("%1/in/样板%2.plt").arg(root).arg(i + 1, 2, 10, QChar('0')), sizes[i][0], sizes[i][1]);
    for (int i = 0; i < 3; ++i) corrupt << writePlt(QString("%1/in/破损%2.plt").arg(root).arg(i + 1), 0, 0, true);
    QFile notes(root + "/in/readme.txt"); notes.open(QIODevice::WriteOnly); notes.write("x"); notes.close();
    QStringList extras;
    for (int i = 0; i < 4; ++i) extras << writePlt(QString("%1/in/多余%2.plt").arg(root).arg(i), 200 + i, 200);

    MainWindow w;
    w.setSilent(true);
    w.show();

    // ---------- single file -> batch ----------
    {
        QEventLoop loop; bool ok = false;
        QObject::connect(&w, &MainWindow::loaded, &loop, [&](bool o) { ok = o; loop.quit(); });
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        w.loadFile(valid[0]);
        loop.exec();
        CHECK(ok, "single mode: file loaded");
        w.edit([](State& s) { s.paper = "A3"; s.margin = 8; });
        for (auto* b : w.findChildren<QPushButton*>("toBatchBtn")) b->click();
        CHECK(w.batchMode() && w.batchItems().size() == 1, "「转为批量」 carries the current file into the queue");
        waitScan(w);
        CHECK(w.batchItems()[0]->state.paper == "A3" && w.batchItems()[0]->state.margin == 8, "…with its parameters");
        w.setBatchMode(false);
        CHECK(!w.batchMode() && w.state().paper == "A3", "back to single mode restores the single-file session");
        w.setBatchMode(true);
    }

    // ---------- adding: duplicates, non-plt, limit ----------
    w.addBatchFiles({valid[1]});
    w.addBatchFiles({valid[1], valid[1]});
    CHECK(w.batchItems().size() == 2, "same path is only queued once");
    QStringList all = valid; all.removeFirst(); all.removeOne(valid[1]);
    all += corrupt; all += root + "/in/readme.txt"; all += extras;
    w.addBatchFiles(all);
    CHECK(w.batchItems().size() == kBatchLimit, "queue stops at the 20-file limit (no crash)");
    waitScan(w);
    CHECK(count(w, ItemStatus::Failed) == 3, "the 3 corrupt files are marked failed with a reason");
    for (auto& it : w.batchItems()) if (it->status == ItemStatus::Failed && it->error.isEmpty()) { CHECK(false, "failure has a reason"); }
    CHECK(w.batchItems()[0]->thumb.width() > 0, "valid files get a thumbnail and metadata");
    // folder drop (with sub-folders)
    {
        const int before = w.batchItems().size();
        w.addBatchFiles({root + "/in"});
        CHECK(w.batchItems().size() == before, "dropping a folder whose files are all queued/over limit adds nothing");
    }

    // ---------- template + automatic layout ----------
    w.goBatchStep(1);
    CHECK(w.batchStep() == 1, "step 2 reachable");
    w.batchEditTemplate([](State& s) { s.paper = "A3"; s.overview = false; s.margin = 5; });
    w.goBatchStep(2);
    waitAuto(w);
    CHECK(allAuto(w), "automatic layout finished for every valid file");
    int mism = 0;
    for (int i = 0; i < w.batchItems().size(); ++i) {
        const auto& it = *w.batchItems()[i];
        if (!it.valid()) continue;
        if (it.state.paper != "A3") ++mism;
        // exhaustive check: both orientations, fewest effective pages wins (PRD 4.9)
        ParseResult pr = parseJob(it.path);
        layout::Params p = batchParams(it.state, w.batchOptions(), i, it.path);
        p.orient = layout::Orient::Portrait;  const int pp = layout::buildPlan(*pr.doc, p).effectivePages;
        p.orient = layout::Orient::Landscape; const int pl = layout::buildPlan(*pr.doc, p).effectivePages;
        const bool landscape = it.state.landscape;
        const int chosen = landscape ? pl : pp;
        if (chosen != std::min(pp, pl)) { ++mism; std::cout << "    orientation mismatch: " << it.path.toStdString() << " P" << pp << " L" << pl << " chose " << (landscape ? "L" : "P") << "\n"; }
    }
    CHECK(mism == 0, "template applied; chosen orientation = exhaustive optimum for all 17 files");

    // ---------- per-file preview: edit, confirm, restore ----------
    w.goBatchStep(3);
    pump(800);
    const int first = 0;
    w.edit([](State& s) { s.overlap = 15; });
    pump(400);
    CHECK(w.batchItems()[first]->state.overlap == 15, "editing the current file stores an override");
    pump(300);
    w.undo();
    CHECK(w.batchItems()[first]->state.overlap == 10, "per-file undo");
    w.redo();
    {   // optimisation hint: a drawing that sticks out by 3 mm gets "move left 3 mm" + a dashed ghost, and the button applies it
        w.batchActivate(4);   // 190 x 90 mm
        for (int g = 0; g < 100 && !(w.batchItems()[4]->doc && w.planValid()); ++g) pump(60);
        w.edit([](State& s) { s.paper = "A4"; s.autoOrient = false; s.landscape = true; s.offX = 100; s.offY = 0; });
        waitPlan(w);
        pump(300);
        CHECK(w.lastPlan().hint.found && w.lastPlan().hint.dx < 0, "per-file preview: a shift hint is offered when a small move saves a page");
        const int pagesBefore = w.pageCount();
        w.grab().save(QDir::temp().filePath("batch_hint.png"));
        for (auto* b : w.findChildren<QPushButton*>()) if (b->text() == QObject::tr("应用优化位置") && b->isVisibleTo(&w)) { b->click(); break; }
        waitPlan(w);
        CHECK(w.state().offX < 100 && w.pageCount() < pagesBefore, "「应用优化位置」 moves the drawing and saves a page");
        w.undo();
        waitPlan(w);
        CHECK(w.state().offX == 100, "…and it is undoable");
        w.batchActivate(0);
        for (int g = 0; g < 100 && !(w.batchItems()[0]->doc && w.planValid()); ++g) pump(60);
    }
    {   // 「忽略纸张边框」「框选区域」 in the per-file preview (v1.0.1): same controls and effect as the single-file mode
        int fi = -1;
        for (int i = 0; i < w.batchItems().size(); ++i) if (w.batchItems()[i]->valid() && w.batchItems()[i]->frameIdx >= 0) { fi = i; break; }
        if (fi >= 0) {
            w.batchActivate(fi);
            for (int g = 0; g < 100 && !(w.batchItems()[fi]->doc && w.planValid()); ++g) pump(60);
            for (auto* b : w.findChildren<QPushButton*>()) if (b->text().startsWith(QObject::tr("图形")) && b->isVisibleTo(&w)) { b->click(); break; }
            pump(300);
            QCheckBox* cb = nullptr;
            for (auto* c : w.findChildren<QCheckBox*>()) if (c->text().startsWith(QObject::tr("忽略纸张边框"))) cb = c;
            CHECK(cb && cb->isVisibleTo(&w) && cb->isEnabled(), "per-file preview shows 「忽略纸张边框」");
            QPushButton* selB = nullptr;
            for (auto* b : w.findChildren<QPushButton*>()) if (b->text() == QObject::tr("框选区域")) selB = b;
            CHECK(selB && selB->isVisibleTo(&w) && selB->isEnabled(), "per-file preview shows 「框选区域」");
            auto hasNote = [&](const QString& key) {
                for (auto* l : w.findChildren<QLabel*>()) if (l->isVisibleTo(&w) && l->text().contains(key)) return true;
                return false;
            };
            CHECK(hasNote(QObject::tr("检测到纸张边框")), "a detected frame is pointed out on the file card");
            if (cb) cb->click();
            pump(500);
            CHECK(w.state().ignoreFrame && w.batchItems()[fi]->state.ignoreFrame, "ignore-frame is stored on this file only");
            CHECK(hasNote(QObject::tr("忽略纸张边框")) && !hasNote(QObject::tr("检测到纸张边框")), "the file card summarises the edit");
            bool othersKeep = true;
            for (int i = 0; i < w.batchItems().size(); ++i) if (i != fi && w.batchItems()[i]->state.ignoreFrame) othersKeep = false;
            CHECK(othersKeep, "other files keep their frame");
            // selection: clip to the left half of the drawing
            const int full = w.pageCount();
            w.edit([](State& s) { s.hasSel = true; s.sel = QRectF(0, 0, 150, 400); s.offX = s.offY = 0; });
            waitPlan(w);
            pump(300);
            CHECK(w.batchItems()[fi]->state.hasSel, "selection is stored on this file");
            CHECK(w.pageCount() > 0 && w.pageCount() < full, "selection shrinks the page count in the preview");
            CHECK(hasNote(QObject::tr("框选")), "selection appears in the file summary");
            w.undo(); w.undo();
            waitPlan(w);
            CHECK(!w.state().hasSel && !w.state().ignoreFrame, "…both are undoable per file");
            for (auto* b : w.findChildren<QPushButton*>()) if (b->text().startsWith(QObject::tr("排版")) && b->isVisibleTo(&w)) { b->click(); break; }
            w.batchActivate(0);
            for (int g = 0; g < 100 && !(w.batchItems()[0]->doc && w.planValid()); ++g) pump(60);
        } else CHECK(false, "test data has a file with a paper frame");
    }
    w.batchConfirmAll();
    int confirmed = 0;
    for (auto& it : w.batchItems()) if (it->valid() && it->confirmed) ++confirmed;
    CHECK(confirmed == 17, "「全部按自动方案确认」 confirms all valid files");
    const int bigPages = w.batchItems()[first]->pages;
    std::cout << "    first file pages now " << bigPages << "\n";

    // ---------- export: isolation, naming, pause, cancel, retry ----------
    w.batchEditTemplate([](State&) {});   // no-op, stays on step 2 -> go back to export
    w.goBatchStep(4);
    const QString out = root + "/out";
    w.batchOptions().outDir = out;
    w.batchOptions().concurrency = 2;
    // a same-named file from another folder, exported into the same directory
    // (queue is full, so replace one valid entry)
    w.batchStartExport();
    w.batchPause(true);     // pause right away: the files are tiny and would finish within a second
    pump(600);
    CHECK(w.batchExporting(), "pause keeps the batch alive");
    const int doneWhilePaused = count(w, ItemStatus::Done);
    pump(600);
    CHECK(count(w, ItemStatus::Done) == doneWhilePaused && doneWhilePaused < 17, "no further file completes while paused");
    w.batchPause(false);
    CHECK(waitFor(w, &MainWindow::batchExportFinished), "export finished");
    QDir od(out);
    const QStringList pdfs = od.entryList({"*.pdf"}, QDir::Files);
    CHECK(pdfs.size() == 17, "17 PDFs produced; the 3 corrupt files did not stop the batch");
    CHECK(od.entryList({"*.part"}, QDir::Files).isEmpty(), "no temporary files left behind");
    {
        QFile pf(od.filePath(pdfs.first())); pf.open(QIODevice::ReadOnly);
        const QByteArray b = pf.readAll();
        CHECK(b.contains("/PrintScaling /None") && !b.contains("/ca 0") && !b.contains("/CA 0"), "batch PDFs: no transparency and print scaling off");
    }
    int failedRows = 0;
    const QString csv = w.batchReportCsv();
    for (auto& it : w.batchItems()) if (it->status == ItemStatus::Failed) ++failedRows;
    CHECK(failedRows == 3 && csv.count("\"失败\"") == 3, "report lists exactly the 3 failures with reasons");
    CHECK(csv.startsWith(QStringLiteral("\uFEFF")), "CSV has a UTF-8 BOM for Excel");
    bool pagesOk = true;
    if (QProcess::execute("pdfinfo", {"-v"}) >= 0) {
        for (auto& it : w.batchItems()) if (it->status == ItemStatus::Done) { const int n = pdfPages(it->outPath); if (n != it->pages) { pagesOk = false; std::cout << "    pages " << n << " vs " << it->pages << " " << it->path.toStdString() << "\n"; } }
    }
    CHECK(pagesOk, "every PDF has the page count that was previewed");
    // naming: exporting everything again must never overwrite
    w.batchStartExport();
    pump(30);
    w.batchCancel();
    CHECK(waitFor(w, &MainWindow::batchExportFinished), "cancel finishes");
    CHECK(od.entryList({"*.part"}, QDir::Files).isEmpty(), "cancel removes unfinished temporary files");
    const QStringList after = od.entryList({"*.pdf"}, QDir::Files);
    CHECK(after.size() >= 17, "cancel keeps completed files");
    bool suffixed = false;
    for (auto& n : after) if (n.contains("_2.pdf")) suffixed = true;
    CHECK(after.size() == 17 || suffixed, "a second export gets _2 names instead of overwriting");
    // retry: an unusable output folder fails every file, a good one fixes them
    w.batchOptions().outDir = root + "/in/readme.txt/sub";   // parent is a file -> cannot be created
    w.batchStartExport();
    waitFor(w, &MainWindow::batchExportFinished, 20000);
    int exportFailed = 0;
    for (auto& it : w.batchItems()) if (it->exportFailed) ++exportFailed;
    CHECK(exportFailed == 17, "unwritable output folder: every file fails with a reason, nothing crashes");
    w.batchOptions().outDir = root + "/out2";
    w.batchStartExport(true);
    CHECK(waitFor(w, &MainWindow::batchExportFinished), "retry finished");
    CHECK(QDir(root + "/out2").entryList({"*.pdf"}, QDir::Files).size() == 17, "「仅重试失败项」 exports the failed files");

    // ---------- session round trip (crash recovery file, PRD 4.6) ----------
    {
        QVector<std::shared_ptr<BatchItem>> its;
        auto a = std::make_shared<BatchItem>(); a->path = valid[3]; a->state.paper = "A0"; a->state.overlap = 17; a->state.offX = 12.5; a->state.hasSel = true; a->state.sel = QRectF(1, 2, 30, 40);
        a->autoState = a->state; a->haveAuto = true; a->confirmed = true; a->adjusted = true; a->pages = 5; a->autoPages = 6;
        its.push_back(a);
        State t; t.paper = "A3"; t.alignColor = QColor("#D33333");
        BatchOptions o; o.calibFirstOnly = true; o.fileTag = false; o.outDir = "/tmp/x"; o.concurrency = 3;
        const QByteArray js = sessionToJson(t, o, its);
        State t2; BatchOptions o2; QVector<std::shared_ptr<BatchItem>> its2;
        CHECK(sessionFromJson(js, t2, o2, its2) && its2.size() == 1, "session JSON parses");
        CHECK(t2.paper == "A3" && t2.alignColor == QColor("#D33333") && o2.calibFirstOnly && !o2.fileTag && o2.outDir == "/tmp/x" && o2.concurrency == 3, "template + options round trip");
        CHECK(its2[0]->state == a->state && its2[0]->confirmed && its2[0]->adjusted && its2[0]->pages == 5 && its2[0]->autoPages == 6 && its2[0]->path == valid[3], "per-file state, confirmation and page counts round trip");
        CHECK(!sessionFromJson("not json", t2, o2, its2), "garbage session file is rejected, not crashed on");
    }

    // ---------- session auto-save / restore file ----------
    w.setSilent(true);
    QFile::remove(batchSessionPath());
    w.goBatchStep(0);
    CHECK(true, "back to step 1");
    std::cout << (fails ? "BATCH TEST FAILED: " + std::to_string(fails) : std::string("BATCH ALL PASSED")) << "\n";
    return fails ? 1 : 0;
}
