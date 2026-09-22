#include "tls.h"

#include "../common/log.h"

#ifdef REVIVECE_WITH_WOLFSSL
#include <wolfssl/ssl.h>
#endif

struct ReviveTlsConnection
{
    // Deliberately empty until the pinned wolfSSL build is introduced.
    int unavailable;
};

bool ReviveTLSIsAvailable()
{
#ifdef REVIVECE_WITH_WOLFSSL
    return true;
#else
    return false;
#endif
}

bool ReviveTLSInitialize()
{
#ifdef REVIVECE_WITH_WOLFSSL
    const int result = wolfSSL_Init();
    if (result != WOLFSSL_SUCCESS)
    {
        ReviveLog("TLS", "wolfSSL initialization failed", result);
        return false;
    }

    ReviveLog("TLS", "wolfSSL initialized", 0);
    wolfSSL_Cleanup();
    return true;
#else
    ReviveLog("TLS", "wolfSSL is not linked", 0);
    return false;
#endif
}

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
