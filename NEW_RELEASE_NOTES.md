### BetterAngle Pro v5.5.204
- **perf: Early-Exit Scan logic.** The scanner now stops immediately once your required match percentage is reached. This reduces CPU usage during active locks.
- **chore: Removed ineffective GDI sub-frame check.** Eliminates micro-stuttering caused by context-switching to the message queue during high-speed scans.
