# Aria Download

A lightweight desktop download manager built with **Qt Widgets and C++17**,
powered by [aria2](https://aria2.github.io/). Download large files, resume
interrupted transfers, and manage a queue without typing a command for every file.

The application controls one local `aria2c` process through JSON-RPC. aria2 handles
the transfers; the GUI handles task controls, progress display, and session recovery.

## Features

- Add downloads from direct HTTP and HTTPS file links.
- Choose a destination folder and 1–16 connections per file.
- Download up to three files concurrently; eight connections per file by default.
- View status, progress, downloaded size, speed, and estimated remaining time.
- Pause, resume, retry, remove tasks, and open their destination folders.
- Retry interrupted connections and selected temporary network failures.
- Save unfinished downloads and restore them when the application reopens.
- Inspect an activity log for connection and server errors.

## Requirements

Windows is the currently tested platform. Linux and macOS builds and packaging
have not been verified.

| Dependency | Requirement |
| --- | --- |
| Qt | 6.2 or later, with Widgets and Network; Test is required when tests are enabled |
| Compiler | C++17 support; the Windows helper uses an MSVC compiler compatible with the Qt kit |
| CMake | 3.21 or later |
| Ninja | Required by the helper script and Ninja build examples |
| aria2 | `aria2c` on `PATH`, or `aria2c.exe` beside the application on Windows |

Install Qt from [qt.io](https://www.qt.io/download) and aria2 from its
[official releases](https://github.com/aria2/aria2/releases). Verify the backend:

```console
aria2c --version
```

aria2 is a separate dependency; its executable is not bundled in this repository.

## Building on Windows

Clone or download this repository. Open an **x64 Visual Studio developer command
prompt** in the repository directory and use a desktop Qt MSVC kit compatible
with your compiler and target architecture. Add CMake, Ninja, and aria2 to `PATH`.

### Build helper

Replace the placeholder with your own Qt kit directory:

```bat
set "QT_ROOT=<Qt MSVC kit directory>"
build-windows.cmd
```

From a regular command prompt, also set `VS_ROOT` to your Visual Studio
installation directory. The script calls its `VC\Auxiliary\Build\vcvars64.bat`:

```bat
set "QT_ROOT=<Qt MSVC kit directory>"
set "VS_ROOT=<Visual Studio installation directory>"
build-windows.cmd
```

The script configures a Release build, compiles, runs the tests, and deploys the
Qt runtime with `windeployqt`. Open the resulting application:

```text
build-release/AriaDownload.exe
```

The helper does not assume a particular username, drive letter, Qt version, or
Visual Studio edition. Ninja must already be available on `PATH`.

### Manual CMake build

In a compiler developer command prompt with `QT_ROOT` set:

```bat
set "PATH=%QT_ROOT%\bin;%PATH%"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"
cmake --build build-release
ctest --test-dir build-release --output-on-failure
windeployqt --release --no-translations --no-opengl-sw build-release\AriaDownload.exe
```

Tests are enabled by default. To build without Qt Test, add `-DBUILD_TESTING=OFF`
to the configuration command and skip `ctest`.

For a Visual Studio multi-configuration generator, select the matching architecture
when configuring, use `--config Release` when building, and pass `-C Release` to
CTest. The executable is normally under the build directory's `Release` subfolder;
run `windeployqt` against that executable.

You can also open `CMakeLists.txt` in **Qt Creator** and select a compatible desktop
kit. Use a fresh build directory when changing compilers or generators.

## Usage

1. Launch `AriaDownload.exe`.
2. Paste a **direct file URL** into File URL.
3. Choose a destination using Save to or Browse.
4. Adjust connections per file if needed and select **Add download**.
5. Select a task to **Pause**, **Resume / Retry**, **Remove**, or **Open folder**.

For repositories such as Zenodo, copy the individual file's download link. A record
or dataset landing page is not a direct file link and may download HTML instead
of the intended data. Reduce parallel connections if a server throttles requests;
more connections do not guarantee higher speed.

### Closing, resuming, and removing

- Closing the app stops downloads and saves unfinished tasks.
- Reopening restores unfinished tasks; paused tasks stay paused.
- Removing a task keeps downloaded files and partial data on disk.
- Keep a partial file and its `.aria2` control file together for resuming.
- Completed results are kept for the current backend session only, up to 1,000 results.

Downloads do not continue in the background after closing the app.

## Connection recovery

| Setting | Default |
| --- | --- |
| aria2 retry attempts | Unlimited (`max-tries=0`) |
| Retry delay | 5 seconds |
| Connection timeout | 30 seconds |
| Transfer timeout | 30 seconds |
| Minimum transfer speed | No cutoff; slow but healthy transfers keep running |
| Progress/control-file save interval | 5 seconds |
| Session save interval | 5 seconds, plus task actions and normal shutdown |

The GUI also requeues stopped tasks with aria2 error codes **2, 5, 6, 19, and 29**:
timeouts, slow-transfer errors, network errors, DNS failures, and temporary server
overload. Other errors remain visible for manual action.

Retries cannot repair invalid URLs, expired links, missing files, disk-space
problems, or incorrect DNS responses. Resume requires server support for byte
ranges and an unchanged remote file. The app does not deliberately overwrite
existing files or automatically rename collisions.

See the [aria2 manual](https://aria2.github.io/manual/en/html/aria2c.html) for backend
options and error-code details.

## Troubleshooting

### aria2c was not found

Install aria2 and add its directory to `PATH`, then restart the app. On Windows,
you can instead place `aria2c.exe` beside `AriaDownload.exe`. The activity log
reports which executable the app selected.

### Connecting or stalled without progress

The activity log opens automatically after an active task makes no progress for
30 seconds. It includes INFO-level retry reasons while filtering routine local
RPC traffic. Check for DNS, timeout, TLS, and server errors.

If a hostname resolves to `0.0.0.0` or `::`, DNS has returned an unusable destination.
Retrying will not fix that. Compare operating-system DNS settings with browser
Secure DNS and proxy settings. A browser may use a different network route from
aria2. Restart the app after correcting network settings to discard cached results.

### A task fails immediately

Select it and read the error details. Check the file URL, destination permissions,
free disk space, and existing-file conflicts. TLS certificate checking remains
enabled; check the system clock and certificate configuration for TLS errors.

### The executable cannot start on another computer

Use `windeployqt` from the same Qt kit used to build the app. Keep the deployed
DLLs and plugin directories beside the executable. Copying only the `.exe` is
insufficient unless the destination already has the required runtime available.
The destination also needs a compatible Microsoft C++ runtime and `aria2c`.

## Data and local backend

Sessions are stored in Qt's per-user application data directory, normally
`%LOCALAPPDATA%\AriaDownload\AriaDownload` on Windows. Window geometry, destination
folder, and connection-count preferences use `QSettings` (normally the current
user's registry on Windows).

- Session files contain source URLs, which may include private access tokens.
- Logs may include URLs and local paths; redact these before sharing.
- RPC listens locally with a random secret and a dynamically selected port.
- The backend does not read the user's global aria2 configuration file.
- A process lock prevents two app instances from sharing a session.
- aria2 is configured to exit if its GUI process disappears.

## Current limitations

- Direct HTTP(S) file links only; no torrents, magnet links, or record-page import.
- No interface for credentials, custom headers, cookies, proxies, or DNS settings.
- No persistent completed-download history, tray mode, or background downloads.
- The table queries up to 1,000 waiting and 1,000 stopped tasks.
- Abrupt termination can lose changes since the last successful save.
- Cross-platform packaging has not been tested.

## Development and tests

```text
src/
  main.cpp             Startup and single-instance lock
  AriaBackend.*        Process lifecycle, RPC, retry policy, and sessions
  DownloadModel.*      Table model and transfer formatting
  MainWindow.*         Widgets interface and UI preferences
tests/
  backend_tests.cpp    Local HTTP server and real aria2 integration tests
  gui_tests.cpp        Offscreen interface smoke test
```

Backend tests interrupt a local transfer and verify resumed content, exercise
pause/restart/resume, check missing-file errors and removal, and verify slow
transfers complete without unnecessary reconnections. The GUI smoke test checks
startup, input, and table structure and saves a screenshot in the build directory.
Tests use temporary data and do not download external datasets.

Run with Qt's runtime directory and `aria2c` on `PATH`:

```console
ctest --test-dir build-release --output-on-failure
```

Detailed reports are written to `backend-results.txt` and `gui-results.txt` in the
test working directory, normally the build directory.

## Contributing

For bug reports, include the operating system, Qt and aria2 versions, reproduction
steps, expected and actual behavior, and relevant redacted log output. For network
issues, mention whether the same URL works with command-line aria2.

Keep pull requests focused, add regression coverage for behavioral fixes, and run
the existing tests. Do not commit generated build output, downloaded datasets,
session files, credentials, or unredacted logs.

## License

Aria Download is licensed under the **GNU General Public License, version 3 only**
(`SPDX-License-Identifier: GPL-3.0-only`). See [LICENSE](LICENSE) for the full text.

You may use, modify, and redistribute this project under GPLv3. When distributing
covered binaries or modified versions, you must comply with GPLv3's requirements,
including providing the corresponding source code and preserving license notices.
The software is provided without warranty, as described in the license.

Contributions to this repository are provided under the same license. Qt and
aria2 are separate dependencies with their own licenses; this project's license
does not replace their terms.
