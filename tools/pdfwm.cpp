// pdfwm: command line front end of the watermark module.  pdfwm analyze in.pdf [--json] | pdfwm remove in.pdf -o out.pdf [--also 2,3]
#include "wm/wm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

static const char* kindName[] = {"独立叠加对象", "斜铺文字", "页眉页脚/页码", "整页位图"};
static const char* tierName[] = {"默认去除", "需确认", "默认保留", "无法处理"};

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法: pdfwm analyze 输入.pdf [--json]\n      pdfwm remove 输入.pdf -o 输出.pdf [--also 序号,序号]\n"); return 2; }
    std::string cmd = argv[1];
    auto in = std::filesystem::u8path(argv[2]);
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
            if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out = std::filesystem::u8path(argv[++i]);
            else if (!std::strcmp(argv[i], "--also") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string t; while (std::getline(ss, t, ',')) also.push_back(std::atoi(t.c_str())); }
        }
        if (out.empty()) { std::fprintf(stderr, "缺少 -o 输出文件\n"); return 2; }
        if (out == in) { std::fprintf(stderr, "输出不能覆盖原文件\n"); return 2; }
        auto r = wm::remove(in, out, also);
        if (!r.ok) { std::fprintf(stderr, "失败: %s\n", r.error.c_str()); return 1; }
        std::printf("已写出 %s：去除叠加对象 %d 个，文字块 %d 个，填充 %d 个，还原线条色 %d 处\n", out.u8string().c_str(), r.formsRemoved, r.textBlocksRemoved, r.fillsRemoved, r.coloursRestored);
        return 0;
    }
    std::fprintf(stderr, "未知命令 %s\n", cmd.c_str());
    return 2;
}
