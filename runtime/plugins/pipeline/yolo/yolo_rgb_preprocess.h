#pragma once
#include "humanvision/humanvision_c.h"
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace humanvision::runtime::yolo {
namespace rgb_detail {

// These qualified geometries have exact integer solutions to the existing OpenCV
// INTER_LINEAR uint8 contract. General ratios retain the reviewed interpolator.
template<int channels, bool bgr>
static inline void CopyOrHalf(const HV_VideoFrame& frame, uint8_t* output, bool half) noexcept {
    constexpr int width = 512, height = 288;
    const auto* source = static_cast<const uint8_t*>(frame.data);
    for (int y = 0; y < height; ++y) {
        const auto* top = source + size_t(y * (half ? 2 : 1)) * frame.stride_bytes;
        const auto* bottom = half ? top + frame.stride_bytes : top;
        auto* destination = output + size_t(y) * width * 3;
        if (!half && channels == 3 && !bgr) {
            std::memcpy(destination, top, width * 3);
            continue;
        }
        int x = 0;
#if defined(__aarch64__)
        // ARM64 NEON deinterleaves RGB(A); alpha/padding never enter the tensor.
        // Pairwise widening avoids byte overflow before exact rounded /4.
        const int step = half ? 8 : 16;
        for (; x + step <= width; x += step) {
            uint8x16_t a[3], b[3];
            const auto* input = top + x * (half ? 2 : 1) * channels;
            if constexpr (channels == 4) {
                const auto v = vld4q_u8(input);
                for (int c = 0; c < 3; ++c) a[c] = v.val[bgr ? 2-c : c];
            } else {
                const auto v = vld3q_u8(input);
                for (int c = 0; c < 3; ++c) a[c] = v.val[bgr ? 2-c : c];
            }
            if (!half) {
                const uint8x16x3_t pixels{{a[0], a[1], a[2]}};
                vst3q_u8(destination + x * 3, pixels);
                continue;
            }
            const auto* other = bottom + x * 2 * channels;
            if constexpr (channels == 4) {
                const auto v = vld4q_u8(other);
                for (int c = 0; c < 3; ++c) b[c] = v.val[bgr ? 2-c : c];
            } else {
                const auto v = vld3q_u8(other);
                for (int c = 0; c < 3; ++c) b[c] = v.val[bgr ? 2-c : c];
            }
            uint8x8x3_t pixels;
            for (int c = 0; c < 3; ++c) {
                const auto sum = vaddq_u16(vpaddlq_u8(a[c]), vpaddlq_u8(b[c]));
                pixels.val[c] = vshrn_n_u16(vaddq_u16(sum, vdupq_n_u16(2)), 2);
            }
            vst3_u8(destination + x * 3, pixels);
        }
#endif
        for (; x < width; ++x) for (int c = 0; c < 3; ++c) {
            const int offset = bgr ? 2-c : c;
            const int position = x * (half ? 2 : 1) * channels + offset;
            const int sum = half ? top[position] + top[position+channels] +
                                  bottom[position] + bottom[position+channels] : top[position];
            destination[x*3+c] = uint8_t(half ? (sum+2)>>2 : sum);
        }
    }
}
// Exact 1280x720 -> 512x288 (5:2) path used by the device's bounded readback.
// The original 11-bit horizontal weights are 512/1536, alternating by x;
// simplifying their staged shifts is exact only for this qualified geometry.
// The overlapping 16-pixel loads cover precisely 20 source pixels, including
// the final block. Never read across the last pixel into row padding/next row.
template<int channels, bool bgr>
static inline void CopyFiveToTwo(const HV_VideoFrame& frame, uint8_t* output) noexcept {
    const auto* source=static_cast<const uint8_t*>(frame.data);
    for(int y=0;y<288;++y) {
        const int first=(5*y+1)/2,wy=y%2?3:1;
        const auto* top=source+size_t(first)*frame.stride_bytes;
        const auto* bottom=top+frame.stride_bytes;
        auto* destination=output+size_t(y)*512*3;
        int x=0;
#if defined(__aarch64__)
        const uint8_t offsets0[8]={0,3,5,8,10,13,15,30};
        const uint8_t offsets1[8]={1,4,6,9,11,14,28,31};
        const uint16_t weights[8]={1,3,1,3,1,3,1,3};
        const auto index0=vld1_u8(offsets0),index1=vld1_u8(offsets1);
        const auto wx0=vld1q_u16(weights),wx1=vsubq_u16(vdupq_n_u16(4),wx0);
        for(;x<512;x+=8) {
            const int firstX=5*x/2;
            uint8x16_t ta[3],tb[3],ba[3],bb[3];
            if constexpr(channels==4) {
                const auto t0=vld4q_u8(top+firstX*channels),t1=vld4q_u8(top+(firstX+4)*channels);
                const auto b0=vld4q_u8(bottom+firstX*channels),b1=vld4q_u8(bottom+(firstX+4)*channels);
                for(int c=0;c<3;++c){const int k=bgr?2-c:c;ta[c]=t0.val[k];tb[c]=t1.val[k];ba[c]=b0.val[k];bb[c]=b1.val[k];}
            } else {
                const auto t0=vld3q_u8(top+firstX*channels),t1=vld3q_u8(top+(firstX+4)*channels);
                const auto b0=vld3q_u8(bottom+firstX*channels),b1=vld3q_u8(bottom+(firstX+4)*channels);
                for(int c=0;c<3;++c){const int k=bgr?2-c:c;ta[c]=t0.val[k];tb[c]=t1.val[k];ba[c]=b0.val[k];bb[c]=b1.val[k];}
            }
            uint8x8x3_t pixels;
            for(int c=0;c<3;++c) {
                const uint8x16x2_t t{{ta[c],tb[c]}},b{{ba[c],bb[c]}};
                const auto a=vaddq_u16(vmulq_u16(vmovl_u8(vqtbl2_u8(t,index0)),wx0),vmulq_u16(vmovl_u8(vqtbl2_u8(t,index1)),wx1));
                const auto other=vaddq_u16(vmulq_u16(vmovl_u8(vqtbl2_u8(b,index0)),wx0),vmulq_u16(vmovl_u8(vqtbl2_u8(b,index1)),wx1));
                const auto sum=vaddq_u16(vshrq_n_u16(vmulq_n_u16(a,wy),2),vshrq_n_u16(vmulq_n_u16(other,4-wy),2));
                pixels.val[c]=vshrn_n_u16(vaddq_u16(sum,vdupq_n_u16(2)),2);
            }
            vst3_u8(destination+x*3,pixels);
        }
#endif
        for(;x<512;++x)for(int c=0;c<3;++c) {
            const int firstX=(5*x+1)/2,wx=x%2?3:1,k=bgr?2-c:c;
            const int a=wx*top[firstX*channels+k]+(4-wx)*top[(firstX+1)*channels+k];
            const int b=wx*bottom[firstX*channels+k]+(4-wx)*bottom[(firstX+1)*channels+k];
            destination[x*3+c]=uint8_t(((wy*a)/4+((4-wy)*b)/4+2)/4);
        }
    }
}
} // namespace rgb_detail

// Output storage is caller-owned, exactly 512*288*3 bytes. False leaves it
// untouched so the caller can use the general path. No allocation or GPU wait.
static inline bool TryFastRgbPreprocess(const HV_VideoFrame& frame, uint8_t* output) noexcept {
    const bool half = frame.width == 1024 && frame.height == 576;
    const bool fiveToTwo = frame.width == 1280 && frame.height == 720;
    if (frame.struct_size < int32_t(sizeof(frame)) || !frame.data || !output ||
        (!half && !fiveToTwo && (frame.width != 512 || frame.height != 288))) return false;
    int channels;
    switch (frame.pixel_format) {
        case HV_PIXEL_RGB24: case HV_PIXEL_BGR24: channels=3; break;
        case HV_PIXEL_RGBA32: case HV_PIXEL_BGRA32: channels=4; break;
        default: return false;
    }
    const int row = frame.width * channels;
    if (frame.stride_bytes < row || frame.data_bytes <
        int64_t(frame.stride_bytes) * (frame.height-1) + row) return false;
    if(fiveToTwo) {
        switch(frame.pixel_format) {
            case HV_PIXEL_RGB24: rgb_detail::CopyFiveToTwo<3,false>(frame,output);break;
            case HV_PIXEL_BGR24: rgb_detail::CopyFiveToTwo<3,true>(frame,output);break;
            case HV_PIXEL_RGBA32: rgb_detail::CopyFiveToTwo<4,false>(frame,output);break;
            case HV_PIXEL_BGRA32: rgb_detail::CopyFiveToTwo<4,true>(frame,output);break;
            default:return false;
        }
        return true;
    }
    switch (frame.pixel_format) {
        case HV_PIXEL_RGB24: rgb_detail::CopyOrHalf<3,false>(frame,output,half); break;
        case HV_PIXEL_BGR24: rgb_detail::CopyOrHalf<3,true>(frame,output,half); break;
        case HV_PIXEL_RGBA32: rgb_detail::CopyOrHalf<4,false>(frame,output,half); break;
        case HV_PIXEL_BGRA32: rgb_detail::CopyOrHalf<4,true>(frame,output,half); break;
        default: return false;
    }
    return true;
}
} // namespace humanvision::runtime::yolo
