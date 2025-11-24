# fplay

FFmpeg + SDL2 RTSP media player.

## Build

Requires:
- Bazel 8.4+
- ffmpeg development headers
- SDL2 development headers
- C++17 compiler

```bash
bazel build //:fplay
```

## Install Locally

```bash
cp bazel-bin/fplay ~/.local/bin/
```

## Usage

```bash
fplay -i 192.168.1.10 -s live.sdp
fplay --ip 192.168.1.10 --port 554 --stream camera/stream1
fplay --help
fplay --version
```

## RPM Packaging

See [packaging/README.packaging.md](packaging/README.packaging.md) for instructions on building RPM packages.

## License

MIT License. See source header.
