// The setup wizard: one window, one question at a time, for users who never installed software.
//
//   DynaRun V3 installed:  Hello -> Install (one UAC prompt) -> Done
//   not installed:         Hello -> get "Dyna Pro Dynamometers.zip" (a network check first; found in Downloads/Desktop, or
//                          downloaded from dynapro.co.uk with pause / resume; one error page, with the site's download page
//                          one click away and "Choose file...")
//                          -> password -> extract -> check it is DynaRun (by the MSI's UpgradeCode, never by
//                          file name) -> Dyna Pro's license -> Install (msiexec /qb and the fix, one UAC prompt)
//                          -> Done -> "Start DynaRun"
// Long steps run on a worker thread and report back with WM_PROGRESS / WM_JOBDONE.
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include "drf_app.h"
#include "drf_package.h"
#include "drf_driver.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

enum { ID_TITLE = 100, ID_BODY, ID_EDIT, ID_RICH, ID_CHECK, ID_PROG, ID_STATUS, ID_EXTRA, ID_SECOND, ID_PRIMARY, ID_KEEPUP, ID_PIN, ID_AUTO, ID_DRV, ID_IMG, ID_INFO, ID_THIRD };
enum { P_HELLO, P_FETCH, P_FAIL, P_PASSWORD, P_EXTRACT, P_CHECK, P_LICENSE, P_RUNNING, P_INSTALL, P_DONE, P_ERROR,
       P_DRV_INTRO, P_DRV_PRESS7, P_DRV_HASH, P_DRV_BDE, P_DRV_PREP, P_DRV_REBOOT, P_DRV_RETRY, P_DRV_WARN, P_DRV_INSTALL, P_DRV_DONE, P_DRV_ERROR };   // driver.c
enum { JOB_ACQUIRE, JOB_EXTRACT, JOB_INSTALL_ALL, JOB_INSTALL_FIX, JOB_DRV_PREP, JOB_DRV_INSTALL, JOB_DRV_CANCEL, JOB_COUNT };   // the order is not used anywhere: JOBS[] says what each job is
#define WM_PROGRESS (WM_APP + 1)
#define WM_JOBDONE  (WM_APP + 2)
#define WM_HINT     (WM_APP + 3)                    // the DynaRun update hint is known (wParam: 0 none, 1 newer version, 2 the site's file differs)
#define TIMER_RUNNING 1

static int g_page, g_job, g_dpi, g_lastpct = -1, g_pending, g_lastrun;   // g_pending: install job waiting for DynaRun to close
static volatile LONG g_cancel;
static install_opts g_opts;                             // what the pages ask for (starts as the command line gave it); only the UI thread changes it
static install_opts g_jobopts;                          // the copy a job runs with (start_job): the worker never reads g_opts while the pages may change it
static install_result g_result;                         // what the install job reports besides its code (the pin hint); written by the worker, read once the job is done
static BOOL g_close_asked;                              // g_opts.close was set by the "Close DynaRun and install" button: it covers that one install job only (job_done takes it back)
static BOOL g_busy, g_closing, g_installed, g_pkg_ours, g_pkg_found, g_status_err, g_badfile, g_nolicense;   // g_nolicense: the setup's license text could not be read
static int g_nfound;   // packages pkg_find_local has handed us so far
static HANDLE g_thread;
static WCHAR g_pkg[MAX_PATH], g_msi[MAX_PATH], g_ver[64], g_note[1100];
static zipent g_zip;
static char g_pw[256];
static char *g_rtf;
static WCHAR g_info[2200];   // what the license page shows about the package (path, size, SHA-256, MSI version/manufacturer)
static DWORD g_neterr, g_total;
static BOOL g_paused, g_pause_req;                      // download paused (worker ended, .part kept) / pause asked, worker still stopping
static int g_hint;                                       // DynaRun update hint (pkg_update_hint): 0 none, 1 a newer version is published, 2 the site's zip differs from the last download
static WCHAR g_hint_inst[40], g_hint_new[40];            // the installed and the published version (hint 1)
static BOOL g_pre;                                        // the driver is ticked in the main flow: its package is fetched and checked BEFORE the install, and the one elevated stage installs the fix and prepares the driver (one UAC prompt)
static int g_inst_job;                                    // the install job (JOB_INSTALL_ALL / JOB_INSTALL_FIX) that waits for the driver package while g_pre
static BOOL g_fix_done;                                   // g_pre: the fix is installed (the driver step after it may still have failed)
static BOOL g_driver_only;                                // DynaRun and its fix are installed (this version) and only the driver is missing: the first page offers just that
static BOOL g_checking, g_marquee, g_drv_allow;          // the non-pausable "checking" phase / marquee bar (size unknown) / the user chose "install anyway" for a zip with another hash
static int g_drv_how;                                    // DRV_SRC_*: the driver zip was downloaded, or found on this computer (nothing downloaded)
static WCHAR g_drv_hex[65];                              // SHA-256 and size of a driver zip that is not the pinned file (the warning page shows them)
static ULONGLONG g_drv_size;
static WCHAR g_drv_rep[1500];                           // the driver files that differ from the pinned ones, one per line (the warning page lists them)
static int g_drv_bl = BL_UNKNOWN;                              // BitLocker hint for the press-7 page, asked in the worker (WMI can take seconds): 1 on, 0 off, -1 cannot be told
// What the acquire flow needs to know about the thing it gets: the DynaRun V3 setup and the Dyna Pro USB driver go through the same pages (looking for
// a copy on this computer, download with pause / resume, one error page); only these texts, the "partial download" test, the search and the check differ.
typedef struct {
    const WCHAR *name_en, *name_zh;          // "the DynaRun V3 setup" / "DynaRun V3 安裝檔"
    const WCHAR *get_en, *get_zh;            // what to click on the website
    const WCHAR *pick_en, *pick_zh;          // what "Choose file..." takes
    BOOL (*partial)(DWORD *have);            // a .part that can be continued
} acq_desc;
static const acq_desc ACQ_SETUP = { L"the DynaRun V3 setup", L"DynaRun V3 安裝檔", L"\"Dyna Run V3 Software\"", L"「Dyna Run V3 Software」",
                                  L"the zip (or the MSI) you downloaded", L"你下載的 zip（或 MSI）", pkg_download_partial };
static const acq_desc ACQ_DRIVER = { L"the Dyna Pro USB driver", L"驅動程式", L"\"Dyna Pro USB Drivers\"", L"「Dyna Pro USB Drivers」",
                                   L"Dyna Pro USB.zip, or ftdibus.inf in the folder where it is unpacked", L"Dyna Pro USB.zip，或已解開它的資料夾裡的 ftdibus.inf", driver_partial };
static const acq_desc *g_acq = &ACQ_SETUP, *g_dlacq;     // the thing being got now; the one g_dltotal belongs to
static BOOL g_acq_fresh;                                 // "Download new version" (update hint): no package already on this computer is taken, it is downloaded
static BOOL g_acq_local_ok;                              // the DynaRun search may hand out another local package (pkg_find_local remembers 8 files)
#define ACQ_NAME(d) T((d)->name_en, (d)->name_zh)
static DWORD g_dltotal, g_dlhave;                       // size of the file being downloaded (server's, 0 = not known) and bytes received so far
static HFONT g_font, g_big;
static HBRUSH g_white, g_grey;
// the Dyna Pro USB driver (driver.c): g_drv_avail: the check box on the first page is offered (Windows 10+, driver not there yet); g_drv: ticked
static BOOL g_drv_avail, g_drv, g_drv_have, g_drv_noreboot;   // g_drv_have: already installed; g_drv_noreboot: Windows would not restart by itself
static WCHAR g_drvzip[MAX_PATH];                              // the checked zip ("" = the staged copy, after a restart)
static HBITMAP g_bmp[2], g_shown;                             // the two screenshots (decoded once) and the scaled one in the picture control
static int g_bw[2], g_bh[2], g_img, g_shown_img = -1, g_shown_w, g_shown_h;
static void layout(void);
static void place(int id, int x, int y, int w, int h);
#define S(x) MulDiv((x), g_dpi, 96)
#define H(id) GetDlgItem(g_hwnd, (id))

/* ---------- small UI helpers ---------- */

static void vis(int id, BOOL on) { ShowWindow(H(id), on ? SW_SHOW : SW_HIDE); }
static void text(int id, const WCHAR *t) { SetWindowTextW(H(id), t ? t : L""); vis(id, t != NULL); }
static int g_sth = -1;               // room the layout reserved for the status line (its measured height, 0 when hidden)
static BOOL g_building;              // set_page is rebuilding the page: it lays out once at its end
static int status_need(void);
// Sets the status line. When the room it needs differs from what the layout reserved (shown, hidden, another number of lines), the page is laid out
// again, so the line is never cut off by the button bar and the flexible parts (license text, picture, info box) give way.
static void status(const WCHAR *t, BOOL err)
{
    g_status_err = err; text(ID_STATUS, t);
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
    g_lastpct = -1;
    vis(ID_PROG, TRUE);
}

static void fmt_mb(WCHAR *o, DWORD n) { wsprintfW(o, L"%lu.%lu", n / 1048576, n % 1048576 / 104858); }

// A download that can be continued where it stopped (its .part and ETag are there): appends how much is already received to g_note and returns TRUE.
// The button then reads "Continue download" instead of "Retry".
static BOOL resume_note(void)
{
    DWORD have = 0; WCHAR a[24], t[24], line[200];
    if (!g_acq->partial(&have)) return FALSE;
    fmt_mb(a, have);
    if (g_dltotal > have) {
        fmt_mb(t, g_dltotal);
        wsprintfW(line, T(L"\n\n%s of %s MB are downloaded. \"Continue download\" carries on from there.", L"\n\n已下載 %s / %s MB，按「繼續下載」會從中斷的地方接續。"), a, t);
    } else
        wsprintfW(line, T(L"\n\n%s MB are downloaded. \"Continue download\" carries on from there.", L"\n\n已下載 %s MB，按「繼續下載」會從中斷的地方接續。"), a);
    lstrcatW(g_note, line);
    return TRUE;
}

static UINT StrToUint(const WCHAR *s) { UINT v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

// ANSI code page of the system locale, also when "Beta: UTF-8" makes the active one 65001
static UINT legacy_acp(void)
{
    WCHAR b[8];
    return GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, b, 8) ? StrToUint(b) : GetACP();
}

static const WCHAR *file_name(const WCHAR *path)
{
    const WCHAR *n = path;
    for (; *path; path++) if (*path == '\\' || *path == '/') n = path + 1;
    return n;
}

// "DynaRun V3", "DynaRunFix.exe" ... for the running-programs page
static void running_names(int run, WCHAR *out)
{
    out[0] = 0;
    if (run & RUN_DYNARUN) lstrcatW(out, L"   DynaRun V3\n");
    if (run & RUN_LAUNCHER) lstrcatW(out, T(L"   DynaRunFix.exe (starting DynaRun)\n", L"   DynaRunFix.exe（正在啟動 DynaRun）\n"));
    if (run & RUN_LEPROC) lstrcatW(out, L"   LEProc.exe (Locale Emulator)\n");
}

// The size is the server's (Content-Length), shown once the download has started.
static void fetch_body(DWORD total)
{
    WCHAR size[40], b[500];
    size[0] = 0;
    if (total) wsprintfW(size, T(L" (%lu.%lu MB)", L"（%lu.%lu MB）"), total / 1048576, total % 1048576 / 104858);
    wsprintfW(b, T(L"Getting %s: a copy on this computer is used if there is one, else it is downloaded from the Dyna Pro website%s.\nThis can take a few minutes.\n\n"
                   L"If it does not work, click \"Open website\" to get it from the website yourself.",
                   L"正在取得%s：電腦上已有就直接使用，否則從 Dyna Pro 官網下載%s，可能需要幾分鐘。\n\n"
                   L"如果下載不順利，可以按「開啟官網」，自己從官網下載。"), ACQ_NAME(g_acq), size);
    g_total = total;
    text(ID_BODY, b);
}

// Last paragraph of the first page.
static const WCHAR *disclaimer(void)
{
    return T(L"\n\nDynaRunFix is an independent open-source tool, not affiliated with or endorsed by Dyna Pro Dynamometers. "
             L"It is provided as is, without warranty (MIT license). DynaRun V3 is licensed to you separately by Dyna Pro.",
             L"\n\nDynaRunFix 是獨立的開源工具，與 Dyna Pro Dynamometers 無關，也未經其認可；依 MIT 授權按現狀提供，不提供任何擔保。"
             L"DynaRun V3 由 Dyna Pro 另行授權給你。");
}

/* ---------- screenshots of the driver steps (PNG resources, decoded with GDI+ looked up at run time: nothing is linked to it) ---------- */

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

// The "Start DynaRun" button is offered only when DynaRun is installed here and our launcher is in place (g_exe comes from locate_dynarun, which
// the wizard runs at start in every mode, so this also holds in the process the sign-in task starts after the restart; g_launcher from init_paths).
// The driver step may be skipped (the fix is installed without it) only in the main install flow, not when it is the whole job (the shortcut, /driver, or the driver-only first page).
static BOOL can_skip_driver(void) { return g_drvmode == DRV_MODE_NONE && !g_driver_only; }

// The line about a newer DynaRun on the first page, and its button. A hint only: nothing happens unless the button is clicked.
static void hint_line(WCHAR *b)
{
    WCHAR t[400];
    if (g_hint == HINT_NEWER) {
        wsprintfW(t, T(L"\n\nA newer DynaRun V3 is available on Dyna Pro's website: %s (installed: %s). \"Download new version\" downloads and installs it; nothing happens unless you click it.",
                       L"\n\nDyna Pro 官網有新版 DynaRun V3：%s（目前已安裝：%s）。按「下載新版」會下載並安裝；不按就什麼都不會發生。"), g_hint_new, g_hint_inst);
        lstrcatW(b, t);
    } else if (g_hint == HINT_ZIPDIFF)
        lstrcatW(b, T(L"\n\nDyna Pro's website may have a newer DynaRun V3 (the file there is not the one downloaded last time). \"Download new version\" gets it; nothing happens unless you click it.",
                      L"\n\nDyna Pro 官網可能有新版 DynaRun V3（那裡的檔案和上次下載的不同）。按「下載新版」會取得它；不按就什麼都不會發生。"));
}
static const WCHAR *hint_btn(void) { return g_hint ? T(L"Download new version", L"下載新版") : NULL; }

static BOOL can_start_dynarun(void) { return g_installed && g_exe[0] && exists(g_launcher); }

// Appends s to d (a buffer of cap characters), cutting it where it no longer fits; d always stays terminated.
static void cat_to(WCHAR *d, int cap, const WCHAR *s)
{
    int n = lstrlenW(d);
    if (n < cap - 1) lstrcpynW(d + n, s, cap - n);
}

// What is said about the taskbar pin (g_result.pin, set by the user stage) and about DynaRun's Chinese text: on the page "Done" and, when the driver goes with
// the install (g_fix_done: the page "Done" is skipped), on the restart page.
static void pin_hint(WCHAR *b)
{
    if (g_result.pin == PIN_NEXT_SIGNIN) lstrcatW(b, T(L"\n\nDynaRun will appear on the taskbar after you sign out and back in.", L"\n\n登出再登入後，DynaRun 會出現在工作列上。"));
    if (g_result.pin == PIN_FAILED) lstrcatW(b, os_build() >= 22000
        ? T(L"\n\nTo pin it to the taskbar: right-click the \"DynaRun V3\" icon on the desktop → Show more options → Pin to taskbar.",
            L"\n\n要釘選到工作列：在桌面「DynaRun V3」圖示上按右鍵 →「顯示其他選項」→「釘選到工作列」。")
        : T(L"\n\nTo pin it to the taskbar: right-click the \"DynaRun V3\" icon on the desktop → Pin to taskbar.",
            L"\n\n要釘選到工作列：在桌面「DynaRun V3」圖示上按右鍵 →「釘選到工作列」。"));
}
static void acp_warning(void)
{
    if (g_zh && legacy_acp() != 950)   // DynaRun's Chinese is Big5: only a zh-TW system locale shows it everywhere
        status(L"注意：這台電腦的「非 Unicode 程式的語言」不是中文（台灣），DynaRun 的中文可能會變成亂碼。\n"
               L"請到「設定 → 時間與語言 → 語言與地區 → 系統管理語言設定 → 變更系統地區設定」選「中文（繁體，台灣）」，再重新開機。", TRUE);
}

static void set_page(int p)
{
    static WCHAR b[2600];
    g_page = p;
    g_paused = g_pause_req = FALSE;
    g_checking = g_marquee = FALSE;                     // a page starts with a plain bar; a download started again (also after the checking phase) shows its progress
    g_building = TRUE;                                  // status() does not lay out while the page is being rebuilt; the layout at the end does
    KillTimer(g_hwnd, TIMER_RUNNING);
    vis(ID_EDIT, FALSE); vis(ID_RICH, FALSE); vis(ID_CHECK, FALSE); vis(ID_PIN, FALSE); vis(ID_AUTO, FALSE); vis(ID_DRV, FALSE); vis(ID_IMG, FALSE); vis(ID_INFO, FALSE); vis(ID_PROG, FALSE); status(NULL, FALSE);
    switch (p) {
    case P_HELLO:
        if (g_driver_only) {
            wsprintfW(b, T(L"DynaRun V3 was found:\n%s\n\nThe fix for Windows 10/11 is already installed (version %s). The Dyna Pro USB driver is not installed yet; "
                           L"without it this computer cannot talk to the dynamometer.\n\nThis installs the driver (one restart is needed). When Windows asks whether to allow changes, click \"Yes\".\n\n"
                           L"If DynaRun does not behave, \"Reinstall the fix...\" installs the fix again.",
                           L"已找到 DynaRun V3：\n%s\n\nWindows 10/11 的相容性修正已經安裝（版本 %s）。但 Dyna Pro USB 驅動程式還沒有安裝；"
                           L"沒有它，這台電腦就無法和馬力機連線。\n\n接下來會安裝驅動程式（需要重新開機一次）。Windows 詢問「是否允許變更」時，請按「是」。\n\n"
                           L"如果 DynaRun 運作不正常，可以按「重新安裝修正…」再安裝一次修正。"), g_exe, WIDEN(DRF_DISPLAY));
            hint_line(b);
            text(ID_TITLE, T(L"Install the Dyna Pro USB driver", L"安裝 Dyna Pro USB 驅動程式"));
            text(ID_BODY, b);
            buttons4(T(L"Install the driver", L"安裝驅動程式"), T(L"Cancel", L"取消"), hint_btn(), T(L"Reinstall the fix...", L"重新安裝修正…"));
            if (g_note[0]) status(g_note, TRUE);
            break;
        }
        if (g_installed) {
            text(ID_TITLE, T(L"Fix DynaRun V3 for this Windows", L"讓 DynaRun V3 在這台電腦正常運作"));
            wsprintfW(b, T(L"DynaRun V3 was found:\n%s\n\nThis installs the fixes for Windows 10/11 (flickering main screen, .Dpr files "
                           L"in OneDrive that do not open, garbled text, \"Run as administrator\").\n\n"
                           L"When Windows asks whether to allow changes, click \"Yes\".%s",
                           L"已找到 DynaRun V3：\n%s\n\n接下來會安裝 Windows 10/11 的相容性修正（主畫面閃爍、OneDrive 裡的 .Dpr 打不開、"
                           L"中文亂碼、以系統管理員執行卡住）。\n\nWindows 詢問「是否允許變更」時，請按「是」。%s"), g_exe, disclaimer());
            hint_line(b);
            text(ID_BODY, b);
            buttons4(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), hint_btn(), NULL);
        } else {
            text(ID_TITLE, T(L"Install DynaRun V3", L"安裝 DynaRun V3"));
            wsprintfW(b, T(L"DynaRun V3 is not installed on this computer yet.\n\nThis program will:\n"
                            L"   1.  download the DynaRun V3 setup from the Dyna Pro website\n"
                            L"   2.  install DynaRun V3\n"
                            L"   3.  add the fixes for Windows 10/11\n\n"
                            L"You need the setup password that Dyna Pro gave you.%s",
                            L"這台電腦還沒有安裝 DynaRun V3。\n\n本程式會自動：\n"
                            L"   1.  從 Dyna Pro 官網下載 DynaRun V3 安裝檔\n"
                            L"   2.  安裝 DynaRun V3\n"
                            L"   3.  加上 Windows 10/11 的相容性修正\n\n"
                            L"過程中需要輸入 Dyna Pro 給你的安裝密碼。%s"), disclaimer());
            text(ID_BODY, b);
            buttons(T(L"Start", L"開始"), T(L"Cancel", L"取消"), T(L"Installed elsewhere...", L"已裝在其他位置…"));
        }
        vis(ID_PIN, TRUE); vis(ID_AUTO, TRUE); vis(ID_DRV, g_drv_avail);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_FETCH:                                    // looking for a copy here, downloading, checking: the same page for the DynaRun setup and the driver
        wsprintfW(b, T(L"Getting %s", L"正在取得%s"), ACQ_NAME(g_acq));
        text(ID_TITLE, b);
        fetch_body(0);
        progress(FALSE); status(L" ", FALSE);
        buttons(T(L"Pause", L"暫停"), T(L"Cancel", L"取消"), T(L"Open website", L"開啟官網"));
        break;
    case P_FAIL:                                     // the one error page of the acquire flow
        if (g_badfile) lstrcpyW(b, T(L"This is not the DynaRun V3 setup", L"這不是 DynaRun V3 安裝檔"));
        else wsprintfW(b, T(L"%s could not be downloaded", L"無法自動下載%s"), ACQ_NAME(g_acq));
        if (!g_zh && !g_badfile) b[0] = (WCHAR)(b[0] >= 'a' && b[0] <= 'z' ? b[0] - 32 : b[0]);          // "the DynaRun V3 setup" at the start of a sentence
        text(ID_TITLE, b);
        {
            BOOL resume = !g_badfile && resume_note();            // a .part to carry on from (appends its size to the note)
            BOOL skip = g_acq == &ACQ_DRIVER && can_skip_driver();
            g_badfile = FALSE;
            wsprintfW(b, T(L"%s\n\nGet it yourself:\n   1.  click \"Open website\"\n   2.  on the page, click the download for %s\n"
                           L"   3.  come back here and click \"Retry\" or \"Continue download\", or \"Choose file...\" to pick %s",
                           L"%s\n\n請自己下載：\n   1.  按「開啟官網」\n   2.  在網頁上下載%s\n"
                           L"   3.  回到這裡按「重試」或「繼續下載」，或按「選擇檔案…」選取%s"), g_note, T(g_acq->get_en, g_acq->get_zh), T(g_acq->pick_en, g_acq->pick_zh));
            text(ID_BODY, b);
            buttons4(resume ? T(L"Continue download", L"繼續下載") : T(L"Retry", L"重試"), skip ? T(L"Skip", L"略過") : T(L"Cancel", L"取消"),
                     T(L"Choose file...", L"選擇檔案…"), T(L"Open website", L"開啟官網"));
        }
        break;
    case P_PASSWORD:
        text(ID_TITLE, T(L"Enter the setup password", L"輸入安裝密碼"));
        wsprintfW(b, T(L"Type the password that Dyna Pro gave you for the DynaRun V3 setup.\n(It is used only to open the setup file and is not saved.)\n\nSetup file: %s",
                       L"請輸入 Dyna Pro 給你的 DynaRun V3 安裝密碼。\n（只用來打開安裝檔，不會被儲存。）\n\n安裝檔：%s"), file_name(g_pkg));
        text(ID_BODY, b);
        vis(ID_EDIT, TRUE);
        buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), T(L"Choose file...", L"選擇檔案…"));
        if (g_note[0]) status(g_note, TRUE);
        SetFocus(H(ID_EDIT)); SendMessageW(H(ID_EDIT), EM_SETSEL, 0, -1);
        break;
    case P_EXTRACT:
        text(ID_TITLE, T(L"Opening the setup file", L"正在打開安裝檔"));
        text(ID_BODY, T(L"Please wait...", L"請稍候…"));
        progress(FALSE);
        buttons(NULL, T(L"Cancel", L"取消"), NULL);
        break;
    case P_CHECK:                                    // everything we know about the setup file, to compare with what Dyna Pro gives
        text(ID_TITLE, T(L"Check the installer", L"確認安裝檔"));
        text(ID_BODY, T(L"Compare this with what Dyna Pro gives you. Dyna Pro does not sign its setup, so DynaRunFix cannot prove that it is exactly what Dyna Pro released: install only a file you got from Dyna Pro.",
                        L"請與 Dyna Pro 提供的資訊比對。Dyna Pro 的安裝檔沒有數位簽章，DynaRunFix 無法證明它與 Dyna Pro 發布的完全一致，請只安裝從 Dyna Pro 取得的檔案。"));
        SetWindowTextW(H(ID_INFO), g_info);
        vis(ID_INFO, TRUE);
        buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), T(L"Open website", L"開啟官網"));
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_LICENSE:
        text(ID_TITLE, T(L"Dyna Pro license agreement", L"Dyna Pro 授權合約"));
        lstrcpyW(b, g_nolicense ? T(L"The license agreement in this DynaRun V3 setup could not be shown.",
                                    L"無法顯示這個 DynaRun V3 安裝檔裡的授權合約。")
                                : T(L"Please read Dyna Pro's license agreement for DynaRun V3.", L"請閱讀 Dyna Pro 的 DynaRun V3 授權合約。"));
        text(ID_BODY, b);
        SetWindowTextW(H(ID_CHECK), g_nolicense ? T(L"I understand and &accept Dyna Pro's license terms", L"我了解並同意 Dyna Pro 的授權條款(&A)")
                                                : T(L"I &accept the license agreement", L"我接受授權合約(&A)"));
        vis(ID_RICH, TRUE); vis(ID_CHECK, TRUE);
        buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), g_nolicense ? T(L"Open website", L"開啟官網") : NULL);
        EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED);
        SetFocus(H(ID_CHECK));                       // the disabled "Install" button cannot hold the keyboard focus
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_RUNNING: {
        WCHAR names[200];
        running_names(g_lastrun = running_programs(), names);
        text(ID_TITLE, T(L"DynaRun is running", L"DynaRun 正在執行"));
        if (g_lastrun & RUN_NOCLOSE) {               // another user's, or one that cannot be identified: closing from here would not help
            wsprintfW(b, T(L"DynaRun is running for another user, or as a program that cannot be identified, so it cannot be closed from here:\n%s\n"
                           L"Ask that user to close DynaRun; this page then offers \"Install\".",
                           L"另一位使用者（或無法辨識的程式）正在執行 DynaRun，無法從這裡關閉：\n%s\n"
                           L"請該使用者先關閉 DynaRun，關閉後這個頁面會出現「安裝」按鈕。"), names);
            text(ID_BODY, b);
            buttons(NULL, T(L"Cancel", L"取消"), NULL);
        } else {
            wsprintfW(b, T(L"DynaRun must be closed to install:\n%s\n"
                           L"\"Close DynaRun and install\" closes it now.\nA test run in progress stops, unsaved data is lost.",
                           L"安裝前必須關閉 DynaRun：\n%s\n"
                           L"按「關閉 DynaRun 並安裝」會立即關閉它。\n正在進行的測試會中斷，沒存檔的資料會遺失。"), names);
            text(ID_BODY, b);
            buttons(T(L"Close DynaRun and install", L"關閉 DynaRun 並安裝"), T(L"Cancel", L"取消"), NULL);
        }
        if (g_note[0]) status(g_note, TRUE);
        SetTimer(g_hwnd, TIMER_RUNNING, 1000, NULL);
        break;
    }
    case P_INSTALL:
        text(ID_TITLE, T(L"Installing", L"安裝中"));
        text(ID_BODY, T(L"Installing, please wait. This takes a minute or two.\n\nWhen Windows asks whether to allow changes, click \"Yes\".",
                        L"正在安裝，請稍候，大約需要一兩分鐘。\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
        progress(TRUE);
        buttons(NULL, NULL, NULL);
        break;
    case P_DONE:
        text(ID_TITLE, T(L"Done", L"完成"));
        lstrcpyW(b, T(L"Everything is installed.\n\nFrom now on, start DynaRun with the \"DynaRun V3\" icon on the desktop as usual.\n\n"
                      L"The first start asks for your dynamometer model. If Windows asks whether to allow changes, click \"Yes\".",
                      L"全部安裝完成。\n\n以後照常點桌面上的「DynaRun V3」圖示啟動即可。\n\n"
                      L"第一次啟動時會要你選擇馬力機型號；如果 Windows 詢問「是否允許變更」，請按「是」。"));
        pin_hint(b);
        if (g_opts.autostart) lstrcatW(b, T(L"\n\nDynaRun will start automatically when you sign in.", L"\n\n以後開機登入後會自動啟動 DynaRun。"));
        lstrcatW(b, T(L"\n\nIf DynaRun's screen ever looks wrong, close DynaRun and start it again.", L"\n\n如果 DynaRun 的畫面顯示不正常，關掉 DynaRun 再開一次即可。"));
        text(ID_BODY, b);
        buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);
        acp_warning();
        break;
    case P_ERROR:
        text(ID_TITLE, T(L"Something went wrong", L"發生問題"));
        text(ID_BODY, g_note);
        buttons(T(L"Close", L"關閉"), NULL, NULL);
        break;
    case P_DRV_INTRO:
        text(ID_TITLE, T(L"Install the Dyna Pro USB driver", L"安裝 Dyna Pro USB 驅動程式"));
        lstrcpyW(b, T(L"Windows needs Dyna Pro's USB driver to talk to the dynamometer.\n\nThis program will:\n"
                        L"   1.  download the driver from the Dyna Pro website (dynapro.co.uk, about 2 MB)\n"
                        L"   2.  restart the computer once: a menu appears, press 7 on the keyboard\n"
                        L"   3.  come back by itself after you sign in and finish the installation\n\n"
                        L"The driver's setting files (INF) in the vendor's package are not in the Microsoft-signed catalog files that ship with them, so Windows cannot validate them and only accepts the driver while "
                        L"driver signature checking is off for that one start. It is back on by itself at the next restart.\n\n"
                        L"If this PC is protected by BitLocker, the protection is suspended until the driver is installed. "
                        L"It resumes by itself, after at most 3 restarts.",
                        L"要讓電腦和馬力機連線，需要安裝 Dyna Pro 提供的 USB 驅動程式。\n\n接下來會：\n"
                        L"   1.  從 Dyna Pro 官網下載驅動程式（dynapro.co.uk，約 2 MB）\n"
                        L"   2.  請你重新開機一次，開機時會出現一個選單，請按鍵盤上的 7\n"
                        L"   3.  登入後本程式會自動回來，完成安裝\n\n"
                        L"廠商封裝中的驅動程式設定檔（INF）不在隨附的 Microsoft 簽署目錄檔中，Windows 無法驗證，所以只有在這一次開機暫時關閉「驅動程式簽章檢查」時才肯安裝；"
                        L"下次重新開機會自動恢復。\n\n"
                        L"如果這台電腦有啟用 BitLocker，保護會暫停到驅動程式安裝完成為止，之後會自動恢復（最多經過 3 次重新開機）。"));
        if (g_pre) lstrcatW(b, T(L"\n\nDynaRun's fix is installed in the same step, just before the restart, so Windows asks for permission only once.",
                                 L"\n\nDynaRun 的修正會在同一步、重新開機之前一起安裝，所以 Windows 只會詢問一次是否允許變更。"));
        text(ID_BODY, b);
        buttons(T(L"Next", L"下一步"), can_skip_driver() ? T(L"Skip", L"略過") : T(L"Cancel", L"取消"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_PRESS7:
        text(ID_TITLE, T(L"After the restart you will see this: press 7", L"重新開機後會看到這個畫面，請按 7"));
        {
            int bl = g_drv_bl;                          // asked in the worker (not elevated: -1 when WMI will not tell)
            b[0] = 0;
            if (g_drv_allow) lstrcatW(b, T(L"Note: this driver file is not the version DynaRunFix knows; you chose to install it.\n\n", L"注意：這個驅動程式檔和已知版本不同，是你確認要安裝的。\n\n"));
            else if (g_drv_how & DRV_SRC_LOCAL) {
                WCHAR nm[100], t[300]; lstrcpynW(nm, file_name(g_drvzip), 100);   // the longest text: ~95 chars + 99 of nm
                if (g_drv_how & DRV_SRC_DIR) wsprintfW(t, T(L"The unpacked driver folder (%s) was found on this computer and checked: no need to download it.\n\n", L"在這台電腦上找到已解開的驅動程式資料夾（%s），檢查通過，不用重新下載。\n\n"), nm);
                else wsprintfW(t, T(L"The driver (%s) was found on this computer and checked: no need to download it again.\n\n", L"在這台電腦上找到驅動程式（%s），檢查通過，不用重新下載。\n\n"), nm);
                lstrcatW(b, t);
            }
            if (!g_drv_allow && (g_drv_how & DRV_ZIPDIFF)) lstrcatW(b, T(L"The zip differs from the known version, but the driver files are identical: it can be installed.\n\n", L"壓縮檔和已知版本不同，但驅動程式檔案相同，可以安裝。\n\n"));
            if (g_pre) lstrcatW(b, T(L"Click \"Install and restart\": DynaRun's fix is installed and the driver is prepared in one step (Windows asks for permission once), then the computer restarts.\n\n",
                                     L"按「安裝並重新開機」後，會一次完成 DynaRun 修正的安裝和驅動程式的準備（Windows 只會詢問一次是否允許變更），接著電腦重新開機。\n\n"));
            lstrcatW(b, T(L"Tip: take a photo of this page with your phone.\nSave your work in other programs before you restart.",
                          L"建議用手機拍下這一頁。\n按「重新開機」前請先存好其他程式的資料。"));
            if (bl > 0) lstrcatW(b, T(L"\n\nThis computer uses BitLocker: protection is suspended for the restart and turned on again by itself after the driver is installed.",
                                      L"\n\n這台電腦開了 BitLocker，重新開機前會暫時停用保護，驅動裝好後自動恢復。"));
            else if (bl < 0) lstrcatW(b, T(L"\n\nIf this computer uses BitLocker, protection is suspended for the restart and turned on again by itself after the driver is installed.",
                                           L"\n\n如果這台電腦有開 BitLocker，重新開機前會暫時停用保護，驅動裝好後自動恢復。"));
            text(ID_BODY, b);
        }
        show_image(0);
        if (g_pre) buttons(T(L"Install and restart", L"安裝並重新開機"), T(L"Cancel", L"取消"), NULL);   // nothing is installed yet
        else buttons(T(L"Restart now", L"重新開機"), T(L"Later", L"稍後"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_HASH: {                                // a driver zip that is not the file this program knows: the user decides
        static WCHAR src[2 * MAX_PATH + 40];
        if (g_drv_how & DRV_SRC_LOCAL) { lstrcpyW(src, (g_drv_how & DRV_SRC_DIR) ? T(L"found on this computer (unpacked folder): ", L"在這台電腦上找到（已解開的資料夾）：") : T(L"found on this computer: ", L"在這台電腦上找到：")); lstrcatW(src, g_drvzip); }
        else lstrcpyW(src, T(L"downloaded from the Dyna Pro website (dynapro.co.uk)", L"從 Dyna Pro 官網下載"));
        text(ID_TITLE, T(L"The driver differs from the known version", L"驅動程式和已知版本不同"));
        // wsprintfW cuts its output at 1024 characters, so the text is put together with bounded appends (b holds 2600): the advice to
        // cancel comes before the details, and the list of files goes last so that only the end of the list can be lost
        b[0] = 0;
        if (g_drv_how & DRV_SRC_DIR) {                  // an unpacked folder: there is no zip to show a size or hash of
            cat_to(b, 2600, T(L"The driver files in this folder are not the version DynaRunFix knows. Dyna Pro may have updated them, or the files may be damaged or tampered with.\n\n"
                              L"Install them only if you are sure that they come from Dyna Pro. If in doubt, click \"Cancel\".\n\nSource: ",
                              L"這個資料夾裡的驅動程式檔和 DynaRunFix 已知的版本不同。可能是 Dyna Pro 更新了驅動程式，也可能是檔案損毀或被竄改。\n\n"
                              L"確定檔案來自 Dyna Pro 才按「仍要安裝」；不確定請按「取消」。\n\n來源："));
            cat_to(b, 2600, src);
            cat_to(b, 2600, L"\n\n");
        } else {
            WCHAR sz[24];
            wsprintfW(sz, L"%lu", (DWORD)g_drv_size);
            cat_to(b, 2600, T(L"The driver files in this package are not the version DynaRunFix knows. Dyna Pro may have updated them, or the file may be damaged or tampered with.\n\n"
                              L"Install it only if you are sure that it comes from Dyna Pro. If in doubt, click \"Cancel\".\n\nSource: ",
                              L"這個驅動程式包裡的驅動程式檔和 DynaRunFix 已知的版本不同。可能是 Dyna Pro 更新了驅動程式，也可能是檔案損毀或被竄改。\n\n"
                              L"確定檔案來自 Dyna Pro 才按「仍要安裝」；不確定請按「取消」。\n\n來源："));
            cat_to(b, 2600, src);
            cat_to(b, 2600, T(L"\nZip size: ", L"\n壓縮檔大小："));
            cat_to(b, 2600, sz);
            cat_to(b, 2600, T(L" bytes\nZip SHA-256: ", L" 位元組\n壓縮檔 SHA-256："));
            cat_to(b, 2600, g_drv_hex);
            cat_to(b, 2600, T(L"\nKnown zip SHA-256: ", L"\n已知的壓縮檔 SHA-256："));
            cat_to(b, 2600, driver_pinned_sha256());
            cat_to(b, 2600, L"\n\n");
        }
        cat_to(b, 2600, T(L"Files that differ (actual / expected):\n", L"不同的檔案（實際 / 已知）：\n"));
        {
            int room = 2600 - lstrlenW(b) - 48;        // keep room for the "and N more" line
            const WCHAR *p = g_drv_rep;
            int used = 0, more = 0;
            while (*p) {
                const WCHAR *e = p; int n;
                while (*e && *e != L'\n') e++;
                n = (int)(e - p) + (*e ? 1 : 0);        // the line with its line feed
                if (!more && used + n <= room) { lstrcpynW(b + lstrlenW(b), p, n + 1); used += n; }
                else more++;
                p += n;
            }
            if (more) {
                WCHAR t[48];
                wsprintfW(t, T(L"  (and %d more)", L"  （另外還有 %d 個）"), more);
                cat_to(b, 2600, t);
            }
        }
        text(ID_BODY, b);
        // "Cancel" is the default button (the right-most one, Enter); "Install anyway" is the second; the website is bottom-left
        buttons(T(L"Cancel", L"取消"), T(L"Install anyway", L"仍要安裝"), T(L"Open website", L"開啟官網"));
        if (g_note[0]) status(g_note, TRUE);
        break;
    }
    case P_DRV_BDE:
        text(ID_TITLE, T(L"BitLocker could not be suspended", L"BitLocker 沒有暫停成功"));
        text(ID_BODY, T(L"This computer uses BitLocker, and its protection could not be suspended for the restart. After the restart Windows will probably ask for the "
                        L"BitLocker recovery key before it shows the start-up menu.\n\nYou can look the key up at https://aka.ms/myrecoverykey (Microsoft account), or ask your administrator. "
                        L"Without the key, choose \"Later\", suspend BitLocker yourself (Settings > Privacy & security > Device encryption, or Control Panel > BitLocker) and try again.",
                        L"這台電腦有開 BitLocker，但重新開機前無法暫停它的保護。重新開機後 Windows 很可能會先要求輸入 BitLocker 修復金鑰（「您必須輸入修復金鑰以存取啟動設定」），才會顯示選單。\n\n"
                        L"修復金鑰可到 https://aka.ms/myrecoverykey 用微軟帳戶查詢，或詢問系統管理員。沒有金鑰的話，請按「稍後」，自己先暫停 BitLocker（設定 → 隱私權與安全性 → 裝置加密，或控制台 → BitLocker）再試一次。"));
        buttons(T(L"Restart anyway", L"仍要重新開機"), T(L"Later", L"稍後"), T(L"Find the recovery key", L"查詢修復金鑰"));
        break;
    case P_DRV_PREP:
        text(ID_TITLE, T(L"Please wait", L"請稍候"));
        text(ID_BODY, T(L"Getting ready...\n\nWhen Windows asks whether to allow changes, click \"Yes\".", L"正在準備…\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
        progress(TRUE);
        buttons(NULL, NULL, NULL);
        break;
    case P_DRV_REBOOT:
        text(ID_TITLE, g_drv_noreboot ? T(L"Please restart the computer", L"請重新開機") : T(L"Restarting", L"即將重新開機"));
        lstrcpyW(b, g_drv_noreboot
            ? T(L"Everything is ready, but Windows would not restart by itself.\n\nPlease restart the computer yourself (Start, the power button, Restart). "
                L"When the menu appears, press 7. After you sign in, this program continues by itself.",
                L"都準備好了，但 Windows 沒有自動重新開機。\n\n請自己重新開機（開始 → 電源 → 重新啟動）。出現選單時請按 7；登入後本程式會自動繼續。")
            : T(L"The computer restarts now.\n\nWhen the menu appears, press 7. After you sign in, this program continues by itself.\n\n"
                L"If the computer does not restart (a program is waiting for you), save your work and restart it yourself.",
                L"電腦即將重新開機。\n\n出現選單時請按 7；登入後本程式會自動繼續。\n\n如果電腦沒有重新開機（有程式在等你回應），請存好資料後自己重新開機。"));
        if (g_fix_done) pin_hint(b);                      // the fix was installed with the driver: the hints of the page "Done", which is skipped
        text(ID_BODY, b);
        buttons(T(L"Close", L"關閉"), NULL, NULL);
        if (g_fix_done) acp_warning();
        break;
    case P_DRV_RETRY:
        text(ID_TITLE, T(L"7 was not pressed", L"剛才沒有按到 7"));
        text(ID_BODY, T(L"The restart menu was skipped, so Windows still checks driver signatures and the driver cannot be installed yet.\n\n"
                        L"Try again? The computer restarts once more. When the menu appears, press 7.",
                        L"剛才重新開機時沒有選到「停用驅動程式強制簽章」（按 7），所以 Windows 還在檢查簽章，驅動程式現在還不能安裝。\n\n"
                        L"要再試一次嗎？電腦會再重新開機一次，出現選單時請按 7。"));
        buttons(T(L"Retry", L"重試"), T(L"Cancel", L"取消"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_WARN:
        text(ID_TITLE, T(L"Last step: install the driver", L"最後一步：安裝驅動程式"));
        text(ID_BODY, T(L"Driver signature checking is off for this start. Next, Windows will show a red security window like this one twice (once for each driver file): click \"Install this driver software anyway\" each time.",
                        L"這次開機已暫時關閉簽章檢查。接下來 Windows 會跳出像下面這樣的紅色安全性視窗兩次（兩個驅動程式檔各一次），請每次都按「仍然安裝此驅動程式軟體」。"));
        show_image(1);
        buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_INSTALL:
        text(ID_TITLE, T(L"Installing the driver", L"正在安裝驅動程式"));
        text(ID_BODY, T(L"Installing, please wait.\n\nIf Windows shows a red warning, choose \"Install this driver software anyway\".\nIf Windows asks whether to allow changes, click \"Yes\".",
                        L"正在安裝，請稍候。\n\n如果 Windows 跳出紅色警告，請選「仍然安裝此驅動程式軟體」。\nWindows 詢問「是否允許變更」時，請按「是」。"));
        progress(TRUE);
        buttons(NULL, NULL, NULL);
        break;
    case P_DRV_DONE:
        text(ID_TITLE, g_drv_have ? T(L"The driver is already installed", L"驅動程式已安裝") : T(L"The driver is installed", L"驅動程式安裝完成"));
        text(ID_BODY, g_drv_have
            ? T(L"The Dyna Pro USB driver is already installed on this computer. Nothing more to do.",
                L"這台電腦已經安裝過 Dyna Pro USB 驅動程式，不需要再裝。")
            : T(L"Connect the Dyna Pro dynamometer by USB and switch it on: Windows recognizes it by itself.\n\n"
                L"Driver signature checking comes back by itself the next time you restart the computer normally. Nothing else to set.",
                L"請用 USB 接上 Dyna Pro 馬力機並開機，Windows 會自動認得它。\n\n"
                L"驅動程式簽章檢查會在下次正常重新開機後自動恢復，不需要再做任何設定。"));
        if (can_start_dynarun()) buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);   // only after a success (this page); a failure shows P_DRV_ERROR
        else buttons(T(L"Close", L"關閉"), NULL, NULL);
        break;
    case P_DRV_ERROR:
        text(ID_TITLE, T(L"The driver was not installed", L"驅動程式沒有安裝"));
        text(ID_BODY, g_note);
        buttons(T(L"Close", L"關閉"), NULL, T(L"Open the manual steps", L"開啟手動安裝說明"));
        break;
    }
    g_note[0] = 0;
    g_building = FALSE;
    layout();
    if (!GetFocus() || !IsWindowEnabled(GetFocus()) || !(GetWindowLongW(GetFocus(), GWL_STYLE) & WS_VISIBLE))
        SetFocus(GetNextDlgTabItem(g_hwnd, NULL, FALSE));       // something must hold the keyboard focus
}

/* ---------- worker thread ---------- */

static void on_progress(DWORD done, DWORD total)
{
    int pct = total ? MulDiv(done, 1000, total) : 0;
    if (done == (DWORD)-1) { PostMessageW(g_hwnd, WM_PROGRESS, done, total); return; }   // the "checking" phase (driver_fetch)
    if (pct == g_lastpct) return;
    g_lastpct = pct;
    PostMessageW(g_hwnd, WM_PROGRESS, done, total);
}

// What a job runs on the worker thread (job_proc), and what is done with its result on the UI thread (JOBS[].on_done, below).
static int jp_acquire(void)                          // the DynaRun setup or the driver: a copy on this computer first, else the download
{
    int rc;
    on_progress((DWORD)-1, 2);                       // the page shows "looking" (a moving bar) until a download starts
    if (g_acq == &ACQ_DRIVER) {
        rc = driver_fetch(g_drvzip, on_progress, &g_cancel, &g_neterr, &g_drv_how, g_drv_hex, &g_drv_size, g_drv_rep, 1500);
        if (rc == PK_OK || rc == DRV_W_HASH) {
            on_progress((DWORD)-1, 1);               // the page says "checking" (marquee) while the BitLocker state is asked
            g_drv_bl = driver_bitlocker();           // here, not while the next page is built: the WMI query can block for seconds
        }
    } else {
        g_pkg_found = g_acq_local_ok && pkg_find_local(g_pkg);
        if (g_pkg_found) rc = PK_OK;
        else { g_pkg_ours = TRUE; rc = pkg_download(g_pkg, on_progress, &g_cancel, &g_neterr); }
    }
    return rc;
}
static int jp_extract(void)
{
    int rc = pkg_extract_checked(&g_zip, g_pw, g_msi, 400UL * 1024UL * 1024UL, on_progress, &g_cancel);   // a size cap, and a free target (shared with the driver)
    SecureZeroMemory(g_pw, sizeof(g_pw));            // the password is not needed any more
    return rc;
}
static int jp_install_all(void) { return g_pre ? install_all_driver(&g_jobopts, &g_result, g_msi, g_drvzip, g_drv_allow ? g_drv_hex : NULL) : install_all(&g_jobopts, &g_result, g_msi); }   // g_pre: the driver's preparation in the same elevated stage
static int jp_install_fix(void) { return g_pre ? install_fix_driver(&g_jobopts, &g_result, g_drvzip, g_drv_allow ? g_drv_hex : NULL) : install_fix(&g_jobopts, &g_result); }
static int jp_drv_prep(void)
{
    // "install anyway" accepts exactly this zip (its SHA-256), nothing else.
    return driver_prep(&g_jobopts, g_drvzip[0] ? g_drvzip : L"-", g_drv_allow ? g_drv_hex : NULL);
}
static int jp_drv_install(void) { return driver_install(&g_jobopts); }
static int jp_drv_cancel(void) { return driver_cancel(&g_jobopts); }

static void jd_acquire(int job, int rc);
static void jd_extract(int job, int rc);
static void jd_install(int job, int rc);
static void jd_drv_prep(int job, int rc);
static void jd_drv_install(int job, int rc);
static void jd_drv_cancel(int job, int rc);

// One row per job, indexed by the JOB_* value (nothing depends on the order of the values).
//   proc        runs on the worker thread; returns the job's result code
//   on_done     runs on the UI thread when the worker has ended (job_done), with the job and its result
//   keep_up     the elevated stage or a system tool works: the UAC prompt and msiexec's window take the foreground, so ID_KEEPUP keeps the wizard behind them
//   bring_front the wizard comes back in front of other windows when the job is done (the elevated part allows it)
typedef struct {
    int (*proc)(void);
    void (*on_done)(int job, int rc);
    BOOL keep_up, bring_front;
} job_desc;
static const job_desc JOBS[JOB_COUNT] = {
    [JOB_ACQUIRE]     = { jp_acquire,     jd_acquire,     FALSE, FALSE },
    [JOB_EXTRACT]     = { jp_extract,     jd_extract,     FALSE, FALSE },
    [JOB_INSTALL_ALL] = { jp_install_all, jd_install,     TRUE,  TRUE  },
    [JOB_INSTALL_FIX] = { jp_install_fix, jd_install,     TRUE,  TRUE  },
    [JOB_DRV_PREP]    = { jp_drv_prep,    jd_drv_prep,    TRUE,  TRUE  },
    [JOB_DRV_INSTALL] = { jp_drv_install, jd_drv_install, TRUE,  TRUE  },
    [JOB_DRV_CANCEL]  = { jp_drv_cancel,  jd_drv_cancel,  TRUE,  TRUE  },
};

static DWORD WINAPI job_proc(LPVOID unused)
{
    int rc = JOBS[g_job].proc();
    PostMessageW(g_hwnd, WM_JOBDONE, g_job, rc);
    (void)unused;
    return 0;
}

// The update hint is asked in the background when the first page shows an installed DynaRun; the answer comes as WM_HINT. Never waited for.
static DWORD WINAPI hint_proc(LPVOID unused)
{
    static WCHAR i[40], n[40]; int r;
    r = pkg_update_hint(i, n, 40, user_log);
    if (r) { lstrcpyW(g_hint_inst, i); lstrcpyW(g_hint_new, n); }
    if (g_hwnd) PostMessageW(g_hwnd, WM_HINT, (WPARAM)r, 0);
    (void)unused;
    return 0;
}

static void start_job(int job, int page)
{
    DWORD tid;
    g_job = job; g_cancel = 0; g_busy = TRUE;
    if (job == JOB_ACQUIRE && g_dlacq != g_acq) { g_dlacq = g_acq; g_dltotal = g_dlhave = 0; }   // a resume of the same download keeps what is known
    set_page(page);
    g_jobopts = g_opts;                              // the job runs with the options as they are now
    g_thread = CreateThread(NULL, 0, job_proc, NULL, 0, &tid);
    if (JOBS[job].keep_up) SetTimer(g_hwnd, ID_KEEPUP, 300, NULL);
}

/* ---------- steps ---------- */

static void open_package(void);

typedef struct { const char *p; LONG left; } rtfsrc;
static DWORD CALLBACK rtf_in(DWORD_PTR c, LPBYTE b, LONG n, LONG *got)
{
    rtfsrc *s = (rtfsrc *)c; LONG i, k = n < s->left ? n : s->left;
    for (i = 0; i < k; i++) b[i] = (BYTE)s->p[i];
    s->p += k; s->left -= k; *got = k;
    return 0;
}

// The install jobs start only once DynaRun is closed (see running_programs in setup.c).
static void install(int job)
{
    g_pending = job;
    if (running_programs()) set_page(P_RUNNING);
    else start_job(job, P_INSTALL);
}

// The install of the fix (job: JOB_INSTALL_ALL / JOB_INSTALL_FIX). With the driver ticked its package is fetched and checked first (the driver pages), and the
// one elevated stage then does both: the UAC prompt comes once, before the restart. The driver-only first page (g_driver_only) has no install: it starts the prep itself.
static void go_install(int job)
{
    if (g_drv && !g_driver_only && g_drvmode == DRV_MODE_NONE && !g_pre) { g_pre = TRUE; g_inst_job = job; g_note[0] = 0; set_page(P_DRV_INTRO); }
    else install(job);
}

// "Skip" on the driver pages of the main flow: the fix is installed without the driver (the sign-in start is then registered by the install itself).
static void skip_driver(void)
{
    user_log(L"driver: skipped, the fix is installed without it");
    g_drv = FALSE; g_opts.autostart_later = FALSE; g_pre = FALSE;
    install(g_inst_job);
}

// Starts the acquire flow for the DynaRun setup or the driver (the same pages; a network check comes first inside the download).
static void acq_start(const acq_desc *d)
{
    g_acq = d;
    if (d == &ACQ_SETUP) { g_pkg_ours = g_pkg_found = FALSE; g_acq_local_ok = !g_acq_fresh && g_nfound < 8; }   // the cap: pkg_reject remembers 8 files
    else { g_drvzip[0] = 0; g_drv_allow = FALSE; }
    start_job(JOB_ACQUIRE, P_FETCH);
}

// Whatever the file is called: only the real DynaRun setup is ever installed.
static void not_dynarun(void)
{
    WCHAR m[MAX_PATH], nm[100];
    g_acq = &ACQ_SETUP;
    lstrcpynW(nm, file_name(g_pkg), 100);       // wsprintfW does not check the size of g_note: a long file name must not overflow it
    wsprintfW(g_note, T(L"\"%s\" is not the DynaRun V3 setup, so it will not be installed.",
                        L"「%s」不是 DynaRun V3 的安裝檔，所以不會安裝它。"), nm);
    pkg_reject(g_pkg);
    pkg_temp_dir(m); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(g_msi, m)) DeleteFileW(g_msi);   // our extracted copy
    if (g_pkg_ours) DeleteFileW(g_pkg);
    if (g_pkg_found) { acq_start(&ACQ_SETUP); return; }   // we picked it, not the user: quietly try the next one or download
    g_badfile = TRUE;
    set_page(P_FAIL);
}

static void fmt_size(WCHAR *o, ULONGLONG n)
{
    DWORD v = (DWORD)n;
    wsprintfW(o, T(L"%lu bytes (%lu.%lu MB)", L"%lu 位元組（%lu.%lu MB）"), v, v / 1048576, v % 1048576 / 104858);
}

// Shown on the license page so the user can compare with what Dyna Pro provides. Display only: nothing here blocks the install.
static void build_info(void)
{
    WCHAR hex[65], hex2[65], ver[64], mfr[128], sz[80]; ULONGLONG size = 0, size2 = 0; BOOL same = !lstrcmpiW(g_pkg, g_msi), okp, okm = FALSE;
    HCURSOR old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    okp = pkg_sha256(g_pkg, hex, &size);
    if (!same) okm = pkg_sha256(g_msi, hex2, &size2);
    msi_info(g_msi, ver, mfr, 64);
    SetCursor(old);
    g_info[0] = 0;
    lstrcatW(g_info, T(L"Source:  ", L"來源：")); 
    lstrcatW(g_info, g_pkg_ours ? T(L"downloaded now from dynapro.co.uk", L"剛從 dynapro.co.uk 下載")
                     : g_pkg_found ? T(L"found on this computer, not downloaded by DynaRunFix", L"在這台電腦上找到，不是 DynaRunFix 下載的")
                     : T(L"chosen by you", L"你選擇的檔案"));
    lstrcatW(g_info, T(L"\r\n\r\nFile:\r\n", L"\r\n\r\n檔案路徑：\r\n")); lstrcatW(g_info, g_pkg);
    fmt_size(sz, size);
    lstrcatW(g_info, T(L"\r\n\r\nSize:  ", L"\r\n\r\n大小：")); lstrcatW(g_info, sz);
    lstrcatW(g_info, L"\r\nSHA-256:\r\n"); lstrcatW(g_info, okp ? hex : L"?");
    if (!same) {
        fmt_size(sz, size2);
        lstrcatW(g_info, T(L"\r\n\r\nSetup.msi inside the file, size:  ", L"\r\n\r\n檔案裡的 Setup.msi，大小：")); lstrcatW(g_info, sz);
        lstrcatW(g_info, L"\r\nSHA-256:\r\n"); lstrcatW(g_info, okm ? hex2 : L"?");
    }
    lstrcatW(g_info, L"\r\n\r\nMSI ProductVersion:  "); lstrcatW(g_info, ver[0] ? ver : L"?");
    lstrcatW(g_info, L"\r\nManufacturer:  "); lstrcatW(g_info, mfr[0] ? mfr : L"?");
}

static void after_extract(void)
{
    if (msi_check(g_msi, g_ver, 64) != MSI_DYNARUN) { not_dynarun(); return; }
    release(g_rtf);
    g_rtf = msi_license_rtf(g_msi);
    g_nolicense = !g_rtf;
    build_info();
    if (g_rtf) {
        rtfsrc src; EDITSTREAM es;
        src.p = g_rtf; src.left = lstrlenA(g_rtf);
        es.dwCookie = (DWORD_PTR)&src; es.dwError = 0; es.pfnCallback = rtf_in;
        SendMessageW(H(ID_RICH), EM_STREAMIN, SF_RTF, (LPARAM)&es);
    } else                                       // never install without the user accepting Dyna Pro's terms
        SetWindowTextW(H(ID_RICH), T(L"DynaRun V3 is licensed to you by Dyna Pro Dynamometers Ltd under their end-user license agreement "
                                     L"and their terms and conditions of sale. The agreement is part of Dyna Pro's setup, but this "
                                     L"installer could not read it from this setup file.\r\n\r\n"
                                     L"To read it, contact Dyna Pro (support@dynapro.co.uk) or see their website "
                                     L"(\"Open website\" below).\r\n\r\n"
                                     L"Install only if you accept Dyna Pro's license terms.",
                                     L"DynaRun V3 是 Dyna Pro Dynamometers Ltd 依其使用者授權合約及銷售條款授權給你使用的軟體。"
                                     L"授權合約包含在 Dyna Pro 的安裝檔中，但本安裝程式無法從這個安裝檔讀出合約內容。\r\n\r\n"
                                     L"如需閱讀合約，請聯絡 Dyna Pro（support@dynapro.co.uk）或參考其官網（下方「開啟官網」）。\r\n\r\n"
                                     L"請在同意 Dyna Pro 的授權條款後再安裝。"));
    SendMessageW(H(ID_CHECK), BM_SETCHECK, BST_UNCHECKED, 0);
    set_page(P_CHECK);
}

static void open_package(void)
{
    if (msi_check(g_pkg, NULL, 0) != MSI_NONE) { lstrcpyW(g_msi, g_pkg); after_extract(); return; }   // by content, not by extension
    if (zip_open(g_pkg, &g_zip) != PK_OK) { g_msi[0] = 0; not_dynarun(); return; }
    pkg_temp_dir(g_msi); lstrcatW(g_msi, L"\\Setup.msi");
    if (g_zip.flags & 1) set_page(P_PASSWORD);
    else { g_pw[0] = 0; start_job(JOB_EXTRACT, P_EXTRACT); }
}

// The ZIP password is bytes; try the usual encodings of what was typed.
static BOOL take_password(void)
{
    static const UINT cps[] = { CP_OEMCP, CP_ACP, CP_UTF8 };
    WCHAR w[128]; int i; BOOL ok = FALSE;
    GetWindowTextW(H(ID_EDIT), w, 128);
    if (!w[0]) return FALSE;
    for (i = 0; i < 3 && !ok; i++)
        ok = WideCharToMultiByte(cps[i], 0, w, -1, g_pw, sizeof(g_pw), NULL, NULL) && zip_password_plausible(&g_zip, g_pw);
    SecureZeroMemory(w, sizeof(w));
    if (!ok) SecureZeroMemory(g_pw, sizeof(g_pw));
    else SetWindowTextW(H(ID_EDIT), L"");           // the typed password does not stay in the edit control (g_pw is wiped after the extraction)
    return ok;
}

static void choose_file(void)
{
    OPENFILENAMEW of; WCHAR f[MAX_PATH];
    zero(&of, sizeof(of)); f[0] = 0;
    of.lStructSize = sizeof(of); of.hwndOwner = g_hwnd;
    of.lpstrFilter = T(L"DynaRun V3 setup (*.zip;*.msi)\0*.zip;*.msi\0All files\0*.*\0", L"DynaRun V3 安裝檔 (*.zip;*.msi)\0*.zip;*.msi\0所有檔案\0*.*\0");
    of.lpstrFile = f; of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    if (g_pkg_ours && lstrcmpiW(g_pkg, f)) DeleteFileW(g_pkg);   // the download that turned out not to be wanted
    lstrcpyW(g_pkg, f); g_pkg_ours = g_pkg_found = FALSE;
    open_package();
}

static void choose_exe(void)
{
    OPENFILENAMEW of; WCHAR f[MAX_PATH];
    zero(&of, sizeof(of)); f[0] = 0;
    of.lStructSize = sizeof(of); of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"DynaRun V3.exe\0DynaRun V3.exe\0*.exe\0*.exe\0";
    of.lpstrFile = f; of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    lstrcpyW(g_exe, f); g_installed = TRUE;
    set_page(P_HELLO);
}

static void drop_msi(void)                           // the decrypted Setup.msi in our temp folder (the user's own MSI is never touched)
{
    WCHAR d[MAX_PATH], m[MAX_PATH];
    pkg_temp_dir(d); lstrcpyW(m, d); lstrcatW(m, L"\\Setup.msi");
    if (g_msi[0] && !lstrcmpiW(g_msi, m)) DeleteFileW(g_msi);
    RemoveDirectoryW(d);                             // only when empty
}

static void cleanup_files(void)
{
    WCHAR d[MAX_PATH], m[MAX_PATH];
    pkg_temp_dir(d); lstrcpyW(m, d); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(g_msi, m)) DeleteFileW(g_msi);
    if (g_pkg_ours) DeleteFileW(g_pkg);
    RemoveDirectoryW(d);
}

/* ---------- the driver (driver.c) ---------- */

// The text of a failed driver step (rc: PK_* of the download, DRV_E_* of driver.c) and the error page.
static void drv_error(int rc)
{
    switch (rc) {
    case DRV_E_HASH:
        lstrcpyW(g_note, T(L"The downloaded file is not the one this program was made for (its SHA-256 differs), so it was not installed. Dyna Pro may have changed the file.",
                           L"下載到的檔案和這個程式預期的不一樣（SHA-256 不符），為了安全沒有安裝。可能是 Dyna Pro 更新了檔案。"));
        break;
    case DRV_E_BCD: lstrcpyW(g_note, T(L"Windows could not be told to show its start-up menu at the next start (bcdedit failed).", L"無法設定「下次開機顯示進階選單」（bcdedit 失敗）。")); break;
    case DRV_E_TASK: lstrcpyW(g_note, T(L"The task that continues the installation after the restart could not be created.", L"無法建立重新開機後繼續安裝的排程工作。")); break;
    case DRV_E_DIR: lstrcpyW(g_note, T(L"The working folder %ProgramData%\\DynaRunFix could not be made safe (an old folder is still in use, or it can be changed by other users). Nothing was installed. Close other programs, restart the PC and try again.",
                                       L"無法把工作資料夾 %ProgramData%\\DynaRunFix 設為安全狀態（舊資料夾仍在使用中，或其他使用者可以修改它）。沒有安裝任何東西。請關閉其他程式、重新啟動電腦後再試一次。")); break;
    case DRV_E_PNP: lstrcpyW(g_note, T(L"Windows did not accept the driver. Driver signature checking may still have been on.", L"Windows 沒有接受這個驅動程式，可能是簽章檢查還開著。")); break;
    default: wsprintfW(g_note, T(L"The driver could not be prepared (code %d).", L"無法準備驅動程式（代碼 %d）。"), rc); break;
    }
    lstrcatW(g_note, T(L"\n\nYou can install the driver by hand instead: click \"Open the manual steps\".", L"\n\n也可以改用手動方式安裝，請按「開啟手動安裝說明」。"));
    if (g_fix_done) lstrcatW(g_note, T(L"\n\nDynaRun's fix itself is installed. To try the driver again later, use \"Install Dyna Pro USB driver\" in the Start menu.",
                                       L"\n\nDynaRun 的修正本身已經安裝好了。之後想再試一次驅動程式，可以用開始功能表裡的「安裝 Dyna Pro USB 驅動程式」。"));
    set_page(P_DRV_ERROR);
}

static void drv_reboot_now(void)
{
    g_drv_noreboot = !driver_reboot();
    set_page(P_DRV_REBOOT);
}

// /driver and /driver-continue: the first page
static void drv_start(void)
{
    if (!driver_supported()) {
        DWORD b = os_build();         // 9200+: Windows 8 / 8.1, 6000+: Vista / 7, below: XP
        lstrcpyW(g_note, T(L"This version of Windows is too old for the automatic installation (it needs Windows 10 or later). Install the driver by hand:\n\n",
                           L"這個 Windows 版本太舊，無法自動安裝（需要 Windows 10 或更新版本）。請手動安裝：\n\n"));
        if (b >= 9200)
            lstrcatW(g_note, T(L"Windows 8 / 8.1: hold Shift and click Restart, then Troubleshoot > Advanced options > Startup Settings > Restart, and press 7 (Disable driver signature enforcement; 32-bit editions do not enforce driver signing and only show the warning, so they can skip this). "
                               L"Then install the driver in Device Manager (Update driver > Browse my computer) and choose \"Install this driver software anyway\" on the red warning.",
                               L"Windows 8 / 8.1：按住 Shift 再按「重新啟動」，依序選 疑難排解 > 進階選項 > 啟動設定 > 重新啟動，然後按 7（停用驅動程式強制簽章；32 位元版不強制驅動程式簽章、只會顯示警告，可略過這一步）。"
                               L"接著在裝置管理員安裝驅動程式（更新驅動程式 > 瀏覽電腦上的驅動程式），並在紅色警告選「仍然安裝此驅動程式軟體」。"));
        else if (b >= 6000)
            lstrcatW(g_note, T(L"Windows Vista / 7: restart and press F8 repeatedly before the Windows logo appears, choose \"Disable Driver Signature Enforcement\" in Advanced Boot Options "
                               L"(32-bit editions do not enforce driver signing and only show the warning, so they can skip this). Then in Device Manager use Update Driver > Browse my computer > Have Disk, and choose \"Install this driver software anyway\" on the red warning.",
                               L"Windows Vista / 7：重新開機，在出現 Windows 標誌之前連續按 F8，在「進階開機選項」選「停用驅動程式強制簽章」（32 位元版不強制驅動程式簽章、只會顯示警告，可略過這一步）。"
                               L"接著在裝置管理員用 更新驅動程式 > 瀏覽電腦 > 從磁片安裝，並在紅色警告選「仍然安裝此驅動程式軟體」。"));
        else
            lstrcatW(g_note, T(L"Windows XP does not enforce driver signatures. Install the driver in Device Manager; when Windows says the software has not passed Windows Logo testing, click \"Continue Anyway\".",
                               L"Windows XP 不強制驅動程式簽章。請在裝置管理員安裝驅動程式；Windows 說軟體未通過 Windows 標誌測試時，按「仍然繼續」。"));
        lstrcatW(g_note, T(L"\n\nThe manual steps have the details: click \"Open the manual steps\".", L"\n\n詳細說明請按「開啟手動安裝說明」。"));
        set_page(P_DRV_ERROR);
        return;
    }
    g_drv_have = driver_installed();
    if (g_drvmode == DRV_MODE_CONTINUE) {
        if (g_drv_have) { if (is_admin()) driver_cancel(&g_opts); set_page(P_DRV_DONE); }       // installed meanwhile: the waiting task goes
        else set_page(driver_ci_state() == DRV_CI_ENFORCED ? P_DRV_RETRY : P_DRV_WARN);
    } else set_page(g_drv_have ? P_DRV_DONE : P_DRV_INTRO);
}

// The one text for every way the acquire flow can end badly (the DynaRun setup and the driver alike): g_note.
static void acq_note(int rc)
{
    switch (rc) {
    case PK_CANCEL: lstrcpyW(g_note, T(L"The download was stopped.", L"已停止下載。")); break;
    case PK_HTTP: wsprintfW(g_note, T(L"The Dyna Pro website answered with error %lu.", L"Dyna Pro 官網回應錯誤 %lu。"), g_neterr); break;
    case PK_CERT: wsprintfW(g_note, T(L"The security certificate of the Dyna Pro website could not be verified (error %lu), so nothing was downloaded. Check the date and time of this computer.",
                                      L"無法驗證 Dyna Pro 官網的安全憑證（錯誤 %lu），因此沒有下載任何東西。請確認電腦的日期與時間。"), g_neterr); break;
    case PK_URL: lstrcpyW(g_note, T(L"The download was refused: the address did not stay on https://dynapro.co.uk (it was redirected elsewhere or to an unencrypted connection). Nothing was downloaded.",
                                    L"已拒絕下載：連線沒有停留在 https://dynapro.co.uk（被導向其他位置或未加密的連線），因此沒有下載任何東西。")); break;
    case PK_IO: wsprintfW(g_note, T(L"The file could not be saved (error %lu).", L"無法儲存檔案（錯誤 %lu）。"), g_neterr); break;
    case DRV_E_HASH: lstrcpyW(g_note, T(L"That file is not a usable Dyna Pro USB driver package (the driver files in it are missing, or are not Dyna Pro's).",
                                        L"這個檔案不是可用的 Dyna Pro USB 驅動程式套件（裡面缺少驅動程式檔，或不是 Dyna Pro 的）。")); break;
    default:                                         // PK_NET and anything unforeseen: the first real error of the attempts
        if (g_neterr == ERROR_INTERNET_DISCONNECTED) lstrcpyW(g_note, T(L"This computer is not connected to a network (Windows reports no connection). Connect it and try again.",
                                                                        L"這台電腦沒有連上網路（Windows 回報沒有網路連線）。請連上網路後再試。"));
        else wsprintfW(g_note, T(L"No connection to the Dyna Pro website (error %lu). Check the internet connection.", L"連不上 Dyna Pro 官網（錯誤 %lu），請確認網路是否正常。"), g_neterr);
        break;
    }
}

// The one error page of the acquire flow.
static void acq_fail(int rc) { acq_note(rc); set_page(P_FAIL); }

// The download worker has ended because "Pause" was clicked: the .part and its ETag are kept, the page stays, the button becomes "Resume".
static void fetch_paused(void)
{
    WCHAR a[24], t[24], s[160];
    g_paused = TRUE; g_pause_req = FALSE;
    fmt_mb(a, g_dlhave);
    if (g_dltotal) { fmt_mb(t, g_dltotal); wsprintfW(s, T(L"Paused: %s of %s MB downloaded.", L"已暫停：已下載 %s / %s MB。"), a, t); }
    else wsprintfW(s, T(L"Paused: %s MB downloaded.", L"已暫停：已下載 %s MB。"), a);
    buttons(T(L"Resume", L"繼續"), T(L"Cancel", L"取消"), T(L"Open website", L"開啟官網"));
    status(s, FALSE);
    layout();
}

// "Cancel" while paused (no worker is running any more): the same result as a cancel during the transfer.
static void fetch_stopped(void) { g_paused = FALSE; acq_fail(PK_CANCEL); }

// The choice on the warning page: install the zip anyway (the elevated step is told, see driver_prep) or cancel. Both are logged.
static void hash_choice(BOOL install)
{
    static WCHAR l[2 * MAX_PATH + 120];
    lstrcpyW(l, install ? L"driver zip with another hash: the user chose to install it anyway: " : L"driver zip with another hash: the user cancelled: ");
    lstrcatW(l, g_drvzip); user_log(l);
    if (install) { g_drv_allow = TRUE; set_page(P_DRV_PRESS7); return; }
    if (!(g_drv_how & DRV_SRC_LOCAL)) driver_forget(g_drvzip);             // our own refused download is not offered again; a file of the user's stays
    if ((g_drv_how & DRV_SRC_LOCAL) && !(g_drv_how & DRV_SRC_PICKED)) { driver_skip_local_other(); user_log(L"the declined copy was found automatically: automatic searches skip such copies from now on"); }   // later tries download
    else if (g_drv_how & DRV_SRC_PICKED) user_log(L"the declined file was chosen by the user: automatic searches are not changed");
    g_drvzip[0] = 0; g_drv_allow = FALSE;
    lstrcpyW(g_note, ((g_drv_how & DRV_SRC_LOCAL) && !(g_drv_how & DRV_SRC_PICKED)) ? T(L"Cancelled: the driver was not installed. Trying again downloads it from the Dyna Pro website.", L"已取消，沒有安裝驅動程式。再試一次會從 Dyna Pro 官網下載。")
                                                 : T(L"Cancelled: the driver was not installed.", L"已取消，沒有安裝驅動程式。"));
    set_page(P_DRV_INTRO);
}

// What the driver check says (from the search, the download or a file the user chose): on to the restart page, to the warning page, or the error page.
static void acq_driver_result(int rc)
{
    if (rc == PK_OK) {
        g_drv_allow = FALSE;
        if (!g_bmp[0]) g_bmp[0] = load_png(18, &g_bw[0], &g_bh[0]);                 // the picture is decoded before the page changes, so the new page is built at once
        set_page(P_DRV_PRESS7);
    } else if (rc == DRV_W_HASH) {
        WCHAR m[2 * MAX_PATH + 100];
        g_drv_allow = FALSE;
        lstrcpyW(m, L"driver package with other files: the warning page is shown for "); lstrcatW(m, g_drvzip); user_log(m);
        set_page(P_DRV_HASH);
    } else acq_fail(rc);
}

// The end of the acquire job. The DynaRun setup goes on to its password / check / license pages (open_package), the driver to its restart or warning page.
static void acq_done(int rc)
{
    BOOL drv = g_acq == &ACQ_DRIVER;
    if ((rc == PK_OK || (drv && rc == DRV_W_HASH)) && g_cancel == 1) {                // "Cancel" while the file was being checked
        if (drv && !(g_drv_how & DRV_SRC_LOCAL) && rc == DRV_W_HASH) driver_forget(g_drvzip);
        acq_fail(PK_CANCEL);
        return;
    }
    if (rc == PK_CANCEL && g_pause_req && g_cancel == 2) { fetch_paused(); return; }
    if (drv) { acq_driver_result(rc); return; }
    if (rc == PK_OK) { if (g_pkg_found) g_nfound++; open_package(); return; }
    acq_fail(rc);
}

// A file dialog for the driver: one dialog, filter "Dyna Pro USB driver (*.zip;*.inf)". IFileOpenDialog where there is one (Vista and later), else the classic dialog.
// The path of the chosen file in p (MAX_PATH); FALSE when the user cancelled.
static BOOL pick_driver_file(WCHAR *p)
{
    static const GUID clsid = { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };   // CLSID_FileOpenDialog
    static const GUID iid = { 0xD57C7288, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 } };      // IID_IFileOpenDialog
    IFileOpenDialog *fd = NULL; HRESULT hr, hrc; BOOL ok = FALSE;
    p[0] = 0;
    hrc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if ((SUCCEEDED(hrc) || hrc == RPC_E_CHANGED_MODE) && SUCCEEDED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&fd))) {
        COMDLG_FILTERSPEC spec[2]; DWORD opt = 0;
        spec[0].pszName = T(L"Dyna Pro USB driver (*.zip;*.inf)", L"Dyna Pro USB 驅動程式 (*.zip;*.inf)"); spec[0].pszSpec = L"*.zip;*.inf";
        spec[1].pszName = T(L"All files", L"所有檔案"); spec[1].pszSpec = L"*.*";
        fd->lpVtbl->SetFileTypes(fd, 2, spec);
        fd->lpVtbl->SetTitle(fd, T(L"Choose the Dyna Pro USB driver: Dyna Pro USB.zip, or ftdibus.inf in the folder where it is unpacked",
                                   L"選擇 Dyna Pro USB 驅動程式：Dyna Pro USB.zip，或已解開它的資料夾裡的 ftdibus.inf"));
        fd->lpVtbl->SetOkButtonLabel(fd, T(L"Use this", L"使用這個"));
        if (SUCCEEDED(fd->lpVtbl->GetOptions(fd, &opt))) fd->lpVtbl->SetOptions(fd, opt | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        hr = fd->lpVtbl->Show(fd, g_hwnd);
        if (SUCCEEDED(hr)) {
            IShellItem *si = NULL;
            if (SUCCEEDED(fd->lpVtbl->GetResult(fd, &si)) && si) {
                WCHAR *name = NULL;
                if (SUCCEEDED(si->lpVtbl->GetDisplayName(si, SIGDN_FILESYSPATH, &name)) && name) {
                    if (lstrlenW(name) < MAX_PATH) { lstrcpyW(p, name); ok = TRUE; }
                    CoTaskMemFree(name);
                }
                si->lpVtbl->Release(si);
            }
        }
        fd->lpVtbl->Release(fd);
    } else {
        OPENFILENAMEW of;
        zero(&of, sizeof(of));
        of.lStructSize = sizeof(of); of.hwndOwner = g_hwnd;
        of.lpstrFilter = T(L"Dyna Pro USB driver (*.zip;*.inf)\0*.zip;*.inf\0All files\0*.*\0", L"Dyna Pro USB 驅動程式 (*.zip;*.inf)\0*.zip;*.inf\0所有檔案\0*.*\0");
        of.lpstrTitle = T(L"Choose Dyna Pro USB.zip, or ftdibus.inf in the folder where it is unpacked", L"選擇 Dyna Pro USB.zip，或已解開它的資料夾裡的 ftdibus.inf");
        of.lpstrFile = p; of.nMaxFile = MAX_PATH; of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        ok = GetOpenFileNameW(&of);
    }
    if (SUCCEEDED(hrc)) CoUninitialize();
    return ok && p[0];
}

// "Choose file..." for the driver: a zip, or an .inf of the folder it is unpacked in (the folder that holds the expected layout is looked for from there, up to
// two levels up); the same check as for a copy found by the search, then the same pages.
static void choose_driver_source(void)
{
    WCHAR p[MAX_PATH], d[MAX_PATH]; int rc, k, n; HCURSOR old;
    if (!pick_driver_file(p)) return;
    n = lstrlenW(p);
    if (n > 4 && !lstrcmpiW(p + n - 4, L".inf")) {                     // an .inf: use the folder it is in, or the first of its parents with the layout (ftdibus.inf, i386, amd64)
        WCHAR *e;
        for (e = p + n; e > p && *e != '\\'; e--);
        *e = 0;                                                        // the file's folder
        lstrcpyW(d, p);
        for (k = 0; k < 3; k++) {
            if (driver_layout_ok(d)) { lstrcpyW(p, d); break; }
            for (e = d + lstrlenW(d); e > d && *e != '\\'; e--);
            if (e == d) break;
            *e = 0;
        }
    }
    { WCHAR l[MAX_PATH + 60]; lstrcpyW(l, L"driver: the user chose "); lstrcatW(l, p); user_log(l); }
    g_acq = &ACQ_DRIVER; lstrcpyW(g_drvzip, p);
    old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    rc = driver_check(g_drvzip, &g_drv_how, g_drv_hex, &g_drv_size, g_drv_rep, 1500);
    g_drv_how |= DRV_SRC_PICKED;                                           // chosen by the user: always checked; declining its warning does not touch the memory of declined auto-found copies
    user_log(L"driver: a file the user chose is checked as it is (the skip of declined auto-found copies does not apply to it)");
    g_drv_bl = driver_bitlocker();
    SetCursor(old);
    acq_driver_result(rc);
}

static void jd_acquire(int job, int rc) { (void)job; acq_done(rc); }

static void jd_extract(int job, int rc)
{
    (void)job;
    if (rc == PK_OK) { after_extract(); return; }
    drop_msi();                                      // a failed or cancelled extraction leaves no decrypted copy
    if (rc == PK_CANCEL) { DestroyWindow(g_hwnd); return; }
    if (g_pkg_found && rc == PK_FORMAT) {            // a copy found on this computer that is damaged (not a usable zip): not offered again; a password or disk problem keeps its own page
        WCHAR nm[100];
        lstrcpynW(nm, file_name(g_pkg), 100);
        pkg_reject(g_pkg);
        g_acq = &ACQ_SETUP; g_pkg_found = FALSE;
        wsprintfW(g_note, T(L"The file found on this computer (%s) is damaged, so it will not be used.", L"在這台電腦上找到的檔案（%s）已損毀，所以不使用它。"), nm);
        set_page(P_FAIL);
        return;
    }
    if (rc == PK_PASSWORD) { lstrcpyW(g_note, T(L"Wrong password, or the file is damaged. Please check the password.", L"密碼不正確，或檔案已損毀。請再確認密碼。")); set_page(P_PASSWORD); return; }
    lstrcpyW(g_note, T(L"The setup could not be unpacked. Is the disk full?", L"無法解開安裝檔，硬碟空間是否不足？"));
    set_page(P_ERROR);
}

static void jd_install(int job, int rc)             // JOB_INSTALL_ALL / JOB_INSTALL_FIX
{
    if (g_pre && FIX_DONE(rc)) {                    // the fix is installed, and the driver step ran in the same elevated stage: its result (as for JOB_DRV_PREP)
        cleanup_files();
        g_fix_done = TRUE;
        if (rc == DRV_SUCCESS) drv_reboot_now();
        else if (rc == DRV_W_BDE) set_page(P_DRV_BDE);
        else drv_error(rc);
        return;
    }
    if (rc == RC_OK) { cleanup_files(); set_page(P_DONE); return; }
    if (rc == RC_RUNNING) { g_pending = job; set_page(P_RUNNING); return; }   // DynaRun was started meanwhile
    if (rc == RC_CANCEL) {
        lstrcpyW(g_note, T(L"Nothing was installed: Windows needs your \"Yes\" to install.", L"沒有安裝：需要在 Windows 詢問時按「是」才能安裝。"));
        set_page(g_pre ? P_DRV_PRESS7 : job == JOB_INSTALL_ALL ? P_LICENSE : P_HELLO);   // g_pre: the page with the button that started it
        return;
    }
    wsprintfW(g_note, T(L"The installation did not finish (code %d).\n\nRestart the computer and run this program again.",
                        L"安裝沒有完成（代碼 %d）。\n\n請重新開機後再執行一次本程式。"), rc);
    set_page(P_ERROR);
}

static void jd_drv_prep(int job, int rc)
{
    (void)job;
    if (rc == DRV_SUCCESS) { drv_reboot_now(); return; }
    if (rc == DRV_W_BDE) { set_page(P_DRV_BDE); return; }      // armed, but BitLocker is still on: the user decides
    if (rc == RC_CANCEL) {
        lstrcpyW(g_note, T(L"Nothing was changed: Windows needs your \"Yes\" first.", L"沒有做任何變更：需要在 Windows 詢問時按「是」。"));
        set_page(g_drvmode == DRV_MODE_CONTINUE ? P_DRV_RETRY : P_DRV_PRESS7);
        return;
    }
    drv_error(rc);
}

static void jd_drv_install(int job, int rc)
{
    (void)job;
    if (rc == DRV_SUCCESS) { g_drv_have = FALSE; set_page(P_DRV_DONE); return; }
    if (rc == RC_CANCEL) { lstrcpyW(g_note, T(L"Nothing was installed: Windows needs your \"Yes\" first.", L"沒有安裝：需要在 Windows 詢問時按「是」。")); set_page(P_DRV_WARN); return; }
    if (rc == DRV_E_ENFORCED) {                        // this start still checks signatures: nothing was installed, the restart stays armed
        lstrcpyW(g_note, T(L"Driver signature checking is still on in this Windows session, so nothing was installed. The restart with key 7 is needed first.", L"這次 Windows 啟動時簽章檢查還開著，所以沒有安裝任何東西。需要先重新開機並按 7。"));
        set_page(P_DRV_RETRY); return;
    }
    drv_error(rc);
}

static void jd_drv_cancel(int job, int rc)
{
    (void)job; (void)rc;
    DestroyWindow(g_hwnd);                           // cancelled (or not allowed to: nothing else can be done from here)
}

static void job_done(int job, int rc)
{
    WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = NULL; g_busy = FALSE;
    if (g_close_asked) { g_opts.close = FALSE; g_close_asked = FALSE; }   // the consent was for the job that has just ended: a later stage must not close a DynaRun started since without asking (/close from the command line stays)
    if (g_closing) { DestroyWindow(g_hwnd); return; }
    JOBS[job].on_done(job, rc);
}

static void on_primary(void)
{
    switch (g_page) {
    case P_HELLO:
        g_opts.pin = SendMessageW(H(ID_PIN), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_opts.autostart = SendMessageW(H(ID_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (g_driver_only) { g_drv = TRUE; g_opts.autostart_later = FALSE; set_page(P_DRV_INTRO); break; }   // the fix is current: straight to the driver
        g_drv = g_drv_avail && SendMessageW(H(ID_DRV), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_opts.autostart_later = g_drv;              // the driver restart comes first: the sign-in start is registered after it
        if (g_installed) go_install(JOB_INSTALL_FIX); else { g_acq_fresh = FALSE; acq_start(&ACQ_SETUP); }
        break;
    case P_FAIL: acq_start(g_acq); break;                                             // Retry / Continue download
    case P_PASSWORD:
        if (!take_password()) {
            status(T(L"Wrong password. Please check it (upper/lower case matters).\nIf the setup file above is not the DynaRun setup, click \"Other file...\".",
                     L"密碼不正確，請再確認（大小寫有差別）。\n如果上面的安裝檔不是 DynaRun 的，請按「選擇其他檔案…」。"), TRUE);
            SetFocus(H(ID_EDIT)); break;
        }
        start_job(JOB_EXTRACT, P_EXTRACT);
        break;
    case P_CHECK: set_page(P_LICENSE); break;
    case P_LICENSE: go_install(JOB_INSTALL_ALL); break;
    case P_RUNNING:
        if (!g_opts.close) { g_opts.close = TRUE; g_close_asked = TRUE; }   // the elevated stage closes DynaRun (see close_programs in setup.c), for this job only
        start_job(g_pending, P_INSTALL);
        break;
    case P_DONE:
        start_dynarun();
        DestroyWindow(g_hwnd);
        break;
    case P_ERROR: DestroyWindow(g_hwnd); break;
    case P_DRV_INTRO: acq_start(&ACQ_DRIVER); break;
    case P_DRV_PRESS7: if (g_pre) install(g_inst_job); else start_job(JOB_DRV_PREP, P_DRV_PREP); break;   // g_pre: the fix and the driver preparation in one elevated stage
    case P_DRV_HASH: hash_choice(FALSE); break;                                       // "Cancel" is the default button
    case P_DRV_RETRY: g_drvzip[0] = 0; start_job(JOB_DRV_PREP, P_DRV_PREP); break;       // the staged driver is still there
    case P_DRV_WARN: start_job(JOB_DRV_INSTALL, P_DRV_INSTALL); break;
    case P_DRV_BDE: drv_reboot_now(); break;                                         // "Restart anyway"
    case P_DRV_DONE:
        if (can_start_dynarun()) start_dynarun();                                     // "Start DynaRun" (otherwise the button reads "Close")
        DestroyWindow(g_hwnd);
        break;
    case P_FETCH:                                                                     // "Pause" / "Resume"
        if (g_paused) { g_paused = FALSE; start_job(JOB_ACQUIRE, P_FETCH); }          // the worker had ended; Range / If-Range continue the .part
        else if (g_busy && !g_pause_req) {
            g_pause_req = TRUE;
            InterlockedExchange(&g_cancel, 2);                                        // 2 = pause (1 = cancel): the worker stops cleanly between two reads and keeps the .part and its ETag
            text(ID_PRIMARY, T(L"Pausing...", L"暫停中…")); EnableWindow(H(ID_PRIMARY), FALSE); layout();
        }
        break;
    case P_DRV_REBOOT: case P_DRV_ERROR: DestroyWindow(g_hwnd); break;
    }
}

static void on_close(void)
{
    if (g_page == P_INSTALL || g_page == P_DRV_PREP || g_page == P_DRV_INSTALL) return;   // msiexec / registry / system tool work must not be interrupted
    if (g_busy) { g_closing = TRUE; InterlockedExchange(&g_cancel, 1); return; }
    // after the restart: closing the window ends the installation like "Cancel" (done at once when this runs elevated, as the task does)
    if (g_drvmode == DRV_MODE_CONTINUE && (g_page == P_DRV_RETRY || g_page == P_DRV_WARN) && is_admin()) driver_cancel(&g_opts);
    if (g_page == P_DRV_BDE) { start_job(JOB_DRV_CANCEL, P_DRV_PREP); return; }       // the task, the menu flag and the suspension were armed: take them back first
    DestroyWindow(g_hwnd);
}

// The second button (and Esc): Cancel / Skip / Later / Close
static void on_second(BOOL button)
{
    if (g_page == P_DRV_HASH) hash_choice(button);                       // the button is "Install anyway"; Esc is a cancel
    else if (g_page == P_FAIL && button && g_acq == &ACQ_DRIVER && can_skip_driver()) skip_driver();   // "Skip": the fix without the driver
    else if (g_page == P_FETCH && button) {
        if (g_paused) fetch_stopped();                  // paused: no worker is running, finish at once
        else InterlockedExchange(&g_cancel, 1);         // stop, then offer the other ways (also overrides a pause that is still taking effect)
    }
    else if (g_page == P_DRV_INTRO && button && can_skip_driver()) skip_driver();   // "Skip": the fix without the driver
    else if (g_page == P_DRV_RETRY || g_page == P_DRV_WARN || g_page == P_DRV_BDE) start_job(JOB_DRV_CANCEL, P_DRV_PREP);          // the task, the menu flag and BitLocker back
    else if (g_page != P_INSTALL) on_close();
}

// The third button (left of the second one): another way on. On the error page: choose the file (or folder) yourself.
static void on_third(void)
{
    if (g_page == P_FAIL) { if (g_acq == &ACQ_DRIVER) choose_driver_source(); else choose_file(); }
    else if (g_page == P_HELLO && g_hint) {                       // "Download new version": the same acquire flow, then the normal install (the MSI upgrades DynaRun)
        g_opts.pin = SendMessageW(H(ID_PIN), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_opts.autostart = SendMessageW(H(ID_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_drv = !g_driver_only && g_drv_avail && SendMessageW(H(ID_DRV), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_opts.autostart_later = g_drv;
        user_log(L"DynaRun update hint: the user chose to download the new version");
        g_acq_fresh = TRUE;                                       // an older package found on this computer would not be the new version
        acq_start(&ACQ_SETUP);
    }
}

/* ---------- window ---------- */

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
    SendMessageW(H(ID_PIN), BM_SETCHECK, g_opts.pin ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(H(ID_AUTO), BM_SETCHECK, g_opts.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
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

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDOK: case ID_PRIMARY: if (IsWindowVisible(H(ID_PRIMARY)) && IsWindowEnabled(H(ID_PRIMARY))) on_primary(); break;
        case IDCANCEL: case ID_SECOND: on_second(LOWORD(w) == ID_SECOND); break;
        case ID_THIRD: on_third(); break;
        case ID_EXTRA:
            if (g_page == P_FAIL || g_page == P_FETCH || g_page == P_CHECK || g_page == P_LICENSE) ShellExecuteW(h, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL);
            else if (g_page == P_DRV_BDE) ShellExecuteW(h, NULL, L"https://aka.ms/myrecoverykey", NULL, NULL, SW_SHOWNORMAL);
            else if (g_page == P_PASSWORD) choose_file();
            else if (g_page == P_HELLO) { if (g_driver_only) { g_driver_only = FALSE; set_page(P_HELLO); } else choose_exe(); }   // "Reinstall the fix..." / "Installed elsewhere..."
            else if (g_page == P_DRV_ERROR) ShellExecuteW(h, NULL, DRIVER_DOC_URL, NULL, NULL, SW_SHOWNORMAL);
            else if (g_page == P_DRV_HASH) ShellExecuteW(h, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL);   // after a failed driver download
            break;
        case ID_CHECK: EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        }
        return 0;
    case WM_PROGRESS: {
        static WCHAR b[100];
        DWORD done = (DWORD)w, total = (DWORD)l;
        if (done == (DWORD)-1) {                    // download over, the file is being checked: not pausable, bar and text say so
            if (g_page == P_FETCH && !g_paused) {
                g_marquee = TRUE;
                progress(TRUE);
                if (total == 2) { WCHAR s[160]; wsprintfW(s, T(L"Looking for %s on this computer...", L"正在尋找電腦上現有的%s…"), ACQ_NAME(g_acq)); status(s, FALSE); }   // before anything is downloaded: a bar that moves
                else {                                                                                       // the file is being checked: not pausable
                    g_checking = TRUE;
                    text(ID_TITLE, T(L"Checking the driver", L"正在檢查驅動程式"));
                    status(T(L"Checking the driver...", L"正在檢查驅動程式…"), FALSE);
                    EnableWindow(H(ID_PRIMARY), FALSE);
                }
                layout();
            }
            return 0;
        }
        if (g_checking) return 0;
        if (total && g_marquee) { g_marquee = FALSE; progress(FALSE); }                                  // the size became known
        else if (!total && !g_marquee && g_page == P_FETCH) { g_marquee = TRUE; progress(TRUE); }   // size unknown: a moving bar and the MB received
        if (!g_marquee) SendMessageW(H(ID_PROG), PBM_SETPOS, total ? MulDiv(done, 1000, total) : 0, 0);
        g_dlhave = done; if (total) g_dltotal = total;
        if (g_page == P_FETCH) {
            if (total != g_total) { fetch_body(total); layout(); }
            wsprintfW(b, total ? L"%lu.%lu / %lu.%lu MB" : L"%lu.%lu MB", done / 1048576, done % 1048576 / 104858,
                      total / 1048576, total % 1048576 / 104858);
            status(b, FALSE);
        }
        return 0;
    }
    case WM_TIMER:                                  // the page follows DynaRun being closed (or started again)
        if (w == TIMER_RUNNING && g_page == P_RUNNING) {
            int run = running_programs();
            if (run != g_lastrun) {
                g_lastrun = run;
                if (run) set_page(P_RUNNING);
                else {                               // closed by the user meanwhile: plain "Install"
                    buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
                    status(T(L"DynaRun is closed.", L"DynaRun 已關閉。"), FALSE);
                    layout();
                }
            }
        }
        if (w == ID_KEEPUP) keep_up(h);
        return 0;
    case WM_HINT:
        g_hint = (int)w;
        if (g_hint && g_page == P_HELLO && !g_busy) set_page(P_HELLO);
        return 0;
    case WM_JOBDONE:
        KillTimer(h, ID_KEEPUP);
        // after the elevated part (which allows it) the wizard comes back in front of other windows
        if (JOBS[w].bring_front) bring_front(h);
        job_done((int)w, (int)l); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; RECT c; HDC dc = BeginPaint(h, &ps); HPEN pen, old;
        GetClientRect(h, &c); c.top = c.bottom - S(FOOTER);
        FillRect(dc, &c, g_grey);
        pen = CreatePen(PS_SOLID, 1, RGB(223, 223, 223)); old = SelectObject(dc, pen);
        MoveToEx(dc, 0, c.top, NULL); LineTo(dc, c.right, c.top);
        SelectObject(dc, old); DeleteObject(pen);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)g_grey;                     // push buttons sit on the grey footer
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w, RGB(255, 255, 255));
        SetTextColor((HDC)w, (HWND)l == H(ID_STATUS) && g_status_err ? RGB(192, 0, 0) : GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)g_white;
    case WM_CLOSE: on_close(); return 0;
    case WM_DESTROY: SecureZeroMemory(g_pw, sizeof(g_pw)); SetWindowTextW(H(ID_EDIT), L""); drop_msi(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int wizard(const install_opts *init)
{
    INITCOMMONCONTROLSEX icc; WNDCLASSW wc; NONCLIENTMETRICSW nm; RECT r; MSG msg; HDC dc; LOGFONTW lf;
    static WCHAR title[100];
    g_opts = *init;                                  // the command line's options; the pages change this copy
    icc.dwSize = sizeof(icc); icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    dc = GetDC(NULL); g_dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
    zero(&nm, sizeof(nm));
#pragma warning(suppress: 4996)
    nm.cbSize = LOBYTE(GetVersion()) >= 6 ? sizeof(nm) : sizeof(nm) - sizeof(int);   // XP: no iPaddedBorderWidth
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, nm.cbSize, &nm, 0);
    lf = nm.lfMessageFont; pick_face(&lf); lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfHeight = -S(17); lf.lfWeight = FW_NORMAL; g_font = CreateFontIndirectW(&lf);   // larger than usual: easy to read
    lf.lfHeight = -S(28); lf.lfWeight = FW_BOLD; g_big = CreateFontIndirectW(&lf);
    g_white = CreateSolidBrush(RGB(255, 255, 255));
    g_grey = CreateSolidBrush(RGB(243, 243, 243));

    zero(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"DynaRunFixSetup";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = g_white;
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(10));
    RegisterClassW(&wc);
    r.left = r.top = 0; r.right = S(620); r.bottom = S(580);                                  // the first page: text, disclaimer, the two options and a one-line note
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    wsprintfW(title, T(L"DynaRunFix Setup %s", L"DynaRunFix 安裝程式 %s"), WIDEN(DRF_DISPLAY));
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                             (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2, (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 1;
    create_controls();
    g_installed = locate_dynarun();
    g_drv_avail = g_drvmode == DRV_MODE_NONE && driver_supported() && !driver_installed();
    g_driver_only = g_installed && g_drv_avail && fix_current();           // the fix is there and current: only the driver is missing
    if (g_drvmode != DRV_MODE_NONE) drv_start();
    else {
        DWORD tid;
        set_page(P_HELLO);
        if (g_installed) CloseHandle(CreateThread(NULL, 0, hint_proc, NULL, 0, &tid));      // is there a newer DynaRun? (a hint; the page does not wait)
    }
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {          // Tab between the visible controls
            HWND n = GetNextDlgTabItem(g_hwnd, GetFocus(), GetKeyState(VK_SHIFT) < 0);
            if (n) SetFocus(n);
            continue;
        }
        if (msg.message == WM_SYSKEYDOWN && msg.wParam == 'A' && g_page == P_LICENSE) {   // Alt+A: "I accept"
            SendMessageW(H(ID_CHECK), BM_CLICK, 0, 0);
            continue;
        }
        if (msg.message == WM_SYSKEYDOWN && (msg.wParam == 'T' || msg.wParam == 'S') && g_page == P_HELLO) {   // Alt+T / Alt+S
            SendMessageW(H(msg.wParam == 'T' ? ID_PIN : ID_AUTO), BM_CLICK, 0, 0);
            continue;
        }
        if (msg.message == WM_SYSKEYDOWN && msg.wParam == 'D' && g_page == P_HELLO && g_drv_avail) {   // Alt+D: the driver
            SendMessageW(H(ID_DRV), BM_CLICK, 0, 0);
            continue;
        }
        if (IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    g_hwnd = NULL;
    if (g_page != P_DRV_REBOOT) autostart_flush();   // unless the restart for the driver is still to come: then the continuation does it
    if (g_page == P_DONE || g_page == P_DRV_DONE || g_page == P_DRV_REBOOT) return RC_OK;      // ended on a success page
    if (g_page == P_FAIL || g_page == P_ERROR || g_page == P_DRV_ERROR) return RC_FAIL;           // ended on an error page
    return RC_CANCEL;                                                                       // closed before the end
}
