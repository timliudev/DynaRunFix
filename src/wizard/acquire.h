// Getting the DynaRun V3 setup (and, through the same pages, the Dyna Pro USB driver): looking for a copy on this computer, the download with pause / resume,
// the one error page, the password, the check of the file and the license pages. Its jobs: JOB_ACQUIRE, JOB_EXTRACT.

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

static void enter_fetch(void)   // looking for a copy here, downloading, checking: the same page for the DynaRun setup and the driver
{
    WCHAR *b = pgbuf;
    wsprintfW(b, T(L"Getting %s", L"正在取得%s"), ACQ_NAME(g_acq));
    text(ID_TITLE, b);
    fetch_body(0);
    progress(FALSE); status(L" ", FALSE);
    buttons(T(L"Pause", L"暫停"), T(L"Cancel", L"取消"), T(L"Open website", L"開啟官網"));
}

static void enter_fail(void)   // the one error page of the acquire flow
{
    g_result = RC_FAIL;                                 // the acquire flow ended on its error page (Retry and the other ways on leave it)
    WCHAR *b = pgbuf;
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
}

static void enter_password(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, T(L"Enter the setup password", L"輸入安裝密碼"));
    wsprintfW(b, T(L"Type the password that Dyna Pro gave you for the DynaRun V3 setup.\n(It is used only to open the setup file and is not saved.)\n\nSetup file: %s",
                   L"請輸入 Dyna Pro 給你的 DynaRun V3 安裝密碼。\n（只用來打開安裝檔，不會被儲存。）\n\n安裝檔：%s"), file_name(g_pkg));
    text(ID_BODY, b);
    vis(ID_EDIT, TRUE);
    buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), T(L"Choose file...", L"選擇檔案…"));
    if (g_note[0]) status(g_note, TRUE);
    SetFocus(H(ID_EDIT)); SendMessageW(H(ID_EDIT), EM_SETSEL, 0, -1);
}

static void enter_extract(void)
{
    text(ID_TITLE, T(L"Opening the setup file", L"正在打開安裝檔"));
    text(ID_BODY, T(L"Please wait...", L"請稍候…"));
    progress(FALSE);
    buttons(NULL, T(L"Cancel", L"取消"), NULL);
}

static void enter_check(void)   // everything we know about the setup file, to compare with what Dyna Pro gives
{
    text(ID_TITLE, T(L"Check the installer", L"確認安裝檔"));
    text(ID_BODY, T(L"Compare this with what Dyna Pro gives you. Dyna Pro does not sign its setup, so DynaRunFix cannot prove that it is exactly what Dyna Pro released: install only a file you got from Dyna Pro.",
                    L"請與 Dyna Pro 提供的資訊比對。Dyna Pro 的安裝檔沒有數位簽章，DynaRunFix 無法證明它與 Dyna Pro 發布的完全一致，請只安裝從 Dyna Pro 取得的檔案。"));
    SetWindowTextW(H(ID_INFO), g_info);
    vis(ID_INFO, TRUE);
    buttons(T(L"Next", L"下一步"), T(L"Cancel", L"取消"), T(L"Open website", L"開啟官網"));
    if (g_note[0]) status(g_note, TRUE);
}

static void enter_license(void)
{
    WCHAR *b = pgbuf;
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
}

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

static void pri_fail(void) { acq_start(g_acq); }                                          // Retry / Continue download

static void pri_password(void)
{
    if (!take_password()) {
        status(T(L"Wrong password. Please check it (upper/lower case matters).\nIf the setup file above is not the DynaRun setup, click \"Other file...\".",
                 L"密碼不正確，請再確認（大小寫有差別）。\n如果上面的安裝檔不是 DynaRun 的，請按「選擇其他檔案…」。"), TRUE);
        SetFocus(H(ID_EDIT));
        return;
    }
    start_job(JOB_EXTRACT, P_EXTRACT);
}

static void pri_check(void) { set_page(P_LICENSE); }
static void pri_license(void) { go_install(JOB_INSTALL_ALL); }

static void pri_fetch(void)                                                               // "Pause" / "Resume"
{
    if (g_paused) { g_paused = FALSE; start_job(JOB_ACQUIRE, P_FETCH); }                  // the worker had ended; Range / If-Range continue the .part
    else if (g_busy && !g_pause_req) {
        g_pause_req = TRUE;
        InterlockedExchange(&g_cancel, 2);                                                // 2 = pause (1 = cancel): the worker stops cleanly between two reads and keeps the .part and its ETag
        text(ID_PRIMARY, T(L"Pausing...", L"暫停中…")); EnableWindow(H(ID_PRIMARY), FALSE); layout();
    }
}

static void sec_fail(BOOL button)
{
    if (button && g_acq == &ACQ_DRIVER && can_skip_driver()) skip_driver();               // "Skip": the fix without the driver
    else on_close();
}

static void sec_fetch(BOOL button)
{
    if (!button) { on_close(); return; }
    if (g_paused) fetch_stopped();                  // paused: no worker is running, finish at once
    else InterlockedExchange(&g_cancel, 1);         // stop, then offer the other ways (also overrides a pause that is still taking effect)
}

// The third button (left of the second one): another way on. On the error page: choose the file (or folder) yourself.
static void thr_fail(void) { if (g_acq == &ACQ_DRIVER) choose_driver_source(); else choose_file(); }

// The extra button (bottom left, link style).
static void ext_dynapro(void) { ShellExecuteW(g_hwnd, NULL, DYNAPRO_PAGE, NULL, NULL, SW_SHOWNORMAL); }   // also after a failed driver download (P_DRV_HASH)
