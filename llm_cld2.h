#pragma once

#include <windows.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL LlmCld2_DetectLanguage(const char* utf8Text, char* languageCode, size_t languageCodeCount, BOOL* isReliable);

#ifdef __cplusplus
}
#endif
