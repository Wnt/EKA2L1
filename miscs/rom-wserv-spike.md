# RAE-6 ROM window-server feasibility spike

`EKA2L1_ROM_WSERV=1` enables an experimental EKA1/epoc7 path for Nokia 9300
RAE-6. Leave the variable **unset** for normal HLE behavior; any value enables
the experiment, including `0`. Other epoc7 devices are untested.

The spike reached framebuffer output: ARM `ewsrv.exe` registered Windowserver,
Desk's connection and subsequent IPC commands completed, and the original ARM
graphics path displayed the Nokia hands startup image through the existing host
framebuffer upload. HLE FBS remained enabled. Full boot is unstable and input is
not implemented. This is not a usable replacement for HLE wserv yet.

## Implementation

- Prestart `Z:\System\Libs\ewsrv.exe` before applications.
- Register the host wserv object as `EKA2L1HostDisplay`, freeing the public
  `Windowserver` name for the ROM process. Qt and HAL still need that host object
  for screen allocation and presentation; removing it would break host casts.
- Skip `patch/scdv.dll.map` so the original ARM scdv exports execute.
- Resolve bare `ELOCL.LOC` to core-ROM `Z:\System\Libs\elocl.dll`. In this package,
  the extracted .LOC is linked outside the supplied core ROM, while the core
  contains an executable locale DLL with the same UID3 0x100065A5.
- Complete executor 0x52 (`UserSvr::ChangeLocale`) while keeping HLE locale
  tables. This is a bootstrap stub, not locale installation.
- Supply VideoDriver control 8 (hardware mode index 0), 9 (one mode), 16
  (specified video information), 13 (display on), and 3/4 (contrast/current max).
  Hardware mode indices are not `TDisplayMode` values. Existing controls 14/15
  and HAL provide the screen properties and framebuffer address.
- Set the statuses for `RequestEvent` (SVC 0xC00079, r1) and `RTimer::Lock`
  (0xC0004A, r0) pending. Otherwise their zero statuses allow their active
  objects to consume the server-receive wakeup, stranding connections. These
  requests are never completed by the spike.

All changes are gated by the switch and `epocver::epoc7`. No host graphics or Qt
changes are necessary for the initial picture. Existing `ScreenBuffer0`,
`map_direct_framebuffer`, and `screen::present_framebuffer` provide allocation,
dirty checking and upload. The supplied panel mode is 640x200 RGB565,
little-endian, stride 1280, with the existing Nokia 32-byte prefix metadata.
Offset/edge correctness remains unverified.

## Runtime result and limitations

The decisive run used ROM wserv, HLE Eikon, and HLE FBS. Windowserver registered
at 17:03:40.622 UTC on 2026-09-25; Desk connect completed at 17:03:40.863. Wserv
remained alive in the 17:04:03.793 thread dump. Its FBS bitmap duplication requests
and client command processing accompany the visible startup frame. The spike
did not trace each framebuffer store to a particular guest thread.

ROM wsini starts `\SYSTEM\PROGRAMS\STARTER`. The resulting full phone startup
repeatedly raises `SpeDe Server Panic -15` and `StarterServer USER 83`. The run
ended about 25 seconds after boot, with guest Main stack access violations and
`KERN-EXEC 3` at the end of the log. Exit status was not retained, so there is no
proven host crash diagnosis. There is no ROM-mode Desk or input success claim.

| Missing/partial interface | Work still required |
|---|---|
| CaptureEventHook / ReleaseEventHook | Ownership and lifecycle, SVC 0xC00077/78. |
| RequestEvent / Cancel | Raw event queue, ABI, completion/cancel, 0xC00079/7A. |
| Host input / AddEvent | Divert to the ROM raw queue instead of the HLE window tree. |
| RTimer::Lock | Display timing, cancellation and cleanup, 0xC0004A. |
| WsRegisterThread / screen power | Thread and power hooks, 0xC00087/C000AD/8000AC. |
| VideoDriver / HAL / EKeyb | Complete requested controls; EKeyb success stubs return no payload. |
| Locale / FullName | Correct ChangeLocale contract and unimplemented FullName SVC 0x80005B. |
| Starter dependencies | Explain and resolve the first boot panics and restart loop. |
| FBS interoperability | Test fonts, bitmap layout/refcounts and graphics beyond startup. |

ROM FBS was not attempted. Host applist/icon/font users directly depend on HLE
FBS objects and would need a guest IPC bridge or refactor. Retaining HLE FBS
means this result does not prove elimination of all rendering defects.

## Validation and reproduction

Build the complete `eka2l1_qt` target and run with its full bin tree (patch,
compat, resources and scripts). With an installed RAE-6 data directory:

```sh
EKA2L1_ROM_WSERV=1 XDG_DATA_HOME=/path/to/private/data DISPLAY=:370 \
  ./eka2l1_qt --device RAE-6 --run Desk --kiosk --kiosk-scale 2 \
  --kiosk-geometry 1280x400+0+0 --kiosk-home 0x101f8e4f \
  --control-socket /path/to/private/control.sock
```

Use a private X server and capture during startup: the picture is transient and
waiting for final quiescence can capture an empty root after boot failure.
Enable `log-ipc`, `Service.Track:info`, `EKA2L1_IPC_WATCH_SECS=6`,
`EKA2L1_THREAD_DUMP_SECS=8`, and `EKA2L1_TRAP_TRACE=1` for diagnosis.

Final Release build passed. With the switch absent, inspected frames showed
Desk and Documents containing `Z3 final HLE typing 9300`. The existing suite
passed 310 test cases / 28,868 assertions. These are smoke-regression results,
not complete behavioral equivalence.

The full artifact report, ARM inventory, run manifests, logs and reviewed frames
are retained in the coordinator's Z3 job directory (`REPORT.md`, `INVENTORY.md`,
`runs/`, `logs/`, `frames/`); ROM bytes and generated data are not committed.
Recommendation: pursue a bounded boot/input follow-up while keeping this mode
opt-in. A useful mixed-mode prototype is estimated at 2–4 engineering weeks;
full ROM FBS may add 1–3 weeks, with substantial compatibility uncertainty.
