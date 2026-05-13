### BetterAngle Pro v5.5.255
- **Feature: Sub-Pixel Interpolation (HUD Smoothing)**. Implemented a LERP-based visual smoothing engine that makes HUD decimals "glide" between values for a premium, high-resolution feel.
- **UI: HUD Smoothing Toggle**. Added a switch to the Engine tab to enable/disable visual interpolation.

### BetterAngle Pro v5.5.254
- Automated build release.

### BetterAngle Pro v5.5.253
- **Feature: Hardware-Direct Input Engine**. Replaced virtual key simulation with raw hardware scancode injection (W: 0x11, A: 0x1E, S: 0x1F, D: 0x20) for improved input fidelity and reduced OS processing overhead.
- **UI: Direct Hardware Input Toggle**. Added new "Engine" tab in Dashboard with toggle to enable/disable direct hardware mode per profile.
- **Compatibility: Regional Keyboard Support**. Implemented MapVirtualKey verification layer to ensure correct scancodes regardless of user's hardware layout.
- **Integration: FOV Transition State Sync**. Direct hardware mode now syncs input state after BlockInput releases during dive/glide transitions.

### BetterAngle Pro v5.5.252
- Automated build release.

### BetterAngle Pro v5.5.251
- **Feature: Atomic Shield (Input Smoothing)**. Implemented a 25ms logic-based smoothing window to prevent input lock flickering during frame stutters or particle interference.
- **UI: Smoothing Toggle**. Added a real-time switch to the Dashboard to enable/disable the Atomic Shield.
- Automated build release.

### BetterAngle Pro v5.5.250
- Automated build release.

### BetterAngle Pro v5.5.249
- Automated build release.

### BetterAngle Pro v5.5.248
- Automated build release.

### BetterAngle Pro v5.5.247
- Automated build release.

### BetterAngle Pro v5.5.246
- Automated build release.

### BetterAngle Pro v5.5.245
- **Emergency Revert: Restored Ultra-Legacy Build**. Rolled back the modern peak restoration. Reverted the codebase to the state of version 5.5.86 (with 700ms skydive stability).

### BetterAngle Pro v5.5.241
- **Stability Fix: Increased Skydive BlockInput to 700ms**. The shorter 300ms window was too aggressive for the legacy v5.5.86 sync logic. Increased back to 700ms to ensure angle stability during transitions. Alt-Tab remains at 200ms.

### BetterAngle Pro v5.5.239
- **Tweak: Faster BlockInput transitions**. Shortened the mouse lock durations for Alt-Tab (200ms) and Skydive transitions (300ms) to improve responsiveness.
