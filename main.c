// mdview - 2022 by Thomas Fuhringer
// 2026 by dodo

#pragma once
//#include <SDKDDKVer.h>
#include <windows.h>
#include <Commctrl.h>
#include <RichEdit.h>
#include <Shellapi.h>
#include <shlwapi.h>
#include <ShellScalingApi.h>
#include <objidl.h>
#include <ole2.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <initguid.h>
#include <wincodec.h>
#include <winhttp.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winhttp.lib")

#include "Resource.h"
#include "viewer_common.h"

#define MAX_LOADSTRING  100
#define MARGIN  20
#define PREVIEW_BYTES (96 * 1024)
#define WM_APP_RENDER_COMPLETE (WM_APP + 1)
#define WM_APP_OPEN_FILE (WM_APP + 2)

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"") // Enables visual styles.

char* markdown2rtf(const char* md, const char* img_path);
char* markdown2rtf_ex(const char* md, const char* img_path, int enable_images);
void markdown_clear_pending_images(void);
int markdown_get_pending_image_count(void);
const char* markdown_get_pending_image_marker(int index);
const char* markdown_get_pending_image_path(int index);

// Global Variables:
HINSTANCE hInst;								 // current instance
HICON hIcon;									 // App icon
TCHAR szAppName[MAX_LOADSTRING];
TCHAR szWindowClass[] = L"MainWindowClass";

HWND hMainWindow = NULL;
HMENU hMainMenu = NULL;
HWND hToolBar;
HWND hRichEdit;
HWND hStatusBar;
WCHAR szCurrentFile[MAX_PATH] = L"";

DWORD dwFilesize;
char* pFileView;
HANDLE hLoadThread = NULL;
volatile LONG g_loadGeneration = 0;

typedef struct {
	char* mdFull;
	char* imgPath;
	DWORD generation;
} RenderTask;

typedef struct {
	char* markerUtf8;
	char* pathUtf8;
} PendingImageRef;

typedef struct {
	char* rtf;
	PendingImageRef* images;
	int imageCount;
} RenderedDocument;

typedef struct {
	const char* data;
	size_t length;
	size_t offset;
} RtfStreamContext;

typedef struct {
	LONG cpMin;
	LONG cpMax;
	BOOL valid;
} VisibleCharRange;

static const DWORD ESC_DOUBLE_PRESS_INTERVAL_MS = 600;
static ULONGLONG g_lastEscPressTimestamp = 0;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
BOOL FileOpen(WCHAR* lpszTextFileName);
void FileOpenDialog();
BOOL CreateToolBar();
BOOL CreateStatusBar();
DWORD WINAPI BackgroundRenderThread(LPVOID lpParam);
static DWORD CALLBACK RichEditStreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* pcb);
static BOOL SetRichEditRtf(HWND hwnd, const char* rtfText);
static RenderedDocument* RenderMarkdownDocument(const char* markdown, const char* imgPath, int enableImages);
static void FreeRenderedDocument(RenderedDocument* doc);
static BOOL InsertPendingImages(HWND hwnd, const RenderedDocument* doc);
static void AppendDebugLog(const char* format, ...);
static BOOL IsHttpUrl(const char* value);
static VisibleCharRange GetVisibleCharRange(HWND hwnd);

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
	OleInitialize(NULL);

	WNDCLASSEX wc;
	MSG msg;
	HACCEL hAccelTable;

	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_COOL_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	hInst = hInstance;
	LoadString(hInstance, IDS_APP, szAppName, MAX_LOADSTRING);
	hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
	hMainMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU_BAR));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = hIcon;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szWindowClass;
	wc.hIconSm = hIcon;

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, L"Window Registration Failed!", szAppName, MB_ICONERROR | MB_OK);
		return 0;
	}

	hMainWindow = CreateWindow(szWindowClass, szAppName, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, hMainMenu, hInstance, NULL);

	if (hMainWindow == NULL)
	{
		MessageBox(NULL, L"Window Creation Failed!", szAppName, MB_ICONERROR | MB_OK);
		//UtlShowError(); 
		return 0;
	}
	DragAcceptFiles(hMainWindow, TRUE);

	CreateToolBar();
	CreateStatusBar();

	ViewerCommonContext commonCtx = { hMainWindow, hStatusBar, szAppName };
	if (!ViewerCommon_RestoreState(&commonCtx))
		ShowWindow(hMainWindow, nCmdShow);

	UpdateWindow(hMainWindow);

	LPWSTR* szArglist;
	int nArgs;
	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (NULL != szArglist && nArgs == 2) {
		size_t pathChars = wcslen(szArglist[1]) + 1;
		WCHAR* pendingPath = (WCHAR*)malloc(pathChars * sizeof(WCHAR));
		if (pendingPath != NULL) {
			wcscpy_s(pendingPath, pathChars, szArglist[1]);
			PostMessage(hMainWindow, WM_APP_OPEN_FILE, 0, (LPARAM)pendingPath);
		}
	}
	if (szArglist != NULL) {
		LocalFree(szArglist);
	}

	// Load accelerator table - IDR_ACCELERATOR must be defined in resource.h
	// and accelerator table must be defined in Resource.rc
	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));

	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			ULONGLONG now = GetTickCount64();
			if (g_lastEscPressTimestamp != 0 && (now - g_lastEscPressTimestamp) <= ESC_DOUBLE_PRESS_INTERVAL_MS)
			{
				g_lastEscPressTimestamp = 0;
				PostMessage(hMainWindow, WM_CLOSE, 0, 0);
				continue;
			}
			g_lastEscPressTimestamp = now;
		}

		if (!TranslateAccelerator(hMainWindow, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	OleUninitialize();
	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int				iClientAreaTop;
	int				iClientAreaHeight;
	int             iClientAreaWidth;

	switch (msg)
	{
	case WM_CREATE:
		DrawMenuBar(hMainWindow);
		HMODULE mftedit = LoadLibraryA("Msftedit.dll");
		if (!mftedit)
		{
			ViewerCommon_ShowLastError(L"Msftedit.dll Load Failed!");
			return 0;
		}
		hRichEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
			ES_MULTILINE | WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_READONLY,
			10, 100, 200, 200,
			hwnd, (HMENU)NULL, GetModuleHandle(NULL), NULL
		);

		if (hRichEdit == NULL)
		{
			ViewerCommon_ShowLastError(L"RichEdit Creation Failed!");
			return 0;
		}
		SendMessage(hRichEdit, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessage(hRichEdit, EM_SETEDITSTYLEEX, 0, SES_EX_HANDLEFRIENDLYURL | SES_HYPERLINKTOOLTIPS);
		// Hide caret for read-only viewer
		HideCaret(hRichEdit);
		break;
	case WM_SETCURSOR:
		if ((HWND)wParam == hRichEdit)
		{
			// Check if user is selecting text (left button is down)
			if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
			{
				SetCursor(LoadCursor(NULL, IDC_IBEAM));
			}
			else
			{
				// Use arrow cursor by default
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
			return TRUE;
		}
		break;
	case WM_LBUTTONDOWN:
		// Hide caret when clicking on text area
		HideCaret(hRichEdit);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_SPACE:
		case VK_NEXT: // Page Down
			SendMessage(hRichEdit, WM_VSCROLL, SB_PAGEDOWN, 0);
			break;
		case VK_PRIOR: // Page Up
			SendMessage(hRichEdit, WM_VSCROLL, SB_PAGEUP, 0);
			break;
		case VK_UP:
			SendMessage(hRichEdit, WM_VSCROLL, SB_LINEUP, 0);
			break;
		case VK_DOWN:
			SendMessage(hRichEdit, WM_VSCROLL, SB_LINEDOWN, 0);
			break;
		case VK_HOME:
			SendMessage(hRichEdit, WM_VSCROLL, SB_TOP, 0);
			break;
		case VK_END:
			SendMessage(hRichEdit, WM_VSCROLL, SB_BOTTOM, 0);
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDM_FILE_EXIT:
			PostQuitMessage(0);
			break;

		case IDM_FILE_OPEN:
			FileOpenDialog();
			break;

		case IDM_FILE_REFRESH:
			if (szCurrentFile[0] != L'\0')
				FileOpen(szCurrentFile);
			break;

		case IDM_HELP_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, About);
			break;
		}
		break;

	case WM_APP_OPEN_FILE:
		if ((WCHAR*)lParam != NULL) {
			FileOpen((WCHAR*)lParam);
			free((WCHAR*)lParam);
		}
		break;

	case WM_MENUSELECT:
		switch (LOWORD(wParam))
		{
		case IDM_FILE_OPEN:
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Open a new file");
			break;
		default:
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"");
		}
		break;

	case WM_SIZE:
		iClientAreaWidth = LOWORD(lParam);
		iClientAreaHeight = HIWORD(lParam);

		int iPart[1] = { -1 };
		SendMessage(hStatusBar, SB_SETPARTS, (WPARAM)1, (LPARAM)iPart);
		SendMessage(hToolBar, TB_AUTOSIZE, 0, 0);
		SendMessage(hStatusBar, WM_SIZE, 0, 0);

		RECT rect;
		GetWindowRect(hToolBar, &rect);
		iClientAreaTop = rect.bottom - rect.top;
		iClientAreaHeight -= iClientAreaTop;
		GetClientRect(hStatusBar, &rect);
		iClientAreaHeight -= rect.bottom;
		MoveWindow(hRichEdit, 0, iClientAreaTop, iClientAreaWidth, iClientAreaHeight, TRUE);
		GetClientRect(hRichEdit, &rect);
		rect.left += MARGIN;
		rect.top += MARGIN;
		rect.right -= MARGIN;
		SendMessage(hRichEdit, EM_SETRECT, 0, (LPARAM)&rect);
		break;

	case WM_DROPFILES: {
		HDROP hDrop = (HDROP)wParam;
		TCHAR szNextFile[MAX_PATH];
		if (DragQueryFile(hDrop, 0, szNextFile, MAX_PATH) > 0) {
			FileOpen(szNextFile);
		}
		DragFinish(hDrop);
		break; }

	case WM_NOTIFY: {
		switch (((LPNMHDR)lParam)->code) {
		case EN_LINK: {
			ENLINK* enLinkInfo = (ENLINK*)lParam;
			TEXTRANGE tr;
			if (enLinkInfo->msg == WM_LBUTTONUP) {
				//LONG len_link = enLinkInfo->chrg.cpMax - enLinkInfo->chrg.cpMin;
				TCHAR szLink[1024];
				tr.chrg = enLinkInfo->chrg;
				tr.lpstrText = szLink;

				SendMessage(hRichEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				ShellExecuteW(NULL, L"open", szLink, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		}
		break; }

	case WM_APP_RENDER_COMPLETE: {
		DWORD generation = (DWORD)lParam;
		RenderedDocument* rendered = (RenderedDocument*)wParam;
		if (rendered != NULL) {
			if (generation == (DWORD)g_loadGeneration) {
				SetRichEditRtf(hRichEdit, rendered->rtf);
				InsertPendingImages(hRichEdit, rendered);
				SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Full document loaded.");
			}
			FreeRenderedDocument(rendered);
		}
		if (hLoadThread != NULL) {
			CloseHandle(hLoadThread);
			hLoadThread = NULL;
		}
		return 0;
	}

	case WM_QUERYENDSESSION:
	case WM_CLOSE:
		{
			ViewerCommonContext commonCtx = { hMainWindow, hStatusBar, szAppName };
			ViewerCommon_SaveState(&commonCtx);
		}
		DestroyWindow(hwnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}


BOOL
FileOpen(WCHAR* lpszTextFileName)
{
	DWORD currentGeneration = (DWORD)InterlockedIncrement(&g_loadGeneration);
	if (hLoadThread != NULL) {
		// We no longer need to wait; close our handle so we don't leak.
		CloseHandle(hLoadThread);
		hLoadThread = NULL;
	}

	ViewerLoadedFile loadedFile;
	char* mdFull = NULL;
	char* path = NULL;
	RenderedDocument* previewDoc = NULL;
	BOOL result = FALSE;

	if (lpszTextFileName == NULL)
	{
		szCurrentFile[0] = L'\0';
		SetRichEditRtf(hRichEdit, "{\\rtf1\\ansi }");
		SendMessageW(hMainWindow, WM_SETTEXT, (WPARAM)0, (LPARAM)szAppName);
		return 0;
	}

	if (!ViewerCommon_LoadUtf8File(lpszTextFileName, &loadedFile)) {
		return 0;
	}
	lstrcpyW(szCurrentFile, loadedFile.fullPath);
	dwFilesize = loadedFile.fileSize;
	mdFull = loadedFile.contentUtf8;
	path = loadedFile.directoryUtf8;
	loadedFile.contentUtf8 = NULL;
	loadedFile.directoryUtf8 = NULL;

	TCHAR szTitle[MAX_PATH + 20];
	szTitle[0] = 0;
	StrCatW(szTitle, loadedFile.filePart);
	StrCatW(szTitle, L" - ");
	StrCatW(szTitle, szAppName);
	SendMessageW(hMainWindow, WM_SETTEXT, (WPARAM)0, (LPARAM)szTitle);

	// Build a quick preview so the UI becomes responsive immediately.
	size_t safeLen = ViewerCommon_ComputePreviewLength(mdFull, (size_t)dwFilesize, PREVIEW_BYTES, 2048);

	char savedChar = mdFull[safeLen];
	mdFull[safeLen] = '\0';
	previewDoc = RenderMarkdownDocument(mdFull, path, 0);
	mdFull[safeLen] = savedChar;

	if (previewDoc != NULL && previewDoc->rtf != NULL) {
		SetRichEditRtf(hRichEdit, previewDoc->rtf);
	}
	else {
		SetRichEditRtf(hRichEdit, "{\\rtf1\\ansi }");
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Could not convert Markdown.");
	}

	if (dwFilesize > safeLen) {
		RenderTask* task = (RenderTask*)malloc(sizeof(RenderTask));
		if (task != NULL) {
			task->mdFull = mdFull;
			task->imgPath = path;
			task->generation = currentGeneration;
			hLoadThread = CreateThread(NULL, 0, BackgroundRenderThread, task, 0, NULL);
			if (hLoadThread == NULL) {
				// Fall back to synchronous full render if thread creation failed.
				RenderedDocument* fullDoc = RenderMarkdownDocument(mdFull, path, 1);
				if (fullDoc != NULL && fullDoc->rtf != NULL) {
					SetRichEditRtf(hRichEdit, fullDoc->rtf);
					InsertPendingImages(hRichEdit, fullDoc);
					SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
				}
				FreeRenderedDocument(fullDoc);
				free(mdFull);
				free(path);
			}
			else {
				SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Preview loaded, rendering full document...");
				// Ownership moves to background thread.
				mdFull = NULL;
				path = NULL;
			}
		}
		else {
			RenderedDocument* fullDoc = RenderMarkdownDocument(mdFull, path, 1);
			if (fullDoc != NULL && fullDoc->rtf != NULL) {
				SetRichEditRtf(hRichEdit, fullDoc->rtf);
				InsertPendingImages(hRichEdit, fullDoc);
				SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
			}
			FreeRenderedDocument(fullDoc);
			free(mdFull);
			free(path);
		}
	}
	else {
		RenderedDocument* fullDoc = RenderMarkdownDocument(mdFull, path, 1);
		if (fullDoc != NULL && fullDoc->rtf != NULL) {
			SetRichEditRtf(hRichEdit, fullDoc->rtf);
			InsertPendingImages(hRichEdit, fullDoc);
		}
		FreeRenderedDocument(fullDoc);
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
		free(mdFull);
		free(path);
	}

	FreeRenderedDocument(previewDoc);

	ViewerCommon_FreeLoadedFile(&loadedFile);
	result = TRUE;
	return result;
}

DWORD WINAPI
BackgroundRenderThread(LPVOID lpParam)
{
	RenderTask* task = (RenderTask*)lpParam;
	if (task == NULL)
		return 0;

	RenderedDocument* rendered = RenderMarkdownDocument(task->mdFull, task->imgPath, 1);

	free(task->mdFull);
	free(task->imgPath);

	DWORD generation = task->generation;
	free(task);

	if (rendered != NULL) {
		PostMessage(hMainWindow, WM_APP_RENDER_COMPLETE, (WPARAM)rendered, (LPARAM)generation);
	}

	return 0;
}

static DWORD CALLBACK
RichEditStreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* pcb)
{
	RtfStreamContext* ctx = (RtfStreamContext*)cookie;
	size_t remaining;
	size_t chunk;

	if (pcb == NULL || ctx == NULL || buffer == NULL) {
		return 1;
	}

	if (ctx->offset >= ctx->length) {
		*pcb = 0;
		return 0;
	}

	remaining = ctx->length - ctx->offset;
	chunk = remaining < (size_t)cb ? remaining : (size_t)cb;
	memcpy(buffer, ctx->data + ctx->offset, chunk);
	ctx->offset += chunk;
	*pcb = (LONG)chunk;
	return 0;
}

static BOOL
SetRichEditRtf(HWND hwnd, const char* rtfText)
{
	EDITSTREAM stream;
	RtfStreamContext ctx;
	CHARRANGE allText;
	const char* safeRtf = rtfText != NULL ? rtfText : "{\\rtf1\\ansi }";

	ctx.data = safeRtf;
	ctx.length = strlen(safeRtf);
	ctx.offset = 0;

	ZeroMemory(&stream, sizeof(stream));
	stream.dwCookie = (DWORD_PTR)&ctx;
	stream.pfnCallback = RichEditStreamInCallback;
	allText.cpMin = 0;
	allText.cpMax = -1;

	SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
	SendMessage(hwnd, EM_SETUNDOLIMIT, 0, 0);
	SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&allText);
	SendMessage(hwnd, EM_STREAMIN, (WPARAM)(SF_RTF | SFF_SELECTION | SF_USECODEPAGE | (CP_UTF8 << 16)), (LPARAM)&stream);
	SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hwnd, NULL, TRUE);
	UpdateWindow(hwnd);

	return stream.dwError == 0;
}

static VisibleCharRange
GetVisibleCharRange(HWND hwnd)
{
	VisibleCharRange range;
	POINTL topPoint;
	POINTL bottomPoint;
	RECT rect;
	LRESULT topChar;
	LRESULT bottomChar;

	range.cpMin = 0;
	range.cpMax = 0;
	range.valid = FALSE;

	if (hwnd == NULL)
		return range;

	if (!GetClientRect(hwnd, &rect))
		return range;

	topPoint.x = rect.left + 4;
	topPoint.y = rect.top + 4;
	bottomPoint.x = rect.left + 4;
	bottomPoint.y = rect.bottom > 8 ? rect.bottom - 8 : rect.bottom;

	topChar = SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&topPoint);
	bottomChar = SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&bottomPoint);
	if (topChar < 0 || bottomChar < 0)
		return range;

	range.cpMin = (LONG)topChar;
	range.cpMax = (LONG)bottomChar;
	if (range.cpMax < range.cpMin) {
		LONG temp = range.cpMin;
		range.cpMin = range.cpMax;
		range.cpMax = temp;
	}
	range.valid = TRUE;
	return range;
}

static unsigned int
ReadBe32(const unsigned char* data)
{
	return ((unsigned int)data[0] << 24) |
		((unsigned int)data[1] << 16) |
		((unsigned int)data[2] << 8) |
	(unsigned int)data[3];
}

static BOOL
IsHttpUrl(const char* value)
{
	if (value == NULL)
		return FALSE;
	return _strnicmp(value, "http://", 7) == 0 || _strnicmp(value, "https://", 8) == 0;
}

static int
GetImageFormatFromPathUtf8(const char* pathUtf8)
{
	const char* ext = strrchr(pathUtf8, '.');
	char lowerExt[8] = { 0 };
	int i = 0;

	if (ext == NULL)
		return 0;
	while (ext[i] && i < 7) {
		lowerExt[i] = (char)tolower((unsigned char)ext[i]);
		i++;
	}
	if (strcmp(lowerExt, ".png") == 0)
		return 1;
	if (strcmp(lowerExt, ".jpg") == 0 || strcmp(lowerExt, ".jpeg") == 0)
		return 2;
	return 0;
}

static BOOL
CreateReadOnlyStreamFromBytes(const unsigned char* data, size_t length, IStream** streamOut)
{
	HGLOBAL globalData = NULL;
	void* memory = NULL;
	IStream* stream = NULL;

	*streamOut = NULL;
	globalData = GlobalAlloc(GMEM_MOVEABLE, length == 0 ? 1 : length);
	if (globalData == NULL)
		goto cleanup;

	memory = GlobalLock(globalData);
	if (memory == NULL)
		goto cleanup;
	if (length > 0)
		memcpy(memory, data, length);
	GlobalUnlock(globalData);
	memory = NULL;

	if (FAILED(CreateStreamOnHGlobal(globalData, TRUE, &stream)) || stream == NULL)
		goto cleanup;

	*streamOut = stream;
	stream = NULL;
	globalData = NULL;
	return TRUE;

cleanup:
	if (memory != NULL)
		GlobalUnlock(globalData);
	if (stream != NULL)
		stream->lpVtbl->Release(stream);
	if (globalData != NULL)
		GlobalFree(globalData);
	return FALSE;
}

static BOOL
DownloadUrlBytes(const char* url, unsigned char** dataOut, size_t* lengthOut)
{
	WCHAR urlW[2048];
	URL_COMPONENTS components;
	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	WCHAR hostName[256];
	WCHAR urlPath[2048];
	DWORD flags = 0;
	unsigned char* buffer = NULL;
	size_t length = 0;

	*dataOut = NULL;
	*lengthOut = 0;

	if (MultiByteToWideChar(CP_UTF8, 0, url, -1, urlW, _countof(urlW)) == 0) {
		AppendDebugLog("DownloadUrlBytes: utf8->wide failed for %s", url);
		return FALSE;
	}

	ZeroMemory(&components, sizeof(components));
	components.dwStructSize = sizeof(components);
	components.lpszHostName = hostName;
	components.dwHostNameLength = _countof(hostName);
	components.lpszUrlPath = urlPath;
	components.dwUrlPathLength = _countof(urlPath);
	components.dwSchemeLength = (DWORD)-1;

	if (!WinHttpCrackUrl(urlW, 0, 0, &components)) {
		AppendDebugLog("DownloadUrlBytes: WinHttpCrackUrl failed for %s", url);
		goto cleanup;
	}
	if (components.nScheme == INTERNET_SCHEME_HTTPS)
		flags |= WINHTTP_FLAG_SECURE;

	hSession = WinHttpOpen(L"mdview/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL) {
		AppendDebugLog("DownloadUrlBytes: WinHttpOpen failed for %s", url);
		goto cleanup;
	}
	hConnect = WinHttpConnect(hSession, hostName, components.nPort, 0);
	if (hConnect == NULL) {
		AppendDebugLog("DownloadUrlBytes: WinHttpConnect failed for %s", url);
		goto cleanup;
	}
	hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (hRequest == NULL) {
		AppendDebugLog("DownloadUrlBytes: WinHttpOpenRequest failed for %s", url);
		goto cleanup;
	}
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		AppendDebugLog("DownloadUrlBytes: WinHttpSendRequest failed for %s", url);
		goto cleanup;
	}
	if (!WinHttpReceiveResponse(hRequest, NULL)) {
		AppendDebugLog("DownloadUrlBytes: WinHttpReceiveResponse failed for %s", url);
		goto cleanup;
	}

	for (;;) {
		DWORD available = 0;
		DWORD bytesRead = 0;
		unsigned char* newBuffer;

		if (!WinHttpQueryDataAvailable(hRequest, &available)) {
			AppendDebugLog("DownloadUrlBytes: WinHttpQueryDataAvailable failed for %s", url);
			goto cleanup;
		}
		if (available == 0)
			break;

		newBuffer = (unsigned char*)realloc(buffer, length + available);
		if (newBuffer == NULL)
			goto cleanup;
		buffer = newBuffer;

		if (!WinHttpReadData(hRequest, buffer + length, available, &bytesRead)) {
			AppendDebugLog("DownloadUrlBytes: WinHttpReadData failed for %s", url);
			goto cleanup;
		}
		length += bytesRead;
	}

	*dataOut = buffer;
	*lengthOut = length;
	buffer = NULL;
	AppendDebugLog("DownloadUrlBytes: downloaded %zu bytes from %s", length, url);

cleanup:
	if (buffer != NULL)
		free(buffer);
	if (hRequest != NULL)
		WinHttpCloseHandle(hRequest);
	if (hConnect != NULL)
		WinHttpCloseHandle(hConnect);
	if (hSession != NULL)
		WinHttpCloseHandle(hSession);
	return *dataOut != NULL;
}

static BOOL
CreateInputStreamFromSourceUtf8(const char* sourceUtf8, IStream** streamOut)
{
	if (IsHttpUrl(sourceUtf8)) {
		unsigned char* data = NULL;
		size_t length = 0;
		BOOL ok = DownloadUrlBytes(sourceUtf8, &data, &length) && CreateReadOnlyStreamFromBytes(data, length, streamOut);
		if (!ok)
			AppendDebugLog("CreateInputStreamFromSourceUtf8: failed for %s", sourceUtf8);
		if (data != NULL)
			free(data);
		return ok;
	}

	return FALSE;
}

static BOOL
ReadPngDimensions(FILE* file, LONG* widthPx, LONG* heightPx)
{
	unsigned char header[24];
	static const unsigned char pngSig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

	if (fread(header, 1, sizeof(header), file) != sizeof(header))
		return FALSE;
	if (memcmp(header, pngSig, sizeof(pngSig)) != 0)
		return FALSE;
	if (memcmp(header + 12, "IHDR", 4) != 0)
		return FALSE;
	*widthPx = (LONG)ReadBe32(header + 16);
	*heightPx = (LONG)ReadBe32(header + 20);
	return *widthPx > 0 && *heightPx > 0;
}

static BOOL
ReadJpegDimensions(FILE* file, LONG* widthPx, LONG* heightPx)
{
	if (fgetc(file) != 0xFF || fgetc(file) != 0xD8)
		return FALSE;

	for (;;) {
		int markerPrefix;
		int markerType;
		int hi;
		int lo;
		int segmentLen;

		do {
			markerPrefix = fgetc(file);
		} while (markerPrefix == 0xFF);

		if (markerPrefix == EOF)
			return FALSE;
		markerType = markerPrefix;

		while (markerType == 0xFF) {
			markerType = fgetc(file);
			if (markerType == EOF)
				return FALSE;
		}

		if (markerType == 0xD9 || markerType == 0xDA)
			return FALSE;
		if (markerType >= 0xD0 && markerType <= 0xD7)
			continue;

		hi = fgetc(file);
		lo = fgetc(file);
		if (hi == EOF || lo == EOF)
			return FALSE;
		segmentLen = (hi << 8) | lo;
		if (segmentLen < 2)
			return FALSE;

		if ((markerType >= 0xC0 && markerType <= 0xC3) ||
			(markerType >= 0xC5 && markerType <= 0xC7) ||
			(markerType >= 0xC9 && markerType <= 0xCB) ||
			(markerType >= 0xCD && markerType <= 0xCF)) {
			int precision = fgetc(file);
			int heightHi = fgetc(file);
			int heightLo = fgetc(file);
			int widthHi = fgetc(file);
			int widthLo = fgetc(file);
			(void)precision;

			if (heightHi == EOF || heightLo == EOF || widthHi == EOF || widthLo == EOF)
				return FALSE;

			*heightPx = (LONG)((heightHi << 8) | heightLo);
			*widthPx = (LONG)((widthHi << 8) | widthLo);
			return *widthPx > 0 && *heightPx > 0;
		}

		if (fseek(file, segmentLen - 2, SEEK_CUR) != 0)
			return FALSE;
	}
}

static BOOL
GetImagePixelSizeFromUtf8Source(const char* sourceUtf8, LONG* widthPx, LONG* heightPx)
{
	FILE* file = NULL;
	BOOL ok = FALSE;
	int format;
	IStream* stream = NULL;
	IWICImagingFactory* factory = NULL;
	IWICBitmapDecoder* decoder = NULL;
	IWICBitmapFrameDecode* frame = NULL;
	HRESULT hr;
	WCHAR pathW[MAX_PATH * 2];

	if (widthPx == NULL || heightPx == NULL || sourceUtf8 == NULL)
		return FALSE;
	*widthPx = 0;
	*heightPx = 0;

	if (IsHttpUrl(sourceUtf8)) {
		hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
			&IID_IWICImagingFactory, (void**)&factory);
		if (FAILED(hr) || factory == NULL)
			goto cleanup;
		if (!CreateInputStreamFromSourceUtf8(sourceUtf8, &stream))
			goto cleanup;
		hr = factory->lpVtbl->CreateDecoderFromStream(factory, stream, NULL,
			WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr) || decoder == NULL) {
			AppendDebugLog("GetImagePixelSizeFromUtf8Source: CreateDecoderFromStream failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
			goto cleanup;
		}
		hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
		if (FAILED(hr) || frame == NULL) {
			AppendDebugLog("GetImagePixelSizeFromUtf8Source: GetFrame failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
			goto cleanup;
		}
		{
			UINT width = 0;
			UINT height = 0;
			hr = frame->lpVtbl->GetSize(frame, &width, &height);
			if (FAILED(hr)) {
				AppendDebugLog("GetImagePixelSizeFromUtf8Source: GetSize failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
				goto cleanup;
			}
			*widthPx = (LONG)width;
			*heightPx = (LONG)height;
			ok = *widthPx > 0 && *heightPx > 0;
		}
		goto cleanup;
	}

	format = GetImageFormatFromPathUtf8(sourceUtf8);
	if (format == 0)
		goto cleanup;

	if (MultiByteToWideChar(CP_UTF8, 0, sourceUtf8, -1, pathW, _countof(pathW)) == 0)
		goto cleanup;
	if (_wfopen_s(&file, pathW, L"rb") != 0 || file == NULL)
		goto cleanup;

	if (format == 1)
		ok = ReadPngDimensions(file, widthPx, heightPx);
	else if (format == 2)
		ok = ReadJpegDimensions(file, widthPx, heightPx);

cleanup:
	if (file != NULL)
		fclose(file);
	if (frame != NULL)
		frame->lpVtbl->Release(frame);
	if (decoder != NULL)
		decoder->lpVtbl->Release(decoder);
	if (factory != NULL)
		factory->lpVtbl->Release(factory);
	if (stream != NULL)
		stream->lpVtbl->Release(stream);
	return ok;
}

static BOOL
CreatePngStreamFromUtf8Source(const char* sourceUtf8, IStream** streamOut)
{
	WCHAR pathW[MAX_PATH * 2];
	IWICImagingFactory* factory = NULL;
	IWICBitmapDecoder* decoder = NULL;
	IWICBitmapFrameDecode* frame = NULL;
	IWICStream* wicStream = NULL;
	IWICBitmapEncoder* encoder = NULL;
	IWICBitmapFrameEncode* encodeFrame = NULL;
	IPropertyBag2* propertyBag = NULL;
	IStream* inputStream = NULL;
	IStream* outputStream = NULL;
	HRESULT hr;

	*streamOut = NULL;

	hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		&IID_IWICImagingFactory, (void**)&factory);
	if (FAILED(hr) || factory == NULL)
		goto cleanup;

	if (IsHttpUrl(sourceUtf8)) {
		if (!CreateInputStreamFromSourceUtf8(sourceUtf8, &inputStream))
			goto cleanup;
		hr = factory->lpVtbl->CreateDecoderFromStream(factory, inputStream, NULL,
			WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr) || decoder == NULL) {
			AppendDebugLog("CreatePngStreamFromUtf8Source: CreateDecoderFromStream failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
			goto cleanup;
		}
	}
	else {
		if (MultiByteToWideChar(CP_UTF8, 0, sourceUtf8, -1, pathW, _countof(pathW)) == 0)
			goto cleanup;
		hr = factory->lpVtbl->CreateDecoderFromFilename(factory, pathW, NULL, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr) || decoder == NULL)
			goto cleanup;
	}

	hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
	if (FAILED(hr) || frame == NULL)
		goto cleanup;

	{
		hr = CreateStreamOnHGlobal(NULL, TRUE, &outputStream);
		if (FAILED(hr) || outputStream == NULL)
			goto cleanup;
	}

	hr = factory->lpVtbl->CreateStream(factory, &wicStream);
	if (FAILED(hr) || wicStream == NULL)
		goto cleanup;

	hr = wicStream->lpVtbl->InitializeFromIStream(wicStream, outputStream);
	if (FAILED(hr))
		goto cleanup;

	hr = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
	if (FAILED(hr) || encoder == NULL)
		goto cleanup;

	hr = encoder->lpVtbl->Initialize(encoder, (IStream*)wicStream, WICBitmapEncoderNoCache);
	if (FAILED(hr))
		goto cleanup;

	hr = encoder->lpVtbl->CreateNewFrame(encoder, &encodeFrame, &propertyBag);
	if (FAILED(hr) || encodeFrame == NULL)
		goto cleanup;

	hr = encodeFrame->lpVtbl->Initialize(encodeFrame, propertyBag);
	if (FAILED(hr))
		goto cleanup;

	hr = encodeFrame->lpVtbl->SetSize(encodeFrame, 0, 0);
	if (FAILED(hr)) {
		UINT width = 0;
		UINT height = 0;
		hr = frame->lpVtbl->GetSize(frame, &width, &height);
		if (FAILED(hr))
			goto cleanup;
		hr = encodeFrame->lpVtbl->SetSize(encodeFrame, width, height);
		if (FAILED(hr))
			goto cleanup;
	}

	{
		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormatDontCare;
		hr = encodeFrame->lpVtbl->SetPixelFormat(encodeFrame, &pixelFormat);
		if (FAILED(hr))
			goto cleanup;
	}

	hr = encodeFrame->lpVtbl->WriteSource(encodeFrame, (IWICBitmapSource*)frame, NULL);
	if (FAILED(hr))
		goto cleanup;

	hr = encodeFrame->lpVtbl->Commit(encodeFrame);
	if (FAILED(hr))
		goto cleanup;

	hr = encoder->lpVtbl->Commit(encoder);
	if (FAILED(hr))
		goto cleanup;

	{
		LARGE_INTEGER zero;
		zero.QuadPart = 0;
		hr = outputStream->lpVtbl->Seek(outputStream, zero, STREAM_SEEK_SET, NULL);
		if (FAILED(hr))
			goto cleanup;
	}

	*streamOut = outputStream;
	outputStream = NULL;

cleanup:
	if (propertyBag != NULL)
		propertyBag->lpVtbl->Release(propertyBag);
	if (encodeFrame != NULL)
		encodeFrame->lpVtbl->Release(encodeFrame);
	if (encoder != NULL)
		encoder->lpVtbl->Release(encoder);
	if (wicStream != NULL)
		wicStream->lpVtbl->Release(wicStream);
	if (frame != NULL)
		frame->lpVtbl->Release(frame);
	if (decoder != NULL)
		decoder->lpVtbl->Release(decoder);
	if (factory != NULL)
		factory->lpVtbl->Release(factory);
	if (inputStream != NULL)
		inputStream->lpVtbl->Release(inputStream);
	if (outputStream != NULL)
		outputStream->lpVtbl->Release(outputStream);
	return *streamOut != NULL;
}

static BOOL
CreatePngStreamAndSizeFromUtf8Source(const char* sourceUtf8, IStream** streamOut, LONG* widthPx, LONG* heightPx)
{
	WCHAR pathW[MAX_PATH * 2];
	IWICImagingFactory* factory = NULL;
	IWICBitmapDecoder* decoder = NULL;
	IWICBitmapFrameDecode* frame = NULL;
	IWICStream* wicStream = NULL;
	IWICBitmapEncoder* encoder = NULL;
	IWICBitmapFrameEncode* encodeFrame = NULL;
	IPropertyBag2* propertyBag = NULL;
	IStream* inputStream = NULL;
	IStream* outputStream = NULL;
	HRESULT hr;

	*streamOut = NULL;
	if (widthPx != NULL)
		*widthPx = 0;
	if (heightPx != NULL)
		*heightPx = 0;

	hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		&IID_IWICImagingFactory, (void**)&factory);
	if (FAILED(hr) || factory == NULL)
		goto cleanup;

	if (IsHttpUrl(sourceUtf8)) {
		if (!CreateInputStreamFromSourceUtf8(sourceUtf8, &inputStream))
			goto cleanup;
		hr = factory->lpVtbl->CreateDecoderFromStream(factory, inputStream, NULL,
			WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr) || decoder == NULL) {
			AppendDebugLog("CreatePngStreamAndSizeFromUtf8Source: CreateDecoderFromStream failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
			goto cleanup;
		}
	}
	else {
		if (MultiByteToWideChar(CP_UTF8, 0, sourceUtf8, -1, pathW, _countof(pathW)) == 0)
			goto cleanup;
		hr = factory->lpVtbl->CreateDecoderFromFilename(factory, pathW, NULL, GENERIC_READ,
			WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr) || decoder == NULL)
			goto cleanup;
	}

	hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
	if (FAILED(hr) || frame == NULL)
		goto cleanup;

	if (widthPx != NULL || heightPx != NULL) {
		UINT width = 0;
		UINT height = 0;
		hr = frame->lpVtbl->GetSize(frame, &width, &height);
		if (FAILED(hr)) {
			AppendDebugLog("CreatePngStreamAndSizeFromUtf8Source: GetSize failed hr=0x%08lx for %s", (unsigned long)hr, sourceUtf8);
			goto cleanup;
		}
		if (widthPx != NULL)
			*widthPx = (LONG)width;
		if (heightPx != NULL)
			*heightPx = (LONG)height;
	}

	hr = CreateStreamOnHGlobal(NULL, TRUE, &outputStream);
	if (FAILED(hr) || outputStream == NULL)
		goto cleanup;

	hr = factory->lpVtbl->CreateStream(factory, &wicStream);
	if (FAILED(hr) || wicStream == NULL)
		goto cleanup;

	hr = wicStream->lpVtbl->InitializeFromIStream(wicStream, outputStream);
	if (FAILED(hr))
		goto cleanup;

	hr = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
	if (FAILED(hr) || encoder == NULL)
		goto cleanup;

	hr = encoder->lpVtbl->Initialize(encoder, (IStream*)wicStream, WICBitmapEncoderNoCache);
	if (FAILED(hr))
		goto cleanup;

	hr = encoder->lpVtbl->CreateNewFrame(encoder, &encodeFrame, &propertyBag);
	if (FAILED(hr) || encodeFrame == NULL)
		goto cleanup;

	hr = encodeFrame->lpVtbl->Initialize(encodeFrame, propertyBag);
	if (FAILED(hr))
		goto cleanup;

	if (widthPx != NULL && heightPx != NULL && *widthPx > 0 && *heightPx > 0) {
		hr = encodeFrame->lpVtbl->SetSize(encodeFrame, (UINT)*widthPx, (UINT)*heightPx);
		if (FAILED(hr))
			goto cleanup;
	}
	else {
		UINT width = 0;
		UINT height = 0;
		hr = frame->lpVtbl->GetSize(frame, &width, &height);
		if (FAILED(hr))
			goto cleanup;
		hr = encodeFrame->lpVtbl->SetSize(encodeFrame, width, height);
		if (FAILED(hr))
			goto cleanup;
	}

	{
		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormatDontCare;
		hr = encodeFrame->lpVtbl->SetPixelFormat(encodeFrame, &pixelFormat);
		if (FAILED(hr))
			goto cleanup;
	}

	hr = encodeFrame->lpVtbl->WriteSource(encodeFrame, (IWICBitmapSource*)frame, NULL);
	if (FAILED(hr))
		goto cleanup;
	hr = encodeFrame->lpVtbl->Commit(encodeFrame);
	if (FAILED(hr))
		goto cleanup;
	hr = encoder->lpVtbl->Commit(encoder);
	if (FAILED(hr))
		goto cleanup;

	{
		LARGE_INTEGER zero;
		zero.QuadPart = 0;
		hr = outputStream->lpVtbl->Seek(outputStream, zero, STREAM_SEEK_SET, NULL);
		if (FAILED(hr))
			goto cleanup;
	}

	*streamOut = outputStream;
	outputStream = NULL;

cleanup:
	if (propertyBag != NULL)
		propertyBag->lpVtbl->Release(propertyBag);
	if (encodeFrame != NULL)
		encodeFrame->lpVtbl->Release(encodeFrame);
	if (encoder != NULL)
		encoder->lpVtbl->Release(encoder);
	if (wicStream != NULL)
		wicStream->lpVtbl->Release(wicStream);
	if (frame != NULL)
		frame->lpVtbl->Release(frame);
	if (decoder != NULL)
		decoder->lpVtbl->Release(decoder);
	if (factory != NULL)
		factory->lpVtbl->Release(factory);
	if (inputStream != NULL)
		inputStream->lpVtbl->Release(inputStream);
	if (outputStream != NULL)
		outputStream->lpVtbl->Release(outputStream);
	return *streamOut != NULL;
}

static RenderedDocument*
RenderMarkdownDocument(const char* markdown, const char* imgPath, int enableImages)
{
	RenderedDocument* doc;
	int i;

	doc = (RenderedDocument*)calloc(1, sizeof(RenderedDocument));
	if (doc == NULL)
		return NULL;

	doc->rtf = markdown2rtf_ex(markdown, imgPath, enableImages);
	if (doc->rtf == NULL) {
		free(doc);
		return NULL;
	}

	doc->imageCount = markdown_get_pending_image_count();
	if (doc->imageCount > 0) {
		doc->images = (PendingImageRef*)calloc(doc->imageCount, sizeof(PendingImageRef));
		if (doc->images == NULL) {
			free(doc->rtf);
			free(doc);
			return NULL;
		}

		for (i = 0; i < doc->imageCount; ++i) {
			const char* marker = markdown_get_pending_image_marker(i);
			const char* pathUtf8 = markdown_get_pending_image_path(i);
			doc->images[i].markerUtf8 = marker ? _strdup(marker) : NULL;
			doc->images[i].pathUtf8 = pathUtf8 ? _strdup(pathUtf8) : NULL;
		}
	}

	markdown_clear_pending_images();
	return doc;
}

static void
FreeRenderedDocument(RenderedDocument* doc)
{
	int i;

	if (doc == NULL)
		return;
	if (doc->images != NULL) {
		for (i = 0; i < doc->imageCount; ++i) {
			if (doc->images[i].markerUtf8 != NULL)
				free(doc->images[i].markerUtf8);
			if (doc->images[i].pathUtf8 != NULL)
				free(doc->images[i].pathUtf8);
		}
		free(doc->images);
	}
	if (doc->rtf != NULL)
		free(doc->rtf);
	free(doc);
}

static BOOL
InsertPendingImages(HWND hwnd, const RenderedDocument* doc)
{
	int remainingCount;
	BOOL* inserted;
	VisibleCharRange visibleRange;
	AppendDebugLog("InsertPendingImages start: count=%d", doc ? doc->imageCount : -1);

	if (doc == NULL || doc->imageCount <= 0)
		return TRUE;

	inserted = (BOOL*)calloc((size_t)doc->imageCount, sizeof(BOOL));
	if (inserted == NULL)
		return FALSE;

	SendMessageW(hwnd, EM_SETREADONLY, FALSE, 0);
	visibleRange = GetVisibleCharRange(hwnd);
	AppendDebugLog("InsertPendingImages visible range: valid=%d cpMin=%ld cpMax=%ld",
		visibleRange.valid ? 1 : 0, (long)visibleRange.cpMin, (long)visibleRange.cpMax);
	remainingCount = doc->imageCount;

	while (remainingCount > 0) {
		int bestIndex = -1;
		LONG bestScore = LONG_MAX;
		FINDTEXTEXW bestFind;
		int i;

		ZeroMemory(&bestFind, sizeof(bestFind));

		for (i = 0; i < doc->imageCount; ++i) {
			FINDTEXTEXW find;
			CHARRANGE range;
			WCHAR markerW[128];
			LONG score;

			if (inserted[i] || doc->images[i].markerUtf8 == NULL || doc->images[i].pathUtf8 == NULL)
				continue;
			if (MultiByteToWideChar(CP_UTF8, 0, doc->images[i].markerUtf8, -1, markerW, _countof(markerW)) == 0)
				continue;

			range.cpMin = 0;
			range.cpMax = -1;
			find.chrg = range;
			find.lpstrText = markerW;
			if (SendMessageW(hwnd, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&find) == -1)
				continue;

			if (!visibleRange.valid) {
				score = find.chrgText.cpMin;
			}
			else if (find.chrgText.cpMax < visibleRange.cpMin) {
				score = visibleRange.cpMin - find.chrgText.cpMax;
			}
			else if (find.chrgText.cpMin > visibleRange.cpMax) {
				score = find.chrgText.cpMin - visibleRange.cpMax;
			}
			else {
				score = 0;
			}

			if (bestIndex < 0 || score < bestScore || (score == bestScore && find.chrgText.cpMin < bestFind.chrgText.cpMin)) {
				bestIndex = i;
				bestScore = score;
				bestFind = find;
			}
		}

		if (bestIndex < 0)
			break;

		{
			LONG widthPx = 0;
			LONG heightPx = 0;
			IStream* imageStream = NULL;
			RICHEDIT_IMAGE_PARAMETERS imageParams;

			AppendDebugLog("Image[%d]: marker=%s path=%s score=%ld cpMin=%ld cpMax=%ld",
				bestIndex,
				doc->images[bestIndex].markerUtf8,
				doc->images[bestIndex].pathUtf8,
				(long)bestScore,
				(long)bestFind.chrgText.cpMin,
				(long)bestFind.chrgText.cpMax);

			if (!CreatePngStreamAndSizeFromUtf8Source(doc->images[bestIndex].pathUtf8, &imageStream, &widthPx, &heightPx)) {
				AppendDebugLog("Image[%d]: png stream/size creation failed", bestIndex);
				inserted[bestIndex] = TRUE;
				remainingCount--;
				continue;
			}

			AppendDebugLog("Image[%d]: size=%ldx%ld", bestIndex, (long)widthPx, (long)heightPx);

			SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&bestFind.chrgText);
			SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)L"");

			ZeroMemory(&imageParams, sizeof(imageParams));
			imageParams.xWidth = (widthPx * 2540) / 96;
			imageParams.yHeight = (heightPx * 2540) / 96;
			imageParams.Ascent = 0;
			imageParams.Type = TA_BASELINE;
			imageParams.pwszAlternateText = L"";
			imageParams.pIStream = imageStream;
			{
				LRESULT hr = SendMessageW(hwnd, EM_INSERTIMAGE, 0, (LPARAM)&imageParams);
				AppendDebugLog("Image[%d]: EM_INSERTIMAGE hr=0x%08lx", bestIndex, (unsigned long)hr);
			}
			imageStream->lpVtbl->Release(imageStream);
			inserted[bestIndex] = TRUE;
			remainingCount--;

			if (bestScore == 0) {
				RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
			}
		}
	}

	SendMessageW(hwnd, EM_SETREADONLY, TRUE, 0);
	InvalidateRect(hwnd, NULL, TRUE);
	UpdateWindow(hwnd);
	free(inserted);

	return TRUE;
}

static void
AppendDebugLog(const char* format, ...)
{
	FILE* file;
	va_list args;

	if (fopen_s(&file, "C:\\~\\Projects\\mdview\\_mdview_image_debug.log", "a") != 0 || file == NULL)
		return;

	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);
	fputc('\n', file);
	fclose(file);
}

void
FileOpenDialog()
{
	OPENFILENAME ofn;
	TCHAR szFile[260] = { 0 };

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hMainWindow;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"All\0*.*\0MarkDown\0*.md\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileNameW(&ofn) == TRUE)
	{
		FileOpen(ofn.lpstrFile);
	}
}

BOOL CreateToolBar()
{
	int iDpi = GetDpiForWindow(hMainWindow);
	const int ImageListID = 0;
	const int numButtons = 3;
	// Custom scaling: 16px at 100% (96 DPI), 48px at 200% (192 DPI).
	// Formula: Size = DPI / 3 - 16
	int bitmapSize = (iDpi / 3) - 16;
	if (bitmapSize < 16) bitmapSize = 16; // Minimum safe size

	const DWORD buttonStyles = BTNS_AUTOSIZE;
	HIMAGELIST hImageList = NULL;

	HWND hWndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | TBSTYLE_WRAPABLE /*| TBSTYLE_LIST | TBSTYLE_EX_MIXEDBUTTONS */, 0, 0, 0, 0,
		hMainWindow, NULL, hInst, NULL);

	if (hWndToolbar == NULL) {
		ViewerCommon_ShowLastError(L"Toolbar Creation Failed!");
		return FALSE;
	}
	hImageList = ImageList_Create(bitmapSize, bitmapSize,   // Dimensions of individual bitmaps.
		ILC_COLOR32 | ILC_MASK,   // Updated to 32-bit color for better scaling
		numButtons, 0);

	SendMessage(hWndToolbar, TB_SETIMAGELIST, (WPARAM)ImageListID, (LPARAM)hImageList);
	SendMessage(hWndToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(bitmapSize, bitmapSize));

	// Load icons with correct DPI scaling
	HICON hOpenIcon = (HICON)LoadImage(GetModuleHandle(L"shell32.dll"), MAKEINTRESOURCE(5), IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);
	// Note: index 4 in ExtractIconEx is resource ID 5 in some shell32 versions, but let's use a more robust way if possible.
	// Actually, LoadIconWithScaleDown is even better for quality.
	// But let's try a simple approach first:
	HICON hOpenLarge, hRefreshLarge;
	ExtractIconEx(L"shell32.dll", 4, &hOpenLarge, NULL, 1);
	ExtractIconEx(L"shell32.dll", 238, &hRefreshLarge, NULL, 1);
	
	// If the extracted icon isn't the right size, we'll use LoadImage on the system icons
	HICON hCloseIcon = (HICON)LoadImage(NULL, IDI_ERROR, IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);

	// Since we want specific size, using LoadImage on resources is better. 
	// For shell32 icons, we can use private ExtractIcon or similar, but for simplicity:
	int iOpenIndex = ImageList_AddIcon(hImageList, hOpenLarge);
	int iRefreshIndex = ImageList_AddIcon(hImageList, hRefreshLarge);
	int iCloseIndex = ImageList_AddIcon(hImageList, hCloseIcon);

	DestroyIcon(hOpenLarge);
	DestroyIcon(hRefreshLarge);

	// Send the TB_BUTTONSTRUCTSIZE message, which is required for backward compatibility.
	SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

	TBBUTTON tbButtons[5] =
	{
		{ iOpenIndex, IDM_FILE_OPEN, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)NULL },
		{ 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
		{ iRefreshIndex, IDM_FILE_REFRESH, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)NULL },
		{ 0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
		{ iCloseIndex, IDM_FILE_EXIT, TBSTATE_ENABLED, BTNS_AUTOSIZE, { 0 }, 0, (INT_PTR)NULL }
	};

	SendMessage(hWndToolbar, TB_ADDBUTTONS, (WPARAM)5, (LPARAM)&tbButtons);

	SendMessage(hWndToolbar, TB_AUTOSIZE, 0, 0);
	int padding = MulDiv(12, iDpi, 96);
	SendMessage(hWndToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(bitmapSize + padding, bitmapSize + padding));
	ShowWindow(hWndToolbar, TRUE);

	hToolBar = hWndToolbar;
	return TRUE;
}

BOOL CreateStatusBar()
{
	hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, L"",
		WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		-100, -100, 10, 10,
		hMainWindow, NULL, hInst, NULL);

	int iPart[3] = { 200, 250, -1 };
	SendMessage(hStatusBar, SB_SETPARTS, (WPARAM)3, (LPARAM)iPart);



	return TRUE;
}



