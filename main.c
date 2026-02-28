// Markdown Viewer - 2022 by Thomas Fuhringer
// 2026 by dodo

#pragma once
//#include <SDKDDKVer.h>
#include <windows.h>
#include <Commctrl.h>
#include <RichEdit.h>
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

DWORD dwFilesize;
char* pFileView;

DWORD lastEscapeTime = 0;
int escapeCount = 0;
BOOL bIsSelecting = FALSE;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
char* toU8(const LPWSTR szUTF16);
int Scale(int iValue);

BOOL FileOpen(WCHAR* lpszTextFileName);
void FileOpenDialog();
BOOL CreateToolBar();
BOOL CreateStatusBar();
void ShowLastError(LPCTSTR lpszContext);
BOOL App_SaveState();
BOOL App_RestoreState();

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
			SendMessage(hRichEdit, WM_VSCROLL, SB_PAGEDOWN, 0);
			break;
		case VK_ESCAPE:
		{
			DWORD currentTime = GetTickCount();
			if (currentTime - lastEscapeTime < 500 && escapeCount == 1) {
				PostQuitMessage(0);
				escapeCount = 0;
			}
			else {
				escapeCount = 1;
				lastEscapeTime = currentTime;
			}
			break;
		}
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
	SETTEXTEX se;
	se.codepage = 65001;// CP_ACP;
	se.flags = ST_DEFAULT;

	WCHAR  lpBufferPathWithFile[PATH_BUFFER_SIZE] = L"";
	WCHAR  lpBufferPathWithoutFile[PATH_BUFFER_SIZE] = L"";
	WCHAR* lpFilePart = NULL;

	if (lpszTextFileName == NULL)
	{
		szCurrentFile[0] = L'\0';
		SendMessageA(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)"");
		SendMessageW(hMainWindow, WM_SETTEXT, (WPARAM)0, (LPARAM)szAppName);
		return 0;
	}

	hFile = CreateFileW(lpszTextFileName, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		ShowLastError(L"Cannot open file.");
		return 0;
	}

	dwFilesize = GetFileSize(hFile, NULL);
	if (dwFilesize == 0) {
		ShowLastError(L"Invalid file.");
		return 0;
	}

	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	pFileView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);

	if (pFileView == NULL) {
		ShowLastError(L"Cannot open file.");
		return 0;
	}

	if (GetFullPathNameW(lpszTextFileName, PATH_BUFFER_SIZE, lpBufferPathWithFile, &lpFilePart) == 0) {
		ShowLastError(L"Invalid file.");
		return 0;
	}
	memcpy(lpBufferPathWithoutFile, lpBufferPathWithFile, PATH_BUFFER_SIZE);
	if (PathCchRemoveFileSpec(lpBufferPathWithoutFile, PATH_BUFFER_SIZE) != S_OK) {
		ShowLastError(L"Invalid file.");
		return 0;
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
	}

	TCHAR szTitle[MAX_PATH + 20];
	szTitle[0] = 0;
	StrCatW(szTitle, lpFilePart);
	StrCatW(szTitle, L" - ");
	StrCatW(szTitle, szAppName);
	SendMessageW(hRichEdit, EM_SETTEXTEX, (WPARAM)&se, (LPARAM)pRTF);
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
	if (*szUTF16 == L'\0')
		return NULL;

	int cbUTF8 = WideCharToMultiByte(CP_UTF8, 0, szUTF16, -1, NULL, 0, NULL, NULL);
	if (cbUTF8 == 0) {
		ShowLastError(L"String conversion failed.");
		return NULL;
	}
	char* strTextUTF8 = (char*)malloc(cbUTF8);
	int result = WideCharToMultiByte(CP_UTF8, 0, szUTF16, -1, strTextUTF8, cbUTF8, NULL, NULL);
	if (result == 0) {
		ShowLastError(L"String conversion failed.");
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
	if (*strTextUTF8 == '\0')
		return NULL;

	int cchUTF16 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strTextUTF8, -1, NULL, 0); // request buffer size
	if (cchUTF16 == 0) {
		ShowLastError(L"String conversion failed.");
		return NULL;
	}
	LPWSTR szUTF16 = (LPWSTR)malloc(cchUTF16 * sizeof(WCHAR));
	int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strTextUTF8, -1, szUTF16, cchUTF16);
	if (result == 0) {
		ShowLastError(L"String conversion failed.");
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


