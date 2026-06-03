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

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

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
		char* rtfResult = (char*)wParam;
		if (rtfResult != NULL) {
			if (generation == (DWORD)g_loadGeneration) {
				SETTEXTEX se;
				se.codepage = 65001;
				se.flags = ST_DEFAULT;
				SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)rtfResult);
				SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Full document loaded.");
			}
			free(rtfResult);
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
	SETTEXTEX se;
	se.codepage = 65001;// CP_ACP;
	se.flags = ST_DEFAULT;

	DWORD currentGeneration = (DWORD)InterlockedIncrement(&g_loadGeneration);
	if (hLoadThread != NULL) {
		// We no longer need to wait; close our handle so we don't leak.
		CloseHandle(hLoadThread);
		hLoadThread = NULL;
	}

	ViewerLoadedFile loadedFile;
	char* mdFull = NULL;
	char* path = NULL;
	char* previewRtf = NULL;
	BOOL result = FALSE;

	if (lpszTextFileName == NULL)
	{
		szCurrentFile[0] = L'\0';
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)"");
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
	previewRtf = markdown2rtf_ex(mdFull, path, 0);
	mdFull[safeLen] = savedChar;

	if (previewRtf != NULL) {
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)previewRtf);
	}
	else {
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)"");
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
				char* fullRtf = markdown2rtf(mdFull, path);
				if (fullRtf != NULL) {
					SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)fullRtf);
					SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
					free(fullRtf);
				}
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
			char* fullRtf = markdown2rtf(mdFull, path);
			if (fullRtf != NULL) {
				SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)fullRtf);
				SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
				free(fullRtf);
			}
			free(mdFull);
			free(path);
		}
	}
	else {
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Document loaded.");
		free(mdFull);
		free(path);
	}

	if (previewRtf != NULL)
		free(previewRtf);

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

	char* fullRtf = markdown2rtf(task->mdFull, task->imgPath);

	free(task->mdFull);
	free(task->imgPath);

	DWORD generation = task->generation;
	free(task);

	if (fullRtf != NULL) {
		PostMessage(hMainWindow, WM_APP_RENDER_COMPLETE, (WPARAM)fullRtf, (LPARAM)generation);
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



