#include "input_h264_color.h"
#include "input_internal.h"
#include <cstring>

namespace {
struct RtspProbe {
  AVFormatContext* format = avformat_alloc_context();
  AVDictionary* options = nullptr;
  AVPacket* packet = nullptr;
  int64_t deadline_us = 0;

  ~RtspProbe() {
    av_packet_free(&packet);
    av_dict_free(&options);
    avformat_close_input(&format);
  }

  static int Interrupt(void* context) {
    return hvinput::NowUs() >= static_cast<RtspProbe*>(context)->deadline_us;
  }
};
} // namespace

extern "C" HV_INPUT_API int HV_INPUT_CALL HV_Input_ProbeRtsp(
    const char* url, int timeout_ms, int* width, int* height) {
  if (!width || !height) return AVERROR(EINVAL);
  *width = 0;
  *height = 0;
  if (!url || timeout_ms <= 0 ||
      (std::strncmp(url, "rtsp://", 7) != 0 && std::strncmp(url, "rtsps://", 8) != 0))
    return AVERROR(EINVAL);

  hvinput::InstallSafeLog();
  RtspProbe probe;
  if (!probe.format) return AVERROR(ENOMEM);
  probe.deadline_us = hvinput::NowUs() + int64_t(timeout_ms) * 1000;
  probe.format->interrupt_callback = {RtspProbe::Interrupt, &probe};
  int result = av_dict_set(&probe.options, "rtsp_transport", "tcp", 0);
  if (result < 0) return result;
  result = avformat_open_input(&probe.format, url, nullptr, &probe.options);
  if (result < 0) return result;

  unsigned video_index = 0;
  while (video_index < probe.format->nb_streams &&
         probe.format->streams[video_index]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    ++video_index;

  if (video_index == probe.format->nb_streams) return AVERROR_STREAM_NOT_FOUND;
  const auto* parameters = probe.format->streams[video_index]->codecpar;
  if (parameters->codec_id != AV_CODEC_ID_H264) return AVERROR_INVALIDDATA;
  if (parameters->width > 0 && parameters->height > 0) {
    *width = parameters->width;
    *height = parameters->height;
    return 0;
  }
  if (parameters->extradata_size > 0 && hvinput::ParseH264Geometry(
          parameters->extradata, size_t(parameters->extradata_size), *width, *height))
    return 0;

  // Cameras can send the SPS in-band. Read compressed video until its SPS
  // arrives, with the same deadline covering setup and reads.
  probe.packet = av_packet_alloc();
  if (!probe.packet) return AVERROR(ENOMEM);
  while (!RtspProbe::Interrupt(&probe)) {
    result = av_read_frame(probe.format, probe.packet);
    if (result < 0) return result;
    const bool found = probe.packet->stream_index == int(video_index) && probe.packet->size > 0 &&
        hvinput::ParseH264Geometry(probe.packet->data, size_t(probe.packet->size), *width, *height);
    av_packet_unref(probe.packet);
    if (found) return 0;
  }
  return AVERROR(ETIMEDOUT);
}
