# Temporary playable milestone (AI-assisted)

Human-approved priority, 2026-10-24: a native C movement prototype using Jak's
real model and animations, walking, running and jumping around a small test map.
The existing full-port goal remains active. This slice is not original GOAL
gameplay and does not establish full-game completion.

Acceptance requires controller-driven analog walking/running, edge-triggered
jumping, gravity and stable floor/platform collision, a follow camera, and
recognizable Jak with appropriate original animation clips. Verify idle, walk,
run, takeoff, airborne, landing, movement bounds, memory and clean START exit in
stock 16 MiB Flycast. No disc preparation or hardware validation is implied.

Implement movement as a portable fixed-step module, independently exercise its
collision/jump behavior, then connect it to a separate Dreamcast executable.
Use a small synthetic map to isolate movement from unfinished level collision.
Reuse the existing PowerVR texture path and upstream SH4ZAM transforms. Keep
animation and character data in private local outputs generated from the
user's existing extraction. Publish source and synthetic fixtures only.

The local extraction contains `eichar-lod0.glb`: 83 skin joints and 95 clips,
including stance, walk, run, jump and landing. This is a structural inventory;
conversion, appearance, sampling and memory budgets still need verification.
Facial morphs, combat, original movement tuning and complete world streaming
are outside this temporary milestone.
