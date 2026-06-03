// llmview - LLM-assisted Markdown viewer
#include <windows.h>
#include <Commctrl.h>
#include <RichEdit.h>
#include <Shellapi.h>
#include <ShellScalingApi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "Resource.h"
#include "viewer_common.h"
#include "llm_translate.h"

#define MAX_LOADSTRING 100
#define MARGIN 20
#define PREVIEW_BYTES (96 * 1024)
#define LLM_QUICK_BYTES (12 * 1024)
#define LLM_WINDOW_BYTES (4 * 1024)
#define LLM_INITIAL_SLICE_CHARS 1200
#define LLM_INITIAL_SLICE_PARAGRAPHS 4
#define LLM_SLICE_CHARS 2200
#define LLM_SLICE_PARAGRAPHS 8
#define LLM_SINGLE_PARAGRAPH_HARD_LIMIT 7000
#define LLM_OVERSIZE_SPLIT_TARGET 1800
#define LLM_OVERSIZE_SPLIT_MAX 2200
#define WM_APP_RENDER_COMPLETE (WM_APP + 1)
#define WM_APP_VISIBLE_RANGE_CHANGED (WM_APP + 2)
#define WM_APP_AUTO_SAVE_RESULT (WM_APP + 3)
#define WM_APP_OPEN_FILE (WM_APP + 4)

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

char* markdown2rtf(const char* md, const char* img_path);
char* markdown2rtf_ex(const char* md, const char* img_path, int enable_images);

typedef enum ParagraphState {
	PARAGRAPH_PENDING = 0,
	PARAGRAPH_INFLIGHT = 1,
	PARAGRAPH_DONE = 2,
	PARAGRAPH_SKIPPED = 3
} ParagraphState;

typedef struct ParagraphInfo {
	char* original;
	size_t originalLen;
	char* translated;
	size_t translatedLen;
	size_t sourceStart;
	size_t sourceEnd;
	ParagraphState state;
	unsigned short failCount;
} ParagraphInfo;

typedef struct TranslationSegment {
	int paragraphIndex;
	char marker[24];
} TranslationSegment;

typedef struct TextBuffer {
	char* data;
	size_t length;
	size_t capacity;
} TextBuffer;

typedef struct DocumentSession {
	DWORD generation;
	char* sourceMarkdown;
	size_t sourceLen;
	char* imagePath;
	ParagraphInfo* paragraphs;
	int paragraphCount;
	int visibleStart;
	int visibleEnd;
	BOOL translationEnabled;
	BOOL hasTranslatedContent;
	BOOL deferInitialBackgroundRender;
} DocumentSession;

typedef struct RenderSnapshot {
	DWORD generation;
	char* markdown;
	char* imagePath;
} RenderSnapshot;

typedef struct TranslationSlice {
	DWORD generation;
	int startParagraph;
	int endParagraph;
	char* markdown;
	TranslationSegment* segments;
	int segmentCount;
} TranslationSlice;

HINSTANCE hInst;
HICON hIcon;
TCHAR szAppName[MAX_LOADSTRING];
TCHAR szWindowClass[] = L"LLMViewWindowClass";
HWND hMainWindow = NULL;
HMENU hMainMenu = NULL;
HWND hToolBar = NULL;
HWND hRichEdit = NULL;
HWND hStatusBar = NULL;
WNDPROC g_originalRichEditProc = NULL;
WCHAR szCurrentFile[MAX_PATH] = L"";
WCHAR g_pendingSavePath[PATH_BUFFER_SIZE] = L"";
volatile LONG g_loadGeneration = 0;
BOOL g_exitThreads = FALSE;
BOOL g_translateToggle = TRUE;
CRITICAL_SECTION g_sessionLock;
HANDLE g_renderEvent = NULL;
HANDLE g_translateEvent = NULL;
HANDLE g_renderThread = NULL;
HANDLE g_translateThread = NULL;
DocumentSession* g_session = NULL;
LlmConfig g_llmConfig;
WCHAR g_lastConfigError[256] = L"";
DWORD g_pendingSaveGeneration = 0;

static const DWORD ESC_DOUBLE_PRESS_INTERVAL_MS = 600;
static ULONGLONG g_lastEscPressTimestamp = 0;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RichEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
BOOL FileOpen(WCHAR* lpszTextFileName);
void FileOpenDialog();
BOOL CreateToolBar();
BOOL CreateStatusBar();
DWORD WINAPI RenderThreadProc(LPVOID lpParam);
DWORD WINAPI TranslateThreadProc(LPVOID lpParam);

static DocumentSession* CreateSession(const ViewerLoadedFile* loadedFile, DWORD generation, BOOL translationEnabled);
static void FreeSession(DocumentSession* session);
static BOOL RenderInitialPreview(const DocumentSession* session);
static size_t FindParagraphWindowEnd(const DocumentSession* session, int startParagraph, size_t targetChars);
static BOOL ApplySliceTranslation(DocumentSession* session, const TranslationSlice* slice, const char* translatedMarkdown, BOOL skippedByLanguage);
static void TrimWrappingCodeFence(char* markdown);
static void TrimBoundaryNewlines(char* text);
static BOOL ComposeDisplayMarkdown(const DocumentSession* session, char** markdownOut);
static BOOL BuildRenderSnapshot(RenderSnapshot* snapshot);
static void FreeRenderSnapshot(RenderSnapshot* snapshot);
static BOOL SelectNextTranslationSlice(TranslationSlice* slice);
static void FreeTranslationSlice(TranslationSlice* slice);
static void UpdateVisibleRangeFromScroll(void);
static int FindParagraphForOffset(const DocumentSession* session, size_t offset);
static BOOL VisibleRangeHasPending(const DocumentSession* session);
static void UpdateTranslateToggleUi(void);
static void QueueRender(void);
static void QueueTranslation(void);
static void ReopenCurrentFile(void);
static void ReplaceSession(DocumentSession* session);
static BOOL TextBuffer_Reserve(TextBuffer* buffer, size_t extra);
static BOOL TextBuffer_AppendN(TextBuffer* buffer, const char* text, size_t textLen);
static BOOL TextBuffer_Append(TextBuffer* buffer, const char* text);
static void TextBuffer_Free(TextBuffer* buffer);
static size_t CountOversizeSegments(const char* text, size_t textLen);
static size_t FindSplitPoint(const char* text, size_t start, size_t textLen, size_t targetLen, size_t maxLen);
static BOOL BuildSegmentedSliceMarkdown(const DocumentSession* session, int startParagraph, int endParagraph, TranslationSegment* segments, int segmentCount, char** markdownOut);
static BOOL ExtractMarkedSegmentTexts(const TranslationSlice* slice, char* translatedMarkdown, char** segmentTexts);
static BOOL BuildLanguageTagSuffix(const char* targetLangUtf8, WCHAR* buffer, size_t bufferCount);
static BOOL LoadResourceString(UINT stringId, WCHAR* buffer, size_t bufferCount);
static void SetStatusBarTextResource(UINT stringId);
static int MessageBoxResource(HWND owner, UINT textId, UINT type);
static BOOL GetDefaultSavePath(WCHAR* buffer, size_t bufferCount);
static BOOL PromptSavePath(WCHAR* pathBuffer, DWORD pathBufferCount);
static BOOL WriteUtf8TextFile(const WCHAR* filePath, const char* textUtf8);
static BOOL SaveCurrentSessionToPath(const WCHAR* filePath);
static BOOL IsSessionTranslationComplete(const DocumentSession* session);
static int GetSessionTranslationPercent(const DocumentSession* session);
static int ShowPartialSaveDialog(int percentComplete);
static void ClearPendingSaveRequest(void);
static void StartDeferredSave(const WCHAR* filePath, DWORD generation);
static BOOL TryAutoSaveCompletedTranslation(DWORD generation);
static BOOL HandleSaveTranslatedCommand(void);

int APIENTRY
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEX wc;
	MSG msg;
	HACCEL hAccelTable;
	INITCOMMONCONTROLSEX icex;

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
	InitializeCriticalSection(&g_sessionLock);

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

	if (!RegisterClassEx(&wc)) {
		MessageBoxResource(NULL, IDS_MSG_WINDOW_REG_FAILED, MB_ICONERROR | MB_OK);
		return 0;
	}

	hMainWindow = CreateWindow(szWindowClass, szAppName, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, hMainMenu, hInstance, NULL);
	if (hMainWindow == NULL) {
		MessageBoxResource(NULL, IDS_MSG_WINDOW_CREATE_FAILED, MB_ICONERROR | MB_OK);
		return 0;
	}

	g_renderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_translateEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_renderThread = CreateThread(NULL, 0, RenderThreadProc, NULL, 0, NULL);
	g_translateThread = CreateThread(NULL, 0, TranslateThreadProc, NULL, 0, NULL);

	DragAcceptFiles(hMainWindow, TRUE);
	CreateToolBar();
	CreateStatusBar();
	UpdateTranslateToggleUi();

	{
		ViewerCommonContext commonCtx = { hMainWindow, hStatusBar, szAppName };
		if (!ViewerCommon_RestoreState(&commonCtx)) {
			ShowWindow(hMainWindow, nCmdShow);
		}
	}

	UpdateWindow(hMainWindow);

	{
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
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
			ULONGLONG now = GetTickCount64();
			if (g_lastEscPressTimestamp != 0 && (now - g_lastEscPressTimestamp) <= ESC_DOUBLE_PRESS_INTERVAL_MS) {
				g_lastEscPressTimestamp = 0;
				PostMessage(hMainWindow, WM_CLOSE, 0, 0);
				continue;
			}
			g_lastEscPressTimestamp = now;
		}
		if (!TranslateAccelerator(hMainWindow, hAccelTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}

LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int iClientAreaTop;
	int iClientAreaHeight;
	int iClientAreaWidth;

	switch (msg) {
	case WM_CREATE: {
		HMODULE mftedit = LoadLibraryA("Msftedit.dll");
		if (!mftedit) {
			ViewerCommon_ShowLastError(L"Msftedit.dll Load Failed!");
			return 0;
		}
		DrawMenuBar(hMainWindow);
		hRichEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
			ES_MULTILINE | WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_READONLY,
			10, 100, 200, 200,
			hwnd, (HMENU)NULL, GetModuleHandle(NULL), NULL);
		if (hRichEdit == NULL) {
			ViewerCommon_ShowLastError(L"RichEdit Creation Failed!");
			return 0;
		}
		SendMessage(hRichEdit, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessage(hRichEdit, EM_SETEDITSTYLEEX, 0, SES_EX_HANDLEFRIENDLYURL | SES_HYPERLINKTOOLTIPS);
		HideCaret(hRichEdit);
		g_originalRichEditProc = (WNDPROC)SetWindowLongPtr(hRichEdit, GWLP_WNDPROC, (LONG_PTR)RichEditProc);
		break;
	}

	case WM_SETCURSOR:
		if ((HWND)wParam == hRichEdit) {
			if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
				SetCursor(LoadCursor(NULL, IDC_IBEAM));
			}
			else {
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDM_FILE_EXIT:
			PostQuitMessage(0);
			break;
		case IDM_FILE_OPEN:
			FileOpenDialog();
			break;
		case IDM_FILE_REFRESH:
			ReopenCurrentFile();
			break;
		case IDM_FILE_SAVE_TRANSLATED:
			HandleSaveTranslatedCommand();
			break;
		case IDM_VIEW_TRANSLATE:
			g_translateToggle = !g_translateToggle;
			UpdateTranslateToggleUi();
			ReopenCurrentFile();
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
		switch (LOWORD(wParam)) {
		case IDM_FILE_OPEN:
			SetStatusBarTextResource(IDS_STATUS_OPEN_FILE);
			break;
		case IDM_FILE_SAVE_TRANSLATED:
			SetStatusBarTextResource(IDS_STATUS_SAVE_TRANSLATED);
			break;
		case IDM_VIEW_TRANSLATE:
			SetStatusBarTextResource(IDS_STATUS_TOGGLE_TRANSLATE);
			break;
		default:
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"");
			break;
		}
		break;
	case WM_SIZE:
		iClientAreaWidth = LOWORD(lParam);
		iClientAreaHeight = HIWORD(lParam);
		{
			int iPart[1] = { -1 };
			SendMessage(hStatusBar, SB_SETPARTS, (WPARAM)1, (LPARAM)iPart);
		}
		SendMessage(hToolBar, TB_AUTOSIZE, 0, 0);
		SendMessage(hStatusBar, WM_SIZE, 0, 0);
		{
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
		}
		PostMessage(hwnd, WM_APP_VISIBLE_RANGE_CHANGED, 0, 0);
		break;
	case WM_DROPFILES: {
		HDROP hDrop = (HDROP)wParam;
		TCHAR szNextFile[MAX_PATH];
		if (DragQueryFile(hDrop, 0, szNextFile, MAX_PATH) > 0) {
			FileOpen(szNextFile);
		}
		DragFinish(hDrop);
		break;
	}
	case WM_NOTIFY:
		if (((LPNMHDR)lParam)->code == EN_LINK) {
			ENLINK* enLinkInfo = (ENLINK*)lParam;
			TEXTRANGE tr;
			if (enLinkInfo->msg == WM_LBUTTONUP) {
				TCHAR szLink[1024];
				tr.chrg = enLinkInfo->chrg;
				tr.lpstrText = szLink;
				SendMessage(hRichEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				ShellExecuteW(NULL, L"open", szLink, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		break;
	case WM_APP_RENDER_COMPLETE: {
		DWORD generation = (DWORD)lParam;
		char* rtfResult = (char*)wParam;
		if (rtfResult != NULL) {
			if (generation == (DWORD)g_loadGeneration) {
				SETTEXTEX se;
				se.codepage = 65001;
				se.flags = ST_DEFAULT;
				SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)rtfResult);
				EnterCriticalSection(&g_sessionLock);
				if (g_session != NULL && g_session->generation == generation && g_session->translationEnabled && g_session->hasTranslatedContent && VisibleRangeHasPending(g_session)) {
					SetStatusBarTextResource(IDS_STATUS_TRANSLATING_REST);
				}
				else if (g_session != NULL && g_session->generation == generation && g_session->translationEnabled && !g_session->hasTranslatedContent) {
					SetStatusBarTextResource(IDS_STATUS_PREVIEW_TRANSLATING);
				}
				else {
					SetStatusBarTextResource(IDS_STATUS_DOCUMENT_LOADED);
				}
				LeaveCriticalSection(&g_sessionLock);
			}
			free(rtfResult);
		}
		return 0;
	}
	case WM_APP_VISIBLE_RANGE_CHANGED:
		UpdateVisibleRangeFromScroll();
		return 0;
	case WM_APP_AUTO_SAVE_RESULT:
		if (wParam) {
			SetStatusBarTextResource(IDS_STATUS_AUTOSAVED);
		}
		else {
			ClearPendingSaveRequest();
			MessageBoxResource(hwnd, IDS_MSG_AUTOSAVE_FAILED, MB_ICONERROR | MB_OK);
		}
		return 0;
	case WM_QUERYENDSESSION:
	case WM_CLOSE:
		{
			ViewerCommonContext commonCtx = { hMainWindow, hStatusBar, szAppName };
			ViewerCommon_SaveState(&commonCtx);
		}
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		g_exitThreads = TRUE;
		SetEvent(g_renderEvent);
		SetEvent(g_translateEvent);
		if (g_renderThread != NULL) {
			WaitForSingleObject(g_renderThread, 3000);
			CloseHandle(g_renderThread);
		}
		if (g_translateThread != NULL) {
			WaitForSingleObject(g_translateThread, 3000);
			CloseHandle(g_translateThread);
		}
		if (g_renderEvent != NULL) CloseHandle(g_renderEvent);
		if (g_translateEvent != NULL) CloseHandle(g_translateEvent);
		EnterCriticalSection(&g_sessionLock);
		FreeSession(g_session);
		g_session = NULL;
		LeaveCriticalSection(&g_sessionLock);
		DeleteCriticalSection(&g_sessionLock);
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK
RichEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = CallWindowProc(g_originalRichEditProc, hwnd, msg, wParam, lParam);
	switch (msg) {
	case WM_VSCROLL:
	case WM_MOUSEWHEEL:
	case WM_SIZE:
	case WM_LBUTTONUP:
	case WM_KEYUP:
		PostMessage(hMainWindow, WM_APP_VISIBLE_RANGE_CHANGED, 0, 0);
		break;
	}
	return result;
}

INT_PTR CALLBACK
About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message) {
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
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
	ViewerLoadedFile loadedFile;
	DocumentSession* session;
	DWORD generation;
	TCHAR szTitle[MAX_PATH + 20];

	if (lpszTextFileName == NULL) {
		return FALSE;
	}
	if (!ViewerCommon_LoadUtf8File(lpszTextFileName, &loadedFile)) {
		return FALSE;
	}

	generation = (DWORD)InterlockedIncrement(&g_loadGeneration);
	lstrcpyW(szCurrentFile, loadedFile.fullPath);
	szTitle[0] = 0;
	lstrcatW(szTitle, loadedFile.filePart);
	lstrcatW(szTitle, L" - ");
	lstrcatW(szTitle, szAppName);
	SendMessageW(hMainWindow, WM_SETTEXT, 0, (LPARAM)szTitle);

	if (g_translateToggle) {
		if (!LlmConfig_LoadFromExeDir(&g_llmConfig, g_lastConfigError, _countof(g_lastConfigError))) {
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)g_lastConfigError);
		}
	}
	else {
		memset(&g_llmConfig, 0, sizeof(g_llmConfig));
		g_lastConfigError[0] = L'\0';
	}

	session = CreateSession(&loadedFile, generation, g_translateToggle && g_llmConfig.valid);
	ViewerCommon_FreeLoadedFile(&loadedFile);
	if (session == NULL) {
		return FALSE;
	}

	ReplaceSession(session);
	ClearPendingSaveRequest();
	RenderInitialPreview(session);
	UpdateVisibleRangeFromScroll();
	if (session->translationEnabled) {
		QueueTranslation();
	}
	else {
		QueueRender();
	}
	return TRUE;
}

static BOOL
IsSessionTranslationComplete(const DocumentSession* session)
{
	int i;

	if (session == NULL || !session->translationEnabled) {
		return TRUE;
	}
	for (i = 0; i < session->paragraphCount; i++) {
		if (session->paragraphs[i].state != PARAGRAPH_DONE && session->paragraphs[i].state != PARAGRAPH_SKIPPED) {
			return FALSE;
		}
	}
	return TRUE;
}

static int
GetSessionTranslationPercent(const DocumentSession* session)
{
	size_t translatedChars = 0;
	size_t totalChars = 0;
	int i;

	if (session == NULL) {
		return 0;
	}

	for (i = 0; i < session->paragraphCount; i++) {
		totalChars += session->paragraphs[i].originalLen;
		if (session->paragraphs[i].state == PARAGRAPH_DONE || session->paragraphs[i].state == PARAGRAPH_SKIPPED) {
			translatedChars += session->paragraphs[i].originalLen;
		}
	}

	if (totalChars == 0) {
		return 100;
	}
	return (int)((translatedChars * 100 + (totalChars / 2)) / totalChars);
}

static BOOL
LoadResourceString(UINT stringId, WCHAR* buffer, size_t bufferCount)
{
	if (buffer == NULL || bufferCount == 0) {
		return FALSE;
	}
	buffer[0] = L'\0';
	return LoadStringW(hInst, stringId, buffer, (int)bufferCount) > 0;
}

static void
SetStatusBarTextResource(UINT stringId)
{
	WCHAR text[512];

	if (hStatusBar == NULL) {
		return;
	}
	if (LoadResourceString(stringId, text, _countof(text))) {
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)text);
	}
}

static int
MessageBoxResource(HWND owner, UINT textId, UINT type)
{
	WCHAR text[512];

	if (!LoadResourceString(textId, text, _countof(text))) {
		return 0;
	}
	return MessageBoxW(owner, text, szAppName, type);
}

static int
ShowPartialSaveDialog(int percentComplete)
{
	enum {
		ID_SAVE_LATER = 1002
	};
	WCHAR mainInstruction[128];
	WCHAR content[512];
	WCHAR fallback[512];
	WCHAR saveLater[64];
	TASKDIALOGCONFIG config;
	TASKDIALOG_BUTTON buttons[3];
	int buttonResult = IDCANCEL;
	HRESULT hr;

	if (!LoadResourceString(IDS_DIALOG_PARTIAL_MAIN, mainInstruction, _countof(mainInstruction))) {
		wcscpy_s(mainInstruction, _countof(mainInstruction), L"Save translated file");
	}
	if (!LoadResourceString(IDS_DIALOG_PARTIAL_CONTENT, content, _countof(content))) {
		wcscpy_s(content, _countof(content), L"Current document is about %d%% translated.");
	}
	if (!LoadResourceString(IDS_DIALOG_PARTIAL_FALLBACK, fallback, _countof(fallback))) {
		wcscpy_s(fallback, _countof(fallback), L"Current document is only partially translated.");
	}
	if (!LoadResourceString(IDS_BTN_SAVE_LATER, saveLater, _countof(saveLater))) {
		wcscpy_s(saveLater, _countof(saveLater), L"Save Later");
	}
	{
		WCHAR formatted[512];
		_snwprintf_s(formatted, _countof(formatted), _TRUNCATE, content, percentComplete);
		wcscpy_s(content, _countof(content), formatted);
	}

	ZeroMemory(&config, sizeof(config));
	ZeroMemory(buttons, sizeof(buttons));
	config.cbSize = sizeof(config);
	config.hwndParent = hMainWindow;
	config.hInstance = hInst;
	config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
	config.dwCommonButtons = 0;
	config.pszWindowTitle = szAppName;
	config.pszMainInstruction = mainInstruction;
	config.pszContent = content;
	buttons[0].nButtonID = IDOK;
	buttons[0].pszButtonText = L"OK";
	buttons[1].nButtonID = IDCANCEL;
	buttons[1].pszButtonText = L"Cancel";
	buttons[2].nButtonID = ID_SAVE_LATER;
	buttons[2].pszButtonText = saveLater;
	config.pButtons = buttons;
	config.cButtons = _countof(buttons);
	config.nDefaultButton = IDOK;

	hr = TaskDialogIndirect(&config, &buttonResult, NULL, NULL);
	if (FAILED(hr)) {
		return MessageBoxW(hMainWindow, fallback, szAppName, MB_ICONQUESTION | MB_YESNOCANCEL);
	}

	return buttonResult;
}

static void
ClearPendingSaveRequest(void)
{
	EnterCriticalSection(&g_sessionLock);
	g_pendingSavePath[0] = L'\0';
	g_pendingSaveGeneration = 0;
	LeaveCriticalSection(&g_sessionLock);
}

static void
StartDeferredSave(const WCHAR* filePath, DWORD generation)
{
	EnterCriticalSection(&g_sessionLock);
	wcsncpy_s(g_pendingSavePath, _countof(g_pendingSavePath), filePath, _TRUNCATE);
	g_pendingSaveGeneration = generation;
	LeaveCriticalSection(&g_sessionLock);
}

static BOOL
TryAutoSaveCompletedTranslation(DWORD generation)
{
	WCHAR savePath[PATH_BUFFER_SIZE];
	char* markdown = NULL;
	BOOL shouldSave = FALSE;
	BOOL success;

	savePath[0] = L'\0';
	EnterCriticalSection(&g_sessionLock);
	if (g_session != NULL &&
		g_session->generation == generation &&
		g_pendingSaveGeneration == generation &&
		g_pendingSavePath[0] != L'\0' &&
		IsSessionTranslationComplete(g_session) &&
		ComposeDisplayMarkdown(g_session, &markdown))
	{
		wcsncpy_s(savePath, _countof(savePath), g_pendingSavePath, _TRUNCATE);
		g_pendingSavePath[0] = L'\0';
		g_pendingSaveGeneration = 0;
		shouldSave = TRUE;
	}
	LeaveCriticalSection(&g_sessionLock);

	if (!shouldSave) {
		if (markdown != NULL) {
			free(markdown);
		}
		return FALSE;
	}

	success = WriteUtf8TextFile(savePath, markdown);
	free(markdown);
	PostMessage(hMainWindow, WM_APP_AUTO_SAVE_RESULT, (WPARAM)success, 0);
	return success;
}

static void
ReplaceSession(DocumentSession* session)
{
	EnterCriticalSection(&g_sessionLock);
	FreeSession(g_session);
	g_session = session;
	LeaveCriticalSection(&g_sessionLock);
}

static DocumentSession*
CreateSession(const ViewerLoadedFile* loadedFile, DWORD generation, BOOL translationEnabled)
{
	DocumentSession* session;
	const char* cursor;
	int index = 0;
	int capacity = 32;
	size_t offset = 0;

	session = (DocumentSession*)calloc(1, sizeof(DocumentSession));
	if (session == NULL) {
		return NULL;
	}

	session->generation = generation;
	session->sourceMarkdown = _strdup(loadedFile->contentUtf8);
	session->imagePath = _strdup(loadedFile->directoryUtf8);
	session->sourceLen = strlen(loadedFile->contentUtf8);
	session->translationEnabled = translationEnabled;
	session->hasTranslatedContent = FALSE;
	session->deferInitialBackgroundRender = translationEnabled;
	if (session->sourceMarkdown == NULL || session->imagePath == NULL) {
		FreeSession(session);
		return NULL;
	}

	session->paragraphs = (ParagraphInfo*)calloc(capacity, sizeof(ParagraphInfo));
	if (session->paragraphs == NULL) {
		FreeSession(session);
		return NULL;
	}

	cursor = session->sourceMarkdown;
	while (*cursor) {
		const char* next = strstr(cursor, "\n\n");
		size_t paraLen = next ? (size_t)(next - cursor) : strlen(cursor);
		if (index >= capacity) {
			ParagraphInfo* expanded;
			capacity *= 2;
			expanded = (ParagraphInfo*)realloc(session->paragraphs, capacity * sizeof(ParagraphInfo));
			if (expanded == NULL) {
				FreeSession(session);
				return NULL;
			}
			memset(expanded + index, 0, (capacity - index) * sizeof(ParagraphInfo));
			session->paragraphs = expanded;
		}
		session->paragraphs[index].original = (char*)malloc(paraLen + 1);
		if (session->paragraphs[index].original == NULL) {
			FreeSession(session);
			return NULL;
		}
		memcpy(session->paragraphs[index].original, cursor, paraLen);
		session->paragraphs[index].original[paraLen] = '\0';
		session->paragraphs[index].originalLen = paraLen;
		session->paragraphs[index].sourceStart = offset;
		session->paragraphs[index].sourceEnd = offset + paraLen;
		session->paragraphs[index].state = translationEnabled ? PARAGRAPH_PENDING : PARAGRAPH_SKIPPED;
		offset += paraLen;
		index++;
		if (!next) {
			break;
		}
		cursor = next + 2;
		offset += 2;
	}

	session->paragraphCount = index;
	if (session->paragraphCount == 0) {
		session->paragraphs[0].original = _strdup("");
		session->paragraphs[0].state = translationEnabled ? PARAGRAPH_PENDING : PARAGRAPH_SKIPPED;
		session->paragraphCount = 1;
	}
	session->visibleStart = 0;
	session->visibleEnd = (int)FindParagraphWindowEnd(session, 0, LLM_WINDOW_BYTES);
	return session;
}

static void
FreeSession(DocumentSession* session)
{
	int i;
	if (session == NULL) {
		return;
	}
	if (session->paragraphs != NULL) {
		for (i = 0; i < session->paragraphCount; i++) {
			if (session->paragraphs[i].original != NULL) free(session->paragraphs[i].original);
			if (session->paragraphs[i].translated != NULL) free(session->paragraphs[i].translated);
		}
		free(session->paragraphs);
	}
	if (session->sourceMarkdown != NULL) free(session->sourceMarkdown);
	if (session->imagePath != NULL) free(session->imagePath);
	free(session);
}

static BOOL
RenderInitialPreview(const DocumentSession* session)
{
	SETTEXTEX se;
	size_t previewLimit;
	size_t previewLen;
	char* fullDisplayMarkdown = NULL;
	const char* previewSource = NULL;
	size_t previewSourceLen = 0;
	char* previewMarkdown;
	char* previewRtf;

	se.codepage = 65001;
	se.flags = ST_DEFAULT;
	if (session == NULL) {
		return FALSE;
	}

	previewLimit = session->translationEnabled ? LLM_QUICK_BYTES : PREVIEW_BYTES;
	if (session->translationEnabled) {
	if (!ComposeDisplayMarkdown(session, &fullDisplayMarkdown)) {
			return FALSE;
		}
		previewSource = fullDisplayMarkdown;
		previewSourceLen = strlen(fullDisplayMarkdown);
	}
	else {
		previewSource = session->sourceMarkdown;
		previewSourceLen = session->sourceLen;
	}
	previewLen = ViewerCommon_ComputePreviewLength(previewSource, previewSourceLen, previewLimit, 2048);
	previewMarkdown = (char*)malloc(previewLen + 1);
	if (previewMarkdown == NULL) {
		if (fullDisplayMarkdown != NULL) free(fullDisplayMarkdown);
		return FALSE;
	}
	memcpy(previewMarkdown, previewSource, previewLen);
	previewMarkdown[previewLen] = '\0';

	previewRtf = markdown2rtf_ex(previewMarkdown, session->imagePath, 0);
	free(previewMarkdown);
	if (fullDisplayMarkdown != NULL) free(fullDisplayMarkdown);
	if (previewRtf == NULL) {
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)"");
		return FALSE;
	}

	SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)previewRtf);
	free(previewRtf);
	SetStatusBarTextResource(session->translationEnabled ? IDS_STATUS_PREVIEW_TRANSLATING : IDS_STATUS_PREVIEW_RENDERING);
	return TRUE;
}

static size_t
FindParagraphWindowEnd(const DocumentSession* session, int startParagraph, size_t targetChars)
{
	size_t total = 0;
	int i;
	if (session == NULL || session->paragraphCount == 0) {
		return 0;
	}
	for (i = startParagraph; i < session->paragraphCount; i++) {
		total += session->paragraphs[i].originalLen;
		if (i > startParagraph) total += 2;
		if (total >= targetChars) {
			return (size_t)i;
		}
	}
	return (size_t)(session->paragraphCount - 1);
}


static BOOL
BuildSegmentedSliceMarkdown(const DocumentSession* session, int startParagraph, int endParagraph, TranslationSegment* segments, int segmentCount, char** markdownOut)
{
	TextBuffer markdown = { 0 };
	int segmentIndex = 0;
	int paragraphIndex;

	*markdownOut = NULL;
	for (paragraphIndex = startParagraph; paragraphIndex <= endParagraph; paragraphIndex++) {
		size_t startOffset = 0;
		size_t paragraphLen = session->paragraphs[paragraphIndex].originalLen;
		const char* paragraphText = session->paragraphs[paragraphIndex].original;

		if (paragraphLen > LLM_SINGLE_PARAGRAPH_HARD_LIMIT) {
			while (startOffset < paragraphLen && segmentIndex < segmentCount) {
				size_t splitPoint = FindSplitPoint(paragraphText, startOffset, paragraphLen, LLM_OVERSIZE_SPLIT_TARGET, LLM_OVERSIZE_SPLIT_MAX);
				if (splitPoint <= startOffset) {
					splitPoint = paragraphLen;
				}
				size_t chunkLen = splitPoint - startOffset;
				if (!TextBuffer_Append(&markdown, segments[segmentIndex].marker) ||
					!TextBuffer_Append(&markdown, "\n") ||
					!TextBuffer_AppendN(&markdown, paragraphText + startOffset, chunkLen) ||
					!TextBuffer_Append(&markdown, "\n\n")) {
					TextBuffer_Free(&markdown);
					return FALSE;
				}
				startOffset = splitPoint;
				segmentIndex++;
			}
		}
		else {
			if (segmentIndex >= segmentCount ||
				!TextBuffer_Append(&markdown, segments[segmentIndex].marker) ||
				!TextBuffer_Append(&markdown, "\n") ||
				!TextBuffer_AppendN(&markdown, paragraphText, paragraphLen) ||
				!TextBuffer_Append(&markdown, "\n\n")) {
				TextBuffer_Free(&markdown);
				return FALSE;
			}
			segmentIndex++;
		}
	}

	if (segmentIndex != segmentCount) {
		TextBuffer_Free(&markdown);
		return FALSE;
	}
	*markdownOut = markdown.data;
	return TRUE;
}

static BOOL
ApplySliceTranslation(DocumentSession* session, const TranslationSlice* slice, const char* translatedMarkdown, BOOL skippedByLanguage)
{
	TextBuffer* paragraphBuffers;
	char** segmentTexts;
	char* markdownCopy;
	int paragraphSpan;
	int i;
	BOOL success = FALSE;

	if (session == NULL || slice == NULL) {
		return FALSE;
	}

	if (skippedByLanguage) {
		for (i = slice->startParagraph; i <= slice->endParagraph; i++) {
			if (session->paragraphs[i].translated != NULL) free(session->paragraphs[i].translated);
			session->paragraphs[i].translated = _strdup(session->paragraphs[i].original);
			session->paragraphs[i].translatedLen = session->paragraphs[i].originalLen;
			session->paragraphs[i].state = PARAGRAPH_SKIPPED;
			session->paragraphs[i].failCount = 0;
		}
		session->hasTranslatedContent = TRUE;
		session->deferInitialBackgroundRender = FALSE;
		return TRUE;
	}

	markdownCopy = _strdup(translatedMarkdown);
	if (markdownCopy == NULL) {
		return FALSE;
	}
	TrimWrappingCodeFence(markdownCopy);
	paragraphSpan = slice->endParagraph - slice->startParagraph + 1;
	paragraphBuffers = (TextBuffer*)calloc(paragraphSpan, sizeof(TextBuffer));
	segmentTexts = (char**)calloc(slice->segmentCount, sizeof(char*));
	if (paragraphBuffers == NULL || segmentTexts == NULL) {
		free(markdownCopy);
		free(paragraphBuffers);
		free(segmentTexts);
		return FALSE;
	}
	if (!ExtractMarkedSegmentTexts(slice, markdownCopy, segmentTexts)) {
		goto cleanup;
	}

	for (i = 0; i < slice->segmentCount; i++) {
		int paragraphOffset = slice->segments[i].paragraphIndex - slice->startParagraph;
		TrimBoundaryNewlines(segmentTexts[i]);
		if (!TextBuffer_Append(&paragraphBuffers[paragraphOffset], segmentTexts[i])) {
			goto cleanup;
		}
	}

	for (i = 0; i < paragraphSpan; i++) {
		int paragraphIndex = slice->startParagraph + i;
		char* translatedText = paragraphBuffers[i].data;
		if (translatedText == NULL) {
			goto cleanup;
		}
		if (session->paragraphs[paragraphIndex].translated != NULL) free(session->paragraphs[paragraphIndex].translated);
		session->paragraphs[paragraphIndex].translated = _strdup(translatedText);
		if (session->paragraphs[paragraphIndex].translated == NULL) {
			goto cleanup;
		}
		session->paragraphs[paragraphIndex].translatedLen = strlen(session->paragraphs[paragraphIndex].translated);
		session->paragraphs[paragraphIndex].state = PARAGRAPH_DONE;
		session->paragraphs[paragraphIndex].failCount = 0;
	}

	session->hasTranslatedContent = TRUE;
	session->deferInitialBackgroundRender = FALSE;
	success = TRUE;

cleanup:
	if (!success) {
		for (i = slice->startParagraph; i <= slice->endParagraph; i++) {
			if (session->paragraphs[i].state == PARAGRAPH_INFLIGHT) {
				session->paragraphs[i].state = PARAGRAPH_PENDING;
				if (session->paragraphs[i].failCount < 255) {
					session->paragraphs[i].failCount++;
				}
			}
		}
	}
	for (i = 0; i < paragraphSpan; i++) {
		TextBuffer_Free(&paragraphBuffers[i]);
	}
	free(paragraphBuffers);
	free(segmentTexts);
	free(markdownCopy);
	return success;
}

static void
TrimWrappingCodeFence(char* markdown)
{
	char* start;
	char* firstNewline;
	char* endFence;
	size_t innerLen;

	if (markdown == NULL) {
		return;
	}

	start = markdown;
	while (*start == '\r' || *start == '\n' || *start == ' ' || *start == '\t') {
		start++;
	}
	if (strncmp(start, "```", 3) != 0) {
		if (start != markdown) {
			memmove(markdown, start, strlen(start) + 1);
		}
		return;
	}

	firstNewline = strpbrk(start, "\r\n");
	if (firstNewline == NULL) {
		return;
	}
	while (*firstNewline == '\r' || *firstNewline == '\n') {
		firstNewline++;
	}

	endFence = strstr(firstNewline, "\n```");
	if (endFence == NULL && firstNewline > markdown) {
		endFence = strstr(firstNewline, "\r\n```");
	}
	if (endFence == NULL) {
		return;
	}

	while (endFence > firstNewline && (endFence[-1] == '\r' || endFence[-1] == '\n')) {
		endFence--;
	}
	innerLen = (size_t)(endFence - firstNewline);
	memmove(markdown, firstNewline, innerLen);
	markdown[innerLen] = '\0';
}

static void
TrimBoundaryNewlines(char* text)
{
	char* start = text;
	char* end;

	if (text == NULL) {
		return;
	}
	while (*start == '\r' || *start == '\n') {
		start++;
	}
	if (start != text) {
		memmove(text, start, strlen(start) + 1);
	}
	end = text + strlen(text);
	while (end > text && (end[-1] == '\r' || end[-1] == '\n')) {
		end--;
	}
	*end = '\0';
}

static BOOL
TextBuffer_Reserve(TextBuffer* buffer, size_t extra)
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
TextBuffer_AppendN(TextBuffer* buffer, const char* text, size_t textLen)
{
	if (!TextBuffer_Reserve(buffer, textLen)) {
		return FALSE;
	}
	memcpy(buffer->data + buffer->length, text, textLen);
	buffer->length += textLen;
	buffer->data[buffer->length] = '\0';
	return TRUE;
}

static BOOL
TextBuffer_Append(TextBuffer* buffer, const char* text)
{
	return TextBuffer_AppendN(buffer, text, strlen(text));
}

static void
TextBuffer_Free(TextBuffer* buffer)
{
	if (buffer != NULL && buffer->data != NULL) {
		free(buffer->data);
		buffer->data = NULL;
		buffer->length = 0;
		buffer->capacity = 0;
	}
}

static size_t
FindSplitPoint(const char* text, size_t start, size_t textLen, size_t targetLen, size_t maxLen)
{
	size_t upperBound;
	size_t preferred;
	size_t pos;

	if (start >= textLen) {
		return textLen;
	}

	upperBound = start + maxLen;
	if (upperBound > textLen) {
		upperBound = textLen;
	}
	preferred = start + targetLen;
	if (preferred > upperBound) {
		preferred = upperBound;
	}

	for (pos = upperBound; pos > preferred; pos--) {
		if (text[pos - 1] == '\n') {
			return pos;
		}
	}
	for (pos = upperBound; pos > start + 1; pos--) {
		unsigned char ch = (unsigned char)text[pos - 1];
		if (ch == '\n' || ch == ' ' || ch == '\t' || ch == '.' || ch == '!' || ch == '?' || ch == ';' || ch == ',' || ch == ':') {
			return pos;
		}
		if (pos >= 3 && (unsigned char)text[pos - 3] == 0xE3 && (unsigned char)text[pos - 2] == 0x80 &&
			((unsigned char)text[pos - 1] == 0x82 || (unsigned char)text[pos - 1] == 0x81 || (unsigned char)text[pos - 1] == 0x9F || (unsigned char)text[pos - 1] == 0x9B)) {
			return pos;
		}
	}

	pos = upperBound;
	while (pos > start && (((unsigned char)text[pos] & 0xC0) == 0x80)) {
		pos--;
	}
	if (pos <= start) {
		pos = upperBound;
	}
	return pos;
}

static size_t
CountOversizeSegments(const char* text, size_t textLen)
{
	size_t count = 0;
	size_t start = 0;

	while (start < textLen) {
		size_t next = FindSplitPoint(text, start, textLen, LLM_OVERSIZE_SPLIT_TARGET, LLM_OVERSIZE_SPLIT_MAX);
		if (next <= start) {
			next = textLen;
		}
		start = next;
		count++;
	}
	return count;
}

static BOOL
ExtractMarkedSegmentTexts(const TranslationSlice* slice, char* translatedMarkdown, char** segmentTexts)
{
	char* cursor = translatedMarkdown;
	int i;

	for (i = 0; i < slice->segmentCount; i++) {
		char* markerPos = strstr(cursor, slice->segments[i].marker);
		char* contentStart;
		char* nextMarker = NULL;

		if (markerPos == NULL) {
			return FALSE;
		}
		contentStart = markerPos + strlen(slice->segments[i].marker);
		while (*contentStart == '\r' || *contentStart == '\n') {
			contentStart++;
		}
		if (i + 1 < slice->segmentCount) {
			char* trimEnd;
			nextMarker = strstr(contentStart, slice->segments[i + 1].marker);
			if (nextMarker == NULL) {
				return FALSE;
			}
			trimEnd = nextMarker;
			while (trimEnd > contentStart && (trimEnd[-1] == '\r' || trimEnd[-1] == '\n')) {
				trimEnd--;
			}
			*trimEnd = '\0';
			cursor = nextMarker;
		}
		segmentTexts[i] = contentStart;
	}
	return TRUE;
}

static BOOL
ComposeDisplayMarkdown(const DocumentSession* session, char** markdownOut)
{
	size_t total = 0;
	char* markdown;
	size_t offset = 0;
	int i;

	*markdownOut = NULL;
	if (session == NULL) {
		return FALSE;
	}
	for (i = 0; i < session->paragraphCount; i++) {
		const ParagraphInfo* paragraph = &session->paragraphs[i];
		const char* text = (paragraph->translated != NULL) ? paragraph->translated : paragraph->original;
		total += strlen(text);
		if (i > 0) total += 2;
	}
	markdown = (char*)malloc(total + 1);
	if (markdown == NULL) return FALSE;

	for (i = 0; i < session->paragraphCount; i++) {
		const ParagraphInfo* paragraph = &session->paragraphs[i];
		const char* text = (paragraph->translated != NULL) ? paragraph->translated : paragraph->original;
		size_t textLen = strlen(text);
		if (i > 0) {
			memcpy(markdown + offset, "\n\n", 2);
			offset += 2;
		}
		memcpy(markdown + offset, text, textLen);
		offset += textLen;
	}
	markdown[offset] = '\0';
	*markdownOut = markdown;
	return TRUE;
}

static BOOL
BuildRenderSnapshot(RenderSnapshot* snapshot)
{
	DocumentSession* session;
	memset(snapshot, 0, sizeof(*snapshot));

	EnterCriticalSection(&g_sessionLock);
	session = g_session;
	if (session == NULL) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}
	snapshot->generation = session->generation;
	if (!ComposeDisplayMarkdown(session, &snapshot->markdown)) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}
	snapshot->imagePath = _strdup(session->imagePath);
	LeaveCriticalSection(&g_sessionLock);

	return snapshot->markdown != NULL && snapshot->imagePath != NULL;
}

static void
FreeRenderSnapshot(RenderSnapshot* snapshot)
{
	if (snapshot->markdown != NULL) free(snapshot->markdown);
	if (snapshot->imagePath != NULL) free(snapshot->imagePath);
	memset(snapshot, 0, sizeof(*snapshot));
}

DWORD WINAPI
RenderThreadProc(LPVOID lpParam)
{
	UNREFERENCED_PARAMETER(lpParam);
	while (!g_exitThreads) {
		RenderSnapshot snapshot;
		char* rtf;
		WaitForSingleObject(g_renderEvent, INFINITE);
		if (g_exitThreads) break;
		if (!BuildRenderSnapshot(&snapshot)) continue;
		rtf = markdown2rtf(snapshot.markdown, snapshot.imagePath);
		if (rtf != NULL) {
			PostMessage(hMainWindow, WM_APP_RENDER_COMPLETE, (WPARAM)rtf, (LPARAM)snapshot.generation);
		}
		FreeRenderSnapshot(&snapshot);
	}
	return 0;
}

static BOOL
SelectNextTranslationSlice(TranslationSlice* slice)
{
	DocumentSession* session;
	int start = -1;
	int end = -1;
	int searchStart;
	int searchEnd;
	int buildEnd;
	size_t charLimit;
	int paragraphLimit;
	size_t totalChars = 0;
	int paragraphCount = 0;
	size_t segmentCount = 0;
	int i;

	memset(slice, 0, sizeof(*slice));
	EnterCriticalSection(&g_sessionLock);
	session = g_session;
	if (session == NULL || !session->translationEnabled) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}

	searchStart = session->visibleStart - 2;
	if (searchStart < 0) searchStart = 0;
	searchEnd = session->visibleEnd + 2;
	if (searchEnd >= session->paragraphCount) searchEnd = session->paragraphCount - 1;
	buildEnd = searchEnd;

	for (i = searchStart; i <= searchEnd && i < session->paragraphCount; i++) {
		if (session->paragraphs[i].state == PARAGRAPH_PENDING) {
			start = i;
			break;
		}
	}
	if (start < 0) {
		for (i = searchEnd + 1; i < session->paragraphCount; i++) {
			if (session->paragraphs[i].state == PARAGRAPH_PENDING) {
				start = i;
				buildEnd = session->paragraphCount - 1;
				break;
			}
		}
	}
	if (start < 0) {
		for (i = 0; i < searchStart; i++) {
			if (session->paragraphs[i].state == PARAGRAPH_PENDING) {
				start = i;
				buildEnd = searchStart - 1;
				break;
			}
		}
	}
	if (start < 0) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}

	charLimit = session->hasTranslatedContent ? LLM_SLICE_CHARS : LLM_INITIAL_SLICE_CHARS;
	paragraphLimit = session->hasTranslatedContent ? LLM_SLICE_PARAGRAPHS : LLM_INITIAL_SLICE_PARAGRAPHS;
	if (session->paragraphs[start].failCount > 0) {
		charLimit = LLM_INITIAL_SLICE_CHARS;
		paragraphLimit = 1;
	}

	for (i = start; i <= buildEnd && i < session->paragraphCount; i++) {
		size_t paragraphLen;
		if (session->paragraphs[i].state != PARAGRAPH_PENDING) {
			if (i == start) {
				continue;
			}
			break;
		}
		if (paragraphCount >= paragraphLimit && paragraphCount > 0) {
			break;
		}

		paragraphLen = session->paragraphs[i].originalLen;
		if (paragraphLen > LLM_SINGLE_PARAGRAPH_HARD_LIMIT) {
			if (paragraphCount > 0) {
				break;
			}
			segmentCount += CountOversizeSegments(session->paragraphs[i].original, paragraphLen);
			totalChars = paragraphLen;
			paragraphCount = 1;
			end = i;
			break;
		}
		if (paragraphCount > 0 && totalChars + paragraphLen > charLimit) {
			break;
		}
		if (paragraphCount == 0 || paragraphLen <= charLimit || session->paragraphs[i].failCount == 0) {
			totalChars += paragraphLen;
			paragraphCount++;
			segmentCount++;
			end = i;
		}
		if (paragraphCount == 1 && paragraphLen > charLimit) {
			break;
		}
	}

	if (end < start || segmentCount == 0) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}

	slice->generation = session->generation;
	slice->startParagraph = start;
	slice->endParagraph = end;
	slice->segmentCount = (int)segmentCount;
	slice->segments = (TranslationSegment*)calloc(segmentCount, sizeof(TranslationSegment));
	if (slice->segments == NULL) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}
	{
		int segmentIndex = 0;
		for (i = start; i <= end; i++) {
			size_t paragraphLen = session->paragraphs[i].originalLen;
			if (paragraphLen > LLM_SINGLE_PARAGRAPH_HARD_LIMIT) {
				size_t startOffset = 0;
				int chunkIndex = 0;
				while (startOffset < paragraphLen && segmentIndex < (int)segmentCount) {
					size_t splitPoint = FindSplitPoint(session->paragraphs[i].original, startOffset, paragraphLen, LLM_OVERSIZE_SPLIT_TARGET, LLM_OVERSIZE_SPLIT_MAX);
					if (splitPoint <= startOffset) {
						splitPoint = paragraphLen;
					}
					slice->segments[segmentIndex].paragraphIndex = i;
					sprintf_s(slice->segments[segmentIndex].marker, sizeof(slice->segments[segmentIndex].marker), "<<<P%05d_S%03d>>>", i, chunkIndex);
					startOffset = splitPoint;
					segmentIndex++;
					chunkIndex++;
				}
			}
			else {
				slice->segments[segmentIndex].paragraphIndex = i;
				sprintf_s(slice->segments[segmentIndex].marker, sizeof(slice->segments[segmentIndex].marker), "<<<P%05d>>>", i);
				segmentIndex++;
			}
		}
	}
	if (!BuildSegmentedSliceMarkdown(session, start, end, slice->segments, slice->segmentCount, &slice->markdown)) {
		free(slice->segments);
		slice->segments = NULL;
		slice->segmentCount = 0;
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}
	for (i = start; i <= end; i++) {
		session->paragraphs[i].state = PARAGRAPH_INFLIGHT;
	}
	LeaveCriticalSection(&g_sessionLock);
	return TRUE;
}

static void
FreeTranslationSlice(TranslationSlice* slice)
{
	if (slice->markdown != NULL) free(slice->markdown);
	if (slice->segments != NULL) free(slice->segments);
	memset(slice, 0, sizeof(*slice));
}

DWORD WINAPI
TranslateThreadProc(LPVOID lpParam)
{
	UNREFERENCED_PARAMETER(lpParam);
	while (!g_exitThreads) {
		WaitForSingleObject(g_translateEvent, INFINITE);
		if (g_exitThreads) break;
		for (;;) {
			TranslationSlice slice;
			char* translated = NULL;
			BOOL skipped = FALSE;
			BOOL applied = FALSE;
			if (!SelectNextTranslationSlice(&slice)) break;
			if (LlmTranslate_MaybeTranslateMarkdown(&g_llmConfig, slice.markdown, &translated, &skipped)) {
				EnterCriticalSection(&g_sessionLock);
				if (g_session != NULL && g_session->generation == slice.generation) {
					applied = ApplySliceTranslation(g_session, &slice, translated, skipped);
				}
				LeaveCriticalSection(&g_sessionLock);
				if (applied) {
					QueueRender();
					TryAutoSaveCompletedTranslation(slice.generation);
				}
				else {
					if (translated != NULL) free(translated);
					FreeTranslationSlice(&slice);
					break;
				}
			}
			else {
				int i;
				EnterCriticalSection(&g_sessionLock);
				if (g_session != NULL && g_session->generation == slice.generation) {
					for (i = slice.startParagraph; i <= slice.endParagraph; i++) {
						g_session->paragraphs[i].state = PARAGRAPH_PENDING;
					}
				}
				LeaveCriticalSection(&g_sessionLock);
				if (translated != NULL) free(translated);
				FreeTranslationSlice(&slice);
				break;
			}
			if (translated != NULL) free(translated);
			FreeTranslationSlice(&slice);
		}
	}
	return 0;
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
	if (GetOpenFileNameW(&ofn) == TRUE) {
		FileOpen(ofn.lpstrFile);
	}
}

static BOOL
BuildLanguageTagSuffix(const char* targetLangUtf8, WCHAR* buffer, size_t bufferCount)
{
	WCHAR* targetLang;
	size_t i;
	size_t out = 0;
	int subtagIndex = 0;
	size_t subtagLen = 0;

	if (buffer == NULL || bufferCount == 0) {
		return FALSE;
	}
	buffer[0] = L'\0';

	if (targetLangUtf8 == NULL || targetLangUtf8[0] == '\0') {
		return FALSE;
	}

	targetLang = ViewerCommon_ToWide(targetLangUtf8);
	if (targetLang == NULL) {
		return FALSE;
	}

	for (i = 0; targetLang[i] != L'\0' && out + 1 < bufferCount; i++) {
		WCHAR ch = targetLang[i];
		if (ch == L'_' || ch == L' ') {
			ch = L'-';
		}

		if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')) {
			if (subtagIndex == 0) {
				buffer[out++] = (WCHAR)towlower(ch);
			}
			else if (subtagLen == 0 && targetLang[i + 1] != L'\0') {
				if (targetLang[i + 1] == L'-' || targetLang[i + 1] == L'_' || targetLang[i + 1] == L' ') {
					buffer[out++] = (WCHAR)towupper(ch);
				}
				else {
					buffer[out++] = (WCHAR)towlower(ch);
				}
			}
			else if (subtagIndex == 1 && subtagLen < 2) {
				buffer[out++] = (WCHAR)towupper(ch);
			}
			else {
				buffer[out++] = (WCHAR)towlower(ch);
			}
			subtagLen++;
		}
		else if (ch >= L'0' && ch <= L'9') {
			buffer[out++] = ch;
			subtagLen++;
		}
		else if (ch == L'-') {
			if (out == 0 || buffer[out - 1] == L'-') {
				continue;
			}
			buffer[out++] = L'-';
			subtagIndex++;
			subtagLen = 0;
		}
	}

	free(targetLang);
	while (out > 0 && buffer[out - 1] == L'-') {
		out--;
	}
	buffer[out] = L'\0';
	return out > 0;
}

static BOOL
GetDefaultSavePath(WCHAR* buffer, size_t bufferCount)
{
	WCHAR drive[_MAX_DRIVE];
	WCHAR dir[_MAX_DIR];
	WCHAR name[_MAX_FNAME];
	WCHAR ext[_MAX_EXT];
	WCHAR langSuffix[96];

	if (buffer == NULL || bufferCount == 0 || szCurrentFile[0] == L'\0') {
		return FALSE;
	}

	_wsplitpath_s(szCurrentFile, drive, _countof(drive), dir, _countof(dir), name, _countof(name), ext, _countof(ext));
	if (name[0] == L'\0') {
		return FALSE;
	}

	if (!BuildLanguageTagSuffix(g_llmConfig.targetLang, langSuffix, _countof(langSuffix))) {
		wcscpy_s(langSuffix, _countof(langSuffix), L"translated");
	}

	_snwprintf_s(buffer, bufferCount, _TRUNCATE, L"%s%s%s_%s%s", drive, dir, name, langSuffix, ext[0] != L'\0' ? ext : L".md");
	return TRUE;
}

static BOOL
PromptSavePath(WCHAR* pathBuffer, DWORD pathBufferCount)
{
	OPENFILENAME ofn;
	WCHAR defaultPath[PATH_BUFFER_SIZE];

	if (pathBuffer == NULL || pathBufferCount == 0) {
		return FALSE;
	}

	pathBuffer[0] = L'\0';
	defaultPath[0] = L'\0';
	if (!GetDefaultSavePath(defaultPath, _countof(defaultPath))) {
		return FALSE;
	}

	wcsncpy_s(pathBuffer, pathBufferCount, defaultPath, _TRUNCATE);
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hMainWindow;
	ofn.lpstrFile = pathBuffer;
	ofn.nMaxFile = pathBufferCount;
	ofn.lpstrFilter = L"Markdown\0*.md\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	ofn.lpstrDefExt = L"md";
	return GetSaveFileNameW(&ofn) == TRUE;
}

static BOOL
WriteUtf8TextFile(const WCHAR* filePath, const char* textUtf8)
{
	HANDLE hFile;
	DWORD bytesWritten = 0;
	DWORD textLen;

	if (filePath == NULL || filePath[0] == L'\0' || textUtf8 == NULL) {
		return FALSE;
	}

	hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	textLen = (DWORD)strlen(textUtf8);
	if (textLen > 0 && !WriteFile(hFile, textUtf8, textLen, &bytesWritten, NULL)) {
		CloseHandle(hFile);
		return FALSE;
	}
	CloseHandle(hFile);
	return bytesWritten == textLen;
}

static BOOL
SaveCurrentSessionToPath(const WCHAR* filePath)
{
	char* markdown = NULL;
	BOOL success = FALSE;

	EnterCriticalSection(&g_sessionLock);
	if (g_session != NULL) {
		success = ComposeDisplayMarkdown(g_session, &markdown);
	}
	LeaveCriticalSection(&g_sessionLock);

	if (!success || markdown == NULL) {
		if (markdown != NULL) {
			free(markdown);
		}
		return FALSE;
	}

	success = WriteUtf8TextFile(filePath, markdown);
	free(markdown);
	return success;
}

static BOOL
HandleSaveTranslatedCommand(void)
{
	WCHAR savePath[PATH_BUFFER_SIZE];
	DWORD generation = 0;
	int percent = 0;
	BOOL translationEnabled = FALSE;
	BOOL translationComplete = FALSE;
	int action;

	savePath[0] = L'\0';

	EnterCriticalSection(&g_sessionLock);
	if (g_session == NULL) {
		LeaveCriticalSection(&g_sessionLock);
		MessageBoxResource(hMainWindow, IDS_MSG_NO_DOCUMENT, MB_ICONINFORMATION | MB_OK);
		return FALSE;
	}
	generation = g_session->generation;
	translationEnabled = g_session->translationEnabled;
	translationComplete = IsSessionTranslationComplete(g_session);
	percent = GetSessionTranslationPercent(g_session);
	LeaveCriticalSection(&g_sessionLock);

	if (!translationEnabled || translationComplete) {
		if (!PromptSavePath(savePath, _countof(savePath))) {
			return FALSE;
		}
		if (!SaveCurrentSessionToPath(savePath)) {
			MessageBoxResource(hMainWindow, IDS_MSG_SAVE_FAILED, MB_ICONERROR | MB_OK);
			return FALSE;
		}
		SetStatusBarTextResource(IDS_STATUS_TRANSLATED_SAVED);
		return TRUE;
	}

	{
		action = ShowPartialSaveDialog(percent);
	}

	if (action == IDCANCEL || action == IDNO) {
		return FALSE;
	}

	if (!PromptSavePath(savePath, _countof(savePath))) {
		return FALSE;
	}

	if (action == IDOK || action == IDYES) {
		if (!SaveCurrentSessionToPath(savePath)) {
			MessageBoxResource(hMainWindow, IDS_MSG_SAVE_FAILED, MB_ICONERROR | MB_OK);
			return FALSE;
		}
		SetStatusBarTextResource(IDS_STATUS_PARTIAL_SAVED);
		return TRUE;
	}

	StartDeferredSave(savePath, generation);
	if (TryAutoSaveCompletedTranslation(generation)) {
		return TRUE;
	}
	SetStatusBarTextResource(IDS_STATUS_DEFERRED_SAVE);
	QueueTranslation();
	return TRUE;
}

BOOL
CreateToolBar()
{
	int iDpi = GetDpiForWindow(hMainWindow);
	const int ImageListID = 0;
	const int numButtons = 5;
	int bitmapSize = (iDpi / 3) - 16;
	const DWORD buttonStyles = BTNS_AUTOSIZE;
	HIMAGELIST hImageList = NULL;
	HWND hWndToolbar;
	HICON hOpenLarge;
	HICON hRefreshLarge;
	HICON hSaveLarge = NULL;
	HICON hTranslateIcon;
	HICON hCloseIcon;
	int iOpenIndex;
	int iRefreshIndex;
	int iSaveIndex;
	int iTranslateIndex;
	int iCloseIndex;
	TBBUTTON tbButtons[9];

	if (bitmapSize < 16) bitmapSize = 16;
	hWndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | TBSTYLE_WRAPABLE, 0, 0, 0, 0, hMainWindow, NULL, hInst, NULL);
	if (hWndToolbar == NULL) {
		ViewerCommon_ShowLastError(L"Toolbar Creation Failed!");
		return FALSE;
	}

	hImageList = ImageList_Create(bitmapSize, bitmapSize, ILC_COLOR32 | ILC_MASK, numButtons, 0);
	SendMessage(hWndToolbar, TB_SETIMAGELIST, (WPARAM)ImageListID, (LPARAM)hImageList);
	SendMessage(hWndToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(bitmapSize, bitmapSize));

	ExtractIconEx(L"shell32.dll", 4, &hOpenLarge, NULL, 1);
	ExtractIconEx(L"shell32.dll", 238, &hRefreshLarge, NULL, 1);
	{
		SHSTOCKICONINFO stockIconInfo;
		ZeroMemory(&stockIconInfo, sizeof(stockIconInfo));
		stockIconInfo.cbSize = sizeof(stockIconInfo);
		if (SUCCEEDED(SHGetStockIconInfo(SIID_DRIVE35, SHGSI_ICON | SHGSI_LARGEICON, &stockIconInfo))) {
			hSaveLarge = stockIconInfo.hIcon;
		}
	}
	hTranslateIcon = (HICON)LoadImage(NULL, IDI_INFORMATION, IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);
	hCloseIcon = (HICON)LoadImage(NULL, IDI_ERROR, IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);

	iOpenIndex = ImageList_AddIcon(hImageList, hOpenLarge);
	iRefreshIndex = ImageList_AddIcon(hImageList, hRefreshLarge);
	iSaveIndex = ImageList_AddIcon(hImageList, hSaveLarge);
	iTranslateIndex = ImageList_AddIcon(hImageList, hTranslateIcon);
	iCloseIndex = ImageList_AddIcon(hImageList, hCloseIcon);

	DestroyIcon(hOpenLarge);
	DestroyIcon(hRefreshLarge);
	if (hSaveLarge != NULL) {
		DestroyIcon(hSaveLarge);
	}
	SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	ZeroMemory(tbButtons, sizeof(tbButtons));
	tbButtons[0].iBitmap = iOpenIndex;
	tbButtons[0].idCommand = IDM_FILE_OPEN;
	tbButtons[0].fsState = TBSTATE_ENABLED;
	tbButtons[0].fsStyle = (BYTE)buttonStyles;
	tbButtons[1].fsState = TBSTATE_ENABLED;
	tbButtons[1].fsStyle = BTNS_SEP;
	tbButtons[2].iBitmap = iSaveIndex;
	tbButtons[2].idCommand = IDM_FILE_SAVE_TRANSLATED;
	tbButtons[2].fsState = TBSTATE_ENABLED;
	tbButtons[2].fsStyle = (BYTE)buttonStyles;
	tbButtons[3].fsState = TBSTATE_ENABLED;
	tbButtons[3].fsStyle = BTNS_SEP;
	tbButtons[4].iBitmap = iRefreshIndex;
	tbButtons[4].idCommand = IDM_FILE_REFRESH;
	tbButtons[4].fsState = TBSTATE_ENABLED;
	tbButtons[4].fsStyle = (BYTE)buttonStyles;
	tbButtons[5].fsState = TBSTATE_ENABLED;
	tbButtons[5].fsStyle = BTNS_SEP;
	tbButtons[6].iBitmap = iTranslateIndex;
	tbButtons[6].idCommand = IDM_VIEW_TRANSLATE;
	tbButtons[6].fsState = TBSTATE_ENABLED | TBSTATE_CHECKED;
	tbButtons[6].fsStyle = BTNS_CHECK | (BYTE)buttonStyles;
	tbButtons[7].fsState = TBSTATE_ENABLED;
	tbButtons[7].fsStyle = BTNS_SEP;
	tbButtons[8].iBitmap = iCloseIndex;
	tbButtons[8].idCommand = IDM_FILE_EXIT;
	tbButtons[8].fsState = TBSTATE_ENABLED;
	tbButtons[8].fsStyle = (BYTE)buttonStyles;
	SendMessage(hWndToolbar, TB_ADDBUTTONS, (WPARAM)_countof(tbButtons), (LPARAM)&tbButtons);
	SendMessage(hWndToolbar, TB_AUTOSIZE, 0, 0);
	SendMessage(hWndToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(bitmapSize + MulDiv(12, iDpi, 96), bitmapSize + MulDiv(12, iDpi, 96)));
	ShowWindow(hWndToolbar, TRUE);
	hToolBar = hWndToolbar;
	return TRUE;
}

BOOL
CreateStatusBar()
{
	hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, -100, -100, 10, 10, hMainWindow, NULL, hInst, NULL);
	return TRUE;
}

static void
UpdateVisibleRangeFromScroll(void)
{
	SCROLLINFO si;
	double ratio;
	size_t startOffset;
	size_t span;
	int startParagraph;
	int endParagraph;
	BOOL hasPending = FALSE;

	EnterCriticalSection(&g_sessionLock);
	if (g_session == NULL) {
		LeaveCriticalSection(&g_sessionLock);
		return;
	}

	ZeroMemory(&si, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	if (!GetScrollInfo(hRichEdit, SB_VERT, &si)) {
		LeaveCriticalSection(&g_sessionLock);
		return;
	}

	ratio = 0.0;
	if (si.nMax > (int)si.nPage) {
		ratio = (double)si.nPos / (double)(si.nMax - (int)si.nPage + 1);
	}
	startOffset = (size_t)(ratio * (double)g_session->sourceLen);
	span = g_session->translationEnabled ? LLM_WINDOW_BYTES : PREVIEW_BYTES;
	startParagraph = FindParagraphForOffset(g_session, startOffset);
	endParagraph = FindParagraphForOffset(g_session, startOffset + span);
	if (endParagraph < startParagraph) {
		endParagraph = startParagraph;
	}
	g_session->visibleStart = startParagraph;
	g_session->visibleEnd = endParagraph;
	hasPending = VisibleRangeHasPending(g_session);
	LeaveCriticalSection(&g_sessionLock);

	if (hasPending) {
		QueueTranslation();
		EnterCriticalSection(&g_sessionLock);
		if (g_session != NULL && !g_session->deferInitialBackgroundRender) {
			LeaveCriticalSection(&g_sessionLock);
			QueueRender();
		}
		else {
			LeaveCriticalSection(&g_sessionLock);
		}
	}
}

static int
FindParagraphForOffset(const DocumentSession* session, size_t offset)
{
	int i;
	if (session == NULL) {
		return 0;
	}
	for (i = 0; i < session->paragraphCount; i++) {
		if (offset <= session->paragraphs[i].sourceEnd) {
			return i;
		}
	}
	return session->paragraphCount - 1;
}

static BOOL
VisibleRangeHasPending(const DocumentSession* session)
{
	int i;
	if (session == NULL) {
		return FALSE;
	}
	for (i = session->visibleStart; i <= session->visibleEnd && i < session->paragraphCount; i++) {
		if (session->paragraphs[i].state != PARAGRAPH_DONE && session->paragraphs[i].state != PARAGRAPH_SKIPPED) {
			return TRUE;
		}
	}
	return FALSE;
}

static void
UpdateTranslateToggleUi(void)
{
	if (hMainMenu != NULL) {
		CheckMenuItem(hMainMenu, IDM_VIEW_TRANSLATE, MF_BYCOMMAND | (g_translateToggle ? MF_CHECKED : MF_UNCHECKED));
	}
	if (hToolBar != NULL) {
		SendMessage(hToolBar, TB_CHECKBUTTON, IDM_VIEW_TRANSLATE, MAKELPARAM(g_translateToggle, 0));
	}
}

static void
QueueRender(void)
{
	if (g_renderEvent != NULL) {
		SetEvent(g_renderEvent);
	}
}

static void
QueueTranslation(void)
{
	if (g_translateEvent != NULL) {
		SetEvent(g_translateEvent);
	}
}

static void
ReopenCurrentFile(void)
{
	if (szCurrentFile[0] != L'\0') {
		FileOpen(szCurrentFile);
	}
}
