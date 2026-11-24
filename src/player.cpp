// Tapo Desktop Client – RTSP player
#include "player.h"
#include "i18n.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <vector>

static std::string averr(int e) {
    char b[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(e, b, sizeof b);
    return b;
}

// percent-encode characters that would break the user:password@host syntax
static std::string url_userinfo(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == ':' || c == '@' || c == '/' || c == '?' || c == '#' || c == '%' || c == '[' || c == ']' ||
            c <= 0x20 || c >= 0x7f) {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        } else {
            o += (char)c;
        }
    }
    return o;
}

void Player::open(const Camera& cam, const Settings& s, bool sd) {
    close();
    cam_ = cam;
    set_ = s;
    sd_ = sd;
    quit_ = false;
    reconnect_req_ = false;
    live_ = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (!frame_) frame_ = av_frame_alloc();
        av_frame_unref(frame_);
        frame_new_ = false;
        status_ = tr("Connecting…");
    }
    th_ = std::thread(&Player::run, this);
}

void Player::close() {
    if (th_.joinable()) {
        quit_ = true;
        th_.join();
    }
    live_ = false;
    std::lock_guard<std::mutex> lk(mx_);
    if (frame_) av_frame_free(&frame_);
    frame_new_ = false;
}

void Player::set_quality(bool sd) {
    sd_ = sd;
    reconnect_req_ = true;
}

std::string Player::status() {
    std::lock_guard<std::mutex> lk(mx_);
    return status_;
}

void Player::set_status(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (status_ == s) return;
        status_ = s;
    }
    logf("%s", s.c_str());
    SDL_Event e{};
    e.type = wake_event_;
    SDL_PushEvent(&e);
}

AVFrame* Player::fetch_frame() {
    std::lock_guard<std::mutex> lk(mx_);
    if (!frame_new_ || !frame_) return nullptr;
    AVFrame* f = av_frame_alloc();
    av_frame_move_ref(f, frame_);
    frame_new_ = false;
    return f;
}

void Player::publish(AVFrame* f) {
    bool wake;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (!frame_) return;
        av_frame_unref(frame_);
        av_frame_move_ref(frame_, f);
        wake = !frame_new_;
        frame_new_ = true;
    }
    if (wake) {
        SDL_Event e{};
        e.type = wake_event_;
        SDL_PushEvent(&e);
    }
}

void Player::set_recording(bool on, const std::string& dir, const std::string& prefix) {
    {
        std::lock_guard<std::mutex> lk(mx_);
        rec_dir_ = dir;
        rec_prefix_ = prefix;
    }
    rec_want_ = on;
}

std::string Player::recording_file() {
    std::lock_guard<std::mutex> lk(mx_);
    return rec_file_;
}

int Player::interrupt_cb(void* opaque) {
    auto* p = static_cast<Player*>(opaque);
    if (p->quit_ || p->reconnect_req_) return 1;
    if (av_gettime_relative() - p->last_io_ > p->io_timeout_) {
        p->timed_out_ = true;
        return 1;
    }
    return 0;
}

void Player::run() {
    int fails = 0;
    while (!quit_) {
        reconnect_req_ = false;
        bool got = false;
        session(got);
        live_ = false;
        if (quit_) break;
        if (reconnect_req_) {
            fails = 0;
            continue;
        }
        if (got) fails = 0;
        fails++;
        std::string why = status();
        double delay;
        if (set_.failover_after > 0 && fails >= set_.failover_after) {
            sd_ = !sd_;
            fails = 0;
            delay = 0.5;
            set_status(why + tr(" – switching to ") + (sd_ ? "SD" : "HD"));
        } else {
            delay = std::min(1.5 * fails, 10.0);
            char b[64];
            snprintf(b, sizeof b, "%s%.0f s", tr(" – retrying in "), delay);
            set_status(why + b);
        }
        for (int i = 0; i < (int)(delay * 20) && !quit_ && !reconnect_req_; i++) usleep(50000);
    }
}

void Player::session(bool& got) {
    const bool sd = sd_;
    const std::string label = sd ? "SD" : "HD";
    const std::string& path = sd ? cam_.stream_sd : cam_.stream_hd;
    std::string url = "rtsp://" + url_userinfo(cam_.user) + ":" + url_userinfo(cam_.password) + "@" + cam_.host +
                      ":" + std::to_string(cam_.rtsp_port) + "/" + path;
    set_status(tr("Connecting to ") + cam_.name + " (" + label + ") …");

    AVFormatContext* fmt = avformat_alloc_context();
    AVCodecContext *vdec = nullptr, *adec = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* fr = nullptr;
    SDL_AudioDeviceID adev = 0;
    int vi = -1, ai = -1;
    std::string fail;

    fmt->interrupt_callback.callback = interrupt_cb;
    fmt->interrupt_callback.opaque = this;
    AVDictionary* o = nullptr;
    av_dict_set(&o, "rtsp_transport", set_.transport.c_str(), 0);
    av_dict_set(&o, "fflags", "nobuffer+discardcorrupt", 0);
    av_dict_set(&o, "flags", "low_delay", 0);
    av_dict_set(&o, "max_delay", "300000", 0);
    av_dict_set(&o, "reorder_queue_size", "16", 0);
    av_dict_set(&o, "timeout", "5000000", 0);
    av_dict_set(&o, "analyzeduration", "1000000", 0);
    av_dict_set(&o, "probesize", "1000000", 0);
    av_dict_set(&o, "user_agent", APP_NAME, 0);

    timed_out_ = false;
    last_io_ = av_gettime_relative();
    io_timeout_ = (int64_t)(set_.startup_timeout * 1e6);

    int r = avformat_open_input(&fmt, url.c_str(), nullptr, &o);
    av_dict_free(&o);
    if (r < 0) {
        fmt = nullptr;  // freed by avformat_open_input
        if (r == AVERROR_HTTP_UNAUTHORIZED || r == AVERROR_HTTP_FORBIDDEN) fail = tr("login failed – check the camera account");
        else if (timed_out_) fail = tr("no answer from camera");
        else fail = averr(r);
        goto out;
    }
    if ((r = avformat_find_stream_info(fmt, nullptr)) < 0) {
        fail = timed_out_ ? trs("no answer from camera") : averr(r);
        goto out;
    }
    {
        const AVCodec* vc = nullptr;
        vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vc, 0);
        if (vi < 0 || !vc) {
            fail = tr("no supported video stream");
            goto out;
        }
        vdec = avcodec_alloc_context3(vc);
        avcodec_parameters_to_context(vdec, fmt->streams[vi]->codecpar);
        if (set_.decoder_threads <= 1) {
            vdec->thread_count = 1;
            vdec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        } else {
            vdec->thread_count = set_.decoder_threads;
            vdec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }
        vdec->flags2 |= AV_CODEC_FLAG2_FAST;
        if ((r = avcodec_open2(vdec, vc, nullptr)) < 0) {
            fail = tr("video decoder: ") + averr(r);
            goto out;
        }

        const AVCodec* ac = nullptr;
        ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vi, &ac, 0);
        if (ai >= 0 && ac) {
            adec = avcodec_alloc_context3(ac);
            avcodec_parameters_to_context(adec, fmt->streams[ai]->codecpar);
            if (avcodec_open2(adec, ac, nullptr) < 0 ||
                (adec->sample_fmt != AV_SAMPLE_FMT_S16 && adec->sample_fmt != AV_SAMPLE_FMT_S16P) ||
                adec->ch_layout.nb_channels < 1 || adec->sample_rate <= 0) {
                avcodec_free_context(&adec);
            }
        }
        if (!adec) ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vi, nullptr, 0);  // still record it
        for (unsigned i = 0; i < fmt->nb_streams; i++)
            if ((int)i != vi && (int)i != ai) fmt->streams[i]->discard = AVDISCARD_ALL;

        if (adec && SDL_WasInit(SDL_INIT_AUDIO)) {
            SDL_AudioSpec want{}, have{};
            want.freq = adec->sample_rate;
            want.format = AUDIO_S16SYS;
            want.channels = (Uint8)std::min(adec->ch_layout.nb_channels, 2);
            want.samples = 512;
            adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (adev) SDL_PauseAudioDevice(adev, muted_ ? 1 : 0);
            else logf("audio: %s", SDL_GetError());
        }
    }

    pkt = av_packet_alloc();
    fr = av_frame_alloc();
    io_timeout_ = (int64_t)(set_.stall_timeout * 1e6);
    last_io_ = av_gettime_relative();

    while (!quit_ && !reconnect_req_) {
        // recording control (start/stop requested by the UI)
        if (rec_want_ && !rec_) rec_open(fmt, vi, ai);
        else if (!rec_want_ && rec_) rec_close();

        r = av_read_frame(fmt, pkt);
        if (r < 0) {
            if (quit_ || reconnect_req_) break;
            fail = timed_out_ ? trs("stream stalled") : (r == AVERROR_EOF ? trs("stream ended") : averr(r));
            break;
        }
        last_io_ = av_gettime_relative();
        if (rec_) rec_write(fmt, pkt, vi, ai);

        if (pkt->stream_index == vi) {
            if (avcodec_send_packet(vdec, pkt) >= 0) {
                while (avcodec_receive_frame(vdec, fr) >= 0) {
                    if (!got) {
                        got = true;
                        live_ = true;
                        set_status(cam_.name + " · " + label + " · " + std::to_string(fr->width) + "x" +
                                   std::to_string(fr->height));
                    }
                    publish(fr);  // moves the reference out of fr
                }
            }
        } else if (pkt->stream_index == ai && adec && adev) {
            bool m = muted_;
            SDL_PauseAudioDevice(adev, m ? 1 : 0);
            if (avcodec_send_packet(adec, pkt) >= 0) {
                while (avcodec_receive_frame(adec, fr) >= 0) {
                    if (!m) {
                        int ch = std::min(fr->ch_layout.nb_channels, 2);
                        Uint32 bytes_per_sec = (Uint32)(adec->sample_rate * ch * 2);
                        // keep audio latency low: drop the queue if it grows beyond 300 ms
                        if (SDL_GetQueuedAudioSize(adev) > bytes_per_sec * 3 / 10) SDL_ClearQueuedAudio(adev);
                        if (fr->format == AV_SAMPLE_FMT_S16) {
                            SDL_QueueAudio(adev, fr->data[0], (Uint32)(fr->nb_samples * ch * 2));
                        } else {  // planar -> interleaved
                            std::vector<int16_t> buf((size_t)fr->nb_samples * ch);
                            for (int s = 0; s < fr->nb_samples; s++)
                                for (int c = 0; c < ch; c++) buf[(size_t)s * ch + c] = ((int16_t*)fr->data[c])[s];
                            SDL_QueueAudio(adev, buf.data(), (Uint32)(buf.size() * 2));
                        }
                    } else {
                        SDL_ClearQueuedAudio(adev);
                    }
                    av_frame_unref(fr);
                }
            }
        }
        av_packet_unref(pkt);
    }

out:
    if (rec_) rec_close();
    if (adev) SDL_CloseAudioDevice(adev);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&vdec);
    avcodec_free_context(&adec);
    if (fmt) avformat_close_input(&fmt);
    if (!fail.empty()) set_status(cam_.name + " (" + label + "): " + fail);
}

// ---------------------------------------------------------------- recording

void Player::rec_open(AVFormatContext* in, int vi, int ai) {
    std::string dir, prefix;
    {
        std::lock_guard<std::mutex> lk(mx_);
        dir = rec_dir_;
        prefix = rec_prefix_;
    }
    if (!mkdirs(dir, 0755)) {
        set_status(tr("Recording: cannot create ") + dir);
        rec_want_ = false;
        return;
    }
    std::string file = dir + "/" + prefix + "_" + timestamp_now() + ".mkv";
    for (int attempt = 0; attempt < 2; attempt++) {
        bool with_audio = ai >= 0 && attempt == 0;
        AVFormatContext* o = nullptr;
        if (avformat_alloc_output_context2(&o, nullptr, "matroska", file.c_str()) < 0 || !o) break;
        rec_map_[0] = rec_map_[1] = -1;
        int src[2] = {vi, with_audio ? ai : -1};
        for (int k = 0; k < 2; k++) {
            if (src[k] < 0) continue;
            AVStream* s = avformat_new_stream(o, nullptr);
            avcodec_parameters_copy(s->codecpar, in->streams[src[k]]->codecpar);
            s->codecpar->codec_tag = 0;
            s->time_base = in->streams[src[k]]->time_base;
            rec_map_[k] = s->index;
        }
        if (avio_open(&o->pb, file.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_free_context(o);
            break;
        }
        if (avformat_write_header(o, nullptr) < 0) {
            avio_closep(&o->pb);
            avformat_free_context(o);
            unlink(file.c_str());
            continue;  // retry without audio
        }
        rec_ = o;
        rec_t0_ = AV_NOPTS_VALUE;
        rec_wait_key_ = true;
        {
            std::lock_guard<std::mutex> lk(mx_);
            rec_file_ = file;
        }
        logf("recording to %s", file.c_str());
        return;
    }
    set_status(tr("Recording failed: cannot write ") + file);
    rec_want_ = false;
}

void Player::rec_write(AVFormatContext* in, AVPacket* pkt, int vi, int ai) {
    int k = pkt->stream_index == vi ? 0 : (pkt->stream_index == ai ? 1 : -1);
    if (k < 0 || rec_map_[k] < 0) return;
    if (rec_wait_key_) {  // start every file with a key frame
        if (k != 0 || !(pkt->flags & AV_PKT_FLAG_KEY)) return;
        rec_wait_key_ = false;
    }
    AVRational itb = in->streams[pkt->stream_index]->time_base;
    int64_t ts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
    if (ts == AV_NOPTS_VALUE) return;
    if (rec_t0_ == AV_NOPTS_VALUE) rec_t0_ = av_rescale_q(ts, itb, AV_TIME_BASE_Q);
    int64_t off = av_rescale_q(rec_t0_, AV_TIME_BASE_Q, itb);
    AVPacket* c = av_packet_clone(pkt);
    if (!c) return;
    if (c->pts != AV_NOPTS_VALUE) c->pts -= off;
    if (c->dts != AV_NOPTS_VALUE) c->dts -= off;
    if ((c->dts != AV_NOPTS_VALUE && c->dts < 0) || (c->pts != AV_NOPTS_VALUE && c->pts < 0)) {
        av_packet_free(&c);
        return;
    }
    AVStream* os = rec_->streams[rec_map_[k]];
    av_packet_rescale_ts(c, itb, os->time_base);
    c->stream_index = os->index;
    c->pos = -1;
    int r = av_interleaved_write_frame(rec_, c);
    av_packet_free(&c);
    if (r < 0) {
        set_status(tr("Recording stopped: ") + averr(r));
        rec_close();
        rec_want_ = false;
    }
}

void Player::rec_close() {
    if (!rec_) return;
    av_write_trailer(rec_);
    avio_closep(&rec_->pb);
    avformat_free_context(rec_);
    rec_ = nullptr;
    std::string f;
    {
        std::lock_guard<std::mutex> lk(mx_);
        f = rec_file_;
        rec_file_.clear();
    }
    logf("recording saved: %s", f.c_str());
}

// ----------------------------------------------------------------- snapshot

bool save_jpeg(const AVFrame* src, const std::string& path, std::string& err) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        err = tr("JPEG encoder missing");
        return false;
    }
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    AVFrame* f = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    SwsContext* sws = nullptr;
    bool ok = false;

    bool full = src->format == AV_PIX_FMT_YUVJ420P ||
                (src->format == AV_PIX_FMT_YUV420P && src->color_range == AVCOL_RANGE_JPEG);
    if (full) {
        av_frame_ref(f, src);
    } else {
        f->format = AV_PIX_FMT_YUVJ420P;
        f->width = src->width;
        f->height = src->height;
        if (av_frame_get_buffer(f, 0) < 0) {
            err = tr("out of memory");
            goto done;
        }
        sws = sws_getContext(src->width, src->height, (AVPixelFormat)src->format, f->width, f->height,
                             AV_PIX_FMT_YUVJ420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            err = tr("unsupported pixel format");
            goto done;
        }
        sws_scale(sws, src->data, src->linesize, 0, src->height, f->data, f->linesize);
    }
    f->format = AV_PIX_FMT_YUVJ420P;
    f->color_range = AVCOL_RANGE_JPEG;
    f->pts = 0;
    f->quality = FF_QP2LAMBDA * 2;

    enc->width = f->width;
    enc->height = f->height;
    enc->time_base = AVRational{1, 25};
    enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
    enc->color_range = AVCOL_RANGE_JPEG;
    enc->flags |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 2;
    if (avcodec_open2(enc, codec, nullptr) < 0) {
        err = tr("cannot open JPEG encoder");
        goto done;
    }
    if (avcodec_send_frame(enc, f) < 0 || avcodec_send_frame(enc, nullptr) < 0 ||
        avcodec_receive_packet(enc, pkt) < 0) {
        err = tr("JPEG encoding failed");
        goto done;
    }
    {
        size_t sl = path.rfind('/');
        if (sl != std::string::npos) mkdirs(path.substr(0, sl), 0755);
        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) {
            err = tr("cannot write ") + path;
            goto done;
        }
        ok = fwrite(pkt->data, 1, (size_t)pkt->size, fp) == (size_t)pkt->size;
        ok = (fclose(fp) == 0) && ok;
        if (!ok) err = tr("write error");
    }
done:
    sws_freeContext(sws);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&enc);
    return ok;
}
