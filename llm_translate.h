#pragma once

#include <windows.h>

typedef struct LlmConfig {
	char provider[64];
	char baseUrl[512];
	char apiKey[256];
	char model[128];
	char systemPrompt[2048];
	char targetLang[64];
	BOOL valid;
} LlmConfig;

BOOL LlmConfig_LoadFromExeDir(LlmConfig* config, WCHAR* errorMessage, size_t errorMessageCount);
BOOL LlmTranslate_MaybeTranslateMarkdown(const LlmConfig* config, const char* markdownUtf8, char** translatedUtf8, BOOL* skippedByLanguage);
