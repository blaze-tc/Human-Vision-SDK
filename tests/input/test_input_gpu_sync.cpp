#include "input_gpu_sync.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
struct Backend:hvinput::InputSyncFdBackend{int duplicates=0,imports=0,closes=0,exports=0;bool dup_fail=false,import_fail=false,export_fail=false;int Duplicate(int fd)override{++duplicates;return dup_fail?-1:fd+100;}bool Import(int fd)override{++imports;return !import_fail;}int Export()override{++exports;return export_fail?-2:42;}void Close(int fd)override{if(fd<100)std::abort();++closes;}};
int main(){Backend b;hvinput::InputGpuSync sync(b);bool wait=true;CHECK(sync.WaitAcquireFdAndOwn(-1,wait)&&!wait&&b.duplicates==0);CHECK(sync.WaitAcquireFdAndOwn(7,wait)&&wait&&b.duplicates==1&&b.imports==1&&b.closes==0);b.import_fail=true;CHECK(!sync.WaitAcquireFdAndOwn(7,wait)&&b.closes==1);b.dup_fail=true;CHECK(!sync.WaitAcquireFdAndOwn(7,wait)&&b.imports==2&&b.closes==1);int fd=-1;CHECK(sync.SignalReleaseFdAndReturnOwnership(fd)&&fd==42);b.export_fail=true;CHECK(!sync.SignalReleaseFdAndReturnOwnership(fd)&&fd==-1);std::puts("PASS positive/negative import/export fd ownership");}
