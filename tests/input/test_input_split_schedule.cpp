#include "input_frame_ring.h"
#include "input_split_schedule.h"
#include <cstdio>

using namespace hvinput;

int main() {
    InputSplitSchedule schedule;
    if (schedule.Choose(1000, false, true, 0) != SplitWork::None) return 1;
    schedule.OfferPreview(101, 1000, 900);
    if (schedule.Sequence() != 101 || schedule.CompletedUs() != 1000) return 2;
    // Analysis is continuously ready. A free preview slot joins its rendering
    // opportunity, and pressure in that ring never delays the analysis lane.
    for (int i = 0; i < 60; ++i) {
        const int64_t now = 2000 + i * 33333;
        const bool hd_slot_free = i % 4 != 0;
        const auto expected = hd_slot_free ? SplitWork::AnalysisAndPreview : SplitWork::Analysis;
        if (schedule.Choose(now, true, hd_slot_free, now - 1000) != expected) {
            std::fputs("Ready analysis prevented a free HD preview opportunity\n", stderr);
            return 3;
        }
    }
    // Without ready analysis, convert a fresh fallback if its slot is free.
    // Its age is measured from decode, even when the analysis fence took time.
    if (schedule.Choose(1000, false, true, 0) != SplitWork::Preview ||
        schedule.Choose(2000, false, false, 0) != SplitWork::DiscardPreview ||
        schedule.Choose(120899, false, true, 0) != SplitWork::Preview ||
        schedule.Choose(120900, false, true, 0) != SplitWork::DiscardPreview ||
        schedule.Choose(120900, true, true, 900) != SplitWork::Analysis ||
        schedule.Choose(120900, true, true, 120899) != SplitWork::AnalysisAndPreview) return 4;
    schedule.OfferPreview(102, 121000, 120950);
    if (schedule.Sequence() != 102 || schedule.Choose(121001, false, true, 0) != SplitWork::Preview) return 5;
    // Rebind removes the old source's fallback even when its slot is free.
    schedule.Reset();
    if (schedule.Sequence() != 0 || schedule.CompletedUs() != 0 ||
        schedule.Choose(121001, false, true, 0) != SplitWork::None ||
        schedule.Choose(121001, true, false, 121000) != SplitWork::Analysis) return 6;

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

    // A failed preview target preparation releases its acquired slot.
    // Analysis can publish from its own successful conversion.
    analysis.Begin(4);
    preview.Begin(4);
    preview.Collect(8);
    model_slot = analysis.Acquire(4, 103);
    preview_slot = preview.Acquire(4, 103);
    preview.Cancel(preview_slot);
    if (preview.Live() || !analysis.Queue(model_slot, 9) ||
        !analysis.Complete(model_slot, 9)) return 10;

    // A batched conversion uses independent slots and one submit fence.
    model_slot = analysis.Acquire(4, 104);
    preview_slot = preview.Acquire(4, 104);
    if (!analysis.Queue(model_slot, 10) || !preview.Queue(preview_slot, 10) ||
        analysis.Complete(model_slot, 9) || preview.Complete(preview_slot, 9) ||
        !analysis.Complete(model_slot, 10) || !preview.Complete(preview_slot, 10) ||
        analysis.Slot(model_slot).sequence != preview.Slot(preview_slot).sequence) return 11;
    analysis.Collect(10);

    // A disconnect can arrive before the shared fence signals. Both old
    // slots must retire without publishing into the new generation.
    model_slot = analysis.Acquire(4, 105);
    preview_slot = preview.Acquire(4, 105);
    if (!analysis.Queue(model_slot, 11) || !preview.Queue(preview_slot, 11)) return 12;
    analysis.Begin(5);
    preview.Begin(5);
    analysis.Collect(10);
    preview.Collect(10);
    if (analysis.Live() != 1 || preview.Live() != 1 ||
        analysis.Complete(model_slot, 11) || preview.Complete(preview_slot, 11)) return 13;
    analysis.Collect(11);
    preview.Collect(11);
    if (analysis.Published() != -1 || preview.Published() != -1 ||
        analysis.Live() != 0 || preview.Live() != 0) return 14;

    // Slow CPU/NPU consumers can hold every analysis texture. A fresh
    // decoded image can still advance the live HD preview.
    for (int i = 0; i < 3; ++i) {
        const auto sequence = 106 + i;
        const auto fence = 12 + i;
        model_slot = analysis.Acquire(5, sequence);
        if (!analysis.Queue(model_slot, fence) || !analysis.Complete(model_slot, fence)) return 15;
        analysis.Observe(model_slot, sequence);
    }
    if (schedule.Choose(122000, analysis.CanStartConversion(),
                        preview.CanStartConversion(), 121999) != SplitWork::PreviewOnly) {
        std::fputs("Full analysis ring starved a fresh HD preview\n", stderr);
        return 16;
    }
    schedule.OfferPreview(108, 122000, 121999);
    if (schedule.Choose(122001, false, true, 122000) != SplitWork::Preview ||
        schedule.Choose(241999, false, true, 241998) != SplitWork::PreviewOnly ||
        schedule.Choose(241999, false, true, 121999) != SplitWork::DiscardPreview ||
        schedule.Choose(122001, false, false, 122000) != SplitWork::DiscardPreview ||
        schedule.Choose(122001, true, true, 122000) != SplitWork::AnalysisAndPreview) return 17;
    schedule.Reset();
    preview_slot = preview.Acquire(5, 109);
    if (!preview.Queue(preview_slot, 15) || !preview.Complete(preview_slot, 15) ||
        analysis.Slot(analysis.Published()).sequence != 108 ||
        preview.Slot(preview.Published()).sequence != 109 ||
        schedule.Choose(242000, false, true, 122000) != SplitWork::None ||
        schedule.Choose(122001, false, true, 0) != SplitWork::None) return 18;
    analysis.Release(0, 106);
    analysis.Collect(15);
    if (schedule.Choose(122002, analysis.CanStartConversion(),
                        preview.CanStartConversion(), 122001) != SplitWork::AnalysisAndPreview) return 19;
    model_slot = analysis.Acquire(5, 110);
    preview_slot = preview.Acquire(5, 110);
    if (!analysis.Queue(model_slot, 16) || !preview.Queue(preview_slot, 16) ||
        !analysis.Complete(model_slot, 16) || !preview.Complete(preview_slot, 16) ||
        analysis.Slot(analysis.Published()).sequence != 110 ||
        preview.Slot(preview.Published()).sequence != 110) return 20;

    std::puts("Bounded HD preview, independent split slots, batched fence and source retirement PASS");
    return 0;
}
