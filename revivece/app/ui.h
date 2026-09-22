#ifndef REVIVECE_UI_H
#define REVIVECE_UI_H

#include <windows.h>

enum ReviveUiRow
{
    REVIVE_UI_DNS = 0,
    REVIVE_UI_TCP,
    REVIVE_UI_TLS,
    REVIVE_UI_CERTIFICATE,
    REVIVE_UI_HOSTNAME,
    REVIVE_UI_IMAP,
    REVIVE_UI_ROW_COUNT
};

enum ReviveUiState
{
    REVIVE_UI_NOT_RUN = 0,
    REVIVE_UI_RUNNING,
    REVIVE_UI_OK,
    REVIVE_UI_FAILED,
    REVIVE_UI_NOT_BUILT
};

struct ReviveUiStatusMessage
{
    ReviveUiRow row;
    ReviveUiState state;
    int nativeError;
};

struct ReviveUiInboxMessage
{
    bool unread;
    wchar_t sender[160];
    wchar_t subject[192];
    wchar_t date[80];
};

ATOM RegisterReviveWindowClass(HINSTANCE instance);
HWND CreateReviveMainWindow(HINSTANCE instance, int showCommand);
LRESULT CALLBACK ReviveWindowProc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam);

#endif
