// The wizard window: the small UI helpers (texts, buttons, status line, progress bar), the screenshots of the driver steps, the controls, the layout
// of the visible ones top-down, bringing the window to the front while the elevated part works.

static void vis(int id, BOOL on) { ShowWindow(H(id), on ? SW_SHOW : SW_HIDE); }
static void text(int id, const WCHAR *t) { SetWindowTextW(H(id), t ? t : L""); vis(id, t != NULL); }
static int g_sth = -1;               // room the layout reserved for the status line (its measured height, 0 when hidden)
static BOOL g_building;              // set_page is rebuilding the page: it lays out once at its end
static int status_need(void);

// Sets the status line. When the room it needs differs from what the layout reserved (shown, hidden, another number of lines), the page is laid out
// again, so the line is never cut off by the button bar and the flexible parts (license text, picture, info box) give way.
static void status(const WCHAR *t, BOOL err)
{
    wz.status_err = err; text(ID_STATUS, t);
    if (g_hwnd && !g_building && status_need() != g_sth) layout();
}

// The button bar of every page: primary = the right-most one and the default (Enter); second = the way out, directly left of it (Cancel, or Skip / Later /
// Close where that is what leaving means); third = another way on, left of the second one; extra = the one link-style action at the bottom left.
static void buttons4(const WCHAR *primary, const WCHAR *second, const WCHAR *third, const WCHAR *extra)
{
    text(ID_PRIMARY, primary); text(ID_SECOND, second); text(ID_THIRD, third); text(ID_EXTRA, extra);
    EnableWindow(H(ID_PRIMARY), TRUE);
    if (primary) SetFocus(H(ID_PRIMARY));
}

static void buttons(const WCHAR *primary, const WCHAR *second, const WCHAR *extra) { buttons4(primary, second, NULL, extra); }

static void progress(BOOL marquee)
{
    HWND p = H(ID_PROG);
    LONG st = GetWindowLongW(p, GWL_STYLE);
    SetWindowLongW(p, GWL_STYLE, marquee ? st | PBS_MARQUEE : st & ~PBS_MARQUEE);
    SendMessageW(p, PBM_SETMARQUEE, marquee, 40);
    SendMessageW(p, PBM_SETRANGE32, 0, 1000);
    SendMessageW(p, PBM_SETPOS, 0, 0);
    g_out.lastpct = -1;
    vis(ID_PROG, TRUE);
}

static void fmt_mb(WCHAR *o, DWORD n) { wsprintfW(o, L"%lu.%lu", n / 1048576, n % 1048576 / 104858); }

static const WCHAR *file_name(const WCHAR *path)
{
    const WCHAR *n = path;
    for (; *path; path++) if (*path == '\\' || *path == '/') n = path + 1;
    return n;
}

typedef struct { UINT32 version; void *callback; BOOL no_thread, no_codecs; } gdip_input;

// The PNG resource id as a bitmap (alpha on white), or NULL when GDI+ is not there or the picture cannot be read.
static HBITMAP load_png(int id, int *w, int *h)
{
    typedef int (WINAPI *startup_t)(ULONG_PTR *, const gdip_input *, void *);
    typedef int (WINAPI *from_stream_t)(IStream *, void **);
    typedef int (WINAPI *to_hbitmap_t)(void *, HBITMAP *, DWORD);
    typedef int (WINAPI *dim_t)(void *, UINT *);
    typedef int (WINAPI *dispose_t)(void *);
    static HMODULE lib; static ULONG_PTR token; static BOOL tried;
    static startup_t startup; static from_stream_t from_stream; static to_hbitmap_t to_hbitmap; static dim_t width, height; static dispose_t dispose;
    WCHAR path[MAX_PATH]; gdip_input in; HRSRC r; const void *data; DWORD n; HGLOBAL mem; void *dst; IStream *st = NULL; void *bmp = NULL; HBITMAP hb = NULL; UINT bw = 0, bh = 0;
    if (!tried) {
        tried = TRUE;
        GetSystemDirectoryW(path, MAX_PATH - 20); lstrcatW(path, L"\\gdiplus.dll");      // by full system path, never searched for
        if ((lib = LoadLibraryW(path)) != NULL) {
            startup = (startup_t)GetProcAddress(lib, "GdiplusStartup");
            from_stream = (from_stream_t)GetProcAddress(lib, "GdipCreateBitmapFromStream");
            to_hbitmap = (to_hbitmap_t)GetProcAddress(lib, "GdipCreateHBITMAPFromBitmap");
            width = (dim_t)GetProcAddress(lib, "GdipGetImageWidth"); height = (dim_t)GetProcAddress(lib, "GdipGetImageHeight");
            dispose = (dispose_t)GetProcAddress(lib, "GdipDisposeImage");
            zero(&in, sizeof(in)); in.version = 1;
            if (!startup || !from_stream || !to_hbitmap || !width || !height || !dispose || startup(&token, &in, NULL)) startup = NULL;
        }
    }
    if (!lib || !startup) return NULL;
    if (!(r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA)) || !(data = LockResource(LoadResource(NULL, r)))) return NULL;
    n = SizeofResource(NULL, r);
    if (!(mem = GlobalAlloc(GMEM_MOVEABLE, n)) || !(dst = GlobalLock(mem))) { if (mem) GlobalFree(mem); return NULL; }
    CopyMemory(dst, data, n); GlobalUnlock(mem);
    if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &st))) {                // the stream owns mem now
        if (from_stream(st, &bmp) == 0 && bmp) {
            width(bmp, &bw); height(bmp, &bh);
            if (to_hbitmap(bmp, &hb, 0xFFFFFFFF) != 0) hb = NULL;           // transparent parts on white
            dispose(bmp);
        }
        IStream_Release(st);
    } else GlobalFree(mem);
    if (hb) { *w = (int)bw; *h = (int)bh; }
    return hb;
}

// Highlight of the "7) Disable driver signature enforcement" line of the restart menu picture (driver-press-7.png, 683 x 468): the line's
// rectangle in picture pixels. Drawn on the scaled copy, so the outline stays sharp at any size and DPI.
// Line 7 of the picture (683 x 468) has its text in rows 266..278 and columns 121..267; line 6 ends at row 258, line 8 starts at row 286. The outline runs 4 rows
// away from the text on each side, in the middle of the gaps, and is 2 pixels thick.
#define HL_X0 116
#define HL_Y0 262
#define HL_X1 272
#define HL_Y1 282

static void draw_highlight(HDC dc, int sw, int sh, int tw, int th)
{
    int x0 = MulDiv(HL_X0, tw, sw), y0 = MulDiv(HL_Y0, th, sh), x1 = MulDiv(HL_X1, tw, sw), y1 = MulDiv(HL_Y1, th, sh);
    int lw = S(2) < 2 ? 2 : S(2), rh = y1 - y0, rad = rh / 2 + 2, lx0 = x1 + lw + rh, lx1;
    HPEN pen = CreatePen(PS_SOLID, lw, RGB(255, 153, 0)), op;
    HBRUSH red = CreateSolidBrush(RGB(224, 52, 0)), ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    LOGFONTW lf; HFONT f, of; const WCHAR *t = T(L"Press 7", L"按 7"); SIZE sz; POINT tri[3]; RECT tr;
    op = (HPEN)SelectObject(dc, pen);
    RoundRect(dc, x0, y0, x1, y1, rad, rad);                                   // orange outline around the line (the pen is centred on the edge)
    // callout to the right of the line: red label "Press 7" with a small arrow pointing at it
    GetObjectW(g_font, sizeof(lf), &lf); lf.lfHeight = -(rh * 7 / 10 < 10 ? 10 : rh * 7 / 10); lf.lfWeight = FW_BOLD; lf.lfQuality = CLEARTYPE_QUALITY;
    f = CreateFontIndirectW(&lf); of = (HFONT)SelectObject(dc, f);
    GetTextExtentPoint32W(dc, t, lstrlenW(t), &sz);
    lx1 = lx0 + sz.cx + rh;
    if (lx1 > tw - 2) { lx0 -= lx1 - (tw - 2); lx1 = tw - 2; }                // keep it inside the picture
    SelectObject(dc, GetStockObject(NULL_PEN)); SelectObject(dc, red);
    RoundRect(dc, lx0, y0, lx1, y1, rad, rad);
    tri[0].x = x1 + lw + 3; tri[0].y = (y0 + y1) / 2;                          // tip, next to the outline
    tri[1].x = lx0 + 1; tri[1].y = y0; tri[2].x = lx0 + 1; tri[2].y = y1;
    Polygon(dc, tri, 3);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    tr.left = lx0; tr.right = lx1; tr.top = y0; tr.bottom = y1;
    DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of); DeleteObject(f);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); DeleteObject(red);
}

static HBITMAP scale_bitmap(HBITMAP src, int sw, int sh, int tw, int th, BOOL highlight)
{
    HDC scr = GetDC(NULL), a = CreateCompatibleDC(scr), b = CreateCompatibleDC(scr); HBITMAP dst = CreateCompatibleBitmap(scr, tw, th), oa, ob;
    oa = SelectObject(a, src); ob = SelectObject(b, dst);
    SetStretchBltMode(b, HALFTONE); SetBrushOrgEx(b, 0, 0, NULL);
    StretchBlt(b, 0, 0, tw, th, a, 0, 0, sw, sh, SRCCOPY);
    if (highlight) draw_highlight(b, sw, sh, tw, th);
    SelectObject(a, oa); SelectObject(b, ob); DeleteDC(a); DeleteDC(b); ReleaseDC(NULL, scr);
    return dst;
}

static void show_image(int which)            // 0: press 7 menu, 1: "install anyway"
{
    g_img = which;
    if (!g_bmp[which]) g_bmp[which] = load_png(18 + which, &g_bw[which], &g_bh[which]);
    vis(ID_IMG, g_bmp[which] != NULL);
}

// Size of the picture of the current page fitted into w x ah (not larger than at 96 dpi scaled to this screen).
static void fit_image(int w, int ah, int *tw, int *th)
{
    int ow = g_bw[g_img], oh = g_bh[g_img];
    if (ah < S(60)) ah = S(60);
    *tw = w; *th = MulDiv(oh, *tw, ow);
    if (*th > ah) { *th = ah; *tw = MulDiv(ow, *th, oh); }
    if (*tw > MulDiv(ow, g_dpi, 96)) { *tw = MulDiv(ow, g_dpi, 96); *th = MulDiv(oh, *tw, ow); }
}

// Fits the picture of the current page into w x ah; returns the height used.
static int place_image(int x, int y, int w, int ah)
{
    int ow = g_bw[g_img], oh = g_bh[g_img], tw, th; HBITMAP old;
    fit_image(w, ah, &tw, &th);
    if (g_shown_img != g_img || g_shown_w != tw || g_shown_h != th) {
        HBITMAP s = scale_bitmap(g_bmp[g_img], ow, oh, tw, th, g_img == 0);
        old = (HBITMAP)SendMessageW(H(ID_IMG), STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)s);
        if (old) DeleteObject(old);
        g_shown = s; g_shown_img = g_img; g_shown_w = tw; g_shown_h = th;
    }
    place(ID_IMG, x, y, tw, th);
    return th;
}

// Appends s to d (a buffer of cap characters), cutting it where it no longer fits; d always stays terminated.
static void cat_to(WCHAR *d, int cap, const WCHAR *s)
{
    int n = lstrlenW(d);
    if (n < cap - 1) lstrcpynW(d + n, s, cap - n);
}

#define FOOTER 76                                   // height of the grey button band, at 96 dpi

static HWND add(const WCHAR *cls, DWORD style, int id)
{
    DWORD ex = (id == ID_EDIT || id == ID_RICH || id == ID_INFO) ? WS_EX_CLIENTEDGE : 0;   // themed border like other Windows text boxes
    HWND c = CreateWindowExW(ex, cls, L"", WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, g_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)(id == ID_TITLE ? g_big : g_font), FALSE);
    return c;
}

static void create_controls(void)
{
    WCHAR lib[MAX_PATH]; const WCHAR *rich;
    // by full system path, never searched for (a planted msftedit.dll in the current or the exe's folder is not loaded)
    GetSystemDirectoryW(lib, MAX_PATH - 20); lstrcatW(lib, L"\\msftedit.dll");
    if (LoadLibraryW(lib)) rich = L"RICHEDIT50W";
    else { GetSystemDirectoryW(lib, MAX_PATH - 20); lstrcatW(lib, L"\\riched20.dll"); LoadLibraryW(lib); rich = L"RichEdit20W"; }
    add(L"STATIC", WS_VISIBLE | SS_NOPREFIX, ID_TITLE);
    add(L"STATIC", WS_VISIBLE | SS_NOPREFIX, ID_BODY);
    add(L"EDIT", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, ID_EDIT);
    add(rich, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY, ID_RICH);
    add(L"EDIT", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY, ID_INFO);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CHECK);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_PIN);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_AUTO);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, ID_DRV);
    add(L"STATIC", SS_BITMAP | SS_NOTIFY, ID_IMG);
    SetWindowTextW(H(ID_DRV), T(L"Also install the Dyna Pro USB &driver (one restart needed)", L"同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）(&D)"));
    SendMessageW(H(ID_DRV), BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(H(ID_PIN), T(L"Pin DynaRun to the &taskbar", L"釘選到工作列(&T)"));
    SetWindowTextW(H(ID_AUTO), T(L"Start DynaRun when Windows &starts", L"開機時自動啟動 DynaRun(&S)"));
    SendMessageW(H(ID_PIN), BM_SETCHECK, wz.opts.pin ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(H(ID_AUTO), BM_SETCHECK, wz.opts.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(H(ID_CHECK), T(L"I &accept the license agreement", L"我接受授權合約(&A)"));
    add(PROGRESS_CLASSW, 0, ID_PROG);
    add(L"STATIC", SS_NOPREFIX, ID_STATUS);
    add(L"BUTTON", WS_TABSTOP, ID_EXTRA);
    add(L"BUTTON", WS_TABSTOP, ID_THIRD);
    add(L"BUTTON", WS_TABSTOP, ID_SECOND);
    add(L"BUTTON", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_PRIMARY);
}

// Height of a control's text wrapped to width w, or its width when w is 0. (The buffer holds the longest text of any page, the license page's
// info block, with room to spare; a cut-off text would measure too short and the controls below it would overlap the last lines.)
static int measure(int id, int w)
{
    static WCHAR t[4096]; RECT r; HDC dc = GetDC(g_hwnd);
    HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(H(id), WM_GETFONT, 0, 0));
    GetWindowTextW(H(id), t, 4096);
    r.left = r.top = 0; r.right = w ? w : 10000; r.bottom = 0;
    DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS | (w ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old); ReleaseDC(g_hwnd, dc);
    return w ? r.bottom : r.right;
}

static BOOL shown(int id) { return (GetWindowLongW(H(id), GWL_STYLE) & WS_VISIBLE) != 0; }   // also before the window is shown

static int status_need(void)
{
    RECT c;
    if (!shown(ID_STATUS)) return 0;
    GetClientRect(g_hwnd, &c);
    return measure(ID_STATUS, c.right - 2 * S(32)) + S(8);
}

static void place(int id, int x, int y, int w, int h) { SetWindowPos(H(id), NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); }

// One top-down pass over the visible controls from their measured sizes. apply FALSE: nothing is moved, it returns the client height the page needs
// (the picture and the license text at the size they want); TRUE: places the controls so that the flexible ones (picture, license text, note) use
// what is left above the button band at `bottom`.
static int stack(BOOL apply, int w, int bottom)
{
    int m = S(32), y, h, sh = shown(ID_STATUS) ? measure(ID_STATUS, w) + S(8) : 0;   // sh: room the status line needs at this width and DPI (it may wrap)
    if (apply) g_sth = sh;
    y = S(shown(ID_PIN) ? 18 : 24);                     // tighter on the first page: its text and the options must fit
    h = measure(ID_TITLE, w); if (apply) place(ID_TITLE, m, y, w, h); y += h + S(shown(ID_PIN) ? 10 : 16);
    h = measure(ID_BODY, w) + S(6); if (apply) place(ID_BODY, m, y, w, h); y += h + S(shown(ID_PIN) ? 6 : 20);
    if (shown(ID_PIN)) {                                // the two options: side by side when they fit, else stacked
        int a = measure(ID_PIN, 0) + S(24), b2 = measure(ID_AUTO, 0) + S(24), side = a + b2 + S(16) <= w;
        if (apply) { place(ID_PIN, m, y, a, S(24)); place(ID_AUTO, side ? m + a + S(16) : m, side ? y : y + S(24), b2, S(24)); }
        y += side ? S(24) : S(48);
    }
    if (shown(ID_DRV)) { h = measure(ID_DRV, w - S(24)) + S(8); if (apply) place(ID_DRV, m, y + S(2), w, h); y += h + S(4); }
    if (shown(ID_IMG)) {
        if (apply) y += place_image(m, y, w, bottom - y - S(10) - sh) + S(8);       // the status line below keeps its room
        else { int tw, th; fit_image(w, 100000, &tw, &th); y += th + S(8); }
    }
    if (shown(ID_EDIT)) { if (apply) place(ID_EDIT, m, y, w < S(360) ? w : S(360), S(34)); y += S(34) + S(12); }
    if (shown(ID_PROG)) { if (apply) place(ID_PROG, m, y, w, S(20)); y += S(20) + S(10); }
    if (shown(ID_RICH)) {
        int cy = apply ? bottom - (sh ? sh + S(6) : S(16)) - S(28) : y + S(200) + S(10);   // the license text: at least ~200 px high; it gives way to the status line
        if (apply) { place(ID_CHECK, m, cy, w, S(28)); place(ID_RICH, m, y, w, cy - S(10) - y); }
        y = cy + S(28) + (apply && sh ? S(4) : 0);
    }
    if (shown(ID_INFO)) {                               // the details of the setup file: a read-only text box (selectable, copyable) with the rest of the page
        int bh2 = apply ? bottom - S(16) - y - sh : S(200);
        if (apply) place(ID_INFO, m, y, w, bh2 > S(60) ? bh2 : S(60));
        y += bh2 + S(16);
    }
    if (shown(ID_STATUS)) {
        if (apply) place(ID_STATUS, m, y, w, bottom - y > 0 ? bottom - y : 0);
        else y += measure(ID_STATUS, w) + S(8);
    }
    return apply ? y : y + S(FOOTER);
}

// Sets the client height of the window to `need` (at least the usual one, at most what the screen's work area allows), keeping it centred.
static void fit_window(int need)
{
    RECT wr, cr, work; int target = need > S(580) ? need : S(580), nc, nh, y;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return;
    GetWindowRect(g_hwnd, &wr); GetClientRect(g_hwnd, &cr);
    nc = (wr.bottom - wr.top) - cr.bottom;
    if (target + nc > work.bottom - work.top) target = work.bottom - work.top - nc;
    if (target == cr.bottom) return;
    nh = target + nc;
    y = wr.top - (nh - (wr.bottom - wr.top)) / 2;
    if (y + nh > work.bottom) y = work.bottom - nh;
    if (y < work.top) y = work.top;
    SetWindowPos(g_hwnd, NULL, wr.left, y, wr.right - wr.left, nh, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Lays the visible controls out top-down from the measured text heights (the window grows when a page needs more room); buttons right-aligned in the footer.
static void layout(void)
{
    static const int btn[] = { ID_PRIMARY, ID_SECOND, ID_THIRD };
    RECT c; int m = S(32), w, x, i, bw, bh = S(36), by, bottom;
    GetClientRect(g_hwnd, &c);
    w = c.right - 2 * m;
    fit_window(stack(FALSE, w, 0));
    GetClientRect(g_hwnd, &c);
    bottom = c.bottom - S(FOOTER);
    stack(TRUE, w, bottom);
    by = bottom + (S(FOOTER) - bh) / 2;
    for (x = c.right - m, i = 0; i < 3; i++) {
        if (!shown(btn[i])) continue;
        bw = measure(btn[i], 0) + S(48); if (bw < S(112)) bw = S(112);
        x -= bw; place(btn[i], x, by, bw, bh); x -= S(10);
    }
    bw = measure(ID_EXTRA, 0) + S(40); if (bw < S(112)) bw = S(112);
    place(ID_EXTRA, m, by, bw, bh);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// The installed UI font for the language (font linking would mix two fonts in Chinese text).
static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found)
{ *(BOOL *)found = TRUE; (void)lf; (void)tm; (void)type; return 0; }

static void pick_face(LOGFONTW *lf)
{
    static const WCHAR *zh[] = { L"Microsoft JhengHei UI", L"Microsoft JhengHei", L"PMingLiU", NULL },
                       *en[] = { L"Segoe UI", L"Tahoma", NULL };
    const WCHAR **f; LOGFONTW q; HDC dc = GetDC(NULL); BOOL found;
    for (f = g_zh ? zh : en; *f; f++) {
        zero(&q, sizeof(q)); q.lfCharSet = DEFAULT_CHARSET; lstrcpyW(q.lfFaceName, *f); found = FALSE;
        EnumFontFamiliesExW(dc, &q, font_found, (LPARAM)&found, 0);
        if (found) { lstrcpyW(lf->lfFaceName, *f); lf->lfCharSet = DEFAULT_CHARSET; break; }
    }
    ReleaseDC(NULL, dc);
}

// Brings the wizard back in front after the elevated part. SetForegroundWindow alone is refused
// when another process (msiexec's progress window, "Windows configures ...") had the foreground:
// raise it to the top of the z-order anyway, then borrow the foreground thread's input to activate it.
static void bring_front(HWND h)
{
    HWND fg = GetForegroundWindow(); DWORD me = GetCurrentThreadId(), other = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    if (SetForegroundWindow(h) || !other || other == me) return;
    if (AttachThreadInput(me, other, TRUE)) {
        BringWindowToTop(h); SetForegroundWindow(h); SetFocus(h);
        AttachThreadInput(me, other, FALSE);
    }
    if (GetForegroundWindow() != h) {                // still refused: at least flash the taskbar button
        FLASHWINFO f; f.cbSize = sizeof(f); f.hwnd = h; f.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG; f.uCount = 3; f.dwTimeout = 0;
        FlashWindowEx(&f);
    }
}

// Image name (no path) of the process that owns window w; "" when it cannot be read.
static const WCHAR *owner_name(HWND w, WCHAR *buf, DWORD n)
{
    typedef BOOL (WINAPI *qfpin_t)(HANDLE, DWORD, LPWSTR, PDWORD);
    static qfpin_t qfpin; DWORD pid = 0; HANDLE p; const WCHAR *name = buf, *c;
    buf[0] = 0;
    if (!qfpin) qfpin = (qfpin_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW");
    if (!qfpin || !GetWindowThreadProcessId(w, &pid)) return buf;
    if ((p = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid)) != NULL) {
        if (!qfpin(p, 0, buf, &n)) buf[0] = 0;
        CloseHandle(p);
    }
    for (c = buf; *c; c++) if (*c == '\\') name = c + 1;
    return name;
}

// While the elevated part installs: the UAC prompt and msiexec's progress window take the foreground,
// and afterwards Windows may activate Explorer, which then covers the wizard. Keep the wizard just
// behind msiexec's window, and above Explorer; other programs the user switches to are left alone.
static void keep_up(HWND h)
{
    static WCHAR b[MAX_PATH]; const WCHAR *n; HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (!fg || fg == h || IsIconic(h)) return;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return;
    n = owner_name(fg, b, MAX_PATH);
    if (!lstrcmpiW(n, L"msiexec.exe") || !lstrcmpiW(n, L"consent.exe"))
        SetWindowPos(h, fg, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else if (!lstrcmpiW(n, L"explorer.exe")) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
