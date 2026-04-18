# mdview

`mdview` is a lightweight Markdown viewer for Windows.

This repository currently contains two desktop executables:

- `mdview.exe`: the original Markdown viewer
- `llmview.exe`: the Markdown viewer with LLM-assisted translation

Both applications are native Win32 programs written in C/C++ and are intended to be portable: build them once, then run the executables directly without an installer.

![Screenshot](/C:/~/Projects/mdview/Screenshot.jpg)

## Features

### mdview

- Open and render local Markdown files
- Display links and open them in the default browser
- Portable single-exe workflow
- Native Win32 UI with toolbar, status bar, and RichEdit-based rendering

### llmview

- Everything from `mdview`
- Toggle LLM translation on and off from the menu or toolbar
- Translate the visible part of a document first, then continue translating in the background
- Save translated Markdown to a new file
- Save partially translated content immediately, or choose a destination first and auto-save after translation finishes
- Default translated filenames use the original filename plus a language tag such as `zh-CN` or `en-US`

## Repository Layout

- [main.c](/C:/~/Projects/mdview/main.c): entry point for `mdview`
- [llmview_main.c](/C:/~/Projects/mdview/llmview_main.c): entry point for `llmview`
- [llm_translate.c](/C:/~/Projects/mdview/llm_translate.c): LLM configuration loading and HTTP translation client
- [viewer_common.c](/C:/~/Projects/mdview/viewer_common.c): shared helpers for file loading, window state, and string conversion
- [MarkDown2RichText.c](/C:/~/Projects/mdview/MarkDown2RichText.c): Markdown-to-RTF conversion
- [build.bat](/C:/~/Projects/mdview/build.bat): build helper for Visual Studio Build Tools / MSBuild

## Build

Requirements:

- Windows
- Visual Studio Build Tools or full Visual Studio with C++ workload
- MSBuild and VC tools

Build the solution:

```bat
build.bat Release x64
```

Common variants:

```bat
build.bat Debug x64
build.bat Release Win32
```

Optional environment variables:

- `VSWHERE_PATH`: custom path to `vswhere.exe`
- `VS_REQUIREMENTS`: custom `vswhere` requirement arguments

If `vswhere.exe` is not available, the build script falls back to common Visual Studio 2017/2019/2022 Build Tools paths.

Build outputs are generated under:

- `x64\Release\`
- `x64\Debug\`
- `mdview\x64\Release\` and `llmview\x64\Release\` intermediate folders

## Running

After a successful build, launch one of these executables:

- `x64\Release\mdview.exe`
- `x64\Release\llmview.exe`

Both programs can also open a file passed on the command line.

## LLM Translation Configuration

`llmview.exe` looks for `llmview.ini` next to the executable.

There is now an example config in:

- [llmview.ini.example](/C:/~/Projects/mdview/llmview.ini.example)

Minimum required sections and keys:

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

`target_lang` should use a language tag such as:

- `zh-CN`
- `en-US`
- `ja-JP`

The `system_prompt` may include `{target_lang}`, which will be replaced with the configured language tag before each request.

## Example `llmview.ini`

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

## Saving Translated Files

In `llmview` you can save translation results from the menu, toolbar, or the `S` shortcut.

When translation is incomplete, the app shows the approximate translated percentage and lets you:

- save the current partial result now
- cancel
- choose a path now and save automatically after the background translation completes

## Notes

- `llmview` skips translation when the source text is already detected as the target language
- Translation requests are currently sent as chat-completions style JSON over HTTP
- Provider compatibility depends on whether the configured endpoint accepts the request format used by the app
- This project is still evolving, and not all Markdown rendering details are fully implemented yet

## Chinese Documentation

Chinese documentation is available in:

- [README_zh-CN.md](/C:/~/Projects/mdview/README_zh-CN.md)
