#ifndef REVIVECE_WOLFSSL_USER_SETTINGS_H
#define REVIVECE_WOLFSSL_USER_SETTINGS_H

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
#define NO_SHA
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_DES3
#define NO_DES3_TLS_SUITES
#define NO_PWDBASED
#define WOLFSSL_NO_SHAKE128
#define WOLFSSL_NO_SHAKE256

#endif
