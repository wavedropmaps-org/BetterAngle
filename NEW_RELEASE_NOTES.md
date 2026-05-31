### BetterAngle Pro v5.5.287
- **Fix: Angle Glitching on Alt-Tab Return**. Fixed a race condition where raw input from the desktop could briefly leak into the game's angle calculation when tabbing back in. Added direct foreground checking and angle state snapshotting to ensure clean focus transitions.
- **Cleanup: Removed Nitro Transition Locking**. Removed obsolete dive/glide input locking code as ghosting is no longer an issue, which prevents accidental locking conflicts.
