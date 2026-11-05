// pdfwm: command line front end of the watermark module.  pdfwm analyze in.pdf [--json] | pdfwm remove in.pdf -o out.pdf [--also 2,3]
#include "wm/wm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

static const char* kindName[] = {"独立叠加对象", "斜铺文字", "页眉页脚/页码", "整页位图", "同层店铺信息栏"};
static const char* tierName[] = {"默认去除", "需确认", "默认保留", "无法处理"};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: pdfwm analyze 输入.pdf [--json]\n      pdfwm remove 输入.pdf -o 输出.pdf [--also 序号,序号]\n"
                             "      pdfwm texts 输入.pdf                      列出 PDF 里的真文字\n"
                             "      pdfwm remove-text 输入.pdf -o 输出.pdf --text 内容[,内容]   去除含有这些内容的真文字\n"
                             "      pdfwm remove-shape 输入.pdf -o 输出.pdf --page 页 --rect x0,y0,x1,y1 [--this-page-only]\n"
                             "                                                框选线条画出的字（单位 pt，原点在页面左上角），并去除所有页上相同的副本\n");
        return 2;
    }
    std::string cmd = argv[1];
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
