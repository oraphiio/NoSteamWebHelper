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

// Forward declarations
static LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI MonitorThread(LPVOID lpParam);
static void KillCefProcesses();
static DWORD GetSteamWebHelperRamUsage();
static BOOL IsSiSRRunning();

// Global State (No static conflicts with config.h)
static HWND g_hTrayWnd = NULL;
static HICON g_hIconGreen = NULL;
static HICON g_hIconRed = NULL;
static HICON g_hIconYellow = NULL;
static BOOL g_bGameRunning = FALSE;
static BOOL g_bSiSRDetected = FALSE;
static UINT WM_TASKBARCREATED = 0;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Load Config
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(hModule, path, MAX_PATH);
        PathRemoveFileSpecW(path);
        wcscat_s(path, MAX_PATH, L"\\NoSteamWebHelper.ini");
        LoadConfig(path);

        WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

        // Create Hidden Window for Tray
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = TrayWindowProc;
        wc.hInstance = hModule;
        wc.lpszClassName = L"NoSteamWebHelperClass";
        RegisterClassExW(&wc);

        g_hTrayWnd = CreateWindowExW(0, wc.lpszClassName, L"Tray", 0, 0, 0, 0, 0, NULL, NULL, hModule, NULL);

        // Load Icons
        g_hIconGreen = LoadIcon(NULL, IDI_APPLICATION); // Replace with custom later
        g_hIconRed = LoadIcon(NULL, IDI_HAND);
        g_hIconYellow = LoadIcon(NULL, IDI_QUESTION);

        // Start Monitor Thread
        CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    }
    return TRUE;
}

static DWORD GetSteamWebHelperRamUsage() {
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

static BOOL IsSiSRRunning() {
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

static void KillCefProcesses() {
    if (g_CurrentMode == MODE_DISABLED) return;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"steamwebhelper.exe") == 0) {
                // Get Command Line to check selective rules
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t cmdLine[MAX_PATH] = {0};
                    DWORD size = MAX_PATH;
                    QueryFullProcessImageNameW(hProc, 0, cmdLine, &size);

                    // Simple check: if selective mode and it's a broker, skip
                    // In a real impl, you'd parse args better, but path often contains hints
                    if (!ShouldKillProcess(cmdLine, g_CurrentMode)) {
                        CloseHandle(hProc);
                        continue;
                    }

                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

static DWORD WINAPI MonitorThread(LPVOID lpParam) {
    while (TRUE) {
        // Detect SiSR
        g_bSiSRDetected = IsSiSRRunning();

        // Auto-switch to Selective if SiSR is detected and user wants it
        if (g_bSiSRDetected && g_Config.autoDetectSiSR && g_CurrentMode == MODE_AGGRESSIVE) {
             // Optional: Auto-switch logic could go here, for now we just note it
        }

        // Check for Game Running (Simplified: Check if any game is running via Steam ID or generic check)
        // For this demo, we assume if SiSR is running, we are in "Gaming Mode"
        BOOL shouldSuppress = g_bSiSRDetected || g_bGameRunning;

        if (shouldSuppress) {
            KillCefProcesses();
        }

        Sleep(2000); // Check every 2 seconds
    }
    return 0;
}

static void UpdateTrayIcon() {
    if (!g_hTrayWnd) return;

    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = g_hTrayWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;

    // Select Icon
    if (g_CurrentMode == MODE_DISABLED) nid.hIcon = g_hIconGreen;
    else if (g_bSiSRDetected) nid.hIcon = g_hIconYellow;
    else nid.hIcon = g_hIconRed;

    // Build Tooltip
    wchar_t szTooltip[256];
    const wchar_t* modeText = (g_CurrentMode == MODE_DISABLED) ? L"Disabled" :
                              (g_CurrentMode == MODE_SELECTIVE) ? L"Selective" : L"Aggressive";

    DWORD ramUsage = 0;
    if (g_Config.showRamUsage && g_CurrentMode != MODE_DISABLED) {
        ramUsage = GetSteamWebHelperRamUsage();
    }

    if (ramUsage > 0) {
        swprintf(szTooltip, 256, L"NoSteamWebHelper - %s Mode\nCEF RAM: %lu KB", modeText, ramUsage);
    } else {
        swprintf(szTooltip, 256, L"NoSteamWebHelper - %s Mode", modeText);
    }

    wcsncpy_s(nid.szTip, 256, szTooltip, _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TASKBARCREATED) {
        UpdateTrayIcon();
        return 0;
    }
    if (uMsg == WM_USER + 1) {
        if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1001, L"Toggle Mode");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1002, L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    if (uMsg == WM_COMMAND) {
        if (wParam == 1001) {
            // Toggle Mode Logic
            g_CurrentMode = (g_CurrentMode + 1) % 3;
            g_Config.mode = g_CurrentMode;
            // Save config...
            UpdateTrayIcon();
        } else if (wParam == 1002) {
            Shell_NotifyIconW(NIM_DELETE, &(NOTIFYICONDATAW){.hWnd=hwnd, .uID=1});
            ExitProcess(0);
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
