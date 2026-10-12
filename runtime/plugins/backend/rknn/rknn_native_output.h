#pragma once
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace humanvision::runtime::rknn {
// Conservative native-output adapter. Only a queried dense half tensor with
// exactly the public logical order is eligible. Blocked/channel-padded layouts
// need a separate, explicit adapter; never guess their addressing from byte size.
struct NativeOutputLayout {
    uint32_t elements=0;
    bool Configure(uint32_t logical_rank,const uint64_t* logical,
                   uint32_t native_rank,const uint64_t* native,
                   uint64_t native_elements,uint64_t allocation_bytes) noexcept {
        elements=0;
        if(!logical||!native||!logical_rank||logical_rank>8||native_rank!=logical_rank)return false;
        uint64_t count=1;
        for(uint32_t i=0;i<logical_rank;++i) {
            if(!logical[i]||logical[i]!=native[i]||logical[i]>2097152/count)return false;
            count*=logical[i];
        }
        if(count!=native_elements||allocation_bytes!=count*2)return false;
        elements=uint32_t(count);return true;
    }
    bool Decode(const void* source,uint32_t source_bytes,float* destination,uint32_t destination_bytes) const noexcept {
        if(!elements||!source||!destination||source_bytes<elements*2||destination_bytes!=elements*4)return false;
        const auto* bytes=static_cast<const unsigned char*>(source);
        uint32_t i=0;
#if defined(__aarch64__)
        // Normal binary16 values widen exactly on baseline ARM64 hardware.
        // Subnormals use integer normalization, preserving them even if the
        // caller enables FPCR flush-to-zero. No optional FP16 arithmetic needed.
        for(;i+4<=elements;) {
            const auto packed=vreinterpret_u16_u8(vld1_u8(bytes+i*2));
            const auto half=vmovl_u16(packed);
            const auto magnitude=vandq_u32(half,vdupq_n_u32(0x7fff));
            if(vmaxvq_u32(vcgeq_u32(magnitude,vdupq_n_u32(0x7c00))))return false;
            const auto has_subnormal=vandq_u32(vcgtq_u32(magnitude,vdupq_n_u32(0)),
                                              vcltq_u32(magnitude,vdupq_n_u32(0x0400)));
            if(!vmaxvq_u32(has_subnormal)) {
                vst1q_f32(destination+i,vcvt_f32_f16(vreinterpret_f16_u16(packed)));
                i+=4;continue;
            }
            const auto sign=vshlq_n_u32(vandq_u32(half,vdupq_n_u32(0x8000)),16);
            auto expanded=vaddq_u32(vshlq_n_u32(magnitude,13),vdupq_n_u32(0x38000000));
            const auto leading=vclzq_u32(magnitude);
            const auto fraction=vandq_u32(vshlq_u32(magnitude,
                vreinterpretq_s32_u32(vsubq_u32(leading,vdupq_n_u32(8)))),vdupq_n_u32(0x7fffff));
            const auto subnormal=vorrq_u32(fraction,vshlq_n_u32(vsubq_u32(vdupq_n_u32(134),leading),23));
            expanded=vbslq_u32(vcltq_u32(magnitude,vdupq_n_u32(0x0400)),subnormal,expanded);
            expanded=vbicq_u32(expanded,vceqq_u32(magnitude,vdupq_n_u32(0)));
            vst1q_f32(destination+i,vreinterpretq_f32_u32(vorrq_u32(sign,expanded)));
            i+=4;
        }
#endif
        for(;i<elements;++i) {
            uint16_t half;std::memcpy(&half,bytes+i*2,2);
            uint32_t exponent=(half>>10)&31,mantissa=half&1023;
            if(exponent==31)return false; // caller rejects the entire frame/falls back
            uint32_t bits=uint32_t(half&0x8000)<<16;
            if(exponent)bits|=((exponent+112)<<23)|(mantissa<<13);
            else if(mantissa) {
                int e=-14;
                while((mantissa&1024)==0){mantissa<<=1;--e;}
                bits|=(uint32_t(e+127)<<23)|((mantissa&1023)<<13);
            }
            std::memcpy(destination+i,&bits,4);
        }
        return true;
    }
};
}
