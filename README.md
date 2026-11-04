# PLT 转多规格 PDF 排版工具（开发工程）

## 里程碑
| 里程碑 | 内容 | 状态 |
|---|---|---|
| M1 | 核心引擎（Qt 无关 C++17）：PLT 解析、分页/重叠/对位标记、最省纸方向、校准框、页码/水印、拼接效果图、输出命名；命令行工具；单元测试 | 已完成，已用 5 个真实 PLT 验证 |
| M2 | Qt6 界面（单文件三步）+ `qt/PdfExporterQt`（QPdfWriter，中文字体嵌入） | 已完成（Qt 6.4.2 编译通过；offscreen 截图 + 自动化界面测试通过；未在 Windows 实机验证） |
| M3 | 批量转换（队列≤20、逐一预览、限并发导出、结果报告） | 已完成（Linux 无头测试通过；未在 Windows 实机验证；未做项见 HANDOFF） |

## 构建与测试（核心，无需 Qt）
    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/plt2pdf tests/data/xxx.plt -o out --paper A4 --orient auto --overview

## 运行界面（Qt 6）
    cmake -S . -B build -G Ninja && cmake --build build
    build/plt_gui                      # 或 build/plt_gui xxx.plt
    QT_QPA_PLATFORM=offscreen build/plt_gui --demo xxx.plt outdir   # 无头截图三步界面并导出 PDF
    ctest --test-dir build --output-on-failure                       # core + batch + gui 三组测试（后两组需 pdfinfo）
    QT_QPA_PLATFORM=offscreen build/plt_gui --demo-batch outdir a.plt b.plt ...   # 无头截图批量五步并导出

## 目录
- `qt/` 界面：`MainWindow`（三步流程、撤销/重做、后台排版/导出）、`CanvasWidget`（缩放/平移/拖拽/框选/分页网格）、`PageRender`（缩略图）、`State.h`（全部可编辑状态）、`PdfExporterQt`
- `core/docops.*` 旋转/镜像/框选裁剪；`core/pagerange.h` 页码范围解析
- `core/plt.*` 解析（IN/SP/PU/PD/PA/PR/CI 等；LB 文本与弧线 AA/AR 等记为 unsupported 并提示）；`findFrame/stripFrame` 纸张边框识别与移除（命令行 `--ignore-frame`）
- `core/layout.*` 分页引擎：页数公式、Liang–Barsky 裁剪、空白页、校准框避让、页码/水印、拼接效果图
- `core/naming.h` 输出命名 `{原名}_{纸张}{方向}.pdf`，重名自动 `_2`、`_3`，不覆盖
- `core/pdf_min.*` 仅供命令行/测试的最小矢量 PDF 写入（仅英文字体；正式导出用 Qt）
- `tests/` 单元测试 + `tests/data/` 5 个真实样例
