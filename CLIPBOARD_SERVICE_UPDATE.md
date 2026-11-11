# Clipboard Service update

- Win+V integration embeds and applies `Windows Clipboard Service.reg` whenever any Clipboard Service global hotkey is exactly **Win+V**.
- The registry state is checked first, so elevation is requested only if Windows clipboard history/policies are not already disabled as required by the supplied registry file. The check is repeated before global hotkeys are registered, including at startup.
- The current-user clipboard-history value is also written before elevation, so the correct HKCU is modified even when UAC credentials come from a different administrator account.
- Global hotkey registration retries transient failures; Win+V receives a longer retry window after the Windows shell policy update.
- Clipboard monitoring reliability was improved without changing the UI or normal features: listener registration is verified, failed listener setup retries automatically, USER32 module handles are released, clipboard-open pings retry briefly when another process has the clipboard locked, and legacy clipboard-chain messages use a timeout so a hung application cannot indefinitely block Clipboard Service.
- The application icon is rebuilt from the supplied 512 px clipboard artwork as a multi-resolution Windows `.ico`; the original 512 px PNG is kept in `res/ClipboardService.png` for future builds.
- Existing settings pages, layout, clipboard history behavior inside Clipboard Service, paste behavior, and other existing functionality are left unchanged.
