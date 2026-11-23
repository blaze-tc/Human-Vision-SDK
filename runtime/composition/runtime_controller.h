#pragma once
#include "composition/source_binding.h"
#include "composition/session.h"
#include <array>
#include <deque>
#include <functional>

namespace humanvision::runtime {
struct OwnedRuntimeConfig {
    std::string root,profile;
    int capacity=1;
    uint32_t policy=0,retirement_timeout_ms=5000;
};
bool CopyRuntimeConfig(const HV_RuntimeControllerConfigV1&,OwnedRuntimeConfig&,std::string&);
class RuntimeController {
public:
    // Composition dependency seam. The public C API always uses the built-in factory.
    using SessionFactory=std::function<std::unique_ptr<RuntimeSession>(const OwnedRuntimeConfig&,GpuConsumerSource*,std::shared_ptr<ObservationJournal>,std::string&)>;
    enum class Operation {Start,Source,Pause,Resume,Stop,Reconfigure,Close};
    static std::unique_ptr<RuntimeController> Create(const HV_RuntimeControllerConfigV1&,std::string&,SessionFactory={});
    ~RuntimeController(); // only after IsRetired; public TryRelease enforces this
    int Request(Operation,uint64_t*,const OwnedRuntimeConfig* config=nullptr,const OwnedSourceConfig* source=nullptr);
    bool IsRetired() const noexcept {return retired_.load(std::memory_order_acquire);}
    int Copy(int64_t,HV_CanonicalBodyV1*,uint32_t,uint32_t*,HV_RuntimeStatsV1*);
    int Submit(const HV_VideoFrame*);
    int SetRegions(const HV_Rect*,uint32_t,int64_t);
    int RequestStatus(uint64_t,HV_RuntimeRequestStatusV1*);
    int Capabilities(HV_RuntimeCapabilitiesV1*);
    int Stats(HV_RuntimeStatsV3*);
    int ReadObservations(uint64_t,HV_RuntimeObservationV1*,uint32_t,HV_RuntimeObservationReadV1*);
    int Diagnostics(char*,uint32_t);
    int CopyPreview(uint64_t,void*,uint32_t,HV_RuntimePreviewV1*);
    int RequestSnapshot(uint64_t*);
    int CopySnapshot(uint64_t,void*,uint32_t,HV_RuntimePreviewV1*);
    int CancelSnapshot(uint64_t);
private:
    struct Command {Operation operation;uint64_t id;OwnedRuntimeConfig config;OwnedSourceConfig source;};
    explicit RuntimeController(OwnedRuntimeConfig,SessionFactory);
    void Run();
    bool Start(std::string&);
    void Retire();
    uint32_t VisibleState() const; // mutex_ held
    void FillCapabilities(HV_RuntimeCapabilitiesV1&) const; // mutex_ held
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    OwnedRuntimeConfig config_;
    OwnedSourceConfig source_config_;
    std::shared_ptr<ObservationJournal> journal_=std::make_shared<ObservationJournal>();
    SessionFactory factory_;
    std::unique_ptr<RuntimeSession> session_;
    std::unique_ptr<NativeSourceBinding> source_;
    std::thread control_;
    std::deque<Command> commands_;
    std::array<HV_RuntimeRequestStatusV1,64> requests_{};
    std::array<int64_t,64> request_deadlines_{};
    std::atomic<bool> retired_{false};
    bool close_requested_=false,admission_=false;
    uint32_t state_=HV_CONTROL_CREATED;
    uint64_t next_request_=0,close_request_=0,epoch_=0,runtime_errors_=0,journal_overruns_=0;
    int64_t operation_started_us_=0,retirement_requested_us_=0;
    std::string error_,fallback_;
};
}
