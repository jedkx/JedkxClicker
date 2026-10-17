# JedkxClicker

A small, portable Windows auto-clicker written in native C++ and Win32.

## Download

The repository includes a ready-to-run [`JedkxClicker.exe`](JedkxClicker.exe). No installer, .NET runtime, or Visual C++ Redistributable is required.

## Features

- Left, right, and middle mouse clicks
- Single or double click actions
- Configurable click interval
- Run until stopped or stop after an exact number of clicks
- Follow the cursor or use a fixed screen point
- Global F6–F11 start/stop shortcut
- Dark interface and an embedded application icon
- No network access, telemetry, settings persistence, logs, startup entry, or registry writes

## Run

Download and launch `JedkxClicker.exe`. The application is self-contained and requires no installation.

> Windows prevents lower-privileged applications from sending input to higher-privileged windows. To control an application running as administrator, run JedkxClicker as administrator too. Some games and anti-cheat systems may block synthetic input; JedkxClicker does not bypass those restrictions.

## Build from source

### Requirements

- Windows
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload

### Build

Open an **x86 Native Tools Command Prompt for Visual Studio** in this folder and run:

```bat
build.bat
```

The build creates `JedkxClicker.exe`. It statically links the C++ runtime, so the produced EXE has no Visual C++ Redistributable dependency.

## GitHub Actions

Every push, pull request, and manual workflow run builds `JedkxClicker.exe` on Windows and uploads it as the `JedkxClicker-windows-x86` artifact. The current prebuilt EXE is also kept in the repository for direct download.

## License

MIT. See [LICENSE](LICENSE).
