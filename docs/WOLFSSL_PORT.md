# wolfSSL port boundary

TLS is intentionally isolated behind `revivece/net/tls.h`. The M2 build links
wolfSSL 5.9.2 at commit `ac01707f552c611fbd135cc723b2682b3e7f80f2`
and exercises `wolfSSL_Init()`/`wolfSSL_Cleanup()`. The connection function
still returns `REVIVE_TLS_NOT_AVAILABLE` and performs no TLS I/O, so an
initialization result cannot be mistaken for a secure connection.

For M2/M3:

1. Keep wolfSSL pinned to a reviewed full release commit rather than tracking
   an unpinned branch.
2. Use one shared `user_settings.h` for both wolfSSL and ReviveTLS and define
   `WOLFSSL_USER_SETTINGS` in both builds.
3. Enable a TLS client, TLS 1.2, X.509, RSA, ECC, SHA-256, AES, SNI, and the
   required certificate parsing. Disable server, DTLS, old TLS, and unused
   protocols only after a source-level dependency review.
4. Supply WinCE-compatible time, entropy, filesystem, and socket adapters where
   the selected release needs them. Entropy must fail closed if a secure source
   cannot be obtained.
5. Load the bundled CA data into the wolfSSL context. Do not read the Windows
   Mobile root store.
6. Set peer verification, SNI, and `wolfSSL_check_domain_name()` before
   `wolfSSL_connect()`.
7. Expose negotiated protocol and verification results to the UI separately.
   A successful TCP connection is not a successful TLS connection.

Relevant upstream API documentation:

- https://www.wolfssl.com/documentation/manuals/wolfssl/chapter04.html
- https://www.wolfssl.com/documentation/manuals/wolfssl/chapter07.html
- https://www.wolfssl.com/documentation/manuals/wolfssl/ssl_8h.html

Do not add an ignore-certificate-errors option. Do not add a plaintext retry.
