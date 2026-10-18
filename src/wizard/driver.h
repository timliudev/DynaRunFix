// The Dyna Pro USB driver pages (driver.c does the work): the introduction, the restart menu, the warning for another zip, BitLocker, the restart, the
// pages after it, and the driver jobs (JOB_DRV_PREP / JOB_DRV_INSTALL / JOB_DRV_CANCEL).

// The "Start DynaRun" button is offered only when DynaRun is installed here and our launcher is in place (g_exe comes from locate_dynarun, which
// the wizard runs at start in every mode, so this also holds in the process the sign-in task starts after the restart; g_launcher from init_paths).
// The driver step may be skipped (the fix is installed without it) only in the main install flow, not when it is the whole job (the shortcut, /driver, or the driver-only first page).
static BOOL can_skip_driver(void) { return wz.flow == FLOW_MAIN || wz.flow == FLOW_MAIN_WITH_DRIVER; }

static void enter_drv_intro(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_INSTALL_THE_DYNA_PRO_USB));
    lstrcpyW(b, TX(S_WINDOWS_NEEDS_DYNA_PRO_S));
    if (wz.flow == FLOW_MAIN_WITH_DRIVER) lstrcatW(b, TX(S_DYNARUN_S_FIX_IS_INSTALLED));
    text(ID_BODY, b);
    buttons(TX(S_NEXT), can_skip_driver() ? TX(S_SKIP) : TX(S_CANCEL), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_press7(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, TX(S_AFTER_THE_RESTART_YOU_WILL));
    {
        int bl = wz.drv_bl;                          // asked in the worker (not elevated: -1 when WMI will not tell)
        b[0] = 0;
        if (wz.drv_allow) lstrcatW(b, TX(S_NOTE_THIS_DRIVER_FILE_IS));
        else if (wz.drv_how & DRV_SRC_LOCAL) {
            WCHAR nm[100], t[300]; lstrcpynW(nm, file_name(wz.drvzip), 100);   // the longest text: ~95 chars + 99 of nm
            if (wz.drv_how & DRV_SRC_DIR) wsprintfW(t, TX(S_THE_UNPACKED_DRIVER_FOLDER_WAS), nm);
            else wsprintfW(t, TX(S_THE_DRIVER_WAS_FOUND_ON), nm);
            lstrcatW(b, t);
        }
        if (!wz.drv_allow && (wz.drv_how & DRV_ZIPDIFF)) lstrcatW(b, TX(S_THE_ZIP_DIFFERS_FROM_THE));
        if (wz.flow == FLOW_MAIN_WITH_DRIVER) lstrcatW(b, TX(S_CLICK_INSTALL_AND_RESTART_DYNARUN));
        lstrcatW(b, TX(S_TIP_TAKE_A_PHOTO_OF));
        if (bl > 0) lstrcatW(b, TX(S_THIS_COMPUTER_USES_BITLOCKER_PROTECTION));
        else if (bl < 0) lstrcatW(b, TX(S_IF_THIS_COMPUTER_USES_BITLOCKER));
        text(ID_BODY, b);
    }
    show_image(0);
    if (wz.flow == FLOW_MAIN_WITH_DRIVER) buttons(TX(S_INSTALL_AND_RESTART), TX(S_CANCEL), NULL);   // nothing is installed yet
    else buttons(TX(S_RESTART_NOW), TX(S_LATER), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_hash(void)   // a driver zip that is not the file this program knows: the user decides
{
    WCHAR *b = pgbuf;
    static WCHAR src[2 * MAX_PATH + 40];
    if (wz.drv_how & DRV_SRC_LOCAL) { lstrcpyW(src, (wz.drv_how & DRV_SRC_DIR) ? TX(S_FOUND_ON_THIS_COMPUTER_UNPACKED) : TX(S_FOUND_ON_THIS_COMPUTER)); lstrcatW(src, wz.drvzip); }
    else lstrcpyW(src, TX(S_DOWNLOADED_FROM_THE_DYNA_PRO));
    text(ID_TITLE, TX(S_THE_DRIVER_DIFFERS_FROM_THE));
    // wsprintfW cuts its output at 1024 characters, so the text is put together with bounded appends (b holds 2600): the advice to
    // cancel comes before the details, and the list of files goes last so that only the end of the list can be lost
    b[0] = 0;
    if (wz.drv_how & DRV_SRC_DIR) {                  // an unpacked folder: there is no zip to show a size or hash of
        cat_to(b, 2600, TX(S_THE_DRIVER_FILES_IN_THIS));
        cat_to(b, 2600, src);
        cat_to(b, 2600, L"\n\n");
    } else {
        WCHAR sz[24];
        wsprintfW(sz, L"%lu", (DWORD)wz.drv_size);
        cat_to(b, 2600, TX(S_THE_DRIVER_FILES_IN_THIS_2));
        cat_to(b, 2600, src);
        cat_to(b, 2600, TX(S_ZIP_SIZE));
        cat_to(b, 2600, sz);
        cat_to(b, 2600, TX(S_BYTES_ZIP_SHA_256));
        cat_to(b, 2600, wz.drv_hex);
        cat_to(b, 2600, TX(S_KNOWN_ZIP_SHA_256));
        cat_to(b, 2600, driver_pinned_sha256());
        cat_to(b, 2600, L"\n\n");
    }
    cat_to(b, 2600, TX(S_FILES_THAT_DIFFER_ACTUAL_EXPECTED));
    {
        int room = 2600 - lstrlenW(b) - 48;        // keep room for the "and N more" line
        const WCHAR *p = wz.drv_rep;
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
            wsprintfW(t, TX(S_AND_MORE), more);
            cat_to(b, 2600, t);
        }
    }
    text(ID_BODY, b);
    // "Cancel" is the default button (the right-most one, Enter); "Install anyway" is the second; the website is bottom-left
    buttons(TX(S_CANCEL), TX(S_INSTALL_ANYWAY), TX(S_OPEN_WEBSITE));
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_bde(void)
{
    text(ID_TITLE, TX(S_BITLOCKER_COULD_NOT_BE_SUSPENDED));
    text(ID_BODY, TX(S_THIS_COMPUTER_USES_BITLOCKER_AND));
    buttons(TX(S_RESTART_ANYWAY), TX(S_LATER), TX(S_FIND_THE_RECOVERY_KEY));
}

static void enter_drv_prep(void)
{
    text(ID_TITLE, TX(S_PLEASE_WAIT_2));
    text(ID_BODY, TX(S_GETTING_READY_WHEN_WINDOWS_ASKS));
    progress(TRUE);
    buttons(NULL, NULL, NULL);
}

static void enter_drv_reboot(void)
{
    wz.result = RC_OK;                                   // the driver step is prepared; the restart continues it
    WCHAR *b = pgbuf;
    text(ID_TITLE, wz.drv_noreboot ? TX(S_PLEASE_RESTART_THE_COMPUTER) : TX(S_RESTARTING));
    lstrcpyW(b, wz.drv_noreboot
        ? TX(S_EVERYTHING_IS_READY_BUT_WINDOWS)
        : TX(S_THE_COMPUTER_RESTARTS_NOW_WHEN));
    if (wz.fix_done) pin_hint(b);                      // the fix was installed with the driver: the hints of the page "Done", which is skipped
    text(ID_BODY, b);
    buttons(TX(S_CLOSE), NULL, NULL);
    if (wz.fix_done) acp_warning();
}

static void enter_drv_retry(void)
{
    text(ID_TITLE, TX(S_7_WAS_NOT_PRESSED));
    text(ID_BODY, TX(S_THE_RESTART_MENU_WAS_SKIPPED));
    buttons(TX(S_RETRY), TX(S_CANCEL), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_warn(void)
{
    text(ID_TITLE, TX(S_LAST_STEP_INSTALL_THE_DRIVER));
    text(ID_BODY, TX(S_DRIVER_SIGNATURE_CHECKING_IS_OFF));
    show_image(1);
    buttons(TX(S_INSTALL), TX(S_CANCEL), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_install(void)
{
    text(ID_TITLE, TX(S_INSTALLING_THE_DRIVER));
    text(ID_BODY, TX(S_INSTALLING_PLEASE_WAIT_IF_WINDOWS));
    progress(TRUE);
    buttons(NULL, NULL, NULL);
}

static void enter_drv_done(void)
{
    wz.result = RC_OK;                                   // the driver is installed (or was already)
    text(ID_TITLE, wz.drv_have ? TX(S_THE_DRIVER_IS_ALREADY_INSTALLED) : TX(S_THE_DRIVER_IS_INSTALLED));
    text(ID_BODY, wz.drv_have
        ? TX(S_THE_DYNA_PRO_USB_DRIVER)
        : TX(S_CONNECT_THE_DYNA_PRO_DYNAMOMETER));
    if (can_start_dynarun()) buttons(TX(S_START_DYNARUN), TX(S_CLOSE), NULL);   // only after a success (this page); a failure shows P_DRV_ERROR
    else buttons(TX(S_CLOSE), NULL, NULL);
}

static void enter_drv_error(void)
{
    wz.result = RC_FAIL;                                 // the driver step failed
    text(ID_TITLE, TX(S_THE_DRIVER_WAS_NOT_INSTALLED));
    text(ID_BODY, wz.note);
    buttons(TX(S_CLOSE), NULL, TX(S_OPEN_THE_MANUAL_STEPS));
}

static int jp_drv_prep(void)
{
    // "install anyway" accepts exactly this zip (its SHA-256), nothing else.
    return driver_prep(&g_in.opts, g_in.drvzip[0] ? g_in.drvzip : L"-", g_in.drv_allow ? g_in.drv_hex : NULL);
}

static int jp_drv_install(void) { return driver_install(&g_in.opts); }
static int jp_drv_cancel(void) { return driver_cancel(&g_in.opts); }

// "Skip" on the driver pages of the main flow: the fix is installed without the driver (the sign-in start is then registered by the install itself).
static void skip_driver(void)
{
    user_log(L"driver: skipped, the fix is installed without it");
    wz.drv = FALSE; wz.opts.autostart_later = FALSE; wz.flow = FLOW_MAIN;
    install(wz.inst_job);
}

// The text of a failed driver step (rc: PK_* of the download, DRV_E_* of driver.c) and the error page.
static void drv_error(int rc)
{
    switch (rc) {
    case DRV_E_HASH:
        lstrcpyW(wz.note, TX(S_THE_DOWNLOADED_FILE_IS_NOT));
        break;
    case DRV_E_BCD: lstrcpyW(wz.note, TX(S_WINDOWS_COULD_NOT_BE_TOLD)); break;
    case DRV_E_TASK: lstrcpyW(wz.note, TX(S_THE_TASK_THAT_CONTINUES_THE)); break;
    case DRV_E_DIR: lstrcpyW(wz.note, TX(S_THE_WORKING_FOLDER_PROGRAMDATA_DYNARUNFIX)); break;
    case DRV_E_PNP: lstrcpyW(wz.note, TX(S_WINDOWS_DID_NOT_ACCEPT_THE)); break;
    default: wsprintfW(wz.note, TX(S_THE_DRIVER_COULD_NOT_BE), rc); break;
    }
    lstrcatW(wz.note, TX(S_YOU_CAN_INSTALL_THE_DRIVER));
    if (wz.fix_done) lstrcatW(wz.note, TX(S_DYNARUN_S_FIX_ITSELF_IS));
    set_page(P_DRV_ERROR);
}

static void drv_reboot_now(void)
{
    wz.drv_noreboot = !driver_reboot();
    set_page(P_DRV_REBOOT);
}

// /driver and /driver-continue: the first page
static void drv_start(void)
{
    if (!driver_supported()) {
        DWORD b = os_build();         // 9200+: Windows 8 / 8.1, 6000+: Vista / 7, below: XP
        lstrcpyW(wz.note, TX(S_THIS_VERSION_OF_WINDOWS_IS));
        if (b >= 9200)
            lstrcatW(wz.note, TX(S_WINDOWS_8_8_1_HOLD));
        else if (b >= 6000)
            lstrcatW(wz.note, TX(S_WINDOWS_VISTA_7_RESTART_AND));
        else
            lstrcatW(wz.note, TX(S_WINDOWS_XP_DOES_NOT_ENFORCE));
        lstrcatW(wz.note, TX(S_THE_MANUAL_STEPS_HAVE_THE));
        set_page(P_DRV_ERROR);
        return;
    }
    wz.drv_have = driver_installed();
    if (wz.flow == FLOW_DRIVER_CONTINUE) {
        if (wz.drv_have) { if (is_admin()) driver_cancel(&wz.opts); set_page(P_DRV_DONE); }       // installed meanwhile: the waiting task goes
        else set_page(driver_ci_state() == DRV_CI_ENFORCED ? P_DRV_RETRY : P_DRV_WARN);
    } else set_page(wz.drv_have ? P_DRV_DONE : P_DRV_INTRO);
}

// The choice on the warning page: install the zip anyway (the elevated step is told, see driver_prep) or cancel. Both are logged.
static void hash_choice(BOOL install)
{
    static WCHAR l[2 * MAX_PATH + 120];
    lstrcpyW(l, install ? L"driver zip with another hash: the user chose to install it anyway: " : L"driver zip with another hash: the user cancelled: ");
    lstrcatW(l, wz.drvzip); user_log(l);
    if (install) { wz.drv_allow = TRUE; set_page(P_DRV_PRESS7); return; }
    if (!(wz.drv_how & DRV_SRC_LOCAL)) driver_forget(wz.drvzip);             // our own refused download is not offered again; a file of the user's stays
    if ((wz.drv_how & DRV_SRC_LOCAL) && !(wz.drv_how & DRV_SRC_PICKED)) { driver_skip_local_other(); user_log(L"the declined copy was found automatically: automatic searches skip such copies from now on"); }   // later tries download
    else if (wz.drv_how & DRV_SRC_PICKED) user_log(L"the declined file was chosen by the user: automatic searches are not changed");
    wz.drvzip[0] = 0; wz.drv_allow = FALSE;
    lstrcpyW(wz.note, ((wz.drv_how & DRV_SRC_LOCAL) && !(wz.drv_how & DRV_SRC_PICKED)) ? TX(S_CANCELLED_THE_DRIVER_WAS_NOT)
                                                 : TX(S_CANCELLED_THE_DRIVER_WAS_NOT_2));
    set_page(P_DRV_INTRO);
}

// What the driver check says (from the search, the download or a file the user chose): on to the restart page, to the warning page, or the error page.
static void acq_driver_result(int rc)
{
    if (rc == PK_OK) {
        wz.drv_allow = FALSE;
        if (!g_bmp[0]) g_bmp[0] = load_png(18, &g_bw[0], &g_bh[0]);                 // the picture is decoded before the page changes, so the new page is built at once
        set_page(P_DRV_PRESS7);
    } else if (rc == DRV_W_HASH) {
        WCHAR m[2 * MAX_PATH + 100];
        wz.drv_allow = FALSE;
        lstrcpyW(m, L"driver package with other files: the warning page is shown for "); lstrcatW(m, wz.drvzip); user_log(m);
        set_page(P_DRV_HASH);
    } else acq_fail(rc);
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
        spec[0].pszName = TX(S_DYNA_PRO_USB_DRIVER_ZIP); spec[0].pszSpec = L"*.zip;*.inf";
        spec[1].pszName = TX(S_ALL_FILES); spec[1].pszSpec = L"*.*";
        fd->lpVtbl->SetFileTypes(fd, 2, spec);
        fd->lpVtbl->SetTitle(fd, TX(S_CHOOSE_THE_DYNA_PRO_USB));
        fd->lpVtbl->SetOkButtonLabel(fd, TX(S_USE_THIS));
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
        of.lpstrFilter = TX(S_DYNA_PRO_USB_DRIVER_ZIP_2);
        of.lpstrTitle = TX(S_CHOOSE_DYNA_PRO_USB_ZIP);
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
    wz.acq = &ACQ_DRIVER; lstrcpyW(wz.drvzip, p);
    old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    rc = driver_check(wz.drvzip, &wz.drv_how, wz.drv_hex, &wz.drv_size, wz.drv_rep, 1500);
    wz.drv_how |= DRV_SRC_PICKED;                                           // chosen by the user: always checked; declining its warning does not touch the memory of declined auto-found copies
    user_log(L"driver: a file the user chose is checked as it is (the skip of declined auto-found copies does not apply to it)");
    wz.drv_bl = driver_bitlocker();
    SetCursor(old);
    acq_driver_result(rc);
}

static void jd_drv_prep(int job, int rc)
{
    (void)job;
    if (rc == DRV_SUCCESS) { drv_reboot_now(); return; }
    if (rc == DRV_W_BDE) { set_page(P_DRV_BDE); return; }      // armed, but BitLocker is still on: the user decides
    if (rc == RC_CANCEL) {
        lstrcpyW(wz.note, TX(S_NOTHING_WAS_CHANGED_WINDOWS_NEEDS));
        set_page(wz.flow == FLOW_DRIVER_CONTINUE ? P_DRV_RETRY : P_DRV_PRESS7);
        return;
    }
    drv_error(rc);
}

static void jd_drv_install(int job, int rc)
{
    (void)job;
    if (rc == DRV_SUCCESS) { wz.drv_have = FALSE; set_page(P_DRV_DONE); return; }
    if (rc == RC_CANCEL) { lstrcpyW(wz.note, TX(S_NOTHING_WAS_INSTALLED_WINDOWS_NEEDS)); set_page(P_DRV_WARN); return; }
    if (rc == DRV_E_ENFORCED) {                        // this start still checks signatures: nothing was installed, the restart stays armed
        lstrcpyW(wz.note, TX(S_DRIVER_SIGNATURE_CHECKING_IS_STILL));
        set_page(P_DRV_RETRY); return;
    }
    drv_error(rc);
}

static void jd_drv_cancel(int job, int rc)
{
    (void)job; (void)rc;
    end_wizard();                                    // cancelled (or not allowed to: nothing else can be done from here)
}

static void pri_drv_intro(void) { acq_start(&ACQ_DRIVER); }
static void pri_drv_press7(void) { if (wz.flow == FLOW_MAIN_WITH_DRIVER) install(wz.inst_job); else start_job(JOB_DRV_PREP, P_DRV_PREP); }   // FLOW_MAIN_WITH_DRIVER: the fix and the driver preparation in one elevated stage
static void pri_drv_hash(void) { hash_choice(FALSE); }                                    // "Cancel" is the default button
static void pri_drv_retry(void) { wz.drvzip[0] = 0; start_job(JOB_DRV_PREP, P_DRV_PREP); }       // the staged driver is still there
static void pri_drv_warn(void) { start_job(JOB_DRV_INSTALL, P_DRV_INSTALL); }
static void pri_drv_bde(void) { drv_reboot_now(); }                                       // "Restart anyway"

static void pri_drv_done(void)
{
    if (can_start_dynarun()) start_dynarun();                                             // "Start DynaRun" (otherwise the button reads "Close")
    end_wizard();
}

// after the restart: closing the window ends the installation like "Cancel" (done at once when this runs elevated, as the task does)
static void cls_drv_task(void)
{
    if (wz.flow == FLOW_DRIVER_CONTINUE && is_admin()) driver_cancel(&wz.opts);
    end_wizard();
}

static void cls_drv_bde(void) { start_job(JOB_DRV_CANCEL, P_DRV_PREP); }                  // the task, the menu flag and the suspension were armed: take them back first

static void sec_drv_intro(BOOL button)
{
    if (button && can_skip_driver()) skip_driver();                                       // "Skip": the fix without the driver
    else on_close();
}

static void sec_drv_hash(BOOL button) { hash_choice(button); }                            // the button is "Install anyway"; Esc is a cancel
static void sec_drv_cancel(BOOL button) { (void)button; start_job(JOB_DRV_CANCEL, P_DRV_PREP); }   // the task, the menu flag and BitLocker back
static void ext_recovery_key(void) { ShellExecuteW(g_hwnd, NULL, L"https://aka.ms/myrecoverykey", NULL, NULL, SW_SHOWNORMAL); }
static void ext_manual(void) { ShellExecuteW(g_hwnd, NULL, DRIVER_DOC_URL, NULL, NULL, SW_SHOWNORMAL); }
