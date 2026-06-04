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
#define IDC_SYNC_LIST 1007
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
void                StartSinglePairSync(HWND hWnd, int index);
std::wstring        GetConfigPath();
bool                PrepareSyncPairsForUse(HWND hWnd, bool showMessageOnFailure);
bool                CheckAndRecoverSinglePair(HWND hWnd, int index, bool showMessageOnFailure);
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
    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_SYNC_LIST && pnmh->code == NM_DBLCLK)
        {
            // 如果实时同步已开启，则禁止手动双击同步
            if (SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED)
            {
                MessageBoxW(hWnd, L"实时同步状态下此功能无效", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            if (pnmv->iItem != -1)
            {
                StartSinglePairSync(hWnd, pnmv->iItem);
            }
        }
        break;
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
        EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), TRUE);
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

    hPairList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        16, 46, 846, 180, hWnd, (HMENU)IDC_SYNC_LIST, hInst, nullptr);
    SendMessageW(hPairList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.pszText = (LPWSTR)L"同步任务";
    lvc.cx = 650;
    ListView_InsertColumn(hPairList, 0, &lvc);

    lvc.pszText = (LPWSTR)L"操作";
    lvc.cx = 150;
    ListView_InsertColumn(hPairList, 1, &lvc);

    hDeleteExtra = CreateWindowW(L"BUTTON", L"同步删除文件", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 236, 120, 24, hWnd, (HMENU)IDC_DELETE_EXTRA, hInst, nullptr);
    hAutoMonitor = CreateWindowW(L"BUTTON", L"实时同步", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        156, 236, 90, 24, hWnd, (HMENU)IDC_AUTO_MONITOR, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"手动同步", WS_CHILD | WS_VISIBLE,
        266, 233, 110, 30, hWnd, (HMENU)IDC_START_SYNC, hInst, nullptr);

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
            const std::wstring source = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_SOURCE_EDIT));
            const std::wstring target = GetWindowTextString(GetDlgItem(hDlg, IDC_DLG_TARGET_EDIT));
            const bool isBidirectional = IsDlgButtonChecked(hDlg, IDC_DLG_BIDIRECTIONAL) == BST_CHECKED;

            if (source.empty() || target.empty())
            {
                MessageBoxW(hDlg, L"请先选择源文件夹和目标文件夹。", L"提示", MB_OK | MB_ICONINFORMATION);
                return (INT_PTR)TRUE;
            }

            SyncPair pair{ source, target, isBidirectional };
            CaptureSyncPairVolumeInfo(pair);
            gSyncPairs.push_back(pair);
            std::wstring display = source + L"  ->  " + target;
            if (isBidirectional)
            {
                display = source + L"  <->  " + target;
            }
            SendMessageW(hPairList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
            RefreshPairList();
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

    // 检查并尝试恢复路径，即使有失败也继续尝试同步其他正常的任务
    PrepareSyncPairsForUse(hWnd, true);

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

void StartSinglePairSync(HWND hWnd, int index)
{
    if (index < 0 || index >= (int)gSyncPairs.size()) return;

    if (!CheckAndRecoverSinglePair(hWnd, index, true))
    {
        return;
    }

    EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), FALSE);
    SendMessageW(hProgressBar, PBM_SETPOS, 0, 0);
    AppendLog(hWnd, L"[系统] 开始同步单项任务...");

    SyncPair pair = gSyncPairs[index];

    std::thread([hWnd, pair]() {
        SyncOptions options;
        options.deleteExtraFiles = SendMessageW(hDeleteExtra, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SaveSettings();

        SyncFolderPair(pair, options, [hWnd](const std::wstring& message)
            {
                std::wstring* msg = new std::wstring(message);
                PostMessageW(hWnd, WM_APPEND_LOG, (WPARAM)msg, 0);
            }, [hWnd](float progress)
            {
                PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
            });

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

        // 尝试恢复路径，即使有失败也允许开启监控（监控线程内部会重试）
        PrepareSyncPairsForUse(hWnd, true);

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
        }, [hWnd](float progress)
        {
            PostMessageW(hWnd, WM_UPDATE_PROGRESS, (WPARAM)(int)(progress * 100), 0);
        });

        AppendLog(hWnd, L"[系统] 实时同步已开启...");
    }
    else
    {
        StopMonitoring();

        SaveSettings();

        EnableWindow(GetDlgItem(hWnd, IDC_ADD_PAIR), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_REMOVE_PAIR), TRUE);
        EnableWindow(GetDlgItem(hWnd, IDC_START_SYNC), TRUE);
        EnableWindow(hDeleteExtra, TRUE);

        AppendLog(hWnd, L"[系统] 实时同步已关闭。");
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
    ListView_DeleteAllItems(hPairList);
    for (int i = 0; i < (int)gSyncPairs.size(); ++i)
    {
        const auto& pair = gSyncPairs[i];
        std::wstring display = pair.source + L"  ->  " + pair.target;
        if (pair.isBidirectional)
        {
            display = pair.source + L"  <->  " + pair.target;
        }

        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)display.c_str();
        lvi.lParam = (LPARAM)i;
        ListView_InsertItem(hPairList, &lvi);
        ListView_SetItemText(hPairList, i, 1, (LPWSTR)L"[双击同步此项]");
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
    const DWORD deleteExtra = GetPrivateProfileIntW(L"Settings", L"DeleteExtraFiles", 0, configPath.c_str());
    const DWORD autoMonitor = GetPrivateProfileIntW(L"Settings", L"AutoMonitor", 0, configPath.c_str());
    SendMessageW(hDeleteExtra, BM_SETCHECK, deleteExtra ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hAutoMonitor, BM_SETCHECK, autoMonitor ? BST_CHECKED : BST_UNCHECKED, 0);

    gSyncPairs.clear();
    const int count = (int)GetPrivateProfileIntW(L"Tasks", L"Count", 0, configPath.c_str());
    for (int i = 0; i < count; ++i)
    {
        WCHAR source[MAX_PATH * 4]{};
        WCHAR target[MAX_PATH * 4]{};
        WCHAR sourceVolumeGuid[MAX_PATH * 4]{};
        WCHAR sourceRelativePath[MAX_PATH * 4]{};
        WCHAR targetVolumeGuid[MAX_PATH * 4]{};
        WCHAR targetRelativePath[MAX_PATH * 4]{};
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        const std::wstring bidirKey = L"Bidirectional" + std::to_wstring(i);
        const std::wstring sourceVolumeGuidKey = L"SourceVolumeGuid" + std::to_wstring(i);
        const std::wstring sourceRelativePathKey = L"SourceRelativePath" + std::to_wstring(i);
        const std::wstring targetVolumeGuidKey = L"TargetVolumeGuid" + std::to_wstring(i);
        const std::wstring targetRelativePathKey = L"TargetRelativePath" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Tasks", sourceKey.c_str(), L"", source, ARRAYSIZE(source), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetKey.c_str(), L"", target, ARRAYSIZE(target), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", sourceVolumeGuidKey.c_str(), L"", sourceVolumeGuid, ARRAYSIZE(sourceVolumeGuid), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", sourceRelativePathKey.c_str(), L"", sourceRelativePath, ARRAYSIZE(sourceRelativePath), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetVolumeGuidKey.c_str(), L"", targetVolumeGuid, ARRAYSIZE(targetVolumeGuid), configPath.c_str());
        GetPrivateProfileStringW(L"Tasks", targetRelativePathKey.c_str(), L"", targetRelativePath, ARRAYSIZE(targetRelativePath), configPath.c_str());
        const int isBidir = GetPrivateProfileIntW(L"Tasks", bidirKey.c_str(), 0, configPath.c_str());
        if (source[0] != L'\0' && target[0] != L'\0')
        {
            SyncPair pair{ source, target, isBidir != 0 };
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

    if (autoMonitor && !gSyncPairs.empty())
    {
        ToggleMonitoring(hWnd);
    }
    else if (autoMonitor)
    {
        SendMessageW(hAutoMonitor, BM_SETCHECK, BST_UNCHECKED, 0);
        AppendLog(hWnd, L"[系统] 未找到同步任务，已取消默认开启实时同步。 ");
        SaveSettings();
    }
}

void SaveSettings()
{
    const std::wstring configPath = GetConfigPath();
    WritePrivateProfileStringW(L"Settings", L"DeleteExtraFiles",
        SendMessageW(hDeleteExtra, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", configPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"AutoMonitor",
        SendMessageW(hAutoMonitor, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0", configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", nullptr, nullptr, configPath.c_str());
    WritePrivateProfileStringW(L"Tasks", L"Count", std::to_wstring(gSyncPairs.size()).c_str(), configPath.c_str());

    for (size_t i = 0; i < gSyncPairs.size(); ++i)
    {
        const std::wstring sourceKey = L"Source" + std::to_wstring(i);
        const std::wstring targetKey = L"Target" + std::to_wstring(i);
        const std::wstring bidirKey = L"Bidirectional" + std::to_wstring(i);
        const std::wstring sourceVolumeGuidKey = L"SourceVolumeGuid" + std::to_wstring(i);
        const std::wstring sourceRelativePathKey = L"SourceRelativePath" + std::to_wstring(i);
        const std::wstring targetVolumeGuidKey = L"TargetVolumeGuid" + std::to_wstring(i);
        const std::wstring targetRelativePathKey = L"TargetRelativePath" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Tasks", sourceKey.c_str(), gSyncPairs[i].source.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", targetKey.c_str(), gSyncPairs[i].target.c_str(), configPath.c_str());
        WritePrivateProfileStringW(L"Tasks", bidirKey.c_str(), gSyncPairs[i].isBidirectional ? L"1" : L"0", configPath.c_str());
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
