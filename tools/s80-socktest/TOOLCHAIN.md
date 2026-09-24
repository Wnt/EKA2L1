# SockTest and the Linux recipe for Symbian OS 7.0s (Nokia 9300, Series 80 v2) binaries

`socktest.cpp` is a small EKA1 ARMI executable that walks the Symbian OS 7.0s socket API one call at a
time and paints every call's return code in its own window, so the first call that fails — or never
completes — under an emulator is named on screen. It was built with the recipe below and run on the real
Nokia 9300 ROM (RAE-6) under this fork (`--device RAE-6`). MIT licence (header of `socktest.cpp`).

## Paths it walks (each in its own thread; the first two enabled paths are drawn)

| Path | What it does | ESock ops |
|---|---|---|
| A | Opera's shape: `RSocketServ::Connect`, `RConnection::Open`, `Start(TCommDbConnPref IAP n, DoNotPrompt)`, `RHostResolver::Open(…, conn)` + `GetByName`, `RSocket::Open(…, conn)`, `Connect`, `Write "GET / HTTP/1.0"`, `RecvOneOrMore` until EOF, close all | 0x3F 0x44 0x3E 0x29 0x3D 0x13 0x0E 0x0C 0x1D 0x2F 0x41 |
| B | implicit connection: `RHostResolver::Open`, `GetByName`, `RSocket::Open`, `Connect`, `Send`, `Read` 12 bytes | 0x28 0x29 0x06 0x13 0x09 0x0D |
| C | RConnection around start: default `Start()`, `Progress`, `ProgressNotification` (KLinkLayerOpen / any, 3 s then cancel), `GetIntSetting IAP\Id`, `IAP\IAPService`, `GetDesSetting IAP\IAPServiceType`, `IAP\Name`, `EnumerateConnections` | 0x43 0x46 0x47 0x48 0x4C 0x4F 0x51 |
| D | the same GET through the ROM's HTTP framework (`RHTTPSession` + ECom protocol plug-in) on the app's own started RConnection; every `THTTPEvent` becomes a row | framework-issued |
| E | as D, but the framework opens and starts its own connection | framework-issued |
| F | as E, plus a self-completing priority-100 active object for 20 s after `SubmitL` (starves the session thread's scheduler, then lets go) | framework-issued |
| x | modifier: also show each finished worker's `RThread::ExitReason()` (needs EKA1 exec 0x3E) | — |

Command line: `<host> <port> <iap> <paths>`, default `127.0.0.1 8931 1 ab`.

## Build

1. Toolchain: razvang-dev/Nokia-N-Gage-SDK-Toolchain **v1.0.1** release tarball
   `Nokia_NGage_SDK_Toolchain_v1.0.0_Linux_x86_64.tar.xz` (sha256 `3964851b…99ab`): prebuilt Linux x86_64
   GCC 2.9-psion-98r2 (Symbian build 539), arm-epoc-pe binutils, `petran`, `rcomp`, `makesis`, … Only its
   compiler, linker and tools are used — not its bundled N-Gage 6.1 SDK.
2. SDK: the Series 80 DP 2.0 SDK (Symbian OS 7.0s), unpacked from its InstallShield cabs with `unshield`.
   `prep-sdk.sh <SDK Epoc32 dir> <dest>` copies `Epoc32/include` + `Epoc32/release/armi/urel` and fixes the two
   things GCC on Linux trips over: CRLF line ends (GCC 2.9 does not treat `\`+CR+LF as a continuation, so
   every multi-line macro in `e32std.h` breaks) and header-name case (`mkcaseinc.py` makes a symlink for every
   `#include` spelling; put `caseinc/` after `Epoc32/include` on the include path).
3. `TC=<toolchain root> SDK=<prepared dir> ./build.sh` → `build/SockTest.exe`. The steps are the SDK's own
   `cl_gcc.pm` recipe for an ARMI UREL EXE: `-march=armv4t -mthumb-interwork -O -fomit-frame-pointer`, macros
   `__SYMBIAN32__ __GCC32__ __EPOC32__ __MARM__ __MARM_ARMI__ __EXE__ NDEBUG _UNICODE`, `EEXE.LIB` first, the
   two-pass `ld` + `dlltool --base-file` link, then `petran -uid1 0x1000007a -uid2 0 -uid3 <uid>`. Imports bind
   by ordinal, so the SDK's ARMI `.lib`s match the 9300 ROM DLLs.

A `.app` or a guest patch DLL differs only in `-D__DLL__`, a `.def`-driven export table, entry `_E32Dll`,
`EDLL.LIB` + `EDLLSTUB.LIB`, and the DLL UIDs (`0x10000079`, then `0x100039CE` for an app or
`0x1000008d` for a shared library). DLLs may not carry writable static data on EKA1; EXEs may.
Symbian OS 7.0s has no platform security: nothing is signed. `makesis socktest.pkg` makes an EKA1 SIS if a
device installer is wanted; the emulator does not need one.

## Run in this emulator

Copy `SockTest.exe` to `<data dir>/EKA2L1/data/drives/c/System/Programs/` and start
`eka2l1_qt --device RAE-6 --run 'C:\System\Programs\SockTest.exe' '<host> <port> <iap> <paths>'` (the argument
after the path becomes the process command line). The process repaints for an hour; when it exits the
frontend closes the emulator.

## Traps met

- `TPath` is a Symbian typedef — do not name your own class that.
- GCC 2.9 gives `cond ? des.Left(n) : des` the type `TDesC8` and slices the `TPtrC8` temporary; the copy then
  reads a garbage pointer (KERN-EXEC 3 inside euser's memcpy). Use `TPtrC8 s(des); if (cond) s.Set(…)`.
- EKA1 `RThread::ExitCategory()` (exec 0xC0003F) is still unimplemented here: calling it on an exited thread
  hands back a bad descriptor and the caller faults. `ExitReason()` (0x3E) is served from this branch on.
- `new (ELeave) T` of a non-`CBase` class does not zero memory.
