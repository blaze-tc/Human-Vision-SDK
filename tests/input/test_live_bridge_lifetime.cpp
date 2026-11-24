#include "live_bridge_state.h"
#include "live_device_lifetime.h"
#include <iostream>
#define CHECK(x) do { if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;} }while(false)
using hvinput::LiveBridgeState;
int main(){
 hvinput::LiveDeviceLifetime device;
 CHECK(!device.Acquire().Epoch());
 device.Activate();
 const auto first_epoch=device.Acquire().Epoch();CHECK(first_epoch);
 CHECK(device.Acquire().Matches(first_epoch));
 device.Invalidate();CHECK(!device.Acquire().Matches(first_epoch));
 device.Activate();CHECK(device.Acquire().Epoch()!=first_epoch);
 CHECK(!device.Acquire().Matches(first_epoch));

 LiveBridgeState s;CHECK(s.Bind(9));CHECK(s.QueueEvent());CHECK(!s.QueueEvent());
 CHECK(s.Begin(2,1)>=0);CHECK(!s.Finish(false));CHECK(s.Writing());
 CHECK(s.Finish(true));int held=s.Acquire(0);CHECK(held>=0);
 for(unsigned n=2;n<=61;++n){CHECK(s.Begin(2,n)>=0);CHECK(s.Finish(true));}
 int latest=s.Acquire(1);CHECK(latest>=0);CHECK(s.Sequence(latest)==61);
 CHECK(s.Release(held,2,1));CHECK(s.Release(latest,2,61));
 CHECK(s.Begin(2,62)>=0);s.AdmitGeneration(3);CHECK(!s.Finish(true));CHECK(s.Acquire(0)<0);
 CHECK(s.Begin(3,63)>=0);CHECK(s.Finish(true));held=s.Acquire(0);CHECK(held>=0);
 s.Close();CHECK(!s.Retired());CHECK(s.Begin(3,64)<0);CHECK(s.Release(held,3,63));
 CHECK(!s.Retired());s.EventFinished();CHECK(s.Retired());
 LiveBridgeState full;CHECK(full.Bind(10));int pins[3];
 for(unsigned n=1;n<=3;++n){CHECK(full.Begin(1,n)>=0);CHECK(full.Finish(true));pins[n-1]=full.Acquire(n-1);CHECK(pins[n-1]>=0);}
 CHECK(full.Begin(1,4)<0);CHECK(full.Release(pins[0],1,1));CHECK(full.Begin(1,4)>=0);
 full.RetireTargets();CHECK(!full.TargetsRetired());CHECK(!full.Finish(true));
 CHECK(full.Release(pins[1],1,2));CHECK(full.Release(pins[2],1,3));CHECK(full.TargetsRetired());
 LiveBridgeState stopped;CHECK(stopped.Bind(11));CHECK(stopped.Begin(1,1)>=0);
 stopped.Close();CHECK(!stopped.Finish(true));CHECK(stopped.Acquire(0)<0);CHECK(stopped.Retired());
 LiveBridgeState deferred;CHECK(deferred.Bind(12));CHECK(deferred.QueueEvent());CHECK(deferred.Begin(1,1)>=0);
 deferred.QueueSubmission();deferred.EventFinished();deferred.Close();
 CHECK(!deferred.Retired());CHECK(!deferred.Finish(true));CHECK(!deferred.Retired());
 deferred.SubmissionFinished();CHECK(deferred.Retired());
 std::cout<<"live target latest/fence/lease/generation/close contracts passed\n";
}
