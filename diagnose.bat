@echo off
REM 诊断脚本 - 检查构建环境配置

REM 设置代码页为 GBK 以正确显示中文
chcp 936 >nul 2>&1

echo ========================================
echo 构建环境诊断工具
echo ========================================
echo.

REM 检查当前目录
echo [1] 检查当前目录
echo 当前目录: %CD%
echo.

REM 检查项目文件
echo [2] 检查项目文件
if exist "MarkdownViewer.sln" (
    echo [OK] MarkdownViewer.sln 存在
) else (
    echo [错误] MarkdownViewer.sln 不存在
    echo 请确保在项目根目录下运行此脚本
    pause
    exit /b 1
)

if exist "MarkdownViewer.vcxproj" (
    echo [OK] MarkdownViewer.vcxproj 存在
) else (
    echo [错误] MarkdownViewer.vcxproj 不存在
)

echo.

REM 检查 Visual Studio Build Tools
echo [3] 检查 Visual Studio Build Tools
set VS_FOUND=0
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    echo [OK] Visual Studio 2022 Build Tools 已安装
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" (
    echo [OK] Visual Studio 2019 Build Tools 已安装
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat" (
    echo [OK] Visual Studio 2017 Build Tools 已安装
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat"
)

if %VS_FOUND%==0 (
    echo [错误] 未找到 Visual Studio Build Tools
    echo 请安装 Visual Studio Build Tools
    pause
    exit /b 1
)

echo.

REM 设置环境并检查工具
echo [4] 设置开发环境并检查工具...
call "%VS_PATH%" >nul 2>&1
REM 恢复代码页
chcp 936 >nul 2>&1

REM 确保回到项目目录
cd /d "%CD%"

echo.
echo [5] 检查编译工具
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] C++ 编译器 (cl.exe) 可用
    cl 2>&1 | findstr /C:"Microsoft" | findstr /C:"Compiler" >nul
    if %ERRORLEVEL% EQU 0 (
        cl 2>&1 | findstr /C:"Version"
    )
) else (
    echo [错误] C++ 编译器不可用
)

where msbuild >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] MSBuild 可用
    msbuild /version
) else (
    echo [错误] MSBuild 不可用
)

echo.

REM 检查解决方案文件路径
echo [6] 检查解决方案文件路径
set SCRIPT_DIR=%~dp0
echo 脚本目录: %SCRIPT_DIR%
echo 解决方案文件: %SCRIPT_DIR%MarkdownViewer.sln
if exist "%SCRIPT_DIR%MarkdownViewer.sln" (
    echo [OK] 解决方案文件路径正确
) else (
    echo [警告] 使用相对路径可能有问题
)

echo.
echo ========================================
echo 诊断完成
echo ========================================
echo.
echo 如果所有检查都通过，可以尝试运行:
echo   build.bat
echo.
pause
