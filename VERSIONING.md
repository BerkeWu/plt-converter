# 版本管理约定

## 版本号
`MAJOR.MINOR.PATCH`，唯一来源是根目录 `VERSION` 文件（CMake、安装包、各端构建脚本都读它）。
- MAJOR：输出文件格式或核心接口不兼容变化
- MINOR：新增功能（如 1.1.0 = 去水印/加水印）
- PATCH：缺陷修复

## 分支
- `main`：始终可发布。只接受已通过 ctest 的合并
- `feature/<名称>`：功能开发（如 `feature/watermark`）
- `release/x.y`：发布前冻结，只修缺陷
- 标签 `vX.Y.Z`：每次发布打一次，带注释，说明验证情况

## 提交信息
`类型(范围): 摘要`，类型取 feat / fix / docs / test / build / refactor。例：`feat(core): 水印候选评分`

## 一套内核，多端复用（Web / 小程序 / Android / iOS）
`core/` 是不依赖 Qt 的 C++17 库，是所有端共享的唯一逻辑来源。规则：
1. `core/` 不得包含 Qt、Windows 或文件对话框相关头文件；输入输出只用内存缓冲区和 `std::filesystem::path`
2. 各端只写“壳”：界面 + 文件选择 + 调用 core + 保存。壳里不写排版或水印判定逻辑
3. 各端接入方式（待做，按需排期）：
   | 端 | 接入方式 |
   |---|---|
   | Windows 桌面 | 现有 Qt6 壳 |
   | Web | core 编译为 WebAssembly（Emscripten），浏览器本地处理，文件不上传 |
   | 微信/支付宝小程序 | 同一份 WASM（需评估包体积与内存上限）；大文件改走服务端 |
   | Android | NDK + JNI 调用 core，界面用 Kotlin |
   | iOS | core 以静态库接入，Swift 经 C 接口调用 |
4. 为此 core 需补一层稳定 C 接口 `core/capi.h`（解析→规划→逐页取绘图指令），各端 PDF 写出可用各自平台的 PDF 库，共用同一份“绘图指令”。这样 4 个端的输出版式完全一致
5. 回归基线：`tests/data/` 的 5 个样例 + 页数/几何黄金值，任何端不得偏离
