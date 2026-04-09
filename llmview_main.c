// llmview - LLM-assisted Markdown viewer
#include <windows.h>
#include <Commctrl.h>
#include <RichEdit.h>
#include <Shellapi.h>
#include <ShellScalingApi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Resource.h"
#include "viewer_common.h"
#include "llm_translate.h"

#define MAX_LOADSTRING 100
#define MARGIN 20
#define PREVIEW_BYTES (96 * 1024)
#define LLM_QUICK_BYTES (12 * 1024)
#define LLM_WINDOW_BYTES (4 * 1024)
#define LLM_INITIAL_SLICE_CHARS 1200
#define LLM_SLICE_CHARS 4000
#define IDM_VIEW_TRANSLATE 40024
#define WM_APP_RENDER_COMPLETE (WM_APP + 1)
#define WM_APP_VISIBLE_RANGE_CHANGED (WM_APP + 2)

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

char* markdown2rtf(const char* md, const char* img_path);

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
} ParagraphInfo;

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
static BOOL ComposeSliceMarkdown(const DocumentSession* session, int startParagraph, int endParagraph, char** markdownOut);
static BOOL ApplySliceTranslation(DocumentSession* session, int startParagraph, int endParagraph, const char* translatedMarkdown, BOOL skippedByLanguage);
static int SplitParagraphsInPlace(char* markdown, char** outParts, int maxParts);
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
		MessageBox(NULL, L"Window Registration Failed!", szAppName, MB_ICONERROR | MB_OK);
		return 0;
	}

	hMainWindow = CreateWindow(szWindowClass, szAppName, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, hMainMenu, hInstance, NULL);
	if (hMainWindow == NULL) {
		MessageBox(NULL, L"Window Creation Failed!", szAppName, MB_ICONERROR | MB_OK);
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
			FileOpen(szArglist[1]);
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
	case WM_MENUSELECT:
		switch (LOWORD(wParam)) {
		case IDM_FILE_OPEN:
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Open a new file");
			break;
		case IDM_VIEW_TRANSLATE:
			SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Toggle LLM translation");
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
					SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Translated visible text shown. Remaining paragraphs are translating...");
				}
				else if (g_session != NULL && g_session->generation == generation && g_session->translationEnabled && !g_session->hasTranslatedContent) {
					SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Preview loaded. Translating visible text in background...");
				}
				else {
					SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
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

	previewRtf = markdown2rtf(previewMarkdown, session->imagePath);
	free(previewMarkdown);
	if (fullDisplayMarkdown != NULL) free(fullDisplayMarkdown);
	if (previewRtf == NULL) {
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)"");
		return FALSE;
	}

	SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)previewRtf);
	free(previewRtf);
	SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)(session->translationEnabled ? L"Preview loaded. Translating visible text in background..." : L"Preview loaded, rendering full document..."));
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
ComposeSliceMarkdown(const DocumentSession* session, int startParagraph, int endParagraph, char** markdownOut)
{
	size_t total = 0;
	char* markdown;
	size_t offset = 0;
	int i;

	*markdownOut = NULL;
	for (i = startParagraph; i <= endParagraph; i++) {
		total += session->paragraphs[i].originalLen;
		if (i > startParagraph) total += 2;
	}
	markdown = (char*)malloc(total + 1);
	if (markdown == NULL) return FALSE;

	for (i = startParagraph; i <= endParagraph; i++) {
		if (i > startParagraph) {
			memcpy(markdown + offset, "\n\n", 2);
			offset += 2;
		}
		memcpy(markdown + offset, session->paragraphs[i].original, session->paragraphs[i].originalLen);
		offset += session->paragraphs[i].originalLen;
	}
	markdown[offset] = '\0';
	*markdownOut = markdown;
	return TRUE;
}

static BOOL
ApplySliceTranslation(DocumentSession* session, int startParagraph, int endParagraph, const char* translatedMarkdown, BOOL skippedByLanguage)
{
	int expected = endParagraph - startParagraph + 1;
	char** parts;
	char* markdownCopy;
	int actual;
	int i;

	if (skippedByLanguage) {
		for (i = startParagraph; i <= endParagraph; i++) {
			if (session->paragraphs[i].translated != NULL) free(session->paragraphs[i].translated);
			session->paragraphs[i].translated = _strdup(session->paragraphs[i].original);
			session->paragraphs[i].translatedLen = session->paragraphs[i].originalLen;
			session->paragraphs[i].state = PARAGRAPH_SKIPPED;
		}
		session->hasTranslatedContent = TRUE;
		session->deferInitialBackgroundRender = FALSE;
		return TRUE;
	}

	markdownCopy = _strdup(translatedMarkdown);
	if (markdownCopy == NULL) return FALSE;
	parts = (char**)calloc(expected, sizeof(char*));
	if (parts == NULL) {
		free(markdownCopy);
		return FALSE;
	}
	actual = SplitParagraphsInPlace(markdownCopy, parts, expected);
	if (actual != expected) {
		for (i = startParagraph; i <= endParagraph; i++) {
			if (session->paragraphs[i].translated != NULL) free(session->paragraphs[i].translated);
			session->paragraphs[i].translated = _strdup(session->paragraphs[i].original);
			session->paragraphs[i].translatedLen = session->paragraphs[i].originalLen;
			session->paragraphs[i].state = PARAGRAPH_SKIPPED;
		}
		free(parts);
		free(markdownCopy);
		return FALSE;
	}

	for (i = 0; i < expected; i++) {
		int paragraphIndex = startParagraph + i;
		if (session->paragraphs[paragraphIndex].translated != NULL) free(session->paragraphs[paragraphIndex].translated);
		session->paragraphs[paragraphIndex].translated = _strdup(parts[i]);
		session->paragraphs[paragraphIndex].translatedLen = strlen(parts[i]);
		session->paragraphs[paragraphIndex].state = PARAGRAPH_DONE;
	}
	session->hasTranslatedContent = TRUE;
	session->deferInitialBackgroundRender = FALSE;

	free(parts);
	free(markdownCopy);
	return TRUE;
}

static int
SplitParagraphsInPlace(char* markdown, char** outParts, int maxParts)
{
	int count = 0;
	char* cursor = markdown;

	while (cursor != NULL && *cursor != '\0' && count < maxParts) {
		char* next = strstr(cursor, "\n\n");
		outParts[count++] = cursor;
		if (next == NULL) {
			break;
		}
		*next = '\0';
		cursor = next + 2;
	}
	return count;
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
	size_t total = 0;
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

	for (i = searchStart; i <= searchEnd && i < session->paragraphCount; i++) {
		if (session->paragraphs[i].state == PARAGRAPH_PENDING) {
			start = i;
			break;
		}
	}
	if (start < 0) {
		LeaveCriticalSection(&g_sessionLock);
		return FALSE;
	}

	end = start;
	{
		size_t sliceLimit = session->hasTranslatedContent ? LLM_SLICE_CHARS : LLM_INITIAL_SLICE_CHARS;
		while (end < session->paragraphCount) {
			size_t nextLen = total + session->paragraphs[end].originalLen + ((end > start) ? 2 : 0);
			if (end > start && nextLen > sliceLimit) {
				break;
			}
			if (session->paragraphs[end].state != PARAGRAPH_PENDING) {
				break;
			}
			total = nextLen;
			end++;
		}
	}
	end--;

	slice->generation = session->generation;
	slice->startParagraph = start;
	slice->endParagraph = end;
	if (!ComposeSliceMarkdown(session, start, end, &slice->markdown)) {
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
			if (!SelectNextTranslationSlice(&slice)) break;
			if (LlmTranslate_MaybeTranslateMarkdown(&g_llmConfig, slice.markdown, &translated, &skipped)) {
				EnterCriticalSection(&g_sessionLock);
				if (g_session != NULL && g_session->generation == slice.generation) {
					ApplySliceTranslation(g_session, slice.startParagraph, slice.endParagraph, translated, skipped);
				}
				LeaveCriticalSection(&g_sessionLock);
				QueueRender();
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

BOOL
CreateToolBar()
{
	int iDpi = GetDpiForWindow(hMainWindow);
	const int ImageListID = 0;
	const int numButtons = 4;
	int bitmapSize = (iDpi / 3) - 16;
	const DWORD buttonStyles = BTNS_AUTOSIZE;
	HIMAGELIST hImageList = NULL;
	HWND hWndToolbar;
	HICON hOpenLarge;
	HICON hRefreshLarge;
	HICON hTranslateIcon;
	HICON hCloseIcon;
	int iOpenIndex;
	int iRefreshIndex;
	int iTranslateIndex;
	int iCloseIndex;
	TBBUTTON tbButtons[7];

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
	hTranslateIcon = (HICON)LoadImage(NULL, IDI_INFORMATION, IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);
	hCloseIcon = (HICON)LoadImage(NULL, IDI_ERROR, IMAGE_ICON, bitmapSize, bitmapSize, LR_SHARED);

	iOpenIndex = ImageList_AddIcon(hImageList, hOpenLarge);
	iRefreshIndex = ImageList_AddIcon(hImageList, hRefreshLarge);
	iTranslateIndex = ImageList_AddIcon(hImageList, hTranslateIcon);
	iCloseIndex = ImageList_AddIcon(hImageList, hCloseIcon);

	DestroyIcon(hOpenLarge);
	DestroyIcon(hRefreshLarge);
	SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	ZeroMemory(tbButtons, sizeof(tbButtons));
	tbButtons[0].iBitmap = iOpenIndex;
	tbButtons[0].idCommand = IDM_FILE_OPEN;
	tbButtons[0].fsState = TBSTATE_ENABLED;
	tbButtons[0].fsStyle = (BYTE)buttonStyles;
	tbButtons[1].fsState = TBSTATE_ENABLED;
	tbButtons[1].fsStyle = BTNS_SEP;
	tbButtons[2].iBitmap = iRefreshIndex;
	tbButtons[2].idCommand = IDM_FILE_REFRESH;
	tbButtons[2].fsState = TBSTATE_ENABLED;
	tbButtons[2].fsStyle = (BYTE)buttonStyles;
	tbButtons[3].fsState = TBSTATE_ENABLED;
	tbButtons[3].fsStyle = BTNS_SEP;
	tbButtons[4].iBitmap = iTranslateIndex;
	tbButtons[4].idCommand = IDM_VIEW_TRANSLATE;
	tbButtons[4].fsState = TBSTATE_ENABLED | TBSTATE_CHECKED;
	tbButtons[4].fsStyle = BTNS_CHECK | (BYTE)buttonStyles;
	tbButtons[5].fsState = TBSTATE_ENABLED;
	tbButtons[5].fsStyle = BTNS_SEP;
	tbButtons[6].iBitmap = iCloseIndex;
	tbButtons[6].idCommand = IDM_FILE_EXIT;
	tbButtons[6].fsState = TBSTATE_ENABLED;
	tbButtons[6].fsStyle = (BYTE)buttonStyles;
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
