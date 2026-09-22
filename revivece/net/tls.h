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

// Availability is a compile-time property. Initialization remains exposed as
// a small diagnostic, while ReviveTLSConnect owns the real TLS lifecycle.
bool ReviveTLSIsAvailable();
bool ReviveTLSInitialize();

// Opens a TLS 1.2 client session over an already-connected socket. The CA
// bundle is loaded into wolfSSL from the supplied PEM file; Windows Mobile's
// certificate store and TLS implementation are never used.
ReviveTlsResult ReviveTLSConnect(ReviveNetConnection* network,
                                 const char* host,
                                 const wchar_t* caBundlePath,
                                 ReviveTlsConnection** tlsConnection);
int ReviveTLSRead(ReviveTlsConnection* connection, void* buffer, int length);
int ReviveTLSWrite(ReviveTlsConnection* connection,
                   const void* buffer, int length);
void ReviveTLSClose(ReviveTlsConnection* connection);

#endif
