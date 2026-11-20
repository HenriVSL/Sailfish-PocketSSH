# Security

## Reporting a vulnerability

Please **don't** open a public issue for a security problem. Use GitHub's
**Security ▸ Report a vulnerability** (private advisory) on this repository, or contact the
maintainer directly. Include the steps to reproduce it and which version you tested.

Vulnerabilities in libssh2, libvterm or OpenSSL themselves should go to those projects. The
fix here is then to upgrade (re-run `tools/vendor-libssh2.sh` / `tools/vendor-libvterm.sh`
with the new version; OpenSSL comes from the system).

## Security model

### Credentials

- Passwords, private keys and key passphrases are stored in `secrets.json` in the app's data
  directory, separate from the host list. The file is written atomically with mode 0600 in a
  0700 directory.
- Sailfish OS has no keychain service that apps can count on (the Sailfish Secrets daemon isn't
  installed on devices), so the secrets are **not encrypted at rest**. They are protected by
  Sailjail, which keeps other sandboxed apps out of PocketSSH's data directory, by Unix
  permissions, and by home directory encryption where the device has it enabled. Anyone with root or
  developer-mode shell access to the unlocked device can read them.
- Saving is optional. Leave the password or passphrase empty and the app asks on each connect.
- Private keys are copied into the secret store when you choose a key file, so the original
  file can be deleted afterwards.

### Server authenticity

- Trust on first use: the first time you connect to a server, the app shows its SHA-256
  host-key fingerprint (the same string `ssh-keygen -lf` prints) and asks you to trust it.
- The trusted key is stored per `host:port`. If the server later presents a different key, the
  connection stops at a red "Host key changed!" dialog showing both fingerprints. The key is
  only replaced if you explicitly trust the new one.
- Trusted keys can be reviewed and forgotten in Settings ▸ Trusted host keys.

### Transport

- All cryptography comes from libssh2 with the system OpenSSL 3, using their default algorithm
  preferences (host key order matches OpenSSH, Ed25519 first).
- Keepalives are sent every 30 seconds. Sends use `MSG_NOSIGNAL`, so a dropped connection
  can't crash the app.

### Terminal

- Terminal programs can *write* to the clipboard (OSC 52); read requests are refused.
- Tapped links only open if they use `http` or `https`.
- The keyboard is told not to learn or predict what you type in the terminal.

### Export files

- Exporting **without** secrets is the default.
- Exporting **with** secrets needs an explicit switch, shows a plain-text warning, puts
  `-with-secrets` in the file name and writes the file with mode 0600. It is saved in
  `~/Documents`, which other apps with the Documents permission can read: move or delete it
  after use.

### What the app never does

- No analytics, tracking, crash reporting or other network traffic besides the SSH connections you open.
- No clipboard reads by terminal programs.

## Ideas for hardening

- Encrypt `secrets.json` with a key derived from an app passphrase, or use Sailfish Secrets
  where it is available.
- Password-protected, encrypted exports as a middle ground between "no secrets" and "plain text".
- Generating keys on the device.
