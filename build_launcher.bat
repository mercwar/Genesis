@echo off
setlocal EnableDelayedExpansion
title Genesis.exe Builder

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BIN=%ROOT%\bin"
set "OBJ=%ROOT%\obj"
set "LIB_DIR=%ROOT%\lib"
set "SRC_C=%ROOT%\launcher.c"
set "SRC_RC=%ROOT%\launcher.rc"
set "RES_OUT=%OBJ%\launcher.res"
set "DLL_IN=%BIN%\Genesis.dll"
set "EXE_OUT=%BIN%\Genesis.exe"
set "CRASH_LOG=%ROOT%\launcher_build_crash.log"

for /f %%A in ('echo prompt $E ^| cmd') do set "ESC=%%A"
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "CYAN=%ESC%[96m"
set "WHITE=%ESC%[97m"
set "RESET=%ESC%[0m"

if not exist "%BIN%" mkdir "%BIN%"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%LIB_DIR%" mkdir "%LIB_DIR%"
if exist "%CRASH_LOG%" del /q "%CRASH_LOG%" >nul 2>&1

echo %CYAN%============================================================%RESET%
echo %WHITE%  GENESIS LAUNCHER BUILDER (EMBEDDED DLL RESOURCE)%RESET%
echo %CYAN%============================================================%RESET%

:: [1/4] Verify Required Source & Embedded DLL Files
set "STEP=1/4 - Verify launcher.c, launcher.rc, and bin\Genesis.dll"
set "DETAIL_LOG="
echo %YELLOW%[%STEP%]...%RESET%

for %%F in ("%SRC_C%" "%SRC_RC%" "%DLL_IN%") do (
    if not exist "%%~F" (
        set "ERR_MSG=Missing required file: %%~F (Build Genesis.dll first)"
        set "LAST_RC=1"
        goto :fail
    )
)

:: [2/4] Auto-Initialize MSVC x64 Environment if Needed
set "STEP=2/4 - Initialize MSVC x64 toolchain"
echo %YELLOW%[%STEP%]...%RESET%

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_PATH=%%I"
        )
    )
    if defined VS_PATH (
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
            call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        )
    )
)

cl /nologo /W4 /O2 /I"%ROOT%" "%SRC_C%" "%RES_OUT%" ^
    /Fo:"%OBJ%\launcher.obj" ^
    /Fe:"%EXE_OUT%" ^
    /link /SUBSYSTEM:WINDOWS >"%DETAIL_LOG%" 2>&1


:: [3/4] Compile launcher.rc -> obj\launcher.res
set "STEP=3/4 - Compile launcher.rc resource"
set "DETAIL_LOG=%OBJ%\rc_launcher.log"
echo %YELLOW%[%STEP%]...%RESET%

rc /nologo /I"%ROOT%" /fo "%RES_OUT%" "%SRC_RC%" >"%DETAIL_LOG%" 2>&1
if errorlevel 1 (
    set "ERR_MSG=rc.exe failed to compile launcher.rc."
    set "LAST_RC=!errorlevel!"
    goto :fail
)

:: [4/4] Compile launcher.c + launcher.res -> bin\Genesis.exe
set "STEP=4/4 - Compile Genesis.exe"
set "DETAIL_LOG=%OBJ%\cl_launcher.log"
echo %YELLOW%[%STEP%]...%RESET%

taskkill /F /IM Genesis.exe >nul 2>&1
sc query Genesis >nul 2>&1
if not errorlevel 1 (
    sc stop Genesis >nul 2>&1
    timeout /t 1 /nobreak >nul
)

cl /nologo /W4 /O2 /I"%ROOT%" "%SRC_C%" "%RES_OUT%" ^
    /Fo:"%OBJ%\launcher.obj" ^
    /Fe:"%EXE_OUT%" >"%DETAIL_LOG%" 2>&1

if errorlevel 1 (
    set "ERR_MSG=cl.exe failed to compile Genesis.exe."
    set "LAST_RC=!errorlevel!"
    goto :fail
)

echo %GREEN%      [OK] Built %EXE_OUT%%RESET%
echo %CYAN%============================================================%RESET%
echo %GREEN%[SUCCESS] Launcher build complete.%RESET%
echo   - EXE : %EXE_OUT%
echo   - RES : %RES_OUT%
echo   - OBJ : %OBJ%\launcher.obj
echo %CYAN%============================================================%RESET%
pause
exit /b 0

:fail
echo %RED%[BUILD FAILED] Step: %STEP%%RESET%
echo %RED%[REASON]       %ERR_MSG% (Exit Code: %LAST_RC%)%RESET%
(
    echo GENESIS LAUNCHER BUILD CRASH REPORT - %DATE% %TIME%
    echo STEP: %STEP%
    echo ERROR: %ERR_MSG%
    echo EXIT CODE: %LAST_RC%
    echo ============================================================
) > "%CRASH_LOG%"
if defined DETAIL_LOG (
    if exist "%DETAIL_LOG%" (
        echo --- Tool Output --- >> "%CRASH_LOG%"
        type "%DETAIL_LOG%" >> "%CRASH_LOG%"
        type "%DETAIL_LOG%"
    )
)
echo %YELLOW%Crash log saved to: %CRASH_LOG%%RESET%
pause
exit /b %LAST_RC%
