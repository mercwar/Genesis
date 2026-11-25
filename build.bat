@echo off
setlocal EnableDelayedExpansion
title Genesis.dll Builder (x64)

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BIN=%ROOT%\bin"
set "OBJ=%ROOT%\obj"
set "LIB_DIR=%ROOT%\lib"
set "SRC_C=%ROOT%\Genesis.c"
set "SRC_DEF=%ROOT%\Genesis.def"
set "DLL_OUT=%BIN%\Genesis.dll"
set "CRASH_LOG=%ROOT%\build_crash.log"

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
echo %WHITE%  GENESIS.DLL BUILDER (x64)%RESET%
echo %CYAN%============================================================%RESET%

:: [1/3] Verify Genesis.c Exists
set "STEP=1/3 - Verify Genesis.c"
set "DETAIL_LOG="
echo %YELLOW%[%STEP%]...%RESET%

if not exist "%SRC_C%" (
    set "ERR_MSG=Missing source file: %SRC_C%"
    set "LAST_RC=1"
    goto :fail
)

if not exist "%SRC_DEF%" (
    (
        echo LIBRARY Genesis
        echo EXPORTS
        echo     ServiceMain
    ) > "%SRC_DEF%"
)

:: [2/3] Force-Initialize MSVC x64 Environment
set "STEP=2/3 - Initialize MSVC x64 toolchain"
echo %YELLOW%[%STEP%]...%RESET%

if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
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

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "ERR_MSG=cl.exe not found. Install Visual Studio C++ Build Tools."
    set "LAST_RC=1"
    goto :fail
)

:: [3/3] Stop Running Service & Compile x64 Genesis.dll
set "STEP=3/3 - Compile x64 Genesis.dll"
set "DETAIL_LOG=%OBJ%\cl_build.log"
echo %YELLOW%[%STEP%]...%RESET%

sc query Genesis >nul 2>&1
if not errorlevel 1 (
    sc stop Genesis >nul 2>&1
    timeout /t 2 /nobreak >nul
)

if exist "%DLL_OUT%" del /f /q "%DLL_OUT%" >nul 2>&1

cl /nologo /LD /W4 /O2 "%SRC_C%" user32.lib advapi32.lib ^
    /Fo:"%OBJ%\Genesis.obj" ^
    /Fe:"%DLL_OUT%" ^
    /link /MACHINE:X64 /DEF:"%SRC_DEF%" /IMPLIB:"%LIB_DIR%\Genesis.lib" >"%DETAIL_LOG%" 2>&1

if errorlevel 1 (
    set "ERR_MSG=cl.exe failed to compile Genesis.dll."
    set "LAST_RC=!errorlevel!"
    goto :fail
)

move /y "%LIB_DIR%\*.exp" "%OBJ%\" >nul 2>&1

echo %GREEN%      [OK] Built x64 %DLL_OUT%%RESET%
echo %CYAN%============================================================%RESET%
echo %GREEN%[SUCCESS] Build complete.%RESET%
echo   - DLL : %DLL_OUT%
echo   - OBJ : %OBJ%\Genesis.obj
echo   - LIB : %LIB_DIR%\Genesis.lib
echo %CYAN%============================================================%RESET%
pause
exit /b 0

:fail
echo %RED%[BUILD FAILED] Step: %STEP%%RESET%
echo %RED%[REASON]       %ERR_MSG% (Exit Code: %LAST_RC%)%RESET%
(
    echo GENESIS.DLL BUILD CRASH REPORT - %DATE% %TIME%
    echo STEP: %STEP%
    echo ERROR: %ERR_MSG%
    echo EXIT CODE: %LAST_RC%
    echo ============================================================
) > "%CRASH_LOG%"
if defined DETAIL_LOG (
    if exist "%DETAIL_LOG%" (
        echo --- Compiler Output --- >> "%CRASH_LOG%"
        type "%DETAIL_LOG%" >> "%CRASH_LOG%"
        type "%DETAIL_LOG%"
    )
)
echo %YELLOW%Crash log saved to: %CRASH_LOG%%RESET%
pause
exit /b %LAST_RC%
