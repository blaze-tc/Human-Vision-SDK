#pragma once

namespace humanvision::gpu {
// One fixed workgroup reduces all logical CHW elements. No atomics or optional
// FP16 shader capabilities: packed half storage is decoded with unpackHalf2x16.
// The image variant reads the actual sampled image; golden always comes from
// independently hash-checked offline bytes uploaded at initialization.
inline constexpr char kParityShader[] = R"glsl(
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
#ifdef PARITY_IMAGE
layout(binding=0) uniform sampler2D actual_image;
#else
layout(binding=0) readonly buffer Actual { uint a[]; };
#endif
layout(binding=1) readonly buffer Golden { uint g[]; };
layout(binding=2) writeonly buffer Summary { uint result[]; };
layout(push_constant) uniform parameter {
    uint width; uint height; uint channels;
    uint a_row; uint a_channel; uint a_pack; uint a_type;
    uint g_row; uint g_channel; uint g_pack; uint g_type;
    float tolerance;
} p;
shared float sums[64]; shared float maxima[64];
shared uint mismatches[64]; shared uint firsts[64];
uint indexOf(uint i,uint row,uint channel,uint pack) {
    uint plane=p.width*p.height; uint c=i/plane;
    uint pixel=i%plane; uint y=pixel/p.width; uint x=pixel%p.width;
    if(channel==1u) return y*row+x*pack+c;
    return (c/pack)*channel+y*row+x*pack+c%pack;
}
float goldenValue(uint i) {
    uint j=indexOf(i,p.g_row,p.g_channel,p.g_pack);
    if(p.g_type==0u) return float((g[j/4u]>>((j%4u)*8u))&255u);
    if(p.g_type==1u) return unpackHalf2x16(g[j/2u])[j%2u];
    return uintBitsToFloat(g[j]);
}
float actualValue(uint i) {
#ifdef PARITY_IMAGE
    uint plane=p.width*p.height; uint pixel=i%plane;
    return texelFetch(actual_image,ivec2(pixel%p.width,pixel/p.width),0)[i/plane]*255.0;
#else
    uint j=indexOf(i,p.a_row,p.a_channel,p.a_pack);
    if(p.a_type==0u) return float((a[j/4u]>>((j%4u)*8u))&255u);
    if(p.a_type==1u) return unpackHalf2x16(a[j/2u])[j%2u];
    return uintBitsToFloat(a[j]);
#endif
}
void main() {
    uint lane=gl_LocalInvocationID.x; uint count=p.width*p.height*p.channels;
    float sum=0.0; float maximum=0.0; uint bad=0u; uint first=0xffffffffu;
    for(uint i=lane;i<count;i+=64u) {
        float av=actualValue(i); float gv=goldenValue(i);
        bool invalid=isnan(av)||isinf(av)||isnan(gv)||isinf(gv);
        float error=invalid ? 3.402823466e38 : abs(av-gv);
        sum+=error; maximum=max(maximum,error);
        if(invalid||error>p.tolerance) { bad++; first=min(first,i); }
    }
    sums[lane]=sum; maxima[lane]=maximum; mismatches[lane]=bad; firsts[lane]=first;
    barrier();
    for(uint step=32u;step>0u;step/=2u) {
        if(lane<step) { sums[lane]+=sums[lane+step]; maxima[lane]=max(maxima[lane],maxima[lane+step]);
            mismatches[lane]+=mismatches[lane+step]; firsts[lane]=min(firsts[lane],firsts[lane+step]); }
        barrier();
    }
    if(lane==0u) {
        result[0]=count; result[1]=mismatches[0]; result[2]=firsts[0];
        result[3]=floatBitsToUint(maxima[0]); result[4]=floatBitsToUint(sums[0]);
        for(uint i=0u;i<9u;i++) result[5u+i]=floatBitsToUint(actualValue(i*(count-1u)/8u));
    }
}
)glsl";
}
