@echo off

if not exist "%~dp0toolchain-env.cmd" (
    echo ERROR: ci\toolchain-env.cmd is missing.
    echo Run ci\install-toolchain.ps1 first.
    exit /b 1
)

call "%~dp0toolchain-env.cmd"
if errorlevel 1 exit /b 1

if /i not "%TARGETCPU%"=="ARMV4I" (
    echo ERROR: TARGETCPU is not ARMV4I.
    exit /b 1
)
if not exist "%REVIVECE_CL%" (
    echo ERROR: CE ARM compiler was not found.
    exit /b 1
)
if not exist "%REVIVECE_LINK%" (
    echo ERROR: CE ARM linker was not found.
    exit /b 1
)

exit /b 0
