#include <windows.h>

int WINAPI WinMain(HINSTANCE instance,
                   HINSTANCE previousInstance,
                   LPWSTR commandLine,
                   int showCommand)
{
    (void)instance;
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    MessageBoxW(NULL,
                L"ARMV4I build works on this device.",
                L"ReviveCE HelloWorld",
                MB_OK | MB_ICONINFORMATION);
    return 0;
}
