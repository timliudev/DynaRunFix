// The setup wizard: one window, one question at a time, for users who never installed software.
//
//   DynaRun V3 installed:  Hello -> Install (one UAC prompt) -> Done
//   not installed:         Hello -> get "Dyna Pro Dynamometers.zip" (found in Downloads/Desktop, or
//                          downloaded from dynapro.co.uk; if that fails: open the site / choose file)
//                          -> password -> extract -> Dyna Pro's license -> Install (msiexec /qb and
//                          the fix, one UAC prompt) -> Done -> "Start DynaRun"
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

enum { ID_TITLE = 100, ID_BODY, ID_EDIT, ID_RICH, ID_CHECK, ID_PROG, ID_STATUS, ID_EXTRA, ID_SECOND, ID_PRIMARY };
enum { P_HELLO, P_FETCH, P_FAIL, P_PASSWORD, P_EXTRACT, P_LICENSE, P_INSTALL, P_DONE, P_ERROR };
enum { JOB_DOWNLOAD, JOB_EXTRACT, JOB_INSTALL_ALL, JOB_INSTALL_FIX };
#define WM_PROGRESS (WM_APP + 1)
#define WM_JOBDONE  (WM_APP + 2)

static int g_page, g_job, g_dpi, g_lastpct = -1;
static volatile LONG g_cancel;
static BOOL g_busy, g_closing, g_installed, g_pkg_ours, g_status_err;
static HANDLE g_thread;
static WCHAR g_pkg[MAX_PATH], g_msi[MAX_PATH], g_ver[64], g_note[300];
static zipent g_zip;
static char g_pw[256];
static char *g_rtf;
static DWORD g_neterr;
static HFONT g_font, g_big;
static HBRUSH g_white, g_grey;
static void layout(void);
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

static void set_page(int p)
{
    static WCHAR b[1200];
    g_page = p;
    vis(ID_EDIT, FALSE); vis(ID_RICH, FALSE); vis(ID_CHECK, FALSE); vis(ID_PROG, FALSE); status(NULL, FALSE);
    switch (p) {
    case P_HELLO:
        if (g_installed) {
            text(ID_TITLE, T(L"Fix DynaRun V3 for this Windows", L"讓 DynaRun V3 在這台電腦正常運作"));
            wsprintfW(b, T(L"DynaRun V3 was found:\n%s\n\nThis installs the fixes for Windows 10/11 (flickering main screen, .Dpr files "
                           L"in OneDrive that do not open, garbled text, \"Run as administrator\").\n\n"
                           L"When Windows asks whether to allow changes, click \"Yes\".",
                           L"已找到 DynaRun V3：\n%s\n\n接下來會安裝 Windows 10/11 的相容性修正（主畫面閃爍、OneDrive 裡的 .Dpr 打不開、"
                           L"中文亂碼、以系統管理員執行卡住）。\n\nWindows 詢問「是否允許變更」時，請按「是」。"), g_exe);
            text(ID_BODY, b);
            buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
        } else {
            text(ID_TITLE, T(L"Install DynaRun V3", L"安裝 DynaRun V3"));
            text(ID_BODY, T(L"DynaRun V3 is not installed on this computer yet.\n\nThis program will:\n"
                            L"   1.  download the DynaRun V3 setup from the Dyna Pro website\n"
                            L"   2.  install DynaRun V3\n"
                            L"   3.  add the fixes for Windows 10/11\n\n"
                            L"You need the setup password that Dyna Pro gave you.",
                            L"這台電腦還沒有安裝 DynaRun V3。\n\n本程式會自動：\n"
                            L"   1.  從 Dyna Pro 官網下載 DynaRun V3 安裝檔\n"
                            L"   2.  安裝 DynaRun V3\n"
                            L"   3.  加上 Windows 10/11 的相容性修正\n\n"
                            L"過程中需要輸入 Dyna Pro 給你的安裝密碼。"));
            buttons(T(L"Start", L"開始"), T(L"Cancel", L"取消"), T(L"Installed elsewhere...", L"已裝在其他位置…"));
        }
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_FETCH:
        text(ID_TITLE, T(L"Downloading the setup", L"正在下載安裝檔"));
        text(ID_BODY, T(L"Downloading the DynaRun V3 setup from the Dyna Pro website (about 82 MB).\nThis can take a few minutes.",
                        L"正在從 Dyna Pro 官網下載 DynaRun V3 安裝檔（約 82 MB），可能需要幾分鐘。"));
        progress(FALSE); status(L" ", FALSE);
        buttons(NULL, T(L"Cancel", L"取消"), NULL);
        break;
    case P_FAIL:
        text(ID_TITLE, T(L"The setup could not be downloaded", L"無法自動下載安裝檔"));
        wsprintfW(b, T(L"%s\n\nDownload it yourself:\n   1.  click \"Open website\"\n   2.  on the page, click the download for \"Dyna Run V3 Software\"\n"
                       L"   3.  come back here and click \"Retry\" (or \"Choose file...\")",
                       L"%s\n\n請自己下載：\n   1.  按「開啟官網」\n   2.  在網頁上下載「Dyna Run V3 Software」\n"
                       L"   3.  下載完成後回到這裡按「重試」（或「選擇檔案…」）"), g_note);
        text(ID_BODY, b);
        buttons(T(L"Retry", L"重試"), T(L"Choose file...", L"選擇檔案…"), T(L"Open website", L"開啟官網"));
        break;
    case P_PASSWORD:
        text(ID_TITLE, T(L"Enter the setup password", L"輸入安裝密碼"));
        text(ID_BODY, T(L"Type the password that Dyna Pro gave you for the DynaRun V3 setup.\n(It is used only to open the setup file and is not saved.)",
                        L"請輸入 Dyna Pro 給你的 DynaRun V3 安裝密碼。\n（只用來打開安裝檔，不會被儲存。）"));
        vis(ID_EDIT, TRUE);
        buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), NULL);
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
        text(ID_BODY, T(L"Please read Dyna Pro's license agreement for DynaRun V3.", L"請閱讀 Dyna Pro 的 DynaRun V3 授權合約。"));
        vis(ID_RICH, TRUE); vis(ID_CHECK, TRUE);
        buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
        EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED);
        SetFocus(H(ID_CHECK));                       // the disabled "Install" button cannot hold the keyboard focus
        if (g_note[0]) status(g_note, TRUE);
        break;
    case P_INSTALL:
        text(ID_TITLE, T(L"Installing", L"安裝中"));
        text(ID_BODY, T(L"Installing, please wait. This takes a minute or two.\n\nWhen Windows asks whether to allow changes, click \"Yes\".",
                        L"正在安裝，請稍候，大約需要一兩分鐘。\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
        progress(TRUE);
        buttons(NULL, NULL, NULL);
        break;
    case P_DONE:
        text(ID_TITLE, T(L"Done", L"完成"));
        text(ID_BODY, T(L"Everything is installed.\n\nFrom now on, start DynaRun with the \"DynaRun V3\" icon on the desktop as usual.\n\n"
                        L"The first start asks for your dynamometer model. If Windows asks whether to allow changes, click \"Yes\".",
                        L"全部安裝完成。\n\n以後照常點桌面上的「DynaRun V3」圖示啟動即可。\n\n"
                        L"第一次啟動時會要你選擇馬力機型號；如果 Windows 詢問「是否允許變更」，請按「是」。"));
        buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);
        if (g_zh && legacy_acp() != 950)   // DynaRun's Chinese is Big5: only a zh-TW system locale shows it everywhere
            status(L"注意：這台電腦的「非 Unicode 程式的語言」不是中文（台灣），DynaRun 的中文可能會變成亂碼。\n"
                   L"請到「設定 → 時間與語言 → 語言與地區 → 系統管理語言設定 → 變更系統地區設定」選「中文（繁體，台灣）」，再重新開機。", TRUE);
        break;
    case P_ERROR:
        text(ID_TITLE, T(L"Something went wrong", L"發生問題"));
        text(ID_BODY, g_note);
        buttons(T(L"Close", L"關閉"), NULL, NULL);
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
    case JOB_EXTRACT:  rc = zip_extract(&g_zip, g_pw, g_msi, on_progress, &g_cancel); break;
    case JOB_INSTALL_ALL: rc = install_all(g_msi); break;
    case JOB_INSTALL_FIX: rc = install_fix(); break;
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

static void fetch(void)
{
    g_pkg_ours = FALSE;
    if (pkg_find_local(g_pkg)) { open_package(); return; }
    g_pkg_ours = TRUE;
    start_job(JOB_DOWNLOAD, P_FETCH);
}

static void after_extract(void)
{
    if (!msi_is_dynarun(g_msi, g_ver, 64)) {
        lstrcpyW(g_note, T(L"This file is not the DynaRun V3 setup.", L"這個檔案不是 DynaRun V3 的安裝檔。"));
        set_page(P_FAIL);
        return;
    }
    release(g_rtf);
    if ((g_rtf = msi_license_rtf(g_msi))) {
        rtfsrc src; EDITSTREAM es;
        src.p = g_rtf; src.left = lstrlenA(g_rtf);
        es.dwCookie = (DWORD_PTR)&src; es.dwError = 0; es.pfnCallback = rtf_in;
        SendMessageW(H(ID_RICH), EM_STREAMIN, SF_RTF, (LPARAM)&es);
        SendMessageW(H(ID_CHECK), BM_SETCHECK, BST_UNCHECKED, 0);
        set_page(P_LICENSE);
    } else start_job(JOB_INSTALL_ALL, P_INSTALL);
}

static void open_package(void)
{
    WCHAR *e = g_pkg + lstrlenW(g_pkg) - 4;
    if (e > g_pkg && !lstrcmpiW(e, L".msi")) { lstrcpyW(g_msi, g_pkg); after_extract(); return; }
    if (zip_open(g_pkg, &g_zip) != PK_OK) {
        lstrcpyW(g_note, T(L"The file is not the DynaRun V3 setup, or it is damaged.", L"這個檔案不是 DynaRun V3 安裝檔，或檔案已損毀。"));
        if (g_pkg_ours) DeleteFileW(g_pkg);
        set_page(P_FAIL);
        return;
    }
    pkg_temp_dir(g_msi); lstrcatW(g_msi, L"\\Setup.msi");
    if (g_zip.flags & 1) set_page(P_PASSWORD);
    else { g_pw[0] = 0; start_job(JOB_EXTRACT, P_EXTRACT); }
}

// The ZIP password is bytes; try the usual encodings of what was typed.
static BOOL take_password(void)
{
    static const UINT cps[] = { CP_OEMCP, CP_ACP, CP_UTF8 };
    WCHAR w[128]; int i;
    GetWindowTextW(H(ID_EDIT), w, 128);
    if (!w[0]) return FALSE;
    for (i = 0; i < 3; i++)
        if (WideCharToMultiByte(cps[i], 0, w, -1, g_pw, sizeof(g_pw), NULL, NULL) && zip_password_plausible(&g_zip, g_pw)) return TRUE;
    g_pw[0] = 0;
    return FALSE;
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
    lstrcpyW(g_pkg, f); g_pkg_ours = FALSE;
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

static void cleanup_files(void)
{
    WCHAR d[MAX_PATH], m[MAX_PATH];
    pkg_temp_dir(d); lstrcpyW(m, d); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(g_msi, m)) DeleteFileW(g_msi);
    if (g_pkg_ours) DeleteFileW(g_pkg);
    RemoveDirectoryW(d);
}

static void job_done(int job, int rc)
{
    WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = NULL; g_busy = FALSE;
    if (g_closing) { DestroyWindow(g_hwnd); return; }
    switch (job) {
    case JOB_DOWNLOAD:
        if (rc == PK_OK) { open_package(); break; }
        if (rc == PK_CANCEL) { DestroyWindow(g_hwnd); break; }
        if (rc == PK_HTTP) wsprintfW(g_note, T(L"The Dyna Pro website answered with error %lu.", L"Dyna Pro 官網回應錯誤 %lu。"), g_neterr);
        else if (rc == PK_IO) wsprintfW(g_note, T(L"The file could not be saved (error %lu).", L"無法儲存檔案（錯誤 %lu）。"), g_neterr);
        else wsprintfW(g_note, T(L"No connection to the Dyna Pro website (error %lu). Check the internet connection.",
                                L"連不上 Dyna Pro 官網（錯誤 %lu），請確認網路是否正常。"), g_neterr);
        set_page(P_FAIL);
        break;
    case JOB_EXTRACT:
        if (rc == PK_OK) { after_extract(); break; }
        if (rc == PK_CANCEL) { DestroyWindow(g_hwnd); break; }
        if (rc == PK_PASSWORD) { lstrcpyW(g_note, T(L"Wrong password, or the file is damaged. Please check the password.", L"密碼不正確，或檔案已損毀。請再確認密碼。")); set_page(P_PASSWORD); break; }
        lstrcpyW(g_note, T(L"The setup could not be unpacked. Is the disk full?", L"無法解開安裝檔，硬碟空間是否不足？"));
        set_page(P_ERROR);
        break;
    case JOB_INSTALL_ALL:
    case JOB_INSTALL_FIX:
        if (rc == 0) { cleanup_files(); set_page(P_DONE); break; }
        if (rc == 2) {
            lstrcpyW(g_note, T(L"Nothing was installed: Windows needs your \"Yes\" to install.", L"沒有安裝：需要在 Windows 詢問時按「是」才能安裝。"));
            set_page(job == JOB_INSTALL_ALL ? P_LICENSE : P_HELLO);
            break;
        }
        wsprintfW(g_note, T(L"The installation did not finish (code %d).\n\nRestart the computer and run this program again.",
                            L"安裝沒有完成（代碼 %d）。\n\n請重新開機後再執行一次本程式。"), rc);
        set_page(P_ERROR);
        break;
    }
}

static void on_primary(void)
{
    switch (g_page) {
    case P_HELLO:
        if (g_installed) start_job(JOB_INSTALL_FIX, P_INSTALL); else fetch();
        break;
    case P_FAIL: fetch(); break;
    case P_PASSWORD:
        if (!take_password()) { status(T(L"Wrong password. Please check it (upper/lower case matters).", L"密碼不正確，請再確認（大小寫有差別）。"), TRUE); SetFocus(H(ID_EDIT)); break; }
        start_job(JOB_EXTRACT, P_EXTRACT);
        break;
    case P_LICENSE: start_job(JOB_INSTALL_ALL, P_INSTALL); break;
    case P_DONE:
        ShellExecuteW(g_hwnd, NULL, g_launcher, NULL, NULL, SW_SHOWNORMAL);
        DestroyWindow(g_hwnd);
        break;
    case P_ERROR: DestroyWindow(g_hwnd); break;
    }
}

static void on_close(void)
{
    if (g_page == P_INSTALL) return;                 // msiexec / registry work must not be interrupted
    if (g_busy) { g_closing = TRUE; InterlockedExchange(&g_cancel, 1); return; }
    DestroyWindow(g_hwnd);
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
    const WCHAR *rich = LoadLibraryW(L"msftedit.dll") ? L"RICHEDIT50W" : (LoadLibraryW(L"riched20.dll"), L"RichEdit20W");
    add(L"STATIC", WS_VISIBLE | SS_NOPREFIX, ID_TITLE);
    add(L"STATIC", WS_VISIBLE | SS_NOPREFIX, ID_BODY);
    add(L"EDIT", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, ID_EDIT);
    add(rich, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY, ID_RICH);
    add(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CHECK);
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
    y = S(24);
    h = measure(ID_TITLE, w); place(ID_TITLE, m, y, w, h); y += h + S(16);
    h = measure(ID_BODY, w) + S(4); place(ID_BODY, m, y, w, h); y += h + S(20);
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

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDOK: case ID_PRIMARY: if (IsWindowVisible(H(ID_PRIMARY)) && IsWindowEnabled(H(ID_PRIMARY))) on_primary(); break;
        case IDCANCEL: case ID_SECOND:
            if (g_page == P_FAIL && LOWORD(w) == ID_SECOND) choose_file();
            else if (g_page != P_INSTALL) on_close();
            break;
        case ID_EXTRA:
            if (g_page == P_FAIL) ShellExecuteW(h, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL);
            else if (g_page == P_HELLO) choose_exe();
            break;
        case ID_CHECK: EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        }
        return 0;
    case WM_PROGRESS: {
        static WCHAR b[100];
        DWORD done = (DWORD)w, total = (DWORD)l;
        SendMessageW(H(ID_PROG), PBM_SETPOS, total ? MulDiv(done, 1000, total) : 0, 0);
        if (g_page == P_FETCH) {
            wsprintfW(b, total ? L"%lu.%lu / %lu.%lu MB" : L"%lu.%lu MB", done / 1048576, done % 1048576 / 104858,
                      total / 1048576, total % 1048576 / 104858);
            status(b, FALSE);
        }
        return 0;
    }
    case WM_JOBDONE: job_done((int)w, (int)l); return 0;
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
    case WM_DESTROY: PostQuitMessage(0); return 0;
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
    r.left = r.top = 0; r.right = S(620); r.bottom = S(480);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    wsprintfW(title, T(L"DynaRunFix Setup %s", L"DynaRunFix 安裝程式 %s"), WIDEN(DRF_VERSION));
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2, (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 1;
    create_controls();
    g_installed = locate_dynarun();
    set_page(P_HELLO);
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
        if (IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    g_hwnd = NULL;
    return g_page == P_DONE ? 0 : 2;
}
