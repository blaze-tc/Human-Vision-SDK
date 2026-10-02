#include "input_internal.h"
#include <algorithm>
namespace hvinput {
void Session::Publish(AVFrame *frame, SwsContext *&scaler, AVRational time_base,
                      int64_t received) {
  if (frame->width < 1 || frame->height < 1) {
    SetError("invalid decoded geometry", AVERROR_INVALIDDATA);
    return;
  }
  double scale = std::min(1.0, std::min(double(max_width) / frame->width,
                                        double(max_height) / frame->height));
  int width = std::max(1, int(frame->width * scale)),
      height = std::max(1, int(frame->height * scale));
  scaler = sws_getCachedContext(scaler, frame->width, frame->height,
                                static_cast<AVPixelFormat>(frame->format),
                                width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                nullptr, nullptr, nullptr);
  if (!scaler) {
    SetError("create RGBA converter", AVERROR(ENOMEM));
    return;
  }
  // Preserve decoded matrix/range during YUV->full-range RGB conversion.
  int matrix =
      frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601;
  if (frame->colorspace == AVCOL_SPC_BT2020_NCL ||
      frame->colorspace == AVCOL_SPC_BT2020_CL)
    matrix = SWS_CS_BT2020;
  const int *coefficients = sws_getCoefficients(matrix);
  if (sws_setColorspaceDetails(scaler, coefficients,
                               frame->color_range == AVCOL_RANGE_JPEG,
                               coefficients, 1, 0, 1 << 16, 1 << 16) < 0) {
    SetError("configure color range", AVERROR_INVALIDDATA);
    return;
  }
  scratch.resize(static_cast<size_t>(width) * height * 4);
  uint8_t *planes[] = {scratch.data(), nullptr, nullptr, nullptr};
  int strides[] = {width * 4, 0, 0, 0};
  if (sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes,
                strides) != height) {
    SetError("convert RGBA", AVERROR_INVALIDDATA);
    return;
  }
  const int64_t decoded = NowUs();
  std::lock_guard<std::mutex> lock(mutex);
  if (stop)
    return;
  // swscale adjusts matrix/range, not RGB gamut or transfer. Claim sRGB only
  // for explicitly compatible primaries AND transfer; all other contracts
  // remain unknown. Every source color contract change advances generation.
  uint32_t color = (frame->color_trc == AVCOL_TRC_IEC61966_2_1 &&
                    frame->color_primaries == AVCOL_PRI_BT709)
                       ? HV_INPUT_COLOR_SRGB
                       : 0;
  if (info.width &&
      (last_width != frame->width || last_height != frame->height ||
       last_format != frame->format || last_matrix != frame->colorspace ||
       last_range != frame->color_range || last_transfer != frame->color_trc ||
       last_primaries != frame->color_primaries))
    ++info.generation;
  last_width = frame->width;
  last_height = frame->height;
  last_format = frame->format;
  last_matrix = frame->colorspace;
  last_range = frame->color_range;
  last_transfer = frame->color_trc;
  last_primaries = frame->color_primaries;
  latest.swap(scratch);
  info.width = width;
  info.height = height;
  info.stride_bytes = width * 4;
  info.rgba_bytes = static_cast<uint32_t>(latest.size());
  ++info.sequence;
  info.received_timestamp_us = received;
  info.decoded_timestamp_us = decoded;
  info.pts_valid = frame->best_effort_timestamp != AV_NOPTS_VALUE;
  info.presentation_timestamp_us =
      info.pts_valid ? av_rescale_q(frame->best_effort_timestamp, time_base,
                                    AVRational{1, 1000000})
                     : 0;
  info.color_space = color;
  error.clear();
  state = HV_INPUT_STREAMING;
}
} // namespace hvinput
