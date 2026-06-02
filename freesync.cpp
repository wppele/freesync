// freesync.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "freesync.h"
#include "SyncEngine.h"

#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <thread>
#include <commctrl.h>
#include <shellapi.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_SOURCE_EDIT 1001
#define IDC_TARGET_EDIT 1002
#define IDC_BROWSE_SOURCE 1003
#define IDC_BROWSE_TARGET 1004
#define IDC_ADD_PAIR 1005
#define IDC_REMOVE_PAIR 1006
#define IDC_PAIR_LIST 1007
#define IDC_DELETE_EXTRA 1008
#define IDC_START_SYNC 1009
#define IDC_LOG_EDIT 1010
#define IDC_PROGRESS_BAR 1011
#define IDC_AUTO_MONITOR 1012
#define IDD_ADD_PAIR_DIALOG 2000
#define IDC_DLG_SOURCE_EDIT 2001
#define IDC_DLG_TARGET_EDIT 2002
#define IDC_DLG_BROWSE_SOURCE 2003
#define IDC_DLG_BROWSE_TARGET 2004
#define IDC_DLG_BIDIRECTIONAL 2005

#define WM_APPEND_LOG (WM_USER + 2)
#define WM_SYNC_COMPLETE (WM_USER + 1)
#define WM_UPDATE_PROGRESS (WM_USER + 3)
#define WM_TRAYICON (WM_USER + 4)

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hSourceEdit;
HWND hTargetEdit;
HWND hPairList;
HWND hDeleteExtra;
HWND hAutoMonitor;
HWND hLogEdit;
HWND hProgressBar;
HFONT hMainFont;
NOTIFYICONDATAW nid = {};
std::vector<SyncPair> gSyncPairs;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    AddPairDlgProc(HWND, UINT, WPARAM, LPARAM);
void                CreateMainControls(HWND hWnd);
void                ResizeMainControls(HWND hWnd);
std::wstring        GetWindowTextString(HWND hWnd);
void                AppendLog(HWND hWnd, const std::wstring& text);
void                RemoveSelectedSyncPair(HWND hWnd);
void                StartSync(HWND hWnd);
void                ToggleMonitoring(HWND hWnd);
void                BrowseFolder(HWND owner, HWND targetEdit);
void                SetupTrayIcon(HWND hWnd);
void                RemoveTrayIcon();
void                ApplyMainFont(HWND hWnd);
void                RefreshPairList();
std::wstring        GetConfigPath();
void                LoadSettings(HWND hWnd);
void                SaveSettings();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_FREESYNC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FREESYNC));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FREESYNC));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_FREESYNC);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, 0, 900, 650, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateMainControls(hWnd);
        SetupTrayIcon(hWnd);
        break;
    case WM_SIZE:
        ResizeMainControls(hWnd);
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDC_BROWSE_SOURCE:
                BrowseFolder(hWnd, hSourceEdit);
                break;
            case IDC_BROWSE_TARGET:
                BrowseFolder(hWnd, hTargetEdit);
                break;
            case IDC_ADD_PAIR:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ADD_PAIR_DIALOG), hWnd, AddPairDlgProc);
                break;
            case IDC_REMOVE_PAIR:
                RemoveSelectedSyncPair(hWnd);
                break;
            case IDC_AUTO_MONITOR:
                ToggleMonitoring(hWnd);
                break;
            case IDC_START_SYNC:
                StartSync(hWnd);
                break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_APPEND_LOG:
    {
        std::wstring* msg = (std::wstring*)wParam;
        AppendLog(hWnd, *msg);
        delete msg;
        return 0;
    }
    case WM_SYNC_COMPLETE:
    {
        EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), TRUE);
        MessageBoxW(hWnd, L"同步完成，请查看日志。", L"完成", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case WM_UPDATE_PROGRESS:
    {
        SendMessageW(hProgressBar, PBM_SETPOS, (WPARAM)wParam, 0);
        return 0;
    }
    case WM_CLOSE:
    {
        int result = MessageBoxW(hWnd, L"选择【是】最小化到系统托盘，选择【否】直接退出程序。", L"退出提示", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (result == IDYES)
        {
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        else if (result == IDNO)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        else
        {
            return 0; // 取消关闭
        }
    }
    case WM_TRAYICON:
    {
        if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"显示窗口");
            AppendMenuW(hMenu, MF_STRING, 2, L"退出程序");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
            if (cmd == 1)
            {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            }
            else if (cmd == 2)
            {
                DestroyWindow(hWnd);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        SaveSettings();
        StopMonitoring();
        RemoveTrayIcon();
        if (hMainFont)
        {
            DeleteObject(hMainFont);
            hMainFont = nullptr;
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void CreateMainControls(HWND hWnd)
{
    CreateWindowW(L"STATIC", L"同步任务:", WS_CHILD | WS_VISIBLE,
        16, 16, 80, 24, hWnd, nullptr, hInst, nullptr);
    
    // "+" 按钮
    CreateWindowW(L"BUTTON", L"➕", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        100, 14, 30, 24, hWnd, (HMENU)IDC_ADD_PAIR, hInst, nullptr);
    
    // "-" 按钮
    CreateWindowW(L"BUTTON", L"➖", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        136, 14, 30, 24, hWnd, (HMENU)IDC_REMOVE_PAIR, hInst, nullptr);

    hPairList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        16, 46, 846, 180, hWnd, (HMENU)IDC_PAIR_LIST, hInst, nullptr);

    hDeleteExtra = CreateWindowW(L"BUTTON", L"同步删除目标中多余文件", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 236, 180, 24, hWnd, (HMENU)IDC_DELETE_EXTRA, hInst, nullptr);
    hAutoMonitor = CreateWindowW(L"BUTTON", L"开启实时监控", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        216, 236, 120, 24, hWnd, (HMENU)IDC_AUTO_MONITOR, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"手动同步", WS_CHILD | WS_VISIBLE,
        356, 233, 110, 30, hWnd, (HMENU)IDC_START_SYNC, hInst, nullptr);

    CreateWindowW(L"STATIC", L"日志:", WS_CHILD | WS_VISIBLE,
        16, 276, 80, 24, hWnd, nullptr, hInst, nullptr);
    hLogEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        16, 300, 846, 280, hWnd, (HMENU)IDC_LOG_EDIT, hInst, nullptr);

    hProgressBar = CreateWindowW(PROGRESS_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
        16, 590, 846, 24, hWnd, (HMENU)IDC_PROGRESS_BAR, hInst, nullptr);
    SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    ApplyMainFont(hWnd);
    LoadSettings(hWnd);
}

void ResizeMainControls(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int margin = 16;

    MoveWindow(hPairList, margin, 46, width - margin * 2, 180, TRUE);
    MoveWindow(hLogEdit, margin, 300, width - margin * 2, max(80, height - 350), TRUE);
    MoveWindow(hProgressBar, margin, height - 34, width - margin * 2, 24, TRUE);
}

std::wstring GetWindowTextString(HWND hWnd)
{
    const int length = GetWindowTextLengthW(hWnd);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hWnd, text.data(), length + 1);
    text.resize(length);
    return text;
}

void AppendLog(HWND hWnd, const std::wstring& text)
{
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
    localtime_s(&tm, &time);
    
    std::wstringstream ss;
    ss << L"[" << std::put_time(&tm, L"%H:%M:%S") << L"] " << text << L"\r\n";
    std::wstring line = ss.str();

    // 限制日志最大长度，保留部分日志即可 (例如最多保留 30,000 个字符)
    const int maxLogLength = 30000;
    int currentLength = GetWindowTextLengthW(hLogEdit);
    
    if (currentLength + line.length() > maxLogLength)
    {
        // 删掉前半部分日志，保留最新的部分
        int deleteLength = currentLength - (maxLogLength / 2);
        SendMessageW(hLogEdit, EM_SETSEL, 0, deleteLength);
        SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
        currentLength = GetWindowTextLengthW(hLogEdit);
    }

    SendMessageW(hLogEdit, EM_SETSEL, currentLength, currentLength);
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

INT_PTR CALLBACK AddPairDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            const std::wstring source = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT));
            const std::wstring target = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT));
            const bool isBidirectional = IsDlgButtonChecked(hDlg, IDC_DLG_BIDIRECTIONAL) == BST_CHECKED;

            if (source.empty() || target.empty())
            {
                MessageBoxW(hDlg, L"请先选择源文件夹和目标文件夹。", L"提示", MB_OK | MB_ICONINFORMATION);
                return (INT_PTR)TRUE;
            }

            SyncPair pair{ source, target, isBidirectional };
            gSyncPairs.push_back(pair);
            std::wstring display = source + L"  ->  " + target;
            if (isBidirectional)
            {
                display = source + L"  <->  " + target;
            }
            SendMessageW(hPairList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
            SaveSettings();
            AppendLog(GetParent(hDlg), L"[添加任务] " + display);
            
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_DLG_BROWSE_SOURCE)
        {
            BrowseFolder(hDlg, GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_DLG_BROWSE_TARGET)
        {
            BrowseFolder(hDlg, GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_DLG_BIDIRECTIONAL)
        {
            if (IsDlgButtonChecked(hDlg, IDC_DLG_BIDIRECTIONAL) == BST_CHECKED)
            {
                MessageBoxW(hDlg, L"双向同步时，两个文件夹的内容会互相合并。开启此选项后，双方都可以作为源文件夹操作。", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void RemoveSelectedSyncPair(HWND hWnd)
{
    const int index = (int)SendMessageW(hPairList, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR)
    {
        MessageBoxW(hWnd, L"请先在任务列表中选择一个任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (index >= 0 && index < (int)gSyncPairs.size())
    {
        gSyncPairs.erase(gSyncPairs.begin() + index);
    }
    SendMessageW(hPairList, LB_DELETESTRING, index, 0);
    SaveSettings();
    AppendLog(hWnd, L"[删除任务] 已删除选中同步任务");
}

void StartSync(HWND hWnd)
{
    if (gSyncPairs.empty())
    {
        MessageBoxW(hWnd, L"请至少添加一组同步任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), FALSE);
    SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
    AppendLog(hWnd, L"[系统] 同步开始...");

    std::thread([hWnd]() {
        SyncOptions options;
        options.deleteExtraFiles = SendMessageW(hDeleteExtra, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SaveSettings();

        SyncFolderPairs(gSyncPairs, options, [hWnd](const std::wstring& message)
            {
                // 使用 PostMessage 确保跨线程安全更新 UI
                std::wstring* msg = new std::wstring(message);
                PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
            }, [hWnd](float progress)
            {
                PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
            });

        PostMessageW(hWnd, WM_SYNC_COMPLETE, 0, 0); // 通知同步完成
    }).detach();
}

void ToggleMonitoring(HWND hWnd)
{
    const bool isChecked = SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (isChecked)
    {
        if (gSyncPairs.empty())
        {
            MessageBoxW(hWnd, L"请至少添加一组同步任务后再开启监控。", L"提示", MB_OK | MB_ICONINFORMATION);
            SendMessageW(hAutoMonitor, BM_SETCHECK, BST_UNCHECKED, 0);
            return;
        }

        EnableWindow(GetDlgItem(hWnd, IDC_ADD_PAIR), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_REMOVE_PAIR), FALSE);
        EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), FALSE);
        EnableWindow(hDeleteExtra, FALSE);

        SyncOptions options;
        options.deleteExtraFiles = SendMessageW(hDeleteExtra, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SaveSettings();

        StartMonitoring(gSyncPairs, options, [hWnd](const std::wstring& message)
        {
            std::wstring* msg = new std::wstring(message);
            PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
        }, nullptr);

        AppendLog(hWnd, L"[系统] 实时监控已开启...");
    }
    else
    {
        StopMonitoring();

        EnableWindow(GetDlgItem(hWnd, IDC_ADD_PAIR), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_REMOVE_PAIR), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), TRUE);
        EnableWindow(hDeleteExtra, TRUE);

        AppendLog(hWnd, L"[系统] 实时监控已关闭。");
    }
}

void BrowseFolder(HWND owner, HWND targetEdit)
{
    BROWSEINFOW bi{};
    WCHAR path[MAX_PATH]{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"请选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        if (SHGetPathFromIDListW(pidl, path))
        {
            SetWindowTextW(targetEdit, path);
        }
        CoTaskMemFree(pidl);
    }
}

void SetupTrayIcon(HWND hWnd)
{
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_FREESYNC)); // 假设图标资源ID是 IDI_FREESYNC
    if (!nid.hIcon)
    {
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // 如果加载失败，使用系统默认图标
    }
    wcscpy_s(nid.szTip, L"FreeSync 实时监控中");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ApplyMainFont(HWND hWnd)
{
    hMainFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    if (!hMainFont)
    {
        return;
    }

    EnumChildWindows(hWnd, [](HWND child, LPARAM font) -> BOOL
        {
            SendMessageW(child, WM_SETFONT, font, TRUE);
            return TRUE;
        }, (LPARAM)hMainFont);
}

void RefreshPairList()
{
    SendMessageW(hPairList, LB_RESETCONTENT, 0, 0);
    for (const auto& pair : gSyncPairs)
    {
        const std::wstring display = pair.source + L"  ->  " + pair.target;
        SendMessageW(hPairList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
}

std::wstring GetConfigPath()
{
    WCHAR appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
    {
        std::wstring folder = std::wstring(appData) + L"\\FreeSync";
        CreateDirectoryW(folder.c_str(), nullptr);
        return folder + L"\\settings.ini";
    }

    return L"settings.ini";
}

void LoadSettings(HWND hWnd)
{
    const std::wstring configPath = GetConfigPath();
    const DWORD deleteExtra = GetPrivateProfileIntW(L"Settings", L"DeleteExtraFiles", 0, configPath.c_str());
    SendMessageW(hDeleteExtra, BM_SETCHECK, deleteExtra ? BST_CHECKED : BST_UNCHECKED, 0);

    gSyncPairs.clear();
    const int count = (int)GetPrivateProfileIntW(L"Tasks", L"Count", 0, configPath.c_str());
    for (int i = 0; i < count; ++i)
    {
        WCHAR source[MAX_PATH * 4]{};
        WCHAR target[MAX_PATH * 4]{};
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Tasks", sourceKey.c_str(), L"", source, ARRAYSIZE(source), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetKey.c_str(), L"", target, ARRAYSIZE(target), configPath.c_str());
        if (source[0] != L'\0' && target[0] != L'\0')
        {
            gSyncPairs.push_back(SyncPair{ source, target });
        }
    }

    RefreshPairList();
    AppendLog(hWnd, L"配置文件: " + configPath);
    if (!gSyncPairs.empty())
    {
        AppendLog(hWnd, L"已加载同步任务: " + std::to_wstring(gSyncPairs.size()) + L" 组");
    }
}

void SaveSettings()
{
    const std::wstring configPath = GetConfigPath();
    WritePrivateProfileStringW(L"Settings", L"DeleteExtraFiles",
        SendMessageW(hDeleteExtra, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", nullptr, nullptr, configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", L"Count", std::to_wstring(gSyncPairs.size()).c_str(), configPath.c_str());

    for (size_t i = 0; i < gSyncPairs.size(); ++i)
    {
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Tasks", sourceKey.c_str(), gSyncPairs[i].source.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", targetKey.c_str(), gSyncPairs[i].target.c_str(), configPath.c_str());
    }
}

// Message handler for about box.
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
