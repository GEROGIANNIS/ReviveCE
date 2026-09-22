@echo off
setlocal

call "%~dp0setup-wince-env.cmd"
if errorlevel 1 exit /b 1

set "REPO_ROOT=%~dp0.."
set "SOURCE=%~dp0hello\hello.c"
set "OBJECT=%REPO_ROOT%\obj\hello\hello.obj"
set "OUTPUT=%REPO_ROOT%\dist\HelloWorld.exe"

if not exist "%REPO_ROOT%\obj\hello" mkdir "%REPO_ROOT%\obj\hello"
if errorlevel 1 exit /b 1
if not exist "%REPO_ROOT%\dist" mkdir "%REPO_ROOT%\dist"
if errorlevel 1 exit /b 1

echo Compiling HelloWorld for Windows CE 5.2 ARMV4I...
"%REVIVECE_CL%" /nologo /c /TC /W4 /O1 /GS- /QRarch4T ^
  /D "_WIN32_WCE=0x0502" /D UNDER_CE /D WINCE ^
  /D ARM /D _ARM_ /D UNICODE /D _UNICODE /D WIN32_PLATFORM_PSPC ^
  /Fo"%OBJECT%" "%SOURCE%"
if errorlevel 1 exit /b 1

echo Linking HelloWorld as a Windows CE GUI executable...
"%REVIVECE_LINK%" /nologo /nodefaultlib /machine:ARM ^
  /subsystem:windowsce,5.02 /entry:WinMainCRTStartup /stack:65536 ^
  /out:"%OUTPUT%" "%OBJECT%" coredll.lib
if errorlevel 1 exit /b 1

if not exist "%OUTPUT%" (
    echo ERROR: HelloWorld.exe was not produced.
    exit /b 1
)

echo Built %OUTPUT%
exit /b 0
