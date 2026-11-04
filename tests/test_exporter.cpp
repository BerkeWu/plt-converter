// Smoke test: export a real PLT through PdfExporterQt and check the PDF is produced.
#include <QGuiApplication>
#include <QFile>
#include <iostream>
#include "../qt/PdfExporterQt.h"
#include "../core/plt.h"

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc < 3) { std::cerr << "usage: test_exporter in.plt out.pdf\n"; return 2; }
    plt::Document doc = plt::parseFile(argv[1]);
    layout::Params p; p.overview = true; p.orient = layout::Orient::Landscape;
    layout::Plan plan = layout::buildPlan(doc, p);
    QString err = PdfExporterQt::write(plan, QString::fromUtf8(argv[2]));
    if (!err.isEmpty()) { std::cerr << "error: " << err.toStdString() << "\n"; return 1; }
    std::cout << "pages " << plan.pages.size() << " -> " << argv[2] << "\n";
    return 0;
}
