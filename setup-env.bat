@echo off
REM 快速设置开发环境的脚本
REM 运行此脚本后，当前命令行窗口将配置好 Visual Studio 开发环境

REM 设置代码页为 GBK 以正确显示中文
::chcp 936 >nul 2>&1

REM 查找 Visual Studio 开发命令环境
set "VS_PATH="
call :find_vsdevcmd
if errorlevel 1 (
    echo 错误: 未找到可用的 Visual Studio 开发环境
    echo 请确保已安装 Visual Studio Build Tools 或包含 C++ 工具集的 Visual Studio
    pause
    exit /b 1
)

REM 设置开发环境
echo 正在设置开发环境...
if defined VS_PATH (
    echo 使用实例: %VS_PATH%
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
    for /f "usebackq delims=" %%I in (`"%VSWHERE_EXE%" -latest -products * %VS_REQUIREMENTS% -property installationPath`) do (
        set "VS_INSTANCE_PATH=%%I"
    )
    if defined VS_INSTANCE_PATH (
        if exist "%VS_INSTANCE_PATH%\Common7\Tools\VsDevCmd.bat" (
            set "VS_PATH=%VS_INSTANCE_PATH%\Common7\Tools\VsDevCmd.bat"
            echo 通过 vswhere 找到实例: %VS_INSTANCE_PATH%
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
        echo 使用回退路径: %%~P
        exit /b 0
    )
)

exit /b 1
