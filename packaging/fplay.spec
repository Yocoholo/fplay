Name:           fplay
Version:        1.0.0
Release:        1%{?dist}
Summary:        FFmpeg + SDL2 RTSP media player
License:        GPL-2.0-only
URL:            https://github.com/yocoholo/fplay
Source0:        %{name}-%{version}.tar.gz
Requires:       ffmpeg-libs, SDL2
%global debug_package %{nil}

%description
Lightweight media player built with FFmpeg and SDL2.

%prep
%setup -q

%build
echo "Prebuilt binary included in source archive."

%install
install -D -m 0755 fplay %{buildroot}/usr/bin/fplay

%files
%license LICENSE
%doc README.md
/usr/bin/fplay

%changelog
* Tue Nov 25 2025 Packager <packager@example.com> - 1.0.0-1
- Align spec License field with repository GPLv2 license text.
