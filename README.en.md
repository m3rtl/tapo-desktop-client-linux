# Tapo Desktop Client

[Deutsch](README.md) · **English**

A lightweight, low-latency desktop viewer for **TP-Link Tapo** cameras on Linux. It can
**pan and tilt** the camera, finds cameras **automatically on your network**, and saves
snapshots and recordings locally.

It ships as a **single file**: there is nothing to install and no dependencies. It runs on
practically any current 64-bit Linux desktop, for example Ubuntu 20.04+, Zorin OS 16+,
Linux Mint 20+, Debian 11+ and Fedora 32+.

The user interface is in German by default. Switch it under **Einstellungen > Sprache >
English**, or start the program with `--lang en`.

## Step by step: getting your camera picture

All you need is your phone with the Tapo app and your Linux computer. Both must be on the
**same Wi-Fi / home network** as the camera.

### Step 1: Create a camera account in the Tapo app

Tapo cameras only let other programs in with a separate login, the **Camera Account**. This
is **not** your regular TP-Link account that you use to sign in to the app.

1. Open the **Tapo app** on your phone.
2. Tap your **camera** so that the live view appears.
3. Tap the **gear icon** (settings) in the top right corner.
4. Tap **Advanced Settings**.
5. Tap **Camera Account**.
6. Choose a **user name** and a **password** and enter both. Depending on the app version,
   the password must be at least 6 to 8 characters long.
7. Tap **Save** and **write both down**, because you will need them in a moment.

If you have several cameras, repeat this for each one. Using the same credentials
everywhere is the easiest option.

### Step 2: Download the program

1. Open the [**Releases**](https://github.com/m3rtl/tapo-desktop-client-linux/releases) page.
2. Under the newest entry, download the file **`tapo-desktop-x86_64-linux`**.
3. Put the file wherever you like, for example in a new folder "Tapo" in your home folder.

### Step 3: Make the program executable (only once)

1. In the file manager, **right-click** the file and choose **Properties**.
2. Turn on **"Executable as Program"** (or "Allow executing file as program"). Depending on
   your system, it is on the **Permissions** tab.
3. Close the window.

You can also do this in a terminal: `chmod +x tapo-desktop-x86_64-linux`

### Step 4: Start the program and add your camera

1. **Double-click** the file. A window opens, and the camera manager (**"Kameras"** in
   German, **"Cameras"** in English) starts scanning your network right away.
2. After a few seconds your camera appears in the list, for example "C200". Click **Add**
   next to it.
3. Enter the **user** and **password** from step 1. You can change the name to anything you
   like, for example "Living room".
4. Click **Test login**. A green **"Login OK"** means everything is fine.
5. Click **Save**. After a few seconds you see the live picture.

Next time there is nothing to enter, because the program remembers your cameras.

### Step 5: Using it

- **Move the camera:** hold the **arrow keys** or use the pad in the bottom right corner.
  Hold **Shift** to move faster.
- **Fullscreen:** double-click the picture or press **F**. **Esc** brings you back.
- **Snapshot:** press **P**.
- **Recording:** press **V**. Snapshots and videos are saved in `Videos/Tapo Desktop` in
  your home folder.
- **Sound:** press **M**.
- If the toolbar at the top is hidden, move the mouse briefly and it comes back.
- **F1** shows all keyboard shortcuts.

### Optional: add it to your application menu

Open a terminal in the folder that contains the file and run:

```sh
./tapo-desktop-x86_64-linux --install-desktop
```

"Tapo Desktop" now appears in your application menu. If you move the file later, run the
command again.

### If something doesn't work

| Problem | Solution |
|---|---|
| "No cameras found" | The computer and the camera must be on the same network, so not a guest Wi-Fi. Alternatively, use "Add camera manually" and enter the IP address. You can find it in the Tapo app under camera > gear icon > Device Info. |
| "Login failed" | Use the user name and password of the **Camera Account** (step 1), not your TP-Link login. After several failed attempts the camera locks you out briefly, so wait a few minutes. |
| Picture stutters or freezes | Press **H** to switch to the smaller SD stream. The program also switches on its own when HD keeps failing. |
| Arrow keys move the wrong way | Enable "Invert directions (ceiling mount)" in the **Settings**. |

Tip: Set up your router so that the camera always gets the same IP address. Depending on
the router, this is called "DHCP reservation" or "always assign the same IP address".

## Features

- **Live view over RTSP** with minimal buffering. Decoding is done on the CPU with FFmpeg,
  scaling on the GPU through SDL2/OpenGL.
- **HD / SD switching** (`stream1` / `stream2`). If a stream fails repeatedly, the app
  automatically falls back to the other one. Stalled streams reconnect on their own.
- **Pan / tilt** over ONVIF, for example on the C200, C210, C220 and C500: use the keys or
  the on-screen pad, with Shift for full speed. **Presets** you saved in the Tapo app are
  recalled with keys 1–8.
- **Auto-discovery** through ONVIF WS-Discovery. When multicast is blocked, the app also
  scans the local /24 network for the ONVIF port.
- **Multiple cameras**, stored locally.
- **Snapshots** (JPEG) and **recording** (MKV). Recording copies the stream without
  re-encoding, so it costs almost no CPU.
- Camera audio (G.711) with mute.
- Fullscreen, auto-hiding controls, and a portable mode for USB sticks.
- User interface in **German** (default) and **English**.

## Keyboard shortcuts

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

## Command line

```
--config FILE      use this configuration file
--portable         keep the configuration next to the executable (tapo-desktop.ini)
--camera N         start with camera N (1 = first)
--fullscreen       start in fullscreen
--lang de|en       user interface language (default: German)
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

**Portable mode**, for example for a USB stick or a second computer: put the executable and
a `tapo-desktop.ini` in the same folder. The program then reads and saves its settings
there. See [config.example.ini](config.example.ini) for an example.

## Building

The release binary is built with [Zig](https://ziglang.org) as a C/C++ cross-compiler
targeting glibc 2.31. FFmpeg (only RTSP, H.264/H.265, G.711, MJPEG and Matroska), SDL2 and
Dear ImGui are linked statically. SDL loads X11, OpenGL, PulseAudio/PipeWire and ALSA at
runtime, as every desktop provides them.

```sh
./build.sh          # downloads and checksum-verifies all sources, result: dist/tapo-desktop
```

Requirements: a Debian/Ubuntu-based host (it uses `apt-get download` to fetch header
packages), `curl`, `cmake`, `make` and a C compiler for building nasm. The first build takes
a few minutes. GitHub Actions builds a release automatically for every `v*` tag.

**Adding a language:** add a translation table to `src/i18n.cpp` and register the
language in `languages()`.

## Limitations

- Linux x86-64 only. Wayland sessions work through XWayland.
- SD-card playback from the camera is not supported, because Tapo uses a proprietary
  protocol for it.
- Only local network access, no Tapo cloud.

## License

MIT, see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md). FFmpeg is LGPL-2.1: `build.sh` rebuilds the program from
source with any FFmpeg version you like.

*Not affiliated with or endorsed by TP-Link. "Tapo" is a trademark of TP-Link.*
