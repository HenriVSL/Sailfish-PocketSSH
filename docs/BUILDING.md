# Building

PocketSSH is a Qt 5 / Silica app built with qmake and an rpm spec
(`rpm/harbour-pocketssh.spec`), so the output is an RPM per CPU architecture.
libssh2 and libvterm are compiled into the app from `3rdparty/`; the only extra
system library is OpenSSL 3 (`libcrypto.so.3`), which every Sailfish OS 5 device has.

## Requirements

Either of:

- the **Sailfish SDK** with the `SailfishOS-5.1.0.11` build targets
  (`SailfishOS-5.1.0.11-aarch64`, `-armv7hl`, `-i486`), or
- just **docker**, using the same SDK image as CI (see [Without the SDK](#without-the-sdk-docker)).

The build target's dependencies (Qt 5 dev files, `libsailfishapp`, `openssl-devel`,
`desktop-file-utils`) come from the spec's `BuildRequires`; sfdk installs them into
the build target by itself on the first build.

RPMs built for 5.1 install and run on Sailfish OS 5.1 and later (e.g. a Jolla C2 on 5.2).

## With the Sailfish SDK

In the maintainer's shared SDK workspace, use `sfdev`, which does a shadow build into
`build-<arch>/` with a per-project build target snapshot (see `~/Sailfish/DEVICES.md`):

```sh
sfdev build emu        # i486 for the emulator  -> build-i486/RPMS/
sfdev build phone      # aarch64 for the phone  -> build-aarch64/RPMS/
```

`sfdev` only knows the architectures of the test devices; for armv7hl use plain sfdk.

Plain sfdk works anywhere inside your SDK workspace directory:

```sh
mkdir -p build-armv7hl && cd build-armv7hl
sfdk -c target=SailfishOS-5.1.0.11-armv7hl build ..
ls RPMS/
```

The release binary is stripped in `%install` (the SDK leaves it unstripped). For a
build you can debug with gdb, keep the symbols:

```sh
sfdev build emu -- --define "pocketssh_keep_symbols 1"
```

## Without the SDK (docker)

CI builds with `.github/scripts/build-rpm.sh`, which runs the same `mb2` shadow build
inside the Sailfish OS Platform SDK docker image (pinned by digest). It works on any
Linux machine with docker, with no SDK installed:

```sh
.github/scripts/build-rpm.sh i486        # or aarch64 / armv7hl
ls RPMS/
```

The first run downloads a ~3 GB image (~13 GB unpacked) per architecture.

## Installing on the emulator or a device

With `sfdev` (it takes the device lock for you):

```sh
sfdev install emu build-i486/RPMS/harbour-pocketssh-1.0.0-1.i486.rpm
sfdev ssh emu1 -- 'invoker --type=silica-qt5 -s /usr/bin/harbour-pocketssh &'
sfdev install phone build-aarch64/RPMS/harbour-pocketssh-1.0.0-1.aarch64.rpm
```

Without `sfdev`, use `sfdk deploy --sdk` from the build directory, or copy the RPM to the
device and install it as described in [RELEASING.md](RELEASING.md#installing-a-release-rpm).

To see the app's log output, start it from a terminal on the device:
`/usr/bin/harbour-pocketssh` (or through `invoker` as above).

## CI

GitHub Actions ([`.github/workflows/`](../.github/workflows/)):

| Workflow | Runs on | Does |
|---|---|---|
| `build.yml` (**Build**) | push to `main`, pull requests, manual run | Builds the aarch64, armv7hl and i486 RPMs and uploads them as artifacts (`harbour-pocketssh-<arch>`) |
| `release.yml` (**Release**) | push of a `v*` tag | Checks the tag against the spec, runs the same builds, publishes a GitHub Release with the RPMs (see [RELEASING.md](RELEASING.md)) |

Both use `.github/scripts/build-rpm.sh`, so a CI build is the same as a local
`sfdk build` against the 5.1.0.11 targets. No secrets are needed. To get a test RPM
from a branch, open the workflow run and download its artifacts.

The SDK images are [CODeRUS's Platform SDK images](https://hub.docker.com/r/coderus/sailfishos-platform-sdk-aarch64)
(built from Jolla's official SDK tooling and target tarballs,
[source](https://github.com/CODeRUS/docker-sailfishos-platform-sdk)). To move to a
newer Sailfish OS SDK release, change `SFOS_RELEASE` and the three image digests in
`build-rpm.sh` together; the script's header says where to look up the digests.

## App icon

The launcher icons in `icons/<size>/harbour-pocketssh.png` are generated from the
Pocket Shell (iOS) icon in `icons/src/`:

```sh
tools/make-icons.sh
```

It cuts the artwork into the Sailfish app icon shape (three round corners, one nearly
square corner, transparent outside) and renders 86, 108, 128 and 172 px. It needs
ImageMagick 7 (`magick`) and `rsvg-convert`.
