// The setup wizard: one window, one question at a time, for users who never installed software.
//
//   DynaRun V3 installed:  Hello -> Install (one UAC prompt) -> Done
//   not installed:         Hello -> get "Dyna Pro Dynamometers.zip" (found in Downloads/Desktop, or
//                          downloaded from dynapro.co.uk; the site's download page is one click away)
//                          -> password -> extract -> check it is DynaRun (by the MSI's UpgradeCode, never by
//                          file name) -> Dyna Pro's license -> Install (msiexec /qb and the fix, one UAC prompt)
//                          -> Done -> "Start DynaRun"
// Long steps run on a worker thread and report back with WM_PROGRESS / WM_JOBDONE.
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <commdlg.h>
#include "setup.h"
#include "version.h"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

enum { ID_TITLE = 100, ID_BODY, ID_EDIT, ID_RICH, ID_CHECK, ID_PROG, ID_STATUS, ID_EXTRA, ID_SECOND, ID_PRIMARY, ID_KEEPUP, ID_PIN, ID_AUTO, ID_DRV, ID_IMG };
enum { P_HELLO, P_FETCH, P_FAIL, P_PASSWORD, P_EXTRACT, P_LICENSE, P_RUNNING, P_INSTALL, P_DONE, P_ERROR,
       P_DRV_INTRO, P_DRV_FETCH, P_DRV_PRESS7, P_DRV_PREP, P_DRV_REBOOT, P_DRV_RETRY, P_DRV_WARN, P_DRV_INSTALL, P_DRV_DONE, P_DRV_ERROR };   // driver.c
enum { JOB_DOWNLOAD, JOB_EXTRACT, JOB_INSTALL_ALL, JOB_INSTALL_FIX, JOB_DRV_FETCH, JOB_DRV_PREP, JOB_DRV_INSTALL, JOB_DRV_CANCEL };
#define WM_PROGRESS (WM_APP + 1)
#define WM_JOBDONE  (WM_APP + 2)
#define TIMER_RUNNING 1

static int g_page, g_job, g_dpi, g_lastpct = -1, g_pending, g_lastrun;   // g_pending: install job waiting for DynaRun to close
static volatile LONG g_cancel;
static BOOL g_busy, g_closing, g_installed, g_pkg_ours, g_pkg_found, g_status_err, g_badfile, g_nolicense;   // g_nolicense: the setup's license text could not be read
static int g_nfound;   // packages pkg_find_local has handed us so far
static HANDLE g_thread;
static WCHAR g_pkg[MAX_PATH], g_msi[MAX_PATH], g_ver[64], g_note[1100];
static zipent g_zip;
static char g_pw[256];
static char *g_rtf;
static WCHAR g_info[2200];   // what the license page shows about the package (path, size, SHA-256, MSI version/manufacturer)
static DWORD g_neterr, g_total;
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
static void status(const WCHAR *t, BOOL err) { g_status_err = err; text(ID_STATUS, t); }

static void buttons(const WCHAR *primary, const WCHAR *second, const WCHAR *extra)
{
    text(ID_PRIMARY, primary); text(ID_SECOND, second); text(ID_EXTRA, extra);
    EnableWindow(H(ID_PRIMARY), TRUE);
    if (primary) SetFocus(H(ID_PRIMARY));
}

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
    WCHAR size[40], b[400];
    size[0] = 0;
    if (total) wsprintfW(size, T(L" (%lu.%lu MB)", L"（%lu.%lu MB）"), total / 1048576, total % 1048576 / 104858);
    wsprintfW(b, T(L"Downloading the DynaRun V3 setup from the Dyna Pro website%s.\nThis can take a few minutes.\n\n"
                   L"If it does not work, click \"Download manually\" to get it from the website yourself.",
                   L"正在從 Dyna Pro 官網下載 DynaRun V3 安裝檔%s，可能需要幾分鐘。\n\n"
                   L"如果下載不順利，可以按「開啟官網手動下載」，自己從官網下載。"), size);
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

static HBITMAP scale_bitmap(HBITMAP src, int sw, int sh, int tw, int th)
{
    HDC scr = GetDC(NULL), a = CreateCompatibleDC(scr), b = CreateCompatibleDC(scr); HBITMAP dst = CreateCompatibleBitmap(scr, tw, th), oa, ob;
    oa = SelectObject(a, src); ob = SelectObject(b, dst);
    SetStretchBltMode(b, HALFTONE); SetBrushOrgEx(b, 0, 0, NULL);
    StretchBlt(b, 0, 0, tw, th, a, 0, 0, sw, sh, SRCCOPY);
    SelectObject(a, oa); SelectObject(b, ob); DeleteDC(a); DeleteDC(b); ReleaseDC(NULL, scr);
    return dst;
}

static void show_image(int which)            // 0: press 7 menu, 1: "install anyway"
{
    g_img = which;
    if (!g_bmp[which]) g_bmp[which] = load_png(18 + which, &g_bw[which], &g_bh[which]);
    vis(ID_IMG, g_bmp[which] != NULL);
}

// Fits the picture of the current page into w x ah (not larger than at 96 dpi scaled to this screen); returns the height used.
static int place_image(int x, int y, int w, int ah)
{
    int ow = g_bw[g_img], oh = g_bh[g_img], tw, th; HBITMAP old;
    if (ah < S(60)) ah = S(60);
    tw = w; th = MulDiv(oh, tw, ow);
    if (th > ah) { th = ah; tw = MulDiv(ow, th, oh); }
    if (tw > MulDiv(ow, g_dpi, 96)) { tw = MulDiv(ow, g_dpi, 96); th = MulDiv(oh, tw, ow); }
    if (g_shown_img != g_img || g_shown_w != tw || g_shown_h != th) {
        HBITMAP s = scale_bitmap(g_bmp[g_img], ow, oh, tw, th);
        old = (HBITMAP)SendMessageW(H(ID_IMG), STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)s);
        if (old) DeleteObject(old);
        g_shown = s; g_shown_img = g_img; g_shown_w = tw; g_shown_h = th;
    }
    place(ID_IMG, x, y, tw, th);
    return th;
}

static void set_page(int p)
{
    static WCHAR b[2600];
    g_page = p;
    KillTimer(g_hwnd, TIMER_RUNNING);
    vis(ID_EDIT, FALSE); vis(ID_RICH, FALSE); vis(ID_CHECK, FALSE); vis(ID_PIN, FALSE); vis(ID_AUTO, FALSE); vis(ID_DRV, FALSE); vis(ID_IMG, FALSE); vis(ID_PROG, FALSE); status(NULL, FALSE);
    switch (p) {
    case P_HELLO:
        if (g_installed) {
            text(ID_TITLE, T(L"Fix DynaRun V3 for this Windows", L"讓 DynaRun V3 在這台電腦正常運作"));
            wsprintfW(b, T(L"DynaRun V3 was found:\n%s\n\nThis installs the fixes for Windows 10/11 (flickering main screen, .Dpr files "
                           L"in OneDrive that do not open, garbled text, \"Run as administrator\").\n\n"
                           L"When Windows asks whether to allow changes, click \"Yes\".%s",
                           L"已找到 DynaRun V3：\n%s\n\n接下來會安裝 Windows 10/11 的相容性修正（主畫面閃爍、OneDrive 裡的 .Dpr 打不開、"
                           L"中文亂碼、以系統管理員執行卡住）。\n\nWindows 詢問「是否允許變更」時，請按「是」。%s"), g_exe, disclaimer());
            text(ID_BODY, b);
            buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
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
    case P_FETCH:
        text(ID_TITLE, T(L"Downloading the setup", L"正在下載安裝檔"));
        fetch_body(0);
        progress(FALSE); status(L" ", FALSE);
        buttons(NULL, T(L"Cancel", L"取消"), T(L"Download manually", L"開啟官網手動下載"));
        break;
    case P_FAIL:
        text(ID_TITLE, g_badfile ? T(L"This is not the DynaRun V3 setup", L"這不是 DynaRun V3 安裝檔")
                                 : T(L"The setup could not be downloaded", L"無法自動下載安裝檔"));
        g_badfile = FALSE;
        wsprintfW(b, T(L"%s\n\nDownload it yourself:\n   1.  click \"Open website\"\n   2.  on the page, click the download for \"Dyna Run V3 Software\"\n"
                       L"   3.  come back here and click \"Retry\" (or \"Choose file...\")",
                       L"%s\n\n請自己下載：\n   1.  按「開啟官網」\n   2.  在網頁上下載「Dyna Run V3 Software」\n"
                       L"   3.  下載完成後回到這裡按「重試」（或「選擇檔案…」）"), g_note);
        text(ID_BODY, b);
        buttons(T(L"Retry", L"重試"), T(L"Choose file...", L"選擇檔案…"), T(L"Open website", L"開啟官網"));
        break;
    case P_PASSWORD:
        text(ID_TITLE, T(L"Enter the setup password", L"輸入安裝密碼"));
        wsprintfW(b, T(L"Type the password that Dyna Pro gave you for the DynaRun V3 setup.\n(It is used only to open the setup file and is not saved.)\n\nSetup file: %s",
                       L"請輸入 Dyna Pro 給你的 DynaRun V3 安裝密碼。\n（只用來打開安裝檔，不會被儲存。）\n\n安裝檔：%s"), file_name(g_pkg));
        text(ID_BODY, b);
        vis(ID_EDIT, TRUE);
        buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), T(L"Other file...", L"選擇其他檔案…"));
        if (g_note[0]) status(g_note, TRUE);
        SetFocus(H(ID_EDIT)); SendMessageW(H(ID_EDIT), EM_SETSEL, 0, -1);
        break;
    case P_EXTRACT:
        text(ID_TITLE, T(L"Opening the setup file", L"正在打開安裝檔"));
        text(ID_BODY, T(L"Please wait...", L"請稍候…"));
        progress(FALSE);
        buttons(NULL, T(L"Cancel", L"取消"), NULL);
        break;
    case P_LICENSE:
        text(ID_TITLE, T(L"Dyna Pro license agreement", L"Dyna Pro 授權合約"));
        lstrcpyW(b, g_nolicense ? T(L"The license agreement in this DynaRun V3 setup could not be shown.",
                                    L"無法顯示這個 DynaRun V3 安裝檔裡的授權合約。")
                                : T(L"Please read Dyna Pro's license agreement for DynaRun V3.", L"請閱讀 Dyna Pro 的 DynaRun V3 授權合約。"));
        lstrcatW(b, L"\n\n"); lstrcatW(b, g_info);
        text(ID_BODY, b);
        SetWindowTextW(H(ID_CHECK), g_nolicense ? T(L"I understand and &accept Dyna Pro's license terms", L"我了解並同意 Dyna Pro 的授權條款(&A)")
                                                : T(L"I &accept the license agreement", L"我接受授權合約(&A)"));
        vis(ID_RICH, TRUE); vis(ID_CHECK, TRUE);
        buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), g_nolicense ? T(L"Open Dyna Pro website", L"開啟 Dyna Pro 官網") : NULL);
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
        if (g_pinresult == 2) lstrcatW(b, T(L"\n\nDynaRun will appear on the taskbar after you sign out and back in.", L"\n\n登出再登入後，DynaRun 會出現在工作列上。"));
        if (g_pinresult == 3) lstrcatW(b, os_build() >= 22000
            ? T(L"\n\nTo pin it to the taskbar: right-click the \"DynaRun V3\" icon on the desktop → Show more options → Pin to taskbar.",
                L"\n\n要釘選到工作列：在桌面「DynaRun V3」圖示上按右鍵 →「顯示其他選項」→「釘選到工作列」。")
            : T(L"\n\nTo pin it to the taskbar: right-click the \"DynaRun V3\" icon on the desktop → Pin to taskbar.",
                L"\n\n要釘選到工作列：在桌面「DynaRun V3」圖示上按右鍵 →「釘選到工作列」。"));
        if (g_autostart) lstrcatW(b, T(L"\n\nDynaRun will start automatically when you sign in.", L"\n\n以後開機登入後會自動啟動 DynaRun。"));
        if (g_drv) lstrcatW(b, T(L"\n\nNext: the Dyna Pro USB driver, so that this computer can talk to the dynamometer.", L"\n\n下一步：安裝 Dyna Pro USB 驅動程式，電腦才能和馬力機連線。"));
        lstrcatW(b, T(L"\n\nIf DynaRun's screen ever looks wrong, close DynaRun and start it again.", L"\n\n如果 DynaRun 的畫面顯示不正常，關掉 DynaRun 再開一次即可。"));
        text(ID_BODY, b);
        if (g_drv) buttons(T(L"Next", L"下一步"), T(L"Close", L"關閉"), NULL);
        else buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);
        if (g_zh && legacy_acp() != 950)   // DynaRun's Chinese is Big5: only a zh-TW system locale shows it everywhere
            status(L"注意：這台電腦的「非 Unicode 程式的語言」不是中文（台灣），DynaRun 的中文可能會變成亂碼。\n"
                   L"請到「設定 → 時間與語言 → 語言與地區 → 系統管理語言設定 → 變更系統地區設定」選「中文（繁體，台灣）」，再重新開機。", TRUE);
        break;
    case P_ERROR:
        text(ID_TITLE, T(L"Something went wrong", L"發生問題"));
        text(ID_BODY, g_note);
        buttons(T(L"Close", L"關閉"), NULL, NULL);
        break;
    case P_DRV_INTRO:
        text(ID_TITLE, T(L"Install the Dyna Pro USB driver", L"安裝 Dyna Pro USB 驅動程式"));
        text(ID_BODY, T(L"Windows needs Dyna Pro's USB driver to talk to the dynamometer.\n\nThis program will:\n"
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
        buttons(T(L"Download and continue", L"下載並繼續"), g_drvmode == DRV_MODE_NONE ? T(L"Skip", L"略過") : T(L"Cancel", L"取消"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_FETCH:
        text(ID_TITLE, T(L"Downloading the driver", L"正在下載驅動程式"));
        text(ID_BODY, T(L"Downloading the driver from the Dyna Pro website and checking it...", L"正在從 Dyna Pro 官網下載驅動程式並檢查…"));
        progress(FALSE); status(L" ", FALSE);
        buttons(NULL, T(L"Cancel", L"取消"), NULL);
        break;
    case P_DRV_PRESS7:
        text(ID_TITLE, T(L"After the restart you will see this: press 7", L"重新開機後會看到這個畫面，請按 7"));
        text(ID_BODY, T(L"Tip: take a photo of this page with your phone.\nSave your work in other programs before you restart.",
                        L"建議用手機拍下這一頁。\n按「重新開機」前請先存好其他程式的資料。"));
        show_image(0);
        buttons(T(L"Restart now", L"重新開機"), T(L"Later", L"稍後"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_PREP:
        text(ID_TITLE, T(L"Please wait", L"請稍候"));
        text(ID_BODY, T(L"Getting ready...\n\nWhen Windows asks whether to allow changes, click \"Yes\".", L"正在準備…\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
        progress(TRUE);
        buttons(NULL, NULL, NULL);
        break;
    case P_DRV_REBOOT:
        text(ID_TITLE, g_drv_noreboot ? T(L"Please restart the computer", L"請重新開機") : T(L"Restarting", L"即將重新開機"));
        text(ID_BODY, g_drv_noreboot
            ? T(L"Everything is ready, but Windows would not restart by itself.\n\nPlease restart the computer yourself (Start, the power button, Restart). "
                L"When the menu appears, press 7. After you sign in, this program continues by itself.",
                L"都準備好了，但 Windows 沒有自動重新開機。\n\n請自己重新開機（開始 → 電源 → 重新啟動）。出現選單時請按 7；登入後本程式會自動繼續。")
            : T(L"The computer restarts now.\n\nWhen the menu appears, press 7. After you sign in, this program continues by itself.\n\n"
                L"If the computer does not restart (a program is waiting for you), save your work and restart it yourself.",
                L"電腦即將重新開機。\n\n出現選單時請按 7；登入後本程式會自動繼續。\n\n如果電腦沒有重新開機（有程式在等你回應），請存好資料後自己重新開機。"));
        buttons(T(L"Close", L"關閉"), NULL, NULL);
        break;
    case P_DRV_RETRY:
        text(ID_TITLE, T(L"7 was not pressed", L"剛才沒有按到 7"));
        text(ID_BODY, T(L"The restart menu was skipped, so Windows still checks driver signatures and the driver cannot be installed yet.\n\n"
                        L"Try again? The computer restarts once more. When the menu appears, press 7.",
                        L"剛才重新開機時沒有選到「停用驅動程式強制簽章」（按 7），所以 Windows 還在檢查簽章，驅動程式現在還不能安裝。\n\n"
                        L"要再試一次嗎？電腦會再重新開機一次，出現選單時請按 7。"));
        buttons(T(L"Try again", L"再試一次"), T(L"Cancel", L"取消"), NULL);
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_DRV_WARN:
        text(ID_TITLE, T(L"Last step: install the driver", L"最後一步：安裝驅動程式"));
        text(ID_BODY, T(L"Driver signature checking is off for this start. Windows may show a red warning like this one, twice (one for each driver file): choose \"Install this driver software anyway\" each time.",
                        L"這次開機已暫時關閉簽章檢查。Windows 可能會跳出像下面這樣的紅色警告（無法驗證此驅動程式軟體的發行者），而且會出現兩次（各對應一個驅動程式檔），每次都請選「仍然安裝此驅動程式軟體」。"));
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
        buttons(T(L"Close", L"關閉"), NULL, NULL);
        break;
    case P_DRV_ERROR:
        text(ID_TITLE, T(L"The driver was not installed", L"驅動程式沒有安裝"));
        text(ID_BODY, g_note);
        buttons(T(L"Close", L"關閉"), NULL, T(L"Open the manual steps", L"開啟手動安裝說明"));
        break;
    }
    g_note[0] = 0;
    layout();
    if (!GetFocus() || !IsWindowEnabled(GetFocus()) || !(GetWindowLongW(GetFocus(), GWL_STYLE) & WS_VISIBLE))
        SetFocus(GetNextDlgTabItem(g_hwnd, NULL, FALSE));       // something must hold the keyboard focus
}

/* ---------- worker thread ---------- */

static void on_progress(DWORD done, DWORD total)
{
    int pct = total ? MulDiv(done, 1000, total) : 0;
    if (pct == g_lastpct) return;
    g_lastpct = pct;
    PostMessageW(g_hwnd, WM_PROGRESS, done, total);
}

static DWORD WINAPI job_proc(LPVOID unused)
{
    int rc = 1;
    switch (g_job) {
    case JOB_DOWNLOAD: rc = pkg_download(g_pkg, on_progress, &g_cancel, &g_neterr); break;
    case JOB_EXTRACT:  rc = zip_extract(&g_zip, g_pw, g_msi, on_progress, &g_cancel); SecureZeroMemory(g_pw, sizeof(g_pw)); break;   // the password is not needed any more
    case JOB_INSTALL_ALL: rc = install_all(g_msi); break;
    case JOB_INSTALL_FIX: rc = install_fix(); break;
    case JOB_DRV_FETCH: rc = driver_fetch(g_drvzip, on_progress, &g_cancel, &g_neterr); break;
    case JOB_DRV_PREP: rc = driver_prep(g_drvzip[0] ? g_drvzip : L"-"); break;
    case JOB_DRV_INSTALL: rc = driver_install(); break;
    case JOB_DRV_CANCEL: rc = driver_cancel(); break;
    }
    PostMessageW(g_hwnd, WM_JOBDONE, g_job, rc);
    (void)unused;
    return 0;
}

static void start_job(int job, int page)
{
    DWORD tid;
    g_job = job; g_cancel = 0; g_busy = TRUE;
    set_page(page);
    g_thread = CreateThread(NULL, 0, job_proc, NULL, 0, &tid);
    if (job == JOB_INSTALL_ALL || job == JOB_INSTALL_FIX || job >= JOB_DRV_PREP) SetTimer(g_hwnd, ID_KEEPUP, 300, NULL);
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

static void fetch(void)
{
    g_pkg_ours = FALSE;
    g_pkg_found = g_nfound < 8 && pkg_find_local(g_pkg);   // the cap: pkg_reject remembers 8 files
    if (g_pkg_found) { g_nfound++; open_package(); return; }
    g_pkg_ours = TRUE;
    start_job(JOB_DOWNLOAD, P_FETCH);
}

// Whatever the file is called: only the real DynaRun setup is ever installed.
static void not_dynarun(void)
{
    WCHAR m[MAX_PATH], nm[100];
    lstrcpynW(nm, file_name(g_pkg), 100);       // wsprintfW does not check the size of g_note: a long file name must not overflow it
    wsprintfW(g_note, T(L"\"%s\" is not the DynaRun V3 setup, so it will not be installed.",
                        L"「%s」不是 DynaRun V3 的安裝檔，所以不會安裝它。"), nm);
    pkg_reject(g_pkg);
    pkg_temp_dir(m); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(g_msi, m)) DeleteFileW(g_msi);   // our extracted copy
    if (g_pkg_ours) DeleteFileW(g_pkg);
    if (g_pkg_found) { fetch(); return; }   // we picked it, not the user: quietly try the next one or download
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
    lstrcatW(g_info, T(L"Package: ", L"安裝檔："));
    lstrcatW(g_info, g_pkg);
    lstrcatW(g_info, g_pkg_ours ? T(L"\n(downloaded now from dynapro.co.uk)", L"\n（剛從 dynapro.co.uk 下載）")
                     : g_pkg_found ? T(L"\n(found on this computer, not downloaded by DynaRunFix)", L"\n（在這台電腦上找到，不是 DynaRunFix 下載的）")
                     : T(L"\n(chosen by you)", L"\n（你選擇的檔案）"));
    fmt_size(sz, size);
    lstrcatW(g_info, T(L"\nSize: ", L"\n大小：")); lstrcatW(g_info, sz);
    lstrcatW(g_info, L"\nSHA-256: "); lstrcatW(g_info, okp ? hex : L"?");
    if (!same) {
        fmt_size(sz, size2);
        lstrcatW(g_info, T(L"\nSetup.msi inside: ", L"\n其中的 Setup.msi：")); lstrcatW(g_info, sz);
        lstrcatW(g_info, L"\nSHA-256: "); lstrcatW(g_info, okm ? hex2 : L"?");
    }
    lstrcatW(g_info, T(L"\nMSI ProductVersion: ", L"\nMSI ProductVersion："));
    lstrcatW(g_info, ver[0] ? ver : L"?");
    lstrcatW(g_info, T(L"   Manufacturer: ", L"   Manufacturer："));
    lstrcatW(g_info, mfr[0] ? mfr : L"?");
    lstrcatW(g_info, T(L"\nCompare this with what Dyna Pro gives you. Dyna Pro does not sign its setup, so DynaRunFix cannot prove that it is exactly what Dyna Pro released: install only a file you got from Dyna Pro.",
                       L"\n請與 Dyna Pro 提供的資訊比對。Dyna Pro 的安裝檔沒有數位簽章，DynaRunFix 無法證明它與 Dyna Pro 發布的完全一致，請只安裝從 Dyna Pro 取得的檔案。"));
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
                                     L"(\"Open Dyna Pro website\" below).\r\n\r\n"
                                     L"Install only if you accept Dyna Pro's license terms.",
                                     L"DynaRun V3 是 Dyna Pro Dynamometers Ltd 依其使用者授權合約及銷售條款授權給你使用的軟體。"
                                     L"授權合約包含在 Dyna Pro 的安裝檔中，但本安裝程式無法從這個安裝檔讀出合約內容。\r\n\r\n"
                                     L"如需閱讀合約，請聯絡 Dyna Pro（support@dynapro.co.uk）或參考其官網（下方「開啟 Dyna Pro 官網」）。\r\n\r\n"
                                     L"請在同意 Dyna Pro 的授權條款後再安裝。"));
    SendMessageW(H(ID_CHECK), BM_SETCHECK, BST_UNCHECKED, 0);
    set_page(P_LICENSE);
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
    case PK_URL: lstrcpyW(g_note, T(L"The download was refused: the address did not stay on https://dynapro.co.uk.", L"已拒絕下載：連線沒有停留在 https://dynapro.co.uk。")); break;
    case PK_CERT: wsprintfW(g_note, T(L"The security certificate of the Dyna Pro website could not be verified (error %lu).", L"無法驗證 Dyna Pro 官網的安全憑證（錯誤 %lu）。"), g_neterr); break;
    case PK_HTTP: wsprintfW(g_note, T(L"The Dyna Pro website answered with error %lu.", L"Dyna Pro 官網回應錯誤 %lu。"), g_neterr); break;
    case PK_IO: wsprintfW(g_note, T(L"The file could not be saved (error %lu).", L"無法儲存檔案（錯誤 %lu）。"), g_neterr); break;
    case PK_NET: wsprintfW(g_note, T(L"No connection to the Dyna Pro website (error %lu).", L"連不上 Dyna Pro 官網（錯誤 %lu）。"), g_neterr); break;
    case DRV_E_BCD: lstrcpyW(g_note, T(L"Windows could not be told to show its start-up menu at the next start (bcdedit failed).", L"無法設定「下次開機顯示進階選單」（bcdedit 失敗）。")); break;
    case DRV_E_TASK: lstrcpyW(g_note, T(L"The task that continues the installation after the restart could not be created.", L"無法建立重新開機後繼續安裝的排程工作。")); break;
    case DRV_E_DIR: lstrcpyW(g_note, T(L"The working folder %ProgramData%\\DynaRunFix could not be made safe (an old folder is still in use, or it can be changed by other users). Nothing was installed. Close other programs, restart the PC and try again.",
                                       L"無法把工作資料夾 %ProgramData%\\DynaRunFix 設為安全狀態（舊資料夾仍在使用中，或其他使用者可以修改它）。沒有安裝任何東西。請關閉其他程式、重新啟動電腦後再試一次。")); break;
    case DRV_E_PNP: lstrcpyW(g_note, T(L"Windows did not accept the driver. Driver signature checking may still have been on.", L"Windows 沒有接受這個驅動程式，可能是簽章檢查還開著。")); break;
    default: wsprintfW(g_note, T(L"The driver could not be prepared (code %d).", L"無法準備驅動程式（代碼 %d）。"), rc); break;
    }
    lstrcatW(g_note, T(L"\n\nYou can install the driver by hand instead: click \"Open the manual steps\".", L"\n\n也可以改用手動方式安裝，請按「開啟手動安裝說明」。"));
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
        if (g_drv_have) { if (is_admin()) driver_cancel(); set_page(P_DRV_DONE); }       // installed meanwhile: the waiting task goes
        else set_page(driver_ci_state() == DRV_CI_ENFORCED ? P_DRV_RETRY : P_DRV_WARN);
    } else set_page(g_drv_have ? P_DRV_DONE : P_DRV_INTRO);
}

static void job_done(int job, int rc)
{
    WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = NULL; g_busy = FALSE;
    if (g_closing) { DestroyWindow(g_hwnd); return; }
    switch (job) {
    case JOB_DOWNLOAD:
        if (rc == PK_OK) { open_package(); break; }
        if (rc == PK_CANCEL) lstrcpyW(g_note, T(L"The download was stopped.", L"已停止下載。"));
        else if (rc == PK_HTTP) wsprintfW(g_note, T(L"The Dyna Pro website answered with error %lu.", L"Dyna Pro 官網回應錯誤 %lu。"), g_neterr);
        else if (rc == PK_CERT) wsprintfW(g_note, T(L"The security certificate of the Dyna Pro website could not be verified (error %lu), so nothing was downloaded. "
                                                    L"Check the date and time of this computer, or download the setup yourself from dynapro.co.uk and choose it with \"Other file...\".",
                                                    L"無法驗證 Dyna Pro 官網的安全憑證（錯誤 %lu），因此沒有下載任何東西。請確認電腦的日期與時間，或自行從 dynapro.co.uk 下載安裝檔後用「選擇其他檔案…」選取。"), g_neterr);
        else if (rc == PK_URL) lstrcpyW(g_note, T(L"The download was refused: the address did not stay on https://dynapro.co.uk (it was redirected elsewhere or to an unencrypted connection). Nothing was downloaded.",
                                                  L"已拒絕下載：連線沒有停留在 https://dynapro.co.uk（被導向其他位置或未加密的連線），因此沒有下載任何東西。"));
        else if (rc == PK_IO) wsprintfW(g_note, T(L"The file could not be saved (error %lu).", L"無法儲存檔案（錯誤 %lu）。"), g_neterr);
        else wsprintfW(g_note, T(L"No connection to the Dyna Pro website (error %lu). Check the internet connection.",
                                L"連不上 Dyna Pro 官網（錯誤 %lu），請確認網路是否正常。"), g_neterr);
        set_page(P_FAIL);
        break;
    case JOB_EXTRACT:
        if (rc == PK_OK) { after_extract(); break; }
        drop_msi();                                  // a failed or cancelled extraction leaves no decrypted copy
        if (rc == PK_CANCEL) { DestroyWindow(g_hwnd); break; }
        if (rc == PK_PASSWORD) { lstrcpyW(g_note, T(L"Wrong password, or the file is damaged. Please check the password.", L"密碼不正確，或檔案已損毀。請再確認密碼。")); set_page(P_PASSWORD); break; }
        lstrcpyW(g_note, T(L"The setup could not be unpacked. Is the disk full?", L"無法解開安裝檔，硬碟空間是否不足？"));
        set_page(P_ERROR);
        break;
    case JOB_INSTALL_ALL:
    case JOB_INSTALL_FIX:
        if (rc == 0) { cleanup_files(); set_page(P_DONE); break; }
        if (rc == RC_RUNNING) { g_pending = job; set_page(P_RUNNING); break; }   // DynaRun was started meanwhile
        if (rc == 2) {
            lstrcpyW(g_note, T(L"Nothing was installed: Windows needs your \"Yes\" to install.", L"沒有安裝：需要在 Windows 詢問時按「是」才能安裝。"));
            set_page(job == JOB_INSTALL_ALL ? P_LICENSE : P_HELLO);
            break;
        }
        wsprintfW(g_note, T(L"The installation did not finish (code %d).\n\nRestart the computer and run this program again.",
                            L"安裝沒有完成（代碼 %d）。\n\n請重新開機後再執行一次本程式。"), rc);
        set_page(P_ERROR);
        break;
    case JOB_DRV_FETCH:
        if (rc == PK_OK) { set_page(P_DRV_PRESS7); break; }
        if (rc == PK_CANCEL) { lstrcpyW(g_note, T(L"The download was stopped.", L"已停止下載。")); set_page(P_DRV_INTRO); break; }
        drv_error(rc);
        break;
    case JOB_DRV_PREP:
        if (rc == 0) { drv_reboot_now(); break; }
        if (rc == 2) {
            lstrcpyW(g_note, T(L"Nothing was changed: Windows needs your \"Yes\" first.", L"沒有做任何變更：需要在 Windows 詢問時按「是」。"));
            set_page(g_drvmode == DRV_MODE_CONTINUE ? P_DRV_RETRY : P_DRV_PRESS7);
            break;
        }
        drv_error(rc);
        break;
    case JOB_DRV_INSTALL:
        if (rc == 0) { g_drv_have = FALSE; set_page(P_DRV_DONE); break; }
        if (rc == 2) { lstrcpyW(g_note, T(L"Nothing was installed: Windows needs your \"Yes\" first.", L"沒有安裝：需要在 Windows 詢問時按「是」。")); set_page(P_DRV_WARN); break; }
        drv_error(rc);
        break;
    case JOB_DRV_CANCEL:
        DestroyWindow(g_hwnd);                       // cancelled (or not allowed to: nothing else can be done from here)
        break;
    }
}

static void on_primary(void)
{
    switch (g_page) {
    case P_HELLO:
        g_pin = SendMessageW(H(ID_PIN), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_autostart = SendMessageW(H(ID_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_drv = g_drv_avail && SendMessageW(H(ID_DRV), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (g_installed) install(JOB_INSTALL_FIX); else fetch();
        break;
    case P_FAIL: fetch(); break;
    case P_PASSWORD:
        if (!take_password()) {
            status(T(L"Wrong password. Please check it (upper/lower case matters).\nIf the setup file above is not the DynaRun setup, click \"Other file...\".",
                     L"密碼不正確，請再確認（大小寫有差別）。\n如果上面的安裝檔不是 DynaRun 的，請按「選擇其他檔案…」。"), TRUE);
            SetFocus(H(ID_EDIT)); break;
        }
        start_job(JOB_EXTRACT, P_EXTRACT);
        break;
    case P_LICENSE: install(JOB_INSTALL_ALL); break;
    case P_RUNNING:
        g_close = TRUE;                              // the elevated stage closes DynaRun (see close_programs in setup.c)
        start_job(g_pending, P_INSTALL);
        break;
    case P_DONE:
        if (g_drv) { set_page(P_DRV_INTRO); break; }                 // "Next": the driver
        start_dynarun();
        DestroyWindow(g_hwnd);
        break;
    case P_ERROR: DestroyWindow(g_hwnd); break;
    case P_DRV_INTRO: g_drvzip[0] = 0; start_job(JOB_DRV_FETCH, P_DRV_FETCH); break;
    case P_DRV_PRESS7: start_job(JOB_DRV_PREP, P_DRV_PREP); break;
    case P_DRV_RETRY: g_drvzip[0] = 0; start_job(JOB_DRV_PREP, P_DRV_PREP); break;       // the staged driver is still there
    case P_DRV_WARN: start_job(JOB_DRV_INSTALL, P_DRV_INSTALL); break;
    case P_DRV_REBOOT: case P_DRV_DONE: case P_DRV_ERROR: DestroyWindow(g_hwnd); break;
    }
}

static void on_close(void)
{
    if (g_page == P_INSTALL || g_page == P_DRV_PREP || g_page == P_DRV_INSTALL) return;   // msiexec / registry / system tool work must not be interrupted
    if (g_busy) { g_closing = TRUE; InterlockedExchange(&g_cancel, 1); return; }
    // after the restart: closing the window ends the installation like "Cancel" (done at once when this runs elevated, as the task does)
    if (g_drvmode == DRV_MODE_CONTINUE && (g_page == P_DRV_RETRY || g_page == P_DRV_WARN) && is_admin()) driver_cancel();
    DestroyWindow(g_hwnd);
}

// The second button (and Esc): Cancel / Skip / Later / Close
static void on_second(BOOL button)
{
    if (g_page == P_FAIL && button) choose_file();
    else if ((g_page == P_FETCH || g_page == P_DRV_FETCH) && button) InterlockedExchange(&g_cancel, 1);   // stop, then offer the other ways
    else if (g_page == P_DRV_INTRO && g_drvmode == DRV_MODE_NONE) { g_drv = FALSE; set_page(P_DONE); }       // "Skip": back to the end of the install
    else if (g_page == P_DRV_RETRY || g_page == P_DRV_WARN) start_job(JOB_DRV_CANCEL, P_DRV_PREP);          // the task, the menu flag and BitLocker back
    else if (g_page != P_INSTALL) on_close();
}

/* ---------- window ---------- */

#define FOOTER 76                                   // height of the grey button band, at 96 dpi

static HWND add(const WCHAR *cls, DWORD style, int id)
{
    DWORD ex = (id == ID_EDIT || id == ID_RICH) ? WS_EX_CLIENTEDGE : 0;   // themed border like other Windows text boxes
    HWND c = CreateWindowExW(ex, cls, L"", WS_CHILD | style, 0, 0, 0, 0, g_hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
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
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CHECK);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_PIN);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_AUTO);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, ID_DRV);
    add(L"STATIC", SS_BITMAP | SS_NOTIFY, ID_IMG);
    SetWindowTextW(H(ID_DRV), T(L"Also install the Dyna Pro USB &driver (one restart needed)", L"同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）(&D)"));
    SendMessageW(H(ID_DRV), BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(H(ID_PIN), T(L"Pin DynaRun to the &taskbar", L"釘選到工作列(&T)"));
    SetWindowTextW(H(ID_AUTO), T(L"Start DynaRun when Windows &starts", L"開機時自動啟動 DynaRun(&S)"));
    SendMessageW(H(ID_PIN), BM_SETCHECK, g_pin ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(H(ID_AUTO), BM_SETCHECK, g_autostart ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(H(ID_CHECK), T(L"I &accept the license agreement", L"我接受授權合約(&A)"));
    add(PROGRESS_CLASSW, 0, ID_PROG);
    add(L"STATIC", SS_NOPREFIX, ID_STATUS);
    add(L"BUTTON", WS_TABSTOP, ID_EXTRA);
    add(L"BUTTON", WS_TABSTOP, ID_SECOND);
    add(L"BUTTON", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_PRIMARY);
}

// Height of a control's text wrapped to width w, or its width when w is 0.
static int measure(int id, int w)
{
    WCHAR t[1200]; RECT r; HDC dc = GetDC(g_hwnd);
    HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(H(id), WM_GETFONT, 0, 0));
    GetWindowTextW(H(id), t, 1200);
    r.left = r.top = 0; r.right = w ? w : 10000; r.bottom = 0;
    DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_NOPREFIX | (w ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old); ReleaseDC(g_hwnd, dc);
    return w ? r.bottom : r.right;
}

static BOOL shown(int id) { return (GetWindowLongW(H(id), GWL_STYLE) & WS_VISIBLE) != 0; }   // also before the window is shown
static void place(int id, int x, int y, int w, int h) { SetWindowPos(H(id), NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); }

// Stacks the visible controls top-down from the measured text heights; buttons right-aligned in the footer.
static void layout(void)
{
    static const int btn[] = { ID_PRIMARY, ID_SECOND };
    RECT c; int m = S(32), w, y, h, x, i, bw, bh = S(36), by, bottom;
    GetClientRect(g_hwnd, &c);
    w = c.right - 2 * m; bottom = c.bottom - S(FOOTER);
    y = S(shown(ID_PIN) ? 18 : 24);                     // tighter on the first page: its text and the options must fit
    h = measure(ID_TITLE, w); place(ID_TITLE, m, y, w, h); y += h + S(shown(ID_PIN) ? 10 : 16);
    h = measure(ID_BODY, w) + S(4); place(ID_BODY, m, y, w, h); y += h + S(shown(ID_PIN) ? 6 : 20);
    if (shown(ID_PIN)) {                                // the two options: side by side when they fit, else stacked
        int a = measure(ID_PIN, 0) + S(24), b2 = measure(ID_AUTO, 0) + S(24), side = a + b2 + S(16) <= w;
        place(ID_PIN, m, y, a, S(24)); place(ID_AUTO, side ? m + a + S(16) : m, side ? y : y + S(24), b2, S(24));
        y += side ? S(24) : S(48);
    }
    if (shown(ID_DRV)) { h = measure(ID_DRV, w - S(24)) + S(8); place(ID_DRV, m, y + S(2), w, h); y += h + S(4); }
    if (shown(ID_IMG)) y += place_image(m, y, w, bottom - y - S(10)) + S(8);
    if (shown(ID_EDIT)) { place(ID_EDIT, m, y, w < S(360) ? w : S(360), S(34)); y += S(34) + S(12); }
    if (shown(ID_PROG)) { place(ID_PROG, m, y, w, S(20)); y += S(20) + S(10); }
    if (shown(ID_RICH)) {
        int cy = bottom - S(16) - S(28);
        place(ID_CHECK, m, cy, w, S(28));
        place(ID_RICH, m, y, w, cy - S(10) - y);
        y = cy + S(28);
    }
    place(ID_STATUS, m, y, w, bottom - y > 0 ? bottom - y : 0);
    by = bottom + (S(FOOTER) - bh) / 2;
    for (x = c.right - m, i = 0; i < 2; i++) {
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
        case ID_EXTRA:
            if (g_page == P_FAIL || g_page == P_FETCH || g_page == P_LICENSE) ShellExecuteW(h, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL);
            else if (g_page == P_PASSWORD) choose_file();
            else if (g_page == P_HELLO) choose_exe();
            else if (g_page == P_DRV_ERROR) ShellExecuteW(h, NULL, DRIVER_DOC_URL, NULL, NULL, SW_SHOWNORMAL);
            break;
        case ID_CHECK: EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        }
        return 0;
    case WM_PROGRESS: {
        static WCHAR b[100];
        DWORD done = (DWORD)w, total = (DWORD)l;
        SendMessageW(H(ID_PROG), PBM_SETPOS, total ? MulDiv(done, 1000, total) : 0, 0);
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
    case WM_JOBDONE:
        KillTimer(h, ID_KEEPUP);
        // after the elevated part (which allows it) the wizard comes back in front of other windows
        if (w == JOB_INSTALL_ALL || w == JOB_INSTALL_FIX || w >= JOB_DRV_PREP) bring_front(h);
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

int wizard(void)
{
    INITCOMMONCONTROLSEX icc; WNDCLASSW wc; NONCLIENTMETRICSW nm; RECT r; MSG msg; HDC dc; LOGFONTW lf;
    static WCHAR title[100];
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
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2, (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 1;
    create_controls();
    g_installed = locate_dynarun();
    g_drv_avail = g_drvmode == DRV_MODE_NONE && driver_supported() && !driver_installed();
    if (g_drvmode != DRV_MODE_NONE) drv_start();
    else set_page(P_HELLO);
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
    return g_page == P_DONE ? 0 : 2;
}
