### BetterAngle Pro v5.5.191
- **fix: AVX2 Per-Pixel Match Precision.** Rewrote the AVX2 fast-path scanner to calculate the L1 distance pixel-by-pixel instead of pairing them. This restores 100% detection accuracy for 1px-thin targets (like text and crosshairs) without sacrificing scan speed.
