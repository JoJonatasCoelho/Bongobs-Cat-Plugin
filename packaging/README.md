# Bongobs Cat - Linux packaging

Scripts and helpers to build, bundle and install the plugin on Linux.

## Build (no libobs-dev needed)

Most distributions do not ship a `libobs-dev` package.  This script builds the
plugin anyway: it takes the **real libobs headers** from the obs-studio sources
and links against a tiny import stub that carries the correct SONAME
(`libobs.so.N`).  The resulting plugin declares `DT_NEEDED libobs.so.N` and
resolves every libobs symbol from the OBS installation at load time - exactly
like a plugin built against libobs-dev.

```sh
packaging/build-with-obs-headers.sh --prefix ~/.config/obs-studio
```

The script clones obs-studio (`33.0.1` by default) and simde into
`~/.cache/bongobs-cat` and reuses them afterwards.  Override with `OBS_TAG`,
`SIMDE_TAG`, `CACHE_DIR`, `BUILD_DIR` and `JOBS`.

If you *do* have the libobs development files, a plain CMake build works too:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Build a release bundle

```sh
packaging/make-release.sh
```

The script compiles the plugin with CMake (default build tree: `build-release/`)
and writes:

```
dist/bongobs-cat-linux-<version>/
├── bin/64bit/bongobs-cat.so
├── data/{locale,Bango Cat}
└── install.sh
dist/bongobs-cat-linux-<version>.tar.gz
```

`libobs` development files must be discoverable by CMake. Use
`CMAKE_PREFIX_PATH` or `EXTRA_CMAKE_ARGS` to point at a custom prefix:

```sh
CMAKE_PREFIX_PATH=/opt/obs packaging/make-release.sh
EXTRA_CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/opt/obs -DCMAKE_BUILD_TYPE=RelWithDebInfo' \
    packaging/make-release.sh
```

Other environment variables: `BUILD_DIR`, `BUILD_TYPE`, `PREFIX`, `DIST_DIR`
(see the header of the script). The script is idempotent: it reconfigures,
rebuilds, and recreates the bundle/tarball from scratch.

## Install

Extract the tarball and run the generated installer:

```sh
tar xzf dist/bongobs-cat-linux-<version>.tar.gz
cd bongobs-cat-linux-<version>
./install.sh                 # auto-detect Flatpak / native per-user
./install.sh --dry-run       # show what would be done
./install.sh --dest ~/.config/obs-studio
sudo ./install.sh --system   # /usr/lib/obs-plugins + /usr/share/obs/obs-plugins
```

Auto-detection order:

1. Flatpak: `~/.var/app/com.obsproject.Studio/config/obs-studio`
   (fallback for the OBS 31+ data layout:
   `~/.var/app/com.obsproject.Studio/data/obs-studio`)
2. Native per-user: `~/.config/obs-studio`, or the OBS 31+ XDG data location
   `${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio`
3. System (with `--system`): `/usr/lib/obs-plugins` +
   `/usr/share/obs/obs-plugins/bongobs-cat`

Restart OBS Studio after installing. Per-user installs do not need root.

## Input device access (udev)

The plugin reads mouse/keyboard events from `/dev/input/event*`. Install the
bundled udev rule so the user of the active session gets access without joining
the `input` group:

```sh
sudo cp packaging/udev/99-bongobs-cat-input.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Log out and back in (or reboot) afterwards.

## Continuous integration

`.github/workflows/linux-build.yml` runs on every push/PR:

- `compile-check`: a fast `g++ -fsyntax-only` pass over every plugin source
  against the real `libobs` headers from obs-studio `33.0.1`.
- `build`: full CMake compile/link and `packaging/make-release.sh`, uploading
  the generated tarball as an artifact.
