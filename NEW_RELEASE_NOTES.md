### BetterAngle Pro v5.5.194
- **fix: Safe Revert to Scalar Matching.** Reverted the pixel-matching engine back to the original one-by-one scalar loop (L2 distance), removing all AVX2 SIMD logic to permanently eliminate thin-target pair-grouping glitches at the cost of a slight latency increase.
