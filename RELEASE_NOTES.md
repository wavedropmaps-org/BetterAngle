### BetterAngle Pro v5.5.234
- Automated build release.

### BetterAngle Pro v5.5.233
- **Legacy Baseline Restoration**: Reverted the codebase to the state of version 5.5.160. This is a pre-AVX2 version that uses the standard scalar scanner. Note: This version does not include recent fixes for keyboard ghosting or thread-affinity input locks.

### BetterAngle Pro v5.5.232
- Automated build release.

### BetterAngle Pro v5.5.231
- **Stable Baseline Restoration**: Reverted the codebase to the state of version 5.5.181. This provides a clean, stable foundation while ensuring compatibility with previous calibration standards. All experimental changes since 181 have been removed.

### BetterAngle Pro v5.5.181
- Automated build release.

### BetterAngle Pro v5.5.160
- Automated build release.

### BetterAngle Pro v5.5.159
- Detector loop now spins (`_mm_pause()`) instead of `Sleep(1)` when actively scanning, so the scanner reacts to a fresh DXGI frame the instant it arrives. Saves ~0.5?1ms of latency per cycle. Pegs one CPU core at 100% during gameplay; drops to 0% (`Sleep(10)`) when Fortnite isn't focused or you're in selection mode. Bounded 16-iteration `_mm_pause` burst on `DXGI_ERROR_WAIT_TIMEOUT` caps the syscall rate at ~1M/s. `g_scannerCpuPct` debug metric switched to a binary 100/0 (the old formula read 0% in spin mode). Colour-matching path is untouched ? no behaviour change to colour selection or detection accuracy.
