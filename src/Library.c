#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "config.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

// --- Configuration & Globals ---
static volatile LONG g_ManualOverride = 0; // 0=None, 1=ForceOn, 2=ForceOff
static UINT g_TaskbarCreatedMsg = 0;
static NOTIFYICONDATAW g_TrayIconData = {0};
static HWND g_TrayHwnd = NULL;

// --- Forward Declarations ---
static DWORD WINAPI MainThreadProc(LPVOID lpParameter);
static DWORD WINAPI RegistryMonitorThreadProc(LPVOID lpParameter);
static LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static VOID CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
static void UpdateTrayTooltip(void);
static DWORD GetSteamWebHelperRamUsage(void);
static BOOL IsSiSRRunning(void);
static void KillSteamWebHelperProcesses(void);

// --- Helper Functions ---

static DWORD GetSteamWebHelperRamUsage(void) {
    DWORD totalRam = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (wcsstr(pe.szExeFile, L"steamwebhelper")) {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        totalRam += (DWORD)(pmc.WorkingSetSize / 1024);
                    }
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return totalRam;
}

static BOOL IsSiSRRunning(void) {
    if (!g_Config.autoDetectSiSR) return FALSE;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return FALSE;

    PROCESSENTRY32W pe = { sizeof(pe) };
    BOOL found = FALSE;
    const wchar_t* targets[] = { L"SiSR.exe", L"SiSR.Core.exe" };

    if (Process32FirstW(hSnap, &pe)) {
        do {
            for (int i = 0; i < 2; i++) {
                if (_wcsicmp(pe.szExeFile, targets[i]) == 0) {
                    found = TRUE;
                    break;
                }
            }
        } while (!found && Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

static void UpdateTrayTooltip(void) {
    if (!g_TrayHwnd) return;

    const wchar_t* modeStr = L"Aggressive";
    if (g_CurrentMode == MODE_SELECTIVE) modeStr = L"Selective";
    else if (g_CurrentMode == MODE_DISABLED) modeStr = L"Disabled";

    DWORD ram = 0;
    if (g_Config.showRamUsage && g_CurrentMode != MODE_DISABLED) {
        ram = GetSteamWebHelperRamUsage();
    }

    wchar_t szTip[128];
    if (ram > 0) {
        // Fixed: swprintf requires <stdio.h>
        swprintf(szTip, 128, L"NoSteamWebHelper (%s)\nRAM: %lu KB", modeStr, ram);
    } else {
        swprintf(szTip, 128, L"NoSteamWebHelper (%s)", modeStr);
    }

    wcsncpy_s(g_TrayIconData.szTip, 128, szTip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_TrayIconData);
}

// --- Original Logic Adapted ---

#define STEAM_REGISTRY_KEY L"SOFTWARE\\Valve\\Steam"
#define RUNNING_APP_ID_VALUE L"RunningAppID"
#define STEAM_WEB_HELPER_EXE L"steamwebhelper.exe"
#define VGUI_POPUP_WINDOW_CLASS L"vguiPopupWindow"

#define MAX_WEB_HELPER_PROCESSES 64
#define MENU_ITEM_TOGGLE 1
#define MENU_ITEM_EXIT 2

typedef struct {
    DWORD dwProcessId;
    DWORD dwParentProcessId;
    BOOL bDescendant;
} WEB_HELPER_ENTRY;

static void KillSteamWebHelperProcesses(void) {
    if (g_CurrentMode == MODE_DISABLED) return;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    WEB_HELPER_ENTRY entries[MAX_WEB_HELPER_PROCESSES];
    DWORD entryCount = 0;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (entryCount == MAX_WEB_HELPER_PROCESSES) break;
            if (CompareStringOrdinal(pe32.szExeFile, -1, STEAM_WEB_HELPER_EXE, -1, TRUE) == CSTR_EQUAL) {
                entries[entryCount].dwProcessId = pe32.th32ProcessID;
                entries[entryCount].dwParentProcessId = pe32.th32ParentProcessID;
                entries[entryCount].bDescendant = FALSE;
                entryCount++;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);

    DWORD dwCurrentProcessId = GetCurrentProcessId();
    BOOL bMarked = TRUE;
    while (bMarked) {
        bMarked = FALSE;
        for (DWORD i = 0; i < entryCount; i++) {
            if (entries[i].bDescendant) continue;
            if (entries[i].dwParentProcessId == dwCurrentProcessId) {
                entries[i].bDescendant = TRUE;
                bMarked = TRUE;
                continue;
            }
            for (DWORD j = 0; j < entryCount; j++) {
                if (entries[j].bDescendant && entries[j].dwProcessId == entries[i].dwParentProcessId) {
                    entries[i].bDescendant = TRUE;
                    bMarked = TRUE;
                    break;
                }
            }
        }
    }

    for (DWORD i = 0; i < entryCount; i++) {
        if (!entries[i].bDescendant) continue;

        // Selective Mode Check
        if (g_CurrentMode == MODE_SELECTIVE) {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entries[i].dwProcessId);
            if (hProc) {
                wchar_t cmdLine[MAX_PATH] = {0};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, cmdLine, &size) > 0) {
                    // Keep broker/gpu in selective mode
                    if (wcsstr(cmdLine, L"--type=broker") || wcsstr(cmdLine, L"--type=gpu-process")) {
                        CloseHandle(hProc);
                        continue;
                    }
                }
                CloseHandle(hProc);
            }
        }

        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entries[i].dwProcessId);
        if (hProcess) {
            TerminateProcess(hProcess, EXIT_SUCCESS);
            CloseHandle(hProcess);
        }
    }
}

static void ApplySuppression(HANDLE hThread, BOOL *pbSuspended, BOOL bSuppress) {
    if (bSuppress) {
        if (!*pbSuspended && SuspendThread(hThread) != (DWORD)-1)
            *pbSuspended = TRUE;
        KillSteamWebHelperProcesses();
    } else if (*pbSuspended) {
        if (ResumeThread(hThread) != (DWORD)-1)
            *pbSuspended = FALSE;
    }
}

static BOOL IsAppRunning(HKEY hKey) {
    BOOL isAppRunning = FALSE;
    DWORD dataSize = sizeof(BOOL);
    if (RegGetValueW(hKey, NULL, RUNNING_APP_ID_VALUE, RRF_RT_REG_DWORD, NULL, &isAppRunning, &dataSize) != ERROR_SUCCESS)
        return FALSE;
    return isAppRunning;
}

static DWORD WINAPI RegistryMonitorThreadProc(LPVOID lpParameter) {
    DWORD dwEventThread = (DWORD)(ULONG_PTR)lpParameter;
    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | SYNCHRONIZE, FALSE, dwEventThread);
    if (!hThread) return EXIT_FAILURE;

    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STEAM_REGISTRY_KEY, 0, KEY_NOTIFY | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        CloseHandle(hThread);
        return EXIT_FAILURE;
    }

    HANDLE hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!hEvent) {
        RegCloseKey(hKey);
        CloseHandle(hThread);
        return EXIT_FAILURE;
    }

    HANDLE waitHandles[2];
    waitHandles[0] = hEvent;
    waitHandles[1] = hThread;

    BOOL isSuspended = FALSE;
    BOOL wasAppRunning = FALSE;
    BOOL isNotifyArmed = FALSE;

    while (TRUE) {
        if (!isNotifyArmed) {
            if (RegNotifyChangeKeyValue(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvent, TRUE) != ERROR_SUCCESS)
                break;
            isNotifyArmed = TRUE;
        }

        BOOL isAppRunning = IsAppRunning(hKey);
        LONG manualOverride = g_ManualOverride;

        if (manualOverride != 0 && isAppRunning != wasAppRunning) {
            InterlockedCompareExchange(&g_ManualOverride, 0, manualOverride);
            manualOverride = 0;
        }
        wasAppRunning = isAppRunning;

        // Logic: If Manual Off -> Suppress. If Manual On -> Don't Suppress.
        // Else: If App Running -> Suppress.
        BOOL bSuppress = (manualOverride == 2) ? TRUE : ((manualOverride == 1) ? FALSE : isAppRunning);

        ApplySuppression(hThread, &isSuspended, bSuppress);

        // Periodically update tray even if no registry change
        UpdateTrayTooltip();

        DWORD dwWait = WaitForMultipleObjects(2, waitHandles, FALSE, 1000); // 1s timeout to update tray
        if (dwWait == WAIT_OBJECT_0) {
            ResetEvent(hEvent);
            isNotifyArmed = FALSE;
        } else if (dwWait == WAIT_OBJECT_0 + 1) {
            break;
        }
    }

    ApplySuppression(hThread, &isSuspended, FALSE);
    CloseHandle(hEvent);
    RegCloseKey(hKey);
    CloseHandle(hThread);
    return EXIT_SUCCESS;
}

static VOID CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    UNREFERENCED_PARAMETER(hWinEventHook);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(idObject);
    UNREFERENCED_PARAMETER(idChild);
    UNREFERENCED_PARAMETER(dwmsEventTime);

    WCHAR szClassName[32] = {0};
    if (!GetClassNameW(hwnd, szClassName, sizeof(szClassName) / sizeof(WCHAR)))
        return;

    if (CompareStringOrdinal(VGUI_POPUP_WINDOW_CLASS, -1, szClassName, -1, FALSE) != CSTR_EQUAL || GetWindowTextLengthW(hwnd) < 1)
        return;

    // Simple flag to prevent multiple threads (simplified from original for brevity)
    static volatile LONG s_ThreadStarted = 0;
    if (InterlockedCompareExchange(&s_ThreadStarted, 1, 0) == 0) {
        HANDLE hThread = CreateThread(NULL, 0, RegistryMonitorThreadProc, (LPVOID)(ULONG_PTR)dwEventThread, 0, NULL);
        if (hThread) CloseHandle(hThread);
        else InterlockedExchange(&s_ThreadStarted, 0);
    }
}

static void ShowContextMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    wchar_t toggleText[32];
    if (g_CurrentMode == MODE_DISABLED) wcscpy_s(toggleText, 32, L"Enable Suppression");
    else wcscpy_s(toggleText, 32, L"Disable Suppression");

    AppendMenuW(hMenu, MF_STRING, MENU_ITEM_TOGGLE, toggleText);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, MENU_ITEM_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    POINT pt = {0};
    GetCursorPos(&pt);
    int nSelection = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    if (nSelection == MENU_ITEM_TOGGLE) {
        // Cycle modes: Aggressive -> Selective -> Disabled -> Aggressive
        g_CurrentMode = (g_CurrentMode + 1) % 3;
        g_Config.mode = g_CurrentMode;
        UpdateTrayTooltip();
    } else if (nSelection == MENU_ITEM_EXIT) {
        Shell_NotifyIconW(NIM_DELETE, &g_TrayIconData);
        ExitProcess(0);
    }
}

static LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        g_TrayHwnd = hWnd;
        g_TaskbarCreatedMsg = RegisterWindowMessageA("TaskbarCreated"); // Use A for safety
        g_TrayIconData.cbSize = sizeof(NOTIFYICONDATAW);
        g_TrayIconData.hWnd = hWnd;
        g_TrayIconData.uID = 1;
        g_TrayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_TrayIconData.uCallbackMessage = WM_USER;
        // Fixed: Cast IDI_APPLICATION to LPCWSTR
        g_TrayIconData.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
        lstrcpynW(g_TrayIconData.szTip, L"NoSteamWebHelper", 128);
        Shell_NotifyIconW(NIM_ADD, &g_TrayIconData);
        UpdateTrayTooltip();
        break;

    case WM_USER:
        if (lParam == WM_RBUTTONDOWN) {
            ShowContextMenu(hWnd);
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_TrayIconData);
        PostQuitMessage(0);
        break;

    default:
        if (uMsg == g_TaskbarCreatedMsg && g_TaskbarCreatedMsg != 0) {
            Shell_NotifyIconW(NIM_ADD, &g_TrayIconData);
            UpdateTrayTooltip();
        }
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static DWORD WINAPI MainThreadProc(LPVOID lpParameter) {
    UNREFERENCED_PARAMETER(lpParameter);

    HWINEVENTHOOK hEventHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, WinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    if (!hEventHook) return EXIT_FAILURE;

    WNDCLASSW wc = {0};
    wc.lpszClassName = L"NoSteamWebHelperTray";
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpfnWndProc = TrayWindowProc;
    RegisterClassW(&wc);

    CreateWindowExW(WS_EX_LEFT | WS_EX_LTRREADING, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);

    MSG msg = {0};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hEventHook) UnhookWinEvent(hEventHook);
    return EXIT_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hLibModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hLibModule);

        wchar_t path[MAX_PATH];
        GetModuleFileNameW(hLibModule, path, MAX_PATH);
        PathRemoveFileSpecW(path);
        wcscat_s(path, MAX_PATH, L"\\NoSteamWebHelper.ini");
        LoadConfig(path);

        HANDLE hThread = CreateThread(NULL, 0, MainThreadProc, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    else if (dwReason == DLL_PROCESS_DETACH && !lpReserved) {
        if (g_TrayIconData.hWnd)
            Shell_NotifyIconW(NIM_DELETE, &g_TrayIconData);
    }

    return TRUE;
}
