#!/usr/bin/env bash
#
# Build the Bongobs Cat OBS plugin and assemble a self-contained Linux release
# bundle:
#
#   dist/bongobs-cat-linux-<version>/
#     bin/64bit/bongobs-cat.so
#     data/{locale,Bango Cat}
#     install.sh
#   dist/bongobs-cat-linux-<version>.tar.gz
#
# Environment variables (all optional):
#   BUILD_DIR          Build tree (default: <repo>/build-release)
#   BUILD_TYPE         CMake build type (default: Release)
#   PREFIX             Install prefix used by "cmake --install" (default:
#                      <BUILD_DIR>/install-root).  The bundle is populated from
#                      <PREFIX>/plugins/bongobs-cat.
#   DIST_DIR           Output directory (default: <repo>/dist)
#   EXTRA_CMAKE_ARGS   Extra arguments passed to the CMake configure step,
#                      space separated (e.g. '-DCMAKE_PREFIX_PATH=/opt/libobs').
#   CMAKE_PREFIX_PATH  Convenience shortcut, forwarded as
#                      -DCMAKE_PREFIX_PATH=<value>.
#
# Examples:
#   # normal build against an installed libobs
#   packaging/make-release.sh
#
#   # build against a custom libobs prefix
#   CMAKE_PREFIX_PATH=/opt/obs packaging/make-release.sh
#
#   # offline/CI harness (real libobs headers + import stub from /tmp/opencode)
#   EXTRA_CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/tmp/opencode/obs-stub' \
#       packaging/make-release.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION="$(
  sed -nE 's/^[[:space:]]*project\([^)]*VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
    "$REPO_ROOT/CMakeLists.txt" | head -n1
)"
: "${VERSION:=0.0.0}"

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-release}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
PREFIX="${PREFIX:-$BUILD_DIR/install-root}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"

BUNDLE_NAME="bongobs-cat-linux-$VERSION"
BUNDLE_DIR="$DIST_DIR/$BUNDLE_NAME"
TARBALL="$DIST_DIR/$BUNDLE_NAME.tar.gz"

# Split EXTRA_CMAKE_ARGS on whitespace.  This is intentional and documented.
EXTRA_ARGS=()
if [[ -n "${EXTRA_CMAKE_ARGS:-}" ]]; then
  read -r -a EXTRA_ARGS <<<"$EXTRA_CMAKE_ARGS"
fi

echo "==> Bongobs Cat release $VERSION"
echo "    repo:      $REPO_ROOT"
echo "    build:     $BUILD_DIR"
echo "    prefix:    $PREFIX"
echo "    dist:      $DIST_DIR"

CMAKE_ARGS=(
  -S "$REPO_ROOT"
  -B "$BUILD_DIR"
  "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
  "-DCMAKE_INSTALL_PREFIX=$PREFIX"
)
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
fi
if ((${#EXTRA_ARGS[@]})); then
  CMAKE_ARGS+=("${EXTRA_ARGS[@]}")
fi

echo "==> Configuring"
cmake "${CMAKE_ARGS[@]}"

echo "==> Building"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(nproc 2>/dev/null || echo 2)"

echo "==> Installing into $PREFIX"
cmake --install "$BUILD_DIR" --prefix "$PREFIX" --config "$BUILD_TYPE"

PLUGIN_ROOT="$PREFIX/plugins/bongobs-cat"
PLUGIN_SO="$PLUGIN_ROOT/bin/64bit/bongobs-cat.so"

if [[ ! -f "$PLUGIN_SO" ]]; then
  echo "error: build did not produce $PLUGIN_SO" >&2
  exit 1
fi
if [[ ! -d "$PLUGIN_ROOT/data/locale" || ! -d "$PLUGIN_ROOT/data/Bango Cat" ]]; then
  echo "error: expected data directory not found under $PLUGIN_ROOT/data" >&2
  exit 1
fi

echo "==> Assembling bundle $BUNDLE_DIR"
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/bin/64bit" "$BUNDLE_DIR/data"
cp "$PLUGIN_SO" "$BUNDLE_DIR/bin/64bit/bongobs-cat.so"
cp -a "$PLUGIN_ROOT/data/." "$BUNDLE_DIR/data/"

# ---------------------------------------------------------------------------
# install.sh (generated, version and bundle contents are fixed at build time)
# ---------------------------------------------------------------------------
cat >"$BUNDLE_DIR/install.sh" <<'INSTALL_EOF'
#!/usr/bin/env bash
#
# Bongobs Cat installer for Linux.
#
# Layouts understood by OBS Studio:
#   * native, per-user : <config root>/plugins/bongobs-cat/{bin,data}
#   * Flatpak          : ~/.var/app/com.obsproject.Studio/config/obs-studio
#                        (new OBS 31+ data layout:
#                         ~/.var/app/com.obsproject.Studio/data/obs-studio)
#   * native, XDG      : $XDG_DATA_HOME/obs-studio (OBS 31+ data layout)
#   * system           : /usr/lib/obs-plugins/bongobs-cat.so
#                        /usr/share/obs/obs-plugins/bongobs-cat/ (needs root)
#
# Usage:
#   ./install.sh                # auto-detect (Flatpak > user > XDG data)
#   ./install.sh --system       # install system-wide (requires root)
#   ./install.sh --dest <dir>   # install under <dir>/plugins/bongobs-cat
#   ./install.sh --dry-run      # only print what would be done
#
set -euo pipefail

VERSION="@VERSION@"
BUNDLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DEST=""
DRY_RUN=0
SYSTEM=0

usage() {
  # Print the header comment block (everything up to "set -euo pipefail").
  sed -n '2,/^set -euo pipefail/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while (($#)); do
  case "$1" in
    --dest)
      [[ $# -ge 2 ]] || { echo "error: --dest needs a directory" >&2; exit 2; }
      DEST="$2"; shift 2 ;;
    --dest=*)
      DEST="${1#--dest=}"; shift ;;
    --dry-run|-n)
      DRY_RUN=1; shift ;;
    --system)
      SYSTEM=1; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

run() {
  if ((DRY_RUN)); then
    printf 'would run:'; printf ' %q' "$@"; printf '\n'
  else
    "$@"
  fi
}

install_user_layout() {
  local root="$1"
  local plugin="$root/plugins/bongobs-cat"
  echo "==> Installing for user layout: $plugin"
  run rm -rf "$plugin"
  run mkdir -p "$plugin/bin/64bit"
  run cp "$BUNDLE_DIR/bin/64bit/bongobs-cat.so" "$plugin/bin/64bit/bongobs-cat.so"
  run mkdir -p "$plugin/data"
  run cp -a "$BUNDLE_DIR/data/." "$plugin/data/"
}

install_system_layout() {
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "error: --system requires root (try: sudo $0 --system)" >&2
    exit 1
  fi
  echo "==> Installing system-wide"
  run install -Dm755 "$BUNDLE_DIR/bin/64bit/bongobs-cat.so" \
    "/usr/lib/obs-plugins/bongobs-cat.so"
  run rm -rf "/usr/share/obs/obs-plugins/bongobs-cat"
  run mkdir -p "/usr/share/obs/obs-plugins/bongobs-cat"
  run cp -a "$BUNDLE_DIR/data/." "/usr/share/obs/obs-plugins/bongobs-cat/"
}

pick_target() {
  local xdg_data="${XDG_DATA_HOME:-$HOME/.local/share}"
  local fp_cfg="$HOME/.var/app/com.obsproject.Studio/config/obs-studio"
  local fp_data="$HOME/.var/app/com.obsproject.Studio/data/obs-studio"

  # Flatpak: prefer the classic config location, fall back to the OBS 31+ data
  # location used inside the sandbox (exposed on the host as .../data/...).
  if [[ -d "$fp_cfg" ]]; then
    echo "flatpak:$fp_cfg"; return
  fi
  if [[ -d "$fp_data" ]]; then
    echo "flatpak:$fp_data"; return
  fi

  # Native OBS: classic config root, then the OBS 31+ XDG data root.
  if [[ -d "$HOME/.config/obs-studio" ]]; then
    echo "native:$HOME/.config/obs-studio"; return
  fi
  if [[ -d "$xdg_data/obs-studio" ]]; then
    echo "native:$xdg_data/obs-studio"; return
  fi
  if [[ "$SYSTEM" -eq 1 ]]; then
    echo "system:"; return
  fi
  if [[ -d "$HOME/.var/app" ]]; then
    echo "flatpak:$fp_cfg"; return
  fi

  # Nothing detected: default to the classic native user location.
  echo "native:$HOME/.config/obs-studio"
}

if [[ -n "$DEST" ]]; then
  install_user_layout "$DEST"
elif ((SYSTEM)); then
  install_system_layout
else
  target="$(pick_target)"
  kind="${target%%:*}"
  root="${target#*:}"
  case "$kind" in
    system) install_system_layout ;;
    *)      install_user_layout "$root" ;;
  esac
fi

echo
echo "Bongobs Cat $VERSION installed."
echo "Restart OBS Studio so the plugin is loaded."
INSTALL_EOF

# Substitute the version placeholder (the rest of install.sh is literal).
sed -i "s/@VERSION@/$VERSION/" "$BUNDLE_DIR/install.sh"
chmod +x "$BUNDLE_DIR/install.sh"

echo "==> Creating $TARBALL"
rm -f "$TARBALL"
tar czf "$TARBALL" -C "$DIST_DIR" "$BUNDLE_NAME"

echo
echo "Done:"
echo "  bundle : $BUNDLE_DIR"
echo "  archive: $TARBALL"
