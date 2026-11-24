#!/usr/bin/env bash
# Builds dist/tapo-desktop: one self-contained x86_64 Linux binary.
#
# FFmpeg (RTSP/H.264/H.265/G.711 only), SDL2 and Dear ImGui are linked statically.
# The binary is built with Zig against glibc 2.31, so it runs on Ubuntu 20.04+,
# Zorin OS 16+, Linux Mint 20+, Debian 11+, Fedora 32+ etc. without installing anything.
# At runtime SDL loads X11 / OpenGL / PulseAudio (PipeWire) / ALSA dynamically –
# these are present on every Linux desktop.
#
# Requirements for building: bash, curl, tar, xz, make, cmake, a host C compiler (for nasm),
# and apt-get (Debian/Ubuntu-based host) to fetch the X11/Pulse/ALSA header packages.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$ROOT/build}"
JOBS="${JOBS:-$(nproc)}"
GLIBC="${GLIBC:-2.31}"
TARGET="x86_64-linux-gnu.$GLIBC"

DL="$WORK/dl"; SRC="$WORK/src"; OBJ="$WORK/obj"; PREFIX="$WORK/prefix"; TOOLS="$WORK/tools"; HDR="$WORK/hdr"
mkdir -p "$DL" "$SRC" "$OBJ" "$PREFIX" "$TOOLS/bin" "$HDR" "$ROOT/dist"

ZIG_VER=0.13.0
FFMPEG_VER=7.1
SDL_VER=2.30.9
IMGUI_VER=1.91.5
NASM_VER=2.16.03

fetch() {  # url sha256 filename
    local url="$1" sha="$2" file="$DL/$3"
    if [ ! -f "$file" ]; then
        echo ">> downloading $3"
        curl -fL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --quiet - || { echo "checksum mismatch: $file" >&2; rm -f "$file"; exit 1; }
}
unpack() {  # archive dirname
    [ -d "$SRC/$2" ] || tar -xf "$DL/$1" -C "$SRC"
}

fetch "https://ziglang.org/download/$ZIG_VER/zig-linux-x86_64-$ZIG_VER.tar.xz" \
      d45312e61ebcc48032b77bc4cf7fd6915c11fa16e4aad116b66c9468211230ea zig-$ZIG_VER.tar.xz
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz" \
      40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6 ffmpeg-$FFMPEG_VER.tar.xz
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" \
      24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4 SDL2-$SDL_VER.tar.gz
fetch "https://github.com/ocornut/imgui/archive/refs/tags/v$IMGUI_VER.tar.gz" \
      2aa2d169c569368439e5d5667e0796d09ca5cc6432965ce082e516937d7db254 imgui-$IMGUI_VER.tar.gz
fetch "https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VER/nasm-$NASM_VER.tar.xz" \
      1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148 nasm-$NASM_VER.tar.xz

unpack zig-$ZIG_VER.tar.xz zig-linux-x86_64-$ZIG_VER
unpack ffmpeg-$FFMPEG_VER.tar.xz ffmpeg-$FFMPEG_VER
unpack SDL2-$SDL_VER.tar.gz SDL2-$SDL_VER
unpack imgui-$IMGUI_VER.tar.gz imgui-$IMGUI_VER
unpack nasm-$NASM_VER.tar.xz nasm-$NASM_VER

ZIG="$SRC/zig-linux-x86_64-$ZIG_VER/zig"
export ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache" ZIG_LOCAL_CACHE_DIR="$WORK/zig-cache"

# compiler wrappers: zig cc targeting old glibc and the baseline x86-64 CPU.
# -ffile-prefix-map keeps local build paths (home directory, user name) out of the binary.
PREFIX_MAP="-ffile-prefix-map=$WORK=/build -ffile-prefix-map=$ROOT=/src"
cat > "$TOOLS/bin/zcc" <<EOF
#!/bin/sh
exec "$ZIG" cc -target $TARGET -mcpu=baseline $PREFIX_MAP "\$@"
EOF
cat > "$TOOLS/bin/zc++" <<EOF
#!/bin/sh
exec "$ZIG" c++ -target $TARGET -mcpu=baseline $PREFIX_MAP "\$@"
EOF
printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$TOOLS/bin/zar"
printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$TOOLS/bin/zranlib"
chmod +x "$TOOLS/bin/"*
export PATH="$TOOLS/bin:$PATH"

# ---------------------------------------------------------------- nasm (host tool)
if [ ! -x "$TOOLS/bin/nasm" ]; then
    echo ">> building nasm"
    mkdir -p "$OBJ/nasm" && cd "$OBJ/nasm"
    "$SRC/nasm-$NASM_VER/configure" --prefix="$TOOLS" >/dev/null
    make -j"$JOBS" >/dev/null && make install >/dev/null
fi

# ------------------------------------------- X11 / PulseAudio / ALSA headers (no libs linked)
HDR_STAMP=2
if [ "$(cat "$HDR/.done" 2>/dev/null)" != "$HDR_STAMP" ]; then
    rm -rf "$HDR" "$WORK/debs" "$PREFIX/lib/libSDL2.a" "$OBJ/sdl" && mkdir -p "$HDR"
    echo ">> fetching header packages"
    mkdir -p "$WORK/debs" && cd "$WORK/debs"
    apt-get download libx11-dev x11proto-dev libxext-dev libxrandr-dev libxrender-dev libxcursor-dev \
        libxi-dev libxfixes-dev libxss-dev libpulse-dev libasound2-dev libgl-dev libglx-dev libegl-dev \
        libgles-dev mesa-common-dev >/dev/null
    for d in *.deb; do dpkg-deb -x "$d" "$HDR"; done
    # SDL derives the runtime sonames from the .so symlinks – point them at the real host libraries
    LIBDIR="$HDR/usr/lib/x86_64-linux-gnu"
    for l in "$LIBDIR"/*.so; do
        t="$(readlink "$l" || true)"
        [ -n "$t" ] || continue
        for sys in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64; do
            if [ -e "$sys/$t" ]; then ln -sf "$sys/$t" "$l"; break; fi
        done
    done
    missing=""
    for n in X11 Xext Xcursor Xi Xfixes Xrandr Xrender Xss pulse asound; do
        [ -e "$LIBDIR/lib$n.so" ] || missing="$missing lib$n"
    done
    if [ -n "$missing" ]; then
        echo "!! runtime libraries missing on this build host:$missing" >&2
        echo "   (needed only to determine sonames; Debian/Ubuntu: sudo apt-get install libx11-6 libxext6" >&2
        echo "   libxcursor1 libxi6 libxfixes3 libxrandr2 libxrender1 libxss1 libpulse0 libasound2)" >&2
        exit 1
    fi
    echo "$HDR_STAMP" > "$HDR/.done"
fi

# --------------------------------------------------------------------------- SDL2
if [ ! -f "$PREFIX/lib/libSDL2.a" ]; then
    echo ">> building SDL2"
    PKG_CONFIG_LIBDIR="$HDR/usr/lib/x86_64-linux-gnu/pkgconfig:$HDR/usr/share/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$HDR" PKG_CONFIG_PATH="" \
    cmake -S "$SRC/SDL2-$SDL_VER" -B "$OBJ/sdl" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=zcc -DCMAKE_AR="$TOOLS/bin/zar" -DCMAKE_RANLIB="$TOOLS/bin/zranlib" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$HDR/usr" \
        -DCMAKE_LIBRARY_PATH="$HDR/usr/lib/x86_64-linux-gnu" -DCMAKE_INCLUDE_PATH="$HDR/usr/include" \
        -DCMAKE_C_FLAGS="-I$HDR/usr/include" -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
        -DSDL_X11=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF \
        -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_RPI=OFF -DSDL_DIRECTFB=OFF \
        -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
        -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF \
        -DSDL_LIBSAMPLERATE=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_HIDAPI=OFF \
        -DSDL_HAPTIC=OFF -DSDL_JOYSTICK=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF \
        > "$OBJ/sdl-configure.log"
    SDL_CFG="$(cat "$OBJ"/sdl/include-config-*/SDL2/SDL_config.h "$OBJ"/sdl/include/SDL2/SDL_config.h 2>/dev/null || true)"
    for def in SDL_VIDEO_DRIVER_X11_DYNAMIC SDL_AUDIO_DRIVER_PULSEAUDIO_DYNAMIC SDL_AUDIO_DRIVER_ALSA_DYNAMIC; do
        if ! grep -q "define $def " <<<"$SDL_CFG"; then
            echo "!! SDL configure: $def not set – the binary would not be portable (see $OBJ/sdl-configure.log)" >&2
            rm -rf "$OBJ/sdl"
            exit 1
        fi
    done
    cmake --build "$OBJ/sdl" -j"$JOBS" >/dev/null
    cmake --install "$OBJ/sdl" >/dev/null
fi

# ------------------------------------------------------------------------- FFmpeg
if [ ! -f "$PREFIX/lib/libavformat.a" ]; then
    echo ">> building FFmpeg (minimal)"
    mkdir -p "$OBJ/ffmpeg" && cd "$OBJ/ffmpeg"
    # neutral --prefix: FFmpeg embeds its configure line in the library
    "$SRC/ffmpeg-$FFMPEG_VER/configure" --prefix=/usr/local \
        --cc=zcc --cxx=zc++ --ar=zar --ranlib=zranlib --x86asmexe=nasm \
        --enable-static --disable-shared --enable-pic --enable-pthreads \
        --disable-autodetect --disable-programs --disable-doc --disable-debug \
        --disable-avdevice --disable-avfilter --disable-swresample \
        --disable-everything \
        --enable-protocol=tcp,udp,rtp,file \
        --enable-demuxer=rtsp,sdp,rtp \
        --enable-decoder=h264,hevc,pcm_alaw,pcm_mulaw \
        --enable-parser=h264,hevc \
        --enable-encoder=mjpeg \
        --enable-muxer=matroska \
        > "$OBJ/ffmpeg-configure.log"
    # zig's glibc stubs still export sysctl(), but <sys/sysctl.h> is gone since glibc 2.32
    sed -i 's/^#define HAVE_SYSCTL 1/#define HAVE_SYSCTL 0/' config.h
    make -j"$JOBS" >/dev/null
    make install DESTDIR="$WORK/ffstage" >/dev/null
    cp -a "$WORK/ffstage/usr/local/include/." "$PREFIX/include/"
    cp -a "$WORK/ffstage/usr/local/lib/"*.a "$PREFIX/lib/"
fi

# ------------------------------------------------------------------- tapo-desktop
echo ">> building tapo-desktop"
IMGUI="$SRC/imgui-$IMGUI_VER"
cd "$ROOT"
zc++ -std=c++17 -O2 -DNDEBUG -Wall -Wno-unused-function \
    -I"$PREFIX/include" -I"$PREFIX/include/SDL2" -I"$IMGUI" -I"$IMGUI/backends" -idirafter "$HDR/usr/include" \
    src/main.cpp src/player.cpp src/onvif.cpp src/config.cpp src/i18n.cpp \
    "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" \
    "$IMGUI/backends/imgui_impl_sdl2.cpp" "$IMGUI/backends/imgui_impl_sdlrenderer2.cpp" \
    "$IMGUI/misc/cpp/imgui_stdlib.cpp" \
    "$PREFIX/lib/libavformat.a" "$PREFIX/lib/libavcodec.a" "$PREFIX/lib/libswscale.a" \
    "$PREFIX/lib/libavutil.a" "$PREFIX/lib/libSDL2.a" \
    -lm -lpthread -ldl -lrt -s \
    -o dist/tapo-desktop

# privacy check: no local paths or user names in the release binary
PATTERNS=(-e "$HOME" -e "$ROOT" -e "$WORK" -e "/home/")
[ -z "${CI:-}" ] && PATTERNS+=(-e "$(id -un)")   # CI user ("runner") is not private
LEAKS="$(strings -n 4 dist/tapo-desktop | grep -F "${PATTERNS[@]}" || true)"
if [ -n "$LEAKS" ]; then
    echo "!! local paths/user name found in dist/tapo-desktop – not fit for release:" >&2
    echo "$LEAKS" | head -20 >&2
    exit 1
fi

echo
echo "done: $ROOT/dist/tapo-desktop ($(du -h dist/tapo-desktop | cut -f1)) – privacy check passed"
