#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <jni.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto tag = "HVSurfaceAB";
constexpr uint64_t fnv_offset = 14695981039346656037ULL;

uint64_t Hash(uint64_t state, const void* bytes, size_t size) {
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) {
        state = (state ^ data[i]) * 1099511628211ULL;
    }
    return state;
}

std::string String(JNIEnv* env, jstring value) {
    const auto* utf = env->GetStringUTFChars(value, nullptr);
    if (!utf) return {};
    std::string result(utf);
    env->ReleaseStringUTFChars(value, utf);
    return result;
}

struct Packet {
    std::vector<uint8_t> bytes;
    int64_t pts;
    int64_t dts;
    int flags;
    Clock::duration arrival;
};

struct Corpus {
    std::vector<uint8_t> csd;
    AVRational time_base{};
    std::vector<Packet> packets;
    uint64_t digest = fnv_offset;
};

struct Demux {
    AVFormatContext* format = avformat_alloc_context();
    AVPacket* packet = av_packet_alloc();
    Clock::time_point deadline = Clock::now() + 20s;

    ~Demux() {
        av_packet_free(&packet);
        avformat_close_input(&format);
    }
};

bool Capture(const std::string& url, size_t count, Corpus& corpus) {
    Demux input;
    if (!input.format || !input.packet) return false;
    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();
    input.format->interrupt_callback = {[](void* context) -> int {
        return Clock::now() >= static_cast<Demux*>(context)->deadline;
    }, &input};
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    const int open = avformat_open_input(&input.format, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    __android_log_print(ANDROID_LOG_INFO, tag, "capture_open result=%d avformat=%u", open, avformat_version());
    if (open < 0) return false;

    // Match the Input decoder. RTSP SDP supplies CSD without probing a CPU decode.
    int video = -1;
    for (unsigned i = 0; i < input.format->nb_streams; ++i) {
        if (input.format->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        video = static_cast<int>(i);
        break;
    }
    if (video < 0) return false;
    const auto* stream = input.format->streams[video];
    const auto* params = stream->codecpar;
    if (params->codec_id != AV_CODEC_ID_H264) return false;
    if (params->extradata_size > 0) corpus.csd.assign(params->extradata, params->extradata + params->extradata_size);
    corpus.time_base = stream->time_base;
    corpus.packets.reserve(count);
    unsigned dropped = 0;
    size_t total_bytes = 0;
    Clock::time_point first_arrival;
    while (corpus.packets.size() < count) {
        const int result = av_read_frame(input.format, input.packet);
        if (result < 0) {
            __android_log_print(ANDROID_LOG_ERROR, tag, "capture_read result=%d captured=%zu", result, corpus.packets.size());
            return false;
        }
        const auto* packet = input.packet;
        if (packet->stream_index == video && packet->size > 0) {
            if (!corpus.packets.empty() || (packet->flags & AV_PKT_FLAG_KEY) != 0) {
                if (corpus.packets.empty()) first_arrival = Clock::now();
                if (total_bytes + static_cast<size_t>(packet->size) > 128U * 1024U * 1024U) return false;
                Packet entry{{packet->data, packet->data + packet->size}, packet->pts, packet->dts,
                             packet->flags, Clock::now() - first_arrival};
                total_bytes += entry.bytes.size();
                corpus.digest = Hash(corpus.digest, entry.bytes.data(), entry.bytes.size());
                corpus.digest = Hash(corpus.digest, &entry.pts, sizeof(entry.pts));
                corpus.digest = Hash(corpus.digest, &entry.dts, sizeof(entry.dts));
                corpus.digest = Hash(corpus.digest, &entry.flags, sizeof(entry.flags));
                corpus.packets.push_back(std::move(entry));
            } else {
                ++dropped;
            }
        }
        av_packet_unref(input.packet);
    }
    const auto& first = corpus.packets.front();
    __android_log_print(ANDROID_LOG_INFO, tag,
        "capture_complete packets=%zu dropped_before_key=%u bytes=%zu time_base=%d/%d csd_bytes=%zu csd_fnv=%016" PRIx64
        " packet_fnv=%016" PRIx64 " first_size=%zu first_pts=%" PRId64 " first_dts=%" PRId64 " first_flags=%d first_fnv=%016" PRIx64,
        corpus.packets.size(), dropped, total_bytes, corpus.time_base.num, corpus.time_base.den, corpus.csd.size(),
        Hash(fnv_offset, corpus.csd.data(), corpus.csd.size()), corpus.digest, first.bytes.size(), first.pts, first.dts, first.flags,
        Hash(fnv_offset, first.bytes.data(), first.bytes.size()));
    return true;
}

struct Decoder {
    AImageReader* reader = nullptr;
    AMediaCodec* codec = nullptr;
    AMediaFormat* format = nullptr;
    bool started = false;

    ~Decoder() {
        if (codec) {
            if (started) AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
        }
        if (format) AMediaFormat_delete(format);
        if (reader) AImageReader_delete(reader);
    }
};

void OutputFormat(AMediaCodec* codec, const char* variant) {
    auto* format = AMediaCodec_getOutputFormat(codec);
    if (!format) return;
    using GetRect = bool (*)(AMediaFormat*, const char*, int32_t*, int32_t*, int32_t*, int32_t*);
    const auto get_rect = reinterpret_cast<GetRect>(dlsym(RTLD_DEFAULT, "AMediaFormat_getRect"));
    int32_t left = -1, top = -1, right = -1, bottom = -1;
    bool has_crop = get_rect && get_rect(format, "crop", &left, &top, &right, &bottom);
    if (!has_crop) {
        has_crop = AMediaFormat_getInt32(format, "crop-left", &left) && AMediaFormat_getInt32(format, "crop-top", &top) &&
            AMediaFormat_getInt32(format, "crop-right", &right) && AMediaFormat_getInt32(format, "crop-bottom", &bottom);
    }
    __android_log_print(ANDROID_LOG_INFO, tag, "format variant=%s crop_known=%d crop_inclusive=%d,%d,%d,%d format=%s",
                        variant, has_crop, left, top, right, bottom, AMediaFormat_toString(format));
    AMediaFormat_delete(format);
}

void FirstImage(AImage* image, const char* variant) {
    int32_t width = 0, height = 0;
    int64_t timestamp = 0;
    AImageCropRect crop{};
    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);
    AImage_getTimestamp(image, &timestamp);
    AImage_getCropRect(image, &crop);
    AHardwareBuffer* buffer = nullptr;
    AHardwareBuffer_Desc desc{};
    if (AImage_getHardwareBuffer(image, &buffer) == AMEDIA_OK && buffer) AHardwareBuffer_describe(buffer, &desc);
    __android_log_print(ANDROID_LOG_INFO, tag,
        "first_image variant=%s size=%dx%d crop_exclusive=%d,%d,%d,%d pts_ns=%" PRId64 " ahb=%ux%u stride=%u format=%u usage=%" PRIu64,
        variant, width, height, crop.left, crop.top, crop.right, crop.bottom, timestamp,
        desc.width, desc.height, desc.stride, desc.format, desc.usage);
}

bool IsStartCode(const std::vector<uint8_t>& csd, size_t i, size_t& length) {
    if (i + 3 > csd.size() || csd[i] != 0 || csd[i + 1] != 0) return false;
    if (csd[i + 2] == 1) { length = 3; return true; }
    if (i + 4 <= csd.size() && csd[i + 2] == 0 && csd[i + 3] == 1) { length = 4; return true; }
    return false;
}

bool SetCsd(AMediaFormat* format, std::vector<uint8_t> const& csd, bool separate) {
    if (!separate) {
        if (!csd.empty()) AMediaFormat_setBuffer(format, "csd-0", csd.data(), csd.size());
        return true;
    }
    std::vector<uint8_t> sps, pps;
    for (size_t start = 0, marker = 0; start < csd.size();) {
        if (!IsStartCode(csd, start, marker) || start + marker >= csd.size()) break;
        const size_t payload = start + marker;
        size_t next_marker = 0, end = payload + 1;
        while (end < csd.size() && !IsStartCode(csd, end, next_marker)) ++end;
        switch (csd[payload] & 31) {
        case 7: sps.insert(sps.end(), csd.begin() + start, csd.begin() + end); break;
        case 8: pps.insert(pps.end(), csd.begin() + start, csd.begin() + end); break;
        }
        start = end;
    }
    if (sps.empty() || pps.empty()) return false;
    AMediaFormat_setBuffer(format, "csd-0", sps.data(), sps.size());
    AMediaFormat_setBuffer(format, "csd-1", pps.data(), pps.size());
    return true;
}

bool Replay(const Corpus& corpus, ANativeWindow* surface, const std::string& codec_name,
            int width, int height, const std::string& variant) {
    const auto name = variant.c_str();
    const bool display = variant.compare(0, 7, "surface") == 0;
    const bool byte_buffer = variant == "byte-buffer";
    const bool yuv = variant == "yuv";
    const bool private_overlay = variant == "private-overlay";
    int reader_max_images = 4;
    if (variant == "private-max3") reader_max_images = 3;
    if (variant == "private-max2") reader_max_images = 2;
    if (variant == "private-max1") reader_max_images = 1;
    const bool coded = variant.find("-coded") != std::string::npos;
    const bool split_csd = variant.find("-split-csd") != std::string::npos;
    if (!display && !byte_buffer && !yuv && !private_overlay && variant.compare(0, 7, "private") != 0) return false;
    const int configured_width = coded ? (width + 15) & ~15 : width;
    const int configured_height = coded ? (height + 15) & ~15 : height;
    unsigned queued = 0, output = 0, released = 0, acquired = 0, outputs_without_input_pts = 0;
    int error = 0;
    const char* stage = "complete";
    const auto fail = [&](const char* at, int status) {
        error = status ? status : -1;
        stage = at;
        __android_log_print(ANDROID_LOG_ERROR, tag, "failure variant=%s stage=%s error=%d", name, stage, error);
    };
    __android_log_print(ANDROID_LOG_INFO, tag,
        "replay_start variant=%s codec=%s config=%dx%d reader=%dx%d packets=%zu packet_fnv=%016" PRIx64 " csd_fnv=%016" PRIx64 " csd_split=%d reader_max_images=%d",
        name, codec_name.c_str(), configured_width, configured_height, width, height, corpus.packets.size(), corpus.digest,
        Hash(fnv_offset, corpus.csd.data(), corpus.csd.size()), split_csd, reader_max_images);
    {
        Decoder decoder;
        auto* window = display ? surface : nullptr;
        if (!display && !byte_buffer) {
            const int format = yuv ? AIMAGE_FORMAT_YUV_420_888 : AIMAGE_FORMAT_PRIVATE;
            uint64_t usage = yuv ? AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN : AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
            if (private_overlay) usage |= AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
            int status = AImageReader_newWithUsage(width, height, format, usage, reader_max_images, &decoder.reader);
            if (status == AMEDIA_OK) status = AImageReader_getWindow(decoder.reader, &window);
            if (status != AMEDIA_OK || !window) fail("reader_window", status);
        }
        if (!error) {
            decoder.codec = AMediaCodec_createCodecByName(codec_name.c_str());
            decoder.format = AMediaFormat_new();
            if (!decoder.codec || !decoder.format) fail("codec_create", -1);
        }
        if (!error) {
            AMediaFormat_setString(decoder.format, AMEDIAFORMAT_KEY_MIME, "video/avc");
            AMediaFormat_setInt32(decoder.format, AMEDIAFORMAT_KEY_WIDTH, configured_width);
            AMediaFormat_setInt32(decoder.format, AMEDIAFORMAT_KEY_HEIGHT, configured_height);
            if (!SetCsd(decoder.format, corpus.csd, split_csd)) fail("split_csd", -1);
        }
        if (!error) {
            const auto status = AMediaCodec_configure(decoder.codec, decoder.format, window, nullptr, 0);
            if (status != AMEDIA_OK) fail("configure", status);
        }
        if (!error) {
            const auto status = AMediaCodec_start(decoder.codec);
            if (status != AMEDIA_OK) fail("codec_start", status);
            decoder.started = !error;
        }
        const auto started = Clock::now();
        auto last_progress = started;
        auto last_input = started;
        std::vector<int64_t> expected_pts;
        expected_pts.reserve(corpus.packets.size());
        while (!error && Clock::now() - last_progress < 5s && Clock::now() - started < 22s) {
            const auto now = Clock::now();
            if (queued < corpus.packets.size() && now - started >= corpus.packets[queued].arrival) {
                const auto index = AMediaCodec_dequeueInputBuffer(decoder.codec, 0);
                if (index >= 0) {
                    const auto& packet = corpus.packets[queued];
                    size_t capacity = 0;
                    auto* data = AMediaCodec_getInputBuffer(decoder.codec, index, &capacity);
                    if (!data || packet.bytes.size() > capacity) {
                        fail("input_capacity", -1);
                        break;
                    }
                    std::memcpy(data, packet.bytes.data(), packet.bytes.size());
                    const int64_t pts_us = packet.pts == AV_NOPTS_VALUE ? 0 :
                        av_rescale_q(packet.pts, corpus.time_base, AVRational{1, 1000000});
                    const auto status = AMediaCodec_queueInputBuffer(decoder.codec, index, 0, packet.bytes.size(), pts_us, 0);
                    if (status != AMEDIA_OK) fail("queue_input", status);
                    else {
                        expected_pts.push_back(pts_us);
                        ++queued;
                        last_progress = last_input = now;
                    }
                } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                    fail("dequeue_input", static_cast<int>(index));
                }
            }
            AMediaCodecBufferInfo info{};
            const auto index = AMediaCodec_dequeueOutputBuffer(decoder.codec, &info, 2000);
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) OutputFormat(decoder.codec, name);
            else if (index >= 0) {
                if ((info.flags & (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG | AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)) == 0) {
                    ++output;
                    const auto count = static_cast<unsigned>(std::count(expected_pts.begin(), expected_pts.end(), info.presentationTimeUs));
                    if (count == 0) ++outputs_without_input_pts;
                    if (output <= 3) {
                        __android_log_print(ANDROID_LOG_INFO, tag,
                            "output variant=%s number=%u pts_us=%" PRId64 " pts_matches=%u size=%d flags=%u queued=%u",
                            name, output, info.presentationTimeUs, count, info.size, info.flags, queued);
                    }
                }
                const auto status = AMediaCodec_releaseOutputBuffer(decoder.codec, index, !byte_buffer);
                if (status != AMEDIA_OK) fail("release_output", status);
                else ++released;
                last_progress = Clock::now();
            } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER && index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                fail("dequeue_output", static_cast<int>(index));
            }
            if (decoder.reader) {
                AImage* image = nullptr;
                int fence = -1;
                const auto status = AImageReader_acquireLatestImageAsync(decoder.reader, &image, &fence);
                if (status == AMEDIA_OK && image) {
                    if (++acquired == 1) FirstImage(image, name);
                    AImage_deleteAsync(image, fence);
                    last_progress = Clock::now();
                } else if (status != AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
                    if (fence >= 0) close(fence);
                    fail("acquire_image", status);
                }
            }
            if (queued == corpus.packets.size() && Clock::now() - last_input > 1500ms) break;
            std::this_thread::sleep_for(1ms);
        }
        if (!error && queued < corpus.packets.size()) fail("queued_input_timeout", -1);
        if (!error && output == 0) fail("no_decoded_output", -1);
        if (!error && decoder.reader && acquired == 0) fail("no_acquired_images", -1);
    }
    __android_log_print(ANDROID_LOG_INFO, tag,
        "replay_complete variant=%s ok=%d stage=%s error=%d queued=%u output=%u released=%u acquired=%u no_input_pts=%u packet_fnv=%016" PRIx64,
        name, error == 0, stage, error, queued, output, released, acquired, outputs_without_input_pts, corpus.digest);
    return error == 0;
}
}

extern "C" JNIEXPORT jint JNICALL Java_com_blazetc_humanvision_surfaceab_ProbeActivity_run(
    JNIEnv* env, jclass, jstring url, jobject surface, jstring codec, jint width, jint height, jint packets, jstring variants) {
    if (!url || !surface || !codec || !variants || width < 16 || height < 16 || width > 8192 || height > 8192 || packets < 2 || packets > 360) return -1;
    Corpus corpus;
    if (!Capture(String(env, url), packets, corpus)) return -2;
    auto* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return -3;
    std::istringstream inputs(String(env, variants));
    const auto codec_name = String(env, codec);
    int successes = 0;
    std::string variant;
    while (std::getline(inputs, variant, ',')) {
        if (Replay(corpus, window, codec_name, width, height, variant)) ++successes;
    }
    ANativeWindow_release(window);
    return successes;
}
