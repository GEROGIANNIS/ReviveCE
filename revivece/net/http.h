#ifndef REVIVECE_HTTP_H
#define REVIVECE_HTTP_H

#include "tls.h"

enum
{
    REVIVE_HTTP_URL_CAPACITY = 512,
    REVIVE_HTTP_HOST_CAPACITY = 256,
    REVIVE_HTTP_PATH_CAPACITY = 512,
    REVIVE_HTTP_RESPONSE_CAPACITY = 32768
};

enum ReviveHttpResult
{
    REVIVE_HTTP_OK = 200,
    REVIVE_HTTP_URL_ERROR,
    REVIVE_HTTP_IO_ERROR,
    REVIVE_HTTP_RESPONSE_ERROR,
    REVIVE_HTTP_STATUS_ERROR,
    REVIVE_HTTP_HEADER_TOO_LARGE,
    REVIVE_HTTP_ENCODING_ERROR
};

struct ReviveHttpUrl
{
    char host[REVIVE_HTTP_HOST_CAPACITY];
    char path[REVIVE_HTTP_PATH_CAPACITY];
    unsigned short port;
};

// Parses a bounded https://host[:port]/path URL. Plain HTTP, fragments,
// credentials, and IPv6 literals are deliberately outside the M7 proof.
bool ReviveHttpParseUrl(const char* text, ReviveHttpUrl* url);

// Retrieves a single HTTP/1.1 response over an already verified TLS session.
// The caller owns response storage. A successful truncated response contains a
// safe prefix and sets truncated; redirects and compressed responses are not
// followed or decoded in M7.
ReviveHttpResult ReviveHttpGet(ReviveTlsConnection* connection,
                               const ReviveHttpUrl* url,
                               char* response,
                               int responseCapacity,
                               int* httpStatus,
                               bool* truncated);

#endif
