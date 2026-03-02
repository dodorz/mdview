# Markdown Viewer
A simple Markdown file renderer for Windows.  
Nothing to install, just run `MarkdownViewer.exe`  
Work in progress, not all Markdown tags implemented yet.  

## Build from source
Requirements:
- Visual Studio Build Tools (or full Visual Studio) with C++ workload
- MSBuild and VC tools (`Microsoft.Component.MSBuild` and `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`)

Build command:
```bat
build.bat Release x64
```

Optional environment variables:
- `VSWHERE_PATH`: custom path to `vswhere.exe`
- `VS_REQUIREMENTS`: custom `vswhere` requirement arguments

If `vswhere.exe` is missing, scripts will automatically fall back to legacy fixed paths for VS 2017/2019/2022 Build Tools.

![](Screenshot.jpg)
