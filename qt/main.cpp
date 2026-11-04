#include <QApplication>
#include <QDir>
#include <QTimer>
#include <functional>
#include <iostream>

#include "MainWindow.h"

// Headless self-check used in the dev sandbox: plt_gui --demo file.plt outdir
// (QT_QPA_PLATFORM=offscreen): imports, screenshots all three steps, exercises edits, exports a PDF.
static void runDemo(MainWindow& w, const QString& file, const QString& dir) {
    QDir().mkpath(dir);
    auto shot = [&w, dir](const QString& name) { w.grab().save(QDir(dir).filePath(name)); std::cout << "saved " << name.toStdString() << "\n"; };
    auto stage = std::make_shared<int>(0);
    QObject::connect(&w, &MainWindow::loaded, &w, [&w, shot](bool ok) {
        if (!ok) { std::cout << "load failed\n"; QApplication::exit(1); return; }
        QTimer::singleShot(400, [&w, shot] { shot("step1_import.png"); if (qEnvironmentVariableIsSet("PLT_DEMO_RED")) w.edit([](State& s) { s.origColor = false; s.color = QColor("#D33333"); s.lineWidth = 0.5; }); w.goStep(1); });
    });
    QObject::connect(&w, &MainWindow::planReady, &w, [&w, shot, stage, dir] {
        if (*stage == 0) {
            *stage = 1;
            QTimer::singleShot(500, [&w, shot, stage] { shot("step2_layout.png"); w.goStep(2); *stage = 2; QTimer::singleShot(500, [&w, shot, stage] { shot("step3_export.png"); *stage = 3; w.edit([](State& s) { s.overview = true; s.paper = "A3"; }); }); });
        } else if (*stage == 3) {
            *stage = 4;
            QTimer::singleShot(400, [&w, shot, dir, stage] {
                shot("step3_a3_overview.png");
                const QString out = QDir(dir).filePath("demo.pdf");
                QFile::remove(out);
                const QString err = w.exportBlocking(out);
                std::cout << "export: " << (err.isEmpty() ? "ok" : err.toStdString()) << ", pages " << w.pageCount() << "\n";
                QApplication::exit(err.isEmpty() ? 0 : 1);
            });
        }
    });
    w.loadFile(file);
}

// plt_gui --demo-batch outdir a.plt b.plt ...   (offscreen) screenshots every batch step and exports into outdir/pdf
static void runBatchDemo(MainWindow& w, const QString& dir, const QStringList& files) {
    QDir().mkpath(dir);
    w.setSilent(true);
    auto shot = [&w, dir](const QString& name) { w.grab().save(QDir(dir).filePath(name)); std::cout << "saved " << name.toStdString() << "\n"; };
    auto after = [](int ms, std::function<void()> f) { QTimer::singleShot(ms, f); };
    w.setBatchMode(true);
    w.addBatchFiles(files);
    QObject::connect(&w, &MainWindow::batchScanFinished, &w, [&w, shot, after, dir]() {
        after(300, [&w, shot, after, dir] {
            shot("b1_add.png");
            w.goBatchStep(1);
            after(300, [&w, shot, after, dir] {
                shot("b2_template.png");
                w.batchEditTemplate([](State& s) { s.paper = "A3"; });
                w.goBatchStep(2);
                after(1500, [&w, shot, after, dir] {
                    shot("b3_auto.png");
                    w.goBatchStep(3);
                    after(1500, [&w, shot, after, dir] {
                        shot("b4_preview.png");
                        w.batchOptions().outDir = QDir(dir).filePath("pdf");
                        w.goBatchStep(4);
                        w.batchStartExport();
                        QObject::connect(&w, &MainWindow::batchExportFinished, &w, [&w, shot, after] { after(300, [&w, shot] { shot("b5_export.png"); QApplication::exit(0); }); });
                    });
                });
            });
        });
    }, Qt::SingleShotConnection);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PLT Converter");
    app.setOrganizationName("HaoyangPattern");
    MainWindow w;
    w.show();
    const QStringList args = app.arguments();
    const int bi = args.indexOf("--demo-batch");
    if (bi >= 0 && args.size() > bi + 2) { runBatchDemo(w, args[bi + 1], args.mid(bi + 2)); return app.exec(); }
    const int di = args.indexOf("--demo");
    if (di >= 0 && args.size() > di + 2) {
        runDemo(w, args[di + 1], args[di + 2]);
    } else if (args.size() > 1 && args[1].endsWith(".plt", Qt::CaseInsensitive)) {
        QTimer::singleShot(0, [&w, f = args[1]] { w.loadFile(f); });
    }
    return app.exec();
}
