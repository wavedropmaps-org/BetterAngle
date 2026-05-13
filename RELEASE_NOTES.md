### BetterAngle Pro v5.5.238
- Automated build release.

### BetterAngle Pro v5.5.237
- **Ultra-Legacy Baseline Restoration**: Reverted the codebase to the state of version 5.5.86. This is a very early version of the Pro engine. It lacks modern features like DXGI, AVX2, and high-precision angle logic. Use with caution.

### BetterAngle Pro v5.5.235
- **Stability Baseline Restoration**: Reverted the codebase to the state of version 5.5.151. This version uses the reliable GDI BitBlt scanner instead of DXGI to ensure 100% color matching accuracy.

### BetterAngle Pro v5.5.233
- **Legacy Baseline Restoration**: Reverted the codebase to the state of version 5.5.160. This is a pre-AVX2 version that uses the standard scalar scanner. Note: This version does not include recent fixes for keyboard ghosting or thread-affinity input locks.

### BetterAngle Pro v5.5.86
- Improved detector and overlay logic for smoother performance
- Automated build release.
