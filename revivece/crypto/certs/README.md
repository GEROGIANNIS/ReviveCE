# ReviveCE Google service trust bundle

`google-roots.pem` is the Google-maintained CA list for clients connecting to
Google services. It was downloaded from `https://pki.goog/roots.pem` on
2026-09-22.

- Certificates: 21
- SHA-256: `ec989df46c8f4419ef2ee2517cad7619d555e4973f3307be697662aa2497e480`
- Current live Gmail chain observed during review:
  `imap.gmail.com -> WR2 -> GTS Root R1`
- GTS Root R1 SHA-256:
  `d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf`

Google says its service certificate chains are not static and recommends
synchronizing this bundle at least every six months. Replace the PEM only from
the official HTTPS endpoint, inspect its certificate list, and update the
pinned hashes in both repository validation scripts in the same commit.
