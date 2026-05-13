### BetterAngle Pro v5.5.236
- Automated build release.

### BetterAngle Pro v5.5.235
- **Stability Baseline Restoration**: Reverted the codebase to the state of version 5.5.151. This version uses the reliable GDI BitBlt scanner instead of DXGI to ensure 100% color matching accuracy.

### BetterAngle Pro v5.5.234
- Automated build release.

### BetterAngle Pro v5.5.233
- **Legacy Baseline Restoration**: Reverted the codebase to the state of version 5.5.160. This is a pre-AVX2 version that uses the standard scalar scanner. Note: This version does not include recent fixes for keyboard ghosting or thread-affinity input locks.

### BetterAngle Pro v5.5.232
- Automated build release.

### BetterAngle Pro v5.5.231
- **Stable Baseline Restoration**: Reverted the codebase to the state of version 5.5.181. This provides a clean, stable foundation while ensuring compatibility with previous calibration standards. All experimental changes since 181 have been removed.

### BetterAngle Pro v5.5.151
- Reverted the DXGI Desktop Duplication scanner back to GDI BitBlt. The color picker (CaptureDesktop) and the detector now sample from the same source again, so picked colors actually match what the scanner sees ? fixes "no colour match" after picking a color. Loses some scan-speed performance but trades it for reliable detection.
