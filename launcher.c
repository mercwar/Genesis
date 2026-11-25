#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include "resource.h"

#ifndef ICC_PROGRESS_CLASSES
#define ICC_PROGRESS_CLASSES 0x00000020
#endif

#ifndef IDI_APP_ICON
#define IDI_APP_ICON 1
#endif


#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#define SERVICE_NAME     "Genesis"
#define DISPLAY_NAME     "Genesis"
#define SVCHOST_GROUP    "GenesisGroup"
#define DLL_FILENAME     "Genesis.dll"
#define WIZARD_TITLE     "Genesis System Service Setup (x64)"

#define IDC_LBL_HEADER   1001
#define IDC_LBL_STATUS   1002
#define IDC_LBL_DETAIL   1003
#define IDC_PROGRESS     1004
#define IDC_BTN_EXIT     1005

#define WM_APP_UPDATE_UI (WM_APP + 1)
#define WM_APP_DONE      (WM_APP + 2)

typedef struct {
    int   percent;
    char  status[128];
    char  detail[260];
    BOOL  isError;
} UiUpdatePacket;

static HWND   g_hWndMain   = NULL;
static HWND   g_hLblHeader = NULL;
static HWND   g_hLblStatus = NULL;
static HWND   g_hLblDetail = NULL;
static HWND   g_hProgress  = NULL;
static HWND   g_hBtnExit   = NULL;
static HFONT  g_hFontBold  = NULL;
static HFONT  g_hFontNorm  = NULL;
static volatile BOOL g_IsFinished = FALSE;

static void PostUiUpdate(int percent, const char *status, const char *detail, BOOL isError) {
    if (!g_hWndMain) return;
    UiUpdatePacket *pkt = (UiUpdatePacket *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(UiUpdatePacket));
    if (!pkt) return;

    pkt->percent = percent;
    pkt->isError = isError;
    if (status) snprintf(pkt->status, sizeof(pkt->status), "%s", status);
    if (detail) snprintf(pkt->detail, sizeof(pkt->detail), "%s", detail);

    if (!PostMessageA(g_hWndMain, WM_APP_UPDATE_UI, 0, (LPARAM)pkt)) {
        HeapFree(GetProcessHeap(), 0, pkt);
    }
}

static void PostErrorStatus(const char *step, DWORD errCode) {
    char sysMsg[160] = {0};
    char detail[260] = {0};

    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        sysMsg, sizeof(sysMsg) - 1, NULL
    );

    // Trim trailing CR/LF from system message
    size_t len = strlen(sysMsg);
    while (len > 0 && (sysMsg[len - 1] == '\r' || sysMsg[len - 1] == '\n')) {
        sysMsg[--len] = '\0';
    }

    snprintf(detail, sizeof(detail), "Error %lu: %s", errCode, sysMsg[0] ? sysMsg : "Unknown error.");
    PostUiUpdate(100, step, detail, TRUE);
}

static BOOL EnsureElevated(void) {
    if (IsUserAnAdmin()) return TRUE;

    char exePath[MAX_PATH] = {0};
    if (!GetModuleFileNameA(NULL, exePath, MAX_PATH)) return FALSE;

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exePath;
    sei.nShow  = SW_SHOWNORMAL;

    if (ShellExecuteExA(&sei)) {
        ExitProcess(0);
    }
    return FALSE;
}

static BOOL StopServiceIfRunning(SC_HANDLE hSCM, const char *svcName) {
    SC_HANDLE hSvc = OpenServiceA(hSCM, svcName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) return TRUE;

    SERVICE_STATUS_PROCESS ssp = {0};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState != SERVICE_STOPPED) {
            PostUiUpdate(8, "Stopping existing Genesis service...", "Releasing file lock on System32\\Genesis.dll", FALSE);
            SERVICE_STATUS dummy = {0};
            ControlService(hSvc, SERVICE_CONTROL_STOP, &dummy);

            for (int i = 0; i < 20; ++i) {
                Sleep(150);
                if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
                    if (ssp.dwCurrentState == SERVICE_STOPPED) break;
                }
            }
        }
    }

    CloseServiceHandle(hSvc);
    return TRUE;
}

static BOOL ExtractEmbeddedDllWithProgress(WORD resourceId, const char *outputPath) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!hRes) return FALSE;

    HGLOBAL hResData = LoadResource(NULL, hRes);
    if (!hResData) return FALSE;

    DWORD totalSize = SizeofResource(NULL, hRes);
    const BYTE *pData = (const BYTE *)LockResource(hResData);
    if (!pData || totalSize == 0) return FALSE;

    HANDLE hFile = CreateFileA(outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    const DWORD chunkSize = 4096;
    DWORD offset = 0;

    while (offset < totalSize) {
        DWORD toWrite = (totalSize - offset > chunkSize) ? chunkSize : (totalSize - offset);
        DWORD written = 0;

        if (!WriteFile(hFile, pData + offset, toWrite, &written, NULL) || written != toWrite) {
            CloseHandle(hFile);
            return FALSE;
        }

        offset += written;

        // Map file copy progress from 15% -> 65%
        int pct = 15 + (int)(((ULONGLONG)offset * 50ULL) / (ULONGLONG)totalSize);
        char detail[260] = {0};
        snprintf(detail, sizeof(detail), "Copying %s (%lu / %lu bytes)", outputPath, offset, totalSize);
        PostUiUpdate(pct, "Extracting signed Genesis.dll to System32...", detail, FALSE);
        Sleep(15);
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    return TRUE;
}

static BOOL RegisterSvchostGroup(const char *groupName, const char *svcName) {
    HKEY hKey = NULL;
    LSTATUS res = RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Svchost",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL
    );
    if (res != ERROR_SUCCESS) return FALSE;

    char multiSz[128] = {0};
    size_t len = strlen(svcName);
    if (len + 2 > sizeof(multiSz)) {
        RegCloseKey(hKey);
        return FALSE;
    }
    memcpy(multiSz, svcName, len);
    multiSz[len] = '\0';
    multiSz[len + 1] = '\0';

    res = RegSetValueExA(hKey, groupName, 0, REG_MULTI_SZ, (const BYTE *)multiSz, (DWORD)(len + 2));
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

static DWORD WINAPI InstallWorkerThread(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);

    char sysDir[MAX_PATH]    = {0};
    char targetDll[MAX_PATH] = {0};

    PostUiUpdate(5, "Detecting Windows System32 directory...", "Querying GetSystemDirectoryA", FALSE);
    UINT sysLen = GetSystemDirectoryA(sysDir, sizeof(sysDir));
    if (sysLen == 0 || sysLen >= sizeof(sysDir)) {
        PostErrorStatus("Failed to detect System32 directory.", GetLastError());
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    snprintf(targetDll, sizeof(targetDll), "%s\\%s", sysDir, DLL_FILENAME);

    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        PostErrorStatus("Failed to open Service Control Manager.", GetLastError());
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    // Stop active service first so System32\Genesis.dll can be overwritten cleanly
    StopServiceIfRunning(hSCM, SERVICE_NAME);

    PostUiUpdate(15, "Extracting signed Genesis.dll to System32...", targetDll, FALSE);
    if (!ExtractEmbeddedDllWithProgress(IDR_GENESIS_DLL, targetDll)) {
        PostErrorStatus("Failed to copy Genesis.dll to System32.", GetLastError());
        CloseServiceHandle(hSCM);
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    PostUiUpdate(72, "Registering Svchost service group...", "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Svchost", FALSE);
    if (!RegisterSvchostGroup(SVCHOST_GROUP, SERVICE_NAME)) {
        PostErrorStatus("Failed to register Svchost group in registry.", GetLastError());
        CloseServiceHandle(hSCM);
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    PostUiUpdate(82, "Configuring Genesis background service...", "Creating/updating service entry in SCM", FALSE);
    char binPath[MAX_PATH] = {0};
    snprintf(binPath, sizeof(binPath), "%%SystemRoot%%\\System32\\svchost.exe -k %s", SVCHOST_GROUP);

    SC_HANDLE hSvc = CreateServiceA(
        hSCM,
        SERVICE_NAME,
        DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_SHARE_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath,
        NULL, NULL, NULL, NULL, NULL
    );

    if (!hSvc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            hSvc = OpenServiceA(hSCM, SERVICE_NAME, SERVICE_ALL_ACCESS);
            if (!hSvc) {
                PostErrorStatus("Failed to open existing Genesis service.", GetLastError());
                CloseServiceHandle(hSCM);
                PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
                return 1;
            }
            ChangeServiceConfigA(hSvc, SERVICE_WIN32_SHARE_PROCESS, SERVICE_AUTO_START, SERVICE_NO_CHANGE,
                                 binPath, NULL, NULL, NULL, NULL, NULL, DISPLAY_NAME);
        } else {
            PostErrorStatus("Failed to create Genesis service.", err);
            CloseServiceHandle(hSCM);
            PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
            return 1;
        }
    }

    PostUiUpdate(90, "Writing ServiceDll registry path...", targetDll, FALSE);
    char regPath[256] = {0};
    snprintf(regPath, sizeof(regPath), "SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters", SERVICE_NAME);

    HKEY hKey = NULL;
    LSTATUS regRes = RegCreateKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (regRes != ERROR_SUCCESS) {
        PostErrorStatus("Failed to open/create service Parameters key.", (DWORD)regRes);
        CloseServiceHandle(hSvc);
        CloseServiceHandle(hSCM);
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    regRes = RegSetValueExA(hKey, "ServiceDll", 0, REG_EXPAND_SZ, (const BYTE *)targetDll, (DWORD)(strlen(targetDll) + 1));
    RegCloseKey(hKey);

    if (regRes != ERROR_SUCCESS) {
        PostErrorStatus("Failed to set ServiceDll registry value.", (DWORD)regRes);
        CloseServiceHandle(hSvc);
        CloseServiceHandle(hSCM);
        PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
        return 1;
    }

    PostUiUpdate(96, "Starting Genesis background service...", "Calling StartServiceA", FALSE);
    char finalDetail[260] = {0};
    if (!StartServiceA(hSvc, 0, NULL)) {
        DWORD startErr = GetLastError();
        if (startErr == ERROR_SERVICE_ALREADY_RUNNING) {
            snprintf(finalDetail, sizeof(finalDetail), "Installed to %s (Service already running)", targetDll);
        } else {
            PostErrorStatus("Service registered, but StartService failed.", startErr);
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
            PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
            return 1;
        }
    } else {
        snprintf(finalDetail, sizeof(finalDetail), "Installed and running: %s", targetDll);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    PostUiUpdate(100, "Installation Complete!", finalDetail, FALSE);
    PostMessageA(g_hWndMain, WM_APP_DONE, 0, 0);
    return 0;
}

static LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hFontBold = CreateFontA(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            g_hFontNorm = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            g_hLblHeader = CreateWindowExA(0, "STATIC", "Installing Genesis x64 System Service",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           20, 16, 440, 22, hwnd, (HMENU)IDC_LBL_HEADER, NULL, NULL);

            g_hLblStatus = CreateWindowExA(0, "STATIC", "Initializing setup...",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                           20, 46, 440, 20, hwnd, (HMENU)IDC_LBL_STATUS, NULL, NULL);

            g_hProgress  = CreateWindowExA(0, PROGRESS_CLASSA, NULL,
                                           WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                           20, 72, 440, 22, hwnd, (HMENU)IDC_PROGRESS, NULL, NULL);

            g_hLblDetail = CreateWindowExA(0, "STATIC", "Preparing system files...",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS | SS_NOPREFIX,
                                           20, 102, 440, 20, hwnd, (HMENU)IDC_LBL_DETAIL, NULL, NULL);

            g_hBtnExit   = CreateWindowExA(0, "BUTTON", "Exit",
                                           WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_DEFPUSHBUTTON,
                                           365, 134, 95, 28, hwnd, (HMENU)IDC_BTN_EXIT, NULL, NULL);

            SendMessageA(g_hLblHeader, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
            SendMessageA(g_hLblStatus, WM_SETFONT, (WPARAM)g_hFontNorm, TRUE);
            SendMessageA(g_hLblDetail, WM_SETFONT, (WPARAM)g_hFontNorm, TRUE);
            SendMessageA(g_hBtnExit,   WM_SETFONT, (WPARAM)g_hFontNorm, TRUE);

            SendMessageA(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageA(g_hProgress, PBM_SETPOS, 0, 0);

            HANDLE hThread = CreateThread(NULL, 0, InstallWorkerThread, NULL, 0, NULL);
            if (hThread) CloseHandle(hThread);
            return 0;
        }

        case WM_APP_UPDATE_UI: {
            UiUpdatePacket *pkt = (UiUpdatePacket *)lParam;
            if (pkt) {
                SendMessageA(g_hProgress, PBM_SETPOS, (WPARAM)pkt->percent, 0);
                if (pkt->isError) {
                    SendMessageA(g_hProgress, PBM_SETSTATE, PBST_ERROR, 0);
                }
                SetWindowTextA(g_hLblStatus, pkt->status);
                SetWindowTextA(g_hLblDetail, pkt->detail);
                HeapFree(GetProcessHeap(), 0, pkt);
            }
            return 0;
        }

        case WM_APP_DONE: {
            g_IsFinished = TRUE;
            EnableWindow(g_hBtnExit, TRUE);
            SetFocus(g_hBtnExit);
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_BTN_EXIT && g_IsFinished) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g_IsFinished) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hFontNorm) DeleteObject(g_hFontNorm);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (!EnsureElevated()) {
        return 1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SetupWndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    wc.hIconSm       = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "GenesisSetupProgressWnd";

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    RECT rc = { 0, 0, 480, 178 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rc, style, FALSE);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hWndMain = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        WIZARD_TITLE,
        style,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWndMain) {
        return 1;
    }

    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

    MSG msg = {0};
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(g_hWndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return 0;
}
