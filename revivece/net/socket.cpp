#include "socket.h"

#include "../common/log.h"

namespace
{
void Report(ReviveNetProgress progress,
            ReviveNetStage stage,
            ReviveNetState state,
            int nativeError,
            void* context)
{
    if (progress != NULL)
        progress(stage, state, nativeError, context);
}

bool WaitForConnect(SOCKET socketHandle, DWORD timeoutMilliseconds,
                    int* nativeError)
{
    fd_set writeSet;
    fd_set errorSet;
    timeval timeout;
    int selected;
    int socketError = 0;
    int socketErrorLength = sizeof(socketError);

    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(socketHandle, &writeSet);
    FD_SET(socketHandle, &errorSet);

    timeout.tv_sec = timeoutMilliseconds / 1000;
    timeout.tv_usec = (timeoutMilliseconds % 1000) * 1000;
    selected = select(0, NULL, &writeSet, &errorSet, &timeout);
    if (selected == SOCKET_ERROR)
    {
        *nativeError = WSAGetLastError();
        return false;
    }
    if (selected == 0)
    {
        *nativeError = WSAETIMEDOUT;
        return false;
    }

    if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&socketError),
                   &socketErrorLength) == SOCKET_ERROR)
    {
        *nativeError = WSAGetLastError();
        return false;
    }
    if (socketError != 0 || FD_ISSET(socketHandle, &errorSet))
    {
        *nativeError = socketError != 0 ? socketError : WSAECONNREFUSED;
        return false;
    }

    return FD_ISSET(socketHandle, &writeSet) != 0;
}
}

bool ReviveNetConnect(const char* host,
                      unsigned short port,
                      DWORD timeoutMilliseconds,
                      ReviveNetConnection* connection,
                      ReviveNetProgress progress,
                      void* progressContext)
{
    WSADATA winsockData;
    hostent* hostEntry;
    sockaddr_in address;
    char** addressCursor;
    u_long nonBlocking = 1;
    int result;
    int nativeError = 0;

    if (host == NULL || connection == NULL)
        return false;

    connection->socketHandle = INVALID_SOCKET;
    connection->winsockStarted = false;

    ReviveLogEndpoint("NET", "endpoint", host, port);

    result = WSAStartup(MAKEWORD(2, 2), &winsockData);
    if (result != 0)
    {
        ReviveLog("NET", "Winsock initialization failed", result);
        Report(progress, REVIVE_NET_DNS, REVIVE_NET_FAILED, result,
               progressContext);
        return false;
    }
    connection->winsockStarted = true;

    Report(progress, REVIVE_NET_DNS, REVIVE_NET_RUNNING, 0, progressContext);
    ReviveLog("NET", "resolving host", 0);
    hostEntry = gethostbyname(host);
    if (hostEntry == NULL || hostEntry->h_addr_list == NULL ||
        hostEntry->h_addr_list[0] == NULL)
    {
        nativeError = WSAGetLastError();
        ReviveLog("NET", "DNS resolution failed", nativeError);
        Report(progress, REVIVE_NET_DNS, REVIVE_NET_FAILED, nativeError,
               progressContext);
        ReviveNetClose(connection);
        return false;
    }
    Report(progress, REVIVE_NET_DNS, REVIVE_NET_SUCCEEDED, 0,
           progressContext);
    ReviveLog("NET", "DNS resolved", 0);

    Report(progress, REVIVE_NET_TCP, REVIVE_NET_RUNNING, 0,
           progressContext);
    for (addressCursor = hostEntry->h_addr_list;
         *addressCursor != NULL; ++addressCursor)
    {
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr = *reinterpret_cast<in_addr*>(*addressCursor);
        for (int index = 0; index < 8; ++index)
            address.sin_zero[index] = 0;

        connection->socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connection->socketHandle == INVALID_SOCKET)
        {
            nativeError = WSAGetLastError();
            continue;
        }

        if (ioctlsocket(connection->socketHandle, FIONBIO, &nonBlocking) ==
            SOCKET_ERROR)
        {
            nativeError = WSAGetLastError();
            closesocket(connection->socketHandle);
            connection->socketHandle = INVALID_SOCKET;
            continue;
        }

        result = connect(connection->socketHandle,
                         reinterpret_cast<sockaddr*>(&address), sizeof(address));
        if (result == 0)
            break;

        nativeError = WSAGetLastError();
        if ((nativeError == WSAEWOULDBLOCK ||
             nativeError == WSAEINPROGRESS || nativeError == WSAEALREADY) &&
            WaitForConnect(connection->socketHandle, timeoutMilliseconds,
                           &nativeError))
            break;

        closesocket(connection->socketHandle);
        connection->socketHandle = INVALID_SOCKET;
    }

    if (connection->socketHandle == INVALID_SOCKET)
    {
        ReviveLog("NET", "TCP connection failed", nativeError);
        Report(progress, REVIVE_NET_TCP, REVIVE_NET_FAILED, nativeError,
               progressContext);
        ReviveNetClose(connection);
        return false;
    }
    nonBlocking = 0;
    if (ioctlsocket(connection->socketHandle, FIONBIO, &nonBlocking) ==
        SOCKET_ERROR)
    {
        nativeError = WSAGetLastError();
        ReviveLog("NET", "blocking socket restore failed", nativeError);
        Report(progress, REVIVE_NET_TCP, REVIVE_NET_FAILED, nativeError,
               progressContext);
        ReviveNetClose(connection);
        return false;
    }

    Report(progress, REVIVE_NET_TCP, REVIVE_NET_SUCCEEDED, 0,
           progressContext);
    ReviveLog("NET", "TCP connected", 0);
    return true;
}

void ReviveNetClose(ReviveNetConnection* connection)
{
    if (connection == NULL)
        return;

    if (connection->socketHandle != INVALID_SOCKET)
    {
        closesocket(connection->socketHandle);
        connection->socketHandle = INVALID_SOCKET;
    }
    if (connection->winsockStarted)
    {
        WSACleanup();
        connection->winsockStarted = false;
    }
}
