# Changelog

## 0.1.1 — 2026-11-24

- Validate the Unity Vulkan context, function resolver and imported live-preview target before issuing live commands.
- Keep delayed queue callbacks and GPU fences bound to the current device generation. Surface live errors without routing the analysis path through preview.
- Preserve existing Input ABI exports and all non-Android-Input native payloads.

## 0.1.0 — 2026-11-24

- Add a native-owned RTSP decode and image-service path with independent live and analysis output pools, GPU synchronization and explicit lease retirement.
- Define paired zero width/height bounds as the decoded visible source resolution. Reject a partially zero request; retain explicit positive bounds for scaled analysis or legacy capped output.
- Preserve visible crop dimensions instead of decoder padding, including 1920×1080 and supported 4K layouts. Report unsupported original-size output without silently substituting a smaller image.
- Expose source decode, acquisition, live publication and analysis counters independently of Unity's frame loop and inference throughput.
- Keep Input independent of SDK inference, models and backend libraries; preserve existing ABI layouts and entry points.

The previous public release was 0.1.0-preview.10. Device-specific sustained throughput and RK3588 acceptance are documented separately.
