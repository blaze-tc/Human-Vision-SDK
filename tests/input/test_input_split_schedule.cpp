#include "input_frame_ring.h"
#include "input_split_schedule.h"
#include <cstdio>

using namespace hvinput;

int main() {
    InputSplitSchedule schedule;
    if (schedule.Choose(1000, false, true) != SplitWork::None) return 1;
    schedule.OfferPreview(101, 1000);
    if (schedule.Sequence() != 101 || schedule.CompletedUs() != 1000) return 2;
    // A running decode never waits for preview or for a Unity acknowledgement.
    if (schedule.Choose(2000, true, true) != SplitWork::Analysis ||
        schedule.Choose(2000, true, false) != SplitWork::Analysis) return 3;
    // Without ready analysis, use available GPU time for the latest preview.
    if (schedule.Choose(1000, false, true) != SplitWork::Preview ||
        schedule.Choose(2000, false, false) != SplitWork::DiscardPreview ||
        schedule.Choose(120999, false, true) != SplitWork::Preview ||
        schedule.Choose(121000, false, true) != SplitWork::DiscardPreview) return 4;
    schedule.OfferPreview(102, 121000);
    if (schedule.Sequence() != 102 || schedule.Choose(121001, false, true) != SplitWork::Preview) return 5;
    schedule.Reset();
    if (schedule.Choose(121001, false, true) != SplitWork::None ||
        schedule.Choose(121001, true, true) != SplitWork::Analysis) return 6;

    // Both output slots refer to one decoded frame and wait for distinct fences.
    InputFrameRing analysis, preview;
    analysis.Begin(3);
    preview.Begin(3);
    int model_slot = analysis.Acquire(3, 101);
    int preview_slot = preview.Acquire(3, 101);
    if (!analysis.Queue(model_slot, 7) || !preview.Queue(preview_slot, 8) ||
        analysis.Complete(model_slot, 6) || !analysis.Complete(model_slot, 7) ||
        preview.Complete(preview_slot, 7)) return 7;
    analysis.Observe(model_slot, 101);
    if (analysis.Published() != model_slot || preview.Published() != -1 ||
        !preview.Complete(preview_slot, 8) ||
        analysis.Slot(model_slot).sequence != preview.Slot(preview_slot).sequence) return 8;
    analysis.Release(model_slot, 101);
    analysis.Collect(7);
    if (analysis.Live() != 0 || preview.Live() != 1) return 9;

    std::puts("Independent frame identity, split fences and bounded preview opportunity PASS");
    return 0;
}
