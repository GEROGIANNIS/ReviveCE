# wolfSSL port boundary

TLS is intentionally isolated behind `revivece/net/tls.h`. The current source
returns `REVIVE_TLS_NOT_AVAILABLE` and performs no I/O. This makes M0/M1 useful
without creating a path that can silently downgrade to plaintext.

For M2/M3:

1. Vendor a reviewed, pinned wolfSSL release rather than tracking an unpinned
   branch.
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
