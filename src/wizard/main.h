// The main flow (the first page, the running-programs page, the install, Done, the error page), the job machinery (JOBS[], start_job, job_done), the page
// table PAGES[] with the window procedure, and wizard() itself.

static UINT StrToUint(const WCHAR *s) { UINT v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

// ANSI code page of the system locale, also when "Beta: UTF-8" makes the active one 65001
static UINT legacy_acp(void)
{
    WCHAR b[8];
    return GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, b, 8) ? StrToUint(b) : GetACP();
}

// "DynaRun V3", "DynaRunFix.exe" ... for the running-programs page
static void running_names(int run, WCHAR *out)
{
    out[0] = 0;
    if (run & RUN_DYNARUN) lstrcatW(out, L"   DynaRun V3\n");
    if (run & RUN_LAUNCHER) lstrcatW(out, T(L"   DynaRunFix.exe (starting DynaRun)\n", L"   DynaRunFix.exe（正在啟動 DynaRun）\n"));
    if (run & RUN_LEPROC) lstrcatW(out, L"   LEProc.exe (Locale Emulator)\n");
}

// Last paragraph of the first page.
static const WCHAR *disclaimer(void)
{
    return T(L"\n\nDynaRunFix is an independent open-source tool, not affiliated with or endorsed by Dyna Pro Dynamometers. "
             L"It is provided as is, without warranty (MIT license). DynaRun V3 is licensed to you separately by Dyna Pro.",
             L"\n\nDynaRunFix 是獨立的開源工具，與 Dyna Pro Dynamometers 無關，也未經其認可；依 MIT 授權按現狀提供，不提供任何擔保。"
             L"DynaRun V3 由 Dyna Pro 另行授權給你。");
}

// The line about a newer DynaRun on the first page, and its button. A hint only: nothing happens unless the button is clicked.
static void hint_line(WCHAR *b)
{
    WCHAR t[400];
    if (wz.hint == HINT_NEWER) {
        wsprintfW(t, T(L"\n\nA newer DynaRun V3 is available on Dyna Pro's website: %s (installed: %s). \"Download new version\" downloads and installs it; nothing happens unless you click it.",
                       L"\n\nDyna Pro 官網有新版 DynaRun V3：%s（目前已安裝：%s）。按「下載新版」會下載並安裝；不按就什麼都不會發生。"), wz.hint_new, wz.hint_inst);
        lstrcatW(b, t);
    } else if (wz.hint == HINT_ZIPDIFF)
        lstrcatW(b, T(L"\n\nDyna Pro's website may have a newer DynaRun V3 (the file there is not the one downloaded last time). \"Download new version\" gets it; nothing happens unless you click it.",
                      L"\n\nDyna Pro 官網可能有新版 DynaRun V3（那裡的檔案和上次下載的不同）。按「下載新版」會取得它；不按就什麼都不會發生。"));
}

static const WCHAR *hint_btn(void) { return wz.hint ? T(L"Download new version", L"下載新版") : NULL; }

static BOOL can_start_dynarun(void) { return wz.installed && g_exe[0] && exists(g_launcher); }

// What is said about the taskbar pin (wz.instres.pin, set by the user stage) and about DynaRun's Chinese text: on the page "Done" and, when the driver goes with
// the install (wz.fix_done: the page "Done" is skipped), on the restart page.
static void pin_hint(WCHAR *b)
{
    if (wz.instres.pin == PIN_NEXT_SIGNIN) lstrcatW(b, T(L"\n\nDynaRun will appear on the taskbar after you sign out and back in.", L"\n\n登出再登入後，DynaRun 會出現在工作列上。"));
    if (wz.instres.pin == PIN_FAILED) lstrcatW(b, os_build() >= 22000
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

static void enter_hello(void)
{
    WCHAR *b = pgbuf;
    if (wz.flow == FLOW_DRIVER_ONLY) {
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
        if (wz.note[0]) status(wz.note, TRUE);
        return;
    }
    if (wz.installed) {
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
    vis(ID_PIN, TRUE); vis(ID_AUTO, TRUE); vis(ID_DRV, wz.drv_avail);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_running(void)
{
    WCHAR *b = pgbuf;
    WCHAR names[200];
    running_names(wz.lastrun = running_programs(), names);
    text(ID_TITLE, T(L"DynaRun is running", L"DynaRun 正在執行"));
    if (wz.lastrun & RUN_NOCLOSE) {               // another user's, or one that cannot be identified: closing from here would not help
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
    if (wz.note[0]) status(wz.note, TRUE);
    SetTimer(g_hwnd, TIMER_RUNNING, 1000, NULL);
}

static void enter_install(void)
{
    text(ID_TITLE, T(L"Installing", L"安裝中"));
    text(ID_BODY, T(L"Installing, please wait. This takes a minute or two.\n\nWhen Windows asks whether to allow changes, click \"Yes\".",
                    L"正在安裝，請稍候，大約需要一兩分鐘。\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
    progress(TRUE);
    buttons(NULL, NULL, NULL);
}

static void enter_done(void)
{
    wz.result = RC_OK;                                   // everything is installed
    WCHAR *b = pgbuf;
    text(ID_TITLE, T(L"Done", L"完成"));
    lstrcpyW(b, T(L"Everything is installed.\n\nFrom now on, start DynaRun with the \"DynaRun V3\" icon on the desktop as usual.\n\n"
                  L"The first start asks for your dynamometer model. If Windows asks whether to allow changes, click \"Yes\".",
                  L"全部安裝完成。\n\n以後照常點桌面上的「DynaRun V3」圖示啟動即可。\n\n"
                  L"第一次啟動時會要你選擇馬力機型號；如果 Windows 詢問「是否允許變更」，請按「是」。"));
    pin_hint(b);
    if (wz.opts.autostart) lstrcatW(b, T(L"\n\nDynaRun will start automatically when you sign in.", L"\n\n以後開機登入後會自動啟動 DynaRun。"));
    lstrcatW(b, T(L"\n\nIf DynaRun's screen ever looks wrong, close DynaRun and start it again.", L"\n\n如果 DynaRun 的畫面顯示不正常，關掉 DynaRun 再開一次即可。"));
    text(ID_BODY, b);
    buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);
    acp_warning();
}

static void enter_error(void)
{
    wz.result = RC_FAIL;                                 // the install failed
    text(ID_TITLE, T(L"Something went wrong", L"發生問題"));
    text(ID_BODY, wz.note);
    buttons(T(L"Close", L"關閉"), NULL, NULL);
}

static int jp_install_all(void) { return g_in.flow == FLOW_MAIN_WITH_DRIVER ? install_all_driver(&g_in.opts, &g_out.instres, g_in.msi, g_in.drvzip, g_in.drv_allow ? g_in.drv_hex : NULL) : install_all(&g_in.opts, &g_out.instres, g_in.msi); }   // FLOW_MAIN_WITH_DRIVER: the driver's preparation in the same elevated stage
static int jp_install_fix(void) { return g_in.flow == FLOW_MAIN_WITH_DRIVER ? install_fix_driver(&g_in.opts, &g_out.instres, g_in.drvzip, g_in.drv_allow ? g_in.drv_hex : NULL) : install_fix(&g_in.opts, &g_out.instres); }
static void mo_install(void) { wz.instres = g_out.instres; }                // the pin hint

static void jd_install(int job, int rc);

// One row per job, indexed by the JOB_* value (nothing depends on the order of the values).
//   proc        runs on the worker thread; returns the job's result code. Reads g_in, writes g_out, touches nothing else of the wizard's state
//   merge       runs on the UI thread after the worker has been joined, before on_done: takes the part of g_out the job produced into wz (NULL: none)
//   on_done     runs on the UI thread when the worker has ended (job_done), with the job and its result
//   keep_up     the elevated stage or a system tool works: the UAC prompt and msiexec's window take the foreground, so ID_KEEPUP keeps the wizard behind them
//   bring_front the wizard comes back in front of other windows when the job is done (the elevated part allows it)
typedef struct {
    int (*proc)(void);
    void (*merge)(void);
    void (*on_done)(int job, int rc);
    BOOL keep_up, bring_front;
} job_desc;

static const job_desc JOBS[JOB_COUNT] = {
    [JOB_ACQUIRE]     = { jp_acquire,     mo_acquire, jd_acquire,     FALSE, FALSE },
    [JOB_EXTRACT]     = { jp_extract,     NULL,       jd_extract,     FALSE, FALSE },
    [JOB_INSTALL_ALL] = { jp_install_all, mo_install, jd_install,     TRUE,  TRUE  },
    [JOB_INSTALL_FIX] = { jp_install_fix, mo_install, jd_install,     TRUE,  TRUE  },
    [JOB_DRV_PREP]    = { jp_drv_prep,    NULL,       jd_drv_prep,    TRUE,  TRUE  },
    [JOB_DRV_INSTALL] = { jp_drv_install, NULL,       jd_drv_install, TRUE,  TRUE  },
    [JOB_DRV_CANCEL]  = { jp_drv_cancel,  NULL,       jd_drv_cancel,  TRUE,  TRUE  },
};

static DWORD WINAPI job_proc(LPVOID unused)
{
    int rc = JOBS[g_in.job].proc();
    PostMessageW(g_hwnd, WM_JOBDONE, g_in.job, rc);
    (void)unused;
    return 0;
}

// The update hint is asked in the background when the first page shows an installed DynaRun; the answer comes as WM_HINT. Never waited for.
// The thread writes g_hint_out and then posts WM_HINT; the UI reads g_hint_out only when that message arrives.
static DWORD WINAPI hint_proc(LPVOID unused)
{
    int r = pkg_update_hint(g_hint_out.inst, g_hint_out.nw, 40, user_log);
    if (g_hwnd) PostMessageW(g_hwnd, WM_HINT, (WPARAM)r, 0);
    (void)unused;
    return 0;
}

// Hands the job what it needs (g_in) and starts its output (g_out) as a copy of the UI's values, on the UI thread, before the worker exists. The password
// leaves the UI's copy here: the worker wipes the one it was given.
static void job_handover(int job)
{
    g_in.job = job; g_in.opts = wz.opts; g_in.flow = wz.flow;
    g_in.acq = wz.acq; g_in.acq_local_ok = wz.acq_local_ok;
    CopyMemory(g_in.msi, wz.msi, sizeof(g_in.msi));
    CopyMemory(&g_in.zip, &wz.zip, sizeof(g_in.zip));
    CopyMemory(g_in.pw, wz.pw, sizeof(g_in.pw)); SecureZeroMemory(wz.pw, sizeof(wz.pw));
    CopyMemory(g_in.drvzip, wz.drvzip, sizeof(g_in.drvzip)); g_in.drv_allow = wz.drv_allow;
    CopyMemory(g_in.drv_hex, wz.drv_hex, sizeof(g_in.drv_hex));
    g_out.lastpct = -1; g_out.neterr = wz.neterr;
    CopyMemory(g_out.pkg, wz.pkg, sizeof(g_out.pkg)); g_out.pkg_found = wz.pkg_found; g_out.pkg_ours = wz.pkg_ours;
    CopyMemory(g_out.drvzip, wz.drvzip, sizeof(g_out.drvzip)); g_out.drv_how = wz.drv_how;
    CopyMemory(g_out.drv_hex, wz.drv_hex, sizeof(g_out.drv_hex)); g_out.drv_size = wz.drv_size;
    CopyMemory(g_out.drv_rep, wz.drv_rep, sizeof(g_out.drv_rep)); g_out.drv_bl = wz.drv_bl;
    g_out.instres = wz.instres;
}

static void start_job(int job, int page)
{
    DWORD tid;
    g_cancel = 0; wz.busy = TRUE;
    if (job == JOB_ACQUIRE && wz.dlacq != wz.acq) { wz.dlacq = wz.acq; wz.dltotal = wz.dlhave = 0; }   // a resume of the same download keeps what is known
    set_page(page);
    job_handover(job);                                // the job runs with the options and the state as they are now
    wz.thread = CreateThread(NULL, 0, job_proc, NULL, 0, &tid);
    if (JOBS[job].keep_up) SetTimer(g_hwnd, ID_KEEPUP, 300, NULL);
}

// The install jobs start only once DynaRun is closed (see running_programs in setup.c).
static void install(int job)
{
    wz.pending = job;
    if (running_programs()) set_page(P_RUNNING);
    else start_job(job, P_INSTALL);
}

// The install of the fix (job: JOB_INSTALL_ALL / JOB_INSTALL_FIX). With the driver ticked its package is fetched and checked first (the driver pages), and the
// one elevated stage then does both: the UAC prompt comes once, before the restart. The driver-only first page (FLOW_DRIVER_ONLY) has no install: it starts the prep itself.
static void go_install(int job)
{
    if (wz.drv && wz.flow == FLOW_MAIN) { wz.flow = FLOW_MAIN_WITH_DRIVER; wz.inst_job = job; wz.note[0] = 0; set_page(P_DRV_INTRO); }
    else install(job);
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
    lstrcpyW(g_exe, f); wz.installed = TRUE;
    set_page(P_HELLO);
}

static void jd_install(int job, int rc)             // JOB_INSTALL_ALL / JOB_INSTALL_FIX
{
    if (wz.flow == FLOW_MAIN_WITH_DRIVER && FIX_DONE(rc)) {                    // the fix is installed, and the driver step ran in the same elevated stage: its result (as for JOB_DRV_PREP)
        cleanup_files();
        wz.fix_done = TRUE;
        if (rc == DRV_SUCCESS) drv_reboot_now();
        else if (rc == DRV_W_BDE) set_page(P_DRV_BDE);
        else drv_error(rc);
        return;
    }
    if (rc == RC_OK) { cleanup_files(); set_page(P_DONE); return; }
    if (rc == RC_RUNNING) { wz.pending = job; set_page(P_RUNNING); return; }   // DynaRun was started meanwhile
    if (rc == RC_CANCEL) {
        lstrcpyW(wz.note, T(L"Nothing was installed: Windows needs your \"Yes\" to install.", L"沒有安裝：需要在 Windows 詢問時按「是」才能安裝。"));
        set_page(wz.flow == FLOW_MAIN_WITH_DRIVER ? P_DRV_PRESS7 : job == JOB_INSTALL_ALL ? P_LICENSE : P_HELLO);   // FLOW_MAIN_WITH_DRIVER: the page with the button that started it
        return;
    }
    wsprintfW(wz.note, T(L"The installation did not finish (code %d).\n\nRestart the computer and run this program again.",
                        L"安裝沒有完成（代碼 %d）。\n\n請重新開機後再執行一次本程式。"), rc);
    set_page(P_ERROR);
}

static void job_done(int job, int rc)
{
    WaitForSingleObject(wz.thread, INFINITE); CloseHandle(wz.thread); wz.thread = NULL; wz.busy = FALSE;   // the join: what the worker wrote is ours now
    if (JOBS[job].merge) JOBS[job].merge();
    if (wz.close_asked) { wz.opts.close = FALSE; wz.close_asked = FALSE; }   // the consent was for the job that has just ended: a later stage must not close a DynaRun started since without asking (/close from the command line stays)
    if (wz.closing) { DestroyWindow(g_hwnd); return; }
    JOBS[job].on_done(job, rc);
}

// The first page's options are read when it is left, whichever button leaves it.
static void take_options(void)
{
    wz.opts.pin = SendMessageW(H(ID_PIN), BM_GETCHECK, 0, 0) == BST_CHECKED;
    wz.opts.autostart = SendMessageW(H(ID_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void end_wizard(void) { DestroyWindow(g_hwnd); }

// The primary button (right-most, Enter), page by page.
static void pri_hello(void)
{
    take_options();
    if (wz.flow == FLOW_DRIVER_ONLY) { wz.drv = TRUE; wz.opts.autostart_later = FALSE; set_page(P_DRV_INTRO); return; }   // the fix is current: straight to the driver
    wz.drv = wz.drv_avail && SendMessageW(H(ID_DRV), BM_GETCHECK, 0, 0) == BST_CHECKED;
    wz.opts.autostart_later = wz.drv;              // the driver restart comes first: the sign-in start is registered after it
    if (wz.installed) go_install(JOB_INSTALL_FIX); else { wz.acq_fresh = FALSE; acq_start(&ACQ_SETUP); }
}

static void pri_running(void)
{
    if (!wz.opts.close) { wz.opts.close = TRUE; wz.close_asked = TRUE; }   // the elevated stage closes DynaRun (see close_programs in setup.c), for this job only
    start_job(wz.pending, P_INSTALL);
}

static void pri_done(void)
{
    start_dynarun();
    end_wizard();
}

// Closing the window (the X, Alt+F4, and "Cancel" / "Close" where that is what leaving means). A page can refuse it (PF_NOCLOSE), or do something first
// (its close hook, which then also ends the wizard if that is what closing means there).
static void on_close(void)
{
    const page_desc *d = &PAGES[wz.page];
    if (d->flags & PF_NOCLOSE) return;                                                    // msiexec / registry / system tool work must not be interrupted
    if (wz.busy) { wz.closing = TRUE; InterlockedExchange(&g_cancel, 1); return; }
    if (d->close) d->close(); else end_wizard();
}

// The second button (and Esc): Cancel / Skip / Later / Close. A page without a handler closes. button FALSE: Esc.
static void sec_none(BOOL button) { (void)button; }                                       // the page "Installing": no way out

static void thr_hello(void)
{
    if (!wz.hint) return;                                          // "Download new version": the same acquire flow, then the normal install (the MSI upgrades DynaRun)
    take_options();
    wz.drv = wz.flow != FLOW_DRIVER_ONLY && wz.drv_avail && SendMessageW(H(ID_DRV), BM_GETCHECK, 0, 0) == BST_CHECKED;
    wz.opts.autostart_later = wz.drv;
    user_log(L"DynaRun update hint: the user chose to download the new version");
    wz.acq_fresh = TRUE;                                           // an older package found on this computer would not be the new version
    acq_start(&ACQ_SETUP);
}

static void ext_hello(void)                                                               // "Reinstall the fix..." / "Installed elsewhere..."
{
    if (wz.flow == FLOW_DRIVER_ONLY) { wz.flow = FLOW_MAIN; set_page(P_HELLO); }
    else choose_exe();
}

// Alt+key shortcuts that exist on one page only (the controls' own & mnemonics do not work while the focus is in the edit or the rich text).
static BOOL when_drv_avail(void) { return wz.drv_avail; }
static const hotkey KEYS_HELLO[] = { { 'T', ID_PIN, NULL }, { 'S', ID_AUTO, NULL }, { 'D', ID_DRV, when_drv_avail }, { 0 } };
static const hotkey KEYS_LICENSE[] = { { 'A', ID_CHECK, NULL }, { 0 } };

// One row per page, indexed by the P_* value. A NULL handler means: nothing happens (second: the page closes).
//   enter    builds the page (the controls, texts and buttons it shows); set_page has reset everything before it
//   primary  second  third  extra    what the four buttons do (ID_PRIMARY / Enter, ID_SECOND / Esc, ID_THIRD, ID_EXTRA)
//   close    what closing the window does on this page, after the common refusals; NULL: ends the wizard
//   keys     the page's Alt+key shortcuts
//   flags    PF_NOCLOSE: closing the window is refused
// A new page: a P_* value, an enter_* function, a row here.
static const page_desc PAGES[P_COUNT] = {
    [P_HELLO]       = { enter_hello,       pri_hello,       NULL,           thr_hello, ext_hello,        NULL,         KEYS_HELLO,   0 },
    [P_FETCH]       = { enter_fetch,       pri_fetch,       sec_fetch,      NULL,      ext_dynapro,      NULL,         NULL,         0 },
    [P_FAIL]        = { enter_fail,        pri_fail,        sec_fail,       thr_fail,  ext_dynapro,      NULL,         NULL,         0 },
    [P_PASSWORD]    = { enter_password,    pri_password,    NULL,           NULL,      choose_file,      NULL,         NULL,         0 },
    [P_EXTRACT]     = { enter_extract,     NULL,            NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_CHECK]       = { enter_check,       pri_check,       NULL,           NULL,      ext_dynapro,      NULL,         NULL,         0 },
    [P_LICENSE]     = { enter_license,     pri_license,     NULL,           NULL,      ext_dynapro,      NULL,         KEYS_LICENSE, 0 },
    [P_RUNNING]     = { enter_running,     pri_running,     NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_INSTALL]     = { enter_install,     NULL,            sec_none,       NULL,      NULL,             NULL,         NULL,         PF_NOCLOSE },
    [P_DONE]        = { enter_done,        pri_done,        NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_ERROR]       = { enter_error,       end_wizard,      NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_DRV_INTRO]   = { enter_drv_intro,   pri_drv_intro,   sec_drv_intro,  NULL,      NULL,             NULL,         NULL,         0 },
    [P_DRV_PRESS7]  = { enter_drv_press7,  pri_drv_press7,  NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_DRV_HASH]    = { enter_drv_hash,    pri_drv_hash,    sec_drv_hash,   NULL,      ext_dynapro,      NULL,         NULL,         0 },
    [P_DRV_BDE]     = { enter_drv_bde,     pri_drv_bde,     sec_drv_cancel, NULL,      ext_recovery_key, cls_drv_bde,  NULL,         0 },
    [P_DRV_PREP]    = { enter_drv_prep,    NULL,            NULL,           NULL,      NULL,             NULL,         NULL,         PF_NOCLOSE },
    [P_DRV_REBOOT]  = { enter_drv_reboot,  end_wizard,      NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_DRV_RETRY]   = { enter_drv_retry,   pri_drv_retry,   sec_drv_cancel, NULL,      NULL,             cls_drv_task, NULL,         0 },
    [P_DRV_WARN]    = { enter_drv_warn,    pri_drv_warn,    sec_drv_cancel, NULL,      NULL,             cls_drv_task, NULL,         0 },
    [P_DRV_INSTALL] = { enter_drv_install, NULL,            NULL,           NULL,      NULL,             NULL,         NULL,         PF_NOCLOSE },
    [P_DRV_DONE]    = { enter_drv_done,    pri_drv_done,    NULL,           NULL,      NULL,             NULL,         NULL,         0 },
    [P_DRV_ERROR]   = { enter_drv_error,   end_wizard,      NULL,           NULL,      ext_manual,       NULL,         NULL,         0 },
};

static void set_page(int p)
{
    wz.page = p;
    wz.result = RC_CANCEL;                               // undecided until a page below says otherwise
    wz.paused = wz.pause_req = FALSE;
    wz.checking = wz.marquee = FALSE;                     // a page starts with a plain bar; a download started again (also after the checking phase) shows its progress
    g_building = TRUE;                                  // status() does not lay out while the page is being rebuilt; the layout at the end does
    KillTimer(g_hwnd, TIMER_RUNNING);
    vis(ID_EDIT, FALSE); vis(ID_RICH, FALSE); vis(ID_CHECK, FALSE); vis(ID_PIN, FALSE); vis(ID_AUTO, FALSE); vis(ID_DRV, FALSE); vis(ID_IMG, FALSE); vis(ID_INFO, FALSE); vis(ID_PROG, FALSE); status(NULL, FALSE);
    PAGES[p].enter();
    wz.note[0] = 0;
    g_building = FALSE;
    layout();
    if (!GetFocus() || !IsWindowEnabled(GetFocus()) || !(GetWindowLongW(GetFocus(), GWL_STYLE) & WS_VISIBLE))
        SetFocus(GetNextDlgTabItem(g_hwnd, NULL, FALSE));       // something must hold the keyboard focus
}

// The window asks the table (the controls are the same on every page).
static void on_primary(void) { if (PAGES[wz.page].primary) PAGES[wz.page].primary(); }
static void on_second(BOOL button) { if (PAGES[wz.page].second) PAGES[wz.page].second(button); else on_close(); }
static void on_third(void) { if (PAGES[wz.page].third) PAGES[wz.page].third(); }
static void on_extra(void) { if (PAGES[wz.page].extra) PAGES[wz.page].extra(); }

// The control id of the Alt+key the current page has a shortcut for; 0 when there is none.
static int page_hotkey(WPARAM key)
{
    const hotkey *k = PAGES[wz.page].keys;
    for (; k && k->key; k++) if (k->key == key && (!k->when || k->when())) return k->id;
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDOK: case ID_PRIMARY: if (IsWindowVisible(H(ID_PRIMARY)) && IsWindowEnabled(H(ID_PRIMARY))) on_primary(); break;
        case IDCANCEL: case ID_SECOND: on_second(LOWORD(w) == ID_SECOND); break;
        case ID_THIRD: on_third(); break;
        case ID_EXTRA: on_extra(); break;
        case ID_CHECK: EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        }
        return 0;
    case WM_PROGRESS: {
        static WCHAR b[100];
        DWORD done = (DWORD)w, total = (DWORD)l;
        if (done == (DWORD)-1) {                    // download over, the file is being checked: not pausable, bar and text say so
            if (wz.page == P_FETCH && !wz.paused) {
                wz.marquee = TRUE;
                progress(TRUE);
                if (total == 2) { WCHAR s[160]; wsprintfW(s, T(L"Looking for %s on this computer...", L"正在尋找電腦上現有的%s…"), ACQ_NAME(wz.acq)); status(s, FALSE); }   // before anything is downloaded: a bar that moves
                else {                                                                                       // the file is being checked: not pausable
                    wz.checking = TRUE;
                    text(ID_TITLE, T(L"Checking the driver", L"正在檢查驅動程式"));
                    status(T(L"Checking the driver...", L"正在檢查驅動程式…"), FALSE);
                    EnableWindow(H(ID_PRIMARY), FALSE);
                }
                layout();
            }
            return 0;
        }
        if (wz.checking) return 0;
        if (total && wz.marquee) { wz.marquee = FALSE; progress(FALSE); }                                  // the size became known
        else if (!total && !wz.marquee && wz.page == P_FETCH) { wz.marquee = TRUE; progress(TRUE); }   // size unknown: a moving bar and the MB received
        if (!wz.marquee) SendMessageW(H(ID_PROG), PBM_SETPOS, total ? MulDiv(done, 1000, total) : 0, 0);
        wz.dlhave = done; if (total) wz.dltotal = total;
        if (wz.page == P_FETCH) {
            if (total != wz.total) { fetch_body(total); layout(); }
            wsprintfW(b, total ? L"%lu.%lu / %lu.%lu MB" : L"%lu.%lu MB", done / 1048576, done % 1048576 / 104858,
                      total / 1048576, total % 1048576 / 104858);
            status(b, FALSE);
        }
        return 0;
    }
    case WM_TIMER:                                  // the page follows DynaRun being closed (or started again)
        if (w == TIMER_RUNNING && wz.page == P_RUNNING) {
            int run = running_programs();
            if (run != wz.lastrun) {
                wz.lastrun = run;
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
        wz.hint = (int)w;
        if (wz.hint) { lstrcpyW(wz.hint_inst, g_hint_out.inst); lstrcpyW(wz.hint_new, g_hint_out.nw); }
        if (wz.hint && wz.page == P_HELLO && !wz.busy) set_page(P_HELLO);
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
        SetTextColor((HDC)w, (HWND)l == H(ID_STATUS) && wz.status_err ? RGB(192, 0, 0) : GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)g_white;
    case WM_CLOSE: on_close(); return 0;
    case WM_DESTROY: SecureZeroMemory(wz.pw, sizeof(wz.pw)); SecureZeroMemory(g_in.pw, sizeof(g_in.pw)); SetWindowTextW(H(ID_EDIT), L""); drop_msi(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int wizard(const install_opts *init)
{
    INITCOMMONCONTROLSEX icc; WNDCLASSW wc; NONCLIENTMETRICSW nm; RECT r; MSG msg; HDC dc; LOGFONTW lf; int key;
    static WCHAR title[100];
    wz.result = RC_CANCEL; wz.flow = FLOW_MAIN; wz.drv_bl = BL_UNKNOWN; wz.acq = &ACQ_SETUP;   // the starting values of the state that are not zero
    wz.opts = *init;                                  // the command line's options; the pages change this copy
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
    if (!g_hwnd) return RC_FAIL;
    create_controls();
    wz.installed = locate_dynarun();
    wz.drv_avail = g_drvmode == DRV_MODE_NONE && driver_supported() && !driver_installed();
    // g_drvmode (set by cmd_driver / cmd_driver_continue before the wizard starts) is read here and nowhere else
    if (g_drvmode == DRV_MODE_STANDALONE) wz.flow = FLOW_DRIVER_ONLY;
    else if (g_drvmode == DRV_MODE_CONTINUE) wz.flow = FLOW_DRIVER_CONTINUE;
    else wz.flow = wz.installed && wz.drv_avail && fix_current() ? FLOW_DRIVER_ONLY : FLOW_MAIN;   // the fix is there and current: only the driver is missing
    if (g_drvmode != DRV_MODE_NONE) drv_start();
    else {
        DWORD tid;
        set_page(P_HELLO);
        if (wz.installed) CloseHandle(CreateThread(NULL, 0, hint_proc, NULL, 0, &tid));      // is there a newer DynaRun? (a hint; the page does not wait)
    }
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {          // Tab between the visible controls
            HWND n = GetNextDlgTabItem(g_hwnd, GetFocus(), GetKeyState(VK_SHIFT) < 0);
            if (n) SetFocus(n);
            continue;
        }
        if (msg.message == WM_SYSKEYDOWN && (key = page_hotkey(msg.wParam)) != 0) {      // Alt+key shortcuts of the page
            SendMessageW(H(key), BM_CLICK, 0, 0);
            continue;
        }
        if (IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    g_hwnd = NULL;
    if (wz.page != P_DRV_REBOOT) autostart_flush();   // unless the restart for the driver is still to come: then the continuation does it
    return wz.result;
}
