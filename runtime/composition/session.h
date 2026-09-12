#pragma once
#include "host/runtime_host.h"
#include "host/profile_manager.h"
#include "host/backend_factory.h"
#include "services/body_services.h"

namespace humanvision::runtime {
// The composition root registers built-ins; the generic Host never names them.
class RuntimeSession {
public:
 ~RuntimeSession();
 bool Start(const std::filesystem::path& root,const std::string& profile,int capacity,std::string& error);
 bool Submit(const HV_VideoFrame&,std::string& error);
 bool SetRegions(const HV_Rect*,uint32_t count,int64_t revision,std::string& error);
 BodySnapshot Copy(int64_t sample_time,HV_RuntimeStatsV1& stats);
 std::string LastError() const;
 std::string Diagnostics() const;
private:
 void Run();
 void Poll(); // mutex_ held
 PluginRegistry registry_;
 std::shared_ptr<const RuntimeProfile> profile_;
 std::unique_ptr<BackendFactory> factory_;
 RuntimeHost body_,hand_;
 LatestFrameSlot input_;
 std::thread worker_;
 mutable std::mutex mutex_;
 BodyServices services_;
 std::array<HV_Rect,8> regions_{};
 uint32_t region_count_=0;
 int64_t revision_=0,body_sequence_=0,hand_sequence_=0,last_body_submit_=0;
 HV_RuntimeStatsV1 stats_{};
 int64_t last_hand_result_time_=0;
 std::string error_;
};
}
