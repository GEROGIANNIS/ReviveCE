#ifndef REVIVECE_UI_H
#define REVIVECE_UI_H

#include "../mail/imap.h"
#include "../net/http.h"
#include "../feeds/feed.h"

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
    unsigned long uid;
    bool unread;
    wchar_t sender[160];
    wchar_t subject[192];
    wchar_t date[80];
};

struct ReviveUiMessageBody
{
    unsigned long uid;
    unsigned long displayedBytes;
    bool hasMore;
    wchar_t sender[160];
    wchar_t subject[192];
    wchar_t date[80];
    wchar_t body[REVIVE_IMAP_BODY_CAPACITY + 2];
    bool usedHtmlFallback;
};

struct ReviveUiHttpResponse
{
    int result;
    int status;
    bool truncated;
    wchar_t body[REVIVE_HTTP_RESPONSE_CAPACITY + 1];
};

struct ReviveUiFeedItem
{
    wchar_t title[REVIVE_FEED_TITLE_CAPACITY];
    wchar_t link[REVIVE_FEED_LINK_CAPACITY];
    wchar_t date[REVIVE_FEED_DATE_CAPACITY];
};

struct ReviveUiFeedResult
{
    int result;
    int httpStatus;
    int itemCount;
    bool atomFormat;
};

ATOM RegisterReviveWindowClass(HINSTANCE instance);
HWND CreateReviveMainWindow(HINSTANCE instance, int showCommand);
LRESULT CALLBACK ReviveWindowProc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam);

#endif
