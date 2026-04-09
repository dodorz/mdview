#include "viewer_common.h"

#include <pathcch.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>

BOOL
ViewerCommon_SaveState(const ViewerCommonContext* ctx)
{
	HKEY hKey;
	WINDOWPLACEMENT wp;
	TCHAR szSubkey[256];

	if (ctx == NULL || ctx->mainWindow == NULL || ctx->appName == NULL) {
		return FALSE;
	}

	lstrcpy(szSubkey, L"Software\\");
	lstrcat(szSubkey, ctx->appName);
	if (RegCreateKeyEx(HKEY_CURRENT_USER, szSubkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
		ViewerCommon_ShowLastError(L"Creating Registry Key");
		return FALSE;
	}

	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(ctx->mainWindow, &wp);

	if ((RegSetValueExW(hKey, L"flags", 0, REG_BINARY, (PBYTE)&wp.flags, sizeof(wp.flags)) != ERROR_SUCCESS) ||
		(RegSetValueExW(hKey, L"showCmd", 0, REG_BINARY, (PBYTE)&wp.showCmd, sizeof(wp.showCmd)) != ERROR_SUCCESS) ||
		(RegSetValueExW(hKey, L"rcNormalPosition", 0, REG_BINARY, (PBYTE)&wp.rcNormalPosition, sizeof(wp.rcNormalPosition)) != ERROR_SUCCESS))
	{
		RegCloseKey(hKey);
		return FALSE;
	}

	RegCloseKey(hKey);
	return TRUE;
}

BOOL
ViewerCommon_RestoreState(const ViewerCommonContext* ctx)
{
	HKEY hKey;
	DWORD dwSizeFlags;
	DWORD dwSizeShowCmd;
	DWORD dwSizeRcNormal;
	WINDOWPLACEMENT wp;
	TCHAR szSubkey[256];

	if (ctx == NULL || ctx->mainWindow == NULL || ctx->appName == NULL) {
		return FALSE;
	}

	lstrcpy(szSubkey, L"Software\\");
	lstrcat(szSubkey, ctx->appName);
	if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
		return FALSE;
	}

	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(ctx->mainWindow, &wp);
	dwSizeFlags = sizeof(wp.flags);
	dwSizeShowCmd = sizeof(wp.showCmd);
	dwSizeRcNormal = sizeof(wp.rcNormalPosition);
	if ((RegQueryValueExW(hKey, L"flags", NULL, NULL, (PBYTE)&wp.flags, &dwSizeFlags) != ERROR_SUCCESS) ||
		(RegQueryValueExW(hKey, L"showCmd", NULL, NULL, (PBYTE)&wp.showCmd, &dwSizeShowCmd) != ERROR_SUCCESS) ||
		(RegQueryValueExW(hKey, L"rcNormalPosition", NULL, NULL, (PBYTE)&wp.rcNormalPosition, &dwSizeRcNormal) != ERROR_SUCCESS))
	{
		RegCloseKey(hKey);
		return FALSE;
	}
	RegCloseKey(hKey);

	if ((wp.rcNormalPosition.left <= (GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXICON))) &&
		(wp.rcNormalPosition.top <= (GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYICON))))
	{
		SetWindowPlacement(ctx->mainWindow, &wp);
		return TRUE;
	}

	return FALSE;
}

void
ViewerCommon_ShowLastError(LPCTSTR context)
{
	TCHAR buf[255];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buf, sizeof(buf) / sizeof(TCHAR), 0);
	MessageBox(0, buf, context, 0);
}

char*
ViewerCommon_ToUtf8(const LPWSTR textUtf16)
{
	int cbUtf8;
	char* textUtf8;

	if (textUtf16 == NULL || *textUtf16 == L'\0') {
		return NULL;
	}

	cbUtf8 = WideCharToMultiByte(CP_UTF8, 0, textUtf16, -1, NULL, 0, NULL, NULL);
	if (cbUtf8 == 0) {
		ViewerCommon_ShowLastError(L"String conversion failed.");
		return NULL;
	}

	textUtf8 = (char*)malloc(cbUtf8);
	if (textUtf8 == NULL) {
		return NULL;
	}

	if (WideCharToMultiByte(CP_UTF8, 0, textUtf16, -1, textUtf8, cbUtf8, NULL, NULL) == 0) {
		free(textUtf8);
		ViewerCommon_ShowLastError(L"String conversion failed.");
		return NULL;
	}

	return textUtf8;
}

LPWSTR
ViewerCommon_ToWide(const char* textUtf8)
{
	int cchUtf16;
	LPWSTR textUtf16;

	if (textUtf8 == NULL || *textUtf8 == '\0') {
		return NULL;
	}

	cchUtf16 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, textUtf8, -1, NULL, 0);
	if (cchUtf16 == 0) {
		ViewerCommon_ShowLastError(L"String conversion failed.");
		return NULL;
	}

	textUtf16 = (LPWSTR)malloc(cchUtf16 * sizeof(WCHAR));
	if (textUtf16 == NULL) {
		return NULL;
	}

	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, textUtf8, -1, textUtf16, cchUtf16) == 0) {
		free(textUtf16);
		ViewerCommon_ShowLastError(L"String conversion failed.");
		return NULL;
	}

	return textUtf16;
}

int
ViewerCommon_Scale(HWND hwnd, int value)
{
	int dpi = GetDpiForWindow(hwnd);
	return MulDiv(value, dpi, 96);
}

BOOL
ViewerCommon_LoadUtf8File(const WCHAR* filePath, ViewerLoadedFile* loadedFile)
{
	HANDLE hFile;
	HANDLE hMap;
	char* pFileView;
	WCHAR* lpFilePart = NULL;

	if (filePath == NULL || loadedFile == NULL) {
		return FALSE;
	}

	memset(loadedFile, 0, sizeof(*loadedFile));

	hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		ViewerCommon_ShowLastError(L"Cannot open file.");
		return FALSE;
	}

	loadedFile->fileSize = GetFileSize(hFile, NULL);
	if (loadedFile->fileSize == 0) {
		CloseHandle(hFile);
		SetLastError(ERROR_INVALID_DATA);
		ViewerCommon_ShowLastError(L"Invalid file.");
		return FALSE;
	}

	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (hMap == NULL) {
		CloseHandle(hFile);
		ViewerCommon_ShowLastError(L"Cannot open file.");
		return FALSE;
	}

	pFileView = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (pFileView == NULL) {
		CloseHandle(hMap);
		CloseHandle(hFile);
		ViewerCommon_ShowLastError(L"Cannot open file.");
		return FALSE;
	}

	if (GetFullPathNameW(filePath, PATH_BUFFER_SIZE, loadedFile->fullPath, &lpFilePart) == 0) {
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		ViewerCommon_ShowLastError(L"Invalid file.");
		return FALSE;
	}

	lstrcpyW(loadedFile->directory, loadedFile->fullPath);
	if (PathCchRemoveFileSpec(loadedFile->directory, PATH_BUFFER_SIZE) != S_OK) {
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		ViewerCommon_ShowLastError(L"Invalid file.");
		return FALSE;
	}

	if (lpFilePart != NULL) {
		lstrcpyW(loadedFile->filePart, lpFilePart);
	}

	loadedFile->contentUtf8 = (char*)malloc((size_t)loadedFile->fileSize + 2);
	if (loadedFile->contentUtf8 == NULL) {
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		SetLastError(ERROR_OUTOFMEMORY);
		ViewerCommon_ShowLastError(L"Out of memory.");
		return FALSE;
	}

	memcpy(loadedFile->contentUtf8, pFileView, loadedFile->fileSize);
	loadedFile->contentUtf8[loadedFile->fileSize] = '\0';
	loadedFile->contentUtf8[loadedFile->fileSize + 1] = '\0';

	loadedFile->directoryUtf8 = ViewerCommon_ToUtf8(loadedFile->directory);
	if (loadedFile->directoryUtf8 == NULL) {
		ViewerCommon_FreeLoadedFile(loadedFile);
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return FALSE;
	}

	UnmapViewOfFile(pFileView);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return TRUE;
}

void
ViewerCommon_FreeLoadedFile(ViewerLoadedFile* loadedFile)
{
	if (loadedFile == NULL) {
		return;
	}

	if (loadedFile->contentUtf8 != NULL) {
		free(loadedFile->contentUtf8);
	}
	if (loadedFile->directoryUtf8 != NULL) {
		free(loadedFile->directoryUtf8);
	}
	memset(loadedFile, 0, sizeof(*loadedFile));
}

size_t
ViewerCommon_ComputePreviewLength(const char* utf8Text, size_t textLen, size_t previewLimit, size_t paragraphBacktrackWindow)
{
	size_t safeLen;
	size_t nl;
	size_t backtrackLimit;

	if (utf8Text == NULL || textLen == 0) {
		return 0;
	}

	safeLen = (textLen < previewLimit) ? textLen : previewLimit;
	if (safeLen == 0) {
		safeLen = textLen;
	}

	if (safeLen >= textLen) {
		return textLen;
	}

	while (safeLen > 0 && ((utf8Text[safeLen] & 0xC0) == 0x80)) {
		safeLen--;
	}

	backtrackLimit = (safeLen > paragraphBacktrackWindow) ? (safeLen - paragraphBacktrackWindow) : 0;
	nl = safeLen;
	while (nl > backtrackLimit && nl > 0 && utf8Text[nl - 1] != '\n') {
		nl--;
	}
	if (nl > 0 && nl > backtrackLimit) {
		safeLen = nl;
	}

	return safeLen;
}

BOOL
ViewerCommon_GetExeDirectory(WCHAR* buffer, size_t bufferCount)
{
	DWORD len;

	if (buffer == NULL || bufferCount == 0) {
		return FALSE;
	}

	len = GetModuleFileNameW(NULL, buffer, (DWORD)bufferCount);
	if (len == 0 || len >= bufferCount) {
		return FALSE;
	}

	return PathCchRemoveFileSpec(buffer, bufferCount) == S_OK;
}
