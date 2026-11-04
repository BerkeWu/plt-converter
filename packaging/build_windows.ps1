<#
 在 Windows 上一键构建：编译 -> 核心测试 -> windeployqt -> 便携 zip -> 安装包（需要已安装 Inno Setup 6）
 前置：Visual Studio 2022（含「使用 C++ 的桌面开发」）、CMake、Qt 6（MSVC 2019/2022 64 位，如 C:\Qt\6.6.3\msvc2019_64）
 用法（在工程根目录的 PowerShell 里）：
   .\packaging\build_windows.ps1 -QtDir C:\Qt\6.6.3\msvc2019_64
#>
param(
  [Parameter(Mandatory = $true)][string]$QtDir,
  [string]$Version = "1.0.0",
  [string]$Generator = "Visual Studio 17 2022"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

cmake -S . -B build -G $Generator -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"
cmake --build build --config Release
# 核心测试（含中文路径用例）；界面测试依赖 poppler 的 pdfinfo，Windows 上跳过
ctest --test-dir build -C Release -R core --output-on-failure

$deploy = Join-Path $root "dist\app"
if (Test-Path $deploy) { Remove-Item $deploy -Recurse -Force }
New-Item -ItemType Directory -Path $deploy | Out-Null
Copy-Item build\Release\plt_gui.exe $deploy
Copy-Item build\Release\plt2pdf.exe $deploy
& "$QtDir\bin\windeployqt.exe" --release --no-translations --no-opengl-sw (Join-Path $deploy "plt_gui.exe")

$zip = Join-Path $root "dist\PLT-Converter-portable-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$deploy\*" -DestinationPath $zip
Write-Host "便携版: $zip"

$iscc = @("$env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
  & $iscc "/DAppVersion=$Version" "/DDeployDir=$deploy" packaging\installer.iss
  Write-Host "安装包: dist\PLT-Converter-Setup-$Version.exe"
} else {
  Write-Warning "未找到 Inno Setup 6，已跳过安装包（便携版可直接解压运行）。下载：https://jrsoftware.org/isdl.php"
}
