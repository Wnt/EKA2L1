/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Symbian OS 7.0s (Series 80 v2: Nokia 9300, 9300i, 9500) variant of the host access point patch.
//
// 7.0s CommDB differs from 6.1 in three ways that matter here:
// - The IAP record names a service AND a bearer (IAPBearer/IAPBearerType); the 6.1 Modem table and the IAP
//   Modem column are gone. The emulator's socket server is the host network, so the IAP is a LAN one:
//   LANService + LANBearer, the record set the Series 80 SDK emulator uses for its own Ethernet access point.
// - The ROM's default CommDB (Z:\System\Data\DefaultCdbv3.dat) is not empty. The 9300's has LAN bearers (WLAN,
//   RNDIS), a GPRS modem bearer, a dial-out ISP, the "Internet" network and the "Mobile" location, but no IAP and
//   no connection preference. The 9300i's and 9500's also have WLAN IAPs. The patch therefore looks for its own
//   IAP by name, not for an empty IAP table. On a 9300 with a fresh C: drive it gets IAP 1 and LAN service 1.
// - NewL(TCommDbDatabaseType) is a BC wrapper that calls NewL(). The 9300's own clients call either (its secure
//   socket library calls NewL(EDatabaseTypeUnspecified)), so the patch replaces NewL(). It opens the database
//   itself with NewL(TCommDbOpeningMethod&), which stays native.
//
// After the first open, the IAP "Host network" is ranked first for outgoing connections and set not to prompt,
// so the ROM's own access point lists, Opera and third-party IAP pickers all see the same access point that the
// emulator's socket server provides.

#include <commdb.h>
#include <cdbpreftable.h>

_LIT(KHostName, "Host network");
_LIT(KHostAgent, "NullAgt");
_LIT(KHostNif, "ethint");
_LIT(KNotUsed, "not used");
_LIT(KHostNetworks, "ip");
_LIT(KZeroAddress, "0.0.0.0");
_LIT(KHttp, "http");
_LIT(KEmpty, "");

static const TUint32 KHostBearerSet = KCommDbBearerCSD | KCommDbBearerWcdma | KCommDbBearerLAN | KCommDbBearerVirtual;

static void Rollback(TAny* aDatabase) {
    static_cast<CCommsDatabase*>(aDatabase)->RollbackTransaction();
}

// Id of the first record in aTable whose Name is "Host network", or 0.
static TUint32 FindHostRecordL(CCommsDatabase& aDb, const TDesC& aTable) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenViewMatchingTextLC(aTable, TPtrC(COMMDB_NAME), KHostName);
    const TInt err = view->GotoFirstRecord();
    if (err == KErrNone) {
        view->ReadUintL(TPtrC(COMMDB_ID), id);
    }
    CleanupStack::PopAndDestroy(view);
    User::LeaveIfError(err == KErrNotFound ? KErrNone : err);
    return id;
}

// Id of the first record in aTable, or 0 when the table is empty.
static TUint32 FirstRecordL(CCommsDatabase& aDb, const TDesC& aTable) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(aTable);
    const TInt err = view->GotoFirstRecord();
    if (err == KErrNone) {
        view->ReadUintL(TPtrC(COMMDB_ID), id);
    }
    CleanupStack::PopAndDestroy(view);
    User::LeaveIfError(err == KErrNotFound ? KErrNone : err);
    return id;
}

static TUint32 InsertNetworkL(CCommsDatabase& aDb) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(NETWORK));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteTextL(TPtrC(COMMDB_NAME), KHostName);
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
    return id;
}

static TUint32 InsertLocationL(CCommsDatabase& aDb) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(LOCATION));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteTextL(TPtrC(COMMDB_NAME), KHostName);
    view->WriteBoolL(TPtrC(LOCATION_MOBILE), ETrue);
    view->WriteBoolL(TPtrC(LOCATION_USE_PULSE_DIAL), EFalse);
    view->WriteBoolL(TPtrC(LOCATION_WAIT_FOR_DIAL_TONE), EFalse);
    view->WriteUintL(TPtrC(LOCATION_PAUSE_AFTER_DIAL_OUT), 0);
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
    return id;
}

static TUint32 InsertLanServiceL(CCommsDatabase& aDb) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(LAN_SERVICE));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteTextL(TPtrC(COMMDB_NAME), KHostName);
    view->WriteTextL(TPtrC(LAN_IF_NETWORKS), KHostNetworks);
    view->WriteTextL(TPtrC(LAN_IP_NETMASK), KZeroAddress);
    view->WriteTextL(TPtrC(LAN_IP_GATEWAY), KZeroAddress);
    view->WriteBoolL(TPtrC(LAN_IP_ADDR_FROM_SERVER), ETrue);
    view->WriteTextL(TPtrC(LAN_IP_ADDR), KZeroAddress);
    view->WriteBoolL(TPtrC(LAN_IP_DNS_ADDR_FROM_SERVER), ETrue);
    view->WriteTextL(TPtrC(LAN_IP_NAME_SERVER1), KZeroAddress);
    view->WriteTextL(TPtrC(LAN_IP_NAME_SERVER2), KZeroAddress);
    view->WriteBoolL(TPtrC(LAN_IP6_DNS_ADDR_FROM_SERVER), EFalse);
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
    return id;
}

static TUint32 InsertLanBearerL(CCommsDatabase& aDb) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(LAN_BEARER));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteTextL(TPtrC(COMMDB_NAME), KHostName);
    view->WriteTextL(TPtrC(LAN_BEARER_AGENT), KHostAgent);
    view->WriteTextL(TPtrC(LAN_BEARER_NIF_NAME), KHostNif);
    view->WriteTextL(TPtrC(LAN_BEARER_LDD_NAME), KNotUsed);
    view->WriteTextL(TPtrC(LAN_BEARER_PDD_NAME), KNotUsed);
    view->WriteUintL(TPtrC(LAST_SOCKET_ACTIVITY_TIMEOUT), static_cast<TUint32>(KMaxTUint32));
    view->WriteUintL(TPtrC(LAST_SESSION_CLOSED_TIMEOUT), 3);
    view->WriteUintL(TPtrC(LAST_SOCKET_CLOSED_TIMEOUT), static_cast<TUint32>(KMaxTUint32));
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
    return id;
}

static TUint32 InsertIapL(CCommsDatabase& aDb, TUint32 aService, TUint32 aBearer, TUint32 aNetwork, TUint32 aLocation) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(IAP));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteTextL(TPtrC(COMMDB_NAME), KHostName);
    view->WriteUintL(TPtrC(IAP_SERVICE), aService);
    view->WriteTextL(TPtrC(IAP_SERVICE_TYPE), TPtrC(LAN_SERVICE));
    view->WriteUintL(TPtrC(IAP_BEARER), aBearer);
    view->WriteTextL(TPtrC(IAP_BEARER_TYPE), TPtrC(LAN_BEARER));
    view->WriteUintL(TPtrC(IAP_NETWORK), aNetwork);
    view->WriteUintL(TPtrC(IAP_NETWORK_WEIGHTING), 0);
    view->WriteUintL(TPtrC(IAP_LOCATION), aLocation);
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
    return id;
}

// "No proxy" for the host network's HTTP, spelled out the way the SDK emulator's CommDB does it, so a browser that
// opens the proxy view for this service finds a record that says not to use one.
static void InsertNoProxyL(CCommsDatabase& aDb, TUint32 aService) {
    TUint32 id = 0;
    CCommsDbTableView* view = aDb.OpenTableLC(TPtrC(PROXIES));
    User::LeaveIfError(view->InsertRecord(id));
    view->WriteUintL(TPtrC(PROXY_ISP), aService);
    view->WriteTextL(TPtrC(PROXY_SERVICE_TYPE), TPtrC(LAN_SERVICE));
    view->WriteBoolL(TPtrC(PROXY_USE_PROXY_SERVER), EFalse);
    view->WriteTextL(TPtrC(PROXY_PROTOCOL_NAME), KHttp);
    view->WriteLongTextL(TPtrC(PROXY_SERVER_NAME), KEmpty);
    view->WriteUintL(TPtrC(PROXY_PORT_NUMBER), 80);
    view->WriteLongTextL(TPtrC(PROXY_EXCEPTIONS), KEmpty);
    User::LeaveIfError(view->PutRecordChanges());
    CleanupStack::PopAndDestroy(view);
}

// Rank the host IAP first for outgoing connections and do not prompt. An existing first choice is pointed at the
// host IAP rather than kept, so a connection that asks CommDB for its default always gets the emulator's network.
static void PreferHostAccessPointL(CCommsDatabase& aDb, TUint32 aIap) {
    CCommsDbConnectionPrefTableView::TCommDbIapBearer bearer;
    bearer.iBearerSet = KHostBearerSet;
    bearer.iIapId = aIap;

    CCommsDbConnectionPrefTableView* view = aDb.OpenConnectionPrefTableViewOnRankLC(ECommDbConnectionDirectionOutgoing, 1);
    const TInt err = view->GotoFirstRecord();
    if (err == KErrNone) {
        view->UpdateBearerL(bearer);
        view->UpdateDialogPrefL(ECommDbDialogPrefDoNotPrompt);
        CleanupStack::PopAndDestroy(view);
        return;
    }
    User::LeaveIfError(err == KErrNotFound ? KErrNone : err);
    CleanupStack::PopAndDestroy(view);

    CCommsDbConnectionPrefTableView::TCommDbIapConnectionPref pref;
    pref.iRanking = 1;
    pref.iDirection = ECommDbConnectionDirectionOutgoing;
    pref.iDialogPref = ECommDbDialogPrefDoNotPrompt;
    pref.iBearer = bearer;
    view = aDb.OpenConnectionPrefTableLC(ECommDbConnectionDirectionOutgoing);
    view->InsertConnectionPreferenceL(pref);
    CleanupStack::PopAndDestroy(view);
}

static void EnsureHostAccessPointL(CCommsDatabase& aDb) {
    if (FindHostRecordL(aDb, TPtrC(IAP))) {
        return;
    }

    User::LeaveIfError(aDb.BeginTransaction());
    CleanupStack::PushL(TCleanupItem(Rollback, &aDb));

    // Another client may have added it between the check and the write lock.
    if (FindHostRecordL(aDb, TPtrC(IAP))) {
        CleanupStack::PopAndDestroy();
        return;
    }

    TUint32 network = FirstRecordL(aDb, TPtrC(NETWORK));
    if (!network) {
        network = InsertNetworkL(aDb);
    }
    TUint32 location = FirstRecordL(aDb, TPtrC(LOCATION));
    if (!location) {
        location = InsertLocationL(aDb);
    }
    const TUint32 service = InsertLanServiceL(aDb);
    const TUint32 bearer = InsertLanBearerL(aDb);
    const TUint32 iap = InsertIapL(aDb, service, bearer, network, location);
    InsertNoProxyL(aDb, service);

    User::LeaveIfError(aDb.CommitTransaction());
    CleanupStack::Pop();

    // The preference is validated against the committed IAP; losing it must not lose the IAP.
    User::LeaveIfError(aDb.BeginTransaction());
    CleanupStack::PushL(TCleanupItem(Rollback, &aDb));
    PreferHostAccessPointL(aDb, iap);
    User::LeaveIfError(aDb.CommitTransaction());
    CleanupStack::Pop();
}

// Replaces CCommsDatabase::NewL() (commdb.dll ordinal 153 on 7.0s). NewL(TCommDbDatabaseType), ordinal 27, calls it.
extern "C" EXPORT_C CCommsDatabase* HostCommsDatabaseNewL() {
    TCommDbOpeningMethod openingMethod;
    CCommsDatabase* db = CCommsDatabase::NewL(openingMethod);
    CleanupStack::PushL(db);
    TRAPD(err, EnsureHostAccessPointL(*db));
    (void)err; // 7.0s has no TRAP_IGNORE; a CommDB that cannot take the record is still returned to the client.
    CleanupStack::Pop(db);
    return db;
}

GLDEF_C TInt E32Dll(TDllReason) {
    return KErrNone;
}
