#include "llm_translate.h"

#include "llm_cld2.h"
#include "viewer_common.h"

#include <pathcch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

typedef struct DynamicBuffer {
	char* data;
	size_t length;
	size_t capacity;
} DynamicBuffer;

static void DynamicBuffer_Free(DynamicBuffer* buffer);
static BOOL DynamicBuffer_Reserve(DynamicBuffer* buffer, size_t extra);
static BOOL DynamicBuffer_AppendN(DynamicBuffer* buffer, const char* text, size_t textLen);
static BOOL DynamicBuffer_Append(DynamicBuffer* buffer, const char* text);
static void TrimAscii(char* text);
static void StripEnclosingQuotes(char* text);
static BOOL ReadIniString(const WCHAR* iniPath, const WCHAR* section, const WCHAR* key, char* buffer, DWORD bufferCount);
static BOOL BuildPrompt(const LlmConfig* config, char* buffer, size_t bufferCount);
static BOOL EscapeJsonString(const char* input, DynamicBuffer* output);
static char* JsonExtractString(const char* json, const char* key);
static char* JsonExtractContentText(const char* json);
static const char* SkipJsonWhitespace(const char* text);
static BOOL HttpPostJson(const char* baseUrl, const char* apiKey, const char* bodyUtf8, char** responseUtf8);
static BOOL DetectTargetLanguage(const LlmConfig* config, const char* utf8Text, BOOL* isTargetLanguage);
static BOOL IsDetectedLanguageTarget(const char* detectedLang, const char* targetLang);

BOOL
LlmConfig_LoadFromExeDir(LlmConfig* config, WCHAR* errorMessage, size_t errorMessageCount)
{
	WCHAR iniPath[PATH_BUFFER_SIZE];
	WCHAR providerSection[128];

	if (config == NULL) {
		return FALSE;
	}

	memset(config, 0, sizeof(*config));

	if (!ViewerCommon_GetExeDirectory(iniPath, PATH_BUFFER_SIZE)) {
		if (errorMessage != NULL && errorMessageCount > 0) {
			_snwprintf_s(errorMessage, errorMessageCount, _TRUNCATE, L"Cannot resolve executable directory.");
		}
		return FALSE;
	}
	if (FAILED(PathCchAppend(iniPath, PATH_BUFFER_SIZE, L"llmview.ini"))) {
		if (errorMessage != NULL && errorMessageCount > 0) {
			_snwprintf_s(errorMessage, errorMessageCount, _TRUNCATE, L"Cannot resolve llmview.ini path.");
		}
		return FALSE;
	}
	if (GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES) {
		if (errorMessage != NULL && errorMessageCount > 0) {
			_snwprintf_s(errorMessage, errorMessageCount, _TRUNCATE, L"llmview.ini not found next to executable.");
		}
		return FALSE;
	}

	if (!ReadIniString(iniPath, L"LLM", L"provider", config->provider, (DWORD)sizeof(config->provider)) ||
		!ReadIniString(iniPath, L"LLM", L"system_prompt", config->systemPrompt, (DWORD)sizeof(config->systemPrompt)) ||
		!ReadIniString(iniPath, L"LLM", L"target_lang", config->targetLang, (DWORD)sizeof(config->targetLang)))
	{
		if (errorMessage != NULL && errorMessageCount > 0) {
			_snwprintf_s(errorMessage, errorMessageCount, _TRUNCATE, L"llmview.ini is missing [LLM] provider/system_prompt/target_lang.");
		}
		return FALSE;
	}

	swprintf_s(providerSection, _countof(providerSection), L"Provider.%S", config->provider);
	if (!ReadIniString(iniPath, providerSection, L"base_url", config->baseUrl, (DWORD)sizeof(config->baseUrl)) ||
		!ReadIniString(iniPath, providerSection, L"api_key", config->apiKey, (DWORD)sizeof(config->apiKey)) ||
		!ReadIniString(iniPath, providerSection, L"model", config->model, (DWORD)sizeof(config->model)))
	{
		if (errorMessage != NULL && errorMessageCount > 0) {
			_snwprintf_s(errorMessage, errorMessageCount, _TRUNCATE, L"llmview.ini is missing provider section [Provider.%S].", config->provider);
		}
		return FALSE;
	}

	config->valid = TRUE;
	return TRUE;
}

BOOL
LlmTranslate_MaybeTranslateMarkdown(const LlmConfig* config, const char* markdownUtf8, char** translatedUtf8, BOOL* skippedByLanguage)
{
	DynamicBuffer requestBody = { 0 };
	char prompt[4096];
	char* responseJson = NULL;
	char* content = NULL;
	BOOL isTargetLanguage = FALSE;
	BOOL result = FALSE;

	if (translatedUtf8 != NULL) {
		*translatedUtf8 = NULL;
	}
	if (skippedByLanguage != NULL) {
		*skippedByLanguage = FALSE;
	}
	if (config == NULL || !config->valid || markdownUtf8 == NULL || translatedUtf8 == NULL) {
		return FALSE;
	}

	if (DetectTargetLanguage(config, markdownUtf8, &isTargetLanguage)) {
		if (isTargetLanguage) {
			*translatedUtf8 = _strdup(markdownUtf8);
			if (skippedByLanguage != NULL) {
				*skippedByLanguage = TRUE;
			}
			return *translatedUtf8 != NULL;
		}
	}

	if (!BuildPrompt(config, prompt, sizeof(prompt))) {
		return FALSE;
	}

	if (!DynamicBuffer_Append(&requestBody, "{")) goto cleanup;
	if (!DynamicBuffer_Append(&requestBody, "\"model\":\"")) goto cleanup;
	if (!EscapeJsonString(config->model, &requestBody)) goto cleanup;
	if (!DynamicBuffer_Append(&requestBody, "\",\"messages\":[{\"role\":\"system\",\"content\":\"")) goto cleanup;
	if (!EscapeJsonString(prompt, &requestBody)) goto cleanup;
	if (!DynamicBuffer_Append(&requestBody, "\"},{\"role\":\"user\",\"content\":\"")) goto cleanup;
	if (!EscapeJsonString("Translate the Markdown below. If the input contains marker lines like <<<P00001>>> or <<<P00001_S001>>>, copy every marker line exactly, keep their order unchanged, and translate only the text between them. Do not add commentary. Do not wrap the answer in code fences.\n\n", &requestBody)) goto cleanup;
	if (!EscapeJsonString(markdownUtf8, &requestBody)) goto cleanup;
	if (!DynamicBuffer_Append(&requestBody, "\"}],\"temperature\":0.2}")) goto cleanup;

	if (!HttpPostJson(config->baseUrl, config->apiKey, requestBody.data, &responseJson)) goto cleanup;

	content = JsonExtractContentText(responseJson);
	if (content == NULL || *content == '\0') goto cleanup;

	*translatedUtf8 = content;
	content = NULL;
	result = TRUE;

cleanup:
	if (content != NULL) {
		free(content);
	}
	if (responseJson != NULL) {
		free(responseJson);
	}
	DynamicBuffer_Free(&requestBody);
	return result;
}

static BOOL
ReadIniString(const WCHAR* iniPath, const WCHAR* section, const WCHAR* key, char* buffer, DWORD bufferCount)
{
	WCHAR valueW[2048];
	DWORD charsRead;
	char* valueUtf8;

	if (buffer == NULL || bufferCount == 0) {
		return FALSE;
	}
	buffer[0] = '\0';

	charsRead = GetPrivateProfileStringW(section, key, L"", valueW, _countof(valueW), iniPath);
	if (charsRead == 0) {
		return FALSE;
	}

	valueUtf8 = ViewerCommon_ToUtf8(valueW);
	if (valueUtf8 == NULL) {
		return FALSE;
	}

	strncpy_s(buffer, bufferCount, valueUtf8, _TRUNCATE);
	free(valueUtf8);
	TrimAscii(buffer);
	StripEnclosingQuotes(buffer);
	return buffer[0] != '\0';
}

static void
TrimAscii(char* text)
{
	size_t len;
	size_t start = 0;

	if (text == NULL) {
		return;
	}

	len = strlen(text);
	while (start < len && (unsigned char)text[start] <= 0x20) {
		start++;
	}
	while (len > start && (unsigned char)text[len - 1] <= 0x20) {
		len--;
	}
	if (start > 0) {
		memmove(text, text + start, len - start);
	}
	text[len - start] = '\0';
}

static void
StripEnclosingQuotes(char* text)
{
	size_t len;

	if (text == NULL) {
		return;
	}
	len = strlen(text);
	if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
		memmove(text, text + 1, len - 2);
		text[len - 2] = '\0';
	}
}

static BOOL
BuildPrompt(const LlmConfig* config, char* buffer, size_t bufferCount)
{
	const char* placeholder = "{target_lang}";
	const char* found;
	size_t prefixLen;

	if (config == NULL || buffer == NULL || bufferCount == 0) {
		return FALSE;
	}

	found = strstr(config->systemPrompt, placeholder);
	if (found == NULL) {
		return strncpy_s(buffer, bufferCount, config->systemPrompt, _TRUNCATE) == 0;
	}

	prefixLen = (size_t)(found - config->systemPrompt);
	if (prefixLen + strlen(config->targetLang) + strlen(found + strlen(placeholder)) + 1 > bufferCount) {
		return FALSE;
	}

	memcpy(buffer, config->systemPrompt, prefixLen);
	buffer[prefixLen] = '\0';
	strcat_s(buffer, bufferCount, config->targetLang);
	strcat_s(buffer, bufferCount, found + strlen(placeholder));
	return TRUE;
}

static BOOL
DynamicBuffer_Reserve(DynamicBuffer* buffer, size_t extra)
{
	size_t required;
	size_t newCapacity;
	char* newData;

	required = buffer->length + extra + 1;
	if (required <= buffer->capacity) {
		return TRUE;
	}

	newCapacity = (buffer->capacity == 0) ? 1024 : buffer->capacity;
	while (newCapacity < required) {
		newCapacity *= 2;
	}

	newData = (char*)realloc(buffer->data, newCapacity);
	if (newData == NULL) {
		return FALSE;
	}

	buffer->data = newData;
	buffer->capacity = newCapacity;
	return TRUE;
}

static BOOL
DynamicBuffer_AppendN(DynamicBuffer* buffer, const char* text, size_t textLen)
{
	if (!DynamicBuffer_Reserve(buffer, textLen)) {
		return FALSE;
	}
	memcpy(buffer->data + buffer->length, text, textLen);
	buffer->length += textLen;
	buffer->data[buffer->length] = '\0';
	return TRUE;
}

static BOOL
DynamicBuffer_Append(DynamicBuffer* buffer, const char* text)
{
	return DynamicBuffer_AppendN(buffer, text, strlen(text));
}

static void
DynamicBuffer_Free(DynamicBuffer* buffer)
{
	if (buffer != NULL && buffer->data != NULL) {
		free(buffer->data);
		buffer->data = NULL;
		buffer->length = 0;
		buffer->capacity = 0;
	}
}

static BOOL
EscapeJsonString(const char* input, DynamicBuffer* output)
{
	const unsigned char* p = (const unsigned char*)input;
	char escape[8];

	while (*p) {
		switch (*p) {
		case '\\':
			if (!DynamicBuffer_Append(output, "\\\\")) return FALSE;
			break;
		case '"':
			if (!DynamicBuffer_Append(output, "\\\"")) return FALSE;
			break;
		case '\r':
			if (!DynamicBuffer_Append(output, "\\r")) return FALSE;
			break;
		case '\n':
			if (!DynamicBuffer_Append(output, "\\n")) return FALSE;
			break;
		case '\t':
			if (!DynamicBuffer_Append(output, "\\t")) return FALSE;
			break;
		default:
			if (*p < 0x20) {
				sprintf_s(escape, sizeof(escape), "\\u%04x", *p);
				if (!DynamicBuffer_Append(output, escape)) return FALSE;
			}
			else {
				if (!DynamicBuffer_AppendN(output, (const char*)p, 1)) return FALSE;
			}
		}
		p++;
	}
	return TRUE;
}

static char*
JsonExtractString(const char* json, const char* key)
{
	const char* found;
	const char* quote;
	DynamicBuffer output = { 0 };

	found = strstr(json, key);
	if (found == NULL) {
		return NULL;
	}

	found += strlen(key);
	while (*found && *found != ':') {
		found++;
	}
	if (*found != ':') {
		return NULL;
	}
	found++;
	while (*found == ' ' || *found == '\r' || *found == '\n' || *found == '\t') {
		found++;
	}
	if (*found != '"') {
		return NULL;
	}
	found++;
	for (quote = found; *quote; quote++) {
		char ch = *quote;
		if (ch == '\\') {
			quote++;
			if (*quote == '\0') {
				break;
			}
			switch (*quote) {
			case '\\': ch = '\\'; break;
			case '"': ch = '"'; break;
			case '/': ch = '/'; break;
			case 'b': ch = '\b'; break;
			case 'f': ch = '\f'; break;
			case 'n': ch = '\n'; break;
			case 'r': ch = '\r'; break;
			case 't': ch = '\t'; break;
			default:
				ch = *quote;
				break;
			}
			if (!DynamicBuffer_AppendN(&output, &ch, 1)) {
				DynamicBuffer_Free(&output);
				return NULL;
			}
			continue;
		}
		if (ch == '"') {
			return output.data;
		}
		if (!DynamicBuffer_AppendN(&output, &ch, 1)) {
			DynamicBuffer_Free(&output);
			return NULL;
		}
	}

	DynamicBuffer_Free(&output);
	return NULL;
}

static const char*
SkipJsonWhitespace(const char* text)
{
	while (text != NULL && (*text == ' ' || *text == '\r' || *text == '\n' || *text == '\t')) {
		text++;
	}
	return text;
}

static char*
JsonExtractContentText(const char* json)
{
	const char* found;
	const char* value;
	char* text;

	if (json == NULL) {
		return NULL;
	}

	text = JsonExtractString(json, "\"output_text\"");
	if (text != NULL && *text != '\0') {
		return text;
	}
	if (text != NULL) {
		free(text);
	}

	text = JsonExtractString(json, "\"content\"");
	if (text != NULL && *text != '\0') {
		return text;
	}
	if (text != NULL) {
		free(text);
	}

	found = strstr(json, "\"content\"");
	while (found != NULL) {
		value = found + strlen("\"content\"");
		while (*value && *value != ':') {
			value++;
		}
		if (*value != ':') {
			break;
		}
		value = SkipJsonWhitespace(value + 1);
		if (*value == '[') {
			text = JsonExtractString(value, "\"text\"");
			if (text != NULL && *text != '\0') {
				return text;
			}
			if (text != NULL) {
				free(text);
			}
		}
		found = strstr(found + strlen("\"content\""), "\"content\"");
	}

	return JsonExtractString(json, "\"text\"");
}

static BOOL
HttpPostJson(const char* baseUrl, const char* apiKey, const char* bodyUtf8, char** responseUtf8)
{
	URL_COMPONENTSW components;
	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	LPWSTR baseUrlW = NULL;
	WCHAR hostName[256];
	WCHAR path[1024];
	WCHAR headers[512];
	DWORD flags = 0;
	BOOL result = FALSE;
	DynamicBuffer response = { 0 };

	ZeroMemory(&components, sizeof(components));
	components.dwStructSize = sizeof(components);
	components.lpszHostName = hostName;
	components.dwHostNameLength = _countof(hostName);
	components.lpszUrlPath = path;
	components.dwUrlPathLength = _countof(path);
	baseUrlW = ViewerCommon_ToWide(baseUrl);
	if (baseUrlW == NULL) {
		return FALSE;
	}

	if (!WinHttpCrackUrl(baseUrlW, 0, 0, &components)) {
		goto cleanup;
	}
	if (components.nScheme == INTERNET_SCHEME_HTTPS) {
		flags |= WINHTTP_FLAG_SECURE;
	}

	hSession = WinHttpOpen(L"llmview/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL) goto cleanup;

	hConnect = WinHttpConnect(hSession, (LPCWSTR)hostName, components.nPort, 0);
	if (hConnect == NULL) goto cleanup;

	if (wcslen(path) == 0) {
		wcscpy_s(path, _countof(path), L"/chat/completions");
	}
	else if (wcsstr(path, L"/chat/completions") == NULL) {
		wcscat_s(path, _countof(path), L"/chat/completions");
	}

	hRequest = WinHttpOpenRequest(hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (hRequest == NULL) goto cleanup;

	swprintf_s(headers, _countof(headers), L"Content-Type: application/json\r\nAuthorization: Bearer %S\r\n", apiKey);
	if (!WinHttpSendRequest(hRequest, headers, (DWORD)-1L, (LPVOID)bodyUtf8, (DWORD)strlen(bodyUtf8), (DWORD)strlen(bodyUtf8), 0)) goto cleanup;
	if (!WinHttpReceiveResponse(hRequest, NULL)) goto cleanup;

	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &available)) goto cleanup;
		if (available == 0) break;
		if (!DynamicBuffer_Reserve(&response, available)) goto cleanup;
		if (!WinHttpReadData(hRequest, response.data + response.length, available, &available)) goto cleanup;
		response.length += available;
		response.data[response.length] = '\0';
	}

	*responseUtf8 = response.data;
	response.data = NULL;
	result = TRUE;

cleanup:
	DynamicBuffer_Free(&response);
	if (baseUrlW != NULL) free(baseUrlW);
	if (hRequest != NULL) WinHttpCloseHandle(hRequest);
	if (hConnect != NULL) WinHttpCloseHandle(hConnect);
	if (hSession != NULL) WinHttpCloseHandle(hSession);
	return result;
}

static BOOL
DetectTargetLanguage(const LlmConfig* config, const char* utf8Text, BOOL* isTargetLanguage)
{
	char probe[1001];
	size_t probeLen;
	char detected[32];
	BOOL reliable = FALSE;

	if (isTargetLanguage != NULL) {
		*isTargetLanguage = FALSE;
	}
	if (config == NULL || utf8Text == NULL || isTargetLanguage == NULL) {
		return FALSE;
	}

	probeLen = strlen(utf8Text);
	if (probeLen > 1000) {
		probeLen = 1000;
		while (probeLen > 0 && ((utf8Text[probeLen] & 0xC0) == 0x80)) {
			probeLen--;
		}
	}
	memcpy(probe, utf8Text, probeLen);
	probe[probeLen] = '\0';

	if (LlmCld2_DetectLanguage(probe, detected, sizeof(detected), &reliable)) {
		TrimAscii(detected);
		if (*detected != '\0') {
			*isTargetLanguage = IsDetectedLanguageTarget(detected, config->targetLang);
		}
		return TRUE;
	}
	return FALSE;
}

static BOOL
IsDetectedLanguageTarget(const char* detectedLang, const char* targetLang)
{
	if (_stricmp(targetLang, "CN") == 0 || _stricmp(targetLang, "ZH") == 0 || _stricmp(targetLang, "ZH-CN") == 0) {
		return _strnicmp(detectedLang, "zh", 2) == 0;
	}
	if (_stricmp(targetLang, "EN") == 0 || _stricmp(targetLang, "EN-US") == 0 || _stricmp(targetLang, "EN-GB") == 0) {
		return _strnicmp(detectedLang, "en", 2) == 0;
	}
	return _stricmp(detectedLang, targetLang) == 0;
}
