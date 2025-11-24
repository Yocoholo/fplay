# fplay RPM Packaging

This directory contains initial assets to build an RPM for `fplay`.

## Files
- `fplay.spec`: RPM spec skeleton (update License and URL before publishing).
- `build-rpm.sh`: Helper script to derive version from Bazel `BUILD` defines and perform a local `rpmbuild`.

## Requirements
Install prerequisite tools:
```
sudo dnf install -y rpm-build bazel gcc ffmpeg-devel SDL2-devel
```
(Adjust for your distribution.)

## Build Steps
1. Ensure version macros in `BUILD` (`DMAJOR_VERSION`, `DMINOR_VERSION`, `DPATCH_VERSION`) are correct.
2. Run:
```
chmod +x packaging/build-rpm.sh
./packaging/build-rpm.sh
```
3. Resulting RPM and SRPM paths are printed at the end.

## Next Improvements
- Add license file and update `License:` field.
- Add desktop entry & man page.
- Integrate `mock` for clean-chroot builds.
- CI automation with rpmlint and signing.
- Auto creation of source tarball from git archive instead of raw copy.

## Mock Build (future)
Example (Fedora):
```
mock -r fedora-41-x86_64 --rebuild /path/to/fplay-<version>-1.src.rpm
```

## Version Sync
The script updates `Version:` in spec if it differs from derived `<major>.<minor>.<patch>`.
