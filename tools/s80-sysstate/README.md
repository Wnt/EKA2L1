# Controlled Series 80 boot helper

Build `SysState.exe` with `TC=<EKA1 toolchain> SDK=<prepared S80 SDK> OUT=<output> ./build.sh`.
Install it as `C:\System\Programs\SysState.exe` in the emulator data directory.
No SDK files or generated executable are tracked here.

The helper keeps SharedData's normal boot-state category alive. With
`EKA2L1_ROM_WSERV=1`, it also connects to the emulator's private
`EKA2L1RomWindowBridge` endpoint and queries the **real** `RWsSession` group list,
name, owning thread, and focus. Snapshots are published atomically. Host kiosk
`list`/`focus` use these snapshots and discard groups whose process has exited.
`switch` replies `OK queued ...`; the helper executes
`SetWindowGroupOrdinalPosition(id, 0)`, then republishes focus. Query `focus` to
observe completion. This preserves the window-group name UID used by apparc,
including apps whose process UID differs from their application UID.

Rebuild this helper when upgrading to the ROM kiosk bridge. An older helper
still boots Desk, but `list`/`focus`/`switch` report that the bridge is not ready;
home recovery avoids blindly starting a duplicate Desk. The endpoint is only
registered for the opt-in 7.0s ROM window server. On default HLE hosts, the helper
retains its original resident state-publisher behavior.

The wire ABI is versioned, bounded to 64 groups with 256 UTF-16 code units per
name, and accepts one SysState session. Unsupported/truncated/duplicate snapshots
are rejected without partially replacing the last list. The helper has no window
group and does not consume keyboard input or paint a status pane.
