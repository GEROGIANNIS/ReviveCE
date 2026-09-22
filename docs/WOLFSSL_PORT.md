# wolfSSL port boundary

TLS is intentionally isolated behind `revivece/net/tls.h`. The M3 build links
wolfSSL 5.9.2 at commit `ac01707f552c611fbd135cc723b2682b3e7f80f2`
and performs TLS 1.2 through custom callbacks over the already-connected
Winsock socket. It loads `google-roots.pem` from beside the executable, enables
peer verification and SNI, checks the requested hostname, and fails closed.
The callbacks bound reads and writes with `select()`; Windows CE 5.2 returns
`WSAENOPROTOOPT` for the desktop Winsock `SO_RCVTIMEO`/`SO_SNDTIMEO` options.

`WOLFSSL_ALT_CERT_CHAINS` is required for Google compatibility. Gmail currently
presents `leaf -> WR2 -> GTS Root R1 cross-sign`, while the trust bundle holds
the self-signed GTS Root R1. Alternate-chain mode lets wolfSSL select the valid
path from WR2 to that trusted root instead of rejecting the extra cross-signed
certificate with `ASN_NO_SIGNER_E`. It does not disable peer verification.

For M3:

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
   CeGCC also requires wolfSSL's explicit ARM alignment hints to be capped at
   8 bytes, `time_t` to be included explicitly, and `NOMINMAX` to be set before
   the Windows CE headers.
5. Load the bundled CA data into the wolfSSL context. Do not read the Windows
   Mobile root store.
6. Set peer verification, SNI, and `wolfSSL_check_domain_name()` before
   `wolfSSL_connect()`.
7. Expose handshake and verification results to the UI separately.
   A successful TCP connection is not a successful TLS connection.

The checked-in bundle is the 21-certificate Google service CA list downloaded
from `https://pki.goog/roots.pem`. Repository validation pins its SHA-256 to
`ec989df46c8f4419ef2ee2517cad7619d555e4973f3307be697662aa2497e480`.
Google advises synchronizing this list at least twice yearly because service
chains can change; updating it requires review plus updating the pinned hash.

Relevant upstream API documentation:

- https://www.wolfssl.com/documentation/manuals/wolfssl/chapter04.html
- https://www.wolfssl.com/documentation/manuals/wolfssl/chapter07.html
- https://www.wolfssl.com/documentation/manuals/wolfssl/ssl_8h.html

Do not add an ignore-certificate-errors option. Do not add a plaintext retry.
