### BetterAngle Pro v5.5.291
- **Fix: Restore FOV Transition Lock**. Both glide→dive and dive→glide transitions now correctly block input for 700ms on a detached thread, preventing mouse movement from corrupting the angle during the sensitivity scale switch.
- **Fix: Overlay Disappearing Bug**. Re-assert `HWND_TOPMOST` on the HUD overlay when Fortnite regains focus after an Alt-Tab so the overlay doesn't disappear behind the game window.
- **Tweak: Alt-Tab Cooldown**. Increased the Alt-Tab return `BlockInput` duration from 200ms to 300ms.
- **Cleanup: Dead Input Locking Logic**. Removed unused variables and mutexes (e.g., `g_mouseSuspendedUntil`, `g_lockMutex`, `g_blockInputMutex`, `FlushPendingInputMessages`) that were remnants of the old locking system.
