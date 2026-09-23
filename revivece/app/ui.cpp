#include "../mail/imap.h"
#include "../mail/smtp.h"
#include "../net/socket.h"
#include "../net/tls.h"
#include "../net/http.h"
#include "../feeds/feed.h"
#include "ui.h"

#include "resource.h"
#include "../common/log.h"

namespace
{
const wchar_t* const kWindowClass = L"ReviveTLSWindow";
const wchar_t* const kReaderWindowClass = L"ReviveCEReaderWindow";
const wchar_t* const kComposeWindowClass = L"ReviveCEComposeWindow";
const wchar_t* const kHttpWindowClass = L"ReviveCEHttpWindow";
const wchar_t* const kFeedWindowClass = L"ReviveCEFeedWindow";
const int kReaderLoadMoreControl = 2001;
const int kComposeSendControl = 2002;
const int kHttpFetchControl = 2003;
const int kFeedFetchControl = 2004;
const int kFeedSourceControl = 2005;
const int kReaderReplyControl = 2006;
const char* const kImapServerHost = "imap.gmail.com";
const unsigned short kImapServerPort = 993;
const char* const kSmtpServerHost = "smtp.gmail.com";
const unsigned short kSmtpServerPort = 465;
const DWORD kConnectTimeoutMilliseconds = 15000;
const wchar_t* const kCABundleFileName = L"google-roots.pem";
const COLORREF kShellColor = RGB(18, 28, 42);
const COLORREF kSurfaceColor = RGB(31, 47, 64);
const COLORREF kInputColor = RGB(242, 246, 247);
const COLORREF kAccentColor = RGB(52, 196, 183);
const COLORREF kTextColor = RGB(239, 247, 247);
const COLORREF kMutedTextColor = RGB(164, 186, 194);

enum WorkerMode { WORKER_TLS_TEST = 0, WORKER_INBOX_REFRESH, WORKER_MESSAGE_FETCH,
                  WORKER_SMTP_SEND, WORKER_HTTP_GET, WORKER_FEED_FETCH };
struct WorkerRequest
{
    HWND window;
    WorkerMode mode;
    unsigned long uid;
    unsigned long bodyBytes;
    ReviveImapCredentials credentials;
    ReviveSmtpMessage outgoing;
    ReviveHttpUrl httpUrl;
};

HWND g_statusControls[REVIVE_UI_ROW_COUNT];
HWND g_runButton = NULL;
HWND g_refreshButton = NULL;
HWND g_openButton = NULL;
HWND g_composeButton = NULL;
HWND g_webButton = NULL;
HWND g_feedsButton = NULL;
HWND g_emailEdit = NULL;
HWND g_passwordEdit = NULL;
HWND g_passwordToggle = NULL;
bool g_passwordVisible = false;
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
wchar_t g_replyRecipient[REVIVE_IMAP_SENDER_CAPACITY];
wchar_t g_replySubject[REVIVE_IMAP_SUBJECT_CAPACITY];
HWND g_composeWindow = NULL;
HWND g_composeRecipient = NULL;
HWND g_composeSubject = NULL;
HWND g_composeBody = NULL;
HWND g_composeSend = NULL;
HWND g_composeStatus = NULL;
HWND g_httpWindow = NULL;
HWND g_httpUrl = NULL;
HWND g_httpFetch = NULL;
HWND g_httpStatus = NULL;
HWND g_httpBody = NULL;
HWND g_feedWindow = NULL;
HWND g_feedUrl = NULL;
HWND g_feedSource = NULL;
HWND g_feedFetch = NULL;
HWND g_feedStatus = NULL;
HWND g_feedItems = NULL;

struct ReviveFeedSource
{
    const wchar_t* name;
    const wchar_t* url;
};

const ReviveFeedSource kFeedSources[] =
{
    { L"Hacker News Front Page", L"https://hnrss.org/frontpage" },
    { L"Hacker News Newest", L"https://hnrss.org/newest" },
    { L"Hacker News Best", L"https://hnrss.org/best" },
    { L"Hacker News Ask", L"https://hnrss.org/ask" },
    { L"Hacker News Show", L"https://hnrss.org/show" }
};
const int kFeedSourceCount = sizeof(kFeedSources) / sizeof(kFeedSources[0]);
ReviveImapCredentials g_sessionCredentials;
bool g_inboxCanOpen = false;
HBRUSH g_shellBrush = NULL;
HBRUSH g_surfaceBrush = NULL;
HBRUSH g_inputBrush = NULL;
HBRUSH g_accentBrush = NULL;

void ClearBytes(void* value, unsigned int length)
{
    volatile unsigned char* cursor = static_cast<volatile unsigned char*>(value);
    while (length-- != 0)
        *cursor++ = 0;
}

void InitializeTheme()
{
    g_shellBrush = CreateSolidBrush(kShellColor);
    g_surfaceBrush = CreateSolidBrush(kSurfaceColor);
    g_inputBrush = CreateSolidBrush(kInputColor);
    g_accentBrush = CreateSolidBrush(kAccentColor);
}

void DestroyTheme()
{
    if (g_shellBrush != NULL)
        DeleteObject(g_shellBrush);
    if (g_surfaceBrush != NULL)
        DeleteObject(g_surfaceBrush);
    if (g_inputBrush != NULL)
        DeleteObject(g_inputBrush);
    if (g_accentBrush != NULL)
        DeleteObject(g_accentBrush);
    g_shellBrush = NULL;
    g_surfaceBrush = NULL;
    g_inputBrush = NULL;
    g_accentBrush = NULL;
}

void DrawBrandMark(HDC deviceContext, int right, int top)
{
    RECT mark;
    mark.left = right - 34;
    mark.top = top;
    mark.right = right - 25;
    mark.bottom = top + 9;
    FillRect(deviceContext, &mark, g_accentBrush);
    mark.left += 12;
    mark.right += 12;
    FillRect(deviceContext, &mark, g_surfaceBrush);
    mark.left -= 6;
    mark.right -= 6;
    mark.top += 11;
    mark.bottom += 11;
    FillRect(deviceContext, &mark, g_accentBrush);
}

bool HandleThemeMessage(HWND window, UINT message, WPARAM wParam,
                        LPARAM lParam, LRESULT* result)
{
    DRAWITEMSTRUCT* item;
    HDC deviceContext;
    RECT client;
    RECT accent;
    if (result == NULL)
        return false;
    if (message == WM_DRAWITEM)
    {
        item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item != NULL && item->CtlType == ODT_BUTTON)
        {
            wchar_t label[64];
            HBRUSH brush = (item->itemState & ODS_SELECTED) != 0 ?
                g_accentBrush : g_surfaceBrush;
            FillRect(item->hDC, &item->rcItem, brush);
            FrameRect(item->hDC, &item->rcItem, g_accentBrush);
            label[0] = L'\0';
            GetWindowText(item->hwndItem, label, sizeof(label) / sizeof(wchar_t));
            SetTextColor(item->hDC, (item->itemState & ODS_SELECTED) != 0 ?
                         kShellColor : kTextColor);
            SetBkMode(item->hDC, TRANSPARENT);
            DrawText(item->hDC, label, -1, &item->rcItem,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            *result = TRUE;
            return true;
        }
    }
    if (message == WM_ERASEBKGND)
    {
        deviceContext = reinterpret_cast<HDC>(wParam);
        GetClientRect(window, &client);
        FillRect(deviceContext, &client, g_shellBrush);
        accent.left = 0;
        accent.top = 0;
        accent.right = client.right;
        accent.bottom = 5;
        FillRect(deviceContext, &accent, g_accentBrush);
        DrawBrandMark(deviceContext, client.right - 10, 14);
        *result = 1;
        return true;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        deviceContext = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        FillRect(deviceContext, &client, g_shellBrush);
        accent.left = 0;
        accent.top = 0;
        accent.right = client.right;
        accent.bottom = 5;
        FillRect(deviceContext, &accent, g_accentBrush);
        DrawBrandMark(deviceContext, client.right - 10, 14);
        EndPaint(window, &paint);
        *result = 0;
        return true;
    }
    if (message == WM_CTLCOLORSTATIC)
    {
        deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, kTextColor);
        SetBkColor(deviceContext, kShellColor);
        SetBkMode(deviceContext, TRANSPARENT);
        *result = reinterpret_cast<LRESULT>(g_shellBrush);
        return true;
    }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
    {
        deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, RGB(20, 31, 42));
        SetBkColor(deviceContext, kInputColor);
        *result = reinterpret_cast<LRESULT>(g_inputBrush);
        return true;
    }
    if (message == WM_CTLCOLORBTN)
    {
        deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, kTextColor);
        SetBkColor(deviceContext, kSurfaceColor);
        *result = reinterpret_cast<LRESULT>(g_surfaceBrush);
        return true;
    }
    return false;
}

const wchar_t* RowName(ReviveUiRow row)
{
    static const wchar_t* const names[REVIVE_UI_ROW_COUNT] =
    { L"DNS", L"TCP", L"TLS 1.2", L"Certificate", L"Hostname", L"SERVICE" };
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
    case REVIVE_HTTP_URL_ERROR: return L"HTTPS URL INVALID";
    case REVIVE_HTTP_IO_ERROR: return L"HTTPS READ FAILED";
    case REVIVE_HTTP_RESPONSE_ERROR: return L"HTTP RESPONSE INVALID";
    case REVIVE_HTTP_STATUS_ERROR: return L"HTTP STATUS NOT OK";
    case REVIVE_HTTP_HEADER_TOO_LARGE: return L"HTTP HEADERS TOO LARGE";
    case REVIVE_HTTP_ENCODING_ERROR: return L"HTTP CONTENT ENCODING";
    case REVIVE_FEED_CONFIGURATION_ERROR: return L"FEED URL INVALID";
    case REVIVE_FEED_FORMAT_ERROR: return L"RSS OR ATOM INVALID";
    case REVIVE_FEED_NO_ITEMS: return L"NO FEED ITEMS";
    default: return L"SERVICE FAILED";
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

void SetMailHint(const wchar_t* hint)
{
    wchar_t text[160];
    if (hint == NULL)
        return;
    wsprintf(text, L"%s  %s", RowName(REVIVE_UI_IMAP), hint);
    SetWindowText(g_statusControls[REVIVE_UI_IMAP], text);
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

void CreatePanelHeader(HWND window, const wchar_t* section, HFONT font,
                       int width, int margin)
{
    wchar_t text[96];
    wsprintf(text, L"REVIVECE  |  %s", section);
    HWND header = CreateWindow(L"STATIC", text, WS_CHILD | WS_VISIBLE,
        margin, margin, width - 2 * margin, 28, window, NULL,
        GetModuleHandle(NULL), NULL);
    SendMessage(header, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void ActivatePanel(HWND panel)
{
    RECT frame;
    if (panel == NULL || g_mainWindow == NULL)
        return;
    GetWindowRect(g_mainWindow, &frame);
    SetWindowPos(panel, HWND_TOP, frame.left, frame.top,
                 frame.right - frame.left, frame.bottom - frame.top,
                 SWP_SHOWWINDOW);
    SetForegroundWindow(panel);
    BringWindowToTop(panel);
}

void CopyReplyAddress(wchar_t* destination, int capacity, const wchar_t* sender)
{
    const wchar_t* start;
    const wchar_t* end;
    int output = 0;
    if (destination == NULL || capacity <= 0)
        return;
    destination[0] = L'\0';
    if (sender == NULL)
        return;
    start = sender;
    while (*start != L'\0' && *start != L'<')
        ++start;
    if (*start == L'<')
        ++start;
    else
        start = sender;
    end = start;
    while (*end != L'\0' && *end != L'>')
        ++end;
    while (start < end && (*start == L' ' || *start == L'\t'))
        ++start;
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t'))
        --end;
    while (start < end && output < capacity - 1)
        destination[output++] = *start++;
    destination[output] = L'\0';
}

void PrepareReply(const ReviveUiMessageBody* content)
{
    int index = 0;
    if (content == NULL)
        return;
    CopyReplyAddress(g_replyRecipient,
                     sizeof(g_replyRecipient) / sizeof(wchar_t), content->sender);
    g_replySubject[0] = L'\0';
    if (content->subject[0] == L'R' && content->subject[1] == L'e' &&
        content->subject[2] == L':' && content->subject[3] == L' ')
        lstrcpyn(g_replySubject, content->subject,
                 sizeof(g_replySubject) / sizeof(wchar_t));
    else
    {
        g_replySubject[index++] = L'R';
        g_replySubject[index++] = L'e';
        g_replySubject[index++] = L':';
        g_replySubject[index++] = L' ';
        while (content->subject[index - 4] != L'\0' &&
               index < static_cast<int>(sizeof(g_replySubject) / sizeof(wchar_t)) - 1)
        {
            g_replySubject[index] = content->subject[index - 4];
            ++index;
        }
        g_replySubject[index] = L'\0';
    }
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

void PostHttpResponse(HWND window, ReviveHttpResult result, int status,
                      const char* body, bool truncated)
{
    ReviveUiHttpResponse* copied = static_cast<ReviveUiHttpResponse*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiHttpResponse)));
    if (copied == NULL)
        return;
    copied->result = static_cast<int>(result);
    copied->status = status;
    copied->truncated = truncated;
    CopyUtf8ToWide(copied->body, sizeof(copied->body) / sizeof(wchar_t), body);
    if (!PostMessage(window, WM_REVIVE_HTTP_RESPONSE, 0, reinterpret_cast<LPARAM>(copied)))
        HeapFree(GetProcessHeap(), 0, copied);
}

void PostFeedItem(HWND window, const ReviveFeedItem* item)
{
    ReviveUiFeedItem* copied = static_cast<ReviveUiFeedItem*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiFeedItem)));
    if (copied == NULL || item == NULL)
    {
        if (copied != NULL)
            HeapFree(GetProcessHeap(), 0, copied);
        return;
    }
    CopyUtf8ToWide(copied->title, sizeof(copied->title) / sizeof(wchar_t), item->title);
    CopyUtf8ToWide(copied->link, sizeof(copied->link) / sizeof(wchar_t), item->link);
    CopyUtf8ToWide(copied->date, sizeof(copied->date) / sizeof(wchar_t), item->date);
    if (!PostMessage(window, WM_REVIVE_FEED_ITEM, 0, reinterpret_cast<LPARAM>(copied)))
        HeapFree(GetProcessHeap(), 0, copied);
}

void PostFeedResult(HWND window, int result, int httpStatus, int itemCount,
                    bool atomFormat)
{
    ReviveUiFeedResult* copied = static_cast<ReviveUiFeedResult*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ReviveUiFeedResult)));
    if (copied == NULL)
        return;
    copied->result = result;
    copied->httpStatus = httpStatus;
    copied->itemCount = itemCount;
    copied->atomFormat = atomFormat;
    if (!PostMessage(window, WM_REVIVE_FEED_RESULT, 0, reinterpret_cast<LPARAM>(copied)))
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
    const bool sending = request->mode == WORKER_SMTP_SEND;
    const bool fetchingHttp = request->mode == WORKER_HTTP_GET ||
                              request->mode == WORKER_FEED_FETCH;
    ReviveSmtpResult sendResult = REVIVE_SMTP_IO_ERROR;
    const char* serverHost = fetchingHttp ? request->httpUrl.host :
                             sending ? kSmtpServerHost : kImapServerHost;
    const unsigned short serverPort = fetchingHttp ? request->httpUrl.port :
                                     sending ? kSmtpServerPort : kImapServerPort;
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
            else if (request->mode == WORKER_SMTP_SEND)
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
            else if (request->mode == WORKER_HTTP_GET)
            {
                char* response = static_cast<char*>(HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, REVIVE_HTTP_RESPONSE_CAPACITY + 1));
                int httpStatus = 0;
                bool truncated = false;
                ReviveHttpResult httpResult = REVIVE_HTTP_IO_ERROR;
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                if (response != NULL)
                    httpResult = ReviveHttpGet(tlsConnection, &request->httpUrl,
                                                response, REVIVE_HTTP_RESPONSE_CAPACITY + 1,
                                                &httpStatus, &truncated);
                if (httpResult == REVIVE_HTTP_OK)
                {
                    ReviveLog("HTTP", "HTTPS response body received", httpStatus);
                    PostHttpResponse(window, httpResult, httpStatus, response, truncated);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                {
                    ReviveLog("HTTP", "HTTPS request failed", static_cast<int>(httpResult));
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED,
                               static_cast<int>(httpResult));
                    PostHttpResponse(window, httpResult, httpStatus, NULL, false);
                }
                if (response != NULL)
                {
                    ClearBytes(response, REVIVE_HTTP_RESPONSE_CAPACITY + 1);
                    HeapFree(GetProcessHeap(), 0, response);
                }
            }
            else
            {
                char* response = static_cast<char*>(HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, REVIVE_HTTP_RESPONSE_CAPACITY + 1));
                ReviveFeedItem* items = static_cast<ReviveFeedItem*>(HeapAlloc(
                    GetProcessHeap(), HEAP_ZERO_MEMORY,
                    sizeof(ReviveFeedItem) * REVIVE_FEED_MAX_ITEMS));
                int httpStatus = 0;
                int itemCount = 0;
                bool truncated = false;
                bool atomFormat = false;
                ReviveHttpResult httpResult = REVIVE_HTTP_IO_ERROR;
                ReviveFeedResult feedResult = REVIVE_FEED_CONFIGURATION_ERROR;
                PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_RUNNING, 0);
                if (response != NULL)
                    httpResult = ReviveHttpGet(tlsConnection, &request->httpUrl,
                                                response, REVIVE_HTTP_RESPONSE_CAPACITY + 1,
                                                &httpStatus, &truncated);
                if (httpResult == REVIVE_HTTP_OK && items != NULL)
                    feedResult = ReviveFeedParse(response, items, REVIVE_FEED_MAX_ITEMS,
                                                 &itemCount, &atomFormat);
                if (httpResult == REVIVE_HTTP_OK && feedResult == REVIVE_FEED_OK)
                {
                    for (int index = 0; index < itemCount; ++index)
                        PostFeedItem(window, &items[index]);
                    ReviveLog("FEED", "RSS or Atom items parsed", itemCount);
                    PostFeedResult(window, REVIVE_FEED_OK, httpStatus, itemCount, atomFormat);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_OK, 0);
                    succeeded = true;
                }
                else
                {
                    const int result = httpResult != REVIVE_HTTP_OK ?
                        static_cast<int>(httpResult) : static_cast<int>(feedResult);
                    ReviveLog("FEED", "feed refresh failed", result);
                    PostFeedResult(window, result, httpStatus, 0, false);
                    PostStatus(window, REVIVE_UI_IMAP, REVIVE_UI_FAILED, result);
                }
                if (items != NULL)
                {
                    ClearBytes(items, sizeof(ReviveFeedItem) * REVIVE_FEED_MAX_ITEMS);
                    HeapFree(GetProcessHeap(), 0, items);
                }
                if (response != NULL)
                {
                    ClearBytes(response, REVIVE_HTTP_RESPONSE_CAPACITY + 1);
                    HeapFree(GetProcessHeap(), 0, response);
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

bool SameText(const char* first, const char* second)
{
    if (first == NULL || second == NULL)
        return false;
    while (*first != '\0' && *second != '\0')
        if (*first++ != *second++)
            return false;
    return *first == '\0' && *second == '\0';
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
    if (mode == WORKER_INBOX_REFRESH)
    {
        const bool hasEmail = CopyEditUtf8(g_emailEdit, request->credentials.email,
                                           sizeof(request->credentials.email));
        const bool hasPassword = CopyEditUtf8(g_passwordEdit,
                                              request->credentials.appPassword,
                                              sizeof(request->credentials.appPassword));
        if (hasEmail && hasPassword)
            CopyMemory(&g_sessionCredentials, &request->credentials,
                       sizeof(g_sessionCredentials));
        else if (hasEmail && !hasPassword &&
                 SameText(request->credentials.email, g_sessionCredentials.email) &&
                 g_sessionCredentials.appPassword[0] != '\0')
            CopyMemory(&request->credentials, &g_sessionCredentials,
                       sizeof(request->credentials));
        else
        {
            ReviveImapClearCredentials(&request->credentials);
            HeapFree(GetProcessHeap(), 0, request);
            SetMailHint(L"ENTER GMAIL + APP PASSWORD");
            return;
        }
    }
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
    if (mode == WORKER_HTTP_GET || mode == WORKER_FEED_FETCH)
    {
        char urlText[REVIVE_HTTP_URL_CAPACITY];
        HWND urlControl = mode == WORKER_FEED_FETCH ? g_feedUrl : g_httpUrl;
        const bool validUrl = CopyEditUtf8(urlControl, urlText, sizeof(urlText)) &&
                              ReviveHttpParseUrl(urlText, &request->httpUrl);
        ClearBytes(urlText, sizeof(urlText));
        if (!validUrl)
        {
            HeapFree(GetProcessHeap(), 0, request);
            SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_HTTP_URL_ERROR);
            return;
        }
    }
    if (mode == WORKER_INBOX_REFRESH)
    {
        SendMessage(g_inbox, LB_RESETCONTENT, 0, 0);
        SetWindowText(g_passwordEdit, L"");
        g_passwordVisible = false;
        SendMessage(g_passwordEdit, EM_SETPASSWORDCHAR, static_cast<WPARAM>(L'*'), 0);
        InvalidateRect(g_passwordEdit, NULL, TRUE);
        SetWindowText(g_passwordToggle, L"SHOW");
        g_inboxCanOpen = false;
        EnableWindow(g_openButton, FALSE);
    }
    ResetRows();
    EnableWindow(g_runButton, FALSE);
    EnableWindow(g_refreshButton, FALSE);
    EnableWindow(g_openButton, FALSE);
    EnableWindow(g_composeButton, FALSE);
    EnableWindow(g_webButton, FALSE);
    EnableWindow(g_feedsButton, FALSE);
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
        EnableWindow(g_webButton, TRUE);
        EnableWindow(g_feedsButton, TRUE);
        if (g_readerLoadMore != NULL)
            EnableWindow(g_readerLoadMore, g_readerHasMore ? TRUE : FALSE);
        if (g_httpFetch != NULL)
            EnableWindow(g_httpFetch, TRUE);
        if (g_feedFetch != NULL)
            EnableWindow(g_feedFetch, TRUE);
    }
}

LRESULT CALLBACK ComposeWindowProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam)
{
    LRESULT themeResult;
    if (HandleThemeMessage(window, message, wParam, lParam, &themeResult))
        return themeResult;
    switch (message)
    {
    case WM_CREATE:
    {
        RECT client;
        HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
        const int margin = 12;
        const int rowHeight = 24;
        const int contentTop = margin + 34;
        const int buttonTop = contentTop + (rowHeight + 4) * 3;
        const int bodyTop = buttonTop + rowHeight + 4;
        int buttonWidth;
        int bodyHeight;
        GetClientRect(window, &client);
        buttonWidth = (client.right - 3 * margin) / 2;
        CreatePanelHeader(window, L"COMPOSE", font, client.right, margin);
        HWND recipientLabel = CreateWindow(L"STATIC", L"To:", WS_CHILD | WS_VISIBLE,
            margin, contentTop, 42, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(recipientLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeRecipient = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_TABSTOP | ES_AUTOHSCROLL, margin + 42, contentTop,
            client.right - 2 * margin - 42, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeRecipient, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND subjectLabel = CreateWindow(L"STATIC", L"Subject:", WS_CHILD | WS_VISIBLE,
            margin, contentTop + rowHeight + 4, 52, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(subjectLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeSubject = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_TABSTOP | ES_AUTOHSCROLL, margin + 52, contentTop + rowHeight + 4,
            client.right - 2 * margin - 52, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeSubject, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeStatus = CreateWindow(L"STATIC", L"Plain text only. ASCII subject.",
            WS_CHILD | WS_VISIBLE, margin, contentTop + (rowHeight + 4) * 2,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_composeStatus, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_composeSend = CreateWindow(L"BUTTON", L"SEND", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW, margin, buttonTop, buttonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(kComposeSendControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_composeSend, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
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
        ActivatePanel(g_composeWindow);
        return;
    }
    if (g_sessionCredentials.email[0] == '\0' ||
        g_sessionCredentials.appPassword[0] == '\0')
    {
        SetRow(REVIVE_UI_IMAP, REVIVE_UI_FAILED, REVIVE_SMTP_CONFIGURATION_ERROR);
        return;
    }
    g_composeWindow = CreateWindow(kComposeWindowClass, L"ReviveCE Compose",
        WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        g_mainWindow, NULL, GetModuleHandle(NULL), NULL);
    if (g_composeWindow != NULL)
    {
        if (g_replyRecipient[0] != L'\0')
            SetWindowText(g_composeRecipient, g_replyRecipient);
        if (g_replySubject[0] != L'\0')
            SetWindowText(g_composeSubject, g_replySubject);
        g_replyRecipient[0] = L'\0';
        g_replySubject[0] = L'\0';
        ActivatePanel(g_composeWindow);
        UpdateWindow(g_composeWindow);
    }
}

LRESULT CALLBACK HttpWindowProc(HWND window, UINT message,
                                WPARAM wParam, LPARAM lParam)
{
    LRESULT themeResult;
    if (HandleThemeMessage(window, message, wParam, lParam, &themeResult))
        return themeResult;
    switch (message)
    {
    case WM_CREATE:
    {
        RECT client;
        HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
        const int margin = 12;
        const int rowHeight = 24;
        const int contentTop = margin + 34;
        int actualButtonWidth;
        int bodyTop;
        int bodyHeight;
        GetClientRect(window, &client);
        actualButtonWidth = (client.right - 3 * margin) / 2;
        CreatePanelHeader(window, L"HTTPS", font, client.right, margin);
        HWND urlLabel = CreateWindow(L"STATIC", L"HTTPS URL:", WS_CHILD | WS_VISIBLE,
            margin, contentTop, 72, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(urlLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_httpUrl = CreateWindow(L"EDIT", L"https://www.google.com/robots.txt",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            margin + 72, contentTop, client.right - 2 * margin - 72, rowHeight, window,
            NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_httpUrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_httpFetch = CreateWindow(L"BUTTON", L"GET", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            BS_DEFPUSHBUTTON | BS_OWNERDRAW, margin, contentTop + rowHeight + 5, actualButtonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(kHttpFetchControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_httpFetch, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            margin * 2 + actualButtonWidth, contentTop + rowHeight + 5, actualButtonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_httpStatus = CreateWindow(L"STATIC", L"HTTPS only. Response limit: 32 KiB.",
            WS_CHILD | WS_VISIBLE, margin, contentTop + (rowHeight + 5) * 2,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_httpStatus, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        bodyTop = contentTop + (rowHeight + 5) * 3;
        bodyHeight = client.bottom - bodyTop - margin;
        if (bodyHeight < rowHeight * 2)
            bodyHeight = rowHeight * 2;
        g_httpBody = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            margin, bodyTop, client.right - 2 * margin, bodyHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_httpBody, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(g_httpUrl);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kHttpFetchControl && HIWORD(wParam) == BN_CLICKED)
        {
            SetWindowText(g_httpStatus, L"FETCHING...");
            EnableWindow(g_httpFetch, FALSE);
            BeginWorker(g_mainWindow, WORKER_HTTP_GET, 0, 0);
            if (g_workerThread == NULL)
            {
                SetWindowText(g_httpStatus, L"ENTER A VALID HTTPS URL");
                EnableWindow(g_httpFetch, TRUE);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_httpWindow = NULL;
        g_httpUrl = NULL;
        g_httpFetch = NULL;
        g_httpStatus = NULL;
        g_httpBody = NULL;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

void ShowHttpResponse(ReviveUiHttpResponse* response)
{
    wchar_t status[160];
    if (response == NULL)
        return;
    if (g_httpWindow != NULL && response->result == REVIVE_HTTP_OK)
    {
        wsprintf(status, L"HTTP %d%s", response->status,
                 response->truncated ? L"  (first 32 KiB shown)" : L"");
        SetWindowText(g_httpStatus, status);
        SetWindowText(g_httpBody, response->body);
    }
    else if (g_httpWindow != NULL)
        SetWindowText(g_httpStatus, MailFailureName(response->result));
    HeapFree(GetProcessHeap(), 0, response);
}

void OpenHttp()
{
    if (g_httpWindow != NULL)
    {
        ActivatePanel(g_httpWindow);
        return;
    }
    g_httpWindow = CreateWindow(kHttpWindowClass, L"ReviveCE HTTPS",
        WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        g_mainWindow, NULL, GetModuleHandle(NULL), NULL);
    if (g_httpWindow != NULL)
    {
        ActivatePanel(g_httpWindow);
        UpdateWindow(g_httpWindow);
    }
}

void SelectFeedSource(int index)
{
    if (g_feedUrl == NULL || index < 0 || index >= kFeedSourceCount)
        return;
    SetWindowText(g_feedUrl, kFeedSources[index].url);
}

LRESULT CALLBACK FeedWindowProc(HWND window, UINT message,
                                WPARAM wParam, LPARAM lParam)
{
    LRESULT themeResult;
    if (HandleThemeMessage(window, message, wParam, lParam, &themeResult))
        return themeResult;
    switch (message)
    {
    case WM_CREATE:
    {
        RECT client;
        HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
        const int margin = 12;
        const int rowHeight = 24;
        const int contentTop = margin + 34;
        int buttonWidth;
        int listTop;
        const int sourceTop = contentTop + rowHeight + 5;
        const int buttonTop = sourceTop + rowHeight + 5;
        GetClientRect(window, &client);
        buttonWidth = (client.right - 3 * margin) / 2;
        CreatePanelHeader(window, L"FEEDS", font, client.right, margin);
        HWND urlLabel = CreateWindow(L"STATIC", L"Feed URL:", WS_CHILD | WS_VISIBLE,
            margin, contentTop, 60, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(urlLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_feedUrl = CreateWindow(L"EDIT", L"https://hnrss.org/frontpage",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            margin + 60, contentTop, client.right - 2 * margin - 60, rowHeight, window,
            NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_feedUrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND sourceLabel = CreateWindow(L"STATIC", L"RSS Feed:", WS_CHILD | WS_VISIBLE,
            margin, sourceTop, 60, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(sourceLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_feedSource = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            margin + 60, sourceTop, client.right - 2 * margin - 60, rowHeight * 6,
            window, reinterpret_cast<HMENU>(kFeedSourceControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_feedSource, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (int index = 0; index < kFeedSourceCount; ++index)
            SendMessage(g_feedSource, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(kFeedSources[index].name));
        SendMessage(g_feedSource, CB_SETCURSEL, 0, 0);
        g_feedFetch = CreateWindow(L"BUTTON", L"OPEN RSS", WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW, margin, buttonTop, buttonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(kFeedFetchControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_feedFetch, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            margin * 2 + buttonWidth, buttonTop, buttonWidth,
            rowHeight, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_feedStatus = CreateWindow(L"STATIC", L"RSS 2.0 and Atom 1.0. No feed is saved yet.",
            WS_CHILD | WS_VISIBLE, margin, buttonTop + rowHeight + 5,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_feedStatus, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        listTop = buttonTop + (rowHeight + 5) * 2;
        g_feedItems = CreateWindow(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            WS_VSCROLL | LBS_NOINTEGRALHEIGHT, margin, listTop,
            client.right - 2 * margin, client.bottom - listTop - margin, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(g_feedItems, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(g_feedUrl);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kFeedSourceControl &&
            HIWORD(wParam) == CBN_SELCHANGE)
        {
            const int index = static_cast<int>(SendMessage(
                g_feedSource, CB_GETCURSEL, 0, 0));
            SelectFeedSource(index);
            return 0;
        }
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED)
        {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kFeedFetchControl && HIWORD(wParam) == BN_CLICKED)
        {
            SendMessage(g_feedItems, LB_RESETCONTENT, 0, 0);
            SetWindowText(g_feedStatus, L"FETCHING FEED...");
            EnableWindow(g_feedFetch, FALSE);
            BeginWorker(g_mainWindow, WORKER_FEED_FETCH, 0, 0);
            if (g_workerThread == NULL)
            {
                SetWindowText(g_feedStatus, L"ENTER A VALID HTTPS FEED URL");
                EnableWindow(g_feedFetch, TRUE);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_feedWindow = NULL;
        g_feedUrl = NULL;
        g_feedSource = NULL;
        g_feedFetch = NULL;
        g_feedStatus = NULL;
        g_feedItems = NULL;
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

void AddFeedItem(const ReviveUiFeedItem* item)
{
    wchar_t text[320];
    const wchar_t* title;
    if (item == NULL || g_feedItems == NULL)
        return;
    title = item->title[0] != L'\0' ? item->title : L"(untitled item)";
    if (item->date[0] != L'\0')
        wsprintf(text, L"%s (%s)", title, item->date);
    else
        wsprintf(text, L"%s", title);
    SendMessage(g_feedItems, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void ShowFeedResult(ReviveUiFeedResult* result)
{
    wchar_t status[160];
    if (result == NULL)
        return;
    if (g_feedStatus != NULL && result->result == REVIVE_FEED_OK)
    {
        wsprintf(status, L"HTTP %d. %d %s items.", result->httpStatus, result->itemCount,
                 result->atomFormat ? L"Atom" : L"RSS");
        SetWindowText(g_feedStatus, status);
    }
    else if (g_feedStatus != NULL)
        SetWindowText(g_feedStatus, MailFailureName(result->result));
    HeapFree(GetProcessHeap(), 0, result);
}

void OpenFeeds()
{
    if (g_feedWindow != NULL)
    {
        ActivatePanel(g_feedWindow);
        return;
    }
    g_feedWindow = CreateWindow(kFeedWindowClass, L"ReviveCE Feeds",
        WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        g_mainWindow, NULL, GetModuleHandle(NULL), NULL);
    if (g_feedWindow != NULL)
    {
        ActivatePanel(g_feedWindow);
        UpdateWindow(g_feedWindow);
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
    LRESULT themeResult;
    if (HandleThemeMessage(window, message, wParam, lParam, &themeResult))
        return themeResult;
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
        const int contentTop = margin + 34;
        wchar_t from[384];
        GetClientRect(window, &client);
        CreatePanelHeader(window, L"MESSAGE", font, client.right, margin);
        wsprintf(from, L"%s", content->sender[0] != L'\0' ? content->sender : L"(unknown sender)");
        HWND fromControl = CreateWindow(L"STATIC", from, WS_CHILD | WS_VISIBLE,
            margin, contentTop, client.right - 2 * margin, rowHeight, window, NULL,
            GetModuleHandle(NULL), NULL);
        SendMessage(fromControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND subjectControl = CreateWindow(L"STATIC",
            content->subject[0] != L'\0' ? content->subject : L"(no subject)",
            WS_CHILD | WS_VISIBLE, margin, contentTop + rowHeight,
            client.right - 2 * margin, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(subjectControl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerDetail = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            margin, contentTop + 2 * rowHeight, client.right - 2 * margin, rowHeight,
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_readerDetail, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerBody = CreateWindow(L"EDIT", content->body,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL, margin, margin + 3 * rowHeight,
            client.right - 2 * margin, client.bottom - (5 * rowHeight + 2 * margin + 34),
            window, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_readerBody, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerLoadMore = CreateWindow(L"BUTTON", L"LOAD MORE",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, margin,
            client.bottom - (rowHeight + margin), (client.right - 3 * margin) / 2,
            rowHeight, window, reinterpret_cast<HMENU>(kReaderLoadMoreControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_readerLoadMore, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND replyButton = CreateWindow(L"BUTTON", L"REPLY",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, margin,
            client.bottom - (2 * rowHeight + margin + 4),
            (client.right - 3 * margin) / 2, rowHeight, window,
            reinterpret_cast<HMENU>(kReaderReplyControl),
            GetModuleHandle(NULL), NULL);
        SendMessage(replyButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backButton = CreateWindow(L"BUTTON", L"BACK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            margin * 2 + (client.right - 3 * margin) / 2,
            client.bottom - (rowHeight + margin), (client.right - 3 * margin) / 2,
            rowHeight, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandle(NULL), NULL);
        SendMessage(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_readerUid = content->uid;
        g_readerDisplayedBytes = content->displayedBytes;
        g_readerHasMore = content->hasMore;
        PrepareReply(content);
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
        if (LOWORD(wParam) == kReaderReplyControl && HIWORD(wParam) == BN_CLICKED)
        {
            OpenCompose();
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
        WS_POPUP | WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        g_mainWindow, NULL, GetModuleHandle(NULL), content);
    if (g_readerWindow != NULL)
    {
        ActivatePanel(g_readerWindow);
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
    const int credentialLabelWidth = 84;
    const int passwordToggleWidth = 50;
    const int passwordEditWidth = width - (2 * margin + credentialLabelWidth +
                                            passwordToggleWidth + 4);
    int top = margin;
    HWND title = CreateWindow(L"STATIC", L"REVIVECE  |  MAIL", WS_CHILD | WS_VISIBLE | SS_CENTER,
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
    HWND emailLabel = CreateWindow(L"STATIC", L"Gmail:", WS_CHILD | WS_VISIBLE,
        margin, top, credentialLabelWidth, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(emailLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_emailEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
        margin + credentialLabelWidth, top, width - (2 * margin + credentialLabelWidth), rowHeight, window,
        reinterpret_cast<HMENU>(IDC_EMAIL), GetModuleHandle(NULL), NULL);
    SendMessage(g_emailEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + 3;
    HWND passwordLabel = CreateWindow(L"STATIC", L"Password:", WS_CHILD | WS_VISIBLE,
        margin, top, credentialLabelWidth, rowHeight, window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(passwordLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_passwordEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
        margin + credentialLabelWidth, top, passwordEditWidth, rowHeight, window,
        reinterpret_cast<HMENU>(IDC_APP_PASSWORD), GetModuleHandle(NULL), NULL);
    SendMessage(g_passwordEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_passwordToggle = CreateWindow(L"BUTTON", L"SHOW", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin + credentialLabelWidth + passwordEditWidth + 4, top, passwordToggleWidth,
        rowHeight, window, reinterpret_cast<HMENU>(IDC_TOGGLE_PASSWORD),
        GetModuleHandle(NULL), NULL);
    SendMessage(g_passwordToggle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += rowHeight + margin / 2;
    const int buttonWidth = (width - 3 * margin) / 2;
    const int buttonHeight = rowHeight + 5;
    g_runButton = CreateWindow(L"BUTTON", L"TEST TLS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_RUN_TEST), GetModuleHandle(NULL), NULL);
    SendMessage(g_runButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_refreshButton = CreateWindow(L"BUTTON", L"REFRESH INBOX", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
        margin * 2 + buttonWidth, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_REFRESH_INBOX), GetModuleHandle(NULL), NULL);
    SendMessage(g_refreshButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += buttonHeight + 5;
    g_openButton = CreateWindow(L"BUTTON", L"OPEN", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_OPEN_MESSAGE), GetModuleHandle(NULL), NULL);
    SendMessage(g_openButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_composeButton = CreateWindow(L"BUTTON", L"COMPOSE", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin * 2 + buttonWidth, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_COMPOSE), GetModuleHandle(NULL), NULL);
    SendMessage(g_composeButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    top += buttonHeight + margin / 2;
    g_webButton = CreateWindow(L"BUTTON", L"WEB GET", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_WEB_GET), GetModuleHandle(NULL), NULL);
    SendMessage(g_webButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    g_feedsButton = CreateWindow(L"BUTTON", L"FEEDS", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        margin * 2 + buttonWidth, top, buttonWidth, buttonHeight, window,
        reinterpret_cast<HMENU>(IDC_FEEDS), GetModuleHandle(NULL), NULL);
    SendMessage(g_feedsButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    windowClass.lpfnWndProc = HttpWindowProc;
    windowClass.lpszClassName = kHttpWindowClass;
    if (RegisterClass(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    windowClass.lpfnWndProc = FeedWindowProc;
    windowClass.lpszClassName = kFeedWindowClass;
    if (RegisterClass(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    return mainClass;
}

HWND CreateReviveMainWindow(HINSTANCE instance, int showCommand)
{
    InitializeTheme();
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
    LRESULT themeResult;
    if (HandleThemeMessage(window, message, wParam, lParam, &themeResult))
        return themeResult;
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
        if (LOWORD(wParam) == IDC_TOGGLE_PASSWORD && HIWORD(wParam) == BN_CLICKED)
        {
            g_passwordVisible = !g_passwordVisible;
            SendMessage(g_passwordEdit, EM_SETPASSWORDCHAR,
                        g_passwordVisible ? 0 : static_cast<WPARAM>(L'*'), 0);
            InvalidateRect(g_passwordEdit, NULL, TRUE);
            SetWindowText(g_passwordToggle, g_passwordVisible ? L"HIDE" : L"SHOW");
            return 0;
        }
        if (LOWORD(wParam) == IDC_OPEN_MESSAGE && HIWORD(wParam) == BN_CLICKED)
        { OpenSelectedMessage(window); return 0; }
        if (LOWORD(wParam) == IDC_COMPOSE && HIWORD(wParam) == BN_CLICKED)
        { OpenCompose(); return 0; }
        if (LOWORD(wParam) == IDC_WEB_GET && HIWORD(wParam) == BN_CLICKED)
        { OpenHttp(); return 0; }
        if (LOWORD(wParam) == IDC_FEEDS && HIWORD(wParam) == BN_CLICKED)
        { OpenFeeds(); return 0; }
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
    case WM_REVIVE_HTTP_RESPONSE:
        ShowHttpResponse(reinterpret_cast<ReviveUiHttpResponse*>(lParam));
        return 0;
    case WM_REVIVE_FEED_ITEM:
    {
        ReviveUiFeedItem* item = reinterpret_cast<ReviveUiFeedItem*>(lParam);
        if (item != NULL)
        {
            AddFeedItem(item);
            HeapFree(GetProcessHeap(), 0, item);
        }
        return 0;
    }
    case WM_REVIVE_FEED_RESULT:
        ShowFeedResult(reinterpret_cast<ReviveUiFeedResult*>(lParam));
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
        EnableWindow(g_webButton, TRUE);
        EnableWindow(g_feedsButton, TRUE);
        if (g_composeSend != NULL)
            EnableWindow(g_composeSend, TRUE);
        if (g_httpFetch != NULL)
            EnableWindow(g_httpFetch, TRUE);
        if (g_feedFetch != NULL)
            EnableWindow(g_feedFetch, TRUE);
        SetFocus(g_inboxCanOpen ? g_inbox : g_refreshButton);
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
        if (g_httpWindow != NULL)
            DestroyWindow(g_httpWindow);
        if (g_feedWindow != NULL)
            DestroyWindow(g_feedWindow);
        DestroyTheme();
        g_mainWindow = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}
