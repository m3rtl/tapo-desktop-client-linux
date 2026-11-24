// Tapo Desktop Client – minimal ONVIF client (SOAP over HTTP, WS-Security digest)
#include "onvif.h"

extern "C" {
#include <libavutil/base64.h>
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

// ------------------------------------------------------------------ helpers

static void random_bytes(unsigned char* b, size_t n) {
    std::random_device rd;
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)rd();
}

static std::string uuid4() {
    unsigned char b[16];
    random_bytes(b, 16);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    char s[40];
    snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
             b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

static std::string xml_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default: o += c;
        }
    }
    return o;
}

// text content of the first element with the given local name (any namespace prefix)
static std::string xml_text(const std::string& s, const std::string& local, size_t from = 0, size_t* end = nullptr) {
    size_t p = from;
    while ((p = s.find(local, p)) != std::string::npos) {
        bool start_ok = p > 0 && (s[p - 1] == ':' || s[p - 1] == '<');
        size_t q = p + local.size();
        if (start_ok && q < s.size() && (s[q] == '>' || s[q] == ' ')) {
            // make sure it is an opening tag
            size_t lt = s.rfind('<', p);
            if (lt != std::string::npos && s[lt + 1] != '/') {
                size_t gt = s.find('>', q);
                if (gt == std::string::npos) return "";
                size_t close = s.find('<', gt + 1);
                if (close == std::string::npos) return "";
                if (end) *end = close;
                return s.substr(gt + 1, close - gt - 1);
            }
        }
        p = q;
    }
    return "";
}

static std::string attr(const std::string& s, size_t tag_pos, const std::string& name) {
    size_t gt = s.find('>', tag_pos);
    size_t p = s.find(name + "=\"", tag_pos);
    if (p == std::string::npos || (gt != std::string::npos && p > gt)) return "";
    p += name.size() + 2;
    size_t q = s.find('"', p);
    return q == std::string::npos ? "" : s.substr(p, q - p);
}

static int tcp_connect(const std::string& host, int port, int timeout_ms) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0) return -1;
    int fd = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) continue;
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            struct pollfd pf = {fd, POLLOUT, 0};
            r = -1;
            if (poll(&pf, 1, timeout_ms) == 1) {
                int err = 0;
                socklen_t l = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
                if (err == 0) r = 0;
            }
        }
        if (r == 0) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
            struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool send_all(int fd, const char* p, size_t n) {
    while (n) {
        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static std::string wsse_header(const std::string& user, const std::string& pw, long long offset) {
    unsigned char nonce[16];
    random_bytes(nonce, sizeof nonce);
    time_t t = time(nullptr) + offset;
    struct tm tm;
    gmtime_r(&t, &tm);
    char created[40];
    strftime(created, sizeof created, "%Y-%m-%dT%H:%M:%SZ", &tm);

    uint8_t digest[20];
    struct AVSHA* sha = av_sha_alloc();
    av_sha_init(sha, 160);
    av_sha_update(sha, nonce, sizeof nonce);
    av_sha_update(sha, (const uint8_t*)created, strlen(created));
    av_sha_update(sha, (const uint8_t*)pw.data(), pw.size());
    av_sha_final(sha, digest);
    av_free(sha);

    char d64[64], n64[64];
    av_base64_encode(d64, sizeof d64, digest, sizeof digest);
    av_base64_encode(n64, sizeof n64, nonce, sizeof nonce);

    return std::string("<s:Header><Security xmlns=\"http://docs.oasis-open.org/wss/2004/01/"
                       "oasis-200401-wss-wssecurity-secext-1.0.xsd\"><UsernameToken><Username>") +
           xml_escape(user) +
           "</Username><Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">" +
           d64 +
           "</Password><Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">" +
           n64 +
           "</Nonce><Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-utility-1.0.xsd\">" +
           created + "</Created></UsernameToken></Security></s:Header>";
}

// POST a SOAP request; returns HTTP status (0 = network error). resp = full HTTP response.
static int soap_post(const std::string& host, int port, const std::string& body, const std::string* user,
                     const std::string* pw, long long offset, std::string& resp, int timeout_ms) {
    std::string env = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">";
    if (user && pw) env += wsse_header(*user, *pw, offset);
    env += "<s:Body>" + body + "</s:Body></s:Envelope>";

    resp.clear();
    int fd = tcp_connect(host, port, timeout_ms);
    if (fd < 0) return 0;
    char hdr[512];
    int hl = snprintf(hdr, sizeof hdr,
                      "POST /onvif/service HTTP/1.1\r\nHost: %s:%d\r\n"
                      "Content-Type: application/soap+xml; charset=utf-8\r\n"
                      "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                      host.c_str(), port, env.size());
    if (!send_all(fd, hdr, (size_t)hl) || !send_all(fd, env.data(), env.size())) {
        ::close(fd);
        return 0;
    }
    char buf[8192];
    for (;;) {
        ssize_t r = recv(fd, buf, sizeof buf, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        resp.append(buf, (size_t)r);
        if (resp.size() > (4u << 20)) break;
    }
    ::close(fd);
    int code = 0;
    if (sscanf(resp.c_str(), "HTTP/%*s %d", &code) != 1) return 0;
    return code;
}

static long long camera_time_offset(const std::string& host, int port) {
    std::string r;
    if (soap_post(host, port, "<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>", nullptr,
                  nullptr, 0, r, 3000) != 200)
        return 0;
    size_t p = r.find("UTCDateTime");
    if (p == std::string::npos) return 0;
    struct tm tm{};
    tm.tm_year = atoi(xml_text(r, "Year", p).c_str()) - 1900;
    tm.tm_mon = atoi(xml_text(r, "Month", p).c_str()) - 1;
    tm.tm_mday = atoi(xml_text(r, "Day", p).c_str());
    tm.tm_hour = atoi(xml_text(r, "Hour", p).c_str());
    tm.tm_min = atoi(xml_text(r, "Minute", p).c_str());
    tm.tm_sec = atoi(xml_text(r, "Second", p).c_str());
    if (tm.tm_year < 100) return 0;
    time_t cam = timegm(&tm);
    long long off = (long long)cam - (long long)time(nullptr);
    return (off > -5 && off < 5) ? 0 : off;
}

static std::string first_profile_token(const std::string& r) {
    size_t p = 0;
    while ((p = r.find("Profiles", p)) != std::string::npos) {
        if (p > 0 && (r[p - 1] == ':' || r[p - 1] == '<') && r[p + 8] == ' ') {
            std::string t = attr(r, p, "token");
            if (!t.empty()) return t;
        }
        p += 8;
    }
    return "";
}

static std::string describe_failure(int code, const std::string& r) {
    if (code == 0) return "camera not reachable";
    if (code == 401 || r.find("NotAuthorized") != std::string::npos ||
        r.find("FailedAuthentication") != std::string::npos)
        return "login failed – check the camera account (user/password)";
    return "camera answered with HTTP " + std::to_string(code);
}

bool onvif_check_login(const Camera& cam, std::string& msg) {
    long long off = camera_time_offset(cam.host, cam.onvif_port);
    std::string r;
    int code = soap_post(cam.host, cam.onvif_port, "<GetProfiles xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/>",
                         &cam.user, &cam.password, off, r, 4000);
    if (code != 200) {
        msg = describe_failure(code, r);
        return false;
    }
    bool ptz = r.find("PTZConfiguration") != std::string::npos;
    msg = std::string("Login OK") + (ptz ? " – pan/tilt supported" : " – no pan/tilt");
    return true;
}

// ---------------------------------------------------------------- discovery

struct Iface {
    in_addr_t addr, mask;  // network byte order
    std::string name;
};

static std::vector<Iface> ipv4_interfaces() {
    std::vector<Iface> v;
    struct ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return v;
    for (auto* i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask) continue;
        if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        std::string n = i->ifa_name;
        if (n.rfind("docker", 0) == 0 || n.rfind("veth", 0) == 0 || n.rfind("br-", 0) == 0 ||
            n.rfind("virbr", 0) == 0)
            continue;
        Iface f;
        f.addr = ((struct sockaddr_in*)i->ifa_addr)->sin_addr.s_addr;
        f.mask = ((struct sockaddr_in*)i->ifa_netmask)->sin_addr.s_addr;
        f.name = n;
        v.push_back(f);
    }
    freeifaddrs(ifs);
    return v;
}

static std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

static void ws_discovery(std::vector<Discovered>& out, std::mutex& m, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    unsigned char ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    std::string probe =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
        "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\"><e:Header><w:MessageID>uuid:" +
        uuid4() +
        "</w:MessageID><w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
        "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
        "</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>";
    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);

    auto send_probe = [&] {
        auto ifs = ipv4_interfaces();
        if (ifs.empty()) {
            sendto(fd, probe.data(), probe.size(), 0, (struct sockaddr*)&dst, sizeof dst);
            return;
        }
        for (auto& i : ifs) {
            struct in_addr a;
            a.s_addr = i.addr;
            setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof a);
            sendto(fd, probe.data(), probe.size(), 0, (struct sockaddr*)&dst, sizeof dst);
        }
    };
    send_probe();

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool resent = false;
    std::vector<char> buf(65536);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long el = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (el >= timeout_ms) break;
        if (!resent && el > 600) {  // UDP may get lost – probe twice
            send_probe();
            resent = true;
        }
        struct pollfd pf = {fd, POLLIN, 0};
        if (poll(&pf, 1, 100) != 1) continue;
        ssize_t n = recv(fd, buf.data(), buf.size() - 1, 0);
        if (n <= 0) continue;
        std::string r(buf.data(), (size_t)n);
        std::string xaddrs = xml_text(r, "XAddrs");
        std::string scopes = xml_text(r, "Scopes");
        // first http://a.b.c.d[:port]/ address
        Discovered d;
        size_t p = 0;
        while ((p = xaddrs.find("http://", p)) != std::string::npos) {
            p += 7;
            size_t e = xaddrs.find_first_of("/ ", p);
            std::string hp = xaddrs.substr(p, e == std::string::npos ? std::string::npos : e - p);
            std::string h = hp;
            int port = 80;
            size_t c = hp.find(':');
            if (c != std::string::npos) {
                h = hp.substr(0, c);
                port = atoi(hp.c_str() + c + 1);
            }
            struct in_addr tmp;
            if (inet_pton(AF_INET, h.c_str(), &tmp) == 1) {
                d.host = h;
                d.onvif_port = port;
                break;
            }
        }
        if (d.host.empty()) continue;
        for (const char* key : {"/hardware/", "/name/"}) {
            size_t s = scopes.find(key);
            if (s != std::string::npos && d.model.empty()) {
                s += strlen(key);
                size_t e = scopes.find(' ', s);
                d.model = url_decode(scopes.substr(s, e == std::string::npos ? std::string::npos : e - s));
            }
        }
        std::lock_guard<std::mutex> lk(m);
        bool dup = false;
        for (auto& o : out)
            if (o.host == d.host) {
                dup = true;
                if (o.model.empty()) o.model = d.model;
            }
        if (!dup) out.push_back(d);
    }
    ::close(fd);
}

// Fallback when multicast is blocked: try TCP 2020 on every host of the local /24 networks
static void tcp_sweep(std::vector<Discovered>& out, std::mutex& m) {
    const int port = 2020;
    std::vector<std::string> candidates;
    for (auto& i : ipv4_interfaces()) {
        uint32_t ip = ntohl(i.addr), mask = ntohl(i.mask);
        if (__builtin_popcount(mask) < 24) mask = 0xFFFFFF00u;
        uint32_t net = ip & mask, bc = net | ~mask;
        if (bc - net > 256) continue;
        std::vector<std::pair<int, uint32_t>> socks;
        for (uint32_t h = net + 1; h < bc; h++) {
            if (h == ip) continue;
            int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) break;
            struct sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(h);
            int r = connect(fd, (struct sockaddr*)&a, sizeof a);
            if (r == 0 || errno == EINPROGRESS) socks.push_back({fd, h});
            else ::close(fd);
        }
        std::vector<struct pollfd> pfs;
        for (auto& s : socks) pfs.push_back({s.first, POLLOUT, 0});
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        std::vector<bool> done(socks.size(), false);
        size_t remaining = socks.size();
        while (remaining > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long el = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
            if (el >= 900) break;
            if (poll(pfs.data(), pfs.size(), (int)(900 - el)) <= 0) break;
            for (size_t k = 0; k < pfs.size(); k++) {
                if (done[k] || !pfs[k].revents) continue;
                done[k] = true;
                remaining--;
                int err = 1;
                socklen_t l = sizeof err;
                getsockopt(pfs[k].fd, SOL_SOCKET, SO_ERROR, &err, &l);
                if (err == 0) {
                    char s[INET_ADDRSTRLEN];
                    struct in_addr a;
                    a.s_addr = htonl(socks[k].second);
                    inet_ntop(AF_INET, &a, s, sizeof s);
                    candidates.push_back(s);
                }
                pfs[k].fd = -pfs[k].fd - 1;  // ignore from now on
            }
        }
        for (auto& s : socks) ::close(s.first);
    }
    for (auto& h : candidates) {
        {
            std::lock_guard<std::mutex> lk(m);
            bool known = false;
            for (auto& o : out) known |= o.host == h;
            if (known) continue;
        }
        std::string r;
        if (soap_post(h, port, "<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>", nullptr,
                      nullptr, 0, r, 1500) == 200 &&
            r.find("GetSystemDateAndTimeResponse") != std::string::npos) {
            std::lock_guard<std::mutex> lk(m);
            bool known = false;
            for (auto& o : out) known |= o.host == h;
            if (!known) out.push_back({h, port, ""});
        }
    }
}

std::vector<Discovered> onvif_discover(int timeout_ms) {
    std::vector<Discovered> out;
    std::mutex m;
    std::thread sweep([&] { tcp_sweep(out, m); });
    ws_discovery(out, m, timeout_ms);
    sweep.join();
    std::sort(out.begin(), out.end(), [](const Discovered& a, const Discovered& b) {
        struct in_addr x{}, y{};
        inet_pton(AF_INET, a.host.c_str(), &x);
        inet_pton(AF_INET, b.host.c_str(), &y);
        return ntohl(x.s_addr) < ntohl(y.s_addr);
    });
    return out;
}

// ---------------------------------------------------------------------- PTZ

void Ptz::open(const Camera& cam) {
    close();
    cam_ = cam;
    {
        std::lock_guard<std::mutex> lk(mx_);
        quit_ = false;
        has_cmd_ = false;
        ok_ = false;
        msg_ = "Pan/tilt: connecting…";
        preset_tokens_.clear();
        preset_names_.clear();
    }
    inited_ = false;
    moving_ = false;
    th_ = std::thread(&Ptz::run, this);
}

void Ptz::close() {
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mx_);
        quit_ = true;
    }
    cv_.notify_all();
    th_.join();
}

void Ptz::move(float x, float y) {
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mx_);
        cmd_ = Cmd();
        cmd_.kind = (x == 0 && y == 0) ? Cmd::Stop : Cmd::Move;
        cmd_.x = x;
        cmd_.y = y;
        has_cmd_ = true;
    }
    cv_.notify_all();
}

void Ptz::goto_preset(int index) {
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(mx_);
        cmd_ = Cmd();
        cmd_.kind = Cmd::Preset;
        cmd_.index = index;
        has_cmd_ = true;
    }
    cv_.notify_all();
}

bool Ptz::available() {
    std::lock_guard<std::mutex> lk(mx_);
    return ok_;
}

std::vector<std::string> Ptz::presets() {
    std::lock_guard<std::mutex> lk(mx_);
    return preset_names_;
}

std::string Ptz::message() {
    std::lock_guard<std::mutex> lk(mx_);
    return msg_;
}

void Ptz::set_msg(const std::string& m) {
    std::lock_guard<std::mutex> lk(mx_);
    msg_ = m;
}

bool Ptz::call(const std::string& body, std::string& resp, int timeout_ms) {
    int code = soap_post(cam_.host, cam_.onvif_port, body, &cam_.user, &cam_.password, offset_, resp, timeout_ms);
    if (code != 200) {
        set_msg("Pan/tilt: " + describe_failure(code, resp));
        logf("ONVIF request failed (HTTP %d)", code);
        return false;
    }
    return true;
}

bool Ptz::init() {
    offset_ = camera_time_offset(cam_.host, cam_.onvif_port);
    std::string r;
    if (!call("<GetProfiles xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/>", r)) return false;
    profile_ = first_profile_token(r);
    if (profile_.empty()) {
        set_msg("Pan/tilt: no media profile found");
        return false;
    }
    inited_ = true;
    if (r.find("PTZConfiguration") == std::string::npos) {
        set_msg("This camera has no pan/tilt");
        return true;
    }
    std::vector<std::string> tokens, names;
    if (call("<GetPresets xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>" + xml_escape(profile_) +
                 "</ProfileToken></GetPresets>",
             r)) {
        size_t p = 0;
        while ((p = r.find("Preset ", p)) != std::string::npos) {
            if (p > 0 && (r[p - 1] == ':' || r[p - 1] == '<')) {
                std::string tok = attr(r, p, "token");
                size_t end = 0;
                std::string name = xml_text(r, "Name", p, &end);
                if (!tok.empty()) {
                    tokens.push_back(tok);
                    names.push_back(name.empty() ? "Preset " + tok : name);
                }
            }
            p += 7;
        }
    }
    std::lock_guard<std::mutex> lk(mx_);
    ok_ = true;
    msg_.clear();
    preset_tokens_ = tokens;
    preset_names_ = names;
    logf("pan/tilt ready (%zu presets)", names.size());
    return true;
}

void Ptz::run() {
    init();
    for (;;) {
        Cmd c;
        {
            std::unique_lock<std::mutex> lk(mx_);
            cv_.wait(lk, [&] { return quit_ || has_cmd_; });
            if (quit_) break;
            c = cmd_;
            has_cmd_ = false;
        }
        if (!inited_ && !init()) continue;
        if (!available()) continue;
        std::string r, prof = "<ProfileToken>" + xml_escape(profile_) + "</ProfileToken>";
        char v[160];
        switch (c.kind) {
            case Cmd::Move:
                snprintf(v, sizeof v, "<PanTilt xmlns=\"http://www.onvif.org/ver10/schema\" x=\"%.2f\" y=\"%.2f\"/>",
                         c.x, c.y);
                if (call("<ContinuousMove xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\">" + prof + "<Velocity>" + v +
                             "</Velocity></ContinuousMove>",
                         r))
                    moving_ = true;
                break;
            case Cmd::Stop:
                call("<Stop xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\">" + prof +
                         "<PanTilt>true</PanTilt><Zoom>false</Zoom></Stop>",
                     r);
                moving_ = false;
                break;
            case Cmd::Preset: {
                std::string tok;
                {
                    std::lock_guard<std::mutex> lk(mx_);
                    if (c.index >= 0 && c.index < (int)preset_tokens_.size()) tok = preset_tokens_[c.index];
                }
                if (!tok.empty())
                    call("<GotoPreset xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\">" + prof + "<PresetToken>" +
                             xml_escape(tok) + "</PresetToken></GotoPreset>",
                         r);
                break;
            }
            default: break;
        }
    }
    if (moving_ && inited_) {  // never leave the camera spinning
        std::string r;
        call("<Stop xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>" + xml_escape(profile_) +
                 "</ProfileToken><PanTilt>true</PanTilt><Zoom>false</Zoom></Stop>",
             r, 2000);
    }
}
