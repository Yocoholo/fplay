#!/usr/bin/env bash
set -euo pipefail

# build-deb.sh: Create a Debian .deb package for fplay
# Requires: bazel, dpkg-deb, gzip, coreutils. Assumes system has runtime libs.
# Output: packaging/dist/fplay_<version>-1_$(dpkg --print-architecture).deb

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_FILE="$PROJECT_ROOT/BUILD"

# Extract version from Bazel copts macros
major=$(grep -o 'DMAJOR_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
minor=$(grep -o 'DMINOR_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
patch=$(grep -o 'DPATCH_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
version="${major}.${minor}.${patch}"
release=1
arch=$(dpkg --print-architecture 2>/dev/null || echo amd64)

echo "[fplay deb] Building optimized Bazel binary" >&2
(cd "$PROJECT_ROOT" && bazel build //:fplay --compilation_mode=opt)

BIN_PATH="$PROJECT_ROOT/bazel-bin/fplay"
if [ ! -f "$BIN_PATH" ]; then
  echo "Binary not found at $BIN_PATH" >&2
  exit 1
fi

PKG_NAME=fplay
DEB_ROOT="$SCRIPT_DIR/deb-work/${PKG_NAME}_${version}-${release}"
USR_BIN_DIR="$DEB_ROOT/usr/bin"
DOC_DIR="$DEB_ROOT/usr/share/doc/${PKG_NAME}"
LICENSE_DIR="$DEB_ROOT/usr/share/licenses/${PKG_NAME}"
CONTROL_DIR="$DEB_ROOT/DEBIAN"

# Ensure cleanup of deb-work staging directory on exit (successful or error)
trap 'rm -rf "$SCRIPT_DIR/deb-work"' EXIT

rm -rf "$DEB_ROOT"
mkdir -p "$USR_BIN_DIR" "$DOC_DIR" "$LICENSE_DIR" "$CONTROL_DIR"

# Copy binary and docs
install -m 0755 "$BIN_PATH" "$USR_BIN_DIR/$PKG_NAME"
[ -f "$PROJECT_ROOT/README.md" ] && install -m 0644 "$PROJECT_ROOT/README.md" "$DOC_DIR/"
[ -f "$PROJECT_ROOT/LICENSE" ] && install -m 0644 "$PROJECT_ROOT/LICENSE" "$LICENSE_DIR/"

# Basic control file
cat > "$CONTROL_DIR/control" <<EOF
Package: ${PKG_NAME}
Version: ${version}-${release}
Section: video
Priority: optional
Architecture: ${arch}
Maintainer: Unknown <unknown@example.com>
Homepage: https://github.com/yocoholo/fplay
Description: FFmpeg + SDL2 RTSP media player (GPL-2.0-only)
Depends: libsdl2-2.0-0, libavcodec58, libavformat58, libavutil56, libswresample3, libswscale5, libc6, libstdc++6
EOF

# Generate md5sums (optional but nice)
( cd "$DEB_ROOT" && find usr -type f ! -path '*/DEBIAN/*' -print0 | sort -z | xargs -0 md5sum ) > "$CONTROL_DIR/md5sums"

# Compress docs (Debian policy recommends compressing large docs; README likely small, so optional)
# gzip -9 -n "$DOC_DIR"/*.md 2>/dev/null || true

OUT_DIR="$SCRIPT_DIR/dist"
mkdir -p "$OUT_DIR"
DEB_FILE="$OUT_DIR/${PKG_NAME}_${version}-${release}_${arch}.deb"

echo "[fplay deb] Building package: $DEB_FILE" >&2
fakeroot dpkg-deb --build "$DEB_ROOT" "$DEB_FILE"

# Show result
if [ -f "$DEB_FILE" ]; then
  echo "Created: $DEB_FILE"
  echo "Install with: sudo dpkg -i $DEB_FILE"
  echo "Check deps with: apt-get -f install (if needed)"
else
  echo "Failed to create .deb" >&2
  exit 1
fi
