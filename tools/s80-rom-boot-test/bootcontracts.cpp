// Guest-side EKA1 executive/VideoDriver contract regression. No ROM bytes.
// Writes C:\boot-contracts.txt; exits with the failed assertion count.
#include <e32base.h>
#include <e32svr.h>
#include <f32file.h>
#include <hal.h>
#include <hal_data.h>

static RFile report;
static TInt failures = 0;
static void Check(const TDesC8& name, TInt actual, TInt expected) {
    TBuf8<160> line;
    line.Append(name); line.Append(_L8(" actual=")); line.AppendNum(actual);
    line.Append(_L8(" expected=")); line.AppendNum(expected);
    line.Append(actual == expected ? _L8(" PASS\r\n") : _L8(" FAIL\r\n"));
    report.Write(line); report.Flush();
    if (actual != expected) ++failures;
}

static TInt DeadTimer(TAny*) {
    RTimer timer;
    if (timer.CreateLocal() != KErrNone) return -100;
    TRequestStatus status;
    timer.Lock(status, ETwelveOClock);
    // Leave an outstanding timer for the thread's kernel handle cleanup.
    return 7;
}

GLDEF_C TInt E32Main() {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    RFs fs;
    if (fs.Connect() != KErrNone) return -1;
    if (report.Replace(fs, _L("C:\\boot-contracts.txt"), EFileWrite) != KErrNone) return -2;
    RFile config;
    TInt opened = config.Open(fs, _L("Z:\\System\\Data\\wsini.ini"), EFileRead);
    Check(_L8("wsini open"), opened, KErrNone);
    if (!opened) {
        TBuf8<512> bytes; config.Read(bytes);
        report.Write(_L8("wsini bytes: "));
        TBuf8<256> ascii;
        for (TInt i=2; i<bytes.Length(); i+=2) ascii.Append(bytes[i]);
        report.Write(ascii); report.Write(_L8("\r\n")); report.Flush(); config.Close();
    }
    RThread self;
    TFullName full = self.FullName();
    Check(_L8("FullName thread suffix"), full.Right(self.Name().Length()).Compare(self.Name()), 0);
    Check(_L8("FullName thread includes owner"), full.Length() > self.Name().Length(), 1);
    RLibrary invalid;
    Check(_L8("ChangeLocale invalid handle"), UserSvr::ChangeLocale(invalid), KErrBadHandle);
    RLibrary locale;
    TInt load = locale.Load(_L("ELOCL.LOC"));
    Check(_L8("locale core fallback"), load, KErrNone);
    if (!load) Check(_L8("ChangeLocale core"), UserSvr::ChangeLocale(locale), KErrNone);
    locale.Close();

    RTimer timer;
    Check(_L8("CreateLocal"), timer.CreateLocal(), KErrNone);
    TRequestStatus status;
    timer.Lock(status, EOneOClock);
    User::WaitForRequest(status);
    Check(_L8("Lock first synchronizes"), status.Int(), KErrGeneral);
    timer.Lock(status, EOneOClock);
    User::WaitForRequest(status);
    Check(_L8("Lock next phase"), status.Int(), KErrNone);
    timer.Lock(status, ETwelveOClock);
    timer.Cancel();
    User::WaitForRequest(status);
    Check(_L8("Lock cancellation"), status.Int(), KErrCancel);
    timer.After(status, 0);
    User::WaitForRequest(status);
    Check(_L8("Reuse after cancel"), status.Int(), KErrNone);
    timer.Close();

    RThread child;
    _LIT(KChildName,"BootTimerChild");
    TInt created = child.Create(KChildName, DeadTimer, 0x2000, 0x1000, 0x10000, 0);
    Check(_L8("child create"), created, KErrNone);
    if (!created) {
        child.Logon(status); child.Resume(); User::WaitForRequest(status);
        Check(_L8("thread with pending Lock exits"), status.Int(), 7);
        child.Close();
    }
    TInt mode = -99;
    Check(_L8("HAL mode query"), HAL::Get(HALData::EDisplayMode, mode), KErrNone);
    Check(_L8("HAL native mode"), mode, 0);
    Check(_L8("HAL select native mode"), HAL::Set(HALData::EDisplayMode, 0), KErrNone);
    Check(_L8("HAL reject unknown mode"), HAL::Set(HALData::EDisplayMode, 1), KErrArgument);
    mode = 0;
    Check(_L8("HAL native bpp query"), HAL::Get(HALData::EDisplayBitsPerPixel, mode), KErrNone);
    Check(_L8("HAL native bpp"), mode, 16);
    mode = 1;
    Check(_L8("HAL reject bpp of unknown mode"), HAL::Get(HALData::EDisplayBitsPerPixel, mode), KErrArgument);
    Check(_L8("HAL display on completion"), HAL::Set(HALData::EDisplayState, 1), KErrNone);
    Check(_L8("HAL kiosk rejects display off"), HAL::Set(HALData::EDisplayState, 0), KErrNotSupported);
    // Let any stale timer callback run; a dead requester must never be signaled.
    User::After(1100000);
    report.Close(); fs.Close(); delete cleanup;
    return failures;
}
