# Tapo Desktop Client

A lightweight, low-latency desktop viewer for **TP-Link Tapo** cameras on Linux, with
**pan/tilt control**, **network auto-discovery**, snapshots and local recording.

It ships as a **single executable**: no installation, no dependencies. Copy it to any
reasonably recent x86-64 Linux desktop (Ubuntu 20.04+, Zorin OS 16+, Mint 20+, Debian 11+,
Fedora 32+, …), make it executable and run it.

## Features

- **Live view over RTSP** with minimal buffering. Decoding is done on the CPU with FFmpeg,
  scaling on the GPU through SDL2/OpenGL.
- **HD / SD switching** (`stream1` / `stream2`). If a stream fails repeatedly, the app
  automatically falls back to the other one. Stalled streams reconnect on their own.
- **Pan / tilt** (C200, C210, C220, C500 …) over ONVIF: hold the arrow keys or WASD, or use the
  on-screen pad. Shift moves at full speed. **Presets** you created in the Tapo app can be
  recalled with keys 1–8.
- **Auto-discovery**: finds cameras through ONVIF WS-Discovery, and falls back to a port scan
  of the local /24 network when multicast is blocked.
- **Multiple cameras**, stored locally.
- **Snapshots** (JPEG) and **recording** (MKV, stream copy without re-encoding, so it costs
  almost no CPU).
- Camera audio (G.711) with mute.
- Fullscreen, auto-hiding controls, and a portable mode for USB sticks.

## Camera setup (once per camera)

Tapo cameras only allow third-party access through a separate **Camera Account**:

1. Open the Tapo app, then select the camera and go to **Settings**, then **Advanced Settings**,
   then **Camera Account**.
2. Choose a user name and a password. This is **not** your TP-Link ID.
3. Use these credentials in Tapo Desktop.

## Usage

Download `tapo-desktop-x86_64-linux` from the
[Releases](https://github.com/m3rtl/tapo-desktop-client-linux/releases) page, then:

```sh
chmod +x tapo-desktop-x86_64-linux
./tapo-desktop-x86_64-linux
```

On first start the camera manager opens and scans your network. Click **Add** next to a found
camera, enter the camera account, then click **Test login** and **Save**.

`./tapo-desktop --install-desktop` adds an entry to your application menu.

| Key | Action |
|---|---|
| Arrow keys / WASD | pan / tilt while held (Shift = fast) |
| Space | stop pan / tilt |
| 1 – 8 | go to preset |
| H / Tab | switch HD / SD |
| M | sound on / off |
| P | snapshot |
| V | start / stop recording |
| F / F11 / double-click | fullscreen |
| PgUp / PgDn | previous / next camera |
| R | reconnect |
| F1 | help |
| Q | quit |

### Command line

```
--config FILE      use this configuration file
--portable         keep the configuration next to the executable (tapo-desktop.ini)
--camera N         start with camera N (1 = first)
--fullscreen       start in fullscreen
--install-desktop  add a menu entry for the current user
--verbose          print FFmpeg diagnostics
```

## Where data is stored

| What | Where |
|---|---|
| Cameras and settings | `~/.config/tapo-desktop/config.ini` (file mode 0600) |
| Portable mode | `tapo-desktop.ini` next to the executable. It is used automatically if it exists. |
| Snapshots and recordings | `~/Videos/Tapo Desktop/` (configurable) |

Camera passwords are stored **in plain text** in that file, readable only by your user. The
app never connects to anything except the cameras you add. There is no cloud, telemetry
or update check.

## Building

The release binary is built with [Zig](https://ziglang.org) as a C/C++ cross-compiler
targeting glibc 2.31. FFmpeg (only RTSP, H.264/H.265, G.711, MJPEG and Matroska), SDL2 and
Dear ImGui are linked statically. SDL loads X11, OpenGL, PulseAudio/PipeWire and ALSA at
runtime, as every desktop provides them.

```sh
./build.sh          # downloads and checksum-verifies all sources, result: dist/tapo-desktop
```

Requirements: a Debian/Ubuntu-based host (it uses `apt-get download` to fetch header
packages), `curl`, `cmake`, `make` and a host C compiler for building nasm. The first build
takes a few minutes.

## Limitations

- Linux x86-64 only. Wayland sessions work through XWayland.
- SD-card playback from the camera is not supported, because Tapo uses a proprietary protocol
  for it.
- Only local network access, no Tapo cloud.

## License

MIT, see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md). FFmpeg is LGPL-2.1: `build.sh` rebuilds the binary from
source with any FFmpeg version you like.

*Not affiliated with or endorsed by TP-Link. "Tapo" is a trademark of TP-Link.*
