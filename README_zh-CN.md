# mdview

`mdview` 是一个轻量的 Windows Markdown 查看器。

当前仓库同时包含两个桌面程序：

- `mdview.exe`：原始的 Markdown 查看器
- `llmview.exe`：带 LLM 翻译功能的 Markdown 查看器

两者都是基于 Win32 的原生 C/C++ 程序，偏向绿色便携使用方式：编译完成后可直接运行，不依赖安装器。

![截图](Screenshot.jpg)

## 功能概览

### mdview

- 打开并渲染本地 Markdown 文件
- 显示链接并在默认浏览器中打开
- 单文件便携运行
- 原生 Win32 界面，包含工具栏、状态栏和基于 RichEdit 的渲染

### llmview

- 继承 `mdview` 的全部能力
- 可通过菜单或工具栏开关 LLM 翻译
- 优先翻译当前可见区域，再在后台继续翻译剩余内容
- 可将翻译后的 Markdown 保存为新文件
- 支持立即保存部分翻译结果，也支持先选目标路径、待后台完全翻译后自动保存
- 默认保存文件名会在原文件名后附加语言代号，例如 `zh-CN`、`en-US`

## 仓库结构

- [main.c](main.c)：`mdview` 入口
- [llmview_main.c](llmview_main.c)：`llmview` 入口
- [llm_translate.c](llm_translate.c)：LLM 配置加载与 HTTP 翻译请求
- [viewer_common.c](viewer_common.c)：文件加载、窗口状态、字符串转换等公共逻辑
- [MarkDown2RichText.c](MarkDown2RichText.c)：Markdown 转 RTF
- [build.bat](build.bat)：Visual Studio Build Tools / MSBuild 构建脚本

## 编译

环境要求：

- Windows
- 安装了 C++ 工作负载的 Visual Studio Build Tools 或完整 Visual Studio
- 可用的 MSBuild 和 VC 工具链

编译命令：

```bat
build.bat Release x64
```

常见变体：

```bat
build.bat Debug x64
build.bat Release Win32
```

可选环境变量：

- `VSWHERE_PATH`：自定义 `vswhere.exe` 路径
- `VS_REQUIREMENTS`：自定义 `vswhere` 的筛选参数

如果系统里没有 `vswhere.exe`，构建脚本会回退到常见的 Visual Studio 2017/2019/2022 Build Tools 默认路径。

构建输出通常位于：

- `x64\Release\`
- `x64\Debug\`
- `mdview\x64\Release\` 与 `llmview\x64\Release\` 等中间目录

## 运行

编译成功后，可以直接运行：

- `x64\Release\mdview.exe`
- `x64\Release\llmview.exe`

两个程序也支持通过命令行直接传入一个文件路径打开文档。

## llmview 翻译配置

`llmview.exe` 会在自身所在目录查找 `llmview.ini`。

仓库中现在提供了一份示例配置：

- [llmview.ini.example](llmview.ini.example)

至少需要以下配置项：

```ini
[LLM]
provider=
system_prompt=
target_lang=

[Provider.<name>]
base_url=
api_key=
model=
```

其中 `target_lang` 建议使用语言代号格式，例如：

- `zh-CN`
- `en-US`
- `ja-JP`

`system_prompt` 中可以包含 `{target_lang}`，程序在发起请求前会自动将它替换成配置里的语言代号。

## `llmview.ini` 示例

```ini
[LLM]
provider=openai
target_lang=zh-CN
system_prompt=Translate the Markdown into {target_lang}. Preserve headings, lists, links, tables, code fences, and inline code. Do not add commentary.

[Provider.openai]
base_url=https://api.openai.com/v1/chat/completions
api_key=YOUR_API_KEY_HERE
model=gpt-4.1-mini
```

## 翻译结果保存

在 `llmview` 中，可以通过菜单、工具栏或快捷键 `S` 保存翻译结果。

当文档尚未完全翻译时，程序会提示当前大致翻译百分比，并允许你：

- 立即保存当前部分翻译结果
- 取消保存
- 先选好保存路径，等待后台翻译完成后自动保存

## 说明

- 如果程序检测到原文已经是目标语言，会跳过翻译
- 当前翻译请求采用 chat-completions 风格的 HTTP JSON 请求
- 是否兼容某个服务商，取决于其接口是否接受当前程序使用的请求格式
- 项目仍在持续迭代，并非所有 Markdown 渲染细节都已完整实现

## English README

英文说明见：

- [README.md](README.md)
