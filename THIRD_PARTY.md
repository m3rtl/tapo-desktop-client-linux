# Third-party components

The release binary statically links the following libraries:

| Component | Version | License | Source |
|---|---|---|---|
| FFmpeg (libavformat, libavcodec, libswscale, libavutil) | 7.1 | LGPL-2.1-or-later | https://ffmpeg.org |
| SDL2 | 2.30.9 | zlib | https://libsdl.org |
| Dear ImGui | 1.91.5 | MIT | https://github.com/ocornut/imgui |
| LLVM libc++ / libc++abi (via Zig) | Zig 0.13.0 | Apache-2.0 WITH LLVM-exception | https://ziglang.org |

FFmpeg is built without any GPL or non-free components (see the `configure` line in
`build.sh`). In line with the LGPL, `build.sh` lets you rebuild and relink the
application against a modified FFmpeg.
