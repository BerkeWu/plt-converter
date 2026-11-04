// CLI: plt2pdf <file.plt> [-o dir] [--paper A4|A3|A0] [--orient auto|portrait|landscape] [--margin 5]
//      [--overlap 10] [--overview] [--no-calib] [--no-pageno] [--keep-empty]
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
#include <filesystem>
#include <iostream>

#include "../core/layout.h"
#include "../core/naming.h"
#include "../core/pdf_min.h"
#include "../core/plt.h"

// Command-line arguments as UTF-8 on every platform (on Windows argv is in the ANSI code page and loses Chinese characters).
static std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(w);
#else
    for (int i = 0; i < argc; ++i) out.push_back(argv[i]);
#endif
    return out;
}

int main(int argc, char** argv) {
    const std::vector<std::string> args = utf8Args(argc, argv);
    argc = int(args.size());
    if (argc < 2) { std::cerr << "usage: plt2pdf file.plt [-o dir] [--paper A4] [--orient auto] [--margin 5] [--overlap 10] [--overview]\n"; return 2; }
    namespace fs = std::filesystem;
    fs::path in = fs::u8path(args[1]), outDir = in.parent_path();
    std::string orient = "auto";
    layout::Params p;
    p.useOriginalColors = true;
    bool ignoreFrame = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = args[i];
        auto val = [&]() -> std::string { if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(2); } return args[++i]; };
        if (a == "-o") outDir = fs::u8path(val());
        else if (a == "--paper") p.paper = layout::paperByName(val());
        else if (a == "--orient") orient = val();
        else if (a == "--margin") p.margin = std::atof(val().c_str());
        else if (a == "--overlap") p.overlap = std::atof(val().c_str());
        else if (a == "--overview") p.overview = true;
        else if (a == "--no-calib") p.calibBox = false;
        else if (a == "--no-pageno") p.pageNumbers = false;
        else if (a == "--keep-empty") p.skipEmpty = false;
        else if (a == "--ignore-frame") ignoreFrame = true;
    }
    try {
        plt::Document doc = plt::parseFile(in);
        std::cout << "file: " << in.filename().u8string() << "\n  size: " << doc.bounds.w() << " x " << doc.bounds.h()
                  << " mm, polylines " << doc.lines.size() << ", segments " << doc.segmentCount << "\n";
        if (doc.unsupported.count("LB")) std::cout << "  notice: " << doc.unsupported["LB"] << " text label(s) (LB) not rendered\n";
        for (auto& u : doc.unsupported) if (u.first != "LB") std::cout << "  notice: command " << u.first << " x" << u.second << " not supported (skipped)\n";
        if (doc.unsupported.empty()) std::cout << "  no unsupported commands (no LB text, no arcs)\n";
        if (ignoreFrame) std::cout << (plt::stripFrame(doc) > 0 ? "  paper frame removed -> " : "  no paper frame found -> ") << doc.bounds.w() << " x " << doc.bounds.h() << " mm\n";
        for (auto& w : doc.warnings) std::cout << "  warning: " << w << "\n";
        layout::OrientChoice oc = layout::chooseOrientation(doc, p);
        std::cout << "  pages portrait=" << oc.portraitPages << " landscape=" << oc.landscapePages << "\n";
        p.orient = orient == "portrait" ? layout::Orient::Portrait : orient == "landscape" ? layout::Orient::Landscape : oc.best;
        layout::Plan plan = layout::buildPlan(doc, p);
        fs::create_directories(outDir);
        fs::path out = naming::uniquePdfPath(outDir, in.stem().u8string(), p.paper.name, p.orient);
        pdfmin::write(plan, out);
        std::cout << "  grid " << plan.nx << "x" << plan.ny << ", effective pages " << plan.effectivePages << ", blank " << plan.blankPages
                  << (p.overview ? ", +overview page" : "") << (plan.calibCollision ? ", CALIB BOX COLLIDES" : "") << "\n  -> " << out.u8string() << "\n";
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
    return 0;
}
