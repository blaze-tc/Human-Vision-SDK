# Runtime profiles

Purpose: data-only composition presets. Consumes plugin IDs/capabilities and
ModelPack IDs; produces validated RuntimeProfile. Allowed dependencies are listed
component IDs; executable source and platform logic are forbidden here.
Primary shipping files: auto.json and cpu.json. The three `android-*-nohands`
profiles are forced-provider diagnostic presets: they keep the same 1-2 TopDown /
3-8 RTMO body policy, disable hand inference, and contain exactly one backend so a
failed provider cannot be measured as another provider. Read
docs/maintenance/PROFILE_GUIDE.md for adding a preset. Focused tests: Profile,
RuntimeSession; architecture guard.
Symptoms: missing plugin, incompatible capacity, unintended CPU fallback.
