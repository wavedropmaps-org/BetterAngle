### BetterAngle Pro v5.5.296
- **Fix: System Tray Quit Button & Left-Click**. Fixed an issue where the "Exit BetterAngle" button in the tray context menu would not quit the application. The tray icon now also natively supports single left-clicks to open the dashboard.
- **Fix: HUD Task View Visibility**. Fixed an issue where the HUD was still visible as a separate window in Windows Task View (Win+Tab).
- **Fix: HUD Accidental Dragging**. Enforced holding the `Ctrl` key to drag the HUD overlay, preventing accidental movement when clicking on the desktop.
- **Fix: Alt+Tab Lag**. Throttled overlay redraws to ~10fps when Fortnite is not the foreground window to fix DWM contention.
