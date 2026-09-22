#include <windows.h>

#include "ui.h"
#include "../common/log.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPTSTR, int showCommand)
{
    MSG message;

    ReviveLog("APP", "ReviveCE Mail M4 starting", 0);
    if (!RegisterReviveWindowClass(instance))
    {
        const DWORD nativeError = GetLastError();
        if (nativeError != ERROR_CLASS_ALREADY_EXISTS)
        {
            ReviveLog("APP", "window class registration failed", nativeError);
            return 1;
        }
    }

    if (CreateReviveMainWindow(instance, showCommand) == NULL)
    {
        ReviveLog("APP", "main window creation failed", GetLastError());
        return 2;
    }

    while (GetMessage(&message, NULL, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    ReviveLog("APP", "ReviveCE Mail stopped", 0);
    return static_cast<int>(message.wParam);
}
