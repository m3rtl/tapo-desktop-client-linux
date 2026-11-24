// Tapo Desktop Client – RTSP player thread (FFmpeg), audio (SDL), recording
#pragma once
#include "app.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct AVFrame;
struct AVFormatContext;
struct AVPacket;

class Player {
public:
    explicit Player(uint32_t wake_event) : wake_event_(wake_event) {}
    ~Player() { close(); }

    void open(const Camera& cam, const Settings& s, bool sd);
    void close();

    void set_quality(bool sd);
    bool quality_sd() const { return sd_; }
    void reconnect() { reconnect_req_ = true; }
    void set_muted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }
    bool live() const { return live_; }
    std::string status();

    // newest decoded frame (caller owns it, free with av_frame_free) or nullptr
    AVFrame* fetch_frame();

    // recording into <dir>/<prefix>_<timestamp>.mkv (stream copy, no re-encoding)
    void set_recording(bool on, const std::string& dir, const std::string& prefix);
    bool recording_wanted() const { return rec_want_; }
    std::string recording_file();

private:
    void run();
    void session(bool& got_frame);
    void set_status(const std::string& s);
    void publish(AVFrame* f);
    void rec_open(AVFormatContext* in, int vi, int ai);
    void rec_write(AVFormatContext* in, AVPacket* pkt, int vi, int ai);
    void rec_close();
    static int interrupt_cb(void* opaque);

    uint32_t wake_event_;
    Camera cam_;
    Settings set_;
    std::thread th_;
    std::atomic<bool> quit_{false}, reconnect_req_{false}, sd_{false}, muted_{false}, live_{false};
    std::atomic<bool> timed_out_{false};
    std::atomic<int64_t> last_io_{0}, io_timeout_{0};

    std::mutex mx_;
    std::string status_;
    AVFrame* frame_ = nullptr;
    bool frame_new_ = false;

    std::atomic<bool> rec_want_{false};
    std::string rec_dir_, rec_prefix_, rec_file_;  // guarded by mx_
    AVFormatContext* rec_ = nullptr;               // player thread only
    int rec_map_[2] = {-1, -1};
    int64_t rec_t0_ = 0;
    bool rec_wait_key_ = true;
};

// Encodes a frame as JPEG. Returns false and sets err on failure.
bool save_jpeg(const AVFrame* frame, const std::string& path, std::string& err);
