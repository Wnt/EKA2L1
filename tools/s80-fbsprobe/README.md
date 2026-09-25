# ARM FBS contract probe

Build with `TC`, `SDK`, and `OUT` as documented in `build.sh`, then copy
`FbsProbe.exe` to the private device's `C:\System\Programs`. No SDK or ROM
bytes belong in this directory.

Run with `--run C:\System\Programs\FbsProbe.exe` and `EKA2L1_ROM_WSERV=1`
so that scdv keeps its ARM exports. The probe uses RFbsSession, CFbsScreenDevice,
CFbsBitGc and CFbsBitmap from the guest ROM. By default it composes a shared bitmap and blits it directly into the screen:
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

The probe synchronizes with a prestarted ROM FBS through RProcess rendezvous.
It checks process open by full name, thread handle ownership and missing-name
failure before running the FBS contracts. It releases each font before opening
the next; that intentionally exercises shared glyph-cache invalidation on
COpenFont address reuse. Actual face/style/bitmap type and metrics are logged.

Pass the guest argument `window` after the executable path to display the canvas
through RWindow/CWindowGc instead. That mode exercises cross-process duplication
and the ROM window server's redraw store. It is a distinct diagnostic: direct
panel success does not imply this path succeeds. The direct image may later be
overwritten by Wserv.

The probe checks IsFileInRom, ESeekAddress and User::IsRomAddress for the stock
Nokia 9300 desk.aif, then loads bitmap 2 from its embedded store at offset 0x4c.
It closes the file and duplicates the loaded bitmap before drawing it. Full ROM
FBS requires the extracted ROM store to have stable, globally readable backing.

The canvas is explicitly assigned a physical size using the panel's pixels-to-twips
conversion. ROM bitmap Create leaves its twips size zero; DrawBitmap(TPoint) uses
that size and therefore draws nothing without SetSizeInTwips. BitBlt uses pixels,
so a successful direct-panel test alone does not diagnose a sharing defect.

Add `aa` to the guest argument (for example `window aa`) to explicitly request
antialiased glyphs. Actual returned bitmap types and metrics are logged; bitmap
fonts may still return monochrome. Default requests leave the choice to ROM FBS.
