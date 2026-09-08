#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Define globals here
SUPPRESSION_MODE g_CurrentMode = MODE_AGGRESSIVE;
AppConfig g_Config = { .mode = MODE_AGGRESSIVE, .showRamUsage = true, .autoDetectSiSR = true };

void LoadConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"r");
    if (!f) return;

    wchar_t line[256];
    while (fgetws(line, 256, f)) {
        if (wcsncmp(line, L"Mode=", 5) == 0) {
            int val = _wtoi(line + 5);
            if (val >= 0 && val <= 2) {
                g_Config.mode = (SUPPRESSION_MODE)val;
                g_CurrentMode = g_Config.mode;
            }
        } else if (wcsncmp(line, L"ShowRam=", 8) == 0) {
            g_Config.showRamUsage = (_wtoi(line + 8) != 0);
        } else if (wcsncmp(line, L"AutoSiSR=", 9) == 0) {
            g_Config.autoDetectSiSR = (_wtoi(line + 9) != 0);
        }
    }
    fclose(f);
}

void SaveConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"w");
    if (!f) return;
    fwprintf(f, L"Mode=%d\n", g_Config.mode);
    fwprintf(f, L"ShowRam=%d\n", g_Config.showRamUsage ? 1 : 0);
    fwprintf(f, L"AutoSiSR=%d\n", g_Config.autoDetectSiSR ? 1 : 0);
    fclose(f);
}

BOOL ShouldKillProcess(const WCHAR* cmdLine, SUPPRESSION_MODE mode) {
    if (mode == MODE_DISABLED) return FALSE;
    if (!cmdLine) return TRUE;

    // Selective mode: Keep broker/GPU processes
    if (mode == MODE_SELECTIVE) {
        if (wcsstr(cmdLine, L"--type=broker")) return FALSE;
        if (wcsstr(cmdLine, L"--type=gpu-process")) return FALSE;
    }

    // Kill anything that looks like a renderer/helper
    if (wcsstr(cmdLine, L"steamwebhelper")) return TRUE;

    return FALSE;
}
