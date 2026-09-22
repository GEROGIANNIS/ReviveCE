#ifndef REVIVECE_SOCKET_H
#define REVIVECE_SOCKET_H

#include <winsock2.h>
#include <windows.h>

enum ReviveNetStage
{
    REVIVE_NET_DNS = 0,
    REVIVE_NET_TCP
};

enum ReviveNetState
{
    REVIVE_NET_RUNNING = 0,
    REVIVE_NET_SUCCEEDED,
    REVIVE_NET_FAILED
};

typedef void (*ReviveNetProgress)(ReviveNetStage stage,
                                  ReviveNetState state,
                                  int nativeError,
                                  void* context);

struct ReviveNetConnection
{
    SOCKET socketHandle;
    bool winsockStarted;
};

// Resolves an IPv4 address and opens a TCP connection. There is deliberately no
// plaintext application-protocol fallback above this function.
bool ReviveNetConnect(const char* host,
                      unsigned short port,
                      DWORD timeoutMilliseconds,
                      ReviveNetConnection* connection,
                      ReviveNetProgress progress,
                      void* progressContext);

void ReviveNetClose(ReviveNetConnection* connection);

#endif
