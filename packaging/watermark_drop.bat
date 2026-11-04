@echo off
chcp 65001 >nul
setlocal
if "%~1"=="" (
  echo 用法：把要去水印的 PDF 文件拖到本文件上，或拖到「去水印（把PDF拖到这里）」快捷方式上。
  pause
  exit /b 1
)
set "IN=%~1"
set "OUT=%~dpn1_去水印.pdf"
echo ===== 检测结果 =====
"%~dp0pdfwm.exe" analyze "%IN%"
echo.
echo ===== 去除（只处理「默认去除」的项，不会覆盖原文件）=====
"%~dp0pdfwm.exe" remove "%IN%" -o "%OUT%"
if errorlevel 1 (
  echo.
  echo 没有生成新文件（可能没有可去除的水印，或文件属于整页位图）。
) else (
  echo.
  echo 已生成：%OUT%
)
echo.
pause
