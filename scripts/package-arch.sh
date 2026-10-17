#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
packaging="$root/packaging"
pkgbuild="$packaging/PKGBUILD"

[[ -f "$pkgbuild" ]] || { echo "PKGBUILD not found: $pkgbuild" >&2; exit 1; }
command -v makepkg >/dev/null || { echo "makepkg not found; install base-devel" >&2; exit 1; }

pkgver="$(sed -n 's/^pkgver=//p' "$pkgbuild" | head -n1)"
pkgrel="$(sed -n 's/^pkgrel=//p' "$pkgbuild" | head -n1)"
tarball="$packaging/mini-ocr-$pkgver.tar.gz"

if [[ ! -f "$root/models/ch_PP-OCRv5_det_mobile.onnx" ]]; then
  echo "Models not found; downloading..."
  "$root/scripts/fetch-models.sh"
fi

echo "Creating source tarball: $tarball"
rm -f "$tarball"
tar -C "$root" \
  --exclude='./build' \
  --exclude='./build-*' \
  --exclude='./dist' \
  --exclude='./packaging' \
  --exclude='./.git' \
  --exclude='*.pkg.tar.*' \
  --transform="s,^\\./,mini-ocr-$pkgver/," \
  -czf "$tarball" .

echo "Building package with makepkg..."
cd "$packaging"
rm -f mini-ocr-*.pkg.tar.*
makepkg -f

pkgfile="$(find "$packaging" -maxdepth 1 -name "mini-ocr-$pkgver-$pkgrel-*.pkg.tar.zst" -print -quit)"
if [[ -n "$pkgfile" ]]; then
  echo
  echo "Package: $pkgfile"
  echo "Install with: sudo pacman -U \"$pkgfile\""
else
  echo "Package built, but the output file was not found." >&2
  exit 1
fi
