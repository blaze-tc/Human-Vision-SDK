# Runtime profiles

Purpose: data-only composition presets. Consumes plugin IDs/capabilities and
ModelPack IDs; produces validated RuntimeProfile. Allowed dependencies are listed
component IDs; executable source and platform logic are forbidden here.
Primary files: auto.json and cpu.json. Read docs/maintenance/PROFILE_GUIDE.md for
adding a preset. Focused tests: Profile, RuntimeSession; architecture guard.
Symptoms: missing plugin, incompatible capacity, unintended CPU fallback.
