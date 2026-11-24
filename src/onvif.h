// Tapo Desktop Client – ONVIF: discovery, login check, pan/tilt
#pragma once
#include "app.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Discovered {
    std::string host;
    int onvif_port = 2020;
    std::string model;  // e.g. "C200" (from WS-Discovery scopes)
};

// WS-Discovery multicast probe + TCP sweep of the local /24 networks (port 2020).
std::vector<Discovered> onvif_discover(int timeout_ms);

// Verifies the camera account via ONVIF. Returns true on success; msg is human readable.
bool onvif_check_login(const Camera& cam, std::string& msg);

class Ptz {
public:
    ~Ptz() { close(); }
    void open(const Camera& cam);
    void close();
    void move(float x, float y);  // velocity -1..1; (0,0) = stop
    void goto_preset(int index);

    bool available();
    std::vector<std::string> presets();
    std::string message();

private:
    struct Cmd {
        enum Kind { None, Move, Stop, Preset } kind = None;
        float x = 0, y = 0;
        int index = 0;
    };
    void run();
    bool init();
    bool call(const std::string& body, std::string& resp, int timeout_ms = 4000);
    void set_msg(const std::string& m);

    Camera cam_;
    std::thread th_;
    std::mutex mx_;
    std::condition_variable cv_;
    bool quit_ = false;
    bool has_cmd_ = false;
    Cmd cmd_;

    // guarded by mx_
    bool ok_ = false;
    std::string msg_;
    std::vector<std::string> preset_tokens_, preset_names_;

    // worker-thread only
    bool inited_ = false;
    bool moving_ = false;
    std::string profile_;
    long long offset_ = 0;
};
