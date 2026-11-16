#include "input_frame_ring.h"
#include <cstdio>
#include <cstring>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)

int ConversionKeepsCadenceBetweenUnityUpdates() {
 // Units of one third millisecond give exact 25 Hz input and 30 Hz Unity
 // clocks. Render callbacks run 2/19 ms after Update; the synthetic GPU
 // signals after 37 ms. These timings exercise the interleaving, not a
 // prediction of the target board's conversion throughput.
 constexpr int duration=18000, inputPeriod=120, unityPeriod=100, completionDelay=111;
 hvinput::InputFrameRing ring;
 ring.Begin(1);
 int active=-1, current=-1, releasing=-1, completesAt=0, completedCount=0;
 uint64_t decoded=0, pending=0, submitted=0, completed=0, currentSequence=0, releaseSequence=0;
 unsigned replacements=0;
 for(int t=0;t<duration;++t) {
  if(t%inputPeriod==0) {
   if(pending)++replacements;
   pending=++decoded;
  }
  const bool unityUpdate=t%unityPeriod==0;
  const bool render=t%unityPeriod==6||t%unityPeriod==57;
  if(!unityUpdate&&!render)continue;
  if(active>=0&&t>=completesAt) {
   CHECK(ring.Complete(active,submitted));
   completed=submitted;
   active=-1;
   ++completedCount;
   ring.Collect(completed);
  }
  if(unityUpdate) {
   if(releasing>=0) {
    CHECK(ring.Slot(releasing).sequence==releaseSequence);
    ring.Release(releasing,releaseSequence);
    ring.Collect(completed);
    releasing=-1;
   }
   const int latest=ring.Published();
   if(latest>=0&&ring.Slot(latest).sequence!=currentSequence) {
    CHECK(ring.Slot(latest).sequence>currentSequence);
    releasing=current;
    releaseSequence=currentSequence;
    current=latest;
    currentSequence=ring.Slot(latest).sequence;
    ring.Observe(current,currentSequence);
   }
  }
  if(render&&active<0&&pending&&ring.CanStartConversion()) {
   active=ring.Acquire(1,pending);
   CHECK(active>=0&&active!=current&&active!=releasing);
   submitted=pending;
   pending=0;
   completesAt=t+completionDelay;
   CHECK(ring.Queue(active,submitted));
  }
 }
 std::printf("complete=%d decoded=%llu replaced=%u\n",completedCount,static_cast<unsigned long long>(decoded),replacements);
 CHECK(decoded==150);
 CHECK(completedCount>=119);
 CHECK(replacements<=30);
 return 0;
}

int main(int argc,char** argv){using namespace hvinput;const char* name=argc>1?argv[1]:"";InputFrameRing ring;ring.Begin(1);
 if(!std::strcmp(name,"NoFreeSlotDropsWithoutRenderThreadWait")){for(int i=0;i<3;++i){CHECK(ring.Acquire(1,i+1)==i);CHECK(ring.Queue(i,i+1));}CHECK(ring.Acquire(1,4)==-1);CHECK(ring.Drops()==1);CHECK(ring.Live()==3);}
 else if(!std::strcmp(name,"SupersededUnobservedPublicationDoesNotLeak")){int a=ring.Acquire(1,1);CHECK(ring.Queue(a,1));CHECK(ring.Complete(a,1));int b=ring.Acquire(1,2);CHECK(ring.Queue(b,2));CHECK(ring.Complete(b,2));ring.Collect(2);CHECK(ring.Live()==1);ring.Observe(b,2);int c=ring.Acquire(1,3);CHECK(ring.Queue(c,3));CHECK(ring.Complete(c,3));ring.Collect(3);CHECK(ring.Live()==2);ring.Observe(c,99);int d=ring.Acquire(1,4);CHECK(ring.Queue(d,4));CHECK(ring.Complete(d,4));ring.Collect(4);CHECK(ring.Live()==2);}
 else if(!std::strcmp(name,"ConsumerCopyKeepsPublishedSlotBusy")){for(int i=0;i<3;++i){CHECK(ring.Acquire(1,i+1)==i);CHECK(ring.Queue(i,i+1));CHECK(ring.Complete(i,i+1));ring.Observe(i,i+1);}CHECK(ring.Acquire(1,4)==-1);ring.Release(0,99);ring.Collect(3);CHECK(ring.Acquire(1,4)==-1);ring.Release(0,1);ring.Collect(3);CHECK(ring.Acquire(1,4)==0);}
 else if(!std::strcmp(name,"ConversionKeepsCadenceBetweenUnityUpdates")){CHECK(ConversionKeepsCadenceBetweenUnityUpdates()==0);}
 else if(!std::strcmp(name,"ConversionNeedsFreeSlotAndProtectsObservedCopies")){
  for(int i=0;i<3;++i){
   CHECK(ring.CanStartConversion());
   const int slot=ring.Acquire(1,i+1);
   CHECK(slot==i);CHECK(ring.Queue(slot,i+1));CHECK(ring.Complete(slot,i+1));
   CHECK(ring.CanStartConversion()==(i<2));
   ring.Observe(slot,99);CHECK(!ring.Slot(slot).observed);
   ring.Observe(slot,i+1);
  }
  CHECK(!ring.CanStartConversion());CHECK(ring.Drops()==0);
  ring.Release(0,1);ring.Collect(3);CHECK(ring.CanStartConversion());
  const int pending=ring.Acquire(1,4);CHECK(ring.Queue(pending,4));CHECK(ring.Complete(pending,4));
  CHECK(!ring.CanStartConversion());
  ring.Begin(2);ring.Collect(4);CHECK(ring.CanStartConversion());CHECK(ring.Drops()==0);
 }
 else if(!std::strcmp(name,"SupersededPublicationWaitsForNewerFence")){
  const int first=ring.Acquire(1,1);
  CHECK(ring.Queue(first,4));CHECK(ring.Complete(first,4));CHECK(!ring.Slot(first).observed);
  CHECK(ring.CanStartConversion());
  const int newer=ring.Acquire(1,2);
  CHECK(newer!=first);CHECK(ring.Queue(newer,6));
  CHECK(!ring.Complete(newer,5));ring.Collect(5);
  CHECK(ring.Published()==first);CHECK(ring.Slot(first).state==InputSlotState::Published);CHECK(ring.Drops()==0);
  CHECK(ring.Complete(newer,6));ring.Collect(6);
  CHECK(ring.Published()==newer);CHECK(ring.Slot(first).state==InputSlotState::Free);CHECK(ring.Drops()==1);
  CHECK(ring.SupersededUnobserved()==1);
 }
 else if(!std::strcmp(name,"CloseBeforeCopyCompletesKeepsBufferAlive")){int s=ring.Acquire(1,1);CHECK(ring.Queue(s,8));ring.Close();ring.Collect(7);CHECK(ring.Live()==1);CHECK(ring.Slot(s).state==InputSlotState::Retiring);CHECK(!ring.Complete(s,7));ring.Collect(8);CHECK(ring.Live()==0);CHECK(ring.Published()==-1);}
 else if(!std::strcmp(name,"ReconnectionRejectsOldGeneration")){int s=ring.Acquire(1,1);CHECK(ring.Queue(s,8));ring.Begin(2);CHECK(!ring.Complete(s,8));CHECK(ring.Published()==-1);CHECK(ring.Acquire(1,2)==-1);ring.Collect(8);int n=ring.Acquire(2,3);CHECK(n>=0);CHECK(ring.Queue(n,9));CHECK(!ring.Complete(n,8));CHECK(ring.Complete(n,9));CHECK(ring.Published()==n);}
 else if(!std::strcmp(name,"EncodedBacklogFlushWaitsForKeyframe")){InputEncodedBacklog backlog;backlog.Reset(100);CHECK(!backlog.Admit(false));CHECK(!backlog.Expired(99));CHECK(backlog.Expired(100));CHECK(backlog.Admit(true));CHECK(backlog.Admit(false));backlog.Reset(200);CHECK(!backlog.Admit(false));CHECK(backlog.Waiting());CHECK(backlog.Admit(true));}
 else return 2;std::puts("PASS");return 0;}
