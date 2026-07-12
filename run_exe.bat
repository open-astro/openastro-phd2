@echo off
setlocal enabledelayedexpansion

rem =======================================================================
rem  run_exe.bat - Configure and build PHD2 on Windows
rem
rem  Every run wipes tmp\ and starts clean, matching run_deb.sh and
rem  run_dmg.sh on the other platforms (and build-exe.ps1's behavior on
rem  this one). This keeps the dev build deterministic and avoids the
rem  in-place CMake reconfigure path, which has historically tripped on
rem  vcpkg's FetchContent UPDATE step.
rem
rem  Usage:
rem    run_exe.bat                Clean configure + build (default)
rem    run_exe.bat -config        Clean configure only, no build
rem    run_exe.bat -launch        Clean configure + build, then start phd2.exe
rem    run_exe.bat -help          Show this help
rem =======================================================================

set BUILD=1
set LAUNCH=0

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-config"      ( set BUILD=0   & shift & goto parse_args )
if /i "%~1"=="--config"     ( set BUILD=0   & shift & goto parse_args )
if /i "%~1"=="-config-only" ( set BUILD=0   & shift & goto parse_args )
if /i "%~1"=="-launch"      ( set LAUNCH=1  & shift & goto parse_args )
if /i "%~1"=="--launch"     ( set LAUNCH=1  & shift & goto parse_args )
if /i "%~1"=="-help"        goto show_help
if /i "%~1"=="--help"       goto show_help
if /i "%~1"=="-?"           goto show_help
if /i "%~1"=="/?"           goto show_help
echo Unknown option: %~1
echo.
goto show_help

:show_help
echo.
echo Usage: run_exe.bat [-config] [-launch] [-help]
echo.
echo Every run wipes tmp\ and configures + builds from scratch (matches the
echo behavior of run_deb.sh, run_dmg.sh, and build-exe.ps1).
echo.
echo   -config   Only run cmake configure, do not build.
echo   -launch   After a successful build, start tmp\Release\phd2.exe.
echo   -help     Show this message.
echo.
exit /b 0

:args_done

rem -----------------------------------------------------------------------
rem  Kill known build-process stragglers that hold file handles in tmp\.
rem  Returns nonzero when the process isn't running -- that's fine.
rem -----------------------------------------------------------------------
taskkill /f /im vctip.exe >nul 2>&1
taskkill /f /im vcpkg.exe >nul 2>&1
taskkill /f /im cl.exe >nul 2>&1
taskkill /f /im MSBuild.exe >nul 2>&1

rem -----------------------------------------------------------------------
rem  Robust delete: try rmdir, retry once, then fall back to renaming the
rem  directory out of the way (works even when files inside are locked).
rem -----------------------------------------------------------------------
if exist tmp (
    rmdir /s /q tmp 2>nul
    if exist tmp (
        echo Locks detected on tmp\. Waiting 3s then retrying...
        timeout /t 3 /nobreak >nul
        rmdir /s /q tmp 2>nul
    )
)

if exist tmp (
    rem  Delayed expansion (!VAR!) is required here: %STAMP% would be expanded
    rem  once at parse time -- before the SET runs -- so every locked run would
    rem  collide on the literal name "tmp_locked_". !STAMP! expands at execution
    rem  time, giving each run a unique target and a truthful message.
    set "STAMP=!RANDOM!!RANDOM!"
    move tmp tmp_locked_!STAMP! >nul 2>&1
    if errorlevel 1 (
        echo.
        echo ERROR: Could not delete or rename tmp\.
        echo A process likely has tmp\ as its current working directory.
        echo Common culprits: a shell sitting in tmp\, Visual Studio with
        echo                  the project loaded, Windows Search indexing.
        echo.
        echo Open Resource Monitor ^(resmon^), search "tmp" under Associated
        echo Handles on the CPU tab, then close the offending process.
        exit /b 1
    )
    echo Locked tmp\ has been renamed to tmp_locked_!STAMP!\
    echo ^(Delete it manually later when nothing holds it.^)
)

mkdir tmp

rem -----------------------------------------------------------------------
rem  Pre-clone vcpkg with visible progress. CMake's FetchContent normally
rem  does this clone silently inside an MSBuild custom-build step, but a
rem  visible ~500MB download beats a stalled-looking build.
rem
rem  IMPORTANT: because we pre-populate tmp\_deps\vcpkg-src, FetchContent
rem  finds the directory already there and SKIPS its own clone+checkout of
rem  the pinned GIT_TAG. So we MUST check out that exact commit here, or the
rem  build silently runs against vcpkg HEAD. Keep VCPKG_PIN in lockstep with
rem  the GIT_TAG in thirdparty\thirdparty.cmake (currently 2026.03.18).
rem -----------------------------------------------------------------------
set "VCPKG_PIN=c3867e714dd3a51c272826eea77267876517ed99"

where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: git is required but not on PATH.
    exit /b 1
)

mkdir tmp\_deps
echo.
echo Pre-cloning vcpkg ^(~500MB; one-time per clean build^)...
git clone --progress https://github.com/microsoft/vcpkg.git tmp\_deps\vcpkg-src
if errorlevel 1 (
    echo.
    echo ERROR: vcpkg clone failed. Check network/proxy settings.
    exit /b 1
)

echo Checking out pinned vcpkg commit %VCPKG_PIN%...
git -C tmp\_deps\vcpkg-src checkout --quiet %VCPKG_PIN%
if errorlevel 1 (
    echo.
    echo ERROR: could not check out pinned vcpkg commit %VCPKG_PIN%.
    echo Verify VCPKG_PIN matches the GIT_TAG in thirdparty\thirdparty.cmake.
    exit /b 1
)

cd tmp
if errorlevel 1 (
    echo.
    echo ERROR: Failed to change directory to tmp\.
    exit /b 1
)

set VCPKG_MAX_CONCURRENCY=%NUMBER_OF_PROCESSORS%
set CMAKE_BUILD_PARALLEL_LEVEL=%NUMBER_OF_PROCESSORS%
set "CL=%CL% /MP"

rem -----------------------------------------------------------------------
rem  Configure. Running cmake explicitly (vs. relying on cmake --build to
rem  reconfigure) surfaces config errors earlier and lets -config work on
rem  its own.
rem -----------------------------------------------------------------------
cmake -Wno-dev -A x64 ..
if errorlevel 1 (
    echo.
    echo CMake configure failed.
    cd ..
    exit /b 1
)

if %BUILD%==0 (
    cd ..
    echo.
    echo Configure complete. Skipping build ^(-config flag^).
    exit /b 0
)

echo.
echo ============================================================
echo Building with %NUMBER_OF_PROCESSORS% cores ^(Release^)...
echo ============================================================
echo.

cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo.
    echo Build failed. See errors above.
    cd ..
    exit /b 1
)

cd ..

echo.
echo ============================================================
echo Build complete.
echo.
echo   Binary:  tmp\Release\phd2.exe
echo   Launch:  start tmp\Release\phd2.exe
echo.
echo ============================================================

if %LAUNCH%==1 (
    echo Launching phd2.exe...
    start "" "tmp\Release\phd2.exe"
)

exit /b 0
