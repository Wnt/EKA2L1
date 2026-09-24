/*
 * SockTest - walk the Symbian OS 7.0s socket API one call at a time and paint
 * every call's return code, so the first call that fails - or never completes -
 * in an emulator is named on screen.  Target: Nokia 9300 / 9300i / 9500
 * (Series 80 v2, Symbian OS 7.0s, EKA1, ARMI).  Built with GCC 2.9-psion-98r2.
 *
 * Up to six paths run side by side, each in its own thread, so a call that hangs
 * in one path cannot hide another (the first two enabled paths are drawn):
 *   A  the Opera / HTTP-framework shape: explicit RConnection, Start(TCommDbConnPref
 *      IAP n, DoNotPrompt), RHostResolver + RSocket opened ON the connection,
 *      Write, RecvOneOrMore until EOF.
 *   B  the classic implicit shape: no RConnection, RHostResolver/RSocket opened
 *      on the session only, Send (no length), Read into a 12-byte buffer.
 *   C  the RConnection calls the HTTP framework / Opera's oprbridge make around the
 *      start: default Start() 0x43, Progress 0x46, ProgressNotification 0x47 (for
 *      KLinkLayerOpen and for any stage; a 3 s timer then cancels 0x48), the IAP
 *      GetIntSetting 0x4C / GetDesSetting 0x4F queries, EnumerateConnections 0x51.
 *   D  the same GET through the ROM's own HTTP framework (http.dll + its ECom
 *      protocol plug-in) on OUR started RConnection (session properties
 *      EHttpSocketServ / EHttpSocketConnection), in a thread with its own active
 *      scheduler; every THTTPEvent becomes a row with its time.
 *   E  as D, but the framework opens and starts its own connection (what Opera's
 *      trace shows: 0x3F + default Start 0x43 from inside the framework).
 *   F  as E, plus a self-completing priority-100 active object for 20 s right after
 *      SubmitL: it starves the session thread's scheduler, then lets go.
 * The main thread owns one full-screen window and repaints the step table every
 * 300 ms: "--" not reached, ".. 1.2s" in progress (a hang shows as a clock that
 * keeps counting), "=  0 OK 12ms" done, "skip" not attempted.
 *
 * Command line (all optional):  <host> <port> <iap> <paths>
 *   defaults 127.0.0.1 8931 1 ab ; <paths> is any of a, b, c, d, e, f (the first
 *   two enabled paths are drawn; all enabled paths run); add x to also show each
 *   finished worker's RThread::ExitReason() (workers return 0x5A00 + path index) -
 *   only on an emulator that implements EKA1 exec 0x3E (s80-socktest), else the
 *   call returns garbage.
 *
 * Copyright (c) 2026 Kernel Hive lab (agent N5).  MIT License:
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions: The above copyright notice and this
 * permission notice shall be included in all copies or substantial portions of the
 * Software.  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include <e32base.h>
#include <e32std.h>
#include <w32std.h>
#include <es_sock.h>
#include <in_sock.h>
#include <CommDbConnPref.h>
#include <e32svr.h>
#include <http.h>
#include <Uri8.h>

const TInt KMaxSteps = 12;
const TInt KStateNotRun = 0;
const TInt KStateRunning = 1;
const TInt KStateDone = 2;
const TInt KStateSkipped = 3;

class TStep
	{
public:
	TInt iState;
	TInt iCode;
	TTime iT0;
	TTime iT1;
	TBuf<40> iNote;
	TBuf<36> iLabel;	// overrides StepName() when set (HTTP event rows)
	};

class TWalkPath
	{
public:
	TInt iEnabled;
	TInt iCount;
	TInt iFinished;
	TStep iStep[KMaxSteps];
	TBuf8<700> iReply;
	};

class TShared
	{
public:
	TBuf<64> iHost;
	TInt iPort;
	TInt iIap;
	TInt iQueryReason;	// 'x' in <paths>: also ask RThread::ExitReason() (EKA1 exec 0x3E)
	TTime iStart;
	TWalkPath iP[6];	// A, B, C, D, E, F
	};

const TInt KPaths = 6;

// ---------------------------------------------------------------- step names

LOCAL_C const TDesC& StepName(TInt aPath, TInt aIdx)
	{
	_LIT(KA0, "A1 RSocketServ::Connect");
	_LIT(KA1, "A2 RConnection::Open 3F");
	_LIT(KA2, "A3 Start(IAP pref) 44");
	_LIT(KA3, "A4 RHostResolver::Open(c) 3E");
	_LIT(KA4, "A5 GetByName 29");
	_LIT(KA5, "A6 RSocket::Open(c) 3D");
	_LIT(KA6, "A7 RSocket::Connect 13");
	_LIT(KA7, "A8 RSocket::Write 0E");
	_LIT(KA8, "A9 RecvOneOrMore 0C");
	_LIT(KA9, "A10 Close all");
	_LIT(KB0, "B1 RSocketServ::Connect");
	_LIT(KB1, "B2 RHostResolver::Open 28");
	_LIT(KB2, "B3 GetByName 29");
	_LIT(KB3, "B4 RSocket::Open 06");
	_LIT(KB4, "B5 RSocket::Connect 13");
	_LIT(KB5, "B6 RSocket::Send 09");
	_LIT(KB6, "B7 RSocket::Read 0D");
	_LIT(KB7, "B8 Close all");
	_LIT(KC0, "C1 RSocketServ::Connect");
	_LIT(KC1, "C2 RConnection::Open 3F");
	_LIT(KC2, "C3 Start() default 43");
	_LIT(KC3, "C4 Progress 46");
	_LIT(KC4, "C5 ProgressNotif(7000) 47");
	_LIT(KC5, "C6 ProgressNotif(any) 47");
	_LIT(KC6, "C7 GetInt IAP\\Id 4C");
	_LIT(KC7, "C8 GetInt IAP\\IAPService 4C");
	_LIT(KC8, "C9 GetDes IAP\\IAPServiceType 4F");
	_LIT(KC9, "C10 GetDes IAP\\Name 4F");
	_LIT(KC10, "C11 EnumerateConnections 51");
	_LIT(KC11, "C12 Close 41");
	_LIT(KD0, "D1 RSocketServ::Connect");
	_LIT(KD1, "D2 RConnection::Open 3F");
	_LIT(KD2, "D3 Start(IAP pref) 44");
	_LIT(KD3, "D4 RHTTPSession::OpenL");
	_LIT(KD4, "D5 SetProperty(ss, conn)");
	_LIT(KD5, "D6 OpenTransactionL GET");
	_LIT(KD6, "D7 SubmitL");
	_LIT(KE0, "E1 RHTTPSession::OpenL");
	_LIT(KE1, "E2 OpenTransactionL GET");
	_LIT(KE2, "E3 SubmitL");
	_LIT(KF0, "F1 RHTTPSession::OpenL");
	_LIT(KF1, "F2 OpenTransactionL GET");
	_LIT(KF2, "F3 SubmitL");
	_LIT(KUnknown, "?");
	if (aPath == 3)
		{
		switch (aIdx)
			{
			case 0: return KD0;
			case 1: return KD1;
			case 2: return KD2;
			case 3: return KD3;
			case 4: return KD4;
			case 5: return KD5;
			case 6: return KD6;
			default: break;
			}
		return KUnknown;
		}
	if (aPath == 4 || aPath == 5)
		{
		switch (aIdx)
			{
			case 0: return (aPath == 4) ? KE0 : KF0;
			case 1: return (aPath == 4) ? KE1 : KF1;
			case 2: return (aPath == 4) ? KE2 : KF2;
			default: break;
			}
		return KUnknown;
		}
	if (aPath == 2)
		{
		switch (aIdx)
			{
			case 0: return KC0;
			case 1: return KC1;
			case 2: return KC2;
			case 3: return KC3;
			case 4: return KC4;
			case 5: return KC5;
			case 6: return KC6;
			case 7: return KC7;
			case 8: return KC8;
			case 9: return KC9;
			case 10: return KC10;
			case 11: return KC11;
			default: break;
			}
		return KUnknown;
		}
	if (aPath == 0)
		{
		switch (aIdx)
			{
			case 0: return KA0;
			case 1: return KA1;
			case 2: return KA2;
			case 3: return KA3;
			case 4: return KA4;
			case 5: return KA5;
			case 6: return KA6;
			case 7: return KA7;
			case 8: return KA8;
			case 9: return KA9;
			default: break;
			}
		}
	else
		{
		switch (aIdx)
			{
			case 0: return KB0;
			case 1: return KB1;
			case 2: return KB2;
			case 3: return KB3;
			case 4: return KB4;
			case 5: return KB5;
			case 6: return KB6;
			case 7: return KB7;
			default: break;
			}
		}
	return KUnknown;
	}

LOCAL_C const TDesC& ErrName(TInt aCode)
	{
	_LIT(KOk, "OK");
	_LIT(KNotFound, "NotFound");
	_LIT(KGeneral, "General");
	_LIT(KCancel, "Cancel");
	_LIT(KNoMemory, "NoMemory");
	_LIT(KNotSupported, "NotSupported");
	_LIT(KArgument, "Argument");
	_LIT(KBadHandle, "BadHandle");
	_LIT(KAlreadyExists, "Exists");
	_LIT(KInUse, "InUse");
	_LIT(KServerTerminated, "SrvTerminated");
	_LIT(KServerBusy, "SrvBusy");
	_LIT(KNotReady, "NotReady");
	_LIT(KUnknownErr, "Unknown");
	_LIT(KAccessDenied, "AccessDenied");
	_LIT(KEof, "Eof");
	_LIT(KTimedOut, "TimedOut");
	_LIT(KCouldNotConnect, "CouldNotConnect");
	_LIT(KDisconnected, "Disconnected");
	_LIT(KBadName, "BadName");
	_LIT(KNetUnreach, "NetUnreach");
	_LIT(KHostUnreach, "HostUnreach");
	_LIT(KDndNameNotFound, "DndNameNotFound");
	_LIT(KEmpty, "");
	switch (aCode)
		{
		case KErrNone: return KOk;
		case KErrNotFound: return KNotFound;
		case KErrGeneral: return KGeneral;
		case KErrCancel: return KCancel;
		case KErrNoMemory: return KNoMemory;
		case KErrNotSupported: return KNotSupported;
		case KErrArgument: return KArgument;
		case KErrBadHandle: return KBadHandle;
		case KErrAlreadyExists: return KAlreadyExists;
		case KErrInUse: return KInUse;
		case KErrServerTerminated: return KServerTerminated;
		case KErrServerBusy: return KServerBusy;
		case KErrNotReady: return KNotReady;
		case KErrUnknown: return KUnknownErr;
		case KErrAccessDenied: return KAccessDenied;
		case KErrEof: return KEof;
		case KErrTimedOut: return KTimedOut;
		case KErrCouldNotConnect: return KCouldNotConnect;
		case KErrDisconnected: return KDisconnected;
		case KErrBadName: return KBadName;
		case -190: return KNetUnreach;
		case -191: return KHostUnreach;
		case -5120: return KDndNameNotFound;
		default: break;
		}
	return KEmpty;
	}

// ---------------------------------------------------------------- step bookkeeping

LOCAL_C void Begin(TWalkPath& aPath, TInt aIdx)
	{
	TStep& s = aPath.iStep[aIdx];
	s.iT0.HomeTime();
	s.iState = KStateRunning;
	}

LOCAL_C void End(TWalkPath& aPath, TInt aIdx, TInt aCode)
	{
	TStep& s = aPath.iStep[aIdx];
	s.iT1.HomeTime();
	s.iCode = aCode;
	s.iState = KStateDone;
	}

LOCAL_C void Skip(TWalkPath& aPath, TInt aIdx)
	{
	aPath.iStep[aIdx].iState = KStateSkipped;
	}

LOCAL_C void AppendReply(TWalkPath& aPath, const TDesC8& aData)
	{
	TInt room = aPath.iReply.MaxLength() - aPath.iReply.Length();
	if (room > 0)
		{
		aPath.iReply.Append(aData.Left(room));
		}
	}

LOCAL_C void BuildRequest(TShared& aSh, TDes8& aReq)
	{
	aReq.Copy(_L8("GET / HTTP/1.0\r\nHost: "));
	TBuf8<64> host;
	host.Copy(aSh.iHost);
	aReq.Append(host);
	aReq.Append(_L8("\r\nUser-Agent: N5-SockTest/1.0 (Symbian OS 7.0s; Nokia 9300)\r\n\r\n"));
	}

// Resolve through aHr if it opened; fall back to the literal so the socket steps still run.
LOCAL_C TInt Resolve(TShared& aSh, TWalkPath& aPath, TInt aIdx, TInt aHrOpen, RHostResolver& aHr,
	TInetAddr& aAddr)
	{
	TInt have = 0;
	TRequestStatus st;
	TBuf<40> text;
	if (aHrOpen)
		{
		TNameEntry ne;
		Begin(aPath, aIdx);
		aHr.GetByName(aSh.iHost, ne, st);
		User::WaitForRequest(st);
		End(aPath, aIdx, st.Int());
		if (st.Int() == KErrNone)
			{
			aAddr = TInetAddr(ne().iAddr);
			aAddr.Output(text);
			aPath.iStep[aIdx].iNote.Copy(text.Left(20));
			TBuf<16> fam;
			fam.Format(_L(" fam %x"), aAddr.Family());
			aPath.iStep[aIdx].iNote.Append(fam);
			have = 1;
			}
		}
	else
		{
		Skip(aPath, aIdx);
		}
	if (!have)
		{
		if (aAddr.Input(aSh.iHost) == KErrNone)
			{
			have = 1;
			aPath.iStep[aIdx].iNote.Copy(_L("literal used"));
			}
		}
	aAddr.SetPort(aSh.iPort);
	return have;
	}

// ---------------------------------------------------------------- path A: Opera's shape

LOCAL_C TInt WorkerA(TAny* aPtr)
	{
	TShared& sh = *(TShared*)aPtr;
	TWalkPath& p = sh.iP[0];
	RSocketServ ss;
	RConnection conn;
	RHostResolver hr;
	RSocket sock;
	TRequestStatus st;
	TInt r;
	TInt i;
	TInt ssOk = 0;
	TInt connOk = 0;
	TInt hrOk = 0;
	TInt sockOk = 0;
	TInt connected = 0;
	TInt wrote = 0;
	TInetAddr addr;

	p.iCount = 10;

	Begin(p, 0);
	r = ss.Connect();
	End(p, 0, r);
	ssOk = (r == KErrNone);
	if (!ssOk)
		{
		for (i = 1; i < 10; i++)
			{
			Skip(p, i);
			}
		p.iFinished = 1;
		return 0;
		}

	Begin(p, 1);
	r = conn.Open(ss);
	End(p, 1, r);
	connOk = (r == KErrNone);

	if (connOk)
		{
		TCommDbConnPref pref;
		pref.SetIapId(sh.iIap);
		pref.SetDialogPreference(ECommDbDialogPrefDoNotPrompt);
		pref.SetDirection(ECommDbConnectionDirectionOutgoing);
		Begin(p, 2);
		conn.Start(pref, st);
		User::WaitForRequest(st);
		End(p, 2, st.Int());
		}
	else
		{
		Skip(p, 2);
		}

	if (connOk)
		{
		Begin(p, 3);
		r = hr.Open(ss, KAfInet, KProtocolInetUdp, conn);
		End(p, 3, r);
		hrOk = (r == KErrNone);
		}
	else
		{
		Skip(p, 3);
		}

	TInt haveAddr = Resolve(sh, p, 4, hrOk, hr, addr);

	if (connOk)
		{
		Begin(p, 5);
		r = sock.Open(ss, KAfInet, KSockStream, KProtocolInetTcp, conn);
		End(p, 5, r);
		sockOk = (r == KErrNone);
		}
	else
		{
		Skip(p, 5);
		}

	if (sockOk && haveAddr)
		{
		Begin(p, 6);
		sock.Connect(addr, st);
		User::WaitForRequest(st);
		End(p, 6, st.Int());
		connected = (st.Int() == KErrNone);
		}
	else
		{
		Skip(p, 6);
		}

	if (connected)
		{
		TBuf8<200> req;
		BuildRequest(sh, req);
		Begin(p, 7);
		sock.Write(req, st);
		User::WaitForRequest(st);
		End(p, 7, st.Int());
		p.iStep[7].iNote.Format(_L("%d bytes"), req.Length());
		wrote = (st.Int() == KErrNone);
		}
	else
		{
		Skip(p, 7);
		}

	if (wrote)
		{
		TBuf8<256> chunk;
		TSockXfrLength len;
		TInt rr = KErrNone;
		TInt total = 0;
		Begin(p, 8);
		for (i = 0; i < 64; i++)
			{
			chunk.Zero();
			sock.RecvOneOrMore(chunk, 0, st, len);
			User::WaitForRequest(st);
			rr = st.Int();
			if (chunk.Length() > 0)
				{
				AppendReply(p, chunk);
				total += chunk.Length();
				p.iStep[8].iNote.Format(_L("%d bytes, %d reads"), total, i + 1);
				}
			if (rr != KErrNone)
				{
				break;
				}
			}
		End(p, 8, rr);
		p.iStep[8].iNote.Format(_L("%d bytes, %d reads"), total, i + 1);
		}
	else
		{
		Skip(p, 8);
		}

	Begin(p, 9);
	sock.Close();
	hr.Close();
	conn.Close();
	ss.Close();
	End(p, 9, KErrNone);
	p.iFinished = 1;
	return 0x5A00;	// distinctive exit reason, read back by the 'x' option
	}

// ---------------------------------------------------------------- path B: implicit connection

LOCAL_C TInt WorkerB(TAny* aPtr)
	{
	TShared& sh = *(TShared*)aPtr;
	TWalkPath& p = sh.iP[1];
	RSocketServ ss;
	RHostResolver hr;
	RSocket sock;
	TRequestStatus st;
	TInt r;
	TInt i;
	TInt hrOk = 0;
	TInt sockOk = 0;
	TInt connected = 0;
	TInt sent = 0;
	TInetAddr addr;

	p.iCount = 8;

	Begin(p, 0);
	r = ss.Connect();
	End(p, 0, r);
	if (r != KErrNone)
		{
		for (i = 1; i < 8; i++)
			{
			Skip(p, i);
			}
		p.iFinished = 1;
		return 0;
		}

	Begin(p, 1);
	r = hr.Open(ss, KAfInet, KProtocolInetUdp);
	End(p, 1, r);
	hrOk = (r == KErrNone);

	TInt haveAddr = Resolve(sh, p, 2, hrOk, hr, addr);

	Begin(p, 3);
	r = sock.Open(ss, KAfInet, KSockStream, KProtocolInetTcp);
	End(p, 3, r);
	sockOk = (r == KErrNone);

	if (sockOk && haveAddr)
		{
		Begin(p, 4);
		sock.Connect(addr, st);
		User::WaitForRequest(st);
		End(p, 4, st.Int());
		connected = (st.Int() == KErrNone);
		}
	else
		{
		Skip(p, 4);
		}

	if (connected)
		{
		TBuf8<200> req;
		BuildRequest(sh, req);
		Begin(p, 5);
		sock.Send(req, 0, st);
		User::WaitForRequest(st);
		End(p, 5, st.Int());
		p.iStep[5].iNote.Format(_L("%d bytes"), req.Length());
		sent = (st.Int() == KErrNone);
		}
	else
		{
		Skip(p, 5);
		}

	if (sent)
		{
		TBuf8<12> head;
		Begin(p, 6);
		sock.Read(head, st);
		User::WaitForRequest(st);
		End(p, 6, st.Int());
		AppendReply(p, head);
		p.iStep[6].iNote.Format(_L("%d bytes"), head.Length());
		}
	else
		{
		Skip(p, 6);
		}

	Begin(p, 7);
	sock.Close();
	hr.Close();
	ss.Close();
	End(p, 7, KErrNone);
	p.iFinished = 1;
	return 0x5A01;
	}

// ---------------------------------------------------------------- path C: RConnection around Start

// Wait for aSt at most aMicros; returns 1 if it completed (aSt holds the code), 0 if still pending.
LOCAL_C TInt WaitAtMost(TRequestStatus& aSt, TInt aMicros)
	{
	RTimer timer;
	if (timer.CreateLocal() != KErrNone)
		{
		User::WaitForRequest(aSt);
		return 1;
		}
	TRequestStatus tst;
	timer.After(tst, aMicros);
	User::WaitForRequest(aSt, tst);
	TInt done = (aSt != KRequestPending);
	if (tst == KRequestPending)
		{
		timer.Cancel();
		User::WaitForRequest(tst);
		}
	timer.Close();
	return done;
	}

// ProgressNotification for aSelected (0 = any stage): completes -> code + stage; still pending after
// 3 s -> CancelProgressNotification (0x48) and record what the cancel returned.
LOCAL_C void Notif(TWalkPath& aPath, TInt aIdx, RConnection& aConn, TUint aSelected)
	{
	TNifProgressBuf prog;
	TRequestStatus st;
	Begin(aPath, aIdx);
	aConn.ProgressNotification(prog, st, aSelected);
	if (WaitAtMost(st, 3000000))
		{
		End(aPath, aIdx, st.Int());
		aPath.iStep[aIdx].iNote.Format(_L("stage %d err %d"), prog().iStage, prog().iError);
		}
	else
		{
		aPath.iStep[aIdx].iNote.Copy(_L("parked 3s, cancel"));
		aConn.CancelProgressNotification();
		User::WaitForRequest(st);
		End(aPath, aIdx, st.Int());
		aPath.iStep[aIdx].iNote.Copy(_L("parked 3s, cancelled"));
		}
	}

LOCAL_C TInt WorkerC(TAny* aPtr)
	{
	TShared& sh = *(TShared*)aPtr;
	TWalkPath& p = sh.iP[2];
	RSocketServ ss;
	RConnection conn;
	TRequestStatus st;
	TInt r;
	TInt i;

	p.iCount = 12;

	Begin(p, 0);
	r = ss.Connect();
	End(p, 0, r);
	if (r == KErrNone)
		{
		Begin(p, 1);
		r = conn.Open(ss);
		End(p, 1, r);
		}
	if (r != KErrNone)
		{
		for (i = 2; i < 12; i++)
			{
			Skip(p, i);
			}
		ss.Close();
		p.iFinished = 1;
		return 0;
		}

	Begin(p, 2);
	conn.Start(st);
	User::WaitForRequest(st);
	End(p, 2, st.Int());

	TNifProgress now;
	Begin(p, 3);
	r = conn.Progress(now);
	End(p, 3, r);
	p.iStep[3].iNote.Format(_L("stage %d err %d"), now.iStage, now.iError);

	Notif(p, 4, conn, KLinkLayerOpen);
	Notif(p, 5, conn, KConnProgressDefault);

	TUint32 v = 0;
	Begin(p, 6);
	r = conn.GetIntSetting(_L("IAP\\Id"), v);
	End(p, 6, r);
	p.iStep[6].iNote.Format(_L("= %u"), v);

	v = 0;
	Begin(p, 7);
	r = conn.GetIntSetting(_L("IAP\\IAPService"), v);
	End(p, 7, r);
	p.iStep[7].iNote.Format(_L("= %u"), v);

	TBuf<40> des;
	Begin(p, 8);
	r = conn.GetDesSetting(_L("IAP\\IAPServiceType"), des);
	End(p, 8, r);
	p.iStep[8].iNote.Copy(des.Left(20));

	des.Zero();
	Begin(p, 9);
	r = conn.GetDesSetting(_L("IAP\\Name"), des);
	End(p, 9, r);
	p.iStep[9].iNote.Copy(des.Left(20));

	TUint count = 0;
	Begin(p, 10);
	r = conn.EnumerateConnections(count);
	End(p, 10, r);
	p.iStep[10].iNote.Format(_L("count %u"), count);

	Begin(p, 11);
	conn.Close();
	ss.Close();
	End(p, 11, KErrNone);
	p.iFinished = 1;
	return 0x5A02;
	}

// ---------------------------------------------------------------- paths D/E/F: the ROM's HTTP framework

LOCAL_C TInt Ms(const TTime& aFrom, const TTime& aTo);

// Path F's stand-in for a UI thread whose redraw AO is always ready (W2's zero-area-window
// spin in Opera): a priority-100 AO that completes itself again on every RunL for
// aSeconds, so every lower-priority AO in the thread - the HTTP framework's included -
// is starved for exactly that long.
class CSpinner : public CActive
	{
public:
	CSpinner(TWalkPath& aPath, TInt aRow, TInt aSeconds);
	~CSpinner();
	void StartSpin();
private:
	void Kick();
	void RunL();
	void DoCancel();
private:
	TWalkPath& iPath;
	TInt iRow;
	TInt iSeconds;
	TInt iSpins;
	TTime iT0;
	};

CSpinner::CSpinner(TWalkPath& aPath, TInt aRow, TInt aSeconds)
	: CActive(100), iPath(aPath), iRow(aRow), iSeconds(aSeconds), iSpins(0)
	{
	CActiveScheduler::Add(this);
	}

CSpinner::~CSpinner()
	{
	Cancel();
	}

void CSpinner::StartSpin()
	{
	iT0.HomeTime();
	Begin(iPath, iRow);
	Kick();
	}

void CSpinner::Kick()
	{
	iStatus = KRequestPending;
	SetActive();
	TRequestStatus* s = &iStatus;
	User::RequestComplete(s, KErrNone);
	}

void CSpinner::RunL()
	{
	iSpins++;
	TTime now;
	now.HomeTime();
	if ((iSpins & 255) == 0)
		{
		iPath.iStep[iRow].iNote.Format(_L("%d spins"), iSpins);
		}
	if (Ms(iT0, now) < iSeconds * 1000)
		{
		Kick();
		return;
		}
	End(iPath, iRow, KErrNone);
	iPath.iStep[iRow].iNote.Format(_L("%d spins, stopped"), iSpins);
	RDebug::Print(_L("N5-SOCKTEST spinner stopped after %d spins"), iSpins);
	}

void CSpinner::DoCancel()
	{
	}

LOCAL_C const TDesC& EventName(TInt aCode)
	{
	_LIT(KSubmit, "Submit");
	_LIT(KCancel, "Cancel");
	_LIT(KClosed, "Closed");
	_LIT(KHeaders, "GotResponseHeaders");
	_LIT(KBody, "GotResponseBodyData");
	_LIT(KComplete, "ResponseComplete");
	_LIT(KTrailers, "GotTrailerHeaders");
	_LIT(KSucceeded, "Succeeded");
	_LIT(KFailed, "Failed");
	_LIT(KUnrecoverable, "UnrecoverableError");
	_LIT(KRequestComplete, "RequestComplete");
	_LIT(KError, "error");
	_LIT(KOther, "event");
	switch (aCode)
		{
		case THTTPEvent::ESubmit: return KSubmit;
		case THTTPEvent::ECancel: return KCancel;
		case THTTPEvent::EClosed: return KClosed;
		case THTTPEvent::EGotResponseHeaders: return KHeaders;
		case THTTPEvent::EGotResponseBodyData: return KBody;
		case THTTPEvent::EResponseComplete: return KComplete;
		case THTTPEvent::EGotResponseTrailerHeaders: return KTrailers;
		case THTTPEvent::ESucceeded: return KSucceeded;
		case THTTPEvent::EFailed: return KFailed;
		case THTTPEvent::EUnrecoverableError: return KUnrecoverable;
		case THTTPEvent::ERequestComplete: return KRequestComplete;
		default: break;
		}
	if (aCode < 0)
		{
		return KError;
		}
	return KOther;
	}

// Every THTTPEvent closes the open "next event" row (label = the event, code = iStatus,
// time = since the previous event) and opens the next one; body parts are coalesced.
class CHttpWalk : public CBase, public MHTTPTransactionCallback
	{
public:
	CHttpWalk(TWalkPath& aPath, TChar aLetter);
	void OpenWaitRow(TInt aRow);
	void MHFRunL(RHTTPTransaction aTransaction, const THTTPEvent& aEvent);
	TInt MHFRunError(TInt aError, RHTTPTransaction aTransaction, const THTTPEvent& aEvent);
public:
	TWalkPath& iPath;
	TChar iLetter;
	TInt iRow;
	TInt iBodyRow;
	TInt iBodyBytes;
	TInt iBodyParts;
	TInt iDone;
	};

CHttpWalk::CHttpWalk(TWalkPath& aPath, TChar aLetter)
	: iPath(aPath), iLetter(aLetter), iRow(0), iBodyRow(-1), iBodyBytes(0), iBodyParts(0), iDone(0)
	{
	}

void CHttpWalk::OpenWaitRow(TInt aRow)
	{
	iRow = aRow;
	if (iRow >= KMaxSteps)
		{
		iRow = KMaxSteps - 1;	// out of rows: keep reusing the last one
		}
	if (iRow + 1 > iPath.iCount)
		{
		iPath.iCount = iRow + 1;
		}
	iPath.iStep[iRow].iLabel.Format(_L("%c%d next THTTPEvent"), (TUint)iLetter, iRow + 1);
	iPath.iStep[iRow].iNote.Zero();
	Begin(iPath, iRow);
	}

void CHttpWalk::MHFRunL(RHTTPTransaction aTransaction, const THTTPEvent& aEvent)
	{
	TInt code = aEvent.iStatus;
	if (code == THTTPEvent::EGotResponseBodyData)
		{
		MHTTPDataSupplier* body = aTransaction.Response().Body();
		TPtrC8 data;
		if (body)
			{
			body->GetNextDataPart(data);
			AppendReply(iPath, data);
			iBodyBytes += data.Length();
			iBodyParts++;
			body->ReleaseData();
			}
		if (iBodyRow >= 0)
			{
			iPath.iStep[iBodyRow].iNote.Format(_L("%d bytes, %d parts"), iBodyBytes, iBodyParts);
			return;
			}
		}
	TInt r = iRow;
	TPtrC name(EventName(code));
	iPath.iStep[r].iLabel.Format(_L("%c%d evt %S"), (TUint)iLetter, r + 1, &name);
	End(iPath, r, code);
	if (code == THTTPEvent::EGotResponseHeaders)
		{
		iPath.iStep[r].iNote.Format(_L("HTTP %d"), aTransaction.Response().StatusCode());
		}
	else if (code == THTTPEvent::EGotResponseBodyData)
		{
		iBodyRow = r;
		iPath.iStep[r].iNote.Format(_L("%d bytes, %d parts"), iBodyBytes, iBodyParts);
		}
	if (code == THTTPEvent::ESucceeded || code == THTTPEvent::EFailed ||
		code == THTTPEvent::EUnrecoverableError)
		{
		iDone = 1;
		aTransaction.Close();
		CActiveScheduler::Stop();
		return;
		}
	OpenWaitRow(r + 1);
	}

TInt CHttpWalk::MHFRunError(TInt aError, RHTTPTransaction /*aTransaction*/, const THTTPEvent& aEvent)
	{
	iPath.iStep[iRow].iNote.Format(_L("RunError %d on evt %d"), aError, aEvent.iStatus);
	return KErrNone;
	}

LOCAL_C void SetConnPropsL(RHTTPSession& aSession, RSocketServ& aSs, RConnection& aConn)
	{
	RStringPool pool = aSession.StringPool();
	RHTTPConnectionInfo ci = aSession.ConnectionInfo();
	ci.SetPropertyL(pool.StringF(HTTP::EHttpSocketServ, RHTTPSession::GetTable()),
		THTTPHdrVal(aSs.Handle()));
	ci.SetPropertyL(pool.StringF(HTTP::EHttpSocketConnection, RHTTPSession::GetTable()),
		THTTPHdrVal((TInt)&aConn));
	}

LOCAL_C RHTTPTransaction OpenGetL(RHTTPSession& aSession, const TUriC8& aUri, CHttpWalk& aWalk)
	{
	RStringPool pool = aSession.StringPool();
	return aSession.OpenTransactionL(aUri, aWalk, pool.StringF(HTTP::EGET, RHTTPSession::GetTable()));
	}

LOCAL_C void HttpWalk(TShared& aSh, TWalkPath& p, TInt aOwnConnection, TChar aLetter,
	TInt aSpinSeconds)
	{
	CSpinner* spinner = NULL;
	RSocketServ ss;
	RConnection conn;
	RHTTPSession session;
	RHTTPTransaction trans;
	TRequestStatus st;
	TInt r = KErrNone;
	TInt row = 0;
	CHttpWalk* walk = NULL;

	if (aOwnConnection)
		{
		Begin(p, 0);
		r = ss.Connect();
		End(p, 0, r);
		if (r == KErrNone)
			{
			Begin(p, 1);
			r = conn.Open(ss);
			End(p, 1, r);
			}
		if (r == KErrNone)
			{
			TCommDbConnPref pref;
			pref.SetIapId(aSh.iIap);
			pref.SetDialogPreference(ECommDbDialogPrefDoNotPrompt);
			pref.SetDirection(ECommDbConnectionDirectionOutgoing);
			Begin(p, 2);
			conn.Start(pref, st);
			User::WaitForRequest(st);
			End(p, 2, st.Int());
			r = st.Int();
			}
		row = 3;
		}
	if (r == KErrNone)
		{
		Begin(p, row);
		TRAP(r, session.OpenL());
		End(p, row, r);
		row++;
		}
	if (r == KErrNone && aOwnConnection)
		{
		Begin(p, row);
		TRAP(r, SetConnPropsL(session, ss, conn));
		End(p, row, r);
		row++;
		}
	TBuf8<120> url;
	TBuf8<64> host8;
	host8.Copy(aSh.iHost);
	url.Format(_L8("http://%S:%d/"), &host8, aSh.iPort);
	TUriParser8 uri;
	if (r == KErrNone)
		{
		r = uri.Parse(url);
		}
	if (r == KErrNone)
		{
		walk = new CHttpWalk(p, aLetter);
		r = walk ? KErrNone : KErrNoMemory;
		}
	if (r == KErrNone)
		{
		Begin(p, row);
		TRAP(r, trans = OpenGetL(session, uri, *walk));
		End(p, row, r);
		row++;
		}
	if (r == KErrNone)
		{
		Begin(p, row);
		TRAP(r, trans.SubmitL());
		End(p, row, r);
		row++;
		}
	if (r == KErrNone)
		{
		if (aSpinSeconds > 0)
			{
			spinner = new CSpinner(p, row, aSpinSeconds);
			if (spinner)
				{
				p.iStep[row].iLabel.Format(_L("%c%d spinner AO pri 100 %ds"), (TUint)aLetter, row + 1,
					aSpinSeconds);
				RDebug::Print(_L("N5-SOCKTEST spinner starts after SubmitL"));
				spinner->StartSpin();
				row++;
				}
			}
		walk->OpenWaitRow(row);
		CActiveScheduler::Start();	// returns after Succeeded / Failed (CHttpWalk stops it)
		}
	delete spinner;
	delete walk;
	session.Close();
	conn.Close();
	ss.Close();
	}

LOCAL_C TInt WorkerHttp(TShared& aSh, TInt aIdx, TInt aOwnConnection, TChar aLetter,
	TInt aSpinSeconds)
	{
	CTrapCleanup* tc = CTrapCleanup::New();
	CActiveScheduler* as = new CActiveScheduler;
	if (tc && as)
		{
		CActiveScheduler::Install(as);
		HttpWalk(aSh, aSh.iP[aIdx], aOwnConnection, aLetter, aSpinSeconds);
		}
	delete as;
	delete tc;
	aSh.iP[aIdx].iFinished = 1;
	return 0x5A00 + aIdx;
	}

LOCAL_C TInt WorkerD(TAny* aPtr)
	{
	return WorkerHttp(*(TShared*)aPtr, 3, 1, 'D', 0);
	}

LOCAL_C TInt WorkerE(TAny* aPtr)
	{
	return WorkerHttp(*(TShared*)aPtr, 4, 0, 'E', 0);
	}

LOCAL_C TInt WorkerF(TAny* aPtr)
	{
	return WorkerHttp(*(TShared*)aPtr, 5, 0, 'F', 20);
	}

// ---------------------------------------------------------------- drawing

LOCAL_C TInt Ms(const TTime& aFrom, const TTime& aTo)
	{
	TTimeIntervalMicroSeconds d = aTo.MicroSecondsFrom(aFrom);
	TInt64 v = d.Int64();
	v /= TInt64(1000);
	return v.GetTInt();
	}

LOCAL_C void StepLine(const TStep& aStep, const TTime& aNow, TDes& aOut)
	{
	TInt ms;
	switch (aStep.iState)
		{
		case KStateRunning:
			ms = Ms(aStep.iT0, aNow);
			aOut.AppendFormat(_L(".. %d.%ds "), ms / 1000, (ms % 1000) / 100);
			aOut.Append(aStep.iNote);
			break;
		case KStateDone:
			ms = Ms(aStep.iT0, aStep.iT1);
			aOut.AppendFormat(_L("= %d "), aStep.iCode);
			aOut.Append(ErrName(aStep.iCode));
			aOut.AppendFormat(_L(" %dms "), ms);
			aOut.Append(aStep.iNote);
			break;
		case KStateSkipped:
			aOut.Append(_L("skip"));
			break;
		default:
			aOut.Append(_L("--"));
			break;
		}
	}

LOCAL_C TRgb StepColour(const TStep& aStep)
	{
	if (aStep.iState == KStateRunning)
		{
		return TRgb(0, 0, 200);
		}
	if (aStep.iState == KStateDone)
		{
		// >= 0: success, or a THTTPEvent code (4 headers, 5 body, 6 complete, 8 succeeded)
		if (aStep.iCode >= 0 || aStep.iCode == KErrEof)
			{
			return TRgb(0, 110, 0);
			}
		return TRgb(200, 0, 0);
		}
	return TRgb(110, 110, 110);
	}

// First line of the status + first body line, printable ASCII only.
LOCAL_C void ReplySummary(const TDesC8& aReply, TDes& aOut)
	{
	TInt i;
	TInt eol = aReply.Find(_L8("\r\n"));
	// Not "(eol >= 0) ? aReply.Left(eol) : aReply": GCC 2.9 gives that ?: the type TDesC8 and
	// slices the TPtrC8 temporary, so the copy reads a garbage iPtr (KERN-EXEC 3, runs n1a/n1b).
	TPtrC8 status(aReply);
	if (eol >= 0)
		{
		status.Set(aReply.Left(eol));
		}
	TInt hdrEnd = aReply.Find(_L8("\r\n\r\n"));
	TBuf8<160> tmp;
	tmp.Copy(status.Left(40));
	if (hdrEnd >= 0)
		{
		TPtrC8 body = aReply.Mid(hdrEnd + 4);
		tmp.Append(_L8(" | "));
		tmp.Append(body.Left(tmp.MaxLength() - tmp.Length()));
		}
	for (i = 0; i < tmp.Length(); i++)
		{
		if (tmp[i] < 0x20 || tmp[i] > 0x7e)
			{
			tmp[i] = ' ';
			}
		}
	TBuf<160> wide;
	wide.Copy(tmp);
	aOut.Append(wide.Left(aOut.MaxLength() - aOut.Length()));
	}

// A worker that panicked would otherwise look like a hang: say so in the reply row.
// Only ExitType() is asked: EKA2L1 (39858137e..92f600bf5) implements the EKA1 exec for it
// (0x3D) but not ExitReason (0x3E) or ExitCategory (0xC0003F) - calling those after a thread
// has exited returns garbage and the copy faults (KERN-EXEC 3, run n1a).
LOCAL_C void ThreadState(RThread* aThread, TWalkPath& aPath, TDes& aOut, TInt aQueryReason)
	{
	if (!aThread)
		{
		return;
		}
	TExitType t = aThread->ExitType();
	if (t == EExitPending)
		{
		return;
		}
	if (aQueryReason)
		{
		aOut.AppendFormat(_L("[exit type %d reason 0x%x] "), (TInt)t, aThread->ExitReason());
		}
	if (t == EExitKill && aPath.iFinished)
		{
		aOut.Append(_L("[done] "));
		return;
		}
	_LIT(KFinished, "finished");
	_LIT(KNotFinished, "NOT finished");
	const TDesC* walk = aPath.iFinished ? &KFinished : &KNotFinished;
	aOut.AppendFormat(_L("[thread exit type %d, walk %S] "), (TInt)t, walk);
	}

LOCAL_C void Draw(TShared& aSh, RWsSession& aWs, RWindow& aWin, CWindowGc& aGc, const CFont* aFont,
	TSize aSize, RThread* aThreads, TInt aStarted)
	{
	_LIT(KPathLetters, "ABCDEF");
	TTime now;
	now.HomeTime();
	TInt row = aFont->HeightInPixels() + 1;
	TInt asc = aFont->AscentInPixels();
	TInt y;
	TInt i;
	TInt col = 0;
	TInt pi;
	TInt maxRows = 0;
	TBuf<200> line;

	aWin.Invalidate();
	aWin.BeginRedraw();
	aGc.Activate(aWin);
	aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	aGc.SetBrushColor(TRgb(255, 255, 255));
	aGc.Clear();
	aGc.UseFont(aFont);

	line.Format(_L("N5 SockTest  %S:%d  IAP %d  t=%ds"), &aSh.iHost, aSh.iPort, aSh.iIap,
		Ms(aSh.iStart, now) / 1000);
	aGc.SetPenColor(TRgb(0, 0, 0));
	aGc.DrawText(line, TPoint(2, asc + 1));

	// the first two enabled paths get a column each
	TInt shown[2];
	TInt nShown = 0;
	for (pi = 0; pi < KPaths && nShown < 2; pi++)
		{
		if (aSh.iP[pi].iEnabled)
			{
			shown[nShown++] = pi;
			}
		}
	for (col = 0; col < nShown; col++)
		{
		pi = shown[col];
		TWalkPath& p = aSh.iP[pi];
		TInt x = (col == 0) ? 2 : (aSize.iWidth / 2 + 2);
		if (p.iCount > maxRows)
			{
			maxRows = p.iCount;
			}
		for (i = 0; i < p.iCount; i++)
			{
			y = asc + 1 + row * (i + 1);
			if (p.iStep[i].iLabel.Length() > 0)
				{
				line.Copy(p.iStep[i].iLabel);
				}
			else
				{
				line.Copy(StepName(pi, i));
				}
			line.Append(' ');
			StepLine(p.iStep[i], now, line);
			aGc.SetPenColor(StepColour(p.iStep[i]));
			// clip to the column so a long note never runs into the next one
			TInt fit = aFont->TextCount(line, aSize.iWidth / 2 - 4);
			aGc.DrawText(line.Left(fit), TPoint(x, y));
			}
		}

	aGc.SetPenColor(TRgb(0, 0, 0));
	y = asc + 1 + row * (maxRows + 1);
	for (col = 0; col < nShown; col++)
		{
		pi = shown[col];
		line.Zero();
		line.Append(KPathLetters()[pi]);
		line.Append(_L(": "));
		if (aStarted)
			{
			ThreadState(&aThreads[pi], aSh.iP[pi], line, aSh.iQueryReason);
			}
		ReplySummary(aSh.iP[pi].iReply, line);
		aGc.DrawText(line.Left(110), TPoint(2, y));
		y += row;
		}

	aGc.DiscardFont();
	aGc.Deactivate();
	aWin.EndRedraw();
	aWs.Flush();
	}

// ---------------------------------------------------------------- main

LOCAL_C void ParseCommandLine(TShared& aSh, TInt& aPaths)
	{
	RProcess me;
	TInt len = me.CommandLineLength();
	if (len <= 0 || len > 200)
		{
		return;
		}
	TBuf<200> cmd;
	me.CommandLine(cmd);
	TLex lex(cmd);
	TPtrC tok = lex.NextToken();
	if (tok.Length() > 0)
		{
		aSh.iHost.Copy(tok.Left(aSh.iHost.MaxLength()));
		}
	tok.Set(lex.NextToken());
	if (tok.Length() > 0)
		{
		TLex l(tok);
		l.Val(aSh.iPort);
		}
	tok.Set(lex.NextToken());
	if (tok.Length() > 0)
		{
		TLex l(tok);
		l.Val(aSh.iIap);
		}
	tok.Set(lex.NextToken());
	if (tok.Length() > 0)
		{
		aPaths = 0;
		if (tok.Locate('a') >= 0 || tok.Locate('A') >= 0)
			{
			aPaths |= 1;
			}
		if (tok.Locate('b') >= 0 || tok.Locate('B') >= 0)
			{
			aPaths |= 2;
			}
		if (tok.Locate('c') >= 0 || tok.Locate('C') >= 0)
			{
			aPaths |= 4;
			}
		if (tok.Locate('d') >= 0 || tok.Locate('D') >= 0)
			{
			aPaths |= 8;
			}
		if (tok.Locate('e') >= 0 || tok.Locate('E') >= 0)
			{
			aPaths |= 16;
			}
		if (tok.Locate('f') >= 0 || tok.Locate('F') >= 0)
			{
			aPaths |= 32;
			}
		if (tok.Locate('x') >= 0 || tok.Locate('X') >= 0)
			{
			aSh.iQueryReason = 1;
			}
		}
	}

LOCAL_C void MainL()
	{
	TShared* sh = new(ELeave) TShared;	// not CBase: members constructed, ints not zeroed
	CleanupStack::PushL(sh);
	TInt i;
	TInt pi;
	for (pi = 0; pi < KPaths; pi++)
		{
		for (i = 0; i < KMaxSteps; i++)
			{
			sh->iP[pi].iStep[i].iState = KStateNotRun;
			sh->iP[pi].iStep[i].iCode = 0;
			}
		sh->iP[pi].iFinished = 0;
		sh->iP[pi].iEnabled = 0;
		}
	sh->iP[0].iCount = 10;
	sh->iP[1].iCount = 8;
	sh->iP[2].iCount = 12;
	sh->iP[3].iCount = 8;	// grows as THTTPEvents arrive
	sh->iP[4].iCount = 4;
	sh->iP[5].iCount = 4;
	sh->iHost.Copy(_L("127.0.0.1"));
	sh->iPort = 8931;
	sh->iIap = 1;
	sh->iQueryReason = 0;
	sh->iStart.HomeTime();
	TInt paths = 3;
	ParseCommandLine(*sh, paths);
	for (pi = 0; pi < KPaths; pi++)
		{
		sh->iP[pi].iEnabled = (paths & (1 << pi)) ? 1 : 0;
		}

	RWsSession ws;
	User::LeaveIfError(ws.Connect());
	CleanupClosePushL(ws);
	CWsScreenDevice* screen = new(ELeave) CWsScreenDevice(ws);
	CleanupStack::PushL(screen);
	User::LeaveIfError(screen->Construct());
	CWindowGc* gc = NULL;
	User::LeaveIfError(screen->CreateContext(gc));
	CleanupStack::PushL(gc);
	RWindowGroup wg(ws);
	User::LeaveIfError(wg.Construct((TUint32)&wg, ETrue));
	CleanupClosePushL(wg);
	wg.SetName(_L("SockTest"));
	wg.SetOrdinalPosition(0);
	RWindow win(ws);
	User::LeaveIfError(win.Construct(wg, (TUint32)&win));
	CleanupClosePushL(win);
	TSize size = screen->SizeInPixels();
	win.SetExtent(TPoint(0, 0), size);
	win.SetBackgroundColor(TRgb(255, 255, 255));
	win.Activate();

	CFont* font = NULL;
	TFontSpec spec(_L("Nokia12"), 12);
	if (screen->GetNearestFontInPixels(font, spec) != KErrNone || !font)
		{
		TFontSpec twips(_L("Nokia12"), 120);
		User::LeaveIfError(screen->GetNearestFontInTwips(font, twips));
		}

	RThread threads[KPaths];
	Draw(*sh, ws, win, *gc, font, size, threads, 0);

	_LIT(KNameA, "SockTestA");
	_LIT(KNameB, "SockTestB");
	_LIT(KNameC, "SockTestC");
	_LIT(KNameD, "SockTestD");
	_LIT(KNameE, "SockTestE");
	_LIT(KNameF, "SockTestF");
	for (pi = 0; pi < KPaths; pi++)
		{
		if (!sh->iP[pi].iEnabled)
			{
			continue;
			}
		TPtrC name(KNameA);
		TThreadFunction fn = WorkerA;
		if (pi == 1)
			{
			name.Set(KNameB);
			fn = WorkerB;
			}
		else if (pi == 2)
			{
			name.Set(KNameC);
			fn = WorkerC;
			}
		else if (pi == 3)
			{
			name.Set(KNameD);
			fn = WorkerD;
			}
		else if (pi == 4)
			{
			name.Set(KNameE);
			fn = WorkerE;
			}
		else if (pi == 5)
			{
			name.Set(KNameF);
			fn = WorkerF;
			}
		// the HTTP framework paths get a bigger stack
		User::LeaveIfError(threads[pi].Create(name, fn, (pi >= 3) ? 0x14000 : 0x8000,
			&User::Heap(), sh));
		threads[pi].Resume();
		}

	// Repaint for up to an hour; the process then exits (which closes the emulator
	// when it was started with --run).
	for (i = 0; i < 12000; i++)
		{
		Draw(*sh, ws, win, *gc, font, size, threads, 1);
		User::After(300000);
		}

	screen->ReleaseFont(font);
	CleanupStack::PopAndDestroy(6); // win, wg, gc, screen, ws, sh
	}

GLDEF_C TInt E32Main()
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	TRAPD(err, MainL());
	delete cleanup;
	return err;
	}
