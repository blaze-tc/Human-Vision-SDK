#pragma once
namespace hvinput {
struct InputSyncFdBackend { virtual ~InputSyncFdBackend()=default; virtual int Duplicate(int)=0; virtual bool Import(int)=0; virtual int Export()=0; virtual void Close(int)=0; };
class InputGpuSync {
 InputSyncFdBackend& backend_;
public:
 explicit InputGpuSync(InputSyncFdBackend& b):backend_(b){}
 // Original acquire fd stays owned by AImage lease on every failure. Vulkan consumes only successful duplicate import.
 bool WaitAcquireFdAndOwn(int acquire_fd,bool& needs_wait);
 bool SignalReleaseFdAndReturnOwnership(int& release_fd);
};
}
