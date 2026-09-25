// sysstate.cpp -- publish the Series 80 v2 system state a Starter-less boot lacks.
//
// On the Nokia 9300 / 9500 (Symbian OS 7.0s) the ROM's Starter.exe keeps the boot state in a
// temporary SharedData category, 0x10005943: while it initialises it sets sys.wd2sr=0, sys.swsr=100
// (the software start-up reason), state.src=[100059c9], state.val=201 and SysState=201
// (ESWStateInitialising); once its start list is done, state.val=203 and SysState=203 (ESWStateNormal).
// (Sequence from Nokia's own SDK emulator log, EPOCWIND.OUT of a cold boot to Desk.)
//
// Under EKA2L1 an application can run without Starter (--run Desk). The ROM Eikon server then finds no
// "state.val": its alarm alert server refuses the ROM AlarmServer's session with KErrNotFound, the
// AlarmServer exits and is restarted twice a second forever. This program sets the values Starter
// would leave behind after a normal boot and then stays resident, since a temporary category lives
// only as long as a session that assigned it (it is started by EKA2L1_PRESTART, never shown).
//
// Build: tools/s80-sysstate/build.sh (the N5 recipe: razvang-dev EKA1 GCC 2.9 + S80 DP 2.0 ARMI libs).
// License: MIT (this file is ours; the SDK headers and libraries keep their own terms).

#include <e32base.h>
#include <e32std.h>
#include <w32std.h>
#include "../../src/emu/services/include/services/window/rom_bridge_protocol.h"

// RSharedDataClient lives in the ROM's CommonEngine.dll; the S80 DP 2.0 SDK ships COMMONENGINE.LIB but
// no header. The declarations below match the library's exports (GCC 2.9 mangled names):
//   __17RSharedDataClient, Connect__17RSharedDataClienti,
//   AssignToTemporaryFile__C17RSharedDataClientG4TUid, SetInt__17RSharedDataClientRC7TDesC16i,
//   SetString__17RSharedDataClientRC7TDesC16T1, Close__17RSharedDataClient.
// The real object's size is not published; the spare bytes keep its members inside our allocation.
class RSharedDataClient : public RSessionBase {
public:
    IMPORT_C RSharedDataClient();
    IMPORT_C TInt Connect(TInt aReserved);
    IMPORT_C TInt AssignToTemporaryFile(const TUid aUid) const;
    IMPORT_C TInt SetInt(const TDesC& aKey, TInt aValue);
    IMPORT_C TInt SetString(const TDesC& aKey, const TDesC& aValue);
    IMPORT_C void Close();

private:
    TUint8 iSpare[256];
};

_LIT(KWd2sr, "sys.wd2sr");
_LIT(KSwsr, "sys.swsr");
_LIT(KStateSrc, "state.src");
_LIT(KStateSrcValue, "[100059c9]");
_LIT(KStateVal, "state.val");
_LIT(KSysState, "SysState");

const TInt KSysStateCategory = 0x10005943;
const TInt KSwStartupReasonNormal = 100;
const TInt KSwStateNormal = 203;

LOCAL_C TInt Publish(RSharedDataClient& aClient) {
    TInt err = aClient.AssignToTemporaryFile(TUid::Uid(KSysStateCategory));
    if (err != KErrNone) {
        return err;
    }

    aClient.SetInt(KWd2sr, 0);
    aClient.SetInt(KSwsr, KSwStartupReasonNormal);
    aClient.SetString(KStateSrc, KStateSrcValue);
    aClient.SetInt(KSysState, KSwStateNormal);
    return aClient.SetInt(KStateVal, KSwStateNormal);
}

// Query the real server instead of reconstructing its private window tree on the host.
// The endpoint exists only in ROM-wserv mode. Older/HLE hosts keep the original
// resident state-publisher behavior when Connect returns KErrNotFound.
class RWindowBridge : public RSessionBase {
public:
    TInt Connect() {
        _LIT(KBridge, "EKA2L1RomWindowBridge");
        return CreateSession(KBridge, TVersion(1, 0, 0), 1);
    }
    TInt Publish(const TDesC8& aSnapshot) {
        const TAny* args[4] = { &aSnapshot, 0, 0, 0 };
        return SendReceive(0, args);
    }
};

LOCAL_C void BridgeL(RWindowBridge& aBridge) {
    using namespace eka2l1_rom_bridge;
    RWsSession ws;
    User::LeaveIfError(ws.Connect());
    CleanupClosePushL(ws);
    CArrayFixFlat<TInt>* ids = new(ELeave) CArrayFixFlat<TInt>(8);
    CleanupStack::PushL(ids);
    snapshot* data = new(ELeave) snapshot;
    CleanupStack::PushL(data);
    TInt switchResult = KErrNone;
    FOREVER {
        ids->Reset();
        TInt err = ws.WindowGroupList(ids);
        if (err == KErrNone && ids->Count() <= max_groups) {
            data->protocol_version = version;
            data->count = ids->Count();
            data->focus = ws.GetFocusWindowGroup();
            data->switch_result = switchResult;
            for (TInt i = 0; i < ids->Count(); ++i) {
                group& row = data->groups[i];
                row.id = (*ids)[i];
                TThreadId thread;
                err = ws.GetWindowGroupClientThreadId(row.id, thread);
                if (err != KErrNone) break;
                row.thread = thread;
                TPtr name(row.name, 0, name_capacity);
                err = ws.GetWindowGroupNameFromIdentifier(row.id, name);
                if (err != KErrNone) break;
                row.name_length = name.Length();
            }
            if (err == KErrNone) {
                TPtrC8 bytes((const TUint8*)data, 16 + ids->Count() * sizeof(group));
                TInt target = aBridge.Publish(bytes);
                if (target > 0) {
                    switchResult = ws.SetWindowGroupOrdinalPosition(target, 0);
                    ws.Flush();
                    continue; // Publish the resulting focus before waiting again.
                }
                if (target < 0) User::Leave(target);
            }
        }
        User::After(250000);
    }
}

GLDEF_C TInt E32Main() {
    RSharedDataClient* client = new RSharedDataClient;
    if (!client) {
        return KErrNoMemory;
    }

    TInt err = client->Connect(0);
    if (err == KErrNone) {
        err = Publish(*client);
    }

    if (err != KErrNone) {
        client->Close();
        delete client;
        return err;
    }

    CTrapCleanup* cleanup = CTrapCleanup::New();
    if (cleanup) {
        RWindowBridge bridge;
        if (bridge.Connect() == KErrNone) {
            TRAPD(bridgeError, BridgeL(bridge));
            RDebug::Print(_L("SysState window bridge stopped: %d"), bridgeError);
            bridge.Close();
        }
        delete cleanup;
    }

    // Stay resident: the temporary category lives with this session. Nothing ever signals us.
    FOREVER {
        User::WaitForAnyRequest();
    }
}
