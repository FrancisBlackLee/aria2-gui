@echo off
setlocal
rem Run from an x64 developer prompt with QT_ROOT set to a Qt MSVC kit.
cd /d "%~dp0"
if not defined QT_ROOT (
  echo Set QT_ROOT to your Qt MSVC kit directory before running this script.
  exit /b 1
)
if not exist "%QT_ROOT%\bin\windeployqt.exe" (
  echo QT_ROOT must contain bin\windeployqt.exe. Select a desktop Qt MSVC kit.
  exit /b 1
)
if defined VS_ROOT (
  call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
  if errorlevel 1 exit /b 1
)
where cl.exe >nul 2>&1
if errorlevel 1 (
  echo Run from an x64 Visual Studio developer prompt or set VS_ROOT.
  exit /b 1
)
set "PATH=%QT_ROOT%\bin;%PATH%"
for %%T in (cmake.exe ctest.exe ninja.exe aria2c.exe) do (
  where %%T >nul 2>&1
  if errorlevel 1 (
    echo Missing %%T. Add its installation directory to PATH.
    exit /b 1
  )
)
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%" -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
cmake --build build-release
if errorlevel 1 exit /b 1
ctest --test-dir build-release --output-on-failure
if errorlevel 1 (
  type build-release\backend-results.txt
  type build-release\gui-results.txt
  exit /b 1
)
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw build-release\AriaDownload.exe
exit /b %errorlevel%
