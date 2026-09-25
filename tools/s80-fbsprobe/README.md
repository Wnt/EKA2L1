# ARM FBS contract probe

Build with `TC`, `SDK`, and `OUT` as documented in `build.sh`, then copy
`FbsProbe.exe` to the private device's `C:\System\Programs`. No SDK or ROM
bytes belong in this directory.

Run with `--run C:\System\Programs\FbsProbe.exe` and `EKA2L1_ROM_WSERV=1`
so that scdv keeps its ARM exports. The probe uses RFbsSession, CFbsScreenDevice,
CFbsBitGc and CFbsBitmap from the guest ROM. It draws directly into the screen:
this is a diagnostic, not a window-server/Desk or input proof. Other apps can
paint over it. Use the controlled boot configuration to avoid Starter.

Expected rows: System regular, SwissA regular, SwissA bold, all requested at
18 pixels. Below them: blue/yellow bitmap, its central masked slice, the same
bitmap after compression. Two identical XOR rectangles should cancel. The
right side holds a scaled ROM splash bitmap. The bitmap is duplicated and the
original handle closed before drawing, testing retained shared storage.
`FBSPROBE` RDebug lines report metrics and completion; enable Emulated.Stdout.

The mixed-mode bootstrap regression this exposes is a cold RFbsSession client
opening FbsSharedChunk before any host launcher has created an icon. A public
HLE endpoint without its heaps lets the guest start a second, ROM FBS and apply
HLE object offsets to the ROM heap. Heaps must be published before guest startup.
