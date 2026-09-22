#include "tls.h"

#include "../common/log.h"

struct ReviveTlsConnection
{
    // Deliberately empty until the pinned wolfSSL build is introduced.
    int unavailable;
};

ReviveTlsResult ReviveTLSConnect(ReviveNetConnection*,
                                 const char*,
                                 const wchar_t*,
                                 ReviveTlsConnection** tlsConnection)
{
    if (tlsConnection != NULL)
        *tlsConnection = NULL;
    ReviveLog("TLS", "wolfSSL is not integrated; connection rejected", 0);
    return REVIVE_TLS_NOT_AVAILABLE;
}

int ReviveTLSRead(ReviveTlsConnection*, void*, int)
{
    return -1;
}

int ReviveTLSWrite(ReviveTlsConnection*, const void*, int)
{
    return -1;
}

void ReviveTLSClose(ReviveTlsConnection*)
{
}
