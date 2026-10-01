#include "plugins/pipeline/yolo/yolo_decoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace humanvision::runtime::yolo {
namespace {
double Sigmoid(double value) {return 1./(1.+std::exp(-std::clamp(value,-80.,80.)));}
bool Tensor(const HV_TensorViewV1& v,const char* name,int rows,int cols) {
    if(v.struct_size<sizeof(v)||v.api_version!=HV_PLUGIN_API_V1||!v.name||
       std::strcmp(v.name,name)||!v.data||v.element_type!=1||
       v.byte_count!=uint64_t(rows)*cols*sizeof(float))return false;
    const bool shape=(v.rank==2&&v.dimensions[0]==rows&&v.dimensions[1]==cols)||
        (v.rank==3&&v.dimensions[0]==1&&v.dimensions[1]==rows&&v.dimensions[2]==cols);
    if(!shape)return false;
    for(uint32_t i=v.rank;i<8;++i)if(v.dimensions[i]!=0)return false;
    return true;
}
double IoU(const double* a,const double* b) {
    const auto area=[](const double* v){return (v[2]-v[0])*(v[3]-v[1]);};
    const double inter=std::max(0.,std::min(a[2],b[2])-std::max(a[0],b[0]))*
        std::max(0.,std::min(a[3],b[3])-std::max(a[1],b[1]));
    const double total=area(a)+area(b)-inter;return total>0?inter/total:0.;
}
}
int AnchorCount(int target) noexcept {
    if(target!=320&&target!=416)return 0;
    return (target/8)*(target/8)+(target/16)*(target/16)+(target/32)*(target/32);
}
int AnchorCount(int width,int height) noexcept {
    if(width==height)return AnchorCount(width);
    if(!((width==640&&height==384)||(width==512&&height==288)))return 0;
    return (width/8)*(height/8)+(width/16)*(height/16)+(width/32)*(height/32);
}
bool BuildGeometry(int sw,int sh,int width,int height,Geometry& out) noexcept {
    if(width==height)return BuildGeometry(sw,sh,width,out);
    out={};
    // Eligible rectangular routes require the pinned 16:9 landscape source.
    // Do not silently stretch/crop or admit an unreviewed source aspect ratio.
    if(sw<=0||sh<=0||!AnchorCount(width,height)||int64_t(sw)*9!=int64_t(sh)*16)return false;
    const double scale=double(width)/sw;
    // Integer aspect proof gives exact resized height, avoiding float truncation.
    const int rh=width*9/16;
    out={width,height,0,(height-rh)/2,sw,sh,float(scale)};return true;
}
bool BuildGeometry(int sw,int sh,int target,Geometry& out) noexcept {
    out={};if(sw<=0||sh<=0||!AnchorCount(target))return false;
    // Pinned upstream resize truncates the shorter extent; square evaluation
    // uses centered integer padding. Decode uses the nominal aspect scale.
    const double scale=double(target)/std::max(sw,sh);
    const int rw=sw>sh?target:int(sw*scale),rh=sw>sh?int(sh*scale):target;
    if(rw<1||rh<1)return false;
    out={target,target,(target-rw)/2,(target-rh)/2,sw,sh,float(scale)};return true;
}
int CanonicalIndex(int i) noexcept {
    constexpr int mapping[]={HV_CANONICAL_NOSE,HV_CANONICAL_EYE_LEFT,HV_CANONICAL_EYE_RIGHT,
      HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT,HV_CANONICAL_SHOULDER_LEFT,HV_CANONICAL_SHOULDER_RIGHT,
      HV_CANONICAL_ELBOW_LEFT,HV_CANONICAL_ELBOW_RIGHT,HV_CANONICAL_WRIST_LEFT,HV_CANONICAL_WRIST_RIGHT,
      HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT,HV_CANONICAL_KNEE_LEFT,HV_CANONICAL_KNEE_RIGHT,
      HV_CANONICAL_ANKLE_LEFT,HV_CANONICAL_ANKLE_RIGHT};
    return i>=0&&i<17?mapping[i]:-1;
}
Decoder::Decoder(int target,int height):target_(target),height_(height?height:target) {
    proposals_.reserve(AnchorCount(target_,height_));selected_.reserve(AnchorCount(target_,height_));
}
bool Decoder::Decode(const HV_TensorViewV1* views,uint32_t view_count,const Geometry& g,
    int64_t timestamp,HV_BodyObservationV1* bodies,float* scores,uint32_t capacity,uint32_t& count) {
    count=0;proposals_.clear();selected_.clear();Geometry expected{};
    if(!views||view_count!=2||!bodies||!scores||capacity<1||capacity>8||
       !BuildGeometry(g.source_width,g.source_height,target_,height_,expected)||
       g.width!=expected.width||g.height!=expected.height||g.left!=expected.left||g.top!=expected.top||
       g.scale!=expected.scale)return false;
    const int rows=AnchorCount(target_,height_);const float *det=nullptr,*kp=nullptr;
    for(uint32_t i=0;i<view_count;++i){
        if(Tensor(views[i],"out0",rows,65)){if(det)return false;det=static_cast<const float*>(views[i].data);}
        else if(Tensor(views[i],"out1",rows,51)){if(kp)return false;kp=static_cast<const float*>(views[i].data);}
        else return false;
    }
    if(!det||!kp||!std::all_of(det,det+rows*65,[](float v){return std::isfinite(v);})||
       !std::all_of(kp,kp+rows*51,[](float v){return std::isfinite(v);}))return false;
    int offset=0;
    for(int stride:{8,16,32}) {
        const int grid=target_/stride,grid_height=height_/stride;
        for(int k=0;k<grid*grid_height;++k){
            const int anchor=offset+k;const float* row=det+anchor*65;
            const float score=float(Sigmoid(row[64]));if(score<.25f)continue;
            Proposal p{};p.score=score;p.anchor=anchor;p.x=k%grid;p.y=k/grid;p.stride=stride;
            double distances[4]{};
            for(int side=0;side<4;++side){
                const float* logits=row+side*16;const double maximum=*std::max_element(logits,logits+16);
                double sum=0,weighted=0;
                for(int bin=0;bin<16;++bin){const double weight=std::exp(logits[bin]-maximum);sum+=weight;weighted+=weight*bin;}
                distances[side]=weighted/sum*stride;
            }
            const double cx=(p.x+.5)*stride,cy=(p.y+.5)*stride;
            p.box[0]=cx-distances[0];p.box[1]=cy-distances[1];p.box[2]=cx+distances[2];p.box[3]=cy+distances[3];
            proposals_.push_back(p);
        }
        offset+=grid*grid_height;
    }
    std::sort(proposals_.begin(),proposals_.end(),[](const auto& a,const auto& b){
        return a.score!=b.score?a.score>b.score:a.anchor<b.anchor;});
    for(auto p:proposals_){
        bool keep=true;for(const auto& q:selected_)if(IoU(p.box,q.box)>.45){keep=false;break;}
        if(keep)selected_.push_back(p);
    }
    // Reference applies source clipping after NMS, then orders by source area.
    for(auto& p:selected_)for(int i=0;i<4;++i)p.box[i]=std::clamp(
        (p.box[i]-(i%2?g.top:g.left))/double(g.scale),0.,double((i%2?g.source_height:g.source_width)-1));
    std::sort(selected_.begin(),selected_.end(),[](const auto& a,const auto& b){
        const double aa=(a.box[2]-a.box[0])*(a.box[3]-a.box[1]),bb=(b.box[2]-b.box[0])*(b.box[3]-b.box[1]);
        if(aa!=bb)return aa>bb;return a.score!=b.score?a.score>b.score:a.anchor<b.anchor;});
    for(const auto& p:selected_){
        if(count==capacity)break;if(p.box[2]<=p.box[0]||p.box[3]<=p.box[1])continue;
        auto& body=bodies[count];body={};body.struct_size=sizeof(body);body.api_version=HV_PLUGIN_API_V1;
        body.bbox_px={float(p.box[0]),float(p.box[1]),float(p.box[2]-p.box[0]),float(p.box[3]-p.box[1])};
        body.confidence=p.score;scores[count]=p.score;
        for(auto& joint:body.joints){joint.struct_size=sizeof(joint);joint.api_version=HV_API_VERSION_040;}
        const float* points=kp+p.anchor*51;
        for(int j=0;j<17;++j){
            auto& joint=body.joints[CanonicalIndex(j)];
            joint.x_px=float(((p.x+double(points[j*3])*2)*p.stride-g.left)/g.scale);
            joint.y_px=float(((p.y+double(points[j*3+1])*2)*p.stride-g.top)/g.scale);
            if(!std::isfinite(joint.x_px)||!std::isfinite(joint.y_px)){count=0;return false;}
            joint.x_norm=joint.x_px/g.source_width;joint.y_norm=joint.y_px/g.source_height;
            joint.confidence=float(Sigmoid(points[j*3+2]));joint.observation_timestamp_us=timestamp;
            joint.valid=joint.confidence>=.2f&&joint.x_px>=0&&joint.y_px>=0&&joint.x_px<g.source_width&&joint.y_px<g.source_height;
        }
        ++count;
    }
    return true;
}
}
