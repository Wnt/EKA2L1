// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
// ARM-side FBS/BitGDI contract probe. It paints directly to the screen; it is not a Desk proof.
#include <e32base.h>
#include <e32std.h>
#include <e32svr.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>

LOCAL_C void RunL() {
    User::LeaveIfError(RFbsSession::Connect());
    RDebug::Print(_L("FBSPROBE connected"));
    CFbsScreenDevice* screen = CFbsScreenDevice::NewL(_L(""), EColor64K);
    CFbsBitGc* gc = 0;
    User::LeaveIfError(screen->CreateContext(gc));
    gc->SetBrushColor(KRgbWhite);
    gc->Clear();
    const TPtrC names[] = {_L("System"), _L("SwissA"), _L("SwissA")};
    for (TInt i=0; i<3; ++i) {
        TFontSpec spec(names[i], 18);
        if (i==2) spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
        CFont* font = 0;
        User::LeaveIfError(screen->GetNearestFontInPixels(font, spec));
        RDebug::Print(_L("FBSPROBE font %d height=%d ascent=%d width=%d"), i,
            font->HeightInPixels(), font->AscentInPixels(), font->TextWidthInPixels(_L("Hello museum 9300")));
        gc->UseFont(font);
        gc->SetPenColor(KRgbBlack);
        gc->DrawText(_L("Hello museum 9300 =A1+A2"), TPoint(12, 28+i*30));
        gc->DiscardFont();
        screen->ReleaseFont(font);
    }
    CFbsBitmap bitmap, mask, duplicate;
    User::LeaveIfError(bitmap.Create(TSize(64,32), EColor64K));
    User::LeaveIfError(mask.Create(TSize(64,32), EGray2));
    CFbsBitmapDevice* device = CFbsBitmapDevice::NewL(&bitmap);
    CFbsBitGc* bgc = 0;
    User::LeaveIfError(device->CreateContext(bgc));
    bgc->SetBrushColor(KRgbBlue); bgc->Clear();
    bgc->SetBrushColor(KRgbYellow); bgc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    bgc->DrawRect(TRect(8,8,56,24));
    delete bgc; delete device;
    device = CFbsBitmapDevice::NewL(&mask);
    User::LeaveIfError(device->CreateContext(bgc));
    bgc->SetBrushColor(KRgbBlack); bgc->Clear();
    bgc->SetBrushColor(KRgbWhite); bgc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    bgc->DrawRect(TRect(16,0,48,32));
    delete bgc; delete device;
    User::LeaveIfError(duplicate.Duplicate(bitmap.Handle()));
    bitmap.Reset(); // The duplicate must retain the storage after the original handle closes.
    gc->BitBlt(TPoint(12,112), &duplicate);
    gc->BitBltMasked(TPoint(88,112), &duplicate, TRect(0,0,64,32), &mask, EFalse);
    TInt compressed = duplicate.Compress();
    RDebug::Print(_L("FBSPROBE duplicate survived close; compress=%d"), compressed);
    gc->BitBlt(TPoint(164,112), &duplicate);
    gc->SetDrawMode(CGraphicsContext::EDrawModeXOR);
    gc->SetBrushColor(KRgbWhite); gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc->DrawRect(TRect(240,112,304,144)); gc->DrawRect(TRect(240,112,304,144));
    gc->SetDrawMode(CGraphicsContext::EDrawModePEN);
    CFbsBitmap rom;
    TInt loaded = rom.Load(_L("Z:\\system\\data\\splashscreen.mbm"), 0);
    RDebug::Print(_L("FBSPROBE ROM MBM load=%d size=%dx%d"), loaded, rom.SizeInPixels().iWidth, rom.SizeInPixels().iHeight);
    if (loaded==KErrNone) gc->DrawBitmap(TRect(360,100,520,180), &rom);
    screen->Update();
    RDebug::Print(_L("FBSPROBE painted"));
    FOREVER { User::WaitForAnyRequest(); }
}
GLDEF_C TInt E32Main() {
    CTrapCleanup* cleanup=CTrapCleanup::New();
    TRAPD(err, RunL());
    RDebug::Print(_L("FBSPROBE exit=%d"), err);
    delete cleanup;
    return err;
}
