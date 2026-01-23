@echo off
REM MarkdownViewer 构建脚本
REM 用法: build.bat [Configuration] [Platform] [MSBuild参数]
REM 示例: build.bat Release x64
REM        build.bat Debug Win32 /t:Rebuild

REM 设置代码页为 GBK 以正确显示中文（Windows 中文系统默认）
chcp 936 >nul 2>&1

setlocal enabledelayedexpansion

REM 保存当前目录（脚本所在目录）
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

REM 检查并终止正在运行的 MarkdownViewer.exe 进程
echo Checking for running MarkdownViewer.exe processes...
for /f "tokens=2" %%i in ('tasklist ^| findstr /i "MarkdownViewer.exe"') do (
    echo Terminating process ID: %%i
    taskkill /f /pid %%i
    if !ERRORLEVEL! EQU 0 (
        echo Successfully terminated MarkdownViewer.exe process %%i
    ) else (
        echo Failed to terminate MarkdownViewer.exe process %%i
    )
)

echo Continuing with build...
echo.



REM 默认配置
set "CONFIG=Release"
set "PLATFORM=x64"

REM 解析参数
if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"

REM 检查解决方案文件是否存在
if not exist "MarkdownViewer.sln" (
    echo Error: MarkdownViewer.sln not found
    echo Current directory: %CD%
    echo Please run this script from the project root directory
    popd
    exit /b 1
)

REM 查找 Visual Studio Build Tools
set "VS_PATH="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo Found Visual Studio 2022 Build Tools
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo Found Visual Studio 2019 Build Tools
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat"
    echo Found Visual Studio 2017 Build Tools
) else (
    echo Error: Visual Studio Build Tools not found
    echo Please ensure Visual Studio Build Tools is installed
    popd
    exit /b 1
)

REM 设置开发环境
echo Setting up development environment...
if defined VS_PATH (
    REM 调用 VsDevCmd.bat 并重定向所有输出
    call "%VS_PATH%" >nul 2>&1
    REM 强制恢复代码页为 GBK（VsDevCmd 可能会改变代码页）
    chcp 936 >nul 2>&1
) else (
    echo Error: VS_PATH not set
    popd
    exit /b 1
)

REM 确保仍在项目目录
cd /d "%SCRIPT_DIR%"

REM 显示配置信息
echo.
echo ========================================
echo Build Configuration
echo ========================================
echo Configuration: %CONFIG%
echo Platform: %PLATFORM%
echo Solution: MarkdownViewer.sln
echo ========================================
echo.

REM 构建项目
echo Starting build...
msbuild "MarkdownViewer.sln" /p:Configuration="%CONFIG%" /p:Platform="%PLATFORM%" %*

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build succeeded!
    echo ========================================
    echo Output directory: %CONFIG%\%PLATFORM%\
) else (
    echo.
    echo ========================================
    echo Build failed! Error code: %ERRORLEVEL%
    echo ========================================
    popd
    exit /b %ERRORLEVEL%
)

popd
endlocal
