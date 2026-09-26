#include "composition/region_assignment.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace humanvision::runtime {
namespace {
bool ValidPoint(const HV_CanonicalJointV1& p) {
    return p.valid&&std::isfinite(p.x_px)&&std::isfinite(p.y_px);
}
bool Contains(const HV_Rect& r,float x,float y) {
    return x>=r.x&&y>=r.y&&x<=r.x+r.width&&y<=r.y+r.height;
}
float Overlap(HV_Rect a,HV_Rect b) {
    const float w=std::max(0.f,std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x));
    const float h=std::max(0.f,std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));
    const float intersection=w*h;
    return intersection/std::max(1.f,a.width*a.height+b.width*b.height-intersection);
}
float PersonCoverage(HV_Rect box,HV_Rect region,int width,int height) {
    const HV_Rect px{region.x*width,region.y*height,region.width*width,region.height*height};
    const float w=std::max(0.f,std::min(box.x+box.width,px.x+px.width)-std::max(box.x,px.x));
    const float h=std::max(0.f,std::min(box.y+box.height,px.y+px.height)-std::max(box.y,px.y));
    return w*h/std::max(1.f,box.width*box.height);
}
struct Candidate {
    uint32_t source=0;
    uint32_t eligible=0;
    int anchored_region=-1;
    float pose=0,detector=0;
};
}
AssignedObservation AssignRegions(const HV_ObservationFrameV1& input,const RegionSet& regions,
                                  int64_t revision,const TrackAnchors& anchors,
                                  const float* detector_scores,const int32_t* crop_track_ids) {
    AssignedObservation out{};out.frame=input;out.frame.body_count=0;
    out.region_indices.fill(-1);out.region_revision=revision;
    if(input.width<=0||input.height<=0||input.body_count>HV_MAX_PEOPLE||
       regions.count>HV_MAX_PEOPLE||anchors.count>HV_MAX_PEOPLE)return out;
    if(!regions.count){
        out.frame=input;out.body_count=input.body_count;
        for(uint32_t i=0;i<input.body_count;++i)
            out.crop_track_ids[i]=crop_track_ids?crop_track_ids[i]:0;
        return out;
    }
    std::array<Candidate,HV_MAX_PEOPLE> candidates{};uint32_t count=0;
    for(uint32_t i=0;i<input.body_count;++i){
        const auto& b=input.bodies[i];const auto& box=b.bbox_px;
        if(!std::isfinite(box.x)||!std::isfinite(box.y)||!std::isfinite(box.width)||
           !std::isfinite(box.height)||box.width<=0||box.height<=0)continue;
        Candidate c{};c.source=i;c.pose=std::isfinite(b.confidence)?b.confidence:0;
        // The pose plugin may provide a detector score through the guarded V3 sidecar.
        c.detector=detector_scores&&std::isfinite(detector_scores[i])?detector_scores[i]:0;
        if(regions.count){
            const auto& pelvis=b.joints[HV_CANONICAL_PELVIS];
            const auto& left=b.joints[HV_CANONICAL_HIP_LEFT];
            const auto& right=b.joints[HV_CANONICAL_HIP_RIGHT];
            float x=box.x+box.width*.5f,y=box.y+box.height*.5f;
            if(ValidPoint(pelvis)){x=pelvis.x_px;y=pelvis.y_px;}
            else if(ValidPoint(left)&&ValidPoint(right)){
                x=(left.x_px+right.x_px)*.5f;y=(left.y_px+right.y_px)*.5f;
            }
            x/=input.width;y/=input.height;
            for(uint32_t r=0;r<regions.count;++r)if(Contains(regions.rects[r],x,y))c.eligible|=1u<<r;
            if(!c.eligible)continue;
        }
        candidates[count++]=c;
    }
    // Maximum-cardinality, then maximum-overlap one-to-one anchor matching.
    // Eight candidates bound the bitmask DP and avoid hot-path allocations.
    constexpr float invalid=-std::numeric_limits<float>::infinity();
    float score[9][256];int previous[9][256],selected[9][256];
    for(auto& row:score)std::fill_n(row,256,invalid);
    score[0][0]=0;
    for(uint32_t a=0;a<anchors.count;++a){const auto& anchor=anchors.items[a];
        for(int mask=0;mask<(1<<count);++mask){
            if(!std::isfinite(score[a][mask]))continue;
            if(score[a][mask]>score[a+1][mask]){
                score[a+1][mask]=score[a][mask];previous[a+1][mask]=mask;
                selected[a+1][mask]=-1;
            }
            if(anchor.region_index<0||anchor.region_index>=int(regions.count)||
               (anchor.observation_timestamp_us>0&&
                (input.source_timestamp_us<anchor.observation_timestamp_us||
                 input.source_timestamp_us-anchor.observation_timestamp_us>800000)))continue;
            for(uint32_t i=0;i<count;++i){const auto& c=candidates[i];
                if((mask&(1<<i))||!(c.eligible&(1u<<anchor.region_index)))continue;
                const bool same_crop=crop_track_ids&&anchor.crop_track_id>0&&
                    crop_track_ids[c.source]==anchor.crop_track_id;
                const float overlap=same_crop?2.f:
                    Overlap(input.bodies[c.source].bbox_px,anchor.bbox_px);
                if(overlap<.2f)continue;
                const int next=mask|(1<<i);const float value=score[a][mask]+100.f+overlap;
                if(value>score[a+1][next]){
                    score[a+1][next]=value;previous[a+1][next]=mask;
                    selected[a+1][next]=int(i);
                }
            }
        }
    }
    int best_mask=0;for(int mask=1;mask<(1<<count);++mask)
        if(score[anchors.count][mask]>score[anchors.count][best_mask])best_mask=mask;
    for(int a=int(anchors.count);a>0;--a){
        const int choice=selected[a][best_mask];
        if(choice>=0)candidates[choice].anchored_region=anchors.items[a-1].region_index;
        best_mask=previous[a][best_mask];
    }
    std::sort(candidates.begin(),candidates.begin()+count,[](const Candidate& a,const Candidate& b){
        if((a.anchored_region>=0)!=(b.anchored_region>=0))return a.anchored_region>=0;
        if(a.pose!=b.pose)return a.pose>b.pose;
        if(a.detector!=b.detector)return a.detector>b.detector;
        return a.source<b.source;
    });
    uint32_t used=0;
    for(uint32_t i=0;i<count;++i){const auto& c=candidates[i];int region=-1;
        if(regions.count){
            if(c.anchored_region>=0&&!(used&(1u<<c.anchored_region)))region=c.anchored_region;
            if(region<0){float best=-1.f;
                for(uint32_t r=0;r<regions.count;++r)if((c.eligible&(1u<<r))&&!(used&(1u<<r))){
                    const float coverage=PersonCoverage(input.bodies[c.source].bbox_px,
                                                        regions.rects[r],input.width,input.height);
                    if(coverage>best){best=coverage;region=int(r);}
                }
            }
            if(region<0)continue;
            used|=1u<<region;
        }
        out.frame.bodies[out.body_count]=input.bodies[c.source];
        out.region_indices[out.body_count]=region;++out.body_count;
        out.crop_track_ids[out.body_count-1]=crop_track_ids?crop_track_ids[c.source]:0;
    }
    out.frame.body_count=out.body_count;
    return out;
}
bool CanPublish(const AssignedObservation& observation,int64_t current_revision) noexcept {
    return observation.region_revision==current_revision;
}
}
