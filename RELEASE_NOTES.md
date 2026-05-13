### BetterAngle Pro v5.5.231
- **Stable Baseline Restoration**: Reverted the codebase to the state of version 5.5.181. This provides a clean, stable foundation while ensuring compatibility with previous calibration standards. All experimental changes since 181 have been removed.

### BetterAngle Pro v5.5.182
- Automated build release.

### BetterAngle Pro v5.5.181
- Automated build release.

### BetterAngle Pro v5.5.180
- **chore: Remove dead code ? FlushPendingInputMessages and g_justRefocused (v5.5.180).** `FlushPendingInputMessages()` was defined in BetterAngle.cpp but never called anywhere in the current codebase (11 lines of dead code). `g_justRefocused` was declared as a global atomic bool and exchanged in DetectorThread, but was never set to true anywhere ? the protection it was meant to provide never fired. Both removed cleanly with no functional impact.
