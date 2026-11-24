// Tapo Desktop Client – main window, UI and input handling
#include "app.h"
#include "i18n.h"
#include "onvif.h"
#include "player.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}
#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

struct App {
    Store store;
    Player* player = nullptr;
    Ptz ptz;
    bool running = true;

    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    int tex_w = 0, tex_h = 0;
    bool tex_full = false;
    AVFrame* shown = nullptr;  // last displayed frame (for snapshots)
    SwsContext* sws = nullptr;
    AVFrame* conv = nullptr;
    Uint32 ev_wake = 0;

    int cur = -1;
    bool fullscreen = false;
    bool muted = false;

    bool show_cams = false, show_settings = false, show_help = false, show_ptz = true;
    bool edit_open = false;
    int edit_idx = -1;
    Camera edit;
    int confirm_remove = -1;
    Settings set_edit;

    std::thread scan_th;
    std::atomic<bool> scanning{false};
    std::mutex scan_mx;
    std::vector<Discovered> scan_res;
    bool scanned = false;

    std::thread test_th;
    std::atomic<bool> testing{false};
    std::mutex test_mx;
    std::string test_msg;
    bool test_ok = false;

    Uint32 last_mouse = 0;
    std::string toast;
    Uint32 toast_until = 0;
    float pad_x = 0, pad_y = 0, sent_x = 0, sent_y = 0;
};
static App A;

static void wake() {
    SDL_Event e{};
    e.type = A.ev_wake;
    SDL_PushEvent(&e);
}

static void notify(const std::string& m) {
    logf("%s", m.c_str());
    A.toast = m;
    A.toast_until = SDL_GetTicks() + 4000;
}

static const Camera* cur_cam() { return (A.cur >= 0 && A.cur < (int)A.store.cams.size()) ? &A.store.cams[A.cur] : nullptr; }

// ------------------------------------------------------------------ actions

static void stop_ptz_motion() {
    if (A.sent_x != 0 || A.sent_y != 0) A.ptz.move(0, 0);
    A.sent_x = A.sent_y = 0;
}

static void open_camera(int idx) {
    if (idx < 0 || idx >= (int)A.store.cams.size()) return;
    A.player->set_recording(false, "", "");
    stop_ptz_motion();
    A.cur = idx;
    A.store.set.last_camera = idx;
    A.store.save();
    const Camera& c = A.store.cams[idx];
    if (A.shown) av_frame_free(&A.shown);
    if (A.tex) {
        SDL_DestroyTexture(A.tex);
        A.tex = nullptr;
        A.tex_w = A.tex_h = 0;
    }
    A.player->open(c, A.store.set, A.store.set.start_sd);
    A.player->set_muted(A.muted);
    A.ptz.open(c);
    SDL_SetWindowTitle(A.win, (c.name + " – " APP_NAME).c_str());
}

static void close_camera() {
    stop_ptz_motion();
    A.player->set_recording(false, "", "");
    A.player->close();
    A.ptz.close();
    A.cur = -1;
    if (A.shown) av_frame_free(&A.shown);
    if (A.tex) {
        SDL_DestroyTexture(A.tex);
        A.tex = nullptr;
        A.tex_w = A.tex_h = 0;
    }
    SDL_SetWindowTitle(A.win, APP_NAME);
}

static void remove_camera(int i) {
    if (i < 0 || i >= (int)A.store.cams.size()) return;
    bool was_cur = i == A.cur;
    if (was_cur) close_camera();
    A.store.cams.erase(A.store.cams.begin() + i);
    if (!was_cur && A.cur > i) A.cur--;
    A.store.set.last_camera = std::max(A.cur, 0);
    A.confirm_remove = -1;
    A.store.save();
    if (was_cur && !A.store.cams.empty()) open_camera(0);
}

static void toggle_fullscreen() {
    A.fullscreen = !A.fullscreen;
    SDL_SetWindowFullscreen(A.win, A.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void toggle_mute() {
    A.muted = !A.muted;
    A.player->set_muted(A.muted);
    notify(A.muted ? tr("Sound off") : tr("Sound on"));
}

static void snapshot() {
    const Camera* c = cur_cam();
    if (!c || !A.shown) {
        notify(tr("No picture yet"));
        return;
    }
    std::string path = A.store.set.media_dir + "/" + sanitize_filename(c->name) + "_" + timestamp_now() + ".jpg";
    std::string err;
    if (save_jpeg(A.shown, path, err)) notify(tr("Snapshot saved: ") + path);
    else notify(tr("Snapshot failed: ") + err);
}

static void toggle_record() {
    const Camera* c = cur_cam();
    if (!c) return;
    bool on = !A.player->recording_wanted();
    A.player->set_recording(on, A.store.set.media_dir, sanitize_filename(c->name));
    notify(on ? tr("Recording to ") + A.store.set.media_dir : trs("Recording stopped"));
}

static void start_scan() {
    if (A.scanning) return;
    if (A.scan_th.joinable()) A.scan_th.join();
    A.scanning = true;
    A.scan_th = std::thread([] {
        auto r = onvif_discover(2500);
        {
            std::lock_guard<std::mutex> lk(A.scan_mx);
            A.scan_res = r;
            A.scanned = true;
        }
        A.scanning = false;
        wake();
    });
}

static void start_test() {
    if (A.testing) return;
    if (A.test_th.joinable()) A.test_th.join();
    A.testing = true;
    Camera c = A.edit;
    A.test_th = std::thread([c] {
        std::string m;
        bool ok = onvif_check_login(c, m);
        {
            std::lock_guard<std::mutex> lk(A.test_mx);
            A.test_msg = m;
            A.test_ok = ok;
        }
        A.testing = false;
        wake();
    });
}

static void begin_edit(int idx, const Discovered* d = nullptr) {
    A.edit_idx = idx;
    A.edit = idx >= 0 ? A.store.cams[idx] : Camera();
    if (idx < 0) {
        // most people use the same camera account on all cameras
        if (const Camera* last = cur_cam() ? cur_cam() : (A.store.cams.empty() ? nullptr : &A.store.cams[0])) {
            A.edit.user = last->user;
            A.edit.password = last->password;
        }
        if (d) {
            A.edit.host = d->host;
            A.edit.onvif_port = d->onvif_port;
            A.edit.name = d->model.empty() ? tr("Camera ") + d->host : "Tapo " + d->model;
        }
    }
    {
        std::lock_guard<std::mutex> lk(A.test_mx);
        A.test_msg.clear();
    }
    A.edit_open = true;
}

static std::string trim_ws(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

static void save_edit() {
    Camera c = A.edit;
    c.host = trim_ws(c.host);
    c.name = trim_ws(c.name);
    if (c.name.empty()) c.name = c.host;
    if (c.rtsp_port <= 0 || c.rtsp_port > 65535) c.rtsp_port = 554;
    if (c.onvif_port <= 0 || c.onvif_port > 65535) c.onvif_port = 2020;
    if (c.stream_hd.empty()) c.stream_hd = "stream1";
    if (c.stream_sd.empty()) c.stream_sd = "stream2";
    if (A.edit_idx >= 0 && A.edit_idx < (int)A.store.cams.size()) {
        A.store.cams[A.edit_idx] = c;
        A.store.save();
        if (A.edit_idx == A.cur) open_camera(A.cur);
    } else {
        A.store.cams.push_back(c);
        A.store.save();
        open_camera((int)A.store.cams.size() - 1);
    }
    A.edit_open = false;
    if (!A.store.save()) notify(tr("Could not save ") + A.store.path);
}

// ------------------------------------------------------------------- video

static void upload(AVFrame* f) {
    bool full = f->format == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;
    if (!A.tex || A.tex_w != f->width || A.tex_h != f->height || A.tex_full != full) {
        if (A.tex) SDL_DestroyTexture(A.tex);
        SDL_SetYUVConversionMode(full ? SDL_YUV_CONVERSION_JPEG : SDL_YUV_CONVERSION_AUTOMATIC);
        A.tex = SDL_CreateTexture(A.ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
        A.tex_w = f->width;
        A.tex_h = f->height;
        A.tex_full = full;
        if (!A.tex) {
            logf("texture: %s", SDL_GetError());
            A.tex_w = A.tex_h = 0;
            return;
        }
    }
    if (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) {
        SDL_UpdateYUVTexture(A.tex, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2],
                             f->linesize[2]);
        return;
    }
    A.sws = sws_getCachedContext(A.sws, f->width, f->height, (AVPixelFormat)f->format, f->width, f->height,
                                 AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!A.sws) return;
    if (!A.conv || A.conv->width != f->width || A.conv->height != f->height) {
        av_frame_free(&A.conv);
        A.conv = av_frame_alloc();
        A.conv->format = AV_PIX_FMT_YUV420P;
        A.conv->width = f->width;
        A.conv->height = f->height;
        if (av_frame_get_buffer(A.conv, 0) < 0) {
            av_frame_free(&A.conv);
            return;
        }
    }
    sws_scale(A.sws, f->data, f->linesize, 0, f->height, A.conv->data, A.conv->linesize);
    SDL_UpdateYUVTexture(A.tex, nullptr, A.conv->data[0], A.conv->linesize[0], A.conv->data[1], A.conv->linesize[1],
                         A.conv->data[2], A.conv->linesize[2]);
}

static SDL_Rect video_rect() {
    int ww, wh;
    SDL_GetRendererOutputSize(A.ren, &ww, &wh);
    if (!A.tex_w || !A.tex_h) return {0, 0, ww, wh};
    double s = std::min((double)ww / A.tex_w, (double)wh / A.tex_h);
    int w = (int)(A.tex_w * s + 0.5), h = (int)(A.tex_h * s + 0.5);
    return {(ww - w) / 2, (wh - h) / 2, w, h};
}

// --------------------------------------------------------------------- UI

static const ImVec4 kGreen(0.45f, 0.85f, 0.45f, 1), kRed(0.95f, 0.35f, 0.35f, 1);

static bool any_dialog() { return A.show_cams || A.show_settings || A.show_help || A.edit_open; }

static void tooltip(const char* t) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t);
}

static void ui_toolbar() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    const Camera* c = cur_cam();
    ImGui::SetNextItemWidth(230);
    if (ImGui::BeginCombo("##cam", c ? c->name.c_str() : tr("No camera"))) {
        for (int i = 0; i < (int)A.store.cams.size(); i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(A.store.cams[i].name.c_str(), i == A.cur) && i != A.cur) open_camera(i);
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr("Manage cameras..."))) A.show_cams = true;
        ImGui::EndCombo();
    }
    tooltip(tr("Camera (PgUp/PgDn)"));

    ImGui::SameLine();
    ImGui::BeginDisabled(!c);
    if (ImGui::Button(A.player->quality_sd() ? " SD " : " HD ")) A.player->set_quality(!A.player->quality_sd());
    tooltip(tr("Stream quality: HD = stream1, SD = stream2 (H)"));
    ImGui::SameLine();
    if (ImGui::Button(A.muted ? tr("Sound off") : tr("Sound on"))) toggle_mute();
    tooltip(tr("Mute / unmute (M)"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Snapshot"))) snapshot();
    tooltip(tr("Save a JPEG of the current picture (P)"));
    ImGui::SameLine();
    bool rec = A.player->recording_wanted();
    if (rec) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1));
    if (ImGui::Button(rec ? tr("Stop recording") : tr("Record"))) toggle_record();
    if (rec) ImGui::PopStyleColor();
    tooltip(tr("Record to a local MKV file without re-encoding (V)"));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Pan/Tilt"), &A.show_ptz);
    tooltip(tr("Show the pan/tilt pad – or use the arrow keys / WASD (Shift = fast)"));
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(tr("Cameras"))) A.show_cams = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Settings"))) {
        A.set_edit = A.store.set;
        A.show_settings = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(A.fullscreen ? tr("Window") : tr("Fullscreen"))) toggle_fullscreen();
    tooltip(tr("F / F11 / double-click"));
    ImGui::SameLine();
    if (ImGui::Button(" ? ")) A.show_help = true;

    if (c) {
        std::string st = A.player->status();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", st.c_str());
    }
    ImGui::End();
}

static void ui_center_status() {
    std::string msg;
    if (A.store.cams.empty()) msg = tr("No camera configured yet.\nClick \"Cameras\" to scan your network.");
    else if (cur_cam() && !A.player->live()) msg = A.player->status();
    if (msg.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted(msg.c_str());
    ImGui::End();
}

static void ui_ptz() {
    A.pad_x = A.pad_y = 0;
    if (!A.show_ptz || !cur_cam()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##ptz", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (!A.ptz.available()) {
        std::string m = A.ptz.message();
        ImGui::TextDisabled("%s", m.empty() ? tr("Pan/tilt not available") : m.c_str());
        ImGui::End();
        return;
    }
    const float b = 46;
    auto hold = [&](const char* id, ImGuiDir dir, float x, float y) {
        ImGui::ArrowButtonEx(id, dir, ImVec2(b, b));
        if (ImGui::IsItemActive()) {
            A.pad_x = x;
            A.pad_y = y;
        }
    };
    ImGui::Dummy(ImVec2(b, b));
    ImGui::SameLine();
    hold("##up", ImGuiDir_Up, 0, 1);
    hold("##left", ImGuiDir_Left, -1, 0);
    ImGui::SameLine();
    if (ImGui::Button(tr("Stop"), ImVec2(b, b))) stop_ptz_motion();
    ImGui::SameLine();
    hold("##right", ImGuiDir_Right, 1, 0);
    ImGui::Dummy(ImVec2(b, b));
    ImGui::SameLine();
    hold("##down", ImGuiDir_Down, 0, -1);

    auto presets = A.ptz.presets();
    if (!presets.empty()) {
        ImGui::Separator();
        for (int i = 0; i < (int)presets.size(); i++) {
            if (i % 4) ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::Button(std::to_string(i + 1).c_str(), ImVec2(b * 0.7f, 0))) A.ptz.goto_preset(i);
            ImGui::PopID();
            tooltip(presets[i].c_str());
        }
    }
    ImGui::End();
}

static void ptz_update() {
    float x = A.pad_x, y = A.pad_y;
    bool fast = false;
    if (cur_cam() && !ImGui::GetIO().WantTextInput && (SDL_GetWindowFlags(A.win) & SDL_WINDOW_INPUT_FOCUS)) {
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) x -= 1;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) x += 1;
        if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) y += 1;
        if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) y -= 1;
        fast = (SDL_GetModState() & KMOD_SHIFT) != 0;
    }
    x = std::clamp(x, -1.0f, 1.0f);
    y = std::clamp(y, -1.0f, 1.0f);
    float sp = fast ? 1.0f : A.store.set.ptz_speed;
    if (A.store.set.ptz_invert) {
        x = -x;
        y = -y;
    }
    x *= sp;
    y *= sp;
    if (x != A.sent_x || y != A.sent_y) {
        A.ptz.move(x, y);
        A.sent_x = x;
        A.sent_y = y;
    }
}

static void ui_cameras() {
    if (!A.show_cams) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((trs("Cameras") + "###cams").c_str(), &A.show_cams, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText(tr("Saved cameras"));
    if (A.store.cams.empty()) ImGui::TextDisabled("%s", tr("None yet – scan your network or add a camera manually."));
    else if (ImGui::BeginTable("cams", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr("Name"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Address"), ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 210);
        for (int i = 0; i < (int)A.store.cams.size(); i++) {
            const Camera& c = A.store.cams[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (i == A.cur) ImGui::TextColored(kGreen, "%s", c.name.c_str());
            else ImGui::TextUnformatted(c.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.host.c_str());
            ImGui::TableNextColumn();
            bool removed = false;
            if (ImGui::SmallButton(tr("Show"))) open_camera(i);
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Edit"))) begin_edit(i);
            ImGui::SameLine();
            if (A.confirm_remove == i) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1));
                if (ImGui::SmallButton(tr("Really remove?"))) {
                    remove_camera(i);
                    removed = true;
                }
                ImGui::PopStyleColor();
            } else if (ImGui::SmallButton(tr("Remove"))) {
                A.confirm_remove = i;
            }
            ImGui::PopID();
            if (removed) break;
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr("Add camera manually"))) begin_edit(-1);

    ImGui::SeparatorText(tr("Find cameras in your network"));
    ImGui::BeginDisabled(A.scanning);
    if (ImGui::Button(A.scanning ? tr("Scanning...") : tr("Scan network"))) start_scan();
    ImGui::EndDisabled();
    if (A.scanning) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("ONVIF discovery + port scan, takes a few seconds"));
    }
    std::vector<Discovered> res;
    bool scanned;
    {
        std::lock_guard<std::mutex> lk(A.scan_mx);
        res = A.scan_res;
        scanned = A.scanned;
    }
    if (scanned && !A.scanning && res.empty())
        ImGui::TextWrapped("%s", tr("No cameras found. Make sure the camera is in the same network as this computer and that "
                           "a Camera Account is set up in the Tapo app."));
    if (!res.empty() && ImGui::BeginTable("found", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(tr("Address"), ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn(tr("Model"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##add", ImGuiTableColumnFlags_WidthFixed, 80);
        for (int i = 0; i < (int)res.size(); i++) {
            const Discovered& d = res[i];
            bool saved = false;
            for (auto& c : A.store.cams) saved |= c.host == d.host;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.host.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.model.empty() ? tr("ONVIF camera") : d.model.c_str());
            ImGui::TableNextColumn();
            if (saved) ImGui::TextDisabled("%s", tr("saved"));
            else if (ImGui::SmallButton(tr("Add"))) begin_edit(-1, &d);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("Tapo cameras need a Camera Account for access from other apps: Tapo app > camera > "
                       "Settings > Advanced Settings > Camera Account. Use those credentials here – not your "
                       "TP-Link ID."));
    ImGui::End();
}

static void ui_edit() {
    if (!A.edit_open) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    if (!ImGui::Begin((trs(A.edit_idx >= 0 ? "Edit camera" : "Add camera") + "###edit").c_str(), &A.edit_open,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(280);
    ImGui::InputText(tr("Name"), &A.edit.name);
    ImGui::InputText(tr("IP address / host"), &A.edit.host);
    ImGui::InputText(tr("User"), &A.edit.user);
    ImGui::InputText(tr("Password"), &A.edit.password, ImGuiInputTextFlags_Password);
    ImGui::TextDisabled("%s", tr("Camera Account from the Tapo app (Advanced Settings)"));
    if (ImGui::TreeNode(tr("Advanced"))) {
        ImGui::InputInt(tr("RTSP port"), &A.edit.rtsp_port, 0);
        ImGui::InputInt(tr("ONVIF port"), &A.edit.onvif_port, 0);
        ImGui::InputText(tr("HD stream path"), &A.edit.stream_hd);
        ImGui::InputText(tr("SD stream path"), &A.edit.stream_sd);
        ImGui::TreePop();
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::BeginDisabled(A.testing || A.edit.host.empty());
    if (ImGui::Button(A.testing ? tr("Testing...") : tr("Test login"))) start_test();
    ImGui::EndDisabled();
    {
        std::lock_guard<std::mutex> lk(A.test_mx);
        if (!A.test_msg.empty() && !A.testing) {
            ImGui::SameLine();
            ImGui::TextColored(A.test_ok ? kGreen : kRed, "%s", A.test_msg.c_str());
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(trim_ws(A.edit.host).empty());
    if (ImGui::Button(tr("Save"))) save_edit();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"))) A.edit_open = false;
    ImGui::End();
}

static void ui_settings() {
    if (!A.show_settings) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((trs("Settings") + "###settings").c_str(), &A.show_settings, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    Settings& s = A.set_edit;
    ImGui::PushItemWidth(240);
    ImGui::SeparatorText(tr("Language"));
    {
        const auto& langs = languages();
        int li = 0;
        for (int i = 0; i < (int)langs.size(); i++)
            if (s.language == langs[i].code) li = i;
        if (ImGui::BeginCombo("##language", langs[li].name)) {
            for (int i = 0; i < (int)langs.size(); i++)
                if (ImGui::Selectable(langs[i].name, i == li)) s.language = langs[i].code;
            ImGui::EndCombo();
        }
    }
    ImGui::SeparatorText(tr("Stream"));
    int q = s.start_sd ? 1 : 0;
    ImGui::RadioButton(tr("Start in HD"), &q, 0);
    ImGui::SameLine();
    ImGui::RadioButton(tr("Start in SD"), &q, 1);
    s.start_sd = q == 1;
    int tp = s.transport == "udp" ? 1 : 0;
    ImGui::RadioButton(tr("TCP (robust)"), &tp, 0);
    ImGui::SameLine();
    ImGui::RadioButton(tr("UDP (lowest latency, needs good Wi-Fi)"), &tp, 1);
    s.transport = tp ? "udp" : "tcp";
    ImGui::SliderInt(tr("Decoder threads"), &s.decoder_threads, 1, 8);
    tooltip(tr("1 = lowest latency. 2 or more spreads the work on slow CPUs (adds ~1 frame delay)."));
    ImGui::SliderInt(tr("Switch HD/SD after errors"), &s.failover_after, 0, 5);
    tooltip(tr("If a stream fails this many times in a row, switch to the other one (0 = never)."));
    ImGui::SliderFloat(tr("Connect timeout (s)"), &s.startup_timeout, 3, 30, "%.0f");
    ImGui::SliderFloat(tr("Stall timeout (s)"), &s.stall_timeout, 2, 30, "%.0f");
    ImGui::Checkbox(tr("Sound on at start"), &s.audio);
    ImGui::SeparatorText(tr("Pan/Tilt"));
    ImGui::SliderFloat(tr("Speed"), &s.ptz_speed, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox(tr("Invert directions (ceiling mount)"), &s.ptz_invert);
    ImGui::SeparatorText(tr("Local storage"));
    ImGui::InputText(tr("Snapshots & recordings"), &s.media_dir);
    ImGui::PopItemWidth();
    ImGui::TextDisabled("%s%s%s", tr("Config file: "), A.store.path.c_str(), A.store.portable ? tr(" (portable)") : "");
    ImGui::Separator();
    if (ImGui::Button(tr("Save"))) {
        const Settings& o = A.store.set;
        bool reopen = o.transport != s.transport || o.decoder_threads != s.decoder_threads ||
                      o.startup_timeout != s.startup_timeout || o.stall_timeout != s.stall_timeout ||
                      o.failover_after != s.failover_after;
        s.last_camera = o.last_camera;
        if (s.media_dir.empty()) s.media_dir = default_media_dir();
        A.store.set = s;
        set_language(s.language);
        if (!A.store.save()) notify(tr("Could not save ") + A.store.path);
        A.show_settings = false;
        if (reopen && cur_cam()) open_camera(A.cur);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"))) A.show_settings = false;
    ImGui::End();
}

static void ui_help() {
    if (!A.show_help) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin((trs("Help") + "###help").c_str(), &A.show_help, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text(APP_NAME " " APP_VERSION);
    ImGui::Separator();
    static const char* keys[][2] = {
        {"Arrow keys / WASD", "pan / tilt while held (Shift = fast)"},
        {"Space", "stop pan / tilt"},
        {"1 - 8", "go to preset (set up in the Tapo app)"},
        {"H / Tab", "switch HD / SD stream"},
        {"M", "sound on / off"},
        {"P", "snapshot (JPEG)"},
        {"V", "start / stop recording (MKV)"},
        {"F / F11 / double-click", "fullscreen"},
        {"PgUp / PgDn", "previous / next camera"},
        {"R", "reconnect"},
        {"Esc", "close dialog / leave fullscreen"},
        {"Q / Ctrl+Q", "quit"},
    };
    if (ImGui::BeginTable("keys", 2)) {
        for (auto& k : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kGreen, "%s", tr(k[0]));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tr(k[1]));
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Not affiliated with TP-Link. Tapo is a trademark of TP-Link."));
    ImGui::End();
}

static void ui_overlays() {
    ImGuiIO& io = ImGui::GetIO();
    if (SDL_GetTicks() < A.toast_until && !A.toast.empty()) {
        ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(A.toast.c_str());
        ImGui::End();
    }
    if (cur_cam() && A.player->recording_wanted() && (SDL_GetTicks() / 600) % 2 == 0) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 p(io.DisplaySize.x - 80, 56);
        dl->AddCircleFilled(ImVec2(p.x, p.y + 9), 7, IM_COL32(230, 40, 40, 255));
        dl->AddText(ImVec2(p.x + 14, p.y), IM_COL32(255, 255, 255, 255), "REC");
    }
}

// ------------------------------------------------------------------ input

static void handle_key(const SDL_KeyboardEvent& k) {
    if (k.repeat || ImGui::GetIO().WantTextInput) return;
    SDL_Keycode c = k.keysym.sym;
    if (c == SDLK_ESCAPE) {
        if (A.edit_open) A.edit_open = false;
        else if (A.show_settings) A.show_settings = false;
        else if (A.show_help) A.show_help = false;
        else if (A.show_cams) A.show_cams = false;
        else if (A.fullscreen) toggle_fullscreen();
        return;
    }
    switch (c) {
        case SDLK_q: A.running = false; break;
        case SDLK_f:
        case SDLK_F11: toggle_fullscreen(); break;
        case SDLK_F1: A.show_help = !A.show_help; break;
        case SDLK_h:
        case SDLK_TAB:
            if (cur_cam()) A.player->set_quality(!A.player->quality_sd());
            break;
        case SDLK_m: toggle_mute(); break;
        case SDLK_r:
            if (cur_cam()) A.player->reconnect();
            break;
        case SDLK_p: snapshot(); break;
        case SDLK_v: toggle_record(); break;
        case SDLK_SPACE: stop_ptz_motion(); break;
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            if (A.store.cams.size() > 1) {
                int n = (int)A.store.cams.size();
                int d = c == SDLK_PAGEDOWN ? 1 : -1;
                open_camera(((A.cur < 0 ? 0 : A.cur) + d + n) % n);
            }
            break;
        default:
            if (c >= SDLK_1 && c <= SDLK_8 && cur_cam()) A.ptz.goto_preset(c - SDLK_1);
            break;
    }
}

// ------------------------------------------------------------------- main

static void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x205E, 0x2190, 0x21FF, 0};
    const char* candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for (const char* p : candidates)
        if (access(p, R_OK) == 0 && io.Fonts->AddFontFromFileTTF(p, 17.0f, nullptr, ranges)) return;
    io.Fonts->AddFontDefault();
}

static int install_desktop_entry() {
    const char* h = getenv("HOME");
    if (!h) return 1;
    std::string dir = std::string(h) + "/.local/share/applications";
    mkdirs(dir, 0755);
    std::string f = dir + "/tapo-desktop.desktop";
    FILE* fp = fopen(f.c_str(), "w");
    if (!fp) {
        fprintf(stderr, "cannot write %s\n", f.c_str());
        return 1;
    }
    fprintf(fp,
            "[Desktop Entry]\nType=Application\nName=" APP_NAME
            "\nComment=Live view and pan/tilt control for Tapo cameras\nComment[de]=Livebild und Schwenksteuerung für Tapo-Kameras"
            "\nExec=\"%s\"\nIcon=camera-web\n"
            "Terminal=false\nCategories=AudioVideo;Video;Network;\nStartupWMClass=tapo-desktop\n",
            exe_path().c_str());
    fclose(fp);
    printf("%s%s\n", tr("Menu entry installed: "), f.c_str());
    return 0;
}

static void usage() {
    // the text must match the key in i18n.cpp exactly
    static const char* kUsage =
        "Usage: tapo-desktop [options]\n"
        "  --config FILE      use this configuration file\n"
        "  --portable         keep the configuration next to the executable (tapo-desktop.ini)\n"
        "  --camera N         start with camera number N (1 = first)\n"
        "  --fullscreen       start in fullscreen\n"
        "  --lang de|en       user interface language (default: German)\n"
        "  --install-desktop  add a menu entry for the current user\n"
        "  --verbose          print FFmpeg diagnostics\n"
        "  --version, --help\n\n"
        "Configuration is stored in ~/.config/tapo-desktop/config.ini (or tapo-desktop.ini\n"
        "next to the executable in portable mode). Press F1 in the app for keyboard shortcuts.\n";
    printf("%s %s – %s\n\n%s", APP_NAME, APP_VERSION, tr("live view and pan/tilt control for Tapo cameras"),
           tr(kUsage));
}

int main(int argc, char** argv) {
    std::string cfg_path, lang, unknown;
    bool portable = false, fullscreen = false, verbose = false, help = false, install = false;
    int start_cam = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) cfg_path = argv[++i];
        else if (a == "--portable") portable = true;
        else if (a == "--camera" && i + 1 < argc) start_cam = atoi(argv[++i]) - 1;
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--fullscreen" || a == "-f") fullscreen = true;
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--install-desktop") install = true;
        else if (a == "--help" || a == "-h") help = true;
        else if (a == "--version") {
            printf(APP_NAME " " APP_VERSION "\n");
            return 0;
        } else if (unknown.empty()) unknown = a;
    }

    A.store.locate(cfg_path);
    if (portable && cfg_path.empty()) {
        A.store.path = exe_dir() + "/tapo-desktop.ini";
        A.store.portable = true;
    }
    A.store.load();
    if (!lang.empty()) A.store.set.language = lang;  // --lang overrides the saved setting
    set_language(A.store.set.language);
    A.store.set.language = language();

    if (!unknown.empty()) {
        fprintf(stderr, "%s%s\n\n", tr("unknown option: "), unknown.c_str());
        usage();
        return 2;
    }
    if (help) {
        usage();
        return 0;
    }
    if (install) return install_desktop_entry();

    av_log_set_level(verbose ? AV_LOG_INFO : AV_LOG_ERROR);
    avformat_network_init();

    logf("config: %s (%zu cameras)", A.store.path.c_str(), A.store.cams.size());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint("SDL_APP_NAME", APP_NAME);
    SDL_SetHint("SDL_AUDIO_DEVICE_STREAM_NAME", "Camera audio");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) logf("no audio: %s", SDL_GetError());
    A.ev_wake = SDL_RegisterEvents(1);

    A.win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 760,
                             SDL_WINDOW_RESIZABLE);
    if (!A.win) {
        fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(A.win, 480, 320);
    A.ren = SDL_CreateRenderer(A.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!A.ren) A.ren = SDL_CreateRenderer(A.win, -1, SDL_RENDERER_SOFTWARE);
    if (!A.ren) {
        fprintf(stderr, "cannot create renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RendererInfo ri;
    if (SDL_GetRendererInfo(A.ren, &ri) == 0) logf("renderer: %s", ri.name);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8;
    st.FrameRounding = 5;
    st.PopupRounding = 5;
    st.GrabRounding = 5;
    st.WindowBorderSize = 0;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(8, 7);
    load_fonts();
    ImGui_ImplSDL2_InitForSDLRenderer(A.win, A.ren);
    ImGui_ImplSDLRenderer2_Init(A.ren);

    A.player = new Player(A.ev_wake);
    A.muted = !A.store.set.audio;
    if (fullscreen) toggle_fullscreen();

    if (A.store.cams.empty()) {
        A.show_cams = true;
        start_scan();
    } else {
        int idx = start_cam >= 0 ? start_cam : A.store.set.last_camera;
        if (idx < 0 || idx >= (int)A.store.cams.size()) idx = 0;
        open_camera(idx);
    }

    A.last_mouse = SDL_GetTicks();
    while (A.running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 30)) {
            do {
                ImGui_ImplSDL2_ProcessEvent(&e);
                switch (e.type) {
                    case SDL_QUIT: A.running = false; break;
                    case SDL_KEYDOWN: handle_key(e.key); break;
                    case SDL_MOUSEMOTION: A.last_mouse = SDL_GetTicks(); break;
                    case SDL_MOUSEBUTTONDOWN:
                        A.last_mouse = SDL_GetTicks();
                        if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2 &&
                            !ImGui::GetIO().WantCaptureMouse)
                            toggle_fullscreen();
                        break;
                    case SDL_WINDOWEVENT:
                        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) stop_ptz_motion();
                        break;
                    default: break;
                }
            } while (SDL_PollEvent(&e));
        }

        if (cur_cam()) {
            if (AVFrame* f = A.player->fetch_frame()) {
                upload(f);
                if (A.shown) av_frame_free(&A.shown);
                A.shown = f;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        bool active = SDL_GetTicks() - A.last_mouse < 3000;
        bool show_controls = active || any_dialog() || !cur_cam() || !A.player->live() || io.WantCaptureMouse;
        if (show_controls) {
            ui_toolbar();
            ui_ptz();
        } else {
            A.pad_x = A.pad_y = 0;
        }
        ui_center_status();
        ui_cameras();
        ui_edit();
        ui_settings();
        ui_help();
        ui_overlays();
        if (A.fullscreen && !show_controls) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        ImGui::Render();
        ptz_update();

        SDL_RenderSetScale(A.ren, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(A.ren, 0, 0, 0, 255);
        SDL_RenderClear(A.ren);
        if (A.tex) {
            SDL_Rect r = video_rect();
            SDL_RenderCopy(A.ren, A.tex, nullptr, &r);
        }
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), A.ren);
        SDL_RenderPresent(A.ren);
    }

    stop_ptz_motion();
    A.ptz.close();
    A.player->close();
    delete A.player;
    if (A.scan_th.joinable()) A.scan_th.join();
    if (A.test_th.joinable()) A.test_th.join();
    if (A.shown) av_frame_free(&A.shown);
    av_frame_free(&A.conv);
    sws_freeContext(A.sws);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (A.tex) SDL_DestroyTexture(A.tex);
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
    return 0;
}
