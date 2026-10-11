using System;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>Bounded, allocation-free observation counting. Track ID is independent
    /// of display slot. Repeated results and predicted copies never create observations.</summary>
    public sealed class FreshSkeletonCounter
    {
        private struct Entry {
            internal long id, sequence, frame, observed, count, complete;
            internal double ageSum, ageMax;
        }
        private readonly Entry[] entries=new Entry[64];
        public int Evictions { get; private set; }
        public void Reset() { Array.Clear(entries,0,entries.Length);Evictions=0; }
        public bool Observe(long id,long sequence,long frame,long observed,bool complete,double ageMs)
        {
            if(id<=0 || sequence<0 || frame<0 || observed<=0)return false;
            int slot=-1,oldest=0;
            for(int i=0;i<entries.Length;i++) {
                if(entries[i].id==id) {slot=i;break;}
                if(entries[i].id==0 && slot<0)slot=i;
                if(entries[i].observed<entries[oldest].observed)oldest=i;
            }
            if(slot<0){slot=oldest;entries[slot]=default;++Evictions;}
            ref var value=ref entries[slot];
            if(value.id!=0 && (sequence<=value.sequence || frame<=value.frame || observed<=value.observed))return false;
            value.id=id;value.sequence=sequence;value.frame=frame;value.observed=observed;
            ++value.count;if(complete)++value.complete;
            if(!double.IsNaN(ageMs)&&!double.IsInfinity(ageMs)&&ageMs>=0){value.ageSum+=ageMs;value.ageMax=Math.Max(value.ageMax,ageMs);}
            return true;
        }
        [Serializable] public sealed class Sample {
            public long stableTrackId, freshObservations, completeCoco17, lastSourceFrame, lastObservedUs;
            public double seconds, freshFps, completeCoco17Fps, localResultAgeMeanMs, localResultAgeMaximumMs;
        }
        /// <summary>Called once per reporting window. Known absent tracks report zero.
        /// Reset on source generation or benchmark phase; age is local pipeline age.</summary>
        public void Drain(double seconds,Action<Sample> emit)
        {
            if(seconds<=0)return;
            for(int i=0;i<entries.Length;i++) {
                ref var value=ref entries[i];if(value.id==0)continue;
                emit(new Sample {stableTrackId=value.id,freshObservations=value.count,completeCoco17=value.complete,
                    lastSourceFrame=value.frame,lastObservedUs=value.observed,seconds=seconds,
                    freshFps=value.count/seconds,completeCoco17Fps=value.complete/seconds,
                    localResultAgeMeanMs=value.count>0?value.ageSum/value.count:-1,
                    localResultAgeMaximumMs=value.count>0?value.ageMax:-1});
                value.count=value.complete=0;value.ageSum=value.ageMax=0;
            }
        }
    }
}
