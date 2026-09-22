#include "../net/socket.h"
#include "../net/tls.h"
#include "ui.h"

#include "resource.h"
#include "../common/log.h"

namespace
{
const wchar_t* const kWindowClass = L"ReviveTLSWindow";
const wchar_t* const kServerDisplay = L"imap.gmail.com:993";
const char* const kServerHost = "imap.gmail.com";
const unsigned short kServerPort = 993;
const DWORD kConnectTimeoutMilliseconds = 15000;

HWND g_statusControls[REVIVE_UI_ROW_COUNT];
HWND g_runButton = NULL;
HANDLE g_workerThread = NULL;

const wchar_t* RowName(ReviveUiRow row)
{
    static const wchar_t* const names[REVIVE_UI_ROW_COUNT] =
    {
        L"DNS",
        L"TCP",
        L"wolfSSL Init",
        L"Certificate",
        L"Hostname"
    };
    return names[row];
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

void PostStatus(HWND window, ReviveUiRow row,
                ReviveUiState state, int nativeError)
{
    ReviveUiStatusMessage* status = static_cast<ReviveUiStatusMessage*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(ReviveUiStatusMessage)));
    if (status == NULL)
        return;

    status->row = row;
    status->state = state;
    status->nativeError = nativeError;
    if (!PostMessage(window, WM_REVIVE_STATUS, 0,
                     reinterpret_cast<LPARAM>(status)))
        HeapFree(GetProcessHeap(), 0, status);
}

void NetworkProgress(ReviveNetStage stage,
                     ReviveNetState state,
                     int nativeError,
                     void* context)
{
    ReviveUiRow row = stage == REVIVE_NET_DNS ? REVIVE_UI_DNS : REVIVE_UI_TCP;
    ReviveUiState uiState = REVIVE_UI_RUNNING;

    if (state == REVIVE_NET_SUCCEEDED)
        uiState = REVIVE_UI_OK;
    else if (state == REVIVE_NET_FAILED)
        uiState = REVIVE_UI_FAILED;

    PostStatus(static_cast<HWND>(context), row, uiState, nativeError);
}

DWORD WINAPI NetworkWorker(void* context)
{
    HWND window = static_cast<HWND>(context);
    ReviveNetConnection connection;
    bool tlsInitialized = false;

    const bool connected = ReviveNetConnect(kServerHost, kServerPort,
        kConnectTimeoutMilliseconds, &connection, NetworkProgress, window);
    if (connected)
    {
        if (ReviveTLSIsAvailable())
        {
            PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_RUNNING, 0);
            tlsInitialized = ReviveTLSInitialize();
            PostStatus(window, REVIVE_UI_TLS,
                       tlsInitialized ? REVIVE_UI_OK : REVIVE_UI_FAILED, 0);
        }
        else
        {
            PostStatus(window, REVIVE_UI_TLS, REVIVE_UI_NOT_BUILT, 0);
        }
        ReviveNetClose(&connection);
    }

    PostMessage(window, WM_REVIVE_TEST_COMPLETE,
                connected && (!ReviveTLSIsAvailable() || tlsInitialized)
                    ? TRUE : FALSE,
                0);
    return 0;
}

void BeginNetworkTest(HWND window)
{
    DWORD threadId = 0;

    if (g_workerThread != NULL)
        return;

    SetRow(REVIVE_UI_DNS, REVIVE_UI_NOT_RUN, 0);
    SetRow(REVIVE_UI_TCP, REVIVE_UI_NOT_RUN, 0);
    SetRow(REVIVE_UI_TLS,
           ReviveTLSIsAvailable() ? REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT,
           0);
    SetRow(REVIVE_UI_CERTIFICATE, REVIVE_UI_NOT_BUILT, 0);
    SetRow(REVIVE_UI_HOSTNAME, REVIVE_UI_NOT_BUILT, 0);
    EnableWindow(g_runButton, FALSE);

    ReviveLog("APP", "network test started", 0);
    g_workerThread = CreateThread(NULL, 0, NetworkWorker, window, 0, &threadId);
    if (g_workerThread == NULL)
    {
        const int nativeError = GetLastError();
        SetRow(REVIVE_UI_DNS, REVIVE_UI_FAILED, nativeError);
        EnableWindow(g_runButton, TRUE);
        ReviveLog("APP", "worker thread creation failed", nativeError);
    }
}

void CreateChildControls(HWND window)
{
    RECT client;
    HFONT font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
    int width;
    int margin;
    int top;
    int rowHeight;

    GetClientRect(window, &client);
    width = client.right - client.left;
    margin = width / 20;
    rowHeight = (client.bottom - client.top) / 13;
    if (rowHeight < 28)
        rowHeight = 28;

    HWND title = CreateWindow(L"STATIC", L"ReviveTLS Test",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        margin, margin, width - 2 * margin, rowHeight,
        window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(title, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    top = margin + rowHeight + margin / 2;
    for (int row = 0; row < REVIVE_UI_ROW_COUNT; ++row)
    {
        g_statusControls[row] = CreateWindow(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin, top, width - 2 * margin, rowHeight,
            window, reinterpret_cast<HMENU>(IDC_STATUS_DNS + row),
            GetModuleHandle(NULL), NULL);
        SendMessage(g_statusControls[row], WM_SETFONT,
                    reinterpret_cast<WPARAM>(font), TRUE);
        SetRow(static_cast<ReviveUiRow>(row),
               row < REVIVE_UI_TLS ||
                   (row == REVIVE_UI_TLS && ReviveTLSIsAvailable())
                       ? REVIVE_UI_NOT_RUN : REVIVE_UI_NOT_BUILT,
               0);
        top += rowHeight;
    }

    HWND serverLabel = CreateWindow(L"STATIC", L"Server:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        margin, top + margin / 2, width - 2 * margin, rowHeight,
        window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(serverLabel, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);

    top += rowHeight;
    HWND serverValue = CreateWindow(L"STATIC", kServerDisplay,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        margin, top + margin / 2, width - 2 * margin, rowHeight,
        window, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(serverValue, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);

    top += rowHeight + margin;
    g_runButton = CreateWindow(L"BUTTON", L"RUN TEST",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        margin, top, width - 2 * margin, rowHeight + margin,
        window, reinterpret_cast<HMENU>(IDC_RUN_TEST),
        GetModuleHandle(NULL), NULL);
    SendMessage(g_runButton, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
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
    return RegisterClass(&windowClass);
}

HWND CreateReviveMainWindow(HINSTANCE instance, int showCommand)
{
    HWND window = CreateWindow(kWindowClass, L"ReviveTLS",
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, instance, NULL);
    if (window != NULL)
    {
        ShowWindow(window, showCommand);
        UpdateWindow(window);
    }
    return window;
}

LRESULT CALLBACK ReviveWindowProc(HWND window, UINT message,
                                  WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateChildControls(window);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RUN_TEST && HIWORD(wParam) == BN_CLICKED)
        {
            BeginNetworkTest(window);
            return 0;
        }
        break;

    case WM_REVIVE_STATUS:
    {
        ReviveUiStatusMessage* status =
            reinterpret_cast<ReviveUiStatusMessage*>(lParam);
        if (status != NULL)
        {
            SetRow(status->row, status->state, status->nativeError);
            HeapFree(GetProcessHeap(), 0, status);
        }
        return 0;
    }

    case WM_REVIVE_TEST_COMPLETE:
        if (g_workerThread != NULL)
        {
            CloseHandle(g_workerThread);
            g_workerThread = NULL;
        }
        EnableWindow(g_runButton, TRUE);
        SetFocus(g_runButton);
        ReviveLog("APP", wParam ? "TCP test completed" : "TCP test failed", 0);
        return 0;

    case WM_CLOSE:
        if (g_workerThread == NULL)
            DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(window, message, wParam, lParam);
}
