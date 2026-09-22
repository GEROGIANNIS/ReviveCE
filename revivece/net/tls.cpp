#include "tls.h"

#include "../common/log.h"

#include <string.h>

#ifdef REVIVECE_WITH_WOLFSSL
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#endif

struct ReviveTlsConnection
{
#ifdef REVIVECE_WITH_WOLFSSL
    WOLFSSL_CTX* context;
    WOLFSSL* session;
    bool wolfSslInitialized;
#else
    int unavailable;
#endif
};

#ifdef REVIVECE_WITH_WOLFSSL
namespace
{
const DWORD kMaximumCABundleBytes = 128 * 1024;
const long kTlsIoTimeoutSeconds = 15;
const char* const kSecureCipherList =
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES256-GCM-SHA384";

int SocketError(int nativeError, bool reading)
{
    switch (nativeError)
    {
    case WSAEWOULDBLOCK:
        return reading ? WOLFSSL_CBIO_ERR_WANT_READ
                       : WOLFSSL_CBIO_ERR_WANT_WRITE;
    case WSAETIMEDOUT:
        return WOLFSSL_CBIO_ERR_TIMEOUT;
    case WSAEINTR:
        return WOLFSSL_CBIO_ERR_ISR;
    case WSAECONNABORTED:
    case WSAECONNRESET:
    case WSAENOTCONN:
        return WOLFSSL_CBIO_ERR_CONN_RST;
    default:
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
}

int WaitForSocket(SOCKET socketHandle, bool reading)
{
    fd_set readySet;
    fd_set errorSet;
    timeval timeout;

    FD_ZERO(&readySet);
    FD_ZERO(&errorSet);
    FD_SET(socketHandle, &readySet);
    FD_SET(socketHandle, &errorSet);
    timeout.tv_sec = kTlsIoTimeoutSeconds;
    timeout.tv_usec = 0;

    const int selected = select(0,
        reading ? &readySet : NULL,
        reading ? NULL : &readySet,
        &errorSet, &timeout);
    if (selected == SOCKET_ERROR)
        return SocketError(WSAGetLastError(), reading);
    if (selected == 0)
        return WOLFSSL_CBIO_ERR_TIMEOUT;
    if (FD_ISSET(socketHandle, &errorSet))
        return WOLFSSL_CBIO_ERR_CONN_RST;
    if (!FD_ISSET(socketHandle, &readySet))
        return WOLFSSL_CBIO_ERR_GENERAL;
    return 0;
}

int ReceiveCallback(WOLFSSL*, char* buffer, int length, void* context)
{
    ReviveNetConnection* network =
        static_cast<ReviveNetConnection*>(context);
    if (network == NULL || network->socketHandle == INVALID_SOCKET)
        return WOLFSSL_CBIO_ERR_GENERAL;

    const int waitResult = WaitForSocket(network->socketHandle, true);
    if (waitResult != 0)
        return waitResult;

    const int result = recv(network->socketHandle, buffer, length, 0);
    if (result > 0)
        return result;
    if (result == 0)
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return SocketError(WSAGetLastError(), true);
}

int SendCallback(WOLFSSL*, char* buffer, int length, void* context)
{
    ReviveNetConnection* network =
        static_cast<ReviveNetConnection*>(context);
    if (network == NULL || network->socketHandle == INVALID_SOCKET)
        return WOLFSSL_CBIO_ERR_GENERAL;

    const int waitResult = WaitForSocket(network->socketHandle, false);
    if (waitResult != 0)
        return waitResult;

    const int result = send(network->socketHandle, buffer, length, 0);
    if (result >= 0)
        return result;
    return SocketError(WSAGetLastError(), false);
}

bool LoadCABundle(const wchar_t* path, unsigned char** contents, long* length)
{
    HANDLE file;
    DWORD size;
    DWORD bytesRead = 0;
    unsigned char* buffer;

    if (path == NULL || contents == NULL || length == NULL)
        return false;
    *contents = NULL;
    *length = 0;

    file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        ReviveLog("TLS", "CA bundle could not be opened", GetLastError());
        return false;
    }

    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 ||
        size > kMaximumCABundleBytes)
    {
        ReviveLog("TLS", "CA bundle size is invalid", static_cast<int>(size));
        CloseHandle(file);
        return false;
    }

    buffer = static_cast<unsigned char*>(
        HeapAlloc(GetProcessHeap(), 0, size));
    if (buffer == NULL)
    {
        ReviveLog("TLS", "CA bundle allocation failed", 0);
        CloseHandle(file);
        return false;
    }

    if (!ReadFile(file, buffer, size, &bytesRead, NULL) || bytesRead != size)
    {
        ReviveLog("TLS", "CA bundle read failed", GetLastError());
        HeapFree(GetProcessHeap(), 0, buffer);
        CloseHandle(file);
        return false;
    }

    CloseHandle(file);
    *contents = buffer;
    *length = static_cast<long>(size);
    return true;
}

bool IsCertificateError(int error)
{
    switch (error)
    {
    case VERIFY_CERT_ERROR:
    case VERIFY_SIGN_ERROR:
    case NO_PEER_CERT:
    case ASN_BEFORE_DATE_E:
    case ASN_AFTER_DATE_E:
    case ASN_SIG_CONFIRM_E:
    case ASN_CRIT_EXT_E:
    case ASN_NO_SIGNER_E:
        return true;
    default:
        return false;
    }
}

void DestroyConnection(ReviveTlsConnection* connection)
{
    if (connection == NULL)
        return;
    if (connection->session != NULL)
        wolfSSL_free(connection->session);
    if (connection->context != NULL)
        wolfSSL_CTX_free(connection->context);
    if (connection->wolfSslInitialized)
        wolfSSL_Cleanup();
    HeapFree(GetProcessHeap(), 0, connection);
}
}
#endif

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

ReviveTlsResult ReviveTLSConnect(ReviveNetConnection* network,
                                 const char* host,
                                 const wchar_t* caBundlePath,
                                 ReviveTlsConnection** tlsConnection)
{
    if (tlsConnection != NULL)
        *tlsConnection = NULL;

#ifdef REVIVECE_WITH_WOLFSSL
    unsigned char* caBundle = NULL;
    long caBundleLength = 0;
    int result;
    int error;
    ReviveTlsConnection* connection;

    if (network == NULL || network->socketHandle == INVALID_SOCKET ||
        host == NULL || host[0] == '\0' || caBundlePath == NULL ||
        tlsConnection == NULL)
        return REVIVE_TLS_CONFIGURATION_ERROR;

    connection = static_cast<ReviveTlsConnection*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                  sizeof(ReviveTlsConnection)));
    if (connection == NULL)
        return REVIVE_TLS_CONFIGURATION_ERROR;

    result = wolfSSL_Init();
    if (result != WOLFSSL_SUCCESS)
    {
        ReviveLog("TLS", "wolfSSL initialization failed", result);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }
    connection->wolfSslInitialized = true;

    connection->context = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (connection->context == NULL)
    {
        ReviveLog("TLS", "TLS 1.2 context creation failed", 0);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }

    wolfSSL_CTX_set_verify(connection->context, WOLFSSL_VERIFY_PEER, NULL);
    if (wolfSSL_CTX_set_cipher_list(connection->context,
                                    kSecureCipherList) != WOLFSSL_SUCCESS)
    {
        ReviveLog("TLS", "secure cipher configuration failed", 0);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }

    if (!LoadCABundle(caBundlePath, &caBundle, &caBundleLength))
    {
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }
    result = wolfSSL_CTX_load_verify_buffer(connection->context, caBundle,
                                             caBundleLength,
                                             WOLFSSL_FILETYPE_PEM);
    HeapFree(GetProcessHeap(), 0, caBundle);
    caBundle = NULL;
    if (result != WOLFSSL_SUCCESS)
    {
        ReviveLog("TLS", "CA bundle rejected by wolfSSL", result);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }

    wolfSSL_CTX_SetIORecv(connection->context, ReceiveCallback);
    wolfSSL_CTX_SetIOSend(connection->context, SendCallback);
    connection->session = wolfSSL_new(connection->context);
    if (connection->session == NULL)
    {
        ReviveLog("TLS", "TLS session allocation failed", 0);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }

    result = wolfSSL_UseSNI(connection->session, WOLFSSL_SNI_HOST_NAME,
                            host, static_cast<unsigned short>(strlen(host)));
    if (result != WOLFSSL_SUCCESS ||
        wolfSSL_check_domain_name(connection->session, host) != WOLFSSL_SUCCESS)
    {
        ReviveLog("TLS", "SNI or hostname configuration failed", result);
        DestroyConnection(connection);
        return REVIVE_TLS_CONFIGURATION_ERROR;
    }

    wolfSSL_SetIOReadCtx(connection->session, network);
    wolfSSL_SetIOWriteCtx(connection->session, network);
    ReviveLog("TLS", "TLS 1.2 handshake started", 0);
    result = wolfSSL_connect(connection->session);
    if (result != WOLFSSL_SUCCESS)
    {
        error = wolfSSL_get_error(connection->session, result);
        ReviveLog("TLS", "TLS handshake rejected", error);
        DestroyConnection(connection);
        if (error == DOMAIN_NAME_MISMATCH)
            return REVIVE_TLS_HOSTNAME_ERROR;
        if (IsCertificateError(error))
            return REVIVE_TLS_CERTIFICATE_ERROR;
        return REVIVE_TLS_HANDSHAKE_ERROR;
    }

    ReviveLog("TLS", "TLS 1.2 handshake and peer verification succeeded", 0);
    *tlsConnection = connection;
    return REVIVE_TLS_OK;
#else
    (void)network;
    (void)host;
    (void)caBundlePath;
    ReviveLog("TLS", "wolfSSL is not linked; connection rejected", 0);
    return REVIVE_TLS_NOT_AVAILABLE;
#endif
}

int ReviveTLSRead(ReviveTlsConnection* connection, void* buffer, int length)
{
#ifdef REVIVECE_WITH_WOLFSSL
    if (connection == NULL || connection->session == NULL ||
        buffer == NULL || length <= 0)
        return -1;
    const int result = wolfSSL_read(connection->session, buffer, length);
    if (result <= 0)
        ReviveLog("TLS", "encrypted read failed",
                  wolfSSL_get_error(connection->session, result));
    return result;
#else
    (void)connection;
    (void)buffer;
    (void)length;
    return -1;
#endif
}

int ReviveTLSWrite(ReviveTlsConnection* connection,
                   const void* buffer, int length)
{
#ifdef REVIVECE_WITH_WOLFSSL
    if (connection == NULL || connection->session == NULL ||
        buffer == NULL || length <= 0)
        return -1;
    const int result = wolfSSL_write(connection->session, buffer, length);
    if (result <= 0)
        ReviveLog("TLS", "encrypted write failed",
                  wolfSSL_get_error(connection->session, result));
    return result;
#else
    (void)connection;
    (void)buffer;
    (void)length;
    return -1;
#endif
}

void ReviveTLSClose(ReviveTlsConnection* connection)
{
#ifdef REVIVECE_WITH_WOLFSSL
    DestroyConnection(connection);
#else
    (void)connection;
#endif
}
