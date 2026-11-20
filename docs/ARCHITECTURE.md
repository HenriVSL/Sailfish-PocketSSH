# Architecture

PocketSSH is a port of [Pocket Shell](https://github.com/HenriVSL/Pocket-Shell) for iOS. The
iOS app was the guideline for behavior; the UI is native Sailfish Silica and the code is
C++/Qt 5.6 + QML. It is built in three layers, and each one can be replaced on its own:

```
┌────────────────────────────────────────────────────────────────────┐
│ UI (QML, Sailfish Silica)                               qml/       │
│  host list · host editor · terminal page · tabs · extra keys ·     │
│  host key / password dialogs · export/import · settings · cover    │
├────────────────────────────────────────────────────────────────────┤
│ App layer (C++)                                         src/app/   │
│  HostModel · SecretStore · KnownHosts · ConfigTransfer ·           │
│  TerminalSession (connect flow) · SessionManager (tabs)            │
├──────────────────────────────────┬─────────────────────────────────┤
│ SshSession             src/ssh/  │ Terminal, TerminalItem          │
│  one worker thread per session   │              src/terminal/      │
│                                  │  emulation · drawing · input    │
├──────────────────────────────────┼─────────────────────────────────┤
│ libssh2 1.11.1 (vendored)        │ libvterm 0.3.3 (vendored)       │
│  + system OpenSSL 3 (libcrypto)  │                                 │
└──────────────────────────────────┴─────────────────────────────────┘
```

## Why libssh2 and libvterm, vendored?

Neither is in the Sailfish OS repositories (only the OpenSSH programs are), so their
unmodified upstream sources live in `3rdparty/` and are compiled straight into the app by
`3rdparty/libssh2.pri` and `3rdparty/libvterm.pri`. `tools/vendor-*.sh` download, verify
(SHA-256, and the upstream GPG signature for libssh2) and unpack a pinned release, so an
upgrade is a version bump plus a re-run. libssh2 links against the system OpenSSL 3
(`libcrypto.so.3`), which every Sailfish OS 5 device has.

libssh2 is the same library the iOS app uses: OpenSSH is a set of programs, not a library,
and running `ssh` in a pty would hand host-key checks and password prompts to a program we
can't present natively. libvterm is the terminal engine Neovim uses; it does the xterm
parsing, screen state and key encoding, and PocketSSH draws the result itself.

## SSH layer: `SshSession` (src/ssh/)

- **Threading:** every libssh2 call runs on one private worker thread per session, because
  libssh2 sessions aren't thread-safe. Public methods queue a task for the worker and return
  at once; results come back as signals in the owner's (GUI) thread.
- **Phases** run in blocking mode with timeouts:
  1. `connectToHost()`: DNS, non-blocking TCP connect with a timeout, `TCP_NODELAY`, then the
     SSH handshake → `hostKeyReceived` (type, base64 blob, `SHA256:` fingerprint as OpenSSH prints it).
  2. `queryAuthMethods()` → the methods the server accepts.
  3. `authenticateWithPassword()` (falling back to keyboard-interactive when the server doesn't
     offer `password`) or `authenticateWithKey()` from a key held in memory.
     `authenticationFailed` keeps the connection up for another try.
  4. `startShell()`: a PTY (`xterm-256color`) and a shell channel, then the I/O loop.
- **I/O loop:** non-blocking; drains stdout and stderr, flushes queued writes, applies the
  latest window size, sends keepalives every 30 s, and `poll()`s the socket plus an eventfd
  that `write()`, `resize()` and `disconnectFromHost()` use to wake it.
- **Teardown** never blocks the UI for long: a blocking phase is cut short by shutting the
  socket down, the destructor waits at most a few seconds, and a worker stuck in a DNS lookup
  (which can't be interrupted) is detached at once and cleans up after itself. Sends use `MSG_NOSIGNAL`, so a dead
  connection can't raise SIGPIPE. `closed(error)` is emitted exactly once.

## Terminal layer (src/terminal/)

- **`Terminal`** wraps a libvterm screen: UTF-8, alternate screen, reflow on resize, 5000 lines
  of scrollback (not filled from the alternate screen), title (OSC 0/2), bell, OSC 52 clipboard
  writes (reads are refused). User input (`sendText`, `sendKey`, `paste`) is encoded by libvterm,
  so it follows the modes the remote program set (application cursor keys, bracketed paste).
  Output is parsed in slices of at most a few milliseconds per event-loop pass, so a flood of
  output can't freeze the UI.
- **`TerminalItem`** (`QQuickPaintedItem`) draws a `Terminal`: glyph runs on the cell grid,
  bold/italic/underline/strike/reverse/dim, wide characters, 256 colors and truecolor, box
  drawing, and the cursor. It takes input-method text from the Sailfish keyboard (predictive
  text and auto-capitals off) and hardware keys, scrolls the scrollback with flick momentum,
  selects on long press, zooms on pinch and reports taps on links. With `resizesTerminal: false`
  it only draws, scaled to fit, which the cover uses for its live miniature.
- `src/terminal/vtermhooks.c` is the one place that reaches into libvterm, to show dim
  text (SGR 2), which libvterm 0.3.3 otherwise drops.

## App layer (src/app/)

- **`TerminalSession`** is one tab. It owns a `Terminal` for its whole lifetime (so scrollback
  survives page switches and reconnects) and a fresh `SshSession` per connect. The connect flow:
  `connect → verify host key (KnownHosts; a dialog if the key is unknown or changed) →
  authenticate (saved secrets; a password or passphrase dialog with up to 3 tries and an
  optional "save") → shell`. The UI watches `prompt`/`promptData` and answers with
  `answerHostKey()` / `answerPassword()`. Signals from an `SshSession` that has been replaced
  are ignored.
- **`SessionManager`** is the list model of tabs and which one is current.
- **`HostModel`** is the host list (`hosts.json`). **`SecretStore`** keeps passwords, keys and
  passphrases (`secrets.json`). **`KnownHosts`** keeps trusted host keys (`known_hosts.json`).
  **`ConfigTransfer`** exports and imports the Pocket Shell JSON format.

## UI (qml/)

```
qml/harbour-pocketssh.qml        ApplicationWindow, showTerminal()/connectTo(), toast
qml/pages/
  HostListPage.qml               sessions + hosts, pull-down menu (add, import, export, settings)
  HostDialog.qml                 add/edit host: address, auth method, password, key file/paste
  TerminalPage.qml               tab strip, terminal, extra keys, session menu, closed overlay,
                                 opens the prompt dialogs
  HostKeyDialog.qml              trust a new or changed host key
  PasswordDialog.qml             password or key passphrase, optional save
  ExportDialog.qml               export with/without secrets to ~/Documents
  SettingsPage.qml               extra keys, visual bell, text size, trusted keys, about
  KnownHostsPage.qml             trusted host keys, forget
qml/components/
  TabStrip.qml  ExtraKeys.qml  SessionListItem.qml  StatusDot.qml  Toast.qml
qml/cover/CoverPage.qml          live terminal miniature or app name + host count
```

The terminal page keeps the Pocket Shell dark palette (background `#0D0F14`, accent
`#4AC9A3`) whatever the ambience, so terminal colors look the same everywhere; the other pages
follow the Sailfish ambience.

## Persistence

All files live in the app's data directory, which Sailjail keeps private to the app:
`~/.local/share/io.github.henrivsl/harbour-pocketssh/` (directory 0700, files 0600).

| Data | File |
|---|---|
| Host list (name, hostname, port, username, auth method) | `hosts.json` |
| Passwords, private keys, passphrases, keyed by host id | `secrets.json` |
| Trusted host keys, keyed by `host:port` | `known_hosts.json` |
| Settings (extra keys, visual bell, text size) | `~/.config/io.github.henrivsl/harbour-pocketssh/settings.conf` |
| Exports | `~/Documents/pocketssh-*.json` |

## Sailjail permissions

`Internet` (SSH connections), `Documents` and `Downloads` (exports, imports and private key
files). Nothing else.

## Tests

| Suite | What | Run |
|---|---|---|
| `tests/app/` | host list, secrets, known hosts, key inspection, export/import | `tests/app/run.sh` |
| `tests/ssh/` | `SshSession` against throwaway local sshd instances (and optionally the emulator) | `tests/ssh/run.sh` |
| `tests/terminal/` | `Terminal` unit tests and offscreen `TerminalItem` render checks; a desktop demo | `tests/terminal/run.sh` |

They build against the host's Qt 5.15 with `qmake-qt5`; the app itself targets Qt 5.6, so new
code must stick to Qt 5.6 APIs.

## Known limitations

- No SFTP, port forwarding, agent forwarding or jump hosts yet. libssh2 supports all of these.
- Keyboard-interactive login answers every prompt with the password, so one-time codes aren't supported.
- No ssh-agent; each host holds its own key.
- When the phone sleeps with the screen off, the network may stop and a session can drop; reconnect from its tab.
