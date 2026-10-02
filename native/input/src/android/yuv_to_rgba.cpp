#include "input_color_contract.h"
#include <stdexcept>
namespace hvinput {
static void Coefficients(const ColorContract& c,float& kr,float& kb) {
 if(c.matrix==ColorMatrix::Bt601){kr=.299f;kb=.114f;}else if(c.matrix==ColorMatrix::Bt709){kr=.2126f;kb=.0722f;}else throw std::invalid_argument("Unknown YUV matrix");
 if(c.range==ColorRange::Unknown)throw std::invalid_argument("Unknown YUV range");
}
Yuv EncodeReference(Rgb a,const ColorContract& c){float kr,kb;Coefficients(c,kr,kb);float y=kr*a.r+(1-kr-kb)*a.g+kb*a.b;float u=(a.b-y)/(2*(1-kb)),v=(a.r-y)/(2*(1-kr));if(c.range==ColorRange::Limited)return {16.f/255+y*219.f/255,128.f/255+u*224.f/255,128.f/255+v*224.f/255};return {y,128.f/255+u,128.f/255+v};}
Rgb DecodeReference(Yuv a,const ColorContract& c){float kr,kb;Coefficients(c,kr,kb);float y=a.y,u=a.u-128.f/255,v=a.v-128.f/255;if(c.range==ColorRange::Limited){y=(y-16.f/255)*255.f/219;u*=255.f/224;v*=255.f/224;}float r=y+2*(1-kr)*v,b=y+2*(1-kb)*u;return {r,(y-kr*r-kb*b)/(1-kr-kb),b};}
uint32_t OutputColorSpace(const ColorContract& c){return c.transfer==13?1:0;} // explicit IEC61966-2-1 only
Point MapOutputToSource(const Transform& t,float x,float y){y=1-y;if(t.mirror)x=1-x;switch(t.rotation){case 0:break;case 90:{auto a=x;x=y;y=1-a;break;}case 180:x=1-x;y=1-y;break;case 270:{auto a=x;x=1-y;y=a;break;}default:throw std::invalid_argument("Rotation must be a quarter turn");}return {(t.left+x*(t.right-t.left))/t.width,(t.top+y*(t.bottom-t.top))/t.height};}
Point MapSourceToOutput(const Transform& t,float x,float y){x=(x*t.width-t.left)/(t.right-t.left);y=(y*t.height-t.top)/(t.bottom-t.top);switch(t.rotation){case 0:break;case 90:{auto a=x;x=1-y;y=a;break;}case 180:x=1-x;y=1-y;break;case 270:{auto a=x;x=y;y=1-a;break;}default:throw std::invalid_argument("Rotation must be a quarter turn");}if(t.mirror)x=1-x;return {x,1-y};}
}
