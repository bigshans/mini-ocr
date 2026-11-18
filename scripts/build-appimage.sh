#!/usr/bin/env bash
# Build self-contained Linux bundles for Mini OCR: an AppImage and a portable
# .tar.gz. Both bundle Qt, OpenCV, Tesseract and the OCR models, so they run on
# any reasonably recent glibc distribution without depending on the system
# OpenCV version (which changes sonames on every minor release).
#
# Usage:
#   ./scripts/build-appimage.sh                          # both
#   MAKE_APPIMAGE=0 ./scripts/build-appimage.sh          # tarball only
#   MAKE_TARBALL=0 ./scripts/build-appimage.sh           # AppImage only
#   BUILD_DIR=/tmp/mini-ocr-bundle ./scripts/build-appimage.sh
#
# Requirements: cmake, gcc, qt6-base, qt6-wayland (optional but recommended),
# opencv, tesseract, curl or wget, and network access on the first run to
# download linuxdeploy/appimagetool into $BUILD_DIR/tools.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build-appimage}"
appdir="$build_dir/AppDir"
tools="$build_dir/tools"
output="$build_dir/Mini_OCR-x86_64.AppImage"
version="$(sed -n 's/^project(mini-ocr VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -n1)"
version="${version:-0.0.0}"
portable_name="mini-ocr-$version-linux-x86_64"
portable_stage="$build_dir/portable/$portable_name"
portable_output="$build_dir/$portable_name.tar.gz"
make_appimage="${MAKE_APPIMAGE:-1}"
make_tarball="${MAKE_TARBALL:-1}"

# linuxdeploy/appimagetool are themselves AppImages; extracting them avoids a
# hard dependency on FUSE while building.
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
# Arch's binutils produce .relr.dyn sections that the bundled strip cannot
# read; stripping is unnecessary (system libraries are already stripped).
export NO_STRIP=1

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

for cmd in cmake curl; do
    command -v "$cmd" >/dev/null 2>&1 || die "required command not found: $cmd"
done

qmake=""
for candidate in qmake6 qmake-qt6 /usr/lib/qt6/bin/qmake6; do
    if command -v "$candidate" >/dev/null 2>&1; then
        qmake="$(command -v "$candidate")"
        break
    fi
done
[[ -n "$qmake" ]] || die "Qt6 qmake not found (install qt6-base / qt6-base-devel)"
export QMAKE="$qmake"

mkdir -p "$tools"

fetch() {
    local url="$1" dest="$2"
    if [[ -x "$dest" ]]; then
        return
    fi
    log "downloading $(basename "$dest")"
    curl -fsSL --retry 3 -o "$dest" "$url" || die "download failed: $url"
    chmod +x "$dest"
}

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
      "$tools/linuxdeploy"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
      "$tools/linuxdeploy-plugin-qt"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
      "$tools/appimagetool"

export PATH="$tools:$PATH"

log "building mini-ocr"
cmake -S "$root" -B "$build_dir/cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DMINI_OCR_INSTALL_LAUNCHER=OFF
cmake --build "$build_dir/cmake" -j"$(nproc)"

log "installing into AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build_dir/cmake"

[[ -f "$root/models/ch_PP-OCRv5_det_mobile.onnx" ]] || \
    die "models missing; run ./scripts/fetch-models.sh first"

# AppImage metadata at the AppDir root.
cp "$appdir/usr/share/applications/mini-ocr.desktop" "$appdir/"
cp "$appdir/usr/share/icons/hicolor/scalable/apps/mini-ocr.svg" "$appdir/"
ln -sf mini-ocr.svg "$appdir/.DirIcon"

log "deploying shared libraries"
linuxdeploy --appdir "$appdir" \
    --executable "$appdir/usr/bin/mini-ocr" \
    --desktop-file "$appdir/mini-ocr.desktop" \
    --icon-file "$appdir/mini-ocr.svg"

log "deploying Qt plugins"
# The PostgreSQL/MySQL/ODBC/... SQL drivers link client libraries that are
# usually not installed and are irrelevant for Mini OCR's SQLite history.
qt_excludes=()
for driver in ibase psql mysql oci odbc mimer; do
    qt_excludes+=("--exclude-library=libqsql${driver}.so")
done
linuxdeploy-plugin-qt --appdir="$appdir" "${qt_excludes[@]}"

# linuxdeploy-plugin-qt only deploys the platform plugin of the current
# session. Copy the Wayland plugins explicitly so the bundle works on both
# X11 and Wayland sessions.
qt_plugins="/usr/lib/qt6/plugins"
wayland_plugins=(
    "$qt_plugins/platforms/libqwayland.so"
    "$qt_plugins/wayland-shell-integration/libxdg-shell.so"
    "$qt_plugins/wayland-shell-integration/libwl-shell-plugin.so"
    "$qt_plugins/wayland-shell-integration/libqt-shell.so"
    "$qt_plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
    "$qt_plugins/wayland-graphics-integration-client/libshm-emulation-server.so"
    "$qt_plugins/wayland-decoration-client/libbradient.so"
)
if [[ -e "$qt_plugins/platforms/libqwayland.so" ]]; then
    log "adding Wayland platform plugins"
    for plugin in "${wayland_plugins[@]}"; do
        [[ -f "$plugin" ]] || continue
        rel="${plugin#"$qt_plugins"/}"
        mkdir -p "$(dirname "$appdir/usr/plugins/$rel")"
        cp "$plugin" "$appdir/usr/plugins/$rel"
        # Resolve the plugin's own dependencies into usr/lib. linuxdeploy copies
        # the given library into usr/lib as well; drop that duplicate, the
        # plugin itself already lives in usr/plugins.
        linuxdeploy --appdir "$appdir" --library "$plugin"
        rm -f "$appdir/usr/lib/$(basename "$plugin")"
    done
else
    log "qt6-wayland not installed; the AppImage will use X11/XWayland only"
fi

# Trim things the app does not use.
rm -rf "$appdir/usr/translations"
find "$appdir/usr/plugins/imageformats" -maxdepth 1 -name 'kimg_*' -delete 2>/dev/null || true

# linuxdeploy creates AppRun as a symlink to the executable; replace it with a
# real file (writing through the symlink would clobber the binary).
rm -f "$appdir/AppRun"
[[ "$(head -c 4 "$appdir/usr/bin/mini-ocr" | od -An -tx1 | tr -d ' \n')" == "7f454c46" ]] || \
    die "$appdir/usr/bin/mini-ocr is not an ELF binary"

cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
set -u
HERE=$(dirname "$(readlink -f "$0")")
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$HERE/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export MINI_OCR_MODEL_DIR="$HERE/usr/share/mini-ocr/models"
exec "$HERE/usr/bin/mini-ocr" "$@"
EOF
chmod +x "$appdir/AppRun"

if [[ "$make_appimage" == 1 ]]; then
    log "creating AppImage"
    rm -f "$output"
    appimagetool --no-appstream "$appdir" "$output"
    log "AppImage: $output ($(du -h "$output" | cut -f1))"
fi

if [[ "$make_tarball" == 1 ]]; then
    log "creating portable tarball"
    rm -rf "$portable_stage"
    mkdir -p "$portable_stage"
    cp -a "$appdir/usr" "$portable_stage/"
    cp "$appdir/AppRun" "$portable_stage/mini-ocr"
    # Make autostart point at this script, not at the extracted usr/bin path.
    sed -i 's|^exec "$HERE/usr/bin/mini-ocr" "$@"|export MINI_OCR_LAUNCHER="$HERE/mini-ocr"\nexec "$HERE/usr/bin/mini-ocr" "$@"|' \
        "$portable_stage/mini-ocr"
    cat > "$portable_stage/README.txt" <<'EOF'
Mini OCR - portable Linux build
===============================

Run:
    ./mini-ocr

Everything needed (Qt, OpenCV, Tesseract, OCR models) is in this directory,
so the system OpenCV version does not matter.

Requirements: glibc not older than the machine this bundle was built on,
a running X11 or Wayland session.

The app stores settings in ~/.config/mini-ocr and history in
~/.local/share/mini-ocr.
EOF
    rm -f "$portable_output"
    tar -C "$build_dir/portable" -czf "$portable_output" "$portable_name"
    log "portable: $portable_output ($(du -h "$portable_output" | cut -f1))"
fi

log "done"
