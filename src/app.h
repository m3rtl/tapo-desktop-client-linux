// Tapo Desktop Client – shared types and helpers
#pragma once
#include <string>
#include <vector>

#define APP_NAME "Tapo Desktop"
#define APP_VERSION "0.1.0"

struct Camera {
    std::string name;
    std::string host;
    std::string user;            // "Camera Account" from the Tapo app
    std::string password;
    int rtsp_port = 554;
    int onvif_port = 2020;
    std::string stream_hd = "stream1";
    std::string stream_sd = "stream2";
};

struct Settings {
    int last_camera = 0;
    bool start_sd = false;        // start with the low-resolution stream
    bool audio = true;
    bool ptz_invert = false;      // ceiling mount
    float ptz_speed = 0.5f;       // 0.1 .. 1.0 (Shift = full speed)
    int decoder_threads = 2;      // 1 = lowest latency, more = less CPU per core
    std::string transport = "tcp";  // tcp | udp
    float startup_timeout = 8.0f; // seconds until the first picture
    float stall_timeout = 5.0f;   // seconds without data => reconnect
    int failover_after = 2;       // failed attempts before switching HD <-> SD
    std::string media_dir;        // snapshots + recordings
};

struct Store {
    std::vector<Camera> cams;
    Settings set;
    std::string path;
    bool portable = false;

    void locate(const std::string& explicit_path);
    bool load();
    bool save() const;
};

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string exe_path();
std::string exe_dir();
std::string default_media_dir();
bool mkdirs(const std::string& path, int mode);
std::string sanitize_filename(const std::string& s);
std::string timestamp_now();  // 2026-01-31_12-00-00
