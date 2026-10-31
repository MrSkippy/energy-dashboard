@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0" || exit /b 1

rem Convenience build script for this SquareLine-exported ESP-IDF project.
rem You can always build directly with:  idf.py build
rem This is an incremental build - run 'idf.py fullclean' first if you need a
rem clean one.
rem
rem Note for maintainers: everything after the idf.py call below is written as
rem straight-line code on purpose. The ESP-IDF tools change the console code
rem page while they run, which corrupts cmd.exe's bookkeeping for the running
rem batch file, and a later 'call :label' then fails with "The system cannot
rem find the batch label specified". Do not reintroduce call/goto past that
rem point.

rem ---------------------------------------------------------------------------
rem Activate ESP-IDF
rem ---------------------------------------------------------------------------

rem 1) Use the already-initialized ESP-IDF environment if available
where idf.py >nul 2>nul
if %errorlevel%==0 goto ready

rem 2) Fall back to initializing ESP-IDF via the Windows installer tools
if not defined IDF_TOOLS_PATH (
    if exist "C:\Espressif\idf_cmd_init.bat" set "IDF_TOOLS_PATH=C:\Espressif"
)
if not defined IDF_TOOLS_PATH goto notfound
if not exist "%IDF_TOOLS_PATH%\idf_cmd_init.bat" goto notfound

rem Ask idf-env which installed ESP-IDF is selected, then initialize it
if exist "%IDF_TOOLS_PATH%\idf-env.exe" (
    for /f "tokens=*" %%i in ('"%IDF_TOOLS_PATH%\idf-env.exe" config get --property idfSelectedId') do set "ESP_IDF_HASH=%%i"
)
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %ESP_IDF_HASH%

where idf.py >nul 2>nul
if not %errorlevel%==0 goto notfound
goto ready

:notfound
echo Error: idf.py not found.
echo Run this script from an "ESP-IDF CMD" / "ESP-IDF PowerShell" prompt (created by the
echo ESP-IDF Windows installer), or set IDF_TOOLS_PATH to your ESP-IDF Tools folder
echo such as C:\Espressif.
echo ESP-IDF install guide: https://docs.espressif.com/projects/esp-idf/en/stable/get-started/
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b 1

:ready

rem ---------------------------------------------------------------------------
rem Show which ESP-IDF is in use and what the project asks for, so a mismatch
rem is visible up front instead of only as a CMake error deep in the output.
rem ---------------------------------------------------------------------------
set "IDF_VER="
for /f "tokens=*" %%v in ('idf.py --version 2^>nul') do set "IDF_VER=%%v"
set "IDF_PIN="
for /f "tokens=1,* delims=:" %%a in ('findstr /r /c:"^ *idf:" main\idf_component.yml 2^>nul') do set "IDF_PIN=%%b"
if defined IDF_PIN set "IDF_PIN=!IDF_PIN:"=!"
if defined IDF_PIN set "IDF_PIN=!IDF_PIN:'=!"
if defined IDF_PIN for /f "tokens=* delims= " %%s in ("!IDF_PIN!") do set "IDF_PIN=%%s"
if defined IDF_VER echo ESP-IDF in use : !IDF_VER!
if defined IDF_PIN echo Project needs  : ESP-IDF !IDF_PIN!
echo.

rem Recover from an incomplete CMake configuration left behind by an earlier
rem failed run: build\ exists but CMake never finished writing its cache, so an
rem incremental build would fail on a half-configured tree.
rem sdkconfig is deliberately left alone - it holds the project configuration.
if exist build if not exist build\CMakeCache.txt (
    echo Found a half-configured build\ directory from an earlier failed run.
    echo Removing it so CMake can configure from scratch...
    rd /s /q build 2>nul
)

idf.py build
set "SCRIPT_EXIT=%errorlevel%"

rem ---------------------------------------------------------------------------
rem Epilogue - straight-line only, see the note at the top of this file.
rem ---------------------------------------------------------------------------
if not "%SCRIPT_EXIT%"=="0" (
    echo.
    echo ------------------------------------------------------------------
    echo Build failed.
    echo.
    echo If the error above mentions "version solving failed", the ESP-IDF you
    echo are building with does not match what this project requires:
    echo.
    if defined IDF_VER echo   ESP-IDF in use : !IDF_VER!
    if defined IDF_PIN echo   Project needs  : ESP-IDF !IDF_PIN!
    echo.
    echo Pick a matching ESP-IDF version - in the Espressif-IDE, or by opening
    echo the "ESP-IDF CMD" shortcut of that version - or set IDF_PATH to one.
    echo.
    echo If you just switched ESP-IDF versions, the old build\ directory can
    echo also be the problem. Run 'idf.py fullclean' once and build again.
    echo ------------------------------------------------------------------
)

if "%SCRIPT_EXIT%"=="0" exit /b 0
rem If this window was opened by double-clicking the script, keep it open so the
rem messages above stay readable instead of vanishing with the console.
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b %SCRIPT_EXIT%
