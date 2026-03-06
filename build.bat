@echo off
REM MarkdownViewer 构建脚本
REM 用法: build.bat [Configuration] [Platform] [MSBuild参数]
REM 示例: build.bat Release x64
REM        build.bat Debug Win32 /t:Rebuild

REM 设置代码页为 GBK 以正确显示中文（Windows 中文系统默认）
::chcp 936 >nul 2>&1

setlocal enabledelayedexpansion

REM 保存当前目录（脚本所在目录）
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

REM 检查并终止正在运行的 mdview.exe 进程
echo Checking for running mdview.exe processes...
for /f "tokens=2" %%i in ('tasklist ^| findstr /i "mdview.exe"') do (
    echo Terminating process ID: %%i
    taskkill /f /pid %%i
    if !ERRORLEVEL! EQU 0 (
        echo Successfully terminated mdview.exe process %%i
    ) else (
        echo Failed to terminate mdview.exe process %%i
    )
)

echo Continuing with build...
echo.

REM 默认配置
set "CONFIG=Release"
set "PLATFORM=x64"
set "MSBUILD_ARGS="

REM 解析参数
if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
if not "%~1"=="" shift
if not "%~1"=="" shift

:collect_extra_args
if "%~1"=="" goto args_done
set "MSBUILD_ARGS=%MSBUILD_ARGS% %~1"
shift
goto collect_extra_args
:args_done

REM 检查解决方案文件是否存在
if not exist "MarkdownViewer.sln" (
    echo Error: MarkdownViewer.sln not found
    echo Current directory: %CD%
    echo Please run this script from the project root directory
    popd
    exit /b 1
)

REM 查找 Visual Studio 开发命令环境
set "VS_PATH="
call :find_vsdevcmd
if errorlevel 1 (
    echo Error: Visual Studio development environment not found
    echo Please ensure Visual Studio Build Tools or Visual Studio with C++ workload is installed
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
echo Toolchain: %VS_PATH%
echo ========================================
echo.

REM 构建项目
echo Starting build...
msbuild "MarkdownViewer.sln" /p:Configuration="%CONFIG%" /p:Platform="%PLATFORM%"%MSBUILD_ARGS%

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
exit /b 0

:find_vsdevcmd
set "VSWHERE_EXE="
set "VS_INSTANCE_PATH="

if defined VSWHERE_PATH (
    if exist "%VSWHERE_PATH%" (
        set "VSWHERE_EXE=%VSWHERE_PATH%"
    )
)

if not defined VSWHERE_EXE (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    )
)

if not defined VS_REQUIREMENTS (
    set "VS_REQUIREMENTS=-requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
)

if defined VSWHERE_EXE (
    for /f "usebackq delims=" %%I in (`"!VSWHERE_EXE!" -latest -products * !VS_REQUIREMENTS! -property installationPath`) do (
        set "VS_INSTANCE_PATH=%%I"
    )
    if defined VS_INSTANCE_PATH (
        if exist "!VS_INSTANCE_PATH!\Common7\Tools\VsDevCmd.bat" (
            set "VS_PATH=!VS_INSTANCE_PATH!\Common7\Tools\VsDevCmd.bat"
            echo Found Visual Studio instance via vswhere: !VS_INSTANCE_PATH!
            exit /b 0
        )
    )
)

REM 回退到硬编码路径，兼容旧环境
for %%P in (
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\Common7\Tools\VsDevCmd.bat"
) do (
    if exist %%~P (
        set "VS_PATH=%%~P"
        echo Found Visual Studio Build Tools at %%~P
        exit /b 0
    )
)

exit /b 1
