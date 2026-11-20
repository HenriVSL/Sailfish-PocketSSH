# Releasing

Releases are built and published by GitHub Actions when a version tag is pushed.
Nothing needs to be built or uploaded by hand.

## 1. Bump the version

The version lives in one place, `rpm/harbour-pocketssh.spec`:

```
Version:    1.1.0
Release:    1
```

Use `Release: 1` for every new version; raise it only to re-release the same version
with packaging-only changes.

## 2. Add a changelog entry

Add an entry at the **top** of `rpm/harbour-pocketssh.changes` (below the comment
lines). sfdk turns this file into the RPM's `%changelog`, and the release workflow uses
the newest entry as the GitHub release notes.

```
* Mon Dec 14 2026 Henri Lavikainen <henri.lavikainen@gmail.com> 1.1.0-1
- Short, user-facing description of what changed.
- One line per change; indent continuation lines by two spaces.

* Fri Nov 20 2026 Henri Lavikainen <henri.lavikainen@gmail.com> 1.0.0-1
- ...
```

The header must be `* <Day> <Mon> <DD> <YYYY> <name> <<email>> <version>-<release>`, the
weekday must match the date (`date -d 2026-12-14 +%a`), and entries are separated by a
blank line. Markdown in the bullet text shows up in the release notes.

## 3. Build and test

Build and try the RPM on the emulator and, for anything hardware-related, a phone
(see [BUILDING.md](BUILDING.md)). Then commit:

```sh
git commit -am "Release 1.1.0"
git push
```

and check that the **Build** workflow on `main` is green.

## 4. Tag and push

```sh
git tag -a v1.1.0 -m "PocketSSH 1.1.0"
git push origin v1.1.0
```

The tag must be `v` + the spec's `Version`.

## 5. What CI does

The **Release** workflow (`.github/workflows/release.yml`):

1. checks that the tag is `v<Version>` and that the newest `.changes` entry is for that
   version, and stops with an error saying what to fix if not;
2. builds the aarch64, armv7hl and i486 RPMs (the same job as the **Build** workflow);
3. creates the GitHub Release `v1.1.0` with the three RPMs and `THIRD_PARTY_NOTICES.md`
   attached, and the newest changelog entry plus install instructions as notes.

If the check fails, fix the spec or changelog, commit, and move the tag:

```sh
git tag -d v1.1.0 && git push origin :refs/tags/v1.1.0
git tag -a v1.1.0 -m "PocketSSH 1.1.0" && git push origin v1.1.0
```

If a release was already created and you need to redo it, delete it first in the GitHub
UI (or `gh release delete v1.1.0`), then re-push the tag as above.

## Installing a release RPM

Pick the RPM for your device's architecture: `aarch64` for 64-bit ARM phones (Jolla C2,
Xperia 10 II and newer), `armv7hl` for 32-bit ARM, `i486` for the SDK emulator.
`uname -m` on the device prints `aarch64`, `armv7l` or `i686`.

**With the GUI:** in **Settings ▸ Untrusted software**, allow installing untrusted
software. Download the RPM with the browser (or copy it to the device over USB), open it
from the browser's downloads or a file manager such as File Browser, and confirm the
installation.

**From a terminal** (Settings ▸ Developer tools ▸ Developer mode, then the Terminal app or
ssh):

```sh
devel-su pkcon install-local ~/Downloads/harbour-pocketssh-1.1.0-1.aarch64.rpm
```

`pkcon` pulls in any missing dependencies (only system packages such as
`openssl-libs` are needed, which every Sailfish OS 5 device already has).

**With Storeman:** if PocketSSH is published on OpenRepos, install and update it from
there with Storeman instead.

Updating works the same way: install the newer RPM over the old one. The app's data in
your home directory (saved hosts, trusted host keys, settings) is not touched by an
update.
