// pdfwm: command line front end of the watermark module.  pdfwm analyze in.pdf [--json] | pdfwm remove in.pdf -o out.pdf [--also 2,3]
#include "wm/wm.h"
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

static const char* kindName[] = {"独立叠加对象", "斜铺文字", "页眉页脚/页码", "整页位图", "同层店铺信息栏"};
static const char* tierName[] = {"默认去除", "需确认", "默认保留", "无法处理"};

static int batchMain(const std::string& cmd, int argc, char** argv);

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: pdfwm analyze 输入.pdf [--json]\n      pdfwm remove 输入.pdf -o 输出.pdf [--also 序号,序号]\n"
                             "      pdfwm texts 输入.pdf                      列出 PDF 里的真文字\n"
                             "      pdfwm remove-text 输入.pdf -o 输出.pdf --text 内容[,内容]   去除含有这些内容的真文字\n"
                             "      pdfwm remove-shape 输入.pdf -o 输出.pdf --page 页 --rect x0,y0,x1,y1 [--this-page-only]\n"
                             "                                                框选线条画出的字（单位 pt，原点在页面左上角），并去除所有页上相同的副本\n"
                             "      pdfwm group 文件1.pdf 文件2.pdf ...         按水印特征自动分组（最多 20 个）\n"
                             "      pdfwm make-recipe 样本.pdf -o 配方.wmrecipe [--name 名称] [--text a,b] [--shape 页:x0,y0,x1,y1[:名称]]\n"
                             "      pdfwm batch --recipe 配方.wmrecipe --out 输出目录 文件1.pdf 文件2.pdf ...   批量套用配方，输出 *_haoyang.pdf 与 报告.csv\n");
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "group" || cmd == "batch") return batchMain(cmd, argc, argv);
    auto in = wm::fromU8(argv[2]);
    if (cmd == "analyze") {
        auto r = wm::analyze(in);
        if (argc > 3 && !std::strcmp(argv[3], "--json")) { std::cout << wm::toJson(r) << "\n"; return r.ok ? 0 : 1; }
        if (!r.ok) { std::fprintf(stderr, "读取失败: %s\n", r.error.c_str()); return 1; }
        std::printf("%d 页，发现 %zu 项\n", r.pageCount, r.findings.size());
        for (size_t i = 0; i < r.findings.size(); ++i) {
            auto& f = r.findings[i];
            std::printf("[%zu] %s · %s · 得分 %d · %d 页 · %d 个对象%s\n    %s\n", i, kindName[(int)f.kind], tierName[(int)f.tier], f.score, f.pages, f.objects,
                        f.contactText ? " · 含联系方式文字" : "", f.label.c_str());
            for (auto& e : f.evidence) std::printf("      - %s\n", e.c_str());
        }
        return 0;
    }
    if (cmd == "remove") {
        std::filesystem::path out; std::vector<int> also;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out = wm::fromU8(argv[++i]);
            else if (!std::strcmp(argv[i], "--also") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string t; while (std::getline(ss, t, ',')) also.push_back(std::atoi(t.c_str())); }
        }
        if (out.empty()) { std::fprintf(stderr, "缺少 -o 输出文件\n"); return 2; }
        if (out == in) { std::fprintf(stderr, "输出不能覆盖原文件\n"); return 2; }
        auto r = wm::remove(in, out, also);
        if (!r.ok) { std::fprintf(stderr, "失败: %s\n", r.error.c_str()); return 1; }
        std::printf("已写出 %s：去除叠加对象 %d 个，文字块 %d 个，填充 %d 个，还原线条色 %d 处，店铺信息栏元素 %d 个\n", wm::toU8(out).c_str(), r.formsRemoved, r.textBlocksRemoved, r.fillsRemoved, r.coloursRestored, r.blockItemsRemoved);
        return 0;
    }
    if (cmd == "make-recipe") {
        std::filesystem::path out; wm::Recipe rc; rc.name = "未命名配方";
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out = wm::fromU8(argv[++i]);
            else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) rc.name = argv[++i];
            else if (!std::strcmp(argv[i], "--text") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string t; while (std::getline(ss, t, ',')) rc.texts.push_back(t); }
            else if (!std::strcmp(argv[i], "--shape") && i + 1 < argc) {
                int pg = 0; wm::Rect rr; char nm[256] = "";
                std::string spec = argv[++i];
                if (std::sscanf(spec.c_str(), "%d:%lf,%lf,%lf,%lf:%255[^\n]", &pg, &rr.x0, &rr.y0, &rr.x1, &rr.y1, nm) < 5) { std::fprintf(stderr, "--shape 格式：页:x0,y0,x1,y1[:名称]\n"); return 2; }
                wm::ShapeTemplate t; t.name = nm[0] ? nm : "图形";
                auto r = wm::extractShape(in, pg, rr, t);
                if (!r.ok) { std::fprintf(stderr, "框选失败: %s\n", r.error.c_str()); return 1; }
                rc.shapes.push_back(t);
            }
        }
        if (out.empty()) { std::fprintf(stderr, "缺少 -o 配方文件\n"); return 2; }
        auto rep = wm::analyze(in);
        if (!rep.ok) { std::fprintf(stderr, "读取失败: %s\n", rep.error.c_str()); return 1; }
        rc.signature = wm::signatureOf(rep);
        rc.removeDetected = !rc.signature.empty();
        std::string err;
        if (!wm::saveRecipe(out, rc, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::printf("已保存配方 %s：自动检测 [%s]，指定文字 %zu 个，图形模板 %zu 个\n", wm::toU8(out).c_str(), rc.signature.c_str(), rc.texts.size(), rc.shapes.size());
        return 0;
    }
    if (cmd == "texts") {
        std::string err;
        auto items = wm::listText(in, &err);
        if (!err.empty()) { std::fprintf(stderr, "读取失败: %s\n", err.c_str()); return 1; }
        std::printf("共 %zu 种文字\n", items.size());
        for (auto& t : items) std::printf("%4d 页 %5d 处  %s\n", t.pages, t.count, t.text.c_str());
        return 0;
    }
    if (cmd == "remove-text" || cmd == "remove-shape") {
        std::filesystem::path out; std::vector<std::string> needles; int page = 0; wm::Rect rc; bool everywhere = true, haveRect = false;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out = wm::fromU8(argv[++i]);
            else if (!std::strcmp(argv[i], "--text") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string t; while (std::getline(ss, t, ',')) needles.push_back(t); }
            else if (!std::strcmp(argv[i], "--page") && i + 1 < argc) page = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--rect") && i + 1 < argc) { haveRect = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf", &rc.x0, &rc.y0, &rc.x1, &rc.y1) == 4; }
            else if (!std::strcmp(argv[i], "--this-page-only")) everywhere = false;
        }
        if (out.empty()) { std::fprintf(stderr, "缺少 -o 输出文件\n"); return 2; }
        if (out == in) { std::fprintf(stderr, "输出不能覆盖原文件\n"); return 2; }
        if (cmd == "remove-text") {
            auto r = wm::removeText(in, out, needles);
            if (!r.ok) { std::fprintf(stderr, "失败: %s\n", r.error.c_str()); return 1; }
            std::printf("已写出 %s：去除 %d 处文字，涉及 %zu 页\n", wm::toU8(out).c_str(), r.removed, r.pagesTouched.size());
            return 0;
        }
        if (page < 1 || !haveRect) { std::fprintf(stderr, "需要 --page 和 --rect x0,y0,x1,y1\n"); return 2; }
        auto r = wm::removeShape(in, out, page, rc, everywhere);
        if (!r.ok) { std::fprintf(stderr, "失败: %s\n", r.error.c_str()); return 1; }
        std::printf("已写出 %s：框选 %d 条线，共去除 %d 条线（另找到 %d 处相同副本），涉及页：", wm::toU8(out).c_str(), r.selected, r.removed, r.copies);
        for (int p : r.pagesTouched) std::printf("%d ", p);
        std::printf("\n");
        return 0;
    }
    std::fprintf(stderr, "未知命令 %s\n", cmd.c_str());
    return 2;
}

static std::string csvQ(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}

static int batchMain(const std::string& cmd, int argc, char** argv) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files; fs::path outDir, recipePath;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = wm::fromU8(argv[++i]);
        else if (!std::strcmp(argv[i], "--recipe") && i + 1 < argc) recipePath = wm::fromU8(argv[++i]);
        else files.push_back(wm::fromU8(argv[i]));
    }
    if (files.empty()) { std::fprintf(stderr, "没有输入文件\n"); return 2; }
    if ((int)files.size() > wm::kMaxBatchFiles) { std::fprintf(stderr, "一次最多处理 %d 个文件（收到 %zu 个）\n", wm::kMaxBatchFiles, files.size()); return 2; }
    std::vector<wm::Report> reps;
    for (auto& f : files) reps.push_back(wm::analyze(f));
    std::vector<std::string> sigs;
    auto grp = wm::groupReports(reps, &sigs);
    if (cmd == "group") {
        for (size_t g = 0; g < sigs.size(); ++g) {
            int n = 0; for (int x : grp) if (x == (int)g) ++n;
            std::printf("分组 %c：%s（%d 个文件）\n", 'A' + (int)g, sigs[g].c_str(), n);
            for (size_t i = 0; i < files.size(); ++i) if (grp[i] == (int)g) std::printf("    %s\n", wm::toU8(files[i].filename()).c_str());
        }
        for (size_t i = 0; i < files.size(); ++i) {
            if (grp[i] == -1) std::printf("未发现水印：%s\n", wm::toU8(files[i].filename()).c_str());
            else if (grp[i] == -2) std::printf("图片水印，无法安全去除：%s\n", wm::toU8(files[i].filename()).c_str());
            else if (grp[i] == -3) std::printf("读取失败：%s\n", wm::toU8(files[i].filename()).c_str());
        }
        return 0;
    }
    if (recipePath.empty() || outDir.empty()) { std::fprintf(stderr, "batch 需要 --recipe 和 --out\n"); return 2; }
    wm::Recipe rc; std::string err;
    if (!wm::loadRecipe(recipePath, rc, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::error_code ec; fs::create_directories(outDir, ec);
    std::ofstream csv(outDir / "去水印报告.csv", std::ios::binary);
    csv << "\xEF\xBB\xBF文件,状态,吻合度,去除项,线条数(前),线条数(后),说明\n";
    int ok = 0, bad = 0, skip = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::string name = wm::toU8(files[i].filename());
        int conf = wm::conformity(reps[i], rc);
        if (grp[i] < 0 && rc.texts.empty() && rc.shapes.empty()) {
            ++skip; csv << csvQ(name) << ",跳过," << conf << ",0,,," << (grp[i] == -2 ? "图片水印" : grp[i] == -3 ? "读取失败" : "未发现水印") << "\n";
            std::printf("跳过  %s\n", name.c_str()); continue;
        }
        fs::path out = outDir / (files[i].stem().string() + "_haoyang.pdf");
        for (int k = 2; fs::exists(out); ++k) out = outDir / (files[i].stem().string() + "_haoyang(" + std::to_string(k) + ").pdf");
        auto r = wm::applyRecipe(files[i], out, rc);
        if (r.ok) {
            ++ok;
            std::printf("完成  %s  去除 %d 项  线条 %ld → %ld ✓\n", name.c_str(), r.removedTotal(), r.strokesBefore, r.strokesAfter);
            csv << csvQ(name) << (conf < 85 ? ",完成·需确认," : ",完成,") << conf << "," << r.removedTotal() << "," << r.strokesBefore << "," << r.strokesAfter << ",\n";
        } else if (r.nothingToDo) {
            ++skip; std::printf("跳过  %s  没有找到配方要去除的内容\n", name.c_str());
            csv << csvQ(name) << ",跳过," << conf << ",0,,,没有找到配方要去除的内容\n";
        } else {
            ++bad;
            std::printf("失败  %s  %s\n", name.c_str(), r.error.c_str());
            csv << csvQ(name) << ",失败," << conf << ",0," << r.strokesBefore << "," << r.strokesAfter << "," << csvQ(r.error) << "\n";
        }
    }
    std::printf("共 %zu 个：完成 %d，失败 %d，跳过 %d。报告：%s\n", files.size(), ok, bad, skip, wm::toU8(outDir / "去水印报告.csv").c_str());
    return bad ? 1 : 0;
}
