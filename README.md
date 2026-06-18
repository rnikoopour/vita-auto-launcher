# Vita Auto Launcher

A taiHEN plugin for PS Vita (firmware 3.60, HENkaku/Ensō) that replaces the default LiveArea launcher with a custom launcher on boot and whenever you return from a game or homebrew.

Comes with a companion configurator app (VPK) for selecting the launcher from within the Vita.

## Components

### Plugin — `VitaAutoLauncher.suprx`

Runs inside SceShell. Redirects to your chosen launcher:

- On boot, when LiveArea would normally appear
- When you close a game or homebrew and return to LiveArea

The redirect fires after a brief delay (up to ~5 seconds) once the previous app has fully exited. This allows games launched from the custom launcher enough time to start before the plugin decides the user has returned to LiveArea.

### Configurator — `VALConfigurator.vpk`

A vita2d app for configuring the plugin without editing files.

| Button | Action |
|--------|--------|
| Up / Down | Browse installed apps |
| O | Select as launcher |
| Triangle | Toggle plugin enabled/disabled (persists across reboots) |
| X | Exit |

## Bypasses

| Input | Scope | How to trigger |
|-------|-------|----------------|
| SELECT | Skip the next redirect only — resumes normally after | Hold SELECT while booting or exiting a game |
| L + R | Disable all redirects until the next reboot | Hold L + R while booting or exiting a game |
| Triangle in configurator | Disable persistently across reboots | Toggle in the configurator (header turns red when active) |

You can also create `ux0:/data/VitaAutoLauncher/disabled` manually to persistently disable without opening the configurator.

## Installation

### Plugin

1. Copy `VitaAutoLauncher.suprx` to `ur0:/tai/`
2. Add to `ur0:/tai/config.txt` under `*main`, before any other entries:
   ```
   *main
   ur0:tai/VitaAutoLauncher.suprx
   ```
3. Reboot

### Configurator

Install `VALConfigurator.vpk` normally via VitaShell or a package manager.

## Data files

| Path | Purpose |
|------|---------|
| `ux0:/data/VitaAutoLauncher/config.txt` | Title ID of the launcher to redirect to (9 chars) |
| `ux0:/data/VitaAutoLauncher/disabled` | If present, plugin is disabled |

## Building

Requires Docker.

```sh
make          # build Docker image and compile everything
make clean    # remove build directory
```

Outputs:
- `build/plugin/VitaAutoLauncher.suprx`
- `build/configurator/VALConfigurator.vpk`

## Requirements

- PS Vita with HENkaku or Ensō (firmware 3.60)
- taiHEN
- Docker (for building)
