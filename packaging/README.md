# Windows 打包说明

产物有两个：
- `PLT-Converter-Setup-<版本>.exe` — 安装包（Inno Setup，默认装到当前用户目录，无需管理员；安装时可选管理员模式装到 Program Files）
- `PLT-Converter-portable-<版本>.zip` — 便携版，解压后双击 `plt_gui.exe`（同目录还有命令行工具 `plt2pdf.exe`）

## 方式 A：GitHub Actions（本机不用装任何东西）
把工程推到 GitHub，Actions 页选 `windows-installer` → Run workflow，几分钟后在 Artifacts 下载。

## 方式 B：本机构建
装好 Visual Studio 2022（C++ 桌面开发）、CMake、Qt 6（MSVC 64 位）、Inno Setup 6，然后：

    .\packaging\build_windows.ps1 -QtDir C:\Qt\6.6.3\msvc2019_64

## 验证清单（建议逐项勾）
1. 在没装 Qt / VS 的干净电脑或虚拟机上安装并启动（检查是否缺 DLL）
2. 导入中文路径、带空格路径、大写 .PLT 的文件
3. 三步流程走完，导出 PDF；用 Acrobat / 浏览器打开：中文水印是否为微软雅黑、是否嵌入
4. 实际打印一页，用校准框量 5×5 cm；打印设置选「实际大小 / 100%」
5. 125% / 150% 缩放的高分屏上界面是否清晰、不重叠
6. 拖入 20+ 页的大文件，界面是否流畅
