#pragma once
#include "humanvision_plugin_v3.h"
#include <vector>

namespace humanvision::runtime::yolo {
struct Geometry {
    int width=0,height=0,left=0,top=0,source_width=0,source_height=0;
    float scale=0;
};
bool BuildGeometry(int source_width,int source_height,int target,Geometry& out) noexcept;
bool BuildGeometry(int source_width,int source_height,int width,int height,Geometry& out) noexcept;
int AnchorCount(int target) noexcept;
int AnchorCount(int width,int height) noexcept;
int CanonicalIndex(int coco_index) noexcept;
class Decoder {
public:
    explicit Decoder(int target,int height=0);
    bool Decode(const HV_TensorViewV1* tensors,uint32_t tensor_count,const Geometry& geometry,
                int64_t timestamp,HV_BodyObservationV1* bodies,float* scores,uint32_t capacity,
                uint32_t& count);
private:
    struct Proposal { double box[4]{};float score=0;int anchor=0,x=0,y=0,stride=0; };
    int target_,height_;
    std::vector<Proposal> proposals_,selected_;
};
}
