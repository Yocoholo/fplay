#!/usr/bin/env bash
set -euo pipefail

# Derive version from Bazel BUILD defines (DMAJOR_VERSION, DMINOR_VERSION, DPATCH_VERSION)
BUILD_FILE="../BUILD"
major=$(grep -o 'DMAJOR_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
minor=$(grep -o 'DMINOR_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
patch=$(grep -o 'DPATCH_VERSION=[0-9]\+' "$BUILD_FILE" | cut -d= -f2)
version="${major}.${minor}.${patch}"

workdir=$(dirname "$(pwd)")
packaging_dir="$(pwd)"
spec_file="$packaging_dir/fplay.spec"

# Update Version in spec if different
if ! grep -q "Version:\s*${version}" "$spec_file"; then
    sed -i -E "s/^(Version:\s*).*/\1${version}/" "$spec_file"
fi

echo "Building release binary with Bazel"
cd "$workdir"
bazel build //:fplay --compilation_mode=opt
cd "$packaging_dir"

# Create source tarball layout (includes prebuilt binary for internal packaging)
srcdir="fplay-${version}"
rm -rf "$srcdir" "${srcdir}.tar.gz"
mkdir "$srcdir"
cp -r "$workdir"/*.cpp "$workdir"/*.h "$workdir"/BUILD "$workdir"/WORKSPACE "$workdir"/MODULE.bazel "$srcdir" 2>/dev/null || true
cp "$workdir/bazel-bin/fplay" "$srcdir/"

# (Skipping strip to avoid issues with external debuginfo expectations.)

# Tarball
TARBALL="${srcdir}.tar.gz"
tar -czf "$TARBALL" "$srcdir"
rm -rf "$srcdir"

# rpmbuild directory structure in temp buildroot
RPMBUILD_ROOT=$(mktemp -d)
for d in BUILD RPMS SOURCES SPECS SRPMS; do
  mkdir -p "$RPMBUILD_ROOT/$d"
done
cp "$TARBALL" "$RPMBUILD_ROOT/SOURCES/"
cp "$spec_file" "$RPMBUILD_ROOT/SPECS/"

# Build the binary inside %build via Bazel when rpmbuild executes
rpmbuild --define "_topdir $RPMBUILD_ROOT" -bb "$RPMBUILD_ROOT/SPECS/fplay.spec"

echo "Built RPMs:" && find "$RPMBUILD_ROOT/RPMS" -type f -name '*.rpm'

echo "SRPM:" && find "$RPMBUILD_ROOT/SRPMS" -type f -name '*.src.rpm'

echo "Done. (Temporary build root: $RPMBUILD_ROOT)"
