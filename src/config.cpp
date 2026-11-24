// Tapo Desktop Client – configuration storage (INI file, mode 0600)
#include "app.h"

#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <climits>
#include <sys/stat.h>
#include <unistd.h>

void logf(const char* fmt, ...) {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    fprintf(stderr, "[tapo-desktop %s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

std::string exe_path() {
    char b[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n <= 0) return "tapo-desktop";
    b[n] = 0;
    return b;
}

std::string exe_dir() {
    std::string p = exe_path();
    size_t s = p.rfind('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}

static std::string home_dir() {
    const char* h = getenv("HOME");
    return (h && *h) ? h : "/tmp";
}

bool mkdirs(const std::string& p, int mode) {
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            std::string part = p.substr(0, i);
            if (mkdir(part.c_str(), mode) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

std::string sanitize_filename(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (isalnum(c) || c == '-' || c == '_' || c >= 0x80) ? (char)c : '_';
    return o.empty() ? "camera" : o;
}

std::string timestamp_now() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S", &tm);
    return b;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string unquote(const std::string& v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
    return v;
}

static std::string quote_if_needed(const std::string& v) {
    if (v.empty()) return v;
    if (isspace((unsigned char)v.front()) || isspace((unsigned char)v.back()) || v.front() == '"' ||
        v.back() == '"' || v.front() == '#' || v.front() == ';')
        return "\"" + v + "\"";
    return v;
}

static bool truthy(const std::string& v) {
    std::string l;
    for (char c : v) l += (char)tolower((unsigned char)c);
    return l == "1" || l == "on" || l == "yes" || l == "true";
}

// XDG_VIDEOS_DIR from ~/.config/user-dirs.dirs, fallback ~/Videos
std::string default_media_dir() {
    std::string dir = home_dir() + "/Videos";
    const char* xc = getenv("XDG_CONFIG_HOME");
    std::string f = (xc && *xc) ? std::string(xc) + "/user-dirs.dirs" : home_dir() + "/.config/user-dirs.dirs";
    if (FILE* fp = fopen(f.c_str(), "r")) {
        char line[1024];
        while (fgets(line, sizeof line, fp)) {
            std::string s = trim(line);
            if (s.rfind("XDG_VIDEOS_DIR=", 0) != 0) continue;
            std::string v = unquote(s.substr(15));
            if (v.rfind("$HOME", 0) == 0) v = home_dir() + v.substr(5);
            if (!v.empty() && v[0] == '/') dir = v;
        }
        fclose(fp);
    }
    return dir + "/Tapo Desktop";
}

void Store::locate(const std::string& explicit_path) {
    if (!explicit_path.empty()) {
        path = explicit_path;
        portable = false;
        return;
    }
    // portable mode: tapo-desktop.ini next to the executable (e.g. on a USB stick)
    std::string p = exe_dir() + "/tapo-desktop.ini";
    if (access(p.c_str(), F_OK) == 0) {
        path = p;
        portable = true;
        return;
    }
    const char* xc = getenv("XDG_CONFIG_HOME");
    std::string base = (xc && *xc) ? xc : home_dir() + "/.config";
    path = base + "/tapo-desktop/config.ini";
    portable = false;
}

bool Store::load() {
    cams.clear();
    set = Settings();
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        set.media_dir = default_media_dir();
        return false;
    }
    char line[4096];
    std::string sec;
    while (fgets(line, sizeof line, f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        if (s[0] == '[') {
            size_t e = s.find(']');
            sec = trim(s.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            for (auto& c : sec) c = (char)tolower((unsigned char)c);
            if (sec == "camera") cams.emplace_back();
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq));
        std::string v = unquote(trim(s.substr(eq + 1)));
        for (auto& c : k) c = (char)tolower((unsigned char)c);

        if (sec == "camera" && !cams.empty()) {
            Camera& c = cams.back();
            if (k == "name") c.name = v;
            else if (k == "host") c.host = v;
            else if (k == "user") c.user = v;
            else if (k == "password") c.password = v;
            else if (k == "rtsp_port") c.rtsp_port = atoi(v.c_str());
            else if (k == "onvif_port") c.onvif_port = atoi(v.c_str());
            else if (k == "stream_hd") c.stream_hd = v;
            else if (k == "stream_sd") c.stream_sd = v;
        } else if (sec == "settings") {
            if (k == "last_camera") set.last_camera = atoi(v.c_str());
            else if (k == "start_quality") set.start_sd = (v == "sd" || v == "SD");
            else if (k == "audio") set.audio = truthy(v);
            else if (k == "ptz_invert") set.ptz_invert = truthy(v);
            else if (k == "ptz_speed") set.ptz_speed = (float)atof(v.c_str());
            else if (k == "decoder_threads") set.decoder_threads = atoi(v.c_str());
            else if (k == "transport") set.transport = (v == "udp") ? "udp" : "tcp";
            else if (k == "startup_timeout") set.startup_timeout = (float)atof(v.c_str());
            else if (k == "stall_timeout") set.stall_timeout = (float)atof(v.c_str());
            else if (k == "failover_after") set.failover_after = atoi(v.c_str());
            else if (k == "media_dir") set.media_dir = (v.rfind("~/", 0) == 0) ? home_dir() + v.substr(1) : v;
        }
    }
    fclose(f);

    for (auto& c : cams) {
        if (c.rtsp_port <= 0) c.rtsp_port = 554;
        if (c.onvif_port <= 0) c.onvif_port = 2020;
        if (c.stream_hd.empty()) c.stream_hd = "stream1";
        if (c.stream_sd.empty()) c.stream_sd = "stream2";
        if (c.name.empty()) c.name = c.host;
    }
    if (set.ptz_speed < 0.1f || set.ptz_speed > 1.0f) set.ptz_speed = 0.5f;
    if (set.decoder_threads < 1 || set.decoder_threads > 16) set.decoder_threads = 2;
    if (set.startup_timeout < 2) set.startup_timeout = 8;
    if (set.stall_timeout < 2) set.stall_timeout = 5;
    if (set.failover_after < 0) set.failover_after = 2;
    if (set.media_dir.empty()) set.media_dir = default_media_dir();
    return true;
}

bool Store::save() const {
    size_t sl = path.rfind('/');
    if (sl != std::string::npos) mkdirs(path.substr(0, sl), 0700);
    std::string tmp = path + ".tmp";
    int old = umask(077);
    FILE* f = fopen(tmp.c_str(), "w");
    umask(old);
    if (!f) {
        logf("cannot write %s: %s", tmp.c_str(), strerror(errno));
        return false;
    }
    fprintf(f, "# Tapo Desktop configuration – contains camera passwords, keep private.\n\n");
    fprintf(f, "[settings]\n");
    fprintf(f, "last_camera = %d\n", set.last_camera);
    fprintf(f, "start_quality = %s\n", set.start_sd ? "sd" : "hd");
    fprintf(f, "audio = %s\n", set.audio ? "on" : "off");
    fprintf(f, "ptz_speed = %.2f\n", set.ptz_speed);
    fprintf(f, "ptz_invert = %s\n", set.ptz_invert ? "on" : "off");
    fprintf(f, "decoder_threads = %d\n", set.decoder_threads);
    fprintf(f, "transport = %s\n", set.transport.c_str());
    fprintf(f, "startup_timeout = %.0f\n", set.startup_timeout);
    fprintf(f, "stall_timeout = %.0f\n", set.stall_timeout);
    fprintf(f, "failover_after = %d\n", set.failover_after);
    fprintf(f, "media_dir = %s\n", quote_if_needed(set.media_dir).c_str());
    for (const auto& c : cams) {
        fprintf(f, "\n[camera]\n");
        fprintf(f, "name = %s\n", quote_if_needed(c.name).c_str());
        fprintf(f, "host = %s\n", c.host.c_str());
        fprintf(f, "user = %s\n", quote_if_needed(c.user).c_str());
        fprintf(f, "password = %s\n", quote_if_needed(c.password).c_str());
        fprintf(f, "rtsp_port = %d\n", c.rtsp_port);
        fprintf(f, "onvif_port = %d\n", c.onvif_port);
        fprintf(f, "stream_hd = %s\n", c.stream_hd.c_str());
        fprintf(f, "stream_sd = %s\n", c.stream_sd.c_str());
    }
    bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    fclose(f);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        logf("cannot save %s", path.c_str());
        unlink(tmp.c_str());
        return false;
    }
    return true;
}
