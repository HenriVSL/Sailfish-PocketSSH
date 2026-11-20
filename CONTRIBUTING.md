# Contributing

Thanks for helping. Issues and pull requests are welcome.

## Getting set up

Follow [docs/BUILDING.md](docs/BUILDING.md). You need the Sailfish SDK (or just docker,
using the same script as CI) and, to try the app, the SDK emulator or a Sailfish OS device
in developer mode.

## Guidelines

- **Never patch vendored upstream code.** libssh2 and libvterm under `3rdparty/` are
  unmodified upstream sources, so upgrades stay trivial. Refresh them only with
  `tools/vendor-libssh2.sh` and `tools/vendor-libvterm.sh`. Our build glue lives next to
  them (`3rdparty/*.pri`, `3rdparty/libssh2-config/`). If you need something they don't
  offer, wrap it in `src/`.
- Keep to what Sailfish OS 5.1 ships: Qt 5.6, QtQuick 2.x and Silica. No extra runtime
  dependencies beyond the system libraries the spec already relies on.
- Match the style of the surrounding code, and use Silica components and `Theme` values in
  QML so the app follows the user's ambience.
- Secrets (passwords, keys, passphrases) never leave the app except through an export the
  user explicitly asked for.
- Test on the emulator, and on a real device when you change keyboard, touch or
  performance-sensitive behaviour.
- Update the docs when you change behaviour they describe. New releases need a
  `rpm/harbour-pocketssh.changes` entry (see [docs/RELEASING.md](docs/RELEASING.md)).

## Pull requests

- One topic per PR, with a short description of what changed and how you tested it.
- The **Build** workflow must pass: it builds the aarch64, armv7hl and i486 RPMs. You can
  run the same build locally with `.github/scripts/build-rpm.sh i486`.
- By contributing, you agree that your contribution is licensed under the project's
  [MIT License](LICENSE).
