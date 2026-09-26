#include "core/stats_collector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace humanvision {

StatsCollector::StatsCollector() : started_at_(std::chrono::steady_clock::now()) {
    stats_.struct_size = sizeof(HV_Stats);
}

void StatsCollector::RecordSubmitted(const bool replaced_pending) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.submitted_frames;
    if (replaced_pending) {
        ++stats_.dropped_frames;
    }
}

void StatsCollector::RecordProcessed(const StageTimings& timings) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.processed_frames;
    stats_.detection_ms = timings.detection_ms;
    stats_.pose_ms = timings.pose_ms;
    stats_.tracking_ms = timings.tracking_ms;
    stats_.total_ms = timings.total_ms;
}

void StatsCollector::RecordTimings(const StageTimings& timings) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.detection_ms = timings.detection_ms;
    stats_.pose_ms = timings.pose_ms;
    stats_.tracking_ms = timings.tracking_ms;
    stats_.total_ms = timings.total_ms;
}

HV_Stats StatsCollector::Snapshot(
    const std::int64_t authoritative_dropped_frames) const {
    std::lock_guard<std::mutex> lock(mutex_);
    HV_Stats result = stats_;
    if (authoritative_dropped_frames >= 0) {
        result.dropped_frames = authoritative_dropped_frames;
    }
    const float elapsed_seconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - started_at_)
            .count();
    if (elapsed_seconds > 0.0F) {
        result.input_fps =
            static_cast<float>(result.submitted_frames) / elapsed_seconds;
        result.inference_fps =
            static_cast<float>(result.processed_frames) / elapsed_seconds;
    }
    return result;
}

bool StatsCollectorV2::CanPublish(const std::int64_t frame_id,const std::uint32_t body_count,
                                  const std::int64_t capture_us,const std::int64_t published_us,
                                  const std::int64_t pose_frame_id,const float pose_total_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frame_id > last_frame_ && capture_us > last_capture_ && capture_us > 0 &&
        published_us >= capture_us && published_us - capture_us <= 10'000'000 &&
        pose_frame_id == frame_id && body_count <= HV_MAX_PEOPLE &&
        std::isfinite(pose_total_ms) && pose_total_ms >= 0.0F;
}

bool StatsCollectorV2::Publish(const std::int64_t frame_id, const std::uint32_t body_count,
                              const std::int64_t capture_us, const std::int64_t published_us,
                              std::int64_t pose_frame_id, const float pose_total_ms,
                              const bool detector_keyframe, const std::uint32_t posed_people,
                              const bool clock_domain_prevalidated) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pose_frame_id < 0) pose_frame_id = frame_id;
    // Ten seconds is an explicit clock-domain sanity bound, not an acceptance
    // age threshold. The device gate checks the much tighter P95 requirement.
    if (frame_id <= last_frame_ || capture_us <= last_capture_ || capture_us <= 0 ||
        published_us < capture_us || (!clock_domain_prevalidated && published_us - capture_us > 10'000'000) ||
        pose_frame_id != frame_id || body_count > HV_MAX_PEOPLE ||
        !std::isfinite(pose_total_ms) || pose_total_ms < 0.0F) return false;
    if (!first_publication_) first_publication_ = published_us;
    last_frame_ = frame_id; last_capture_ = capture_us;
    stats_.source_frame_id = frame_id;
    stats_.capture_timestamp_us = capture_us;
    stats_.publication_timestamp_us = published_us;
    ++stats_.fresh_observation_frames;
    const float age = static_cast<float>(published_us - capture_us) / 1000.0F;
    ages_[next_] = age;
    if (detector_keyframe) {
        detector_ages_[detector_next_] = age;
        detector_next_ = (detector_next_ + 1) % kWindow;
        detector_count_ = std::min(detector_count_ + 1,kWindow);
    } else {
        pose_ages_[pose_next_] = age;
        pose_next_ = (pose_next_ + 1) % kWindow;
        pose_count_ = std::min(pose_count_ + 1,kWindow);
    }
    const auto divisor = posed_people ? posed_people : body_count;
    if(divisor){pose_body_ms_[pose_body_next_] = pose_total_ms / divisor;
        pose_body_next_=(pose_body_next_+1)%kWindow;
        pose_body_count_=std::min(pose_body_count_+1,kWindow);}
    publication_times_[next_] = published_us;
    next_ = (next_ + 1) % kWindow;
    count_ = std::min(count_ + 1, kWindow);
    return true;
}

void StatsCollectorV2::Sample(const std::int64_t frame_id, const std::int64_t sampled_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto time=sampled_us==0?std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count():sampled_us;
    if (frame_id != last_frame_ || frame_id <= 0 || time<=0 || time<=last_sample_) return;
    ++stats_.output_samples;
    if (!first_sample_) first_sample_ = time;
    last_sample_ = time;
    sample_times_[sample_next_]=time;sample_next_=(sample_next_+1)%kWindow;
    sample_count_=std::min(sample_count_+1,kWindow);
}

HV_RuntimeStatsV2 StatsCollectorV2::Snapshot(std::int64_t now_us,
                                              const std::uint32_t provenance) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if(now_us==0)now_us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto result = stats_;
    result.struct_size = sizeof(result);
    result.api_version = HV_RUNTIME_STATS_V2_VERSION;
    result.capture_provenance=provenance;
    result.sensor_capture_age_p50_ms=std::numeric_limits<float>::quiet_NaN();
    result.sensor_capture_age_p95_ms=std::numeric_limits<float>::quiet_NaN();
    if (count_) {
        auto quantile = [](const std::array<float,kWindow>& values,std::size_t size, float fraction) {
            std::array<float,kWindow> sorted{};
            std::copy_n(values.begin(),size,sorted.begin());
            std::sort(sorted.begin(),sorted.begin()+size);
            return sorted[static_cast<std::size_t>(std::ceil(fraction*size))-1];
        };
        result.age_p50_ms = quantile(ages_,count_, .5F);
        result.age_p95_ms = quantile(ages_,count_, .95F);
        if(provenance==HV_CAPTURE_PROVENANCE_SENSOR_VERIFIED){
            result.sensor_capture_age_p50_ms=result.age_p50_ms;
            result.sensor_capture_age_p95_ms=result.age_p95_ms;}
        if(pose_count_){result.pose_age_p50_ms=quantile(pose_ages_,pose_count_,.5F);
            result.pose_age_p95_ms=quantile(pose_ages_,pose_count_,.95F);}
        if(detector_count_){result.scheduled_detector_frame_age_p50_ms=quantile(detector_ages_,detector_count_,.5F);
            result.scheduled_detector_frame_age_p95_ms=quantile(detector_ages_,detector_count_,.95F);}
        if(pose_body_count_){result.pose_per_body_p50_ms=quantile(pose_body_ms_,pose_body_count_,.5F);
            result.pose_per_body_p95_ms=quantile(pose_body_ms_,pose_body_count_,.95F);}
        std::size_t current=0;std::int64_t first=0;
        for(std::size_t i=0;i<count_;++i){const auto time=publication_times_[i];
            if(time>0&&time<=now_us&&now_us-time<=10'000'000){++current;first=first?std::min(first,time):time;}}
        if(current>1&&now_us>first)result.fresh_observation_fps=
            static_cast<float>(current-1)*1'000'000.0F/static_cast<float>(now_us-first);
    }
    std::size_t samples=0;std::int64_t first_sample=0;
    for(std::size_t i=0;i<sample_count_;++i){const auto time=sample_times_[i];
        if(time>0&&time<=now_us&&now_us-time<=10'000'000){++samples;
            first_sample=first_sample?std::min(first_sample,time):time;}}
    if(samples>1&&now_us>first_sample)result.output_sampling_fps=
        static_cast<float>(samples-1)*1'000'000.0F/static_cast<float>(now_us-first_sample);
    return result;
}

}  // namespace humanvision
