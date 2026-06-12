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
#include <atomic>
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_LOADSTRING 100

// We use the defines from resource.h directly instead of redeclaring them
#define IDC_AUTO_MONITOR 1012
#define IDD_ADD_PAIR_DIALOG 2000

#define WM_APPEND_LOG (WM_USER + 2)
#define WM_SYNC_COMPLETE (WM_USER + 1)
#define WM_UPDATE_PROGRESS (WM_USER + 3)
#define WM_TRAYICON (WM_USER + 4)
#define WM_TASK_SYNC_COMPLETE (WM_USER + 5)
#define WM_SHOW_MAIN_WINDOW (WM_USER + 6)

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hSourceEdit;
HWND hTargetEdit;
HWND hPairList;
HWND hAutoMonitor;
HWND hLogEdit;
HWND hProgressBar;
HWND hStatusLabel;
HFONT hMainFont;
NOTIFYICONDATAW nid = {};
std::vector<SyncPair> gSyncPairs;
HANDLE gSingleInstanceMutex = nullptr;
constexpr const wchar_t* SINGLE_INSTANCE_MUTEX_NAME = L"Local\\FreeSync_SingleInstance_v26_0611";

struct TaskRunState
{
    std::wstring status = L"未运行";
    unsigned long long copied = 0;
    unsigned long long skipped = 0;
    unsigned long long deleted = 0;
    unsigned long long failed = 0;
};

std::vector<TaskRunState> gTaskStates;
std::atomic<int> gActiveSyncJobs{ 0 };

struct TaskSyncResult
{
    int index = -1;
    SyncStats stats;
};

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
void                StartSinglePairSync(HWND hWnd, int index);
void                EditSelectedSyncPair(HWND hWnd);
void                ShowTaskContextMenu(HWND hWnd, int x, int y);
void                SetStatusText(const std::wstring& text);
void                SetSyncControlsEnabled(HWND hWnd, bool enabled);
void                SetTaskEditControlsEnabled(HWND hWnd, bool enabled);
void                SetManualSyncControlsEnabled(HWND hWnd, bool enabled);
void                EnsureTaskStates();
void                SetTaskRunState(size_t index, const SyncStats& stats);
std::wstring        FormatTaskRunState(size_t index);
std::wstring        GetTaskSummaryText();
int                 CountAutoMonitorTasks();
void                SetMonitoringUi(HWND hWnd, bool monitoring);
bool                IsMonitoringChecked();
std::wstring        GetConfigPath();
bool                PrepareSyncPairsForUse(HWND hWnd, bool showMessageOnFailure);
bool                CheckAndRecoverSinglePair(HWND hWnd, int index, bool showMessageOnFailure);
void                LoadSettings(HWND hWnd);
void                SaveSettings();
void                ToggleAutoStart(HWND hWnd);
bool                IsAutoStartEnabled();
bool                ActivateExistingInstance();
void                ShowMainWindow(HWND hWnd);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // TODO: Place code here.
    bool startMinimized = false;
    if (wcsstr(lpCmdLine, L"/minimized") != nullptr)
    {
        startMinimized = true;
    }

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_FREESYNC, szWindowClass, MAX_LOADSTRING);

    gSingleInstanceMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (gSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ActivateExistingInstance();
        CloseHandle(gSingleInstanceMutex);
        gSingleInstanceMutex = nullptr;
        return FALSE;
    }

    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, startMinimized ? SW_HIDE : nCmdShow))
    {
        if (gSingleInstanceMutex)
        {
            CloseHandle(gSingleInstanceMutex);
            gSingleInstanceMutex = nullptr;
        }
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

    if (gSingleInstanceMutex)
    {
        CloseHandle(gSingleInstanceMutex);
        gSingleInstanceMutex = nullptr;
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
    CW_USEDEFAULT, 0, 960, 700, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

bool ActivateExistingInstance()
{
    HWND hWnd = nullptr;
    for (int i = 0; i < 20 && !hWnd; ++i)
    {
        hWnd = FindWindowW(szWindowClass, nullptr);
        if (!hWnd)
        {
            hWnd = FindWindowW(nullptr, szTitle);
        }
        if (!hWnd)
        {
            Sleep(100);
        }
    }

    if (!hWnd)
    {
        return false;
    }

    PostMessageW(hWnd, WM_SHOW_MAIN_WINDOW, 0, 0);
    ShowMainWindow(hWnd);
    return true;
}

void ShowMainWindow(HWND hWnd)
{
    if (!hWnd)
    {
        return;
    }

    ShowWindow(hWnd, SW_RESTORE);
    ShowWindow(hWnd, SW_SHOW);
    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);
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
    case WM_SHOW_MAIN_WINDOW:
        ShowMainWindow(hWnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
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
                DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_ADD_PAIR_DIALOG), hWnd, AddPairDlgProc, -1); // -1 means add new
                break;
            case IDC_EDIT_PAIR:
                EditSelectedSyncPair(hWnd);
                break;
            case IDC_REMOVE_PAIR:
                RemoveSelectedSyncPair(hWnd);
                break;
            case IDC_SYNC_SELECTED:
            {
                const int index = ListView_GetNextItem(hPairList, -1, LVNI_SELECTED);
                if (index >= 0)
                {
                    StartSinglePairSync(hWnd, index);
                }
                break;
            }
            case IDC_AUTO_MONITOR:
                ToggleMonitoring(hWnd);
                break;
            case IDC_RUN_AT_STARTUP:
                ToggleAutoStart(hWnd);
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
    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_SYNC_LIST && pnmh->code == NM_DBLCLK)
        {
            EditSelectedSyncPair(hWnd);
            return 0;
        }
    if (pnmh->idFrom == IDC_SYNC_LIST && pnmh->code == NM_RCLICK)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"显示窗口");
            AppendMenuW(hMenu, MF_STRING, 2, L"同步全部");
            AppendMenuW(hMenu, MF_STRING, 3,
                IsMonitoringChecked() ? L"停止实时同步监控" : L"启动实时同步监控");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 4, L"退出程序");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
            if (cmd == 1)
            {
                ShowMainWindow(hWnd);
            }
            else if (cmd == 2)
            {
                StartSync(hWnd);
            }
            else if (cmd == 3)
            {
                const bool checked = IsMonitoringChecked();
                SendMessageW(hAutoMonitor, BM_SETCHECK, checked ? BST_UNCHECKED : BST_CHECKED, 0);
                ToggleMonitoring(hWnd);
            }
            else if (cmd == 4)
            {
                DestroyWindow(hWnd);
            }
        }
        return 0;
    }
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
        const bool monitoring = IsMonitoringChecked();
        EnableWindow(hAutoMonitor, TRUE);
        SetTaskEditControlsEnabled(hWnd, !monitoring);
        SetManualSyncControlsEnabled(hWnd, true);
        SendMessageW(hProgressBar, PBM_SETPOS, 100, 0);
        RefreshPairList();
        SetStatusText(L"同步完成");
        return 0;
    }
    case WM_UPDATE_PROGRESS:
    {
        SendMessageW(hProgressBar, PBM_SETPOS, (WPARAM)wParam, 0);
        SetStatusText(L"正在同步... " + std::to_wstring((int)wParam) + L"%");
        return 0;
    }
    case WM_TASK_SYNC_COMPLETE:
    {
        TaskSyncResult* result = (TaskSyncResult*)wParam;
        if (result)
        {
            SetTaskRunState((size_t)result->index, result->stats);
            RefreshPairList();
            delete result;
        }
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
            ShowMainWindow(hWnd);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"显示窗口");
            AppendMenuW(hMenu, MF_STRING, 2, L"同步全部");
            AppendMenuW(hMenu, MF_STRING, 3,
                IsMonitoringChecked() ? L"停止实时同步监控" : L"启动实时同步监控");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 4, L"退出程序");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
            if (cmd == 1)
            {
                ShowMainWindow(hWnd);
            }
            else if (cmd == 2)
            {
                StartSync(hWnd);
            }
            else if (cmd == 3)
            {
                const bool checked = IsMonitoringChecked();
                SendMessageW(hAutoMonitor, BM_SETCHECK, checked ? BST_UNCHECKED : BST_CHECKED, 0);
                ToggleMonitoring(hWnd);
            }
            else if (cmd == 4)
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
    CreateWindowW(L"STATIC", L"同步任务", WS_CHILD | WS_VISIBLE,
        20, 18, 90, 24, hWnd, nullptr, hInst, nullptr);

    CreateWindowW(L"BUTTON", L"添加任务", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        112, 14, 92, 30, hWnd, (HMENU)IDC_ADD_PAIR, hInst, nullptr);

    CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        212, 14, 72, 30, hWnd, (HMENU)IDC_EDIT_PAIR, hInst, nullptr);

    CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        292, 14, 72, 30, hWnd, (HMENU)IDC_REMOVE_PAIR, hInst, nullptr);

    hPairList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        20, 52, 900, 220, hWnd, (HMENU)IDC_SYNC_LIST, hInst, nullptr);
    SendMessageW(hPairList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);

    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = (LPWSTR)L"任务与路径";
    lvc.cx = 470;
    ListView_InsertColumn(hPairList, 0, &lvc);

    lvc.pszText = (LPWSTR)L"模式";
    lvc.cx = 90;
    ListView_InsertColumn(hPairList, 1, &lvc);

    lvc.pszText = (LPWSTR)L"删除策略";
    lvc.cx = 90;
    ListView_InsertColumn(hPairList, 2, &lvc);

    lvc.pszText = (LPWSTR)L"实时同步";
    lvc.cx = 90;
    ListView_InsertColumn(hPairList, 3, &lvc);

    lvc.pszText = (LPWSTR)L"最近结果";
    lvc.cx = 260;
    ListView_InsertColumn(hPairList, 4, &lvc);

    HWND hRunAtStartup = CreateWindowW(L"BUTTON", L"开机自动运行", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 286, 140, 24, hWnd, (HMENU)IDC_RUN_AT_STARTUP, hInst, nullptr);
    if (IsAutoStartEnabled())
    {
        SendMessageW(hRunAtStartup, BM_SETCHECK, BST_CHECKED, 0);
    }

    hAutoMonitor = CreateWindowW(L"BUTTON", L"启动实时同步监控", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        174, 286, 170, 24, hWnd, (HMENU)IDC_AUTO_MONITOR, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"同步选中", WS_CHILD | WS_VISIBLE,
        360, 282, 100, 32, hWnd, (HMENU)IDC_SYNC_SELECTED, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"同步全部", WS_CHILD | WS_VISIBLE,
        468, 282, 100, 32, hWnd, (HMENU)IDC_START_SYNC, hInst, nullptr);

    CreateWindowW(L"STATIC", L"运行日志", WS_CHILD | WS_VISIBLE,
        20, 328, 80, 24, hWnd, nullptr, hInst, nullptr);
    hLogEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        20, 354, 900, 260, hWnd, (HMENU)IDC_LOG_EDIT, hInst, nullptr);

    hStatusLabel = CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 628, 300, 22, hWnd, nullptr, hInst, nullptr);

    hProgressBar = CreateWindowW(PROGRESS_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
        20, 652, 900, 18, hWnd, (HMENU)IDC_PROGRESS_BAR, hInst, nullptr);
    SendMessageW(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(hProgressBar, PBM_SETSTEP, 1, 0);

    ApplyMainFont(hWnd);
    LoadSettings(hWnd);
}

void ResizeMainControls(HWND hWnd)
{
    if (!hPairList || !hLogEdit || !hProgressBar)
    {
        return;
    }

    RECT rc;
    GetClientRect(hWnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int margin = 20;
    const int contentWidth = max(320, width - margin * 2);
    const int listHeight = max(160, min(260, (height - 190) / 2));
    const int optionsTop = 60 + listHeight + 14;
    const int logTitleTop = optionsTop + 46;
    const int logTop = logTitleTop + 26;
    const int statusTop = height - 48;
    const int progressTop = height - 24;
    const int logHeight = max(90, statusTop - logTop - 8);

    MoveWindow(hPairList, margin, 52, contentWidth, listHeight, TRUE);
    MoveWindow(GetDlgItem(hWnd, IDC_RUN_AT_STARTUP), margin, optionsTop, 140, 24, TRUE);
    MoveWindow(hAutoMonitor, margin + 154, optionsTop, 170, 24, TRUE);
    MoveWindow(GetDlgItem(hWnd, IDC_SYNC_SELECTED), margin + 340, optionsTop - 4, 100, 32, TRUE);
    MoveWindow(GetDlgItem(hWnd, IDC_START_SYNC), margin + 448, optionsTop - 4, 100, 32, TRUE);
    MoveWindow(hLogEdit, margin, logTop, contentWidth, logHeight, TRUE);
    MoveWindow(hStatusLabel, margin, statusTop, contentWidth, 22, TRUE);
    MoveWindow(hProgressBar, margin, progressTop, contentWidth, 18, TRUE);

    const int resultWidth = 260;
    const int monitorWidth = 90;
    const int deleteWidth = 90;
    const int modeWidth = 90;
    const int pathWidth = max(260, contentWidth - resultWidth - monitorWidth - deleteWidth - modeWidth - 8);

    ListView_SetColumnWidth(hPairList, 0, pathWidth);
    ListView_SetColumnWidth(hPairList, 1, modeWidth);
    ListView_SetColumnWidth(hPairList, 2, deleteWidth);
    ListView_SetColumnWidth(hPairList, 3, monitorWidth);
    ListView_SetColumnWidth(hPairList, 4, resultWidth);
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

void SetStatusText(const std::wstring& text)
{
    if (hStatusLabel)
    {
        SetWindowTextW(hStatusLabel, text.c_str());
    }
}

void SetSyncControlsEnabled(HWND hWnd, bool enabled)
{
    SetTaskEditControlsEnabled(hWnd, enabled);
    SetManualSyncControlsEnabled(hWnd, enabled);
}

void SetTaskEditControlsEnabled(HWND hWnd, bool enabled)
{
    EnableWindow(GetDlgItem(hWnd, IDC_ADD_PAIR), enabled);
    EnableWindow(GetDlgItem(hWnd, IDC_EDIT_PAIR), enabled);
    EnableWindow(GetDlgItem(hWnd, IDC_REMOVE_PAIR), enabled);
}

void SetManualSyncControlsEnabled(HWND hWnd, bool enabled)
{
    EnableWindow(GetDlgItem(hWnd, IDC_SYNC_SELECTED), enabled);
    EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), enabled);
}

void EnsureTaskStates()
{
    if (gTaskStates.size() < gSyncPairs.size())
    {
        gTaskStates.resize(gSyncPairs.size());
    }
    else if (gTaskStates.size() > gSyncPairs.size())
    {
        gTaskStates.resize(gSyncPairs.size());
    }
}

void SetTaskRunState(size_t index, const SyncStats& stats)
{
    EnsureTaskStates();
    if (index >= gTaskStates.size())
    {
        return;
    }

    TaskRunState& state = gTaskStates[index];
    state.copied = stats.copiedFiles.load();
    state.skipped = stats.skippedFiles.load();
    state.deleted = stats.deletedFiles.load();
    state.failed = stats.failedFiles.load();
    state.status = state.failed > 0 ? L"部分失败" : L"成功";
}

std::wstring FormatTaskRunState(size_t index)
{
    EnsureTaskStates();
    if (index >= gTaskStates.size())
    {
        return L"未运行";
    }

    const TaskRunState& state = gTaskStates[index];
    if (state.status == L"未运行")
    {
        return state.status;
    }

    return state.status + L"：复制 " + std::to_wstring(state.copied) +
        L"，删除 " + std::to_wstring(state.deleted) +
        L"，失败 " + std::to_wstring(state.failed);
}

std::wstring GetTaskSummaryText()
{
    int deleteEnabled = 0;
    int autoMonitorEnabled = 0;
    for (const auto& pair : gSyncPairs)
    {
        if (pair.deleteExtraFiles)
        {
            deleteEnabled++;
        }
        if (pair.autoMonitor)
        {
            autoMonitorEnabled++;
        }
    }

    return L"任务 " + std::to_wstring(gSyncPairs.size()) +
        L" 个；镜像删除 " + std::to_wstring(deleteEnabled) +
        L" 个；实时同步 " + std::to_wstring(autoMonitorEnabled) + L" 个。";
}

int CountAutoMonitorTasks()
{
    int count = 0;
    for (const auto& pair : gSyncPairs)
    {
        if (pair.autoMonitor)
        {
            count++;
        }
    }
    return count;
}

void SetMonitoringUi(HWND hWnd, bool monitoring)
{
    SendMessageW(hAutoMonitor, BM_SETCHECK, monitoring ? BST_CHECKED : BST_UNCHECKED, 0);
    SetTaskEditControlsEnabled(hWnd, !monitoring);
    SetManualSyncControlsEnabled(hWnd, true);
    SetStatusText(monitoring ? L"实时同步监控运行中" : L"就绪");
}

bool IsMonitoringChecked()
{
    return SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void EditSelectedSyncPair(HWND hWnd)
{
    if (SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        MessageBoxW(hWnd, L"实时同步状态下无法编辑任务，请先关闭实时同步。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const int index = ListView_GetNextItem(hPairList, -1, LVNI_SELECTED);
    if (index == -1)
    {
        MessageBoxW(hWnd, L"请先在任务列表中选择一个任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_ADD_PAIR_DIALOG), hWnd, AddPairDlgProc, (LPARAM)index);
}

void ShowTaskContextMenu(HWND hWnd, int x, int y)
{
    const int index = ListView_GetNextItem(hPairList, -1, LVNI_SELECTED);
    if (index == -1)
    {
        return;
    }

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDC_SYNC_SELECTED, L"同步此任务");
    AppendMenuW(hMenu, MF_STRING, IDC_EDIT_PAIR, L"编辑任务");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDC_REMOVE_PAIR, L"删除任务");

    if (IsMonitoringChecked())
    {
        if (index >= 0 && index < (int)gSyncPairs.size() && gSyncPairs[index].autoMonitor)
        {
            EnableMenuItem(hMenu, IDC_SYNC_SELECTED, MF_BYCOMMAND | MF_GRAYED);
        }
        EnableMenuItem(hMenu, IDC_EDIT_PAIR, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(hMenu, IDC_REMOVE_PAIR, MF_BYCOMMAND | MF_GRAYED);
    }

    SetForegroundWindow(hWnd);
    const int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd != 0)
    {
        SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
    }
}

INT_PTR CALLBACK AddPairDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int editIndex = -1;

    switch (message)
    {
    case WM_INITDIALOG:
        editIndex = (int)lParam;
        if (editIndex >= 0 && editIndex < (int)gSyncPairs.size())
        {
            const auto& pair = gSyncPairs[editIndex];
            SetWindowTextW(GetDlgItem(hDlg, IDC_DLG_TASK_NAME), pair.taskName.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT), pair.source.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT), pair.target.c_str());
            CheckDlgButton(hDlg, IDC_DLG_BIDIRECTIONAL, pair.isBidirectional ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_DLG_DELETE_EXTRA, pair.deleteExtraFiles ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_DLG_AUTO_MONITOR, pair.autoMonitor ? BST_CHECKED : BST_UNCHECKED);
        }
        DragAcceptFiles(hDlg, TRUE);
        return (INT_PTR)TRUE;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        const UINT dropCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        if (dropCount == 0)
        {
            DragFinish(hDrop);
            return (INT_PTR)TRUE;
        }

        auto getDroppedFolder = [&](UINT index) -> std::wstring
            {
                WCHAR path[MAX_PATH * 4]{};
                if (DragQueryFileW(hDrop, index, path, ARRAYSIZE(path)) == 0)
                {
                    return L"";
                }

                std::wstring folder = path;
                const DWORD attributes = GetFileAttributesW(folder.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    return folder;
                }

                const size_t slashPos = folder.find_last_of(L"\\/");
                if (slashPos != std::wstring::npos)
                {
                    folder.resize(slashPos);
                }
                return folder;
            };

        const std::wstring firstFolder = getDroppedFolder(0);
        if (!firstFolder.empty())
        {
            HWND sourceEdit = GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT);
            HWND targetEdit = GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT);
            HWND dropTarget = nullptr;

            POINT pt{};
            if (DragQueryPoint(hDrop, &pt))
            {
                dropTarget = ChildWindowFromPoint(hDlg, pt);
            }

            if (dropTarget == sourceEdit)
            {
                SetWindowTextW(sourceEdit, firstFolder.c_str());
            }
            else if (dropTarget == targetEdit)
            {
                SetWindowTextW(targetEdit, firstFolder.c_str());
            }
            else if (GetWindowTextLengthW(sourceEdit) == 0)
            {
                SetWindowTextW(sourceEdit, firstFolder.c_str());
                if (dropCount > 1)
                {
                    const std::wstring secondFolder = getDroppedFolder(1);
                    if (!secondFolder.empty())
                    {
                        SetWindowTextW(targetEdit, secondFolder.c_str());
                    }
                }
            }
            else if (GetWindowTextLengthW(targetEdit) == 0)
            {
                SetWindowTextW(targetEdit, firstFolder.c_str());
            }
            else
            {
                SetWindowTextW(sourceEdit, firstFolder.c_str());
                if (dropCount > 1)
                {
                    const std::wstring secondFolder = getDroppedFolder(1);
                    if (!secondFolder.empty())
                    {
                        SetWindowTextW(targetEdit, secondFolder.c_str());
                    }
                }
            }
        }

        DragFinish(hDrop);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            std::wstring taskName = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_TASK_NAME));
            const std::wstring source = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT));
            const std::wstring target = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT));
            const bool isBidirectional = IsDlgButtonChecked(hDlg, IDC_DLG_BIDIRECTIONAL) == BST_CHECKED;
            const bool deleteExtraFiles = IsDlgButtonChecked(hDlg, IDC_DLG_DELETE_EXTRA) == BST_CHECKED;
            const bool autoMonitor = IsDlgButtonChecked(hDlg, IDC_DLG_AUTO_MONITOR) == BST_CHECKED;

            if (source.empty() || target.empty())
            {
                MessageBoxW(hDlg, L"请先选择源文件夹和目标文件夹。", L"提示", MB_OK | MB_ICONINFORMATION);
                return (INT_PTR)TRUE;
            }

            if (taskName.empty())
            {
                // 如果没有填写任务名称，使用源文件夹名作为默认名称
                size_t pos = source.find_last_of(L"\\/");
                if (pos != std::wstring::npos && pos + 1 < source.length())
                {
                    taskName = source.substr(pos + 1);
                }
                else
                {
                    taskName = L"新建任务";
                }
            }

            SyncPair pair{ taskName, source, target, isBidirectional, deleteExtraFiles, autoMonitor };
            CaptureSyncPairVolumeInfo(pair);
            
            if (editIndex >= 0 && editIndex < (int)gSyncPairs.size())
            {
                gSyncPairs[editIndex] = pair;
                AppendLog(GetParent(hDlg), L"[修改任务] " + taskName + L": " + source + L" <-> " + target);
            }
            else
            {
                gSyncPairs.push_back(pair);
                EnsureTaskStates();
                AppendLog(GetParent(hDlg), L"[添加任务] " + taskName + L": " + source + L" <-> " + target);
            }

            RefreshPairList();
            SaveSettings();
            
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
    const int index = ListView_GetNextItem(hPairList, -1, LVNI_SELECTED);
    if (index == -1)
    {
        MessageBoxW(hWnd, L"请先在任务列表中选择一个任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (index >= 0 && index < (int)gSyncPairs.size())
    {
        const SyncPair removedPair = gSyncPairs[index];
        DeleteSnapshotForPair(removedPair, [hWnd](const std::wstring& message)
            {
                AppendLog(hWnd, message);
        });
        gSyncPairs.erase(gSyncPairs.begin() + index);
        if (index >= 0 && index < (int)gTaskStates.size())
        {
            gTaskStates.erase(gTaskStates.begin() + index);
        }
    }
    RefreshPairList();
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

    const bool monitoring = IsMonitoringChecked();
    std::vector<size_t> taskIndexes;
    taskIndexes.reserve(gSyncPairs.size());
    for (size_t i = 0; i < gSyncPairs.size(); ++i)
    {
        if (!monitoring || !gSyncPairs[i].autoMonitor)
        {
            taskIndexes.push_back(i);
        }
    }

    if (taskIndexes.empty())
    {
        MessageBoxW(hWnd, L"实时同步监控运行中，所有任务都已由监控接管，没有可手动同步的任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 检查并尝试恢复路径，即使有失败也继续尝试同步其他正常的任务
    PrepareSyncPairsForUse(hWnd, true);
    CleanupOldTrashForPairs(gSyncPairs, 7, [hWnd](const std::wstring& message)
    {
        AppendLog(hWnd, message);
    });

    SetSyncControlsEnabled(hWnd, false);
    EnableWindow(hAutoMonitor, FALSE);
    SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
    SetStatusText(monitoring ? L"正在同步非实时任务..." : L"正在同步全部任务...");
    if (monitoring)
    {
        AppendLog(hWnd, L"[系统] 实时同步监控运行中，本次仅手动同步未开启实时同步的任务。");
    }
    AppendLog(hWnd, L"[系统] 同步开始。" + GetTaskSummaryText());

    std::thread([hWnd, taskIndexes]() {
        SaveSettings();

        SyncStats total;
        std::vector<SyncPair> pairs = gSyncPairs;
        for (size_t index : taskIndexes)
        {
            if (index >= pairs.size())
            {
                continue;
            }

            SyncStats current = SyncFolderPair(pairs[index], [hWnd](const std::wstring& message)
            {
                std::wstring* msg = new std::wstring(message);
                PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
            }, [hWnd](float progress)
            {
                PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
            });

            total.copiedFiles += current.copiedFiles.load();
            total.skippedFiles += current.skippedFiles.load();
            total.deletedFiles += current.deletedFiles.load();
            total.failedFiles += current.failedFiles.load();
            SetTaskRunState(index, current);
        }

        std::wstring* summary = new std::wstring(
            L"全部任务完成: 复制 " + std::to_wstring(total.copiedFiles.load()) +
            L"，跳过 " + std::to_wstring(total.skippedFiles.load()) +
            L"，删除 " + std::to_wstring(total.deletedFiles.load()) +
            L"，失败 " + std::to_wstring(total.failedFiles.load()));
        PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)summary, 0);
        PostMessageW(hWnd, WM_UPDATE_PROGRESS, 100, 0);
        PostMessageW(hWnd, WM_SYNC_COMPLETE, 0, 0); // 通知同步完成
    }).detach();
}

void StartSinglePairSync(HWND hWnd, int index)
{
    if (index < 0 || index >= (int)gSyncPairs.size()) return;

    if (IsMonitoringChecked() && gSyncPairs[index].autoMonitor)
    {
        MessageBoxW(hWnd, L"该任务已启用实时同步监控，请停止监控后再手动同步此任务。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!CheckAndRecoverSinglePair(hWnd, index, true))
    {
        return;
    }
    CleanupOldTrashForPairs(gSyncPairs, 7, [hWnd](const std::wstring& message)
    {
        AppendLog(hWnd, message);
    });

    SetSyncControlsEnabled(hWnd, false);
    EnableWindow(hAutoMonitor, FALSE);
    SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
    SetStatusText(L"正在同步选中任务...");
    AppendLog(hWnd, L"[系统] 开始同步单项任务...");

    SyncPair pair = gSyncPairs[index];

    std::thread([hWnd, pair]() {
        SaveSettings();

        SyncStats stats = SyncFolderPair(pair, [hWnd](const std::wstring& message)
            {
                std::wstring* msg = new std::wstring(message);
                PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
            }, [hWnd](float progress)
            {
                PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
            });

        for (size_t i = 0; i < gSyncPairs.size(); ++i)
        {
            if (gSyncPairs[i].source == pair.source && gSyncPairs[i].target == pair.target && gSyncPairs[i].taskName == pair.taskName)
            {
                SetTaskRunState(i, stats);
                break;
            }
        }

        PostMessageW(hWnd, WM_SYNC_COMPLETE, 0, 0);
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
            SaveSettings();
            return;
        }

        if (CountAutoMonitorTasks() == 0)
        {
            MessageBoxW(hWnd, L"当前没有任务启用实时同步，请先在任务中勾选“实时同步”。", L"提示", MB_OK | MB_ICONINFORMATION);
            SendMessageW(hAutoMonitor, BM_SETCHECK, BST_UNCHECKED, 0);
            SaveSettings();
            return;
        }

        // 尝试恢复路径，即使有失败也允许开启监控（监控线程内部会重试）
        PrepareSyncPairsForUse(hWnd, true);
        CleanupOldTrashForPairs(gSyncPairs, 7, [hWnd](const std::wstring& message)
        {
            AppendLog(hWnd, message);
        });

        SetMonitoringUi(hWnd, true);

        StartMonitoring(gSyncPairs, [hWnd](const std::wstring& message)
        {
            std::wstring* msg = new std::wstring(message);
            PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
        }, [hWnd](float progress)
        {
            PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
        }, [hWnd](int index, const SyncStats& stats)
        {
            TaskSyncResult* result = new TaskSyncResult{ index, stats };
            PostMessageW(hWnd, WM_TASK_SYNC_COMPLETE, (WPARAM)result, 0);
        });

        AppendLog(hWnd, L"[系统] 实时同步监控已启动...");
    }
    else
    {
        StopMonitoring();

        SaveSettings();

        SetMonitoringUi(hWnd, false);

        AppendLog(hWnd, L"[系统] 实时同步监控已停止。");
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
    wcscpy_s(nid.szTip, L"FreeSync 实时同步中");
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
    EnsureTaskStates();
    ListView_DeleteAllItems(hPairList);
    for (int i = 0; i < (int)gSyncPairs.size(); ++i)
    {
        const auto& pair = gSyncPairs[i];
        std::wstring paths = pair.source + L"  ->  " + pair.target;
        if (pair.isBidirectional)
        {
            paths = pair.source + L"  <->  " + pair.target;
        }

        std::wstring display = paths;
        if (!pair.taskName.empty())
        {
            display = L"[" + pair.taskName + L"] " + paths;
        }

        std::wstring modeText = pair.isBidirectional ? L"双向合并" : L"单向复制";
        std::wstring deleteText = pair.deleteExtraFiles ? L"安全删除" : L"保留多余";
        std::wstring monitorText = pair.autoMonitor ? L"已开启" : L"未开启";

        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)display.c_str();
        lvi.lParam = (LPARAM)i;
        ListView_InsertItem(hPairList, &lvi);
        ListView_SetItemText(hPairList, i, 1, (LPWSTR)modeText.c_str());
        ListView_SetItemText(hPairList, i, 2, (LPWSTR)deleteText.c_str());
        ListView_SetItemText(hPairList, i, 3, (LPWSTR)monitorText.c_str());
        std::wstring stateText = FormatTaskRunState(i);
        ListView_SetItemText(hPairList, i, 4, (LPWSTR)stateText.c_str());
    }
}

bool PrepareSyncPairsForUse(HWND hWnd, bool showMessageOnFailure)
{
    bool changed = false;
    std::vector<std::wstring> originalSources;
    std::vector<std::wstring> originalTargets;
    originalSources.reserve(gSyncPairs.size());
    originalTargets.reserve(gSyncPairs.size());

    for (const auto& pair : gSyncPairs)
    {
        originalSources.push_back(pair.source);
        originalTargets.push_back(pair.target);
    }

    const bool allOk = ResolveSyncPairPaths(gSyncPairs, [hWnd](const std::wstring& message)
        {
            AppendLog(hWnd, message);
        });

    for (size_t i = 0; i < gSyncPairs.size(); ++i)
    {
        if (gSyncPairs[i].source != originalSources[i] || gSyncPairs[i].target != originalTargets[i])
        {
            changed = true;
            break;
        }
    }

    if (changed)
    {
        RefreshPairList();
        SaveSettings();
    }

    if (!allOk && showMessageOnFailure)
    {
        std::wstring error = L"部分同步任务路径不可用，请检查以下任务是否已连接对应磁盘：\n\n";
        for (const auto& pair : gSyncPairs)
        {
            std::error_code ec;
            if (!std::filesystem::exists(pair.source, ec) || !std::filesystem::exists(pair.target, ec))
            {
                error += L"• " + pair.source + L" -> " + pair.target + L"\n";
            }
        }
        MessageBoxW(hWnd, error.c_str(), L"路径不可用", MB_OK | MB_ICONWARNING);
    }

    return allOk;
}

bool CheckAndRecoverSinglePair(HWND hWnd, int index, bool showMessageOnFailure)
{
    if (index < 0 || index >= (int)gSyncPairs.size()) return false;

    SyncPair& pair = gSyncPairs[index];
    std::wstring oldSource = pair.source;
    std::wstring oldTarget = pair.target;

    bool ok = TryResolveSyncPairPaths(pair, [hWnd](const std::wstring& message) {
        AppendLog(hWnd, message);
    });

    if (pair.source != oldSource || pair.target != oldTarget)
    {
        RefreshPairList();
        SaveSettings();
    }

    if (!ok && showMessageOnFailure)
    {
        std::wstring error = L"任务路径不可用：\n";
        error += L"源: " + oldSource + L"\n";
        error += L"目标: " + oldTarget + L"\n\n请确保相关磁盘已插入。";
        MessageBoxW(hWnd, error.c_str(), L"路径错误", MB_OK | MB_ICONWARNING);
    }

    return ok;
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
    const DWORD autoMonitorAll = GetPrivateProfileIntW(L"Settings", L"AutoMonitorAll", 0, configPath.c_str());
    SendMessageW(hAutoMonitor, BM_SETCHECK, autoMonitorAll ? BST_CHECKED : BST_UNCHECKED, 0);

    gSyncPairs.clear();
    gTaskStates.clear();
    const int count = (int)GetPrivateProfileIntW(L"Tasks", L"Count", 0, configPath.c_str());
    for (int i = 0; i < count; ++i)
    {
        WCHAR taskName[MAX_PATH]{};
        WCHAR source[MAX_PATH * 4]{};
        WCHAR target[MAX_PATH * 4]{};
        WCHAR sourceVolumeGuid[MAX_PATH * 4]{};
        WCHAR sourceRelativePath[MAX_PATH * 4]{};
        WCHAR targetVolumeGuid[MAX_PATH * 4]{};
        WCHAR targetRelativePath[MAX_PATH * 4]{};
        const std::wstring taskNameKey = L"TaskName" + std::to_wstring(i);
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        const std::wstring bidirKey = L"Bidirectional" + std::to_wstring(i);
        const std::wstring deleteExtraKey = L"DeleteExtra" + std::to_wstring(i);
        const std::wstring autoMonitorKey = L"AutoMonitor" + std::to_wstring(i);
        const std::wstring sourceVolumeGuidKey = L"SourceVolumeGuid" + std::to_wstring(i);
        const std::wstring sourceRelativePathKey = L"SourceRelativePath" + std::to_wstring(i);
        const std::wstring targetVolumeGuidKey = L"TargetVolumeGuid" + std::to_wstring(i);
        const std::wstring targetRelativePathKey = L"TargetRelativePath" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Tasks", taskNameKey.c_str(), L"", taskName, ARRAYSIZE(taskName), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", sourceKey.c_str(), L"", source, ARRAYSIZE(source), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetKey.c_str(), L"", target, ARRAYSIZE(target), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", sourceVolumeGuidKey.c_str(), L"", sourceVolumeGuid, ARRAYSIZE(sourceVolumeGuid), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", sourceRelativePathKey.c_str(), L"", sourceRelativePath, ARRAYSIZE(sourceRelativePath), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetVolumeGuidKey.c_str(), L"", targetVolumeGuid, ARRAYSIZE(targetVolumeGuid), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetRelativePathKey.c_str(), L"", targetRelativePath, ARRAYSIZE(targetRelativePath), configPath.c_str());
        const int isBidir = GetPrivateProfileIntW(L"Tasks", bidirKey.c_str(), 0, configPath.c_str());
        const int deleteExtra = GetPrivateProfileIntW(L"Tasks", deleteExtraKey.c_str(), 0, configPath.c_str());
        const int autoMonitor = GetPrivateProfileIntW(L"Tasks", autoMonitorKey.c_str(), 0, configPath.c_str());
        if (source[0] != L'\0' && target[0] != L'\0')
        {
            SyncPair pair{ taskName, source, target, isBidir != 0, deleteExtra != 0, autoMonitor != 0 };
            pair.sourceVolumeGuid = sourceVolumeGuid;
            pair.sourceRelativePath = sourceRelativePath;
            pair.targetVolumeGuid = targetVolumeGuid;
            pair.targetRelativePath = targetRelativePath;
            gSyncPairs.push_back(pair);
        }
    }

    if (!gSyncPairs.empty())
    {
        PrepareSyncPairsForUse(hWnd, false);
    }

    RefreshPairList();
    AppendLog(hWnd, L"配置文件: " + configPath);
    if (!gSyncPairs.empty())
    {
        AppendLog(hWnd, L"已加载同步任务: " + std::to_wstring(gSyncPairs.size()) + L" 组");
    }

    if (autoMonitorAll && !gSyncPairs.empty())
    {
        ToggleMonitoring(hWnd);
    }
    else if (autoMonitorAll)
    {
        SendMessageW(hAutoMonitor, BM_SETCHECK, BST_UNCHECKED, 0);
        AppendLog(hWnd, L"[系统] 未找到同步任务，已取消默认开启实时同步。 ");
        SaveSettings();
    }
}

void SaveSettings()
{
    const std::wstring configPath = GetConfigPath();

    // 如果文件不存在，则创建一个带有 UTF-16 LE BOM 的空文件
    // 这样能确保 WritePrivateProfileStringW 总是以 Unicode (UTF-16) 编码写入，防止中文路径变乱码
    if (GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            const unsigned char bom[] = { 0xFF, 0xFE };
            DWORD bytesWritten = 0;
            WriteFile(hFile, bom, sizeof(bom), &bytesWritten, NULL);
            CloseHandle(hFile);
        }
    }

    WritePrivateProfileStringW(L"Settings", L"AutoMonitorAll",
        SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", nullptr, nullptr, configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", L"Count", std::to_wstring(gSyncPairs.size()).c_str(), configPath.c_str());

    for (size_t i = 0; i < gSyncPairs.size(); ++i)
    {
        const std::wstring taskNameKey = L"TaskName" + std::to_wstring(i);
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        const std::wstring bidirKey = L"Bidirectional" + std::to_wstring(i);
        const std::wstring deleteExtraKey = L"DeleteExtra" + std::to_wstring(i);
        const std::wstring autoMonitorKey = L"AutoMonitor" + std::to_wstring(i);
        const std::wstring sourceVolumeGuidKey = L"SourceVolumeGuid" + std::to_wstring(i);
        const std::wstring sourceRelativePathKey = L"SourceRelativePath" + std::to_wstring(i);
        const std::wstring targetVolumeGuidKey = L"TargetVolumeGuid" + std::to_wstring(i);
        const std::wstring targetRelativePathKey = L"TargetRelativePath" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Tasks", taskNameKey.c_str(), gSyncPairs[i].taskName.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", sourceKey.c_str(), gSyncPairs[i].source.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", targetKey.c_str(), gSyncPairs[i].target.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", bidirKey.c_str(), gSyncPairs[i].isBidirectional ? L"1" : L"0", configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", deleteExtraKey.c_str(), gSyncPairs[i].deleteExtraFiles ? L"1" : L"0", configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", autoMonitorKey.c_str(), gSyncPairs[i].autoMonitor ? L"1" : L"0", configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", sourceVolumeGuidKey.c_str(), gSyncPairs[i].sourceVolumeGuid.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", sourceRelativePathKey.c_str(), gSyncPairs[i].sourceRelativePath.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", targetVolumeGuidKey.c_str(), gSyncPairs[i].targetVolumeGuid.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", targetRelativePathKey.c_str(), gSyncPairs[i].targetRelativePath.c_str(), configPath.c_str());
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

bool IsAutoStartEnabled()
{
    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (lRes == ERROR_SUCCESS)
    {
        WCHAR szPath[MAX_PATH];
        DWORD dwSize = sizeof(szPath);
        lRes = RegQueryValueExW(hKey, L"FreeSync", nullptr, nullptr, (LPBYTE)szPath, &dwSize);
        RegCloseKey(hKey);
        if (lRes == ERROR_SUCCESS)
        {
            return true;
        }
    }
    return false;
}

void ToggleAutoStart(HWND hWnd)
{
    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_READ, &hKey);
    if (lRes == ERROR_SUCCESS)
    {
        if (IsAutoStartEnabled())
        {
            // 已开启，则关闭
            RegDeleteValueW(hKey, L"FreeSync");
            AppendLog(hWnd, L"[系统] 已取消开机自启动。");
        }
        else
        {
            // 未开启，则开启
            WCHAR szPath[MAX_PATH];
            GetModuleFileNameW(nullptr, szPath, MAX_PATH);
            std::wstring command = L"\"" + std::wstring(szPath) + L"\" /minimized";
            RegSetValueExW(hKey, L"FreeSync", 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)((command.length() + 1) * sizeof(WCHAR)));
            AppendLog(hWnd, L"[系统] 已设置开机自启动。");
        }
        RegCloseKey(hKey);
    }
}
