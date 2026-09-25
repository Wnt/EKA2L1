// Copyright (c) 2026 EKA2L1 Team. SPDX-License-Identifier: GPL-3.0-or-later
// ARM-side FBS/BitGDI contract probe. Optional "window" argument exercises Wserv sharing.
#include <e32base.h>
#include <e32std.h>
#include <e32svr.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>
#include <w32std.h>
#include <f32file.h>
#include <openfont.h>

LOCAL_C void ConnectFbsL() {
    TInt err = RFbsSession::Connect();
    if (err == KErrNotFound) {
        // The emulator prestarts ROM FBS. Its heap exists before its public
        // server does; synchronize with that process instead of spawning a peer.
        TFindProcess finder(_L("*fbserv*"));
        TFullName name;
        User::LeaveIfError(finder.Next(name));
        RProcess process;
        User::LeaveIfError(process.Open(name));
        TRequestStatus ready;
        process.Rendezvous(ready);
        err = RFbsSession::Connect(); // Close the ready-before-arm race.
        if (err == KErrNone) process.RendezvousCancel(ready);
        User::WaitForRequest(ready);
        process.Close();
        if (err != KErrNone) {
            User::LeaveIfError(ready.Int());
            err = RFbsSession::Connect();
        }
    }
    User::LeaveIfError(err);
}

LOCAL_C void RunL() {
    RProcess self, opened;
    User::LeaveIfError(opened.Open(self.FullName(), EOwnerThread));
    if (opened.Id() != self.Id()) User::Leave(KErrCorrupt);
    opened.Close();
    if (opened.Open(_L("Z4_NoSuchProcess")) != KErrNotFound) User::Leave(KErrCorrupt);
    RDebug::Print(_L("FBSPROBE process open by name PASS"));
    TBuf<32> arguments;
    self.CommandLine(arguments);
    const TBool antialiased = arguments.FindF(_L("aa")) >= 0;
    ConnectFbsL();
    RDebug::Print(_L("FBSPROBE connected"));
    RWsSession ws;
    User::LeaveIfError(ws.Connect());
    ws.Flush(); // Let Wserv initialize its screen before the direct-screen probe.
    CFbsScreenDevice* screen = CFbsScreenDevice::NewL(_L(""), EColor64K);
    CFbsTypefaceStore* store = CFbsTypefaceStore::NewL(screen);
    const TGlyphBitmapType defaultBitmapType = store->DefaultBitmapType();
    RDebug::Print(_L("FBSPROBE font store default bitmap=%d"), defaultBitmapType);
    if (defaultBitmapType != EMonochromeGlyphBitmap && defaultBitmapType != EAntiAliasedGlyphBitmap)
        User::Leave(KErrCorrupt);
    RDebug::Print(_L("FBSPROBE default bitmap enum contract PASS"));
    delete store;
    CFbsBitmap canvas;
    User::LeaveIfError(canvas.Create(TSize(640,200), EColor64K));
    const TSize initialTwips = canvas.SizeInTwips();
    RDebug::Print(_L("FBSPROBE initial canvas twips=%dx%d"), initialTwips.iWidth, initialTwips.iHeight);
    // DrawBitmap(TPoint) uses the bitmap's physical size. ROM Create leaves
    // twips zero; BitBlt (the direct probe) uses pixels and hid this error.
    canvas.SetSizeInTwips(TSize(screen->HorizontalPixelsToTwips(640), screen->VerticalPixelsToTwips(200)));
    CFbsBitmapDevice* canvasDevice = CFbsBitmapDevice::NewL(&canvas);
    CFbsBitGc* gc = 0;
    User::LeaveIfError(canvasDevice->CreateContext(gc));
    gc->SetBrushStyle(CGraphicsContext::ESolidBrush);
    gc->SetBrushColor(KRgbWhite);
    gc->Clear();
    const TPtrC names[] = {_L("System"), _L("SwissA"), _L("SwissA")};
    for (TInt i=0; i<3; ++i) {
        TFontSpec spec(names[i], 18);
        if (i==2) spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
        if (antialiased) spec.iFontStyle.SetBitmapType(EAntiAliasedGlyphBitmap);
        RDebug::Print(_L("FBSPROBE requested bitmap=%d"), spec.iFontStyle.BitmapType());
        CFont* font = 0;
        User::LeaveIfError(screen->GetNearestFontInPixels(font, spec));
        RDebug::Print(_L("FBSPROBE font %d height=%d ascent=%d width=%d"), i,
            font->HeightInPixels(), font->AscentInPixels(), font->TextWidthInPixels(_L("Hello museum 9300")));
        TOpenFontMetrics metrics;
        if (static_cast<CFbsFont*>(font)->GetFontMetrics(metrics))
            RDebug::Print(_L("FBSPROBE metrics size=%d ascent=%d descent=%d maxHeight=%d maxDepth=%d"),
                metrics.Size(), metrics.Ascent(), metrics.Descent(), metrics.MaxHeight(), metrics.MaxDepth());
        TFontSpec actual = font->FontSpecInTwips();
        RDebug::Print(_L("FBSPROBE actual %S heightTwips=%d bold=%d bitmap=%d"),
            &actual.iTypeface.iName, actual.iHeight, actual.iFontStyle.StrokeWeight(), actual.iFontStyle.BitmapType());
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
    RFs files;
    User::LeaveIfError(files.Connect());
    TUint8* address = files.IsFileInRom(_L("Z:\\System\\Apps\\desk\\desk.aif"));
    RFile file;
    User::LeaveIfError(file.Open(files, _L("Z:\\System\\Apps\\desk\\desk.aif"), EFileRead));
    TInt seek = 0x4c;
    TInt seekResult = file.Seek(ESeekAddress, seek);
    file.Close();
    TBool inRom = EFalse;
    User::IsRomAddress(inRom, address);
    RDebug::Print(_L("FBSPROBE file ROM=%x seekResult=%d seek=%x UserIsRom=%d"), address, seekResult, seek, inRom);
    if (address && (seekResult != KErrNone || (TUint)seek != (TUint)address + 0x4c || !inRom)) User::Leave(KErrCorrupt);
    files.Close();
    CFbsBitmap extracted;
    TInt extractedResult = extracted.Load(_L("Z:\\System\\Apps\\desk\\desk.aif"), 2, ETrue, 0x4c);
    RDebug::Print(_L("FBSPROBE extracted ROM-format AIF load=%d"), extractedResult);
    if (extractedResult == KErrNone) {
        CFbsBitmap iconCopy;
        TInt copied = iconCopy.Duplicate(extracted.Handle());
        extracted.Reset();
        RDebug::Print(_L("FBSPROBE extracted duplicate after file close=%d"), copied);
        if (copied == KErrNone) gc->BitBlt(TPoint(560,112), &iconCopy);
    }
    RWindowGroup group(ws);
    RWindow window(ws);
    if (arguments.FindF(_L("window")) >= 0) {
        // Keep the completed image in a real window's redraw store. A direct panel
        // scribble can be erased by the ROM window server's later background paint.
        CWsScreenDevice* wsScreen = new(ELeave) CWsScreenDevice(ws);
        User::LeaveIfError(wsScreen->Construct());
        User::LeaveIfError(group.Construct(1));
        group.SetOrdinalPosition(0, 1);
        User::LeaveIfError(window.Construct(group, 2));
        window.SetExtent(TPoint(0,0), TSize(640,200));
        window.SetBackgroundColor(KRgbWhite);
        window.Activate();
        window.Invalidate();
        CWindowGc* windowGc = 0;
        User::LeaveIfError(wsScreen->CreateContext(windowGc));
        windowGc->Activate(window);
        window.BeginRedraw();
        windowGc->Clear();
        windowGc->DrawBitmap(TPoint(0,0), &canvas);
        window.EndRedraw();
        windowGc->Deactivate();
        ws.Flush();
    } else {
        CFbsBitGc* screenGc = 0;
        User::LeaveIfError(screen->CreateContext(screenGc));
        screenGc->BitBlt(TPoint(0,0), &canvas);
        screen->Update();
    }
    RDebug::Print(_L("FBSPROBE canvas handle=%x data=%x size=%dx%d"), canvas.Handle(), canvas.DataAddress(), canvas.SizeInPixels().iWidth, canvas.SizeInPixels().iHeight);
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
