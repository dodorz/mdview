#pragma once

#include <windows.h>

#define PATH_BUFFER_SIZE 4096

typedef struct ViewerCommonContext {
	HWND mainWindow;
	HWND statusBar;
	const WCHAR* appName;
} ViewerCommonContext;

typedef struct ViewerLoadedFile {
	char* contentUtf8;
	char* directoryUtf8;
	WCHAR fullPath[PATH_BUFFER_SIZE];
	WCHAR directory[PATH_BUFFER_SIZE];
	WCHAR filePart[PATH_BUFFER_SIZE];
	DWORD fileSize;
} ViewerLoadedFile;

BOOL ViewerCommon_SaveState(const ViewerCommonContext* ctx);
BOOL ViewerCommon_RestoreState(const ViewerCommonContext* ctx);
void ViewerCommon_ShowLastError(LPCTSTR context);
char* ViewerCommon_ToUtf8(const LPWSTR textUtf16);
LPWSTR ViewerCommon_ToWide(const char* textUtf8);
int ViewerCommon_Scale(HWND hwnd, int value);
BOOL ViewerCommon_LoadUtf8File(const WCHAR* filePath, ViewerLoadedFile* loadedFile);
void ViewerCommon_FreeLoadedFile(ViewerLoadedFile* loadedFile);
size_t ViewerCommon_ComputePreviewLength(const char* utf8Text, size_t textLen, size_t previewLimit, size_t paragraphBacktrackWindow);
BOOL ViewerCommon_GetExeDirectory(WCHAR* buffer, size_t bufferCount);
