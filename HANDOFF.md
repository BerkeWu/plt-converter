# 项目交接说明（给新对话里的 Claude）

你好。请继续开发「PLT转多规格PDF排版工具」。本压缩包是上一个对话的全部成果，**请先读完本文件，再读 README.md 和 PRD（产品需求文档_PRD_…_v2.4.docx）**。

## 本次对话的目标
沙箱已开启网络。请：
1. 用 apt 安装 CMake、g++ 与 Qt 6 开发包（Ubuntu 24.04 对应 Qt 6.4.2；只使用 6.4 与更新版本都有的接口）。
2. 先把 `qt/PdfExporterQt.*` 真正编译跑通（该文件写好但从未编译过），验证中文水印、拼接效果图文字能嵌入 PDF（字体用系统可用的 CJK 字体，目标平台字体为微软雅黑/苹方，需做字体回退）。
3. 实现 M2：Qt 6 单文件三步界面（导入 → 排版设置 → 预览导出），用 `QT_QPA_PLATFORM=offscreen` 运行并截图，对照设计稿检查。
4. 之后做 M3：批量转换（队列≤20、逐一预览、限并发导出、结果报告）。

## 已完成（M1，核心引擎，无 Qt 依赖，已用 tests/data 的 5 个真实 PLT 验证）
- 解析 HP-GL：IN/SP/PU/PD/PA/PR/CI；LB 与 AA/AR 等只计数提示；单位 1unit=0.025mm（40 单位/mm）；`findFrame/stripFrame` 识别并移除纸张边框。
- 分页：页数 = 1 + ceil((图形宽 − 可打印宽)/(可打印宽 − 重叠))；Liang–Barsky 裁剪；空白页跳过；页码“行-列”；对位标记；校准框 5×5cm 仅第 1 页并自动避让；最省纸方向（按页数，并列比拼接缝、留白，再默认竖向）。
- 页码：横向左上角，竖向底部中间（竖向时页码在上、底部水印在下叠放）。水印固定：顶部“好样纸样plt转换器”，底部“打印时，打印比例请选择「按实际」或「100%」”。
- 拼接效果图（参数默认关）：开启后 PDF 第 1 页为缩小示意图，无页码，有“拼接效果图”文字说明。
- 输出命名：`{原名}_{纸张}{方向}.pdf`，重名自动 `_2`、`_3`，绝不覆盖。
- 命令行 `plt2pdf` 与 `tests/test_core.cpp`（全部通过）。注意：`core/pdf_min` 仅英文字体，正式导出走 Qt。
- 注意：/mnt/user-data/outputs 目录里的可执行文件无执行权限，请把编译产物放到 /home/claude 下运行。

## 已确认的产品决策（详见 PRD 第10章）
批量每文件一个 PDF；最省纸只按页数；纸张由用户指定，不自动选纸；不做套料；不支持非 1:1 缩放；队列上限 20；单文件步骤条三步（导入→排版设置→预览导出）；批量五步；重叠区默认 10mm（范围 0–30）；页边距默认 5mm；忽略纸张边框开关默认关；LB 文字本期仅提示；技术栈 Qt 6；目标平台 Windows 10/11（用户曾问及 Mac 版，尚未决定，暂按 Windows，代码保持跨平台）。

## 设计稿
https://claude.ai/artifact/Dqr3rPDZWbThHJgnxhyQvK （两个界面：单文件排版设置、批量逐一预览）。主色青绿 #0B7A70，左参数栏 300px、中间画布、底部缩略图。若无法读取该链接，请按 PRD 第4、5章自行设计。

## 样例文件发现（tests/data）
中式背心两个文件含“门幅纸张边框”；5 个文件均无 LB；有的文件文字本身是倒置的（文件原样，需要后续提供旋转/镜像按钮 PRD 1.B）。

## 沟通约定
用户使用中文，希望遇到疑问及时提问；不确定的产品问题先问再做。

---
## M2 完成情况（第二次对话）
已完成并验证（Ubuntu 24.04 / Qt 6.4.2，offscreen）：`qt/PdfExporterQt` 编译通过，中文水印/效果图文字嵌入 PDF（回退字体 WenQuanYi，Windows 上走微软雅黑）；Qt6 单文件三步界面；`ctest` 两项（core + gui）全过；截图见 `docs/screenshots/`。

**核心引擎的小改动（均有单测）**：`Params` 新增 `lineR/G/B`、`singlePage`；校准框文字随边长变化；新增 `layout::autoOffset`、`layout::shiftHint`、`naming::uniquePdfNameTagged`、`core/docops.*`（旋转/镜像/框选裁剪）、`core/pagerange.h`。

**已按设计稿/PRD 实现**：拖拽/右键平移/滚轮缩放/方向键微调；框选（裁剪或整条保留）；旋转 90/180/270、水平镜像；忽略纸张边框；单页输出；自动最省纸方向；校准框/页码/对位标记/效果图/输出空白页开关；线色与线宽；自动优化/居中/左上对齐；平移减页提示、改用更大纸张提示；撤销/重做（Ctrl+Z / Ctrl+Y）；150ms 防抖后台排版；缩略图条与全页勾选预览；页码范围；≥20 页二次确认；可取消导出；完成后打开文件/文件夹；设置记忆（QSettings）。

**已知取舍（请与产品确认）**
1. 【已确认】「原色」：解析器未读取笔色，原色按黑色输出。
2. 「输出空白页」：全局开关，开启后空白页进入预览并默认勾选（可在第 3 步逐页取消勾选）；关闭时空白页不进入预览。页码仍按网格位置编号。
3. 【已确认】单页输出的文件名为 `{原名}_单页.pdf`。
4. 解析阶段只有不确定进度条（无百分比）。
6. 中文路径：核心接口 `plt::parseFile` / `pdfmin::write` / `naming::uniquePdfPath` 已改用 `std::filesystem::path`（UTF-8 文本请用 `fs::u8path()` 构造）；plt2pdf 在 Windows 用 `CommandLineToArgvW` 取 UTF-8 参数；MSVC 下加 `/utf-8` 编译选项并嵌入 `packaging/utf8.manifest`（activeCodePage=UTF-8）。Linux 下已实测通过，Windows 上尚未验证。
5. 未做：Windows 实机验证、高 DPI 实机、批量模式（M3，顶栏「批量转换」按钮暂置灰）。

## Windows 打包（未在本沙箱验证，沙箱无 Windows 工具链）
见 `packaging/README.md`：`packaging/build_windows.ps1`（本机一键）或 `.github/workflows/windows-installer.yml`（GitHub Actions），产出 Inno Setup 安装包 + 便携 zip。安装向导目前为英文界面（官方 Inno 不带简体中文语言包）。待用户实机验证后再决定是否需要图标、代码签名、.plt 文件关联。

---
## 第三轮修改（Windows 实机验证反馈）
1. 「忽略纸张边框」：DZ 类文件里边框矩形被 CAD 画了两遍，旧 `stripFrame` 只删一条，另一条留在图上。现 `plt::stripFrame` 返回删除条数并移除所有重合的边框线；界面在复选框下方显示「已移除 N 条边框线」或「没有识别到可移除的纸张边框」（复选框不再因未识别而置灰）。
2. 「拼接对位标记」：每页可打印区四角的「+」之间用虚线连接；默认蓝色 #2563EB，界面提供 5 个预设色 + 自定义；线宽 0.2 mm，虚线 2 mm 画 / 1.5 mm 空。
3. 「拼接效果图」默认开启。旧版本把「关闭」写进了用户设置，所以加了 `settingsVersion=2` 迁移：升级后该项重置为开启。
4. 页码：30 pt（原 10 pt 的 3 倍）、加粗、带边框（距文字 2 mm）、整体 80% 不透明。横向纸张在左上角（离角标 3.5 mm），竖向在底部中间；底部水印与页码上移 1.5 mm、顶部水印下移，避免压在虚线上。校准框摆放会避开页码框。
附：导出器测文字宽度改为大字号测量再换算（小字号下 QFontMetricsF 不可靠，页码框因此一度画歪）。

---
## 第四轮：弧线 + 原色
- 解析器新增：`AA/AR`（圆心+转角，负角度为顺时针）、`AT/RT`（三点弧，三点共线时退化为直线）、`EA/ER`（矩形轮廓，笔位不变）。圆弧按 ≤2° 细分，1:1 打印不显折角。仍不支持：`LB` 文字、`RA/RR/WG/EW/FP` 等填充类（会计入「已跳过」提示）。
- 原色：解析 `PC n,r,g,b`（HP-GL/2）和 `CR` 颜色范围（默认 0–255）；`Document::penColors`、`Params::useOriginalColors`。未定义颜色的笔印黑色；近白色（亮度>0.9）印浅灰，避免在白纸上消失。拼接效果图页同样按原色。
- 注意：5 个样例里的 `PC;` 不带参数（=恢复默认），样例没有任何颜色信息，所以样例的「原色」输出仍是黑色。圆弧/彩色笔只用合成文件和单元测试验证，还没有真实 CAD 文件验证。
- 未做（有需要再加）：HP-GL/1 无 `PC` 的老文件按笔号套用默认色板；`LT` 线型（虚线/点线，目前忽略）；`PW` 笔宽（目前统一用界面线宽）。

---
## 第五轮：M3 批量转换（已完成）
入口：顶栏「单文件 / 批量转换」切换；单文件界面有「转为批量转换」按钮（带入当前文件与参数）。五步：1 添加文件 → 2 通用参数 → 3 自动排版 → 4 逐一预览 → 5 批量导出（界面结构依 `Batch.dc.html`：左队列 + 中画布 + 右参数/优化提示，底部汇总栏）。

代码结构：`qt/BatchCore.*`（无界面逻辑：扫描、自动排版、单文件导出、输出名预分配、CSV、会话 JSON）、`qt/MainWindowBatch.cpp`（批量界面与流程）、`qt/BatchWidgets.h`（可拖拽排序的队列表格）。逐一预览复用单文件编辑器：切换文件时只替换 `EditCtx`（State/撤销栈/几何），所以拖拽、框选、旋转、撤销、优化提示全部可用，且每个文件独立撤销。

**已实现（对照 PRD 4.1–4.9）**：队列上限 20（超出提示、不崩溃）；非 .plt 忽略并提示数量；文件夹（可含子文件夹）；同路径去重、同名同大小标「疑似重复」；后台解析（并发 ≤min(核数-1,4)）、结果逐条出现；缩略图；拖拽/按钮排序、删除、全选/反选；通用参数模板（命名保存/载入/删除）+ 校准框范围 + 页码旁文件名（≤20 字）+ 初始位置策略 + 输出目录；自动选最省纸方向（与穷举一致，含 30 用例单测）；逐文件覆盖标记 ✎、还原为自动方案、恢复通用设置、应用到后续所有文件（不复制位置）、全部按自动方案确认、筛选（有优化提示/页数最多）、「虚线=优化位置」；汇总栏（文件数/已确认/预计页数/纸张面积/耗时）；惰性加载（只保留当前+下一个文件的几何）；导出前汇总确认、解析失败文件「跳过后继续」、大文件（线段>50 万或页>100）单独导出并二次确认、磁盘空间预估；输出名导出前统一预分配（重名 _2/_3，不覆盖）；2 路（可设 1–4）有界并发；暂停/继续/取消（取消清理 .part）；失败隔离、仅重试失败项；结果统计、打开输出文件夹、CSV 报告（UTF-8 BOM）；会话每 30 秒自动保存，下次启动提示恢复。

**未实现 / 与 PRD 的差距（请确认是否需要）**
1. 内存预算监控（峰值 ≤1.5GB、>80% 自动降并发）：未做实际内存监测；靠「只载入当前+下一个文件」「并发≤4」「大文件独占」间接控制。
2. 运行中磁盘写满：目前该文件失败并记录原因，不会「暂停全部任务让用户换目录」；可用「仅重试失败项」换目录重导。
3. 缩略图磁盘缓存 / LRU：未做（队列最多 20 张，全部常驻内存）。
4. 「扫描只读包围盒不载入全部几何」：解析器仍需完整读文件，扫描完立即释放几何，只留元数据与缩略图。20 文件 ≤15 秒、导出峰值内存、帧率 ≥30fps 等性能验收项没有用真实大文件实测。
5. 「允许旋转图形 90° 参与自动比较」：未做（PRD 默认关闭）。
6. 批量中 ←/→ 切换文件：画布聚焦时方向键用于微调位置，所以切换文件用 Ctrl+←/→、PgUp/PgDn、上一个/下一个按钮，或在左侧队列里按 ←/→/↑/↓。
7. 批量里不支持逐文件的页码范围选择（单文件模式有）。

测试：`ctest` 三项（core / batch / gui）；`test_batch` 覆盖 20 个文件上限、3 个损坏文件隔离、重名编号、暂停/取消/重试、会话往返、与穷举一致的方向选择。

---
## 第六轮：M3 安装包实机打印反馈（单文件转换后打印拼接）
用户反馈三点：矢量线条打印后模糊；改线条颜色时预览画布不变色；拼接时各页矢量线错位。

**自查结论（Linux 无头环境，用合成图形和 600 dpi 渲染）**
- 分页几何是精确的：相邻两页重叠区在 600 dpi 下逐像素吻合（偏移 0.0 mm）。所以错位不是坐标算错。
- PDF 内容流是纯矢量描边（线宽 0.25 mm，无任何位图）；未能复现“模糊”。
- 但 PDF 里确实有透明度：上一轮给页码加的 80% 不透明度（ExtGState ca 0.8）。很多打印通道（Acrobat/GDI/浏览器）遇到透明度会把整页栅格化，这是最可能的模糊原因，**但没有在用户的打印环境里确认**。
- 目录里没有任何打印缩放设置：Acrobat/浏览器默认“适应页面”会缩小每页，重叠区就对不上。
- 对位标记有设计缺陷：只标了每页可打印区的四角，相邻页的“+”之间没有对应关系；按“十字对十字”拼，会正好错开一个重叠量（默认 10 mm）。

**已修改**
1. 导出 PDF 不再含任何透明度：页码的 80% 改为与白底预混合的实心灰（外观等价）。回归测试检查导出文件里没有 `/ca 0` `/CA 0`。
2. 导出后给 PDF 目录追加 `/ViewerPreferences << /PrintScaling /None >>`（增量更新，经 pypdf 与 poppler 验证结构合法），Acrobat、Foxit、基于 PDFium 的浏览器默认按 100% 打印。
3. 对位标记新增“内侧十字”：有相邻页的一侧，在邻页边缘落点处也画“+”，相邻页拼合后十字与十字重合（实测第 2 页内侧十字 x=282.00 mm，第 3 页外侧十字 x=5.00 mm，差 277 mm = 可打印宽 − 重叠量）。
4. 预览画布按所选线条颜色（含“原色”的笔号颜色）和线宽绘制，不再固定青绿色；步骤 1 的导入预览仍用中性青绿。
5. 进度条配色微调。

**仍需用户确认**：打印用的软件和“页面缩放”选项、打印机型号、第 1 页 5×5 cm 校准框实测边长、模糊的照片或扫描件。若校准框不是 50 mm，说明打印时被缩放。

---
## 第七轮：拼接框模型（参照用户提供的 80.pdf 样例）
用户反馈：1) 定位点太多，每个角只留一个；2) 重叠区的纸样线条和定位框虚线都要印出来，像样例 PDF；3) 批量一并调整。

**新的拼接模型（core/layout）**
- 拼接框（定位框）距纸边 `inset = max(重叠区, 页边距)`；相邻页的框紧挨着（步长 = 框的大小）。
- 框外仍印出纸样线条，一直印到页边距（框外延伸 `strip = inset − 页边距`）。相邻两页把虚线框叠合，框外那圈线条与邻页框内的线条逐点重合（600 dpi 实测偏移 0.0 mm）。
- 定位标记：四条贯穿整页的虚线（像“#”）+ 每个框角一个“+”，共 4 个“+”，没有别的定位点。上一轮加的“内侧十字”已删除。
- “重叠区”的含义因此变为“框外延伸宽度”（滑块 0–30 mm，默认 10）。默认参数（页边距 5、重叠区 10）下页数与旧模型相同（PRD 示例 560×380 mm 仍是竖向 6 页、横向 4 页）。
- 空白页判定改为“框内没有线”；没有邻页的外侧，延伸区算本页的内容。
- 页码、校准框改为放在框内；顶部/底部水印位于框与页边之间。拼接效果图页新增一行“拼接框 W x H mm：打印后量一下蓝色虚线框”，与样例的做法一致。
- `layout::tileGeometry()` 是页数/网格的唯一来源，画布、批量页数估算、优化提示、自动位置都改用它。

**批量转换**：导出、缩略图、页数估算、优化提示、逐一预览全部走同一个引擎，随之调整；批量测试（20 文件上限、损坏文件隔离、方向与穷举一致、暂停取消重试、优化提示交互）全部通过。

**注意**：样例 PDF 80.pdf 带有第三方店铺的斜铺水印，只用作拼接版式参考，没有对它做任何处理。

---
## 第八轮：去水印核心模块（feature/watermark 分支，未合入主线）
用户提供 5 组真实带水印 PDF（A–E）。需求分析与实测结论见 Claude Docs 需求文档“样例实证”一节，设计稿见 Design 画布“去水印功能设计稿”。

**代码**
- `core/wm/wm.h/.cpp`：`wm::analyze()` 逐对象打分分档（High ≥75 / Confirm 45–74 / Keep），`wm::remove()` 重写内容流；`toJson()` 供界面使用。
  - 独立叠加对象：按（表单内容哈希、矩阵、不透明度）分组；斜置、半透明、跨页相同、Artifact 标记、KSPX 记号、只含文字各自加分；去除时整块拿掉 `q cm BDC…EMC Q`，并从页面资源里删除该表单和图形状态，使水印文字不再留在文件里。
  - 压平文字：找到数量最多的斜置同字号文字串，其中最浅的颜色为水印色 G；其余颜色是被混色的线条。混色模型 blended = (1-α)·原色 + (G-(1-α))，α 在 5%–60% 里拟合；候选原色里取出现次数最多且误差 <0.06 的，否则解析反推。去除 G 色文字块、G 色填充（f→n，B→S），并把混色还原成原色。
  - 页码类（正向、不透明、各页内容不同）归为 PageStamp，默认保留（样例 C 的“第 1/6 页”）。
  - 整页位图（图片面积 ≥60% 的页占一半以上）归为 Bitmap，拒绝处理。
  - 联系方式文字只记标志位 `contactText`，不保存原文。
- `tools/pdfwm.cpp`：`pdfwm analyze in.pdf [--json]`、`pdfwm remove in.pdf -o out.pdf [--also 序号]`，不覆盖原文件。
- `tests/test_wm.cpp` + `tests/data_wm/`（样例副本，仅供开发，勿外传）：A 去除后与用户的无水印版指令数完全一致且资源里无 KSP 残留；C、D 去除后再分析找不到斜铺水印、几何指令数不变、页码表单保留；E 被拒绝。
- `prototype/watermark/`：Python 原型（对照用，C++ 输出与其逐像素一致）。

**验证（渲染对比）**：A 去除后与无水印版 10 页差异 0 像素；C/D 浅灰水印像素 C 页1 43160→1654、D 页1 24158→232；`qpdf --check` 通过。

**构建**：需要 libqpdf（Ubuntu：`apt install libqpdf-dev`）。缺库时 CMake 自动跳过 pltwm / pdfwm / test_wm。Windows 打包尚未带上 QPDF（下一步：GitHub Actions 里用 vcpkg 安装 qpdf，并把 qpdf29.dll 等放进安装包）。

**已知缺口**：样例 B（同层店铺信息栏）需要“范围扩展”；D 样例第 3 页仍有约 4900 个浅灰像素，可能是纸样里本来就有的深灰圆环，待查；GBK 编码的联系方式关键词（微信、小红书）暂不能识别，目前只靠 ASCII 账号模式；输出文件名与保存位置、界面接入未做。

**打包（第八轮补充）**：分支版本号为 1.1.0-dev（`VERSION`、安装脚本、构建脚本一致），安装包名 `PLT-Converter-Setup-1.1.0-dev.exe`，与正式 1.0.0 区分。GitHub Actions 里先用 vcpkg 装 `qpdf:x64-windows`，`build_windows.ps1 -VcpkgRoot` 把 vcpkg 工具链传给 CMake，并把 `pdfwm.exe` 与 QPDF 运行库复制进安装目录。安装包里新增开始菜单项“去水印（把PDF拖到这里）”，对应 `watermark_drop.bat`：先显示检测结果，再把去水印后的文件写到原文件旁边（`原名_去水印.pdf`），不覆盖原文件。界面尚未接入，这是给实机验证用的临时入口。
**未在 Windows 上验证**：QPDF 头文件在 vcpkg 最新版（12.x）需要 C++20，所以水印库单独按 C++20 编译，其余仍是 C++17；Linux 上已用 QPDF 11.9 通过测试，12.x 的接口差异需在 Actions 里确认。


## 第九轮：批量「逐一预览」补齐图形功能 + 提示条遮挡（v1.0.1，分支 fix/batch-preview，本次合并并入）
- 用户反馈：批量逐一预览缺「忽略纸张边框」「框选区域」，且预览框底部提示文字遮挡图形。
- 排查：这两个功能在批量里数据通路本来就通（`State.ignoreFrame/hasSel/sel/selClip` → `deriveState` → 导出走 `buildFullDoc/cropWorkDoc`），问题是入口难发现：共用的导入面板在批量里带着单文件才需要的说明/文件信息，控件被挤到滚动区外。
- 改动：`MainWindow::syncUi` 批量下隐藏 `impDrag_/impInfo_`；页签改名「图形与选区」并在有图形编辑时加「●」；信息卡新增 `bu_.shapeNote`（`shapeSummary()`）。
- 提示条：`CanvasWidget` 新增 `reserve_/viewH()`，提示条占用底部独立条带，`refit/actualSize/focusCell/比例尺/可见区域` 全部改用 `viewH()`，绘制裁剪到上方区域。
- 测试：`tests/test_batch.cpp` 增加逐一预览的忽略边框 / 框选 / 摘要 / 撤销用例。ctest 3/3 通过（Qt 6 offscreen，当时在 main 分支独立验证）。
- 合并记录：这是 `main` 分支上独立发布的 v1.0.1，当时未合入 `feature/watermark`。本次（第二十一轮）合并时发现它附带提交了一份 `build/` 编译产物目录（271 个文件），已在合并时剔除并补了 `.gitignore`，不影响这里列的功能性改动；代码部分（`CanvasWidget.*`、`MainWindow.*`、`MainWindowBatch.cpp`、`test_batch.cpp`）与 `feature/watermark` 分支无重叠，`git merge` 自动合并、零冲突。


## 第十轮：用户实测漏检（Acrobat 背景/水印写法）
- 现象：用户的 2426 女士飘带领衬衫（22 页，Microsoft Print To PDF 输出后用 Acrobat 加「背景」水印）WPS 里有水印、苹果文稿里没有，拖到 watermark_drop.bat 显示「7 页/发现 0 项」（注：用户截图是另一份文件；本样例为后发的 22 页文件，同属此类）。
- 原因：水印是页面里 `/Artifact <</Subtype /Background>> BDC q /GS0 gs /Fm0 Do Q EMC`，倾斜角（Tm）和透明度（gs，0.39/0.33）都在 Form 内部；旧逻辑只看 `Do` 外层矩阵与透明度，且 Form 里有白色填充矩形被当作「非纯文字」，得分仅 30 被归为页码类保留。
- 修复：`scanForm()` 读取 Form 内部首个文字的倾斜角、最低透明度；白色填充/描边的图形不计入「非文字」；`PieceInfo/ADBE_CompoundType` 加 10 分。得分 95，默认去除。去除后各页仅水印区域像素变化，封面页不变。
- 测试：`tests/data_wm/G_acrobat_bg.pdf`（原文件前 6 页）+ `test_wm` 新增 G 段。ctest 4/4 通过。


## 第十一轮：B 型（同层店铺信息栏）
- 样例：F54 吊带包屁衣66（有/无水印，8 页）。信息栏（SNOW 标志、小红书/微信图标、联系方式、灰色底块）直接画在页面内容流里，与纸样同一图层；第 1 页另有「5cm 测试框」里的小号联系方式 + 二维码 + 黄色底块。用户提供的「无水印」版是 WPS 重新导出的，指令序列对不上，只能做像素对比。
- 算法（core/wm/wm.cpp `traceEvents` / `detectBlocks`）：把每页内容流拆成事件（文字、路径填充/描边、Do 图片/表单），记录页面坐标包围盒。
  1. 种子：联系方式类文字（≥8 位连续数字或 ≥6 位英文字母数字）且在 ≥60% 页重复出现。
  2. 容器：包围种子的有底色填充块（填充路径或「只含填充」的 Form，面积 <30% 页面）；一并去除，不受区间限制。
  3. 连续区间：从种子向前后扩展，遇到描边路径或含线条/文字的 Form 就停（纸样线条是描边，不会被卷进来）。
  4. 区间内、中心落在区域内的：填充路径、图片、只含填充的 Form 去除；文字只去种子和「每页同位置相同」的品牌字/图标（n≥3）。逐页变化的页码文字、只出现 1 页的「5cm 测试框」文字保留。
  5. 去除时只删路径构造+填充指令，不删颜色/状态指令；不再被引用的图片、Form 从页面资源里摘除，二维码不会留在文件里。
- 评分：重复 +40/+25、同位置 +15、压在底色块上 +15、面积 ≤10% +10、带每页相同品牌字/图标 +10；≥75 默认去除。F54 得分 90。
- 验证：与用户无水印版逐页像素对比，差异 <0.15%（抗锯齿）；第 5 页用户的「无水印版」里其实还留着 SNOW 字样，我们去得更干净。所有无水印文件、A/C/D/E/G 样例、80.pdf 无新误报。ctest 4/4。
- 已知限制：信息栏如果没有联系方式类文字（只有图标/店名）则不会被识别；图片水印/整页位图仍无法处理；本版未在 Windows 实机验证。


## 第十二轮：去除指定文字（用户需求：用户填写或选中指定内容）
- 样例：f57c0ada「小红叶501」（10 页，Microsoft Print To PDF）。PDF 里没有字体/文字，字符是 CAD 单线字体画成的短折线，每笔一个 `q cm m l.. S Q`。因此「输入文字」对这类文件无从匹配，只能「框选一次」。
- `wm::removeShape(in,out,page,rect,everywhere)`（rect 为页面左上角为原点、单位 pt）：选框内完整落入的描边线 = 选中笔画组；以点数最多的 ≤6 笔为锚，在各页找同点数笔画，枚举 镜像×4 旋转×2 方向，最小二乘估缩放（0.3–3 倍），再整组校验（允许 ≤10% 缺失；缺失笔画若落在页边缘/页外视为被页边切断，要求至少命中 max(3, 25%) 笔），命中后用所有匹配点二次拟合再匹配。只删路径构造 + 描边指令，保留颜色/线宽状态。容差 0.35pt（源坐标在 0.16pt 网格上）。
- `wm::listText/removeText`：解析 ToUnicode（bfchar/bfrange、codespace 宽度），Type0 默认 2 字节，无 ToUnicode 时按 Latin-1。去除 = 删掉含目标子串的 Tj/TJ。对线条字给出明确报错（提示改用框选）。
- 验证：f57c0ada 框选第 2 页「小红叶」13 笔 → 共去除 106 笔，涉及第 1–8 页（不同字号、180° 旋转、被页边切断的都命中），第 9–10 页本无此字；被去除线条最大 41pt，纸样轮廓线未动；「501」「面料A」保留。
- 待做：GUI（水印页里加「去除文字」：输入框 + 文字列表勾选 + 画布框选）；把框选的笔画组存成命名模板（如「小红叶」），以后输入名称即可跨文件去除；整字级匹配（只删「小红叶」不动单独出现的「叶」）目前靠整组校验，页边切断的情形只要求 25% 笔画命中。


## 第十三轮：水印处理界面 + 批量去水印（设计稿 6–11，用户确认：批量上限 20 个、阈值 75、其余按稿）
- 设计稿（Design artifact「去水印功能设计稿」）新增画板：6 去除指定文字（按内容）、7 框选图形（线条字）、8 批量添加文件并自动分组、9 配方设置与试跑、10 运行与报告、11 差异/校验失败/自动套用弹窗。
- 核心（core/wm）：`ShapeTemplate`、`extractShape`、`removeShapeTemplate`（`removeShape` 重构为 `shapeImpl`，模板作为「虚拟页」参与匹配）；`Recipe`、`signatureOf`、`conformity`、`groupReports`、`saveRecipe/loadRecipe`、`countStrokes`、`applyRecipe`（检测→指定文字→各图形模板，逐步写临时文件，最后按「线条数 = 原有 − 图形模板去除数」校验）；`drawPage`（矢量预览，递归进 Form，贝塞尔取 8 点折线，位图只画虚线框，无 ToUnicode 的双字节旧字体不画字）。
- CLI：`pdfwm group`、`make-recipe`、`batch --recipe --out`。
- Qt：`qt/WatermarkPanel`（三步）、`qt/DrawView`（预览 + 橡皮筋框选，原图/去除后的差异用「图元几何键」多重集合比较，不需要逐项目关联）。MainWindow 用 QStackedWidget 包了一层，顶栏第三个模式按钮；`plt_gui --watermark [文件…]` 直接进入。
- 测试：`test_wm`（分组、配方存取、吻合度、校验、模板）、`test_wmgui`（20 个上限、完整流程含框选图形与文字、不覆盖原件、嵌入主窗口）。ctest 5/5。
- 取舍/限制：①「需确认」项只能在单文件（组内只有 1 个文件）时勾选并一并去除，批量只自动去除「默认去除」项；②配方里的指定文字对同组文件按子串匹配，图形模板按形状匹配，二者不依赖检测；③预览不是位图渲染：不画图片内容、渐变/图案、剪裁路径，页面旋转（/Rotate）未处理，无 ToUnicode 的 GBK 字体文字显示为空；④批量按顺序单线程处理，20 个文件取决于页数；⑤Windows 打包/QPDF 12 未实机验证；⑥吻合度是启发式（均分 − 30×缺失 − 8×多余）。


## 第十四轮：PLT 线条字去除（用户需求：处理 plt 中「小红叶」类文字，输出仍为 plt，可快捷键进入 plt 转 pdf）
- 解析器 `core/plt.cpp` 为每条折线记录源文本区间（`Polyline.src`）和是否可编辑（`editable`；含 AA/AR/AT/RT 圆弧的线不可编辑）。
- `core/pltedit.cpp`：`extractTemplate`（框内完整落入的线 = 模板）、`findTemplateCopies`（相似变换匹配：镜像×4 旋转×正反方向，最小二乘缩放 0.3–3 倍，≥3 点锚笔画，网格索引，容差 0.12 mm）、`removeLines`（不重新生成文件：PD→PU，PA/PR 包成 `PU;…PD;`，CI/EA/ER 丢弃，其余字节不动，笔位不变，所以后面的相对移动仍正确）。
- `WatermarkPanel`：`.plt` 进入「PLT」组；预览用 plt::Document 在内存里画前/后（单位 mm，四周留 10 mm）；框选坐标换算回 mm；运行时每个文件按配方里的图形模板找线 → `removeLines` → 重新解析校验线条数 = 原有 − 去除数 → 写 `原名_去水印.plt`。第三步出现「转 PLT→PDF（n 个）· Ctrl+Enter」，发出 `convertRequested`，MainWindow 接上：1 个文件→`loadFile`（单文件模式），多个→批量转换 `addBatchFiles`。
- 测试：test_core（PLT 块：相对移动、镜像/放大/旋转 180、弧不可编辑、模板复用）、test_wmgui `pltLabelFlow`（3 个 PLT、含 1.5 倍副本、框选→运行→输出可解析→原件不变→Ctrl+Enter 离开水印页）。
- 未验证：真实 CAD 导出的带「小红叶」PLT（字符可能用圆弧/曲线或点很多的笔画，需要样本）；Windows 实机；Ctrl+Enter 在批量页是否与既有快捷键冲突（未发现）。


## 第十五轮：清除队列 / 预览缩放 / 输出后缀 haoyang
- `WatermarkPanel::clearQueue()` + 底栏按钮（有配方时二次确认，`setSilent(true)` 跳过确认）；运行中/分析中拒绝。`DrawView` 增加 zoom_/pan_（fitRect 基准 × zoom，滚轮锚点缩放），`pageToWidget` 供测试。输出名后缀常量在 `WatermarkPanel::startRun` 与 `tools/pdfwm.cpp`；CSV 报告名、输出文件夹名「去水印输出」未改。测试 `clearQueueAndZoom`。


## 第十六轮：卡顿/风扇狂转/开始处理置灰（用户反馈）
- 复现：`WM_REAL=文件1;文件2 test_wmgui idleIsQuiet`：2426（22 页）选组后连续翻页 20 次 = 111 CPU 秒。原因见 CHANGELOG。修复在 `WatermarkPanel::refreshPreview`（previewPool_、previewGen_ 原子、pvMutex_ 保护的 drawCache_/afterFiles_、`previewKey` 指纹）。
- 约定：任何会被「点击/翻页/拖动」触发的后台计算必须 ①单飞或只认最新 ②按 (输入, 参数) 缓存 ③不与「开始处理」抢同一个线程池。新增此类功能时加一个「快速操作 N 次，计算次数不增加」的测试（参考 `previewApplyCount()`）。
- 测试注意：会话文件 `batch_session.json` 会让下一个 MainWindow 弹「恢复会话」模态框，测试必须清理（`pltToPdfStaysResponsive` 已处理）。
- 未能复现：用户说的「+VX: xiangersggzs」水印检测不到——手头所有真实样本（2426、17a8ba2f、小童花边、F54、f57c0ada）在最新版的检测结果与旧版一致（core/wm 自 1d9d75d 起未改动）；需要用户提供那份 PDF。


## 第十七轮：文档1/文档2 漏检、全类型复查、性能
- 文档1：Form XObject 内的倾斜灰色文字，每页位置不同 → `detectArtifacts` 用 `textSig` 聚类（`DoRec.textSig`，`Finding.extraKeys` 存同簇其余表单键，`remove()` 一并丢弃）。
- 文档2：Fill & Sign 嵌套表单（`DoRec.fillSign`，`decodeFormText` 经 ToUnicode 解码），+60，联系方式再 +25；同层平铺倾斜文字在 `detectBlocks` 里 +30（`Ev.angle`）。
- 性能：`core/wm/wm.cpp` 自写 `Tokenizer`/`Arg`/`Instr`（保留原文字节，序列化不改写数字）；`traceEvents(..., keepPts=false)`；`WatermarkPanel::workPool_`（≤3 线程、低优先级）。仍可优化：`applyRecipe`/`drawPage`/`countStrokes` 各自重复解析整份文件（文档2 首次审阅预览约 7 s，后台单线程、有缓存）。
- 位图型（e8cf131f）：页面由 11 块 JPEG 拼成，水印在像素里，无独立对象，不可去除。
- 回归：tests/test_wm.cpp 新增 I、J 两段；ctest 5/5。


## 第十八轮：参数区优化 + 手动空白页（单文件/批量共用面板）
- `qt/Widgets.h`：`ClickSpinBox`（锁定=只读+无按钮，点击/`eventFilter` 解锁，editingFinished/失焦/Esc 重新锁定，`wheelEvent` 一律忽略）、`NoWheelSlider`。无 Q_OBJECT：测试里用 `dynamic_cast`，不要用 `qobject_cast/findChild<ClickSpinBox*>`（会匹配所有 QDoubleSpinBox）。
- `State`：默认 margin 0 / lineWidth 0.1 / ignoreFrame true；新增 `fileTag`（单文件）、`blanked`（QStringList，「行-列」标签）。`loadSettings` 设置版本 3 才读取旧的边距/重叠/线宽/校准框。批量的文件名开关仍是 `BatchOptions::fileTag`（默认 false，面板里的单文件开关在批量模式隐藏；单→批 `convertToBatch` 会带过去）。
- 手动空白页：`layout::buildPlan` 对 `blankLabels` 里的格子清空纸样线条、`manual=true`、不被 skipEmpty 丢弃；`chooseOrientation/autoOffset` 忽略它。`MainWindow::setPagesBlank/updateBlankButtons/pageContextMenu`；`applyState` 在几何/选区变化时清空；批量 `deriveState` 保留每个文件自己的 `blanked`。
- 测试隔离：test_gui / test_wmgui 通过 `XDG_CONFIG_HOME` 隔离设置（之前会污染 ~/.config 导致默认值断言偶发失败）。


## 第十九轮：性能回调
- 第十六轮为防 CPU 暴走把水印线程限为 ≤3、低优先级、预览单线程、文件串行，实际过慢。现在：`workPool_` = clamp(核数−1, 2, 8) 正常优先级；`previewPool_` = 2 线程 + `applyMutex_`；`startRun` 每文件一个 `async` 任务，用 `remaining` 计数收尾。
- `applyRecipe`：不再先 `analyze` 再 `remove`（`remove` 返回 `kNothingToRemove` 视为“没有可去除”）；`countStrokes(in)` 用 `std::async` 并行，`verify=false` 时两次计数都跳过。
- `isOp()`、`fastDeflate()`（`Pl_Flate::setCompressionLevel(3)`，进程级设置）。
- 已知：新聚类逻辑下文档1型的得分证据文字变了（例如 A_artifact 100→85，仍为 High，仅缺「内容和位置完全相同/Artifact/PieceInfo」加分项）；如需恢复可在 `detectArtifacts` 簇分支补这些证据。


## 第二十轮：位图彩色水印去除 + 一键添加水印
- `core/wm/bitmap.{h,cpp}`：模型 pixel = (1-a)·c·(1,1,1) + a·W。色度向量 q = p − mean(p) 指向固定方向 u，|q| = a·K；K、mean(W) 由「叠在白纸」(mean>120) 与「叠在黑线」(mean<70) 两组样本的中位数比值解出。`fit` 拒绝条件：彩色像素 <0.05%、色相一致 <93%、饱和像素（色度>70）>5%（= 纸样本身是彩色，如 E_bitmap 的蓝线）。`clean`：max−min≤4 的像素不动；色度>75 或色相不符的不动；c≥200 记为纸白；字形边缘 2px 内 ≥215 及全图 ≥244 的近白像素刷成 255。
- `detectBitmap` 抽样 3 页（首/中/尾）拟合，结果放 `Finding::bitmapModel`；`remove()` 对每页 ≥25 万像素的图片逐张清理（同一对象只做一次），`RemoveResult::imagesCleaned/pixelsChanged`，计入 `FileResult::removedTotal()`。
- `drawPage` 现在给图片返回 `DrawItem::Image`（≤1400px 预览像素），`DrawView` 按 CTM 画出；审阅提示「本页图片里的彩色水印将逐像素去除」。
- `wm::addWatermark(in,out,StampSpec)`：`insertionSlots` 找可插入位置（BT/ET 外、无标记内容、不在建路径中、无裁剪；且在最后一个大面积不透明绘制之后——Do/sh/内联图/面积>2% 的填充），单元 = `q [tile×inv(CTM)] cm /GSnnn gs r g b rg <path> f Q`，ExtGState 名称随机 `/GS100..999`，`/BM /Multiply`。`pdfwm stamp-boxes in out` 为调试命令。
- `qt/Stamp.{h,cpp}`：`StampSettings`（QSettings 组 `stamp`）、`buildStampSpec`（字体 200px/em 建轮廓再缩放，每字一个 piece）、`StampDialog`（带正片叠底预览）、`runStampFlow`（后台线程 + 进度框；`PLT_NO_DIALOGS` 环境变量用于测试时不弹结果框）。
- 测试：test_wm K 段（去色、黑线像素数不变、批量配方路径校验通过、E_bitmap 被拒绝且原因正确），test_wmgui `stampIsPlainContent`（无新标记、线条/文字数不变、一键流程输出 `_水印`）。
- 未验证：没有在真实 WPS 里试过「删除水印」；设计依据是它识别水印所依赖的结构标记，本实现一个都不产生。


## 第二十一轮：软件改名 + 应用图标 + 独立「加水印」模式 + 导出时加水印 + 空格拖拽
- **背景**：用户提出 4 点需求——①给 PDF 加水印单独开一个入口，支持单个/批量；②PLT→PDF、去水印导出前可以选择加水印，参数（字体/颜色/间隔密度/透明度）可设；③说明当前版本管理方式，保证以后更新互不影响；④先确认计划再开工——确认后追加：水印仍要求 WPS 识别不到；设计一个含「好样」二字的应用图标；单文件 PLT→PDF 后续要出 App/小程序端（提醒 `core/` 保持 Qt 无关）；version 管理问题要「想办法不影响现有功能，不改写代码」来解决（即不做大范围重构，只加新文件/新分支点）。中途又追加：软件改名「好样格式转换」、运行性能不能降、预览面板统一支持空格拖拽。
- **软件改名**：`qt/MainWindow.cpp` 窗口标题与顶部 logo 文案改为「好样格式转换」；`packaging/installer.iss` 的 `AppName`/`DefaultDirName`/`DefaultGroupName`/`OutputBaseFilename`/`[Icons]`/`[Run]` 文案同步；`packaging/build_windows.ps1`、`.github/workflows/windows-installer.yml` 里的产物文件名（zip/exe）同步改掉。**特意不改**：`QSettings` 的组织名/应用名（`"HaoyangPattern"`/`"PltConverter"`）和 `installer.iss` 的 `AppId` GUID——两者都是已装用户设置/升级识别的键，改名不改这俩，老用户设置和「原地升级」都不受影响。
- **应用图标**：用 Playwright 截图 + Pillow 重新采样生成多尺寸 `packaging/icon.ico`（16/24/32/48/64/128/256）。设计过程：先出 3 个图形概念，用户要求图标上要出现「好样」两个汉字，于是改为纯文字构图（概念 D/E 两版），用户选定 E 版（满版淡色纸样图纸底纹 + 虚线裁片轮廓 + 居中「好样」大字，红/米色调）；中途我主动提出 E 版底纹在 16px 太淡几乎看不见，建议简化为概念 F，用户先同意「用 E 的构图但简化底纹」，但最终改口「还是用回 E 版吧」——最终产物是未简化的原始 E 版。接入方式：`CMakeLists.txt` 在 `WIN32` 下 `enable_language(RC)` + `configure_file(packaging/app.rc.in → build/app.rc @ONLY)`，`.rc` 里 `IDI_ICON1 ICON` 指向图标绝对路径，并写入 `VS_VERSION_INFO`（公司名「好样纸样」、产品名/描述「好样格式转换」，版本号取自 CMake `PROJECT_VERSION`）；`plt_gui` target 新增 `target_sources(... "${CMAKE_BINARY_DIR}/app.rc")`（仅 WIN32）。`installer.iss` 新增 `SetupIconFile=icon.ico`。**未验证**：没有真实 Windows 环境跑过 `rc.exe`，图标/版本信息在 exe 上的实际显示效果要等 CI 的 Windows 构建才能确认。
- **新增独立「加水印」模式**（对应需求①）：顶部新增第 4 个按钮「加水印」（`MainWindow::showStamp`/`stampVisible`/`stampPanel`，镜像已有 `showWatermark` 一套），进入全新 `StampPanel`（`qt/Stamp.h/.cpp`）——自己的文件列表，支持拖拽/「添加文件」/移除/清空，不需要先走「去水印」或「PLT→PDF」。内部复用重构后的 `StampForm`（见下）。
- **导出前可选加水印**（对应需求②）：单文件导出面板（`MainWindow::buildExportPanel`）在导出按钮上方新增「导出时添加水印」勾选框 +「水印设置…」按钮；批量导出页（`MainWindowBatch.cpp`）同样新增一行。勾选后，`onExport()` / `batchStartExport()` 在**已有的**导出完成之后，直接对刚导出好的文件调用 `wm::addWatermark`（通过 `stampOutputPath` 决定最终文件名），而不是额外起一个处理阶段；默认不勾选，不影响现有导出行为。批量路径特别处理：加水印失败时保留未加水印的导出结果而不让整项失败（best-effort）。**性能**：这是直接响应用户「注意运行性能不能太低」的要求——批量加水印复用已有的 `exportPool_` 线程池、在同一个 per-file 任务里顺手做，没有新增一遍文件遍历或新的并行层；全量 ctest 墙钟时间（~63–65s）加入该功能前后无变化。
- **水印参数拆分与预设**（对应需求②「间隔密度」）：原来单一 `gapMm` 拆成 `gapXMm`/`gapYMm`（横向/纵向独立），并提供稀（45mm）/中（25mm）/密（10mm）三个一键预设按钮（点预设会把两个轴设成同一个值，之后仍可单独微调任一轴，不额外记"自定义"状态）。`StampSettings::load()` 做了向后兼容迁移：若新键不存在但老的单一 `gapMm` 存在，复制到两个新字段；`save()` 始终写新键并删除旧键。
- **水印引擎本体未动**（对应需求⑤「WPS 不能检测到」+ 需求⑦「App/小程序端复用」）：`core/wm/wm.h/.cpp` 的 `addWatermark`/`StampSpec`/`insertionSlots` 这次完全没有改动逻辑——它原本就是 Qt/Windows 无关的（只用 QPDF + `std::filesystem`），本来就满足「一套内核，多端复用」。评估过是否要把它物理搬进独立的 `core/stamp/` 目录来更贴合用户「单独模块」的字面意思，判断收益低风险高（`wm.cpp` 内部有私有辅助函数要跟着公开或复制一份）后决定不搬，在提交信息里说明了这个工程判断，而不是悄悄绕开。回归测试 `stampIsPlainContent()` 仍验证：输出 PDF 无任何新增标记（无注释/Form XObject/`/Artifact`/`/Watermark`/OCG/PieceInfo），纸样线条数、文字数不变。
- **版本管理问题（需求③④⑧）的回答与落地**：当面回答了用户「现在怎么管版本」的问题（worktree 分支模型：`main` 发布分支打 tag，`feature/watermark` 本次开发分支，功能分支合并前跑全量 ctest），并按「不改写代码、只加不改」的要求：本次所有新功能都是新增文件（`qt/Stamp.*`、`packaging/*`）或在既有函数里加 `#ifdef PLT_WITH_WM` 包裹的新增代码块，没有对 `core/wm/wm.cpp` 等已验证稳定的核心逻辑做改写；新增 `.gitignore`（`/build/`、`/build-*/`、`*.o`、`*.obj`）防止重复发生「误提交 build 目录」（`fix/batch-preview` 合并时发现并清理过一次 271 文件的构建产物提交）。
- **预览面板空格拖拽统一**（对应需求④）：`DrawView`（去水印/加水印用的预览，原来只有右键/中键拖动）补上了 `CanvasWidget`（PLT→PDF 预览）已有的「按住空格，左键也能拖」——按下 `Qt::Key_Space`（忽略 `isAutoRepeat()`）置位 `space_`，与左键拖动条件做 OR；`setFocusPolicy(Qt::StrongFocus)` + `focusOutEvent` 失焦自动复位，避免切窗口/弹对话框时卡在「打开的手」光标。新增回归测试验证按住空格可左键拖动、松开或失焦后恢复。
- **测试**：`tests/test_wmgui.cpp` 新增 `stampPanelMode()`（独立加水印模式：加文件/移除/清空/运行）、`mainWindowStampMode()`（顶部第 4 按钮切换）、`singleFileExportStampControls()`（单文件导出面板的勾选框+设置按钮）、空格拖拽回归块；修了合并/重构带来的 `st.gapMm` → `gapXMm`/`gapYMm` 编译错误。`tests/test_batch.cpp` 新增批量导出页加水印控件的存在性/默认状态/`PLT_WITH_WM` 可见性校验。全量 ctest 5/5 通过。
- **未验证/已知限制**：① Windows `rc.exe` 编译 `.rc`、图标与版本信息在真实 exe 上的效果，需等 GitHub Actions Windows 构建验证；② 新的间距预设/独立加水印模式没有再跑一次真实 WPS「删除水印」测试（底层 `addWatermark` 引擎本身的无标记保证经单测验证、且本轮未改动）；③ 结构性验证（`grep`/`awk`）确认所有新代码在 `PLT_WITH_WM` 缺失时仍可正常跳过编译，但未实际在无 QPDF 环境里编译验证。
- 提交：`762a045`（独立加水印模式/导出时加水印/间距预设）、`8de18fc`（改名+空格拖拽）、`d036a43`（图标接入）。


## 第二十二轮：页边距/重叠区合并 + 拼接效果图挪到最后一页 + 排版窗口点击空白/恢复
- **背景**：用户提出 4 点需求——①【页边距】=【重叠区】合二为一，蓝色定位虚线即页边距线，框外到纸边始终印纸样；②拼接效果图挪到 PDF 最后一页；③空白页在**排版设置的排版窗口**（`CanvasWidget`，不是缩略图列表）里点击即可删除/恢复；④页边距控件挪到「图形位置」顶部；并要求先讲清楚理解和计划、确认后再开工。确认时用户只对③重新定了范围（必须是排版窗口画布，而不是我最初理解的缩略图列表），其余 3 点按计划直接通过。
- **分支基础纠正**：原计划在 `main` 上开新分支，但 `main` 还是 1.0.0 发布版，没有手动空白页引擎（`Page::manual`/`State::blanked`，第十八轮才加，只在 `feature/watermark` 上）；发现后删掉重建，改为从 `feature/watermark` 切出 `feature/layout-margin-merge`。
- **页边距/重叠区合并**（核心 `core/layout.h/.cpp`）：`Params` 去掉 `overlap`，只留 `margin`（默认 10mm）；`tileGeometry` 原来 `inset=max(overlap,margin)`、`strip=inset-margin`（框外先留白再印重叠）简化为 `inset=strip=margin`（框外到纸边整段都印纸样，无单独"不印安全区"）。结构性结论：合并后 `fw+2*strip` 恒等于 `pageW`，即一页是否够用只取决于 `offX+图宽 ≤ pageW`，与页边距取值完全无关——这是旧的非对称页边距/重叠模型下不成立的新性质，测试里据此重新推导了多处坐标。Qt 层联动：`State.h`（字段/序列化/比较）、`MainWindow.cpp/h`（控件合并、`syncUi`、`loadSettings`/`saveSettings` 版本 3→4，旧值不沿用）、`MainWindowBatch.cpp`、`BatchCore.cpp`（错误文案）、`plt2pdf.cpp`（去掉 `--overlap`）。已保存的批量模板 JSON 若带旧 `overlap` 键，迁移时取 `max(margin,overlap)` 尽量保留原拼接位置（比 QSettings 更保守，因为是用户主动存的模板文件）。
- **拼接效果图挪到最后一页**：`layout::buildPlan` 里 `pages.insert(begin(),...)` 改 `push_back(...)`；开关文案「PDF 第 1 页」→「PDF 最后一页」。
- **排版窗口点击空白/恢复**（对应用户重新界定的③）：`CanvasWidget::Grid` 加 `blank`/`manual`（`QSet<int>`，复用已有的手动空白页引擎，不新建状态）；`cellAt`/`clickableCellAt` 判定：有内容格可点击变空白，手动空白格可点击恢复，从未有内容的格子不可点；`mouseReleaseEvent` 用 4px 位移阈值区分"点击"（发 `cellClicked` 信号）和"拖动图形"（原有的 `offsetEdited`）；悬停时光标按模式给反馈。`MainWindow` 新增 `connect(canvas_, &CanvasWidget::cellClicked, ...)` 调用既有的 `setPagesBlank`，与缩略图列表的"设为空白页"共享同一套状态，互相同步。新增测试辅助 `CanvasWidget::testCellCenter` 供 `QTest::mousePress/mouseMove/mouseRelease` 模拟点击。
- **页边距控件挪位**（对应④）：合并后的页边距滑块+输入框从原来的位置搬到「图形位置（mm）」区块最上方，原有的分段预设按钮组去掉。
- **测试重新推导**：`tests/test_core.cpp` 的注册框几何测试（原 `margin=5,overlap=10`）改成 `margin=10` 后，拼接缝坐标、竖线坐标等全部手推重算（`local_x = global_x + margin - u0`，margin 5→10 对应 +5 偏移），验证了"两条竖线恰好相距一个框宽"这个与页边距无关的不变量仍成立；`tests/test_batch.cpp` 的"优化位置提示"子测试因新结构性质（页面是否够用不再看页边距）而失效，改用 `offX=110`（保证 `110+190>297` 必然触发二列）重新设计。
- **调试插曲（环境问题，非代码 bug）**：改完后直接跑 `./test_gui`（没按 `ctest` 的方式传 `tests/data` 作为 `argv[1]`）导致测试里"把样例 PLT 复制到一个中文+空格路径再导入"的步骤因相对路径 `tests/data` 解析不到源文件而静默失败（`QFile::copy` 返回值未检查），复制目标根本不存在，导入时弹出"源文件不可访问"的 `QMessageBox::warning`——在 `QT_QPA_PLATFORM=offscreen` 下没有人去关掉这个弹窗，`test_gui` 卡死在 `QDialog::exec()`。用 gdb 对卡住的进程打 `thread apply all bt` 定位到这一行，并确认**旧的、本轮完全没改过的** `build/test_gui` 二进制在同样的误操作下一样卡死，排除是本轮代码改动引入的问题；换成 `ctest`（自动传入 `${CMAKE_SOURCE_DIR}/tests/data`）后运行正常。顺手把测试里另一个真实存在、但更隐蔽的小毛病也修了："坏文件被拒绝"两个用例原来只用一次性 `QTimer::singleShot(300,...)` 去关掉预期的错误弹窗，如果异步解析耗时正好跨过 300ms（例如线程池冷启动），弹窗会在定时器已经触发过之后才出现，没人再去关，一样会卡死；改成 `loadDismissingDialogs()`：用一个 30ms 周期的 `QTimer` 在整个 `load()` 调用期间持续清理弹窗，不再赌一次时机。
- **验证**：`ninja` 全量重编译零错误（除既有无关警告）；`ctest --output-on-failure` 5/5 全过（core/batch/wmgui/gui/wm，总耗时与改动前相近，~98s）；新增的排版窗口点击测试 4 个断言（找到可点内容格与自动空白格、点内容格变空白、再点恢复、点自动空白格无效果）全部通过。
- **未验证/已知限制**：① Windows 实机未测；② 本轮未改动水印/加水印相关代码；③ 页边距 0mm 仍不代表物理零边距（部分打印机强制留白），提示文案已更新但依赖用户用校准框自行校准，未做自动检测。
