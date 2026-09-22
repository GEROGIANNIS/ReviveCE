#ifndef REVIVECE_TLS_H
#define REVIVECE_TLS_H

#include "socket.h"

enum ReviveTlsResult
{
    REVIVE_TLS_OK = 0,
    REVIVE_TLS_NOT_AVAILABLE,
    REVIVE_TLS_CONFIGURATION_ERROR,
    REVIVE_TLS_HANDSHAKE_ERROR,
    REVIVE_TLS_CERTIFICATE_ERROR,
    REVIVE_TLS_HOSTNAME_ERROR
};

struct ReviveTlsConnection;

// M2 device proof. Availability is a compile-time property; initialization
// runs wolfSSL's real global setup and cleanup without claiming a handshake.
bool ReviveTLSIsAvailable();
bool ReviveTLSInitialize();

// M2/M3 integration boundary. Until wolfSSL is compiled for ARMV4I this
// function always fails closed with REVIVE_TLS_NOT_AVAILABLE.
ReviveTlsResult ReviveTLSConnect(ReviveNetConnection* network,
                                 const char* host,
                                 const wchar_t* caBundlePath,
                                 ReviveTlsConnection** tlsConnection);
int ReviveTLSRead(ReviveTlsConnection* connection, void* buffer, int length);
int ReviveTLSWrite(ReviveTlsConnection* connection,
                   const void* buffer, int length);
void ReviveTLSClose(ReviveTlsConnection* connection);

#endif
