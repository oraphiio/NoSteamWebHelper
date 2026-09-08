#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

typedef enum {
    MODE_AGGRESSIVE = 0,
    MODE_SELECTIVE = 1,
    MODE_DISABLED = 2
} SUPPRESSION_MODE;

typedef struct {
    SUPPRESSION_MODE mode;
    bool showRamUsage;
    bool autoDetectSiSR;
} AppConfig;

// Global variables defined in config.c
extern SUPPRESSION_MODE g_CurrentMode;
extern AppConfig g_Config;

// Functions
void LoadConfig(const wchar_t* path);
void SaveConfig(const wchar_t* path);
BOOL ShouldKillProcess(const WCHAR* cmdLine, SUPPRESSION_MODE mode);

#endif
