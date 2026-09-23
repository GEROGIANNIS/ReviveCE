#include "../mail/imap.h"
#include "../mail/smtp.h"
#include "../net/socket.h"
#include "../net/tls.h"
#include "ui.h"

#include "resource.h"
#include "../common/log.h"

namespace
{
const wchar_t* const kWindowClass = L"ReviveTLSWindow";
const wchar_t* const kReaderWindowClass = L"ReviveCEReaderWindow";
const wchar_t* const kComposeWindowClass = L"ReviveCEComposeWindow";
const int kReaderLoadMoreControl = 2001;
const int kComposeSendControl = 2002;
const char* const kImapServerHost = "imap.gmail.com";
const unsigned short kImapServerPort = 993;
const char* const kSmtpServerHost = "smtp.gmail.com";
const unsigned short kSmtpServerPort = 465;
const DWORD kConnectTimeoutMilliseconds = 15000;
const wchar_t* const kCABundleFileName = L"google-roots.pem";

enum WorkerMode { WORKER_TLS_TEST = 0, WORKER_INBOX_REFRESH, WORKER_MESSAGE_FETCH,
                  WORKER_SMTP_SEND };
struct WorkerRequest
{
    HWND window;
    WorkerMode mode;
    unsigned long uid;
    unsigned long bodyBytes;
    ReviveImapCredentials credentials;
    ReviveSmtpMessage outgoing;
};

HWND g_statusControls[REVIVE_UI_ROW_COUNT];
HWND g_runButton = NULL;
HWND g_refreshButton = NULL;
HWND g_openButton = NULL;
HWND g_composeButton = NULL;
HWND g_emailEdit = NULL;
HWND g_passwordEdit = NULL;
HWND g_inbox = NULL;
HANDLE g_workerThread = NULL;
HWND g_readerWindow = NULL;
HWND g_readerBody = NULL;
HWND g_readerDetail = NULL;
HWND g_readerLoadMore = NULL;
HWND g_mainWindow = NULL;
unsigned long g_readerUid = 0;
unsigned long g_readerDisplayedBytes = 0;
bool g_readerHasMore = false;
HWND g_composeWindow = NULL;
HWND g_composeRecipient = NULL;
HWND g_composeSubject = NULL;
HWND g_composeBody = NULL;
HWND g_composeSend = NULL;
HWND g_composeStatus = NULL;
ReviveImapCredentials g_sessionCredentials;
bool g_inboxCanOpen = false;

void ClearBytes(void* value, unsigned int length)
{
    volatile unsigned char* cursor = static_cast<volatile unsigned char*>(value);
    while (length-- != 0)
        *cursor++ = 0;
}

const wchar_t* RowName(ReviveUiRow row)
{
    static const wchar_t* const names[REVIVE_UI_ROW_COUNT] =
    { L"DNS", L"TCP", L"TLS 1.2", L"Certificate", L"Hostname", L"MAIL" };
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

const wchar_t* MailFailureName(int result)
{
    switch (result)
    {
    case REVIVE_IMAP_CONFIGURATION_ERROR: return L"SELECT A MESSAGE";
    case REVIVE_IMAP_IO_ERROR: return L"NETWORK READ FAILED";
    case REVIVE_IMAP_GREETING_ERROR: return L"SERVER GREETING FAILED";
    case REVIVE_IMAP_AUTHENTICATION_ERROR: return L"APP PASSWORD REJECTED";
    case REVIVE_IMAP_SELECT_ERROR: return L"INBOX SELECT FAILED";
    case REVIVE_IMAP_SEARCH_ERROR: return L"INBOX SEARCH FAILED";
    case REVIVE_IMAP_FETCH_ERROR: return L"MESSAGE FETCH FAILED";
    case REVIVE_IMAP_RESPONSE_TOO_LARGE: return L"SERVER RESPONSE TOO LARGE";
    case REVIVE_IMAP_BODY_TOO_LARGE: return L"MESSAGE TEXT TOO LARGE";
    case REVIVE_IMAP_UNSUPPORTED_MESSAGE: return L"MESSAGE FORMAT NOT SUPPORTED";
    case REVIVE_SMTP_CONFIGURATION_ERROR: return L"CHECK RECIPIENT OR TEXT";
    case REVIVE_SMTP_IO_ERROR: return L"NETWORK WRITE FAILED";
    case REVIVE_SMTP_GREETING_ERROR: return L"SMTP GREETING FAILED";
    case REVIVE_SMTP_EHLO_ERROR: return L"SMTP EHLO FAILED";
    case REVIVE_SMTP_AUTHENTICATION_ERROR: return L"APP PASSWORD REJECTED";
    case REVIVE_SMTP_SENDER_ERROR: return L"SENDER REJECTED";
    case REVIVE_SMTP_RECIPIENT_ERROR: return L"RECIPIENT REJECTED";
    case REVIVE_SMTP_DATA_ERROR: return L"SMTP DATA FAILED";
    case REVIVE_SMTP_MESSAGE_ERROR: return L"MESSAGE REJECTED";
    case REVIVE_SMTP_RESPONSE_TOO_LARGE: return L"SMTP RESPONSE TOO LARGE";
    default: return L"MAIL FAILED";
    }
}

void SetRow(ReviveUiRow row, ReviveUiState state, int nativeError)
{
    wchar_t text[160];
    if (row < REVIVE_UI_DNS || row >= REVIVE_UI_ROW_COUNT)
        return;
    if (row == REVIVE_UI_IMAP && state == REVIVE_UI_FAILED && nativeError > 0)
        wsprintf(text, L"%s  %s (%d)", RowName(row),
                 MailFailureName(nativeError), nativeError);
    else if (nativeError != 0)
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
                     const char* body, bool usedHtmlFallback,
                     unsigned long displayedBytes, bool hasMore)
{
    ReviveUiMessageBody* copied = static_cast<ReviveUiMessageBody*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiMessageBody)));
    if (copied == NULL)
        return;
    copied->uid = message->uid;
    copied->displayedBytes = displayedBytes;
    copied->hasMore = hasMore;
    CopyUtf8ToWide(copied->sender, sizeof(copied->sender) / sizeof(wchar_t), message->sender);
    CopyUtf8ToWide(copied->subject, sizeof(copied->subject) / sizeof(wchar_t), message->subject);
    CopyUtf8ToWide(copied->date, sizeof(copied->date) / sizeof(wchar_t), message->date);
    CopyUtf8ToWide(copied->body, sizeof(copied->body) / sizeof(wchar_t), body);
    copied->usedHtmlFallback = usedHtmlFallback;
    if (!PostMessage(window, WM_REVIVE_MESSAGE_BODY, 0, reinterpret_cast<LPARAM>(copied)))
        HeapFree(GetProcessHeap(), 0, copied);
}

void PostSendResult(HWND window, ReviveSmtpResult result)
{
    PostMessage(window, WM_REVIVE_SEND_RESULT,
                static_cast<WPARAM>(result), 0);
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
    const bool sending = request->mode == WORKER_SMTP_SEND;
    ReviveSmtpResult sendResult = REVIVE_SMTP_IO_ERROR;
    const char* serverHost = sending ? kSmtpServerHost : kImapServerHost;
    const unsigned short serverPort = sending ? kSmtpServerPort : kImapServerPort;
    const bool connected = ReviveNetConnect(serverHost, serverPort,
        kConnectTimeoutMilliseconds, &connection, NetworkProgress, window);
    if (connected && ReviveTLSIsAvailable())
    {
        ReviveTlsResult tlsResult = REVIVE_TLS_CONFIGURATION_ERROR;
        PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_RUNNING, 0);
        PostStatus(window, REVIVE_UI_CERTIFICATE, REVIVE_UI_RUNNING, 0);
        PostStatus(window, REVIVE_UI_HOSTNAME, REVIVE_UI_RUNNING, 0);
        if (BuildCABundlePath(caBundlePath, MAX_PATH))
            tlsResult = ReviveTLSConnect(&connection, serverHost, caBundlePath, &tlsConnection);
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
                {
                    ReviveLog("IMAP", "message fetch failed",
                              static_cast<int>(imapResult));
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(imapResult));
                }
                ClearBytes(messages, sizeof(messages));
            }
            else if (request->mode == WORKER_MESSAGE_FETCH)
            {
                ReviveImapMessage header;
                char* body = static_cast<char*>(HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, request->bodyBytes + 2));
                bool usedHtmlFallback = false;
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                ReviveImapResult imapResult = REVIVE_IMAP_CONFIGURATION_ERROR;
                bool hasMore = false;
                if (body != NULL)
                    imapResult = ReviveImapFetchMessage(tlsConnection,
                        &request->credentials, request->uid, request->bodyBytes,
                        &header, body, static_cast<int>(request->bodyBytes + 2),
                        &usedHtmlFallback, &hasMore);
                if (imapResult == REVIVE_IMAP_OK)
                {
                    PostMessageBody(window, &header, body, usedHtmlFallback,
                                    request->bodyBytes, hasMore);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                {
                    ReviveLog("IMAP", "message fetch failed",
                              static_cast<int>(imapResult));
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(imapResult));
                }
                ClearBytes(&header, sizeof(header));
                if (body != NULL)
                {
                    ClearBytes(body, request->bodyBytes + 2);
                    HeapFree(GetProcessHeap(), 0, body);
                }
            }
            else
            {
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                sendResult = ReviveSmtpSendMessage(
                    tlsConnection, &request->credentials, &request->outgoing);
                if (sendResult == REVIVE_SMTP_OK)
                {
                    ReviveLog("SMTP", "plain-text message accepted", 0);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                {
                    ReviveLog("SMTP", "message send failed", static_cast<int>(sendResult));
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(sendResult));
                }
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
    ReviveSmtpClearMessage(&request->outgoing);
    ClearBytes(request, sizeof(*request));
    HeapFree(GetProcessHeap(), 0, request);
    if (sending)
        PostSendResult(window, sendResult);
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
    wchar_t* wide;
    int copied;
    bool succeeded;
    if (edit == NULL || destination == NULL || capacity < 2)
        return false;
    wide = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                            capacity * sizeof(wchar_t)));
    if (wide == NULL)
        return false;
    copied = GetWindowText(edit, wide, capacity);
    succeeded = copied > 0 && WideCharToMultiByte(CP_UTF8, 0, wide, -1,
        destination, capacity, NULL, NULL) != 0;
    ClearBytes(wide, capacity * sizeof(wchar_t));
    HeapFree(GetProcessHeap(), 0, wide);
    return succeeded;
}

void BeginWorker(HWND window, WorkerMode mode, unsigned long uid,
                 unsigned long bodyBytes)
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
    request->bodyBytes = bodyBytes;
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
    if (mode == WORKER_MESSAGE_FETCH || mode == WORKER_SMTP_SEND)
    {
        if ((mode == WORKER_MESSAGE_FETCH && (uid == 0 || bodyBytes == 0 ||
            bodyBytes > REVIVE_IMAP_BODY_CAPACITY)) ||
            g_sessionCredentials.email[0] == '\0' ||
            g_sessionCredentials.appPassword[0] == '\0')
        {
            HeapFree(GetProcessHeap(), 0, request);
            SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_IMAP_CONFIGURATION_ERROR);
            return;
        }
        CopyMemory(&request->credentials, &g_sessionCredentials,
                   sizeof(request->credentials));
    }
    if (mode == WORKER_SMTP_SEND &&
        (!CopyEditUtf8(g_composeRecipient, request->outgoing.recipient,
                       sizeof(request->outgoing.recipient)) ||
         !CopyEditUtf8(g_composeSubject, request->outgoing.subject,
                       sizeof(request->outgoing.subject)) ||
         !CopyEditUtf8(g_composeBody, request->outgoing.body,
                       sizeof(request->outgoing.body))))
    {
        ReviveImapClearCredentials(&request->credentials);
        ReviveSmtpClearMessage(&request->outgoing);
        HeapFree(GetProcessHeap(), 0, request);
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_SMTP_CONFIGURATION_ERROR);
        return;
    }
    if (mode == WORKER_INBOX_REFRESH)
    {
        SendMessage(g_inbox, LB_RESETCONTENT, 0, 0);
        SetWindowText(g_passwordEdit, L"");
        g_inboxCanOpen = false;
        EnableWindow(g_openButton, FALSE);
    }
    ResetRows();
    EnableWindow(g_runButton, FALSE);
    EnableWindow(g_refreshButton, FALSE);
    EnableWindow(g_openButton, FALSE);
    EnableWindow(g_composeButton, FALSE);
    g_workerThread = CreateThread(NULL, 0, NetworkWorker, request, 0, &threadId);
    if (g_workerThread == NULL)
    {
        const int nativeError = GetLastError();
        ReviveImapClearCredentials(&request->credentials);
        HeapFree(GetProcessHeap(), 0, request);
        SetRow(REVIVE_UI_DNS, REVIVE_UI_FAILED, nativeError);
        EnableWindow(g_runButton, TRUE);
        EnableWindow(g_refreshButton, TRUE);
        EnableWindow(g_openButton, g_inboxCanOpen ? TRUE : FALSE);
        EnableWindow(g_composeButton, TRUE);
        if (g_readerLoadMore != NULL)
            EnableWindow(g_readerLoadMore, g_readerHasMore ? TRUE : FALSE);
    }
}

LRESULT CALLBACK ComposeWindowProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        RECT client;
        HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
        const int margin = 12;
        const int rowHeight = 24;
        const int buttonTop = margin + (rowHeight + 4) * 3;
        const int bodyTop = buttonTop + rowHeight + 4;
        int buttonWidth;
        int bodyHeight;
        GetClientRect(window, &client);
        buttonWidth = (client.right - 3 * margin) / 2;
        HWND recipientLabel = CreateWindow(L"STATIC", L"To:", WS_CHILD | WS_VISIBLE,
            margin, margin, 42, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(recipientLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeRecipient = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_TABSTOP | ES_AUTOHSCROLL, margin + 42, margin,
            client.right - 2 * margin - 42, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeRecipient, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND subjectLabel = CreateWindow(L"STATIC", L"Subject:", WS_CHILD | WS_VISIBLE,
            margin, margin + rowHeight + 4, 52, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(subjectLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeSubject = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_TABSTOP | ES_AUTOHSCROLL, margin + 52, margin + rowHeight + 4,
            client.right - 2 * margin - 52, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeSubject, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeStatus = CreateWindow(L"STATIC", L"Plain text only. ASCII subject.",
            WS_CHILD | WS_VISIBLE, margin, margin + (rowHeight + 4) * 2,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_composeStatus, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeSend = CreateWindow(L"BUTTON", L"SEND", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_DEFPUSHBUTTON, margin, buttonTop, buttonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(kComposeSendControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeSend, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            margin * 2 + buttonWidth, buttonTop, buttonWidth, rowHeight, window,
            reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        bodyHeight = client.bottom - bodyTop - margin;
        if (bodyHeight < rowHeight * 2)
            bodyHeight = rowHeight * 2;
        g_composeBody = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            margin, bodyTop, client.right - 2 * margin, bodyHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeBody, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(g_composeRecipient);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kComposeSendControl && HIWORD(wParam) == BN_CLICKED)
        {
            SetWindowText(g_composeStatus, L"SENDING...");
            EnableWindow(g_composeSend, FALSE);
            BeginWorker(g_mainWindow, WORKER_SMTP_SEND, 0, 0);
            if (g_workerThread == NULL)
            {
                SetWindowText(g_composeStatus, L"CHECK RECIPIENT OR TEXT");
                EnableWindow(g_composeSend, TRUE);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_composeWindow = NULL;
        g_composeRecipient = NULL;
        g_composeSubject = NULL;
        g_composeBody = NULL;
        g_composeSend = NULL;
        g_composeStatus = NULL;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

void OpenCompose()
{
    if (g_composeWindow != NULL)
    {
        ShowWindow(g_composeWindow, SW_SHOW);
        return;
    }
    if (g_sessionCredentials.email[0] == '\0' ||
        g_sessionCredentials.appPassword[0] == '\0')
    {
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_SMTP_CONFIGURATION_ERROR);
        return;
    }
    g_composeWindow = CreateWindow(kComposeWindowClass, L"ReviveCE Compose",
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL,
        GetModuleHandle(NULL), NULL);
    if (g_composeWindow != NULL)
    {
        ShowWindow(g_composeWindow, SW_SHOW);
        UpdateWindow(g_composeWindow);
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
    BeginWorker(window, WORKER_MESSAGE_FETCH, static_cast<unsigned long>(itemData),
                REVIVE_IMAP_INITIAL_BODY_BYTES);
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
    {
        SendMessage(g_inbox, LB_SETITEMDATA, item, message->uid);
        g_inboxCanOpen = true;
        EnableWindow(g_openButton, TRUE);
    }
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
        g_readerDetail = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            margin, margin + 2 * rowHeight, client.right - 2 * margin, rowHeight,
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_readerDetail, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerBody = CreateWindow(L"EDIT", content->body,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL, margin, margin + 3 * rowHeight,
            client.right - 2 * margin, client.bottom - (5 * rowHeight + 2 * margin),
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_readerBody, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerLoadMore = CreateWindow(L"BUTTON", L"LOAD MORE",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, margin,
            client.bottom - (rowHeight + margin), (client.right - 3 * margin) / 2,
            rowHeight, window, reinterpret_cast<HMENU>(kReaderLoadMoreControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_readerLoadMore, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            margin * 2 + (client.right - 3 * margin) / 2,
            client.bottom - (rowHeight + margin), (client.right - 3 * margin) / 2,
            rowHeight, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerUid = content->uid;
        g_readerDisplayedBytes = content->displayedBytes;
        g_readerHasMore = content->hasMore;
        wchar_t notice[160];
        wsprintf(notice, L"%s%s%s", content->date,
                 content->usedHtmlFallback ? L"  (HTML converted to text)" : L"",
                 content->hasMore ? L"  (more available)" : L"");
        SetWindowText(g_readerDetail, notice);
        EnableWindow(g_readerLoadMore, content->hasMore ? TRUE : FALSE);
        SetFocus(backButton);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kReaderLoadMoreControl && HIWORD(wParam) == BN_CLICKED &&
            g_readerHasMore && g_readerDisplayedBytes < REVIVE_IMAP_BODY_CAPACITY)
        {
            unsigned long nextBytes = g_readerDisplayedBytes + REVIVE_IMAP_BODY_PAGE_BYTES;
            if (nextBytes > REVIVE_IMAP_BODY_CAPACITY)
                nextBytes = REVIVE_IMAP_BODY_CAPACITY;
            EnableWindow(g_readerLoadMore, FALSE);
            BeginWorker(g_mainWindow, WORKER_MESSAGE_FETCH, g_readerUid, nextBytes);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_readerWindow = NULL;
        g_readerBody = NULL;
        g_readerDetail = NULL;
        g_readerLoadMore = NULL;
        g_readerUid = 0;
        g_readerDisplayedBytes = 0;
        g_readerHasMore = false;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

void ShowMessageReader(ReviveUiMessageBody* content)
{
    if (content == NULL)
        return;
    if (g_readerWindow != NULL && content->uid == g_readerUid)
    {
        wchar_t notice[160];
        SetWindowText(g_readerBody, content->body);
        g_readerDisplayedBytes = content->displayedBytes;
        g_readerHasMore = content->hasMore;
        wsprintf(notice, L"%s%s%s", content->date,
                 content->usedHtmlFallback ? L"  (HTML converted to text)" : L"",
                 content->hasMore ? L"  (more available)" : L"");
        SetWindowText(g_readerDetail, notice);
        EnableWindow(g_readerLoadMore, content->hasMore ? TRUE : FALSE);
        HeapFree(GetProcessHeap(), 0, content);
        return;
    }
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
    HWND title = CreateWindow(L"STATIC", L"ReviveCE Mail (M6)", WS_CHILD | WS_VISIBLE | SS_CENTER,
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
    const int buttonWidth = (width - 3 * margin) / 2;
    const int buttonHeight = rowHeight + 5;
    g_runButton = CreateWindow(L"BUTTON", L"TEST TLS", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        margin, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_RUN_TEST), GetModuleHandle(NULL), NULL);
    SendMessage(g_runButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_refreshButton = CreateWindow(L"BUTTON", L"REFRESH INBOX", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        margin * 2 + buttonWidth, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_REFRESH_INBOX), GetModuleHandle(NULL), NULL);
    SendMessage(g_refreshButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += buttonHeight + 5;
    g_openButton = CreateWindow(L"BUTTON", L"OPEN", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        margin, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_OPEN_MESSAGE), GetModuleHandle(NULL), NULL);
    SendMessage(g_openButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_composeButton = CreateWindow(L"BUTTON", L"COMPOSE", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        margin * 2 + buttonWidth, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_COMPOSE), GetModuleHandle(NULL), NULL);
    SendMessage(g_composeButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += buttonHeight + margin / 2;
    HWND inboxLabel = CreateWindow(L"STATIC", L"REFRESH loads mail. Select a row, then OPEN.", WS_CHILD | WS_VISIBLE,
        margin, top, width - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(inboxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight;
    g_inbox = CreateWindow(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
        margin, top, width - 2 * margin, client.bottom - top - margin, window,
        reinterpret_cast<HMENU>(IDC_INBOX), GetModuleHandle(NULL), NULL);
    SendMessage(g_inbox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnableWindow(g_openButton, FALSE);
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
    windowClass.lpfnWndProc = ComposeWindowProc;
    windowClass.lpszClassName = kComposeWindowClass;
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
    case WM_CREATE:
        g_mainWindow = window;
        CreateChildControls(window);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RUN_TEST && HIWORD(wParam) == BN_CLICKED)
        { BeginWorker(window, WORKER_TLS_TEST, 0, 0); return 0; }
        if (LOWORD(wParam) == IDC_REFRESH_INBOX && HIWORD(wParam) == BN_CLICKED)
        { BeginWorker(window, WORKER_INBOX_REFRESH, 0, 0); return 0; }
        if (LOWORD(wParam) == IDC_OPEN_MESSAGE && HIWORD(wParam) == BN_CLICKED)
        { OpenSelectedMessage(window); return 0; }
        if (LOWORD(wParam) == IDC_COMPOSE && HIWORD(wParam) == BN_CLICKED)
        { OpenCompose(); return 0; }
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
    case WM_REVIVE_SEND_RESULT:
        if (g_composeStatus != NULL)
            SetWindowText(g_composeStatus, wParam == REVIVE_SMTP_OK ?
                          L"SENT. Gmail accepted the message." :
                          MailFailureName(static_cast<int>(wParam)));
        return 0;
    case WM_REVIVE_TEST_COMPLETE:
        if (g_workerThread != NULL)
        {
            CloseHandle(g_workerThread);
            g_workerThread = NULL;
        }
        EnableWindow(g_runButton, TRUE);
        EnableWindow(g_refreshButton, TRUE);
        EnableWindow(g_openButton, g_inboxCanOpen ? TRUE : FALSE);
        EnableWindow(g_composeButton, TRUE);
        if (g_composeSend != NULL)
            EnableWindow(g_composeSend, TRUE);
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
        if (g_composeWindow != NULL)
            DestroyWindow(g_composeWindow);
        g_mainWindow = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}
