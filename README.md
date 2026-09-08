# NoSteamWebHelper

A lightweight, automated Windows utility designed to suppress resource-heavy `steamwebhelper.exe` background processes. It hooks natively into Steam's lifecycle, freezing or terminating helper sub-processes while a game is actively running to maximize available RAM and system responsiveness.

## Features

* **Dynamic Performance Recovery:** Automatically detects when a Steam game starts and terminates redundant web helper instances.
* **Three Suppression Modes:**
  * **Aggressive:** Eliminates all `steamwebhelper` components upon game launch.
  * **Selective:** Retains critical broker and GPU processes (`--type=broker`, `--type=gpu-process`) to preserve minimum UI functionality, while cleaning up memory-heavy renderers.
  * **Disabled:** Suspends program automation entirely when full client functionality is needed.
* **Smart Ecosystem Detection:** Integrates with community projects by auto-detecting third-party launchers (like `SiSR.exe` / `SiSR.Core.exe`).
* **Real-time System Tray Monitor:** Displays the current active suppression mode and tracks total RAM wasted/saved by web helper processes directly from the taskbar tooltip.
* **Zero Overhead Injection:** Built as a minimal native C DLL meant to run seamlessly alongside target environments with negligible footprint.

---

## Configuration (`NoSteamWebHelper.ini`)

The application automatically reads from and writes to a configuration file located in its local directory. Below is the structure and configuration key definitions:

```ini
Mode=0
ShowRam=1
AutoSiSR=1
```

### Configuration Keys

| Key | Value | Description |
| :--- | :--- | :--- |
| **`Mode`** | `0` (Aggressive)<br>`1` (Selective)<br>`2` (Disabled) | Sets the baseline behavior for background engine behavior. |
| **`ShowRam`** | `1` (Enabled)<br>`0` (Disabled) | Calculates and prints total active web helper memory footprints to the system tray. |
| **`AutoSiSR`** | `1` (Enabled)<br>`0` (Disabled) | Automatically searches process trees for active SiSR ecosystem applications. |

---

## Technical Architecture

* **WinEvent Hooks:** Monitors window hierarchies (`vguiPopupWindow`) via custom `SetWinEventHook` bindings to intercept initialization calls cleanly.
* **Registry Monitoring:** Employs an event-driven `RegNotifyChangeKeyValue` worker loop on `HKCU\SOFTWARE\Valve\Steam` to track `RunningAppID` changes with zero polling waste.
* **Process Interception Topology:** Walks process snapshots linearly through `CreateToolhelp32Snapshot` to map parent-child PID branches, avoiding accidental system modifications.

---

## Building from Source

The project relies purely on native Win32 APIs and requires no heavy external frameworks. 

### Prerequisites
* Windows SDK (10/11)
* MSVC Compiler toolchain (via Visual Studio) or MinGW gcc

### Compilation via MSVC (Developer Command Prompt)
```bash
cl.exe /LD /O2 main.c config.c /link /OUT:NoSteamWebHelper.dll user32.lib shell32.lib psapi.lib shlwapi.lib advapi32.lib
```
