# Aria Download

A small Qt 6 / C++17 desktop download manager using one local `aria2c` JSON-RPC backend.

## Run on this Windows PC

After building, open `build-release/AriaDownload.exe`. Keep the Qt DLLs and plugin
folders beside the executable. The app finds `aria2c.exe` on PATH, or beside itself.

1. Paste a **direct HTTP(S) file URL** (for Zenodo, copy a file's download link).
2. Choose a download folder and click **Add download**.
3. Select a row to pause, resume/retry, remove, or open its folder.

Eight connections per file and three simultaneous files are the defaults. Reduce
connections to 1–4 when a server limits parallel requests. Progress, downloaded
bytes, speed and estimated remaining time update once per second.

Closing the app stops downloads and saves unfinished tasks. Reopening restores
them; paused tasks stay paused. Removing a task **keeps its files**, including any
partial file and `.aria2` control file. Completed history is kept for the current
backend session only (up to 1,000 results). This first version accepts direct file
links, not Zenodo record pages, torrents, or magnet links.

## Connection recovery

- Unlimited aria2 retries, five-second retry delay, 30-second connection/read timeouts.
- No minimum-speed cutoff: healthy slow transfers keep running, as with the CLI command.
  The 30-second timeout still reconnects unresponsive connections.
- If aria2 stops with a timeout, slow-transfer, network, DNS or temporary-server
  error (codes 2, 5, 6, 19, 29), the GUI requeues it after five seconds.
- Resumes partial data, saves `.aria2` progress and the session every five seconds.
- Missing files, authorization failures, disk errors and unsupported range resume
  remain visible for manual action; repeatedly retrying those would not fix them.
- Existing files are never deliberately overwritten or automatically renamed.
  Keep `.aria2` control files with partial downloads. Resume depends on the server
  supporting byte ranges and the remote file remaining unchanged.

Settings and `aria2.session` are in Qt's per-user application data directory
(normally `%LOCALAPPDATA%/AriaDownload/AriaDownload`). Session files contain source
URLs, so treat links containing access tokens as private. The backend uses a random
RPC secret and loopback-only port; it does not read your global aria2 config.
TLS certificate checking stays enabled. A process lock protects the session from
two app instances, and aria2 exits if its GUI process disappears.

## Build and test

Requirements: Qt 6.2+ Widgets, Network and Test; CMake 3.21+; C++17 compiler; aria2c.
On this PC, run:

```bat
build-windows.cmd
```

The script uses Qt 6.6.1 MSVC, Visual Studio 2022 and Ninja, runs the integration
tests, then deploys the Qt runtime beside the executable. Override `QT_ROOT` and
`VS_ROOT` for other installation locations. No administrator access is required.

Alternatively, in a compiler developer shell with Qt on PATH:

```bat
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.6.1/msvc2019_64
cmake --build build-release
ctest --test-dir build-release --output-on-failure
windeployqt --release build-release/AriaDownload.exe
```

Open `CMakeLists.txt` in Qt Creator to develop the app. Use a matching Qt/compiler
kit. The backend integration test uses a local HTTP range server and the real
aria2 executable: it deliberately truncates a transfer, verifies resumed bytes,
checks pause/restart/resume persistence, and checks 404 handling and removal.
No external dataset is downloaded by the tests.

If a task receives no data for 30 seconds, the activity log opens automatically.
It includes aria2's INFO-level retry reasons (hidden by the original WARN setting),
so DNS, connection timeouts and server errors can be distinguished. The tests also
verify that a transfer below 10 KiB/s completes without being restarted.

## Source layout

- `AriaBackend`: process lifecycle, asynchronous RPC, session saving and retry policy.
- `DownloadModel`: stable table rows and transfer formatting.
- `MainWindow`: native Widgets interface and persisted UI preferences.
- `tests/backend_tests.cpp`: real backend integration coverage.

Retry and session behavior follows the [official aria2 manual](https://aria2.github.io/manual/en/html/aria2c.html).
