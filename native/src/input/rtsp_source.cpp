#include "humanvision/humanvision_rtsp.h"
#include <atomic>
#include <algorithm>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace {
int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct RtspSource {
    std::string url;
    int max_width, max_height, timeout_ms;
    bool tcp;
    std::atomic<bool> stop{false};
    std::atomic<int64_t> deadline{0};
    std::atomic<int> state{1}; // 1 connecting, 2 streaming, 3 reconnecting, 4 stopped
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::vector<uint8_t> latest, scratch;
    int width = 0, height = 0;
    int64_t sequence = 0, timestamp = 0;
    ~RtspSource() { stop = true; wake.notify_all(); if (worker.joinable()) worker.join(); }
    static int Interrupt(void* value) {
        auto* source = static_cast<RtspSource*>(value);
        return source->stop || NowUs() > source->deadline;
    }
    void Decode() {
        AVFormatContext* format = avformat_alloc_context();
        if (!format) return;
        format->interrupt_callback = {Interrupt, this};
        AVDictionary* options = nullptr;
        av_dict_set(&options, "rtsp_transport", tcp ? "tcp" : "udp", 0);
        av_dict_set(&options, "fflags", "nobuffer", 0);
        av_dict_set(&options, "flags", "low_delay", 0);
        av_dict_set(&options, "analyzeduration", "1000000", 0);
        av_dict_set(&options, "probesize", "1048576", 0);
        deadline = NowUs() + static_cast<int64_t>(timeout_ms) * 1000;
        int opened = avformat_open_input(&format, url.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (opened < 0) { if (format) avformat_close_input(&format); return; }
        AVCodecContext* decoder = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* packet = nullptr;
        SwsContext* scaler = nullptr;
        do {
            deadline = NowUs() + static_cast<int64_t>(timeout_ms) * 1000;
            if (avformat_find_stream_info(format, nullptr) < 0) break;
            const AVCodec* codec = nullptr;
            int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
            if (stream < 0 || !codec) break;
            decoder = avcodec_alloc_context3(codec);
            if (!decoder || avcodec_parameters_to_context(decoder, format->streams[stream]->codecpar) < 0) break;
            decoder->thread_count = 2;
            if (avcodec_open2(decoder, codec, nullptr) < 0) break;
            frame = av_frame_alloc(); packet = av_packet_alloc();
            if (!frame || !packet) break;
            while (!stop) {
                deadline = NowUs() + static_cast<int64_t>(timeout_ms) * 1000;
                if (av_read_frame(format, packet) < 0) break;
                if (packet->stream_index != stream) { av_packet_unref(packet); continue; }
                int sent = avcodec_send_packet(decoder, packet);
                av_packet_unref(packet);
                if (sent < 0) continue;
                while (!stop && avcodec_receive_frame(decoder, frame) == 0) {
                    if (frame->width <= 0 || frame->height <= 0) continue;
                    double scale = std::min(1.0, std::min(double(max_width) / frame->width, double(max_height) / frame->height));
                    int w = std::max(1, int(frame->width * scale)), h = std::max(1, int(frame->height * scale));
                    scaler = sws_getCachedContext(scaler, frame->width, frame->height,
                        static_cast<AVPixelFormat>(frame->format), w, h, AV_PIX_FMT_RGBA,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!scaler) break;
                    scratch.resize(static_cast<size_t>(w) * h * 4);
                    uint8_t* planes[] = {scratch.data(), nullptr, nullptr, nullptr};
                    int strides[] = {w * 4, 0, 0, 0};
                    if (sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides) != h) continue;
                    { std::lock_guard<std::mutex> lock(mutex);
                        latest.swap(scratch); width = w; height = h; timestamp = NowUs(); ++sequence;
                    }
                    state = 2;
                }
            }
        } while (false);
        sws_freeContext(scaler);
        av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&decoder);
        avformat_close_input(&format);
    }
    void Run() {
        while (!stop) {
            try { Decode(); } catch (...) { }
            if (stop) break;
            state = 3;
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::seconds(2), [this] { return stop.load(); });
        }
        state = 4;
    }
};
}

extern "C" HV_API void* HV_CALL HV_RtspOpen(const char* url, int width, int height, int tcp, int timeout_ms) {
    if (!url || std::string(url).rfind("rtsp://", 0) != 0 || width < 1 || height < 1 ||
        width > 4096 || height > 4096 || timeout_ms < 100) return nullptr;
    try {
        auto source = std::make_unique<RtspSource>();
        source->url = url; source->max_width = width; source->max_height = height;
        source->tcp = tcp != 0; source->timeout_ms = timeout_ms;
        avformat_network_init();
        source->worker = std::thread(&RtspSource::Run, source.get());
        return source.release();
    } catch (...) { return nullptr; }
}
extern "C" HV_API int HV_CALL HV_RtspCopyFrame(void* handle, int64_t after_sequence, void* rgba,
    int capacity, int* width, int* height, int64_t* sequence, int64_t* timestamp) {
    auto* source = static_cast<RtspSource*>(handle);
    if (!source || !rgba || !width || !height || !sequence || !timestamp) return -1;
    std::lock_guard<std::mutex> lock(source->mutex);
    if (source->sequence <= after_sequence || source->state != 2) return 0;
    if (capacity < 0 || static_cast<size_t>(capacity) < source->latest.size()) return -1;
    std::memcpy(rgba, source->latest.data(), source->latest.size());
    *width = source->width; *height = source->height;
    *sequence = source->sequence; *timestamp = source->timestamp;
    return 1;
}
extern "C" HV_API int HV_CALL HV_RtspState(void* handle) {
    return handle ? static_cast<RtspSource*>(handle)->state.load() : 4;
}
extern "C" HV_API void HV_CALL HV_RtspClose(void* handle) {
    auto* source = static_cast<RtspSource*>(handle);
    if (!source) return;
    source->stop = true;
    source->wake.notify_all();
    try { std::thread([source] { delete source; }).detach(); }
    catch (...) { delete source; }
}
