#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

static LONG                  g_Shown = 0;
static SERVICE_STATUS        g_Status = {0};
static SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
static HANDLE                g_StopEvent = NULL;

static DWORD WINAPI ShowOnceThread(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);

    if (InterlockedCompareExchange(&g_Shown, 1, 0) == 0) {
        DWORD sessionId = WTSGetActiveConsoleSessionId();
        if (sessionId != 0xFFFFFFFF) {
            WCHAR title[] = L"Genesis Sigtned with Mercwar AI";
            WCHAR msg[]   = L"I am CVBGOD, and I have given it to you!";
            DWORD resp    = 0;

            WTSSendMessageW(
                WTS_CURRENT_SERVER_HANDLE,
                sessionId,
                title,
                (DWORD)(sizeof(title) - sizeof(WCHAR)),
                msg,
                (DWORD)(sizeof(msg) - sizeof(WCHAR)),
                MB_OK | MB_ICONINFORMATION,
                0,
                &resp,
                FALSE
            );
        }
    }
    return 0;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP && g_Status.dwCurrentState == SERVICE_RUNNING) {
        g_Status.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_Status);
        if (g_StopEvent) SetEvent(g_StopEvent);
    }
}

__declspec(dllexport) VOID WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    g_StatusHandle = RegisterServiceCtrlHandlerW(L"Genesis", ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_Status.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_Status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_Status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_Status);

    HANDLE hNotifyThread = CreateThread(NULL, 0, ShowOnceThread, NULL, 0, NULL);
    if (hNotifyThread) CloseHandle(hNotifyThread);

    g_StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_StopEvent) {
        WaitForSingleObject(g_StopEvent, INFINITE);
        CloseHandle(g_StopEvent);
        g_StopEvent = NULL;
    }

    g_Status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_Status);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    UNREFERENCED_PARAMETER(lpvReserved);
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
