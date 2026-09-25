# RAE-6 ROM window-server controlled boot

Set `EKA2L1_ROM_WSERV=1` on an epoc7 device. This also selects ROM Eikon;
HLE FBS remains. The host wserv object is the display adapter, while ARM
`ewsrv.exe` owns the public Windowserver endpoint and uses original scdv.

The ROM filesystem presents an in-memory copy of `Z:\System\Data\wsini.ini`
with STARTUP set to C:\System\Programs\SysState.exe. No firmware files are changed.
The existing prestart/app launch mechanism supplies the service set instead:
ROM wserv and SecurityServer, then the requested application, which
connects to ROM Eikon. Wserv starts SysState through its normal delayed STARTUP path. Supply `C:\System\Programs\SysState.exe` built from
`tools/s80-sysstate`; it keeps SharedData state.val=203 resident. The existing
phone-less Phone Server stand-in remains active. The default HLE path is unchanged.

Set `EKA2L1_ROM_STARTER=1` as well to retain the original STARTUP line for
phone-stack investigation. This is currently unsupported: SpeDe opens
ISA_IF_DRIVER_2ND, whose logical channel is absent; it converts its trapped
construction failure to panic -15. Starter also tries the absent D_EXC.exe
and then calls process kill with an invalid handle, leading to USER 83
(ETUtlKernelServerSend). These failures are not suppressed in the kernel.

The EKA1 executive additions are FullName (UTF-16, descriptor then handle),
RTimer::Lock (status, phase, handle), and window-server registration/power
hooks. Lock follows the nominal 64 Hz Symbian phase grid, including the
initial KErrGeneral synchronization and cancellation. A dying requester
abandons outstanding timers before its memory is freed. The host panel is
always powered: on requests complete; off requests return KErrNotSupported.
VideoDriver exposes the single native mode, validates mode selection, and
uses display HAL for dimensions, stride, pixel format and buffer address.
There is no analog contrast or indexed palette on this backend.

ChangeLocale validates the library and completes the executor status even on
failure. Only the core English ELocl.dll is supported; other locale DLLs are
explicitly rejected. The package's unmapped ELOCL.LOC still resolves to that
core DLL. Runtime installation of arbitrary locale DLL tables is not implemented.

Raw input uses the EKA1 CaptureEventHook/RequestEvent executive contract. Qt
and control-socket input enter a kernel FIFO owned by the capturing thread;
ARM wserv owns key state, translation, repeat and client delivery. Nordic
keyboard index 6 must match the station X mapping. Adjacent pointer moves
coalesce; key/button edges remain ordered, with overflow reported at 4096.
Owner death/reset drops stale requests and events.

The control socket accepts `key <name> [down|up]` and `type <UTF-8 text>`.
`key` names physical keys (including shift, chr, arrows and F1–F12); `type`
uses the configured layout's physical combinations. These commands return
`OK queued ...` and execute in order, one command/character per 30 ms.
This pacing avoids overrunning ARM wserv's own small client event queue.
It does not pace external X input. Reset and quit discard pending commands.
Unicode with no physical combination has no direct ROM character bypass.
Use EKA2L1_KEYLOG=1 for raw type/tick/scancode/coordinate/completion traces.
The standalone ARM client in `tools/s80-input-probe` displays pointer events;
Desk icon selection by pointer remains unproven.
Guest contract tests are in `tools/s80-rom-boot-test` (same toolchain setup as
SysState). Run BootContracts.exe and inspect C:\boot-contracts.txt. Host
regressions cover the clock phase grid and wsini filtering in ekatests.
