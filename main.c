// Markdown Viewer � 2022 by Thomas F�hringer

#pragma once
//#include <SDKDDKVer.h>
#include <windows.h>
#include <objbase.h>
#include <Commctrl.h>
#include <RichEdit.h>
#include <RichOle.h>
#include <Shellapi.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <ShellScalingApi.h>

#include "Resource.h"

#define MAX_LOADSTRING  100
#define PATH_BUFFER_SIZE 4096
#define MARGIN  20

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"") // Enables visual styles.

char* markdown2rtf(const char* md, const char* img_path);

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

static const DWORD ESC_DOUBLE_PRESS_INTERVAL_MS = 600;
static ULONGLONG g_lastEscPressTimestamp = 0;

DWORD dwFilesize;
char* pFileView;

typedef struct _SimpleRichEditOleCallback {
	IRichEditOleCallback iface;
	LONG refCount;
} SimpleRichEditOleCallback;

static HRESULT STDMETHODCALLTYPE OleCb_QueryInterface(IRichEditOleCallback* This, REFIID riid, void** ppvObject);
static ULONG STDMETHODCALLTYPE OleCb_AddRef(IRichEditOleCallback* This);
static ULONG STDMETHODCALLTYPE OleCb_Release(IRichEditOleCallback* This);
static HRESULT STDMETHODCALLTYPE OleCb_GetNewStorage(IRichEditOleCallback* This, LPSTORAGE* lplpstg);
static HRESULT STDMETHODCALLTYPE OleCb_GetInPlaceContext(IRichEditOleCallback* This, LPOLEINPLACEFRAME* lplpFrame, LPOLEINPLACEUIWINDOW* lplpDoc, LPOLEINPLACEFRAMEINFO lpFrameInfo);
static HRESULT STDMETHODCALLTYPE OleCb_ShowContainerUI(IRichEditOleCallback* This, BOOL fShow);
static HRESULT STDMETHODCALLTYPE OleCb_QueryInsertObject(IRichEditOleCallback* This, LPCLSID pclsid, LPSTORAGE pstg, LONG cp);
static HRESULT STDMETHODCALLTYPE OleCb_DeleteObject(IRichEditOleCallback* This, LPOLEOBJECT poleobj);
static HRESULT STDMETHODCALLTYPE OleCb_QueryAcceptData(IRichEditOleCallback* This, LPDATAOBJECT pdataobj, CLIPFORMAT* pcfFormat, DWORD reco, BOOL fReally, HGLOBAL hMetaPict);
static HRESULT STDMETHODCALLTYPE OleCb_ContextSensitiveHelp(IRichEditOleCallback* This, BOOL fEnterMode);
static HRESULT STDMETHODCALLTYPE OleCb_GetClipboardData(IRichEditOleCallback* This, CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj);
static HRESULT STDMETHODCALLTYPE OleCb_GetDragDropEffect(IRichEditOleCallback* This, BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect);
static HRESULT STDMETHODCALLTYPE OleCb_GetContextMenu(IRichEditOleCallback* This, WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg, HMENU* lphmenu);

static IRichEditOleCallbackVtbl g_richEditOleCallbackVtbl = {
	OleCb_QueryInterface,
	OleCb_AddRef,
	OleCb_Release,
	OleCb_GetNewStorage,
	OleCb_GetInPlaceContext,
	OleCb_ShowContainerUI,
	OleCb_QueryInsertObject,
	OleCb_DeleteObject,
	OleCb_QueryAcceptData,
	OleCb_ContextSensitiveHelp,
	OleCb_GetClipboardData,
	OleCb_GetDragDropEffect,
	OleCb_GetContextMenu
};

static SimpleRichEditOleCallback g_richEditOleCallback = { { &g_richEditOleCallbackVtbl }, 1 };

// Forward declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
char* toU8(const LPWSTR szUTF16);
LPWSTR toW(const char* strTextUTF8);
int Scale(int iValue);

BOOL FileOpen(WCHAR* lpszTextFileName);
void FileOpenDialog();
BOOL CreateToolBar();
BOOL CreateStatusBar();
BOOL App_SaveState();
BOOL App_RestoreState();
void ShowLastError(LPCTSTR lpszContext);
BOOL SetRichEditRtf(HWND hEdit, const char* rtfText);
BOOL InsertMarkdownImages(HWND hEdit, const char* md, const WCHAR* baseDir);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	HRESULT oleInit = OleInitialize(NULL);

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

	if (!App_RestoreState())
		ShowWindow(hMainWindow, nCmdShow);

	UpdateWindow(hMainWindow);

	LPWSTR* szArglist;
	int nArgs;
	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (NULL != szArglist && nArgs == 2)
		FileOpen(szArglist[1]);

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
	if (SUCCEEDED(oleInit)) {
		OleUninitialize();
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
			ShowLastError(L"Msftedit.dll Load Failed!");
			return 0;
		}
		hRichEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
			ES_MULTILINE | WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_READONLY,
			10, 100, 200, 200,
			hwnd, (HMENU)NULL, GetModuleHandle(NULL), NULL
		);

		if (hRichEdit == NULL)
		{
			ShowLastError(L"RichEdit Creation Failed!");
			return 0;
		}
		SendMessage(hRichEdit, EM_SETOLECALLBACK, 0, (LPARAM)&g_richEditOleCallback.iface);
		SendMessage(hRichEdit, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessage(hRichEdit, EM_SETEDITSTYLEEX, 0, SES_EX_HANDLEFRIENDLYURL | SES_HYPERLINKTOOLTIPS);
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
		InflateRect(&rect, -MARGIN, -MARGIN);
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
			TEXTRANGEW tr;
			if (enLinkInfo->msg == WM_LBUTTONUP) {
				//LONG len_link = enLinkInfo->chrg.cpMax - enLinkInfo->chrg.cpMin;
				WCHAR szLink[1024];
				tr.chrg = enLinkInfo->chrg;
				tr.lpstrText = szLink;

				SendMessage(hRichEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				ShellExecuteW(NULL, L"open", szLink, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		}
		break; }

	case WM_QUERYENDSESSION:
	case WM_CLOSE:
		App_SaveState();
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
	HANDLE hFile, hMap;

	WCHAR  lpBufferPathWithFile[PATH_BUFFER_SIZE] = L"";
	WCHAR  lpBufferPathWithoutFile[PATH_BUFFER_SIZE] = L"";
	WCHAR* lpFilePart = NULL;

	if (lpszTextFileName == NULL)
	{
		szCurrentFile[0] = L'\0';
		SetWindowTextW(hRichEdit, L"");
		SendMessageW(hMainWindow, WM_SETTEXT, (WPARAM)0, (LPARAM)szAppName);
		return FALSE;
	}

	hFile = CreateFileW(lpszTextFileName, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		ShowLastError(L"Cannot open file.");
		return FALSE;
	}

	dwFilesize = GetFileSize(hFile, NULL);
	if (dwFilesize == 0) {
		ShowLastError(L"Invalid file.");
		CloseHandle(hFile);
		return FALSE;
	}

	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	pFileView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);

	if (pFileView == NULL) {
		ShowLastError(L"Cannot open file.");
		CloseHandle(hMap);
		CloseHandle(hFile);
		return FALSE;
	}

	if (GetFullPathNameW(lpszTextFileName, PATH_BUFFER_SIZE, lpBufferPathWithFile, &lpFilePart) == 0) {
		ShowLastError(L"Invalid file.");
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return FALSE;
	}
	memcpy(lpBufferPathWithoutFile, lpBufferPathWithFile, PATH_BUFFER_SIZE);
	if (PathCchRemoveFileSpec(lpBufferPathWithoutFile, PATH_BUFFER_SIZE) != S_OK) {
		ShowLastError(L"Invalid file.");
		UnmapViewOfFile(pFileView);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return FALSE;
	}
	lstrcpyW(szCurrentFile, lpszTextFileName);

	char* md = malloc(dwFilesize + 2);
	//memset(md,0, dwFilesize + 1);
	memcpy(md, pFileView, dwFilesize);
	*(md + dwFilesize) = '\0';
	*(md + dwFilesize + 1) = '\0';

	UnmapViewOfFile(pFileView);
	CloseHandle(hMap);
	CloseHandle(hFile);

	char* path = toU8(lpBufferPathWithoutFile);
	char* pRTF = markdown2rtf(md, path);
	if (pRTF == NULL) {
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Could not convert Markdown.");
		free(md);
		free(path);
		return FALSE;
	}

	TCHAR szTitle[MAX_PATH + 20];
	szTitle[0] = 0;
	StrCatW(szTitle, lpFilePart);
	StrCatW(szTitle, L" - ");
	StrCatW(szTitle, szAppName);
	if (!SetRichEditRtf(hRichEdit, pRTF)) {
		SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Failed to load rich text content.");
	}
	InsertMarkdownImages(hRichEdit, md, lpBufferPathWithoutFile);
	SendMessageW(hMainWindow, WM_SETTEXT, (WPARAM)0, (LPARAM)szTitle);
	free(md);
	free(pRTF);
	free(path);
	return TRUE;
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
		ShowLastError(L"Toolbar Creation Failed!");
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

BOOL App_SaveState()
{
	HKEY hKey;
	WINDOWPLACEMENT wp;
	TCHAR szSubkey[256];
	lstrcpy(szSubkey, L"Software\\");
	lstrcat(szSubkey, szAppName);
	if (RegCreateKeyEx(HKEY_CURRENT_USER, szSubkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
		ShowLastError(L"Creating Registry Key");
		return FALSE;
	}

	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(hMainWindow, &wp);

	if ((RegSetValueExW(hKey, L"flags", 0, REG_BINARY,
		(PBYTE)&wp.flags, sizeof(wp.flags)) != ERROR_SUCCESS) ||
		(RegSetValueExW(hKey, L"showCmd", 0, REG_BINARY,
			(PBYTE)&wp.showCmd, sizeof(wp.showCmd)) != ERROR_SUCCESS) ||
		(RegSetValueExW(hKey, L"rcNormalPosition", 0, REG_BINARY,
			(PBYTE)&wp.rcNormalPosition, sizeof(wp.rcNormalPosition)) != ERROR_SUCCESS))
	{
		RegCloseKey(hKey);
		return FALSE;
	}

	RegCloseKey(hKey);
	return TRUE;
}

BOOL App_RestoreState()
{
	HKEY hKey;
	DWORD dwSizeFlags, dwSizeShowCmd, dwSizeRcNormal;
	WINDOWPLACEMENT wp;
	TCHAR szSubkey[256];

	lstrcpy(szSubkey, L"Software\\");
	lstrcat(szSubkey, szAppName);
	if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubkey, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
	{
		wp.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hMainWindow, &wp);
		dwSizeFlags = sizeof(wp.flags);
		dwSizeShowCmd = sizeof(wp.showCmd);
		dwSizeRcNormal = sizeof(wp.rcNormalPosition);
		if ((RegQueryValueExW(hKey, L"flags", NULL, NULL,
			(PBYTE)&wp.flags, &dwSizeFlags) != ERROR_SUCCESS) ||
			(RegQueryValueExW(hKey, L"showCmd", NULL, NULL,
				(PBYTE)&wp.showCmd, &dwSizeShowCmd) != ERROR_SUCCESS) ||
			(RegQueryValueExW(hKey, L"rcNormalPosition", NULL, NULL,
				(PBYTE)&wp.rcNormalPosition, &dwSizeRcNormal) != ERROR_SUCCESS))
		{
			RegCloseKey(hKey);
			return FALSE;
		}
		RegCloseKey(hKey);
		if ((wp.rcNormalPosition.left <=
			(GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXICON))) &&
			(wp.rcNormalPosition.top <= (GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYICON))))
		{
			SetWindowPlacement(hMainWindow, &wp);
			return TRUE;
		}
	}
	return FALSE;
}


// converts a Wide Char (Unicode) string to UTF-8
char*
toU8(const LPWSTR szUTF16)
{
	if (szUTF16 == NULL)
		return NULL;
	if (*szUTF16 == L'\0') {
		char* empty = (char*)malloc(1);
		if (empty != NULL) {
			empty[0] = '\0';
		}
		return empty;
	}

	int cbUTF8 = WideCharToMultiByte(CP_UTF8, 0, szUTF16, -1, NULL, 0, NULL, NULL);
	if (cbUTF8 == 0) {
		ShowLastError(L"String conversion to UTF-8 failed.");
		return NULL;
	}
	char* strTextUTF8 = (char*)malloc(cbUTF8);
	int result = WideCharToMultiByte(CP_UTF8, 0, szUTF16, -1, strTextUTF8, cbUTF8, NULL, NULL);
	if (result == 0) {
		ShowLastError(L"String conversion to UTF-8 failed.");
		return NULL;
	}
	return strTextUTF8;
}

// converts a UTF-8 string to Wide Char (Unicode)
LPWSTR
toW(const char* strTextUTF8)
{
	if (strTextUTF8 == NULL)
		return NULL;
	if (*strTextUTF8 == '\0') {
		LPWSTR empty = (LPWSTR)malloc(sizeof(WCHAR));
		if (empty != NULL) {
			empty[0] = L'\0';
		}
		return empty;
	}

	int cchUTF16 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strTextUTF8, -1, NULL, 0); // request buffer size
	if (cchUTF16 == 0) {
		ShowLastError(L"String conversion to wide character failed.");
		return NULL;
	}
	LPWSTR szUTF16 = (LPWSTR)malloc(cchUTF16 * sizeof(WCHAR));
	int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strTextUTF8, -1, szUTF16, cchUTF16);
	if (result == 0) {
		ShowLastError(L"String conversion to wide character failed.");
		return NULL;
	}
	return szUTF16;
}

void ShowLastError(LPCTSTR lpszContext)
{
	TCHAR buf[255];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, buf, sizeof(buf) / sizeof(TCHAR), 0);
	MessageBox(0, buf, lpszContext, 0);
}

int Scale(int iValue)
{
	int iDpi = GetDpiForWindow(hMainWindow);
	return MulDiv(iValue, iDpi, 96);
}

typedef struct _RTF_STREAM_CTX {
	const char* data;
	size_t len;
	size_t pos;
} RTF_STREAM_CTX;

static DWORD CALLBACK
RtfStreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb)
{
	RTF_STREAM_CTX* ctx = (RTF_STREAM_CTX*)dwCookie;
	if (ctx == NULL || pbBuff == NULL || pcb == NULL) {
		return 1;
	}

	size_t remain = (ctx->pos < ctx->len) ? (ctx->len - ctx->pos) : 0;
	size_t ncopy = (remain < (size_t)cb) ? remain : (size_t)cb;
	if (ncopy > 0) {
		memcpy(pbBuff, ctx->data + ctx->pos, ncopy);
		ctx->pos += ncopy;
	}
	*pcb = (LONG)ncopy;
	return 0;
}

BOOL
SetRichEditRtf(HWND hEdit, const char* rtfText)
{
	if (hEdit == NULL || rtfText == NULL) {
		return FALSE;
	}

	RTF_STREAM_CTX ctx;
	ctx.data = rtfText;
	ctx.len = strlen(rtfText);
	ctx.pos = 0;

	EDITSTREAM es;
	ZeroMemory(&es, sizeof(es));
	es.dwCookie = (DWORD_PTR)&ctx;
	es.pfnCallback = RtfStreamInCallback;

	SendMessage(hEdit, WM_SETREDRAW, FALSE, 0);
	SetWindowTextW(hEdit, L"");
	SendMessage(hEdit, EM_STREAMIN, (WPARAM)SF_RTF, (LPARAM)&es);
	SendMessage(hEdit, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hEdit, NULL, TRUE);

	return es.dwError == 0;
}

static void
TrimImageTarget(const char* src, char* dst, size_t dstSize)
{
	size_t len = strlen(src);
	size_t start = 0;
	size_t end = len;
	while (start < len && (src[start] == ' ' || src[start] == '\t'))
		start++;
	while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t'))
		end--;
	if (end > start + 1 && src[start] == '<' && src[end - 1] == '>') {
		start++;
		end--;
	}
	size_t outLen = end - start;
	if (outLen >= dstSize)
		outLen = dstSize - 1;
	memcpy(dst, src + start, outLen);
	dst[outLen] = '\0';
}

static BOOL
InsertImageAtSelection(HWND hEdit, const WCHAR* imagePath, const WCHAR* altText)
{
	IStream* pStream = NULL;
	HRESULT hr = SHCreateStreamOnFileEx(imagePath, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &pStream);
	if (FAILED(hr) || pStream == NULL)
		return FALSE;

	RICHEDIT_IMAGE_PARAMETERS rip;
	ZeroMemory(&rip, sizeof(rip));
	rip.xWidth = 0;
	rip.yHeight = 0;
	rip.Ascent = 0;
	rip.Type = TA_BASELINE;
	rip.pwszAlternateText = altText;
	rip.pIStream = pStream;

	LRESULT inserted = SendMessage(hEdit, EM_INSERTIMAGE, 0, (LPARAM)&rip);
	pStream->lpVtbl->Release(pStream);
	return inserted != 0;
}

BOOL
InsertMarkdownImages(HWND hEdit, const char* md, const WCHAR* baseDir)
{
	if (hEdit == NULL || md == NULL || baseDir == NULL)
		return FALSE;

	const char* p = md;
	LONG searchStart = 0;
	while (*p) {
		if (p[0] == '!' && p[1] == '[') {
			const char* labelEnd = strstr(p + 2, "](");
			if (labelEnd) {
				const char* urlStart = labelEnd + 2;
				const char* urlEnd = strstr(urlStart, ")");
				if (urlEnd) {
					size_t tokenLen = (size_t)(urlEnd - p + 1);
					char* token = (char*)malloc(tokenLen + 1);
					if (token == NULL)
						return FALSE;
					memcpy(token, p, tokenLen);
					token[tokenLen] = '\0';

					size_t altLen = (size_t)(labelEnd - (p + 2));
					char* alt = (char*)malloc(altLen + 1);
					if (alt == NULL) {
						free(token);
						return FALSE;
					}
					memcpy(alt, p + 2, altLen);
					alt[altLen] = '\0';

					size_t urlLen = (size_t)(urlEnd - urlStart);
					char* target = (char*)malloc(urlLen + 1);
					if (target == NULL) {
						free(token);
						free(alt);
						return FALSE;
					}
					memcpy(target, urlStart, urlLen);
					target[urlLen] = '\0';

					char normalized[1024];
					TrimImageTarget(target, normalized, sizeof(normalized));

					WCHAR* tokenW = toW(token);
					WCHAR* altW = toW(alt);
					WCHAR* targetW = toW(normalized);
					if (tokenW && altW && targetW) {
						WCHAR fullPath[MAX_PATH * 4];
						if (PathIsRelativeW(targetW)) {
							wsprintfW(fullPath, L"%s\\%s", baseDir, targetW);
						} else {
							lstrcpyW(fullPath, targetW);
						}

						FINDTEXTEXW ft;
						ZeroMemory(&ft, sizeof(ft));
						ft.chrg.cpMin = searchStart;
						ft.chrg.cpMax = -1;
						ft.lpstrText = tokenW;

						LRESULT found = SendMessage(hEdit, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft);
						if (found != -1) {
							CHARRANGE sel;
							sel.cpMin = ft.chrgText.cpMin;
							sel.cpMax = ft.chrgText.cpMax;
							SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);
							SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");

							sel.cpMin = ft.chrgText.cpMin;
							sel.cpMax = ft.chrgText.cpMin;
							SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);

							if (InsertImageAtSelection(hEdit, fullPath, altW)) {
								searchStart = ft.chrgText.cpMin + 1;
							} else {
								SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)tokenW);
								searchStart = ft.chrgText.cpMin + (LONG)lstrlenW(tokenW);
							}
						}
					}

					free(token);
					free(alt);
					free(target);
					free(tokenW);
					free(altW);
					free(targetW);
					p = urlEnd + 1;
					continue;
				}
			}
		}
		p++;
	}
	return TRUE;
}

static HRESULT STDMETHODCALLTYPE
OleCb_QueryInterface(IRichEditOleCallback* This, REFIID riid, void** ppvObject)
{
	if (ppvObject == NULL) {
		return E_POINTER;
	}
	*ppvObject = NULL;
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRichEditOleCallback)) {
		*ppvObject = This;
		OleCb_AddRef(This);
		return S_OK;
	}
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
OleCb_AddRef(IRichEditOleCallback* This)
{
	SimpleRichEditOleCallback* self = (SimpleRichEditOleCallback*)This;
	return (ULONG)InterlockedIncrement(&self->refCount);
}

static ULONG STDMETHODCALLTYPE
OleCb_Release(IRichEditOleCallback* This)
{
	SimpleRichEditOleCallback* self = (SimpleRichEditOleCallback*)This;
	return (ULONG)InterlockedDecrement(&self->refCount);
}

static HRESULT STDMETHODCALLTYPE
OleCb_GetNewStorage(IRichEditOleCallback* This, LPSTORAGE* lplpstg)
{
	UNREFERENCED_PARAMETER(This);
	LPLOCKBYTES lockBytes = NULL;
	HRESULT hr = CreateILockBytesOnHGlobal(NULL, TRUE, &lockBytes);
	if (FAILED(hr)) {
		return hr;
	}
	hr = StgCreateDocfileOnILockBytes(lockBytes, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE, 0, lplpstg);
	lockBytes->lpVtbl->Release(lockBytes);
	return hr;
}

static HRESULT STDMETHODCALLTYPE
OleCb_GetInPlaceContext(IRichEditOleCallback* This, LPOLEINPLACEFRAME* lplpFrame, LPOLEINPLACEUIWINDOW* lplpDoc, LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(lplpFrame);
	UNREFERENCED_PARAMETER(lplpDoc);
	UNREFERENCED_PARAMETER(lpFrameInfo);
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
OleCb_ShowContainerUI(IRichEditOleCallback* This, BOOL fShow)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(fShow);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
OleCb_QueryInsertObject(IRichEditOleCallback* This, LPCLSID pclsid, LPSTORAGE pstg, LONG cp)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(pclsid);
	UNREFERENCED_PARAMETER(pstg);
	UNREFERENCED_PARAMETER(cp);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
OleCb_DeleteObject(IRichEditOleCallback* This, LPOLEOBJECT poleobj)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(poleobj);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
OleCb_QueryAcceptData(IRichEditOleCallback* This, LPDATAOBJECT pdataobj, CLIPFORMAT* pcfFormat, DWORD reco, BOOL fReally, HGLOBAL hMetaPict)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(pdataobj);
	UNREFERENCED_PARAMETER(pcfFormat);
	UNREFERENCED_PARAMETER(reco);
	UNREFERENCED_PARAMETER(fReally);
	UNREFERENCED_PARAMETER(hMetaPict);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
OleCb_ContextSensitiveHelp(IRichEditOleCallback* This, BOOL fEnterMode)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(fEnterMode);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
OleCb_GetClipboardData(IRichEditOleCallback* This, CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(lpchrg);
	UNREFERENCED_PARAMETER(reco);
	UNREFERENCED_PARAMETER(lplpdataobj);
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
OleCb_GetDragDropEffect(IRichEditOleCallback* This, BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(fDrag);
	UNREFERENCED_PARAMETER(grfKeyState);
	UNREFERENCED_PARAMETER(pdwEffect);
	return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
OleCb_GetContextMenu(IRichEditOleCallback* This, WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg, HMENU* lphmenu)
{
	UNREFERENCED_PARAMETER(This);
	UNREFERENCED_PARAMETER(seltype);
	UNREFERENCED_PARAMETER(lpoleobj);
	UNREFERENCED_PARAMETER(lpchrg);
	UNREFERENCED_PARAMETER(lphmenu);
	return E_NOTIMPL;
}
