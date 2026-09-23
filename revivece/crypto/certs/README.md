# ReviveCE service trust bundle

`google-roots.pem` contains the Google-maintained CA list plus the official
Let's Encrypt ISRG Root X1 certificate. The additional root is required for
the default `https://hnrss.org/frontpage` feed, whose current chain uses the
Let's Encrypt YR1 intermediate.

- Certificates: 22
- SHA-256: `e7e2d35bd9d8b205b93581f8635cd299a0c7e2c361b7f303b5c9659a0a9d1363`
- Current live Gmail chain observed during review:
  `imap.gmail.com -> WR2 -> GTS Root R1 (GlobalSign cross-sign)`
- Presented cross-sign SHA-256:
  `3ee0278df71fa3c125c4cd487f01d774694e6fc57e0cd94c24efd769133918e5`
- GTS Root R1 SHA-256:
  `d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf`

Google says its service certificate chains are not static and recommends
synchronizing this bundle at least every six months. Replace the PEM only from
the official HTTPS endpoint, inspect its certificate list, and update the
pinned hashes in both repository validation scripts in the same commit.

The ISRG Root X1 certificate is published at
`https://letsencrypt.org/certs/isrgrootx1.pem`.
