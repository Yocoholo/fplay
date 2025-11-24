Name:           fplay
Version:        1.0.0
Release:        1%{?dist}
Summary:        FFmpeg + SDL2 media player
License:        FIXME
URL:            https://example.com/fplay
Source0:        %{name}-%{version}.tar.gz
# Bazel build performed before rpmbuild; no BuildRequires for bazel here.
Requires:       ffmpeg-libs, SDL2
%global debug_package %{nil}

%description
Lightweight media player built with FFmpeg and SDL2.

%prep
%setup -q

%build
# Binary already built (via Bazel) and included in source archive.

%install
install -D -m 0755 fplay %{buildroot}/usr/bin/fplay

%files
/usr/bin/fplay

%changelog
* Mon Nov 24 2025 Packager <packager@example.com> - 1.0.0-1
- Initial RPM build.
