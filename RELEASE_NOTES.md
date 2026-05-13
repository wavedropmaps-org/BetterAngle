### BetterAngle Pro v5.5.246
- Automated build release.

### BetterAngle Pro v5.5.245
- **Emergency Revert: Restored Ultra-Legacy Build**. Rolled back the modern peak restoration. Reverted the codebase to the state of version 5.5.86 (with 700ms skydive stability).

### BetterAngle Pro v5.5.241
- **Stability Fix: Increased Skydive BlockInput to 700ms**. The shorter 300ms window was too aggressive for the legacy v5.5.86 sync logic. Increased back to 700ms to ensure angle stability during transitions. Alt-Tab remains at 200ms.

### BetterAngle Pro v5.5.239
- **Tweak: Faster BlockInput transitions**. Shortened the mouse lock durations for Alt-Tab (200ms) and Skydive transitions (300ms) to improve responsiveness.
