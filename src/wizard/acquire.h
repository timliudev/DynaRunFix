// Getting the DynaRun V3 setup (and, through the same pages, the Dyna Pro USB driver): looking for a copy on this computer, the download with pause / resume,
// the one error page, the password, the check of the file and the license pages. Its jobs: JOB_ACQUIRE, JOB_EXTRACT.

// A download that can be continued where it stopped (its .part and ETag are there): appends how much is already received to wz.note and returns TRUE.
// The button then reads "Continue download" instead of "Retry".
static BOOL resume_note(void)
{
    DWORD have = 0; WCHAR a[24], t[24], line[200];
    if (!wz.acq->partial(&have)) return FALSE;
    fmt_mb(a, have);
    if (wz.dltotal > have) {
        fmt_mb(t, wz.dltotal);
        wsprintfW(line, TX(S_OF_MB_ARE_DOWNLOADED_CONTINUE), a, t);
    } else
        wsprintfW(line, TX(S_MB_ARE_DOWNLOADED_CONTINUE_DOWNLOAD), a);
    lstrcatW(wz.note, line);
    return TRUE;
}

// The size is the server's (Content-Length), shown once the download has started.
static void fetch_body(DWORD total)
{
    WCHAR size[40], b[500];
    size[0] = 0;
    if (total) wsprintfW(size, TX(S_MB), total / 1048576, total % 1048576 / 104858);
    wsprintfW(b, TX(S_GETTING_A_COPY_ON_THIS), ACQ_NAME(wz.acq), size);
    wz.total = total;
    text(ID_BODY, b);
}

static void enter_fetch(void)   // looking for a copy here, downloading, checking: the same page for the DynaRun setup and the driver
{
    WCHAR *b = pgbuf;
    wsprintfW(b, TX(S_GETTING), ACQ_NAME(wz.acq));
    text(ID_TITLE, b);
    fetch_body(0);
    progress(FALSE); status(L" ", FALSE);
    buttons(TX(S_PAUSE), TX(S_CANCEL), TX(S_OPEN_WEBSITE));
}

static void enter_fail(void)   // the one error page of the acquire flow
{
    wz.result = RC_FAIL;                                 // the acquire flow ended on its error page (Retry and the other ways on leave it)
    WCHAR *b = pgbuf;
    if (wz.badfile) lstrcpyW(b, TX(S_THIS_IS_NOT_THE_DYNARUN));
    else wsprintfW(b, TX(S_COULD_NOT_BE_DOWNLOADED), ACQ_NAME(wz.acq));
    if (!g_zh && !wz.badfile) b[0] = (WCHAR)(b[0] >= 'a' && b[0] <= 'z' ? b[0] - 32 : b[0]);          // "the DynaRun V3 setup" at the start of a sentence
    text(ID_TITLE, b);
    {
        BOOL resume = !wz.badfile && resume_note();            // a .part to carry on from (appends its size to the note)
        BOOL skip = wz.acq == &ACQ_DRIVER && can_skip_driver();
        wz.badfile = FALSE;
        wsprintfW(b, TX(S_GET_IT_YOURSELF_1_CLICK), wz.note, T(wz.acq->get_en, wz.acq->get_zh), T(wz.acq->pick_en, wz.acq->pick_zh));
        text(ID_BODY, b);
        buttons4(resume ? TX(S_CONTINUE_DOWNLOAD) : TX(S_RETRY), skip ? TX(S_SKIP) : TX(S_CANCEL),
                 TX(S_CHOOSE_FILE), TX(S_OPEN_WEBSITE));
    }
}

static void enter_password(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_ENTER_THE_SETUP_PASSWORD));
    wsprintfW(b, TX(S_TYPE_THE_PASSWORD_THAT_DYNA), file_name(wz.pkg));
    text(ID_BODY, b);
    vis(ID_EDIT, TRUE);
    buttons(TX(S_NEXT), TX(S_CANCEL), TX(S_CHOOSE_FILE));
    if (wz.note[0]) status(wz.note, TRUE);
    SetFocus(H(ID_EDIT)); SendMessageW(H(ID_EDIT), EM_SETSEL, 0, -1);
}

static void enter_extract(void)
{
    text(ID_TITLE, TX(S_OPENING_THE_SETUP_FILE));
    text(ID_BODY, TX(S_PLEASE_WAIT));
    progress(FALSE);
    buttons(NULL, TX(S_CANCEL), NULL);
}

// The notice for a setup whose version is not the tested one (not an error: the install goes on). VER_TESTED, and VER_V4PLUS (which has its own page), say nothing.
static void ver_notice(void)
{
    static WCHAR t[700];
    if (wz.verclass == VER_UNKNOWN) status(TX(S_VER_UNKNOWN_NOTICE), FALSE);
    else if (wz.verclass == VER_UNTESTED) { wsprintfW(t, TX(S_VER_UNTESTED_NOTICE), wz.ver, DYNARUN_TESTED_VERSION); status(t, FALSE); }
}

static void enter_check(void)   // everything we know about the setup file, to compare with what Dyna Pro gives
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_CHECK_THE_INSTALLER));
    lstrcpyW(b, TX(S_COMPARE_THIS_WITH_WHAT_DYNA));
    if (wz.pkg_found) lstrcatW(b, TX(S_FOUND_LOCAL_HINT));      // a copy found on this computer: the way to the website's file is offered
    text(ID_BODY, b);
    SetWindowTextW(H(ID_INFO), wz.info);
    vis(ID_INFO, TRUE);
    buttons4(TX(S_NEXT), TX(S_CANCEL), wz.pkg_found ? TX(S_DOWNLOAD_INSTEAD) : NULL, TX(S_OPEN_WEBSITE));
    if (wz.note[0]) status(wz.note, TRUE);
    else ver_notice();
}

// DynaRun V4 or later: the fix was never tested with it and V4 may not need it. Laid out like the license page: the check box where the accept box is, the
// buttons "Install the fix anyway" (needs the box), "Install original software only", "Cancel".
static void enter_ver4(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_V4_TITLE));
    wsprintfW(b, TX(S_V4_BODY), wz.ver, DYNARUN_TESTED_VERSION);
    text(ID_BODY, b);
    SetWindowTextW(H(ID_CHECK), TX(S_V4_CHECK));
    vis(ID_CHECK, TRUE);
    buttons4(TX(S_V4_INSTALL_FIX), TX(S_V4_ORIGINAL_ONLY), TX(S_CANCEL), NULL);
    EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED);
    SetFocus(H(ID_CHECK));
}

static void enter_license(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_DYNA_PRO_LICENSE_AGREEMENT));
    lstrcpyW(b, wz.nolicense ? TX(S_THE_LICENSE_AGREEMENT_IN_THIS)
                            : TX(S_PLEASE_READ_DYNA_PRO_S));
    text(ID_BODY, b);
    SetWindowTextW(H(ID_CHECK), wz.nolicense ? TX(S_I_UNDERSTAND_AND_ACCEPT_DYNA)
                                            : TX(S_I_ACCEPT_THE_LICENSE_AGREEMENT));
    vis(ID_RICH, TRUE); vis(ID_CHECK, TRUE);
    buttons(TX(S_INSTALL), TX(S_CANCEL), wz.nolicense ? TX(S_OPEN_WEBSITE) : NULL);
    EnableWindow(H(ID_PRIMARY), SendMessageW(H(ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED);
    SetFocus(H(ID_CHECK));                       // the disabled "Install" button cannot hold the keyboard focus
    if (wz.note[0]) status(wz.note, TRUE);
    else ver_notice();
}

// The progress callback of the worker (the downloads, the extraction, the driver check). It only posts; the percentage it compares with is the worker's own
// (g_out.lastpct, set to -1 by start_job): the UI never touches it.
static void on_progress(DWORD done, DWORD total)
{
    int pct = total ? MulDiv(done, 1000, total) : 0;
    if (done == (DWORD)-1) { PostMessageW(g_hwnd, WM_PROGRESS, done, total); return; }   // the "checking" phase (driver_fetch)
    if (pct == g_out.lastpct) return;
    g_out.lastpct = pct;
    PostMessageW(g_hwnd, WM_PROGRESS, done, total);
}

// What a job runs on the worker thread (job_proc), and what is done with its result on the UI thread (JOBS[].merge, then JOBS[].on_done, below).
// A jp_ function reads g_in and writes g_out, nothing else of the wizard's state.
static int jp_acquire(void)                          // the DynaRun setup or the driver: a copy on this computer first, else the download
{
    int rc;
    on_progress((DWORD)-1, 2);                       // the page shows "looking" (a moving bar) until a download starts
    if (g_in.acq == &ACQ_DRIVER) {
        rc = driver_fetch(g_out.drvzip, on_progress, &g_cancel, &g_out.neterr, &g_out.drv_how, g_out.drv_hex, &g_out.drv_size, g_out.drv_rep, 1500);
        if (rc == PK_OK || rc == DRV_W_HASH) {
            on_progress((DWORD)-1, 1);               // the page says "checking" (marquee) while the BitLocker state is asked
            g_out.drv_bl = driver_bitlocker();       // here, not while the next page is built: the WMI query can block for seconds
        }
    } else {
        g_out.pkg_found = g_in.acq_local_ok && pkg_find_local(g_out.pkg);
        if (g_out.pkg_found) rc = PK_OK;
        else { g_out.pkg_ours = TRUE; rc = pkg_download(g_out.pkg, on_progress, &g_cancel, &g_out.neterr); }
    }
    return rc;
}

// The UI takes what jp_acquire produced (after the join): for the driver its zip and check result, else the DynaRun setup's file.
static void mo_acquire(void)
{
    wz.neterr = g_out.neterr;
    if (g_in.acq == &ACQ_DRIVER) {
        CopyMemory(wz.drvzip, g_out.drvzip, sizeof(wz.drvzip));
        wz.drv_how = g_out.drv_how;
        CopyMemory(wz.drv_hex, g_out.drv_hex, sizeof(wz.drv_hex));
        wz.drv_size = g_out.drv_size;
        CopyMemory(wz.drv_rep, g_out.drv_rep, sizeof(wz.drv_rep));
        wz.drv_bl = g_out.drv_bl;
    } else {
        CopyMemory(wz.pkg, g_out.pkg, sizeof(wz.pkg));
        wz.pkg_found = g_out.pkg_found; wz.pkg_ours = g_out.pkg_ours;
    }
}

static int jp_extract(void)
{
    int rc = pkg_extract_checked(&g_in.zip, g_in.pw, g_in.msi, 400UL * 1024UL * 1024UL, on_progress, &g_cancel);   // a size cap, and a free target (shared with the driver)
    SecureZeroMemory(g_in.pw, sizeof(g_in.pw));      // the password is not needed any more
    return rc;
}

static void open_package(void);

typedef struct { const char *p; LONG left; } rtfsrc;

static DWORD CALLBACK rtf_in(DWORD_PTR c, LPBYTE b, LONG n, LONG *got)
{
    rtfsrc *s = (rtfsrc *)c; LONG i, k = n < s->left ? n : s->left;
    for (i = 0; i < k; i++) b[i] = (BYTE)s->p[i];
    s->p += k; s->left -= k; *got = k;
    return 0;
}

// Starts the acquire flow for the DynaRun setup or the driver (the same pages; a network check comes first inside the download).
static void acq_start(const acq_desc *d)
{
    wz.acq = d;
    if (d == &ACQ_SETUP) { wz.pkg_ours = wz.pkg_found = FALSE; wz.acq_local_ok = !wz.acq_fresh && wz.nfound < 8; }   // the cap: pkg_reject remembers 8 files
    else { wz.drvzip[0] = 0; wz.drv_allow = FALSE; }
    start_job(JOB_ACQUIRE, P_FETCH);
}

// Whatever the file is called: only the real DynaRun setup is ever installed.
static void not_dynarun(void)
{
    WCHAR m[MAX_PATH], nm[100];
    wz.acq = &ACQ_SETUP;
    lstrcpynW(nm, file_name(wz.pkg), 100);       // wsprintfW does not check the size of wz.note: a long file name must not overflow it
    wsprintfW(wz.note, TX(S_IS_NOT_THE_DYNARUN_V3), nm);
    pkg_reject(wz.pkg);
    pkg_temp_dir(m); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(wz.msi, m)) DeleteFileW(wz.msi);   // our extracted copy
    if (wz.pkg_ours) DeleteFileW(wz.pkg);
    if (wz.pkg_found) { acq_start(&ACQ_SETUP); return; }   // we picked it, not the user: quietly try the next one or download
    wz.badfile = TRUE;
    set_page(P_FAIL);
}

static void fmt_size(WCHAR *o, ULONGLONG n)
{
    DWORD v = (DWORD)n;
    wsprintfW(o, TX(S_BYTES_MB), v, v / 1048576, v % 1048576 / 104858);
}

// Shown on the check page so the user can compare with what Dyna Pro provides. Display only: nothing here blocks the install. The identity of the setup comes
// first (where it is from, the MSI's ProductVersion and Manufacturer), then the file: path, size, SHA-256 (and the same of the Setup.msi inside a zip).
static void build_info(void)
{
    WCHAR hex[65], hex2[65], ver[64], mfr[128], sz[80]; ULONGLONG size = 0, size2 = 0; BOOL same = !lstrcmpiW(wz.pkg, wz.msi), okp, okm = FALSE;
    HCURSOR old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    okp = pkg_sha256(wz.pkg, hex, &size);
    if (!same) okm = pkg_sha256(wz.msi, hex2, &size2);
    msi_info(wz.msi, ver, mfr, 64);
    SetCursor(old);
    wz.info[0] = 0;
    lstrcatW(wz.info, TX(S_SOURCE));
    lstrcatW(wz.info, wz.pkg_ours ? TX(S_DOWNLOADED_NOW_FROM_DYNAPRO_CO)
                     : wz.pkg_found ? TX(S_FOUND_ON_THIS_COMPUTER_NOT)
                     : TX(S_CHOSEN_BY_YOU));
    lstrcatW(wz.info, TX(S_PRODUCT_VERSION)); lstrcatW(wz.info, ver[0] ? ver : L"?");
    lstrcatW(wz.info, TX(S_MANUFACTURER)); lstrcatW(wz.info, mfr[0] ? mfr : L"?");
    lstrcatW(wz.info, TX(S_FILE)); lstrcatW(wz.info, wz.pkg);
    fmt_size(sz, size);
    lstrcatW(wz.info, TX(S_SIZE)); lstrcatW(wz.info, sz);
    lstrcatW(wz.info, L"\r\nSHA-256:\r\n"); lstrcatW(wz.info, okp ? hex : L"?");
    if (!same) {
        fmt_size(sz, size2);
        lstrcatW(wz.info, TX(S_SETUP_MSI_INSIDE_THE_FILE)); lstrcatW(wz.info, sz);
        lstrcatW(wz.info, L"\r\nSHA-256:\r\n"); lstrcatW(wz.info, okm ? hex2 : L"?");
    }
}

static void after_extract(void)
{
    WCHAR l[160];
    if (msi_check(wz.msi, wz.ver, 64) != MSI_DYNARUN) { not_dynarun(); return; }
    wz.verclass = dynarun_ver_class(wz.ver);                 // the version gate: the check page notices, the V4 page, the license page follow from it
    if (wz.flow == FLOW_MSI_ONLY) wz.flow = FLOW_MAIN;       // a new package is a new decision
    wsprintfW(l, L"setup version %s: class %d (0 unreadable, 1 tested, 2 other, 3 V4 or later)", wz.ver[0] ? wz.ver : L"?", wz.verclass); user_log(l);
    release(wz.rtf);
    wz.rtf = msi_license_rtf(wz.msi);
    wz.nolicense = !wz.rtf;
    build_info();
    if (wz.rtf) {
        rtfsrc src; EDITSTREAM es;
        src.p = wz.rtf; src.left = lstrlenA(wz.rtf);
        es.dwCookie = (DWORD_PTR)&src; es.dwError = 0; es.pfnCallback = rtf_in;
        SendMessageW(H(ID_RICH), EM_STREAMIN, SF_RTF, (LPARAM)&es);
    } else                                       // never install without the user accepting Dyna Pro's terms
        SetWindowTextW(H(ID_RICH), TX(S_DYNARUN_V3_IS_LICENSED_TO));
    SendMessageW(H(ID_CHECK), BM_SETCHECK, BST_UNCHECKED, 0);
    set_page(P_CHECK);
}

static void open_package(void)
{
    if (msi_check(wz.pkg, NULL, 0) != MSI_NONE) { lstrcpyW(wz.msi, wz.pkg); after_extract(); return; }   // by content, not by extension
    if (zip_open(wz.pkg, &wz.zip) != PK_OK) { wz.msi[0] = 0; not_dynarun(); return; }
    pkg_temp_dir(wz.msi); lstrcatW(wz.msi, L"\\Setup.msi");
    if (wz.zip.flags & 1) set_page(P_PASSWORD);
    else { wz.pw[0] = 0; start_job(JOB_EXTRACT, P_EXTRACT); }
}

// The ZIP password is bytes; try the usual encodings of what was typed.
static BOOL take_password(void)
{
    static const UINT cps[] = { CP_OEMCP, CP_ACP, CP_UTF8 };
    WCHAR w[128]; int i; BOOL ok = FALSE;
    GetWindowTextW(H(ID_EDIT), w, 128);
    if (!w[0]) return FALSE;
    for (i = 0; i < 3 && !ok; i++)
        ok = WideCharToMultiByte(cps[i], 0, w, -1, wz.pw, sizeof(wz.pw), NULL, NULL) && zip_password_plausible(&wz.zip, wz.pw);
    SecureZeroMemory(w, sizeof(w));
    if (!ok) SecureZeroMemory(wz.pw, sizeof(wz.pw));
    else SetWindowTextW(H(ID_EDIT), L"");           // the typed password does not stay in the edit control (wz.pw is wiped after the extraction)
    return ok;
}

static void choose_file(void)
{
    OPENFILENAMEW of; WCHAR f[MAX_PATH];
    zero(&of, sizeof(of)); f[0] = 0;
    of.lStructSize = sizeof(of); of.hwndOwner = g_hwnd;
    of.lpstrFilter = TX(S_DYNARUN_V3_SETUP_ZIP_MSI);
    of.lpstrFile = f; of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    if (wz.pkg_ours && lstrcmpiW(wz.pkg, f)) DeleteFileW(wz.pkg);   // the download that turned out not to be wanted
    lstrcpyW(wz.pkg, f); wz.pkg_ours = wz.pkg_found = FALSE;
    open_package();
}

static void drop_msi(void)                           // the decrypted Setup.msi in our temp folder (the user's own MSI is never touched)
{
    WCHAR d[MAX_PATH], m[MAX_PATH];
    pkg_temp_dir(d); lstrcpyW(m, d); lstrcatW(m, L"\\Setup.msi");
    if (wz.msi[0] && !lstrcmpiW(wz.msi, m)) DeleteFileW(wz.msi);
    RemoveDirectoryW(d);                             // only when empty
}

static void cleanup_files(void)
{
    WCHAR d[MAX_PATH], m[MAX_PATH];
    pkg_temp_dir(d); lstrcpyW(m, d); lstrcatW(m, L"\\Setup.msi");
    if (!lstrcmpiW(wz.msi, m)) DeleteFileW(wz.msi);
    if (wz.pkg_ours) DeleteFileW(wz.pkg);
    RemoveDirectoryW(d);
}

// The one text for every way the acquire flow can end badly (the DynaRun setup and the driver alike): wz.note.
static void acq_note(int rc)
{
    switch (rc) {
    case PK_CANCEL: lstrcpyW(wz.note, TX(S_THE_DOWNLOAD_WAS_STOPPED)); break;
    case PK_HTTP: wsprintfW(wz.note, TX(S_THE_DYNA_PRO_WEBSITE_ANSWERED), wz.neterr); break;
    case PK_CERT: wsprintfW(wz.note, TX(S_THE_SECURITY_CERTIFICATE_OF_THE), wz.neterr); break;
    case PK_URL: lstrcpyW(wz.note, TX(S_THE_DOWNLOAD_WAS_REFUSED_THE)); break;
    case PK_IO: wsprintfW(wz.note, TX(S_THE_FILE_COULD_NOT_BE), wz.neterr); break;
    case DRV_E_HASH: lstrcpyW(wz.note, TX(S_THAT_FILE_IS_NOT_A)); break;
    default:                                         // PK_NET and anything unforeseen: the first real error of the attempts
        if (wz.neterr == ERROR_INTERNET_DISCONNECTED) lstrcpyW(wz.note, TX(S_THIS_COMPUTER_IS_NOT_CONNECTED));
        else wsprintfW(wz.note, TX(S_NO_CONNECTION_TO_THE_DYNA), wz.neterr);
        break;
    }
}

// The one error page of the acquire flow.
static void acq_fail(int rc) { acq_note(rc); set_page(P_FAIL); }

// The download worker has ended because "Pause" was clicked: the .part and its ETag are kept, the page stays, the button becomes "Resume".
static void fetch_paused(void)
{
    WCHAR a[24], t[24], s[160];
    wz.paused = TRUE; wz.pause_req = FALSE;
    fmt_mb(a, wz.dlhave);
    if (wz.dltotal) { fmt_mb(t, wz.dltotal); wsprintfW(s, TX(S_PAUSED_OF_MB_DOWNLOADED), a, t); }
    else wsprintfW(s, TX(S_PAUSED_MB_DOWNLOADED), a);
    buttons(TX(S_RESUME), TX(S_CANCEL), TX(S_OPEN_WEBSITE));
    status(s, FALSE);
    layout();
}

// "Cancel" while paused (no worker is running any more): the same result as a cancel during the transfer.
static void fetch_stopped(void) { wz.paused = FALSE; acq_fail(PK_CANCEL); }

// The end of the acquire job. The DynaRun setup goes on to its password / check / license pages (open_package), the driver to its restart or warning page.
static void acq_done(int rc)
{
    BOOL drv = wz.acq == &ACQ_DRIVER;
    if ((rc == PK_OK || (drv && rc == DRV_W_HASH)) && g_cancel == 1) {                // "Cancel" while the file was being checked
        if (drv && !(wz.drv_how & DRV_SRC_LOCAL) && rc == DRV_W_HASH) driver_forget(wz.drvzip);
        acq_fail(PK_CANCEL);
        return;
    }
    if (rc == PK_CANCEL && wz.pause_req && g_cancel == 2) { fetch_paused(); return; }
    if (drv) { acq_driver_result(rc); return; }
    if (rc == PK_OK) { if (wz.pkg_found) wz.nfound++; open_package(); return; }
    acq_fail(rc);
}

static void jd_acquire(int job, int rc) { (void)job; acq_done(rc); }

static void jd_extract(int job, int rc)
{
    (void)job;
    if (rc == PK_OK) { after_extract(); return; }
    drop_msi();                                      // a failed or cancelled extraction leaves no decrypted copy
    if (rc == PK_CANCEL) { DestroyWindow(g_hwnd); return; }
    if (wz.pkg_found && rc == PK_FORMAT) {            // a copy found on this computer that is damaged (not a usable zip): not offered again; a password or disk problem keeps its own page
        WCHAR nm[100];
        lstrcpynW(nm, file_name(wz.pkg), 100);
        pkg_reject(wz.pkg);
        wz.acq = &ACQ_SETUP; wz.pkg_found = FALSE;
        wsprintfW(wz.note, TX(S_THE_FILE_FOUND_ON_THIS), nm);
        set_page(P_FAIL);
        return;
    }
    if (rc == PK_PASSWORD) { lstrcpyW(wz.note, TX(S_WRONG_PASSWORD_OR_THE_FILE)); set_page(P_PASSWORD); return; }
    lstrcpyW(wz.note, TX(S_THE_SETUP_COULD_NOT_BE));
    set_page(P_ERROR);
}

static void pri_fail(void) { acq_start(wz.acq); }                                          // Retry / Continue download

static void pri_password(void)
{
    if (!take_password()) {
        status(TX(S_WRONG_PASSWORD_PLEASE_CHECK_IT), TRUE);
        SetFocus(H(ID_EDIT));
        return;
    }
    start_job(JOB_EXTRACT, P_EXTRACT);
}

static void pri_check(void) { set_page(wz.verclass == VER_V4PLUS ? P_VER4 : P_LICENSE); }

// The V4 page: "Install the fix anyway" goes on to the license as for any version; "Install original software only" does too, but the install is Dyna Pro's MSI alone
// (FLOW_MSI_ONLY: no fix, no shortcuts, no pin, no sign-in start, no USB driver: the driver step belongs to the fix and was made for V3's unsigned FTDI driver).
static void pri_ver4(void) { set_page(P_LICENSE); }
static void sec_ver4(BOOL button)
{
    if (!button) { on_close(); return; }                    // Esc
    user_log(L"V4 or later: the user chose to install only Dyna Pro's original software");
    wz.flow = FLOW_MSI_ONLY; wz.drv = FALSE; wz.opts.autostart_later = FALSE;
    set_page(P_LICENSE);
}
static void thr_cancel(void) { on_close(); }

// "Download it" on the check page of a copy found on this computer: the same acquire flow, but the download (nothing found here is taken). The user's own file is not touched.
static void thr_check(void)
{
    if (!wz.pkg_found) return;
    user_log(L"the user chose the download from the Dyna Pro website instead of the copy found on this computer");
    drop_msi();                                              // our decrypted copy of it, if it was a zip
    wz.pkg_found = FALSE; wz.acq_fresh = TRUE;
    acq_start(&ACQ_SETUP);
}

static void pri_license(void) { if (wz.flow == FLOW_MSI_ONLY) install(JOB_INSTALL_MSI); else go_install(JOB_INSTALL_ALL); }

static void pri_fetch(void)                                                               // "Pause" / "Resume"
{
    if (wz.paused) { wz.paused = FALSE; start_job(JOB_ACQUIRE, P_FETCH); }                  // the worker had ended; Range / If-Range continue the .part
    else if (wz.busy && !wz.pause_req) {
        wz.pause_req = TRUE;
        InterlockedExchange(&g_cancel, 2);                                                // 2 = pause (1 = cancel): the worker stops cleanly between two reads and keeps the .part and its ETag
        text(ID_PRIMARY, TX(S_PAUSING)); EnableWindow(H(ID_PRIMARY), FALSE); layout();
    }
}

static void sec_fail(BOOL button)
{
    if (button && wz.acq == &ACQ_DRIVER && can_skip_driver()) skip_driver();               // "Skip": the fix without the driver
    else on_close();
}

static void sec_fetch(BOOL button)
{
    if (!button) { on_close(); return; }
    if (wz.paused) fetch_stopped();                  // paused: no worker is running, finish at once
    else InterlockedExchange(&g_cancel, 1);         // stop, then offer the other ways (also overrides a pause that is still taking effect)
}

// The third button (left of the second one): another way on. On the error page: choose the file (or folder) yourself.
static void thr_fail(void) { if (wz.acq == &ACQ_DRIVER) choose_driver_source(); else choose_file(); }

// The extra button (bottom left, link style).
static void ext_dynapro(void) { ShellExecuteW(g_hwnd, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL); }   // also after a failed driver download (P_DRV_HASH)
