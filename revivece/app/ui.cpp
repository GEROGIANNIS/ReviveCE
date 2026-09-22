#include "../mail/imap.h"
#include "../net/socket.h"
#include "../net/tls.h"
#include "ui.h"

#include "resource.h"
#include "../common/log.h"

namespace
{
const wchar_t* const kWindowClass = L"ReviveTLSWindow";
const wchar_t* const kReaderWindowClass = L"ReviveCEReaderWindow";
const char* const kServerHost = "imap.gmail.com";
const unsigned short kServerPort = 993;
const DWORD kConnectTimeoutMilliseconds = 15000;
const wchar_t* const kCABundleFileName = L"google-roots.pem";

enum WorkerMode { WORKER_TLS_TEST = 0, WORKER_INBOX_REFRESH, WORKER_MESSAGE_FETCH };
struct WorkerRequest
{
    HWND window;
    WorkerMode mode;
    unsigned long uid;
    ReviveImapCredentials credentials;
};

HWND g_statusControls[REVIVE_UI_ROW_COUNT];
HWND g_runButton = NULL;
HWND g_refreshButton = NULL;
HWND g_openButton = NULL;
HWND g_emailEdit = NULL;
HWND g_passwordEdit = NULL;
HWND g_inbox = NULL;
HANDLE g_workerThread = NULL;
HWND g_readerWindow = NULL;
ReviveImapCredentials g_sessionCredentials;

void ClearBytes(void* value, unsigned int length)
{
    volatile unsigned char* cursor = static_cast<volatile unsigned char*>(value);
    while (length-- != 0)
        *cursor++ = 0;
}

const wchar_t* RowName(ReviveUiRow row)
{
    static const wchar_t* const names[REVIVE_UI_ROW_COUNT] =
    { L"DNS", L"TCP", L"TLS 1.2", L"Certificate", L"Hostname", L"IMAP" };
    return names[row];
}

bool BuildCABundlePath(wchar_t* path, DWORD capacity)
{
    DWORD length;
    DWORD separator = 0;
    DWORD nameLength = 0;
    if (path == NULL || capacity == 0)
        return false;
    length = GetModuleFileName(NULL, path, capacity);
    if (length == 0 || length >= capacity)
        return false;
    for (DWORD index = 0; index < length; ++index)
        if (path[index] == L'\\' || path[index] == L'/')
            separator = index + 1;
    while (kCABundleFileName[nameLength] != L'\0')
        ++nameLength;
    if (separator + nameLength + 1 > capacity)
        return false;
    for (DWORD index = 0; index <= nameLength; ++index)
        path[separator + index] = kCABundleFileName[index];
    return true;
}

const wchar_t* StateName(ReviveUiState state)
{
    switch (state)
    {
    case REVIVE_UI_RUNNING: return L"TESTING...";
    case REVIVE_UI_OK: return L"OK";
    case REVIVE_UI_FAILED: return L"FAILED";
    case REVIVE_UI_NOT_BUILT: return L"NOT BUILT";
    default: return L"NOT RUN";
    }
}

void SetRow(ReviveUiRow row, ReviveUiState state, int nativeError)
{
    wchar_t text[96];
    if (row < REVIVE_UI_DNS || row >= REVIVE_UI_ROW_COUNT)
        return;
    if (nativeError != 0)
        wsprintf(text, L"%s  ..........  %s (%d)", RowName(row),
                 StateName(state), nativeError);
    else
        wsprintf(text, L"%s  ..........  %s", RowName(row), StateName(state));
    SetWindowText(g_statusControls[row], text);
}

void PostStatus(HWND window, ReviveUiRow row, ReviveUiState state, int nativeError)
{
    ReviveUiStatusMessage* status = static_cast<ReviveUiStatusMessage*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiStatusMessage)));
    if (status == NULL)
        return;
    status->row = row;
    status->state = state;
    status->nativeError = nativeError;
    if (!PostMessage(window, WM_REVIVE_STATUS, 0, reinterpret_cast<LPARAM>(status)))
        HeapFree(GetProcessHeap(), 0, status);
}

void NetworkProgress(ReviveNetStage stage, ReviveNetState state,
                     int nativeError, void* context)
{
    ReviveUiState uiState = state == REVIVE_NET_SUCCEEDED ? REVIVE_UI_OK :
                            state == REVIVE_NET_FAILED ? REVIVE_UI_FAILED :
                            REVIVE_UI_RUNNING;
    PostStatus(static_cast<HWND>(context), stage == REVIVE_NET_DNS ?
               REVIVE_UI_DNS : REVIVE_UI_TCP, uiState, nativeError);
}

void CopyUtf8ToWide(wchar_t* destination, int capacity, const char* source)
{
    if (destination == NULL || capacity <= 0)
        return;
    destination[0] = L'\0';
    if (source != NULL && source[0] != '\0')
        MultiByteToWideChar(CP_UTF8, 0, source, -1, destination, capacity);
}

void PostInboxMessage(HWND window, const ReviveImapMessage* message)
{
    ReviveUiInboxMessage* copied = static_cast<ReviveUiInboxMessage*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiInboxMessage)));
    if (copied == NULL)
        return;
    copied->uid = message->uid;
    copied->unread = message->unread;
    CopyUtf8ToWide(copied->sender, sizeof(copied->sender) / sizeof(wchar_t), message->sender);
    CopyUtf8ToWide(copied->subject, sizeof(copied->subject) / sizeof(wchar_t), message->subject);
    CopyUtf8ToWide(copied->date, sizeof(copied->date) / sizeof(wchar_t), message->date);
    if (!PostMessage(window, WM_REVIVE_INBOX_MESSAGE, 0, reinterpret_cast<LPARAM>(copied)))
        HeapFree(GetProcessHeap(), 0, copied);
}

void PostMessageBody(HWND window, const ReviveImapMessage* message,
                     const char* body, bool usedHtmlFallback)
{
    ReviveUiMessageBody* copied = static_cast<ReviveUiMessageBody*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiMessageBody)));
    if (copied == NULL)
        return;
    CopyUtf8ToWide(copied->sender, sizeof(copied->sender) / sizeof(wchar_t), message->sender);
    CopyUtf8ToWide(copied->subject, sizeof(copied->subject) / sizeof(wchar_t), message->subject);
    CopyUtf8ToWide(copied->date, sizeof(copied->date) / sizeof(wchar_t), message->date);
    CopyUtf8ToWide(copied->body, sizeof(copied->body) / sizeof(wchar_t), body);
    copied->usedHtmlFallback = usedHtmlFallback;
    if (!PostMessage(window, WM_REVIVE_MESSAGE_BODY, 0, reinterpret_cast<LPARAM>(copied)))
        HeapFree(GetProcessHeap(), 0, copied);
}

void PostTlsSuccess(HWND window)
{
    PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_OK, 0);
    PostStatus(window, REVIVE_UI_CERTIFICATE, REVIVE_UI_OK, 0);
    PostStatus(window, REVIVE_UI_HOSTNAME, REVIVE_UI_OK, 0);
}

void PostTlsFailure(HWND window, ReviveTlsResult result)
{
    PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_FAILED,
               result == REVIVE_TLS_CERTIFICATE_ERROR || result == REVIVE_TLS_HOSTNAME_ERROR ?
                   0 : static_cast<int>(result));
    PostStatus(window, REVIVE_UI_CERTIFICATE,
               result == REVIVE_TLS_CERTIFICATE_ERROR ? REVIVE_UI_FAILED :
               result == REVIVE_TLS_HOSTNAME_ERROR ? REVIVE_UI_OK : REVIVE_UI_NOT_RUN, 0);
    PostStatus(window, REVIVE_UI_HOSTNAME,
               result == REVIVE_TLS_HOSTNAME_ERROR ? REVIVE_UI_FAILED : REVIVE_UI_NOT_RUN, 0);
}

DWORD WINAPI NetworkWorker(void* context)
{
    WorkerRequest* request = static_cast<WorkerRequest*>(context);
    HWND window = request->window;
    ReviveNetConnection connection;
    ReviveTlsConnection* tlsConnection = NULL;
    wchar_t caBundlePath[MAX_PATH];
    bool succeeded = false;
    const bool connected = ReviveNetConnect(kServerHost, kServerPort,
        kConnectTimeoutMilliseconds, &connection, NetworkProgress, window);
    if (connected && ReviveTLSIsAvailable())
    {
        ReviveTlsResult tlsResult = REVIVE_TLS_CONFIGURATION_ERROR;
        PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_RUNNING, 0);
        PostStatus(window, REVIVE_UI_CERTIFICATE, REVIVE_UI_RUNNING, 0);
        PostStatus(window, REVIVE_UI_HOSTNAME, REVIVE_UI_RUNNING, 0);
        if (BuildCABundlePath(caBundlePath, MAX_PATH))
            tlsResult = ReviveTLSConnect(&connection, kServerHost, caBundlePath, &tlsConnection);
        if (tlsResult == REVIVE_TLS_OK)
        {
            PostTlsSuccess(window);
            if (request->mode == WORKER_TLS_TEST)
            {
                char greeting[256];
                if (ReviveTLSRead(tlsConnection, greeting, sizeof(greeting)) > 0)
                {
                    ReviveLog("IMAP", "encrypted server greeting received", 0);
                    succeeded = true;
                }
                else
                    PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_FAILED, 0);
            }
            else if (request->mode == WORKER_INBOX_REFRESH)
            {
                ReviveImapMessage messages[REVIVE_IMAP_MAX_MESSAGES];
                int messageCount = 0;
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                const ReviveImapResult imapResult = ReviveImapFetchInbox(
                    tlsConnection, &request->credentials, messages,
                    REVIVE_IMAP_MAX_MESSAGES, &messageCount);
                if (imapResult == REVIVE_IMAP_OK)
                {
                    for (int index = 0; index < messageCount; ++index)
                        PostInboxMessage(window, &messages[index]);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(imapResult));
                ClearBytes(messages, sizeof(messages));
            }
            else
            {
                ReviveImapMessage header;
                char body[REVIVE_IMAP_BODY_CAPACITY];
                bool usedHtmlFallback = false;
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                const ReviveImapResult imapResult = ReviveImapFetchMessage(
                    tlsConnection, &request->credentials, request->uid, &header,
                    body, sizeof(body), &usedHtmlFallback);
                if (imapResult == REVIVE_IMAP_OK)
                {
                    PostMessageBody(window, &header, body, usedHtmlFallback);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(imapResult));
                ClearBytes(&header, sizeof(header));
                ClearBytes(body, sizeof(body));
            }
        }
        else
            PostTlsFailure(window, tlsResult);
    }
    else if (connected)
    {
        PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_NOT_BUILT, 0);
        PostStatus(window, REVIVE_UI_CERTIFICATE, REVIVE_UI_NOT_BUILT, 0);
        PostStatus(window, REVIVE_UI_HOSTNAME, REVIVE_UI_NOT_BUILT, 0);
    }
    if (tlsConnection != NULL)
        ReviveTLSClose(tlsConnection);
    if (connected)
        ReviveNetClose(&connection);
    ReviveImapClearCredentials(&request->credentials);
    ClearBytes(request, sizeof(*request));
    HeapFree(GetProcessHeap(), 0, request);
    PostMessage(window, WM_REVIVE_TEST_COMPLETE, succeeded ? TRUE : FALSE, 0);
    return 0;
}

void ResetRows()
{
    SetRow(REVIVE_UI_DNS, REVIVE_UI_NOT_RUN, 0);
    SetRow(REVIVE_UI_TCP, REVIVE_UI_NOT_RUN, 0);
    SetRow(REVIVE_UI_TLS, ReviveTLSIsAvailable() ? REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT, 0);
    SetRow(REVIVE_UI_CERTIFICATE, ReviveTLSIsAvailable() ? REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT, 0);
    SetRow(REVIVE_UI_HOSTNAME, ReviveTLSIsAvailable() ? REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT, 0);
    SetRow(REVIVE_UI_IMAP, REVIVE_UI_NOT_RUN, 0);
}

bool CopyEditUtf8(HWND edit, char* destination, int capacity)
{
    wchar_t wide[REVIVE_IMAP_PASSWORD_CAPACITY];
    const int copied = GetWindowText(edit, wide, sizeof(wide) / sizeof(wchar_t));
    bool succeeded = copied > 0 && WideCharToMultiByte(CP_UTF8, 0, wide, -1,
        destination, capacity, NULL, NULL) != 0;
    ClearBytes(wide, sizeof(wide));
    return succeeded;
}

void BeginWorker(HWND window, WorkerMode mode, unsigned long uid)
{
    DWORD threadId = 0;
    if (g_workerThread != NULL)
        return;
    WorkerRequest* request = static_cast<WorkerRequest*>(HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WorkerRequest)));
    if (request == NULL)
        return;
    request->window = window;
    request->mode = mode;
    request->uid = uid;
    if (mode == WORKER_INBOX_REFRESH &&
        (!CopyEditUtf8(g_emailEdit, request->credentials.email, sizeof(request->credentials.email)) ||
         !CopyEditUtf8(g_passwordEdit, request->credentials.appPassword, sizeof(request->credentials.appPassword))))
    {
        ReviveImapClearCredentials(&request->credentials);
        HeapFree(GetProcessHeap(), 0, request);
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_IMAP_CONFIGURATION_ERROR);
        return;
    }
    if (mode == WORKER_INBOX_REFRESH)
        CopyMemory(&g_sessionCredentials, &request->credentials,
                   sizeof(g_sessionCredentials));
    if (mode == WORKER_MESSAGE_FETCH)
    {
        if (uid == 0 || g_sessionCredentials.email[0] == '\0' ||
            g_sessionCredentials.appPassword[0] == '\0')
        {
            HeapFree(GetProcessHeap(), 0, request);
            SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_IMAP_CONFIGURATION_ERROR);
            return;
        }
        CopyMemory(&request->credentials, &g_sessionCredentials,
                   sizeof(request->credentials));
    }
    if (mode == WORKER_INBOX_REFRESH)
    {
        SendMessage(g_inbox, LB_RESETCONTENT, 0, 0);
        SetWindowText(g_passwordEdit, L"");
    }
    ResetRows();
    EnableWindow(g_runButton, FALSE);
    EnableWindow(g_refreshButton, FALSE);
    EnableWindow(g_openButton, FALSE);
    g_workerThread = CreateThread(NULL, 0, NetworkWorker, request, 0, &threadId);
    if (g_workerThread == NULL)
    {
        const int nativeError = GetLastError();
        ReviveImapClearCredentials(&request->credentials);
        HeapFree(GetProcessHeap(), 0, request);
        SetRow(REVIVE_UI_DNS, REVIVE_UI_FAILED, nativeError);
        EnableWindow(g_runButton, TRUE);
        EnableWindow(g_refreshButton, TRUE);
        EnableWindow(g_openButton, TRUE);
    }
}

void OpenSelectedMessage(HWND window)
{
    const int selected = SendMessage(g_inbox, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR)
    {
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_IMAP_CONFIGURATION_ERROR);
        return;
    }
    const LRESULT itemData = SendMessage(g_inbox, LB_GETITEMDATA, selected, 0);
    if (itemData == LB_ERR || itemData == 0)
    {
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_IMAP_CONFIGURATION_ERROR);
        return;
    }
    BeginWorker(window, WORKER_MESSAGE_FETCH, static_cast<unsigned long>(itemData));
}

void AddInboxMessage(const ReviveUiInboxMessage* message)
{
    wchar_t text[512];
    const wchar_t* sender = message->sender[0] != L'\0' ? message->sender : L"(unknown sender)";
    const wchar_t* subject = message->subject[0] != L'\0' ? message->subject : L"(no subject)";
    const wchar_t* date = message->date[0] != L'\0' ? message->date : L"unknown date";
    wsprintf(text, L"%s%s - %s (%s)", message->unread ? L"* " : L"  ",
             sender, subject, date);
    const int item = SendMessage(g_inbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    if (item != LB_ERR && item != LB_ERRSPACE)
        SendMessage(g_inbox, LB_SETITEMDATA, item, message->uid);
}

LRESULT CALLBACK ReaderWindowProc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* created = reinterpret_cast<CREATESTRUCT*>(lParam);
        ReviveUiMessageBody* content =
            static_cast<ReviveUiMessageBody*>(created->lpCreateParams);
        RECT client;
        HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
        const int margin = 12;
        const int rowHeight = 24;
        wchar_t from[384];
        wchar_t notice[128];
        GetClientRect(window, &client);
        wsprintf(from, L"%s", content->sender[0] != L'\0' ? content->sender : L"(unknown sender)");
        HWND fromControl = CreateWindow(L"STATIC", from, WS_CHILD | WS_VISIBLE,
            margin, margin, client.right - 2 * margin, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(fromControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND subjectControl = CreateWindow(L"STATIC",
            content->subject[0] != L'\0' ? content->subject : L"(no subject)",
            WS_CHILD | WS_VISIBLE, margin, margin + rowHeight,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(subjectControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        wsprintf(notice, L"%s%s", content->date,
                 content->usedHtmlFallback ? L"  (HTML converted to text)" : L"");
        HWND detailControl = CreateWindow(L"STATIC", notice, WS_CHILD | WS_VISIBLE,
            margin, margin + 2 * rowHeight, client.right - 2 * margin, rowHeight,
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(detailControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND bodyControl = CreateWindow(L"EDIT", content->body,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL, margin, margin + 3 * rowHeight,
            client.right - 2 * margin, client.bottom - (5 * rowHeight + 2 * margin),
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(bodyControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            margin, client.bottom - (rowHeight + margin), client.right - 2 * margin,
            rowHeight, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(backButton);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_readerWindow = NULL;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

void ShowMessageReader(ReviveUiMessageBody* content)
{
    if (content == NULL)
        return;
    if (g_readerWindow != NULL)
        DestroyWindow(g_readerWindow);
    g_readerWindow = CreateWindow(kReaderWindowClass, L"ReviveCE Message",
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN), NULL, NULL, GetModuleHandle(NULL), content);
    if (g_readerWindow != NULL)
    {
        ShowWindow(g_readerWindow, SW_SHOW);
        UpdateWindow(g_readerWindow);
    }
    HeapFree(GetProcessHeap(), 0, content);
}

void CreateChildControls(HWND window)
{
    RECT client;
    HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int margin = width / 20;
    const int rowHeight = 25;
    int top = margin;
    HWND title = CreateWindow(L"STATIC", L"ReviveCE Mail (M5)", WS_CHILD | WS_VISIBLE | SS_CENTER,
        margin, top, width - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(title, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + margin / 2;
    for (int row = 0; row < REVIVE_UI_ROW_COUNT; ++row)
    {
        g_statusControls[row] = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin, top, width - 2 * margin, rowHeight, window,
            reinterpret_cast<HMENU>(IDC_STATUS_DNS + row), GetModuleHandle(NULL), NULL);
        SendMessage(g_statusControls[row], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetRow(static_cast<ReviveUiRow>(row), ReviveTLSIsAvailable() || row < REVIVE_UI_TLS ?
               REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT, 0);
        top += rowHeight;
    }
    HWND emailLabel = CreateWindow(L"STATIC", L"Gmail address:", WS_CHILD | WS_VISIBLE,
        margin, top, width / 3, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(emailLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_emailEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
        margin + width / 3, top, width - (2 * margin + width / 3), rowHeight, window,
        reinterpret_cast<HMENU>(IDC_EMAIL), GetModuleHandle(NULL), NULL);
    SendMessage(g_emailEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + 3;
    HWND passwordLabel = CreateWindow(L"STATIC", L"App password:", WS_CHILD | WS_VISIBLE,
        margin, top, width / 3, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(passwordLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_passwordEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
        margin + width / 3, top, width - (2 * margin + width / 3), rowHeight, window,
        reinterpret_cast<HMENU>(IDC_APP_PASSWORD), GetModuleHandle(NULL), NULL);
    SendMessage(g_passwordEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + margin / 2;
    const int buttonWidth = (width - 4 * margin) / 3;
    g_runButton = CreateWindow(L"BUTTON", L"TEST TLS", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        margin, top, buttonWidth, rowHeight + 5, window,
        reinterpret_cast<HMENU>(IDC_RUN_TEST), GetModuleHandle(NULL), NULL);
    SendMessage(g_runButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_refreshButton = CreateWindow(L"BUTTON", L"REFRESH INBOX", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        margin * 2 + buttonWidth, top, buttonWidth, rowHeight + 5, window,
        reinterpret_cast<HMENU>(IDC_REFRESH_INBOX), GetModuleHandle(NULL), NULL);
    SendMessage(g_refreshButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_openButton = CreateWindow(L"BUTTON", L"OPEN", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        margin * 3 + buttonWidth * 2, top, buttonWidth, rowHeight + 5, window,
        reinterpret_cast<HMENU>(IDC_OPEN_MESSAGE), GetModuleHandle(NULL), NULL);
    SendMessage(g_openButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + margin + 5;
    HWND inboxLabel = CreateWindow(L"STATIC", L"Newest 25 messages (* = unread; select then OPEN):", WS_CHILD | WS_VISIBLE,
        margin, top, width - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(inboxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight;
    g_inbox = CreateWindow(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
        margin, top, width - 2 * margin, client.bottom - top - margin, window,
        reinterpret_cast<HMENU>(IDC_INBOX), GetModuleHandle(NULL), NULL);
    SendMessage(g_inbox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}
}

ATOM RegisterReviveWindowClass(HINSTANCE instance)
{
    WNDCLASS windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = ReviveWindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    ATOM mainClass = RegisterClass(&windowClass);
    if (mainClass == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    if (mainClass == 0)
        mainClass = 1;

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = ReaderWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kReaderWindowClass;
    if (RegisterClass(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    return mainClass;
}

HWND CreateReviveMainWindow(HINSTANCE instance, int showCommand)
{
    HWND window = CreateWindow(kWindowClass, L"ReviveCE Mail", WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, instance, NULL);
    if (window != NULL)
    {
        ShowWindow(window, showCommand);
        UpdateWindow(window);
    }
    return window;
}

LRESULT CALLBACK ReviveWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE: CreateChildControls(window); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RUN_TEST && HIWORD(wParam) == BN_CLICKED)
        { BeginWorker(window, WORKER_TLS_TEST, 0); return 0; }
        if (LOWORD(wParam) == IDC_REFRESH_INBOX && HIWORD(wParam) == BN_CLICKED)
        { BeginWorker(window, WORKER_INBOX_REFRESH, 0); return 0; }
        if (LOWORD(wParam) == IDC_OPEN_MESSAGE && HIWORD(wParam) == BN_CLICKED)
        { OpenSelectedMessage(window); return 0; }
        if (LOWORD(wParam) == IDC_INBOX && HIWORD(wParam) == LBN_DBLCLK)
        { OpenSelectedMessage(window); return 0; }
        break;
    case WM_REVIVE_STATUS:
    {
        ReviveUiStatusMessage* status = reinterpret_cast<ReviveUiStatusMessage*>(lParam);
        if (status != NULL)
        {
            SetRow(status->row, status->state, status->nativeError);
            HeapFree(GetProcessHeap(), 0, status);
        }
        return 0;
    }
    case WM_REVIVE_INBOX_MESSAGE:
    {
        ReviveUiInboxMessage* inboxMessage = reinterpret_cast<ReviveUiInboxMessage*>(lParam);
        if (inboxMessage != NULL)
        {
            AddInboxMessage(inboxMessage);
            HeapFree(GetProcessHeap(), 0, inboxMessage);
        }
        return 0;
    }
    case WM_REVIVE_MESSAGE_BODY:
    {
        ReviveUiMessageBody* body = reinterpret_cast<ReviveUiMessageBody*>(lParam);
        ShowMessageReader(body);
        return 0;
    }
    case WM_REVIVE_TEST_COMPLETE:
        if (g_workerThread != NULL)
        {
            CloseHandle(g_workerThread);
            g_workerThread = NULL;
        }
        EnableWindow(g_runButton, TRUE);
        EnableWindow(g_refreshButton, TRUE);
        EnableWindow(g_openButton, TRUE);
        SetFocus(g_refreshButton);
        ReviveLog("APP", wParam ? "secure operation completed" : "secure operation failed", 0);
        return 0;
    case WM_CLOSE:
        if (g_workerThread == NULL)
            DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ReviveImapClearCredentials(&g_sessionCredentials);
        if (g_readerWindow != NULL)
            DestroyWindow(g_readerWindow);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}
