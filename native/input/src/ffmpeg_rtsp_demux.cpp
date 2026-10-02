#include "input_internal.h"
#include <mutex>
namespace hvinput {
// FFmpeg's default global callback can print complete input URLs/userinfo
// before our error path runs. Suppress this plugin's library logging at the
// callback boundary (including decoder worker logs). Public errors use
// operation+av_strerror, never URL or arbitrary FFmpeg printf strings. This
// callback is process-global; hosts sharing the same FFmpeg DLL must not
// replace it with an unsafe logger.
static void SafeLog(void *, int, const char *, va_list) {}
void InstallSafeLog() {
  static std::once_flag once;
  std::call_once(once, [] {
    av_log_set_callback(SafeLog);
    avformat_network_init();
  });
}
struct DecodeResources {
  AVFormatContext *format = avformat_alloc_context();
  AVCodecContext *decoder = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  SwsContext *scaler = nullptr;
  ~DecodeResources() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
  }
};
void Session::Decode() {
  DecodeResources r;
  if (!r.format) {
    SetError("allocate demuxer", AVERROR(ENOMEM));
    return;
  }
  r.format->interrupt_callback = {Interrupt, this};
  AVDictionary *options = nullptr;
  av_dict_set(&options, "rtsp_transport", tcp ? "tcp" : "udp", 0);
  av_dict_set(&options, "flags", "low_delay", 0);
  av_dict_set(&options, "analyzeduration", "1000000", 0);
  av_dict_set(&options, "probesize", "1048576", 0);
  deadline = NowUs() + int64_t(timeout_ms) * 1000;
  int result = avformat_open_input(&r.format, url.c_str(), nullptr, &options);
  av_dict_free(&options);
  if (result < 0) {
    if (!stop)
      SetError("open RTSP", result);
    return;
  }
  deadline = NowUs() + int64_t(timeout_ms) * 1000;
  result = avformat_find_stream_info(r.format, nullptr);
  if (result < 0) {
    if (!stop)
      SetError("probe RTSP", result);
    return;
  }
  const AVCodec *codec = nullptr;
  int stream =
      av_find_best_stream(r.format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (stream < 0 || !codec) {
    SetError("select video stream",
             stream < 0 ? stream : AVERROR_DECODER_NOT_FOUND);
    return;
  }
  r.decoder = avcodec_alloc_context3(codec);
  if (!r.decoder) {
    SetError("allocate decoder", AVERROR(ENOMEM));
    return;
  }
  result = avcodec_parameters_to_context(r.decoder,
                                         r.format->streams[stream]->codecpar);
  if (result < 0) {
    SetError("configure decoder", result);
    return;
  }
  r.decoder->thread_count = 2;
  result = avcodec_open2(r.decoder, codec, nullptr);
  if (result < 0) {
    SetError("open decoder", result);
    return;
  }
  r.frame = av_frame_alloc();
  r.packet = av_packet_alloc();
  if (!r.frame || !r.packet) {
    SetError("allocate decode buffers", AVERROR(ENOMEM));
    return;
  }
  while (!stop) {
    deadline = NowUs() + int64_t(timeout_ms) * 1000;
    result = av_read_frame(r.format, r.packet);
    const auto received = NowUs();
    if (result < 0) {
      if (!stop)
        SetError("read RTSP", result);
      return;
    }
    if (r.packet->stream_index != stream) {
      av_packet_unref(r.packet);
      continue;
    }
    // Drain on send EAGAIN and retry the SAME packet. Never discard arbitrary
    // P/B packets to implement latest-frame: only completed RGBA publications
    // overwrite.
    result = avcodec_send_packet(r.decoder, r.packet);
    if (result == AVERROR(EAGAIN)) {
      int drained;
      while (!stop &&
             (drained = avcodec_receive_frame(r.decoder, r.frame)) == 0) {
        Publish(r.frame, r.scaler, r.format->streams[stream]->time_base,
                received);
        av_frame_unref(r.frame);
      }
      if (!stop)
        result = avcodec_send_packet(r.decoder, r.packet);
    }
    av_packet_unref(r.packet);
    if (stop)
      return;
    if (result < 0) {
      SetError("submit compressed packet", result);
      return;
    }
    while (!stop && (result = avcodec_receive_frame(r.decoder, r.frame)) == 0) {
      Publish(r.frame, r.scaler, r.format->streams[stream]->time_base,
              received);
      av_frame_unref(r.frame);
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
      SetError("decode frame", result);
      return;
    }
  }
}
} // namespace hvinput
