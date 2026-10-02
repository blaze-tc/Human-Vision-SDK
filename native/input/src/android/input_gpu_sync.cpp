#include "input_gpu_sync.h"
namespace hvinput {
bool InputGpuSync::WaitAcquireFdAndOwn(int fd,bool& wait){wait=false;if(fd<0)return true;int duplicate=backend_.Duplicate(fd);if(duplicate<0)return false;if(!backend_.Import(duplicate)){backend_.Close(duplicate);return false;}wait=true;return true;}
bool InputGpuSync::SignalReleaseFdAndReturnOwnership(int& fd){fd=-1;int exported=backend_.Export();if(exported < -1)return false;fd=exported;return true;}
}
