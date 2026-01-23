@echo off
REM 快速设置开发环境的脚本
REM 运行此脚本后，当前命令行窗口将配置好 Visual Studio 开发环境

REM 设置代码页为 GBK 以正确显示中文
chcp 936 >nul 2>&1

REM 查找 Visual Studio Build Tools
set "VS_PATH="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo 找到 Visual Studio 2022 Build Tools
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo 找到 Visual Studio 2019 Build Tools
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo 找到 Visual Studio 2017 Build Tools
) else (
    echo 错误: 未找到 Visual Studio Build Tools
    echo 请确保已安装 Visual Studio Build Tools
    pause
    exit /b 1
)

REM 设置开发环境
echo 正在设置开发环境...
if defined VS_PATH (
    call "%VS_PATH%" >nul 2>&1
    REM 恢复代码页
    chcp 936 >nul 2>&1
) else (
    echo 错误: VS_PATH 未设置
    pause
    exit /b 1
)

echo.
echo ========================================
echo 环境已配置完成！
echo ========================================
echo 您现在可以使用以下命令：
echo   msbuild MarkdownViewer.sln /p:Configuration=Release /p:Platform=x64
echo   或
echo   build.bat
echo ========================================
echo.

REM 验证环境
echo 验证环境...
cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] C++ 编译器可用
) else (
    echo [错误] C++ 编译器不可用
)

msbuild /version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] MSBuild 可用
) else (
    echo [错误] MSBuild 不可用
)

echo.
echo 环境准备就绪！
