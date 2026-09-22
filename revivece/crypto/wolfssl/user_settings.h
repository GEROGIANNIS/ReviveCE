#ifndef REVIVECE_WOLFSSL_USER_SETTINGS_H
#define REVIVECE_WOLFSSL_USER_SETTINGS_H

/* CeGCC's PE/COFF writer supports at most 8-byte object alignment. wolfSSL
 * enables explicit alignment on pre-ARMv6 targets, so cap every larger hint. */
#define ALIGN16  WOLFSSL_ALIGN(8)
#define ALIGN32  WOLFSSL_ALIGN(8)
#define ALIGN64  WOLFSSL_ALIGN(8)
#define ALIGN128 WOLFSSL_ALIGN(8)
#define ALIGN256 WOLFSSL_ALIGN(8)
#define WOLFSSL_GENERAL_ALIGNMENT 8

/* WinCE headers otherwise define function-like min/max macros, which collide
 * with wolfCrypt's constant-time helpers. wc_port.h also needs time_t visible. */
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <time.h>

/* ReviveCE is a TLS 1.2 client. All transport I/O is supplied by revive_net. */
#define WOLFSSL_USER_IO
#define NO_WOLFSSL_SERVER
#define NO_OLD_TLS
#undef WOLFSSL_TLS13

/* Windows CE has a small process stack; move large temporary buffers to heap. */
#define WOLFSSL_SMALL_STACK
#define SINGLE_THREADED
#define NO_WRITEV
#define NO_FILESYSTEM

/* TLS extensions required by modern public services. */
#define HAVE_TLS_EXTENSIONS
#define HAVE_SNI
#define HAVE_SUPPORTED_CURVES
#define HAVE_EXTENDED_MASTER
#define HAVE_ENCRYPT_THEN_MAC
#define HAVE_SERVER_RENEGOTIATION_INFO

/* Google may append a cross-signed root for legacy clients. Validate the peer
 * to our trusted self-signed root without requiring every trailing certificate
 * presented by the server to form the selected path. */
#define WOLFSSL_ALT_CERT_CHAINS

/* General-purpose constant-time math for RSA and ECC certificate chains. */
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define WOLFSSL_NO_ASM
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

/* Modern TLS 1.2 primitives. */
#define HAVE_HASHDRBG
#define HAVE_AESGCM
#define GCM_SMALL
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512

/* Features and legacy algorithms outside the ReviveCE client profile. */
#define NO_SESSION_CACHE
#define NO_PSK
#define NO_DH
#define NO_DSA
/* SHA-1 and MD5 are NOT disabled. wolfSSL's X.509 certificate parser requires
 * SHA-1 internally for certificate chain verification and fingerprinting, even
 * when the negotiated cipher suite uses only SHA-256/384. Disabling SHA-1
 * causes wolfSSL_connect() to fail with a certificate error before hostname
 * verification runs. MD5 is similarly needed for ASN.1 OID processing during
 * certificate parsing. Neither algorithm is used in the negotiated cipher
 * suite — they are only used internally by wolfSSL's certificate engine. */
#define NO_RC4
#define NO_MD4
#define NO_DES3
#define NO_DES3_TLS_SUITES
#define NO_PWDBASED
#define WOLFSSL_NO_SHAKE128
#define WOLFSSL_NO_SHAKE256

#endif
