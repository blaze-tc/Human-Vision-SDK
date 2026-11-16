#include "input_frame_ring.h"
#include "input_split_schedule.h"
#include <cstdio>

using namespace hvinput;

int main() {
    InputSplitSchedule handoff;
    if (handoff.Waiting()) return 1;
    handoff.CompleteAnalysis(3, 101, 1000);
    if (!handoff.Waiting() || handoff.Sequence() != 101) return 2;
    if (handoff.Acknowledge(2, 101, 3000) || handoff.Acknowledge(3, 100, 3000)) return 3;
    if (handoff.Decide(42999, true, true) != PreviewDecision::Wait ||
        handoff.Decide(43000, true, true) != PreviewDecision::Discard ||
        handoff.Decide(43000, true, false) != PreviewDecision::Convert ||
        handoff.Decide(2000, false, true) != PreviewDecision::Discard) return 4;
    if (!handoff.Acknowledge(3, 101, 2000) || !handoff.Acknowledge(3, 101, 3000) ||
        handoff.CompletedUs() != 1000 || handoff.AcknowledgedUs() != 2000 ||
        handoff.Decide(2000, true, true) != PreviewDecision::Convert) return 5;
    handoff.Reset();
    if (handoff.Acknowledge(3, 101, 3000)) return 6;

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

    std::puts("Independent frame identity, split fences and bounded preview handoff PASS");
    return 0;
}
