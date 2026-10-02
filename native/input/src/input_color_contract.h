#pragma once
#include <cstdint>
namespace hvinput {
enum class ColorMatrix:uint32_t { Unknown,Bt601,Bt709 };
enum class ColorRange:uint32_t { Unknown,Full,Limited };
struct ColorContract { ColorMatrix matrix=ColorMatrix::Unknown;ColorRange range=ColorRange::Unknown;uint32_t transfer=0,primaries=0; };
struct Rgb {float r,g,b;};struct Yuv {float y,u,v;};struct Point {float x,y;};
struct Transform {int width,height,left,top,right,bottom,rotation;bool mirror;};
Rgb DecodeReference(Yuv,const ColorContract&);Yuv EncodeReference(Rgb,const ColorContract&);
uint32_t OutputColorSpace(const ColorContract&);
Point MapOutputToSource(const Transform&,float,float);Point MapSourceToOutput(const Transform&,float,float);
}
