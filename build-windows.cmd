@echo off
setlocal
rem Run from any directory. Override QT_ROOT if your Qt installation differs.
cd /d "%~dp0"
if not defined QT_ROOT set "QT_ROOT=C:\Qt\6.6.1\msvc2019_64"
if not defined VS_ROOT set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=C:\Qt\Tools\Ninja;%QT_ROOT%\bin;%PATH%"
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
