#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace humanvision::runtime::rknn {
// The vendor query must establish NHWC/FLOAT16 before using this layout. Padding
// is explicit; no decoder/gralloc layout or channel packing is inferred here.
struct NativeInputLayout {
    uint32_t width=0,height=0,stride=0,channels=0,bytes=0;
    bool Configure(uint32_t w,uint32_t h,uint32_t row,uint32_t c,uint32_t capacity) {
        *this={};
        if(!w||!h||row<w||c<3||c>16||capacity>4u*1024*1024)return false;
        if(uint64_t(row)*c*2>capacity || uint64_t(h)>capacity/(uint64_t(row)*c*2))return false;
        width=w;height=h;stride=row;channels=c;bytes=capacity;return true;
    }
};
class NativeInputTransform {
    uint16_t table[3][256]{};
    bool ready=false;
    // Round to nearest, ties to even; normalization validation bounds all inputs
    // to finite binary16 values, including subnormal values and signed zero.
    static uint16_t Half(float value) {
        uint32_t bits;std::memcpy(&bits,&value,4);
        uint32_t sign=(bits>>16)&0x8000,mag=bits&0x7fffffff;
        if(mag<0x33000000)return uint16_t(sign);
        if(mag<0x38800000) {
            uint32_t shift=126-(mag>>23),mantissa=(mag&0x7fffff)|0x800000;
            uint32_t base=mantissa>>shift,remainder=mantissa&((uint32_t(1)<<shift)-1),half=uint32_t(1)<<(shift-1);
            return uint16_t(sign+base+(remainder>half||(remainder==half&&(base&1))));
        }
        uint32_t base=(mag-0x38000000)>>13,remainder=mag&0x1fff;
        return uint16_t(sign+base+(remainder>0x1000||(remainder==0x1000&&(base&1))));
    }
public:
    bool Configure(const float* mean,const float* norm) {
        ready=false;if(!mean||!norm)return false;
        for(unsigned c=0;c<3;++c)for(unsigned v=0;v<256;++v) {
            if(!std::isfinite(mean[c])||!std::isfinite(norm[c]))return false;
            const float value=(float(v)-mean[c])*norm[c];
            if(!std::isfinite(value)||std::fabs(value)>65504)return false;
            table[c][v]=Half(value);
        }
        ready=true;return true;
    }
    uint16_t Value(unsigned channel,uint8_t value)const{return channel<3?table[channel][value]:0;}
    bool Pack(const void* rgb,uint32_t rgb_bytes,const NativeInputLayout& layout,void* memory,uint32_t capacity)const {
        if(!ready||!rgb||!memory||reinterpret_cast<uintptr_t>(memory)%alignof(uint16_t)||!layout.bytes||capacity<layout.bytes||
            uint64_t(layout.width)*layout.height*3!=rgb_bytes)return false;
        // The buffer is reused; initialize padding as well, so prior frames cannot
        // influence an implementation that vector-loads a complete padded pixel.
        std::memset(memory,0,layout.bytes);
        auto* dst=static_cast<uint16_t*>(memory);auto* src=static_cast<const uint8_t*>(rgb);
        for(uint32_t y=0;y<layout.height;++y) {
            auto* row=dst+size_t(y)*layout.stride*layout.channels;
            for(uint32_t x=0;x<layout.width;++x)for(unsigned c=0;c<3;++c)
                row[size_t(x)*layout.channels+c]=table[c][*src++];
        }
        return true;
    }
};
}
