// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
// An ARM window client: shows what ROM wserv delivers, without Eikon's input policy.
#include <e32base.h>
#include <e32svr.h>
#include <w32std.h>
#include <fbs.h>

LOCAL_C void RunL() {
    RWsSession ws;
    User::LeaveIfError(ws.Connect());
    CWsScreenDevice* screen = new(ELeave) CWsScreenDevice(ws);
    User::LeaveIfError(screen->Construct());
    CWindowGc* gc = 0;
    User::LeaveIfError(screen->CreateContext(gc));
    RWindowGroup group(ws);
    User::LeaveIfError(group.Construct(1, ETrue));
    group.SetOrdinalPosition(0, 100);
    RWindow window(ws);
    User::LeaveIfError(window.Construct(group, 2));
    window.SetExtent(TPoint(0,0), TSize(640,200));
    window.SetVisible(ETrue);
    window.Activate();
    CFont* font = 0;
    User::LeaveIfError(screen->GetNearestFontInPixels(font, TFontSpec(_L("SwissA"),18)));
    TBuf<120> line(_L("Click here: waiting for pointer"));
    TInt count = 0;
    TRequestStatus eventStatus, redrawStatus;
    ws.EventReady(&eventStatus);
    ws.RedrawReady(&redrawStatus);
    ws.Flush();
    FOREVER {
        User::WaitForRequest(eventStatus, redrawStatus);
        if (eventStatus != KRequestPending) {
            User::LeaveIfError(eventStatus.Int());
            TWsEvent event;
            ws.GetEvent(event);
            if (event.Type() == EEventPointer) {
                TPointerEvent* pointer = event.Pointer();
                if (pointer->iType == TPointerEvent::EButton1Down) ++count;
                line.Format(_L("Pointer clicks=%d type=%d x=%d y=%d"), count,
                    pointer->iType, pointer->iPosition.iX, pointer->iPosition.iY);
                RDebug::Print(line);
                window.Invalidate();
            }
            ws.EventReady(&eventStatus);
        }
        if (redrawStatus != KRequestPending) {
            User::LeaveIfError(redrawStatus.Int());
            TWsRedrawEvent redraw;
            ws.GetRedraw(redraw);
            gc->Activate(window);
            window.BeginRedraw();
            gc->SetBrushColor(count ? KRgbYellow : KRgbWhite);
            gc->Clear();
            gc->UseFont(font);
            gc->SetPenColor(KRgbBlack);
            gc->DrawText(_L("ROM wserv ARM pointer client"), TPoint(12,35));
            gc->DrawText(line, TPoint(12,90));
            gc->DiscardFont();
            window.EndRedraw();
            gc->Deactivate();
            ws.RedrawReady(&redrawStatus);
        }
        ws.Flush();
    }
}
GLDEF_C TInt E32Main() {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    TRAPD(err, RunL());
    RDebug::Print(_L("INPUTPROBE exit %d"), err);
    delete cleanup;
    return err;
}
