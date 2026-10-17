// The Dyna Pro USB driver pages (driver.c does the work): the introduction, the restart menu, the warning for another zip, BitLocker, the restart, the
// pages after it, and the driver jobs (JOB_DRV_PREP / JOB_DRV_INSTALL / JOB_DRV_CANCEL).

// The "Start DynaRun" button is offered only when DynaRun is installed here and our launcher is in place (g_exe comes from locate_dynarun, which
// the wizard runs at start in every mode, so this also holds in the process the sign-in task starts after the restart; g_launcher from init_paths).
// The driver step may be skipped (the fix is installed without it) only in the main install flow, not when it is the whole job (the shortcut, /driver, or the driver-only first page).
static BOOL can_skip_driver(void) { return wz.flow == FLOW_MAIN || wz.flow == FLOW_MAIN_WITH_DRIVER; }

static void enter_drv_intro(void)
{
    WCHAR *b = pgbuf;
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
    if (wz.flow == FLOW_MAIN_WITH_DRIVER) lstrcatW(b, T(L"\n\nDynaRun's fix is installed in the same step, just before the restart, so Windows asks for permission only once.",
                             L"\n\nDynaRun 的修正會在同一步、重新開機之前一起安裝，所以 Windows 只會詢問一次是否允許變更。"));
    text(ID_BODY, b);
    buttons(T(L"Next", L"下一步"), can_skip_driver() ? T(L"Skip", L"略過") : T(L"Cancel", L"取消"), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_press7(void)
{
    WCHAR *b = pgbuf;
    text(ID_TITLE, T(L"After the restart you will see this: press 7", L"重新開機後會看到這個畫面，請按 7"));
    {
        int bl = wz.drv_bl;                          // asked in the worker (not elevated: -1 when WMI will not tell)
        b[0] = 0;
        if (wz.drv_allow) lstrcatW(b, T(L"Note: this driver file is not the version DynaRunFix knows; you chose to install it.\n\n", L"注意：這個驅動程式檔和已知版本不同，是你確認要安裝的。\n\n"));
        else if (wz.drv_how & DRV_SRC_LOCAL) {
            WCHAR nm[100], t[300]; lstrcpynW(nm, file_name(wz.drvzip), 100);   // the longest text: ~95 chars + 99 of nm
            if (wz.drv_how & DRV_SRC_DIR) wsprintfW(t, T(L"The unpacked driver folder (%s) was found on this computer and checked: no need to download it.\n\n", L"在這台電腦上找到已解開的驅動程式資料夾（%s），檢查通過，不用重新下載。\n\n"), nm);
            else wsprintfW(t, T(L"The driver (%s) was found on this computer and checked: no need to download it again.\n\n", L"在這台電腦上找到驅動程式（%s），檢查通過，不用重新下載。\n\n"), nm);
            lstrcatW(b, t);
        }
        if (!wz.drv_allow && (wz.drv_how & DRV_ZIPDIFF)) lstrcatW(b, T(L"The zip differs from the known version, but the driver files are identical: it can be installed.\n\n", L"壓縮檔和已知版本不同，但驅動程式檔案相同，可以安裝。\n\n"));
        if (wz.flow == FLOW_MAIN_WITH_DRIVER) lstrcatW(b, T(L"Click \"Install and restart\": DynaRun's fix is installed and the driver is prepared in one step (Windows asks for permission once), then the computer restarts.\n\n",
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
    if (wz.flow == FLOW_MAIN_WITH_DRIVER) buttons(T(L"Install and restart", L"安裝並重新開機"), T(L"Cancel", L"取消"), NULL);   // nothing is installed yet
    else buttons(T(L"Restart now", L"重新開機"), T(L"Later", L"稍後"), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_hash(void)   // a driver zip that is not the file this program knows: the user decides
{
    WCHAR *b = pgbuf;
    static WCHAR src[2 * MAX_PATH + 40];
    if (wz.drv_how & DRV_SRC_LOCAL) { lstrcpyW(src, (wz.drv_how & DRV_SRC_DIR) ? T(L"found on this computer (unpacked folder): ", L"在這台電腦上找到（已解開的資料夾）：") : T(L"found on this computer: ", L"在這台電腦上找到：")); lstrcatW(src, wz.drvzip); }
    else lstrcpyW(src, T(L"downloaded from the Dyna Pro website (dynapro.co.uk)", L"從 Dyna Pro 官網下載"));
    text(ID_TITLE, T(L"The driver differs from the known version", L"驅動程式和已知版本不同"));
    // wsprintfW cuts its output at 1024 characters, so the text is put together with bounded appends (b holds 2600): the advice to
    // cancel comes before the details, and the list of files goes last so that only the end of the list can be lost
    b[0] = 0;
    if (wz.drv_how & DRV_SRC_DIR) {                  // an unpacked folder: there is no zip to show a size or hash of
        cat_to(b, 2600, T(L"The driver files in this folder are not the version DynaRunFix knows. Dyna Pro may have updated them, or the files may be damaged or tampered with.\n\n"
                          L"Install them only if you are sure that they come from Dyna Pro. If in doubt, click \"Cancel\".\n\nSource: ",
                          L"這個資料夾裡的驅動程式檔和 DynaRunFix 已知的版本不同。可能是 Dyna Pro 更新了驅動程式，也可能是檔案損毀或被竄改。\n\n"
                          L"確定檔案來自 Dyna Pro 才按「仍要安裝」；不確定請按「取消」。\n\n來源："));
        cat_to(b, 2600, src);
        cat_to(b, 2600, L"\n\n");
    } else {
        WCHAR sz[24];
        wsprintfW(sz, L"%lu", (DWORD)wz.drv_size);
        cat_to(b, 2600, T(L"The driver files in this package are not the version DynaRunFix knows. Dyna Pro may have updated them, or the file may be damaged or tampered with.\n\n"
                          L"Install it only if you are sure that it comes from Dyna Pro. If in doubt, click \"Cancel\".\n\nSource: ",
                          L"這個驅動程式包裡的驅動程式檔和 DynaRunFix 已知的版本不同。可能是 Dyna Pro 更新了驅動程式，也可能是檔案損毀或被竄改。\n\n"
                          L"確定檔案來自 Dyna Pro 才按「仍要安裝」；不確定請按「取消」。\n\n來源："));
        cat_to(b, 2600, src);
        cat_to(b, 2600, T(L"\nZip size: ", L"\n壓縮檔大小："));
        cat_to(b, 2600, sz);
        cat_to(b, 2600, T(L" bytes\nZip SHA-256: ", L" 位元組\n壓縮檔 SHA-256："));
        cat_to(b, 2600, wz.drv_hex);
        cat_to(b, 2600, T(L"\nKnown zip SHA-256: ", L"\n已知的壓縮檔 SHA-256："));
        cat_to(b, 2600, driver_pinned_sha256());
        cat_to(b, 2600, L"\n\n");
    }
    cat_to(b, 2600, T(L"Files that differ (actual / expected):\n", L"不同的檔案（實際 / 已知）：\n"));
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
            wsprintfW(t, T(L"  (and %d more)", L"  （另外還有 %d 個）"), more);
            cat_to(b, 2600, t);
        }
    }
    text(ID_BODY, b);
    // "Cancel" is the default button (the right-most one, Enter); "Install anyway" is the second; the website is bottom-left
    buttons(T(L"Cancel", L"取消"), T(L"Install anyway", L"仍要安裝"), T(L"Open website", L"開啟官網"));
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_bde(void)
{
    text(ID_TITLE, T(L"BitLocker could not be suspended", L"BitLocker 沒有暫停成功"));
    text(ID_BODY, T(L"This computer uses BitLocker, and its protection could not be suspended for the restart. After the restart Windows will probably ask for the "
                    L"BitLocker recovery key before it shows the start-up menu.\n\nYou can look the key up at https://aka.ms/myrecoverykey (Microsoft account), or ask your administrator. "
                    L"Without the key, choose \"Later\", suspend BitLocker yourself (Settings > Privacy & security > Device encryption, or Control Panel > BitLocker) and try again.",
                    L"這台電腦有開 BitLocker，但重新開機前無法暫停它的保護。重新開機後 Windows 很可能會先要求輸入 BitLocker 修復金鑰（「您必須輸入修復金鑰以存取啟動設定」），才會顯示選單。\n\n"
                    L"修復金鑰可到 https://aka.ms/myrecoverykey 用微軟帳戶查詢，或詢問系統管理員。沒有金鑰的話，請按「稍後」，自己先暫停 BitLocker（設定 → 隱私權與安全性 → 裝置加密，或控制台 → BitLocker）再試一次。"));
    buttons(T(L"Restart anyway", L"仍要重新開機"), T(L"Later", L"稍後"), T(L"Find the recovery key", L"查詢修復金鑰"));
}

static void enter_drv_prep(void)
{
    text(ID_TITLE, T(L"Please wait", L"請稍候"));
    text(ID_BODY, T(L"Getting ready...\n\nWhen Windows asks whether to allow changes, click \"Yes\".", L"正在準備…\n\nWindows 詢問「是否允許變更」時，請按「是」。"));
    progress(TRUE);
    buttons(NULL, NULL, NULL);
}

static void enter_drv_reboot(void)
{
    wz.result = RC_OK;                                   // the driver step is prepared; the restart continues it
    WCHAR *b = pgbuf;
    text(ID_TITLE, wz.drv_noreboot ? T(L"Please restart the computer", L"請重新開機") : T(L"Restarting", L"即將重新開機"));
    lstrcpyW(b, wz.drv_noreboot
        ? T(L"Everything is ready, but Windows would not restart by itself.\n\nPlease restart the computer yourself (Start, the power button, Restart). "
            L"When the menu appears, press 7. After you sign in, this program continues by itself.",
            L"都準備好了，但 Windows 沒有自動重新開機。\n\n請自己重新開機（開始 → 電源 → 重新啟動）。出現選單時請按 7；登入後本程式會自動繼續。")
        : T(L"The computer restarts now.\n\nWhen the menu appears, press 7. After you sign in, this program continues by itself.\n\n"
            L"If the computer does not restart (a program is waiting for you), save your work and restart it yourself.",
            L"電腦即將重新開機。\n\n出現選單時請按 7；登入後本程式會自動繼續。\n\n如果電腦沒有重新開機（有程式在等你回應），請存好資料後自己重新開機。"));
    if (wz.fix_done) pin_hint(b);                      // the fix was installed with the driver: the hints of the page "Done", which is skipped
    text(ID_BODY, b);
    buttons(T(L"Close", L"關閉"), NULL, NULL);
    if (wz.fix_done) acp_warning();
}

static void enter_drv_retry(void)
{
    text(ID_TITLE, T(L"7 was not pressed", L"剛才沒有按到 7"));
    text(ID_BODY, T(L"The restart menu was skipped, so Windows still checks driver signatures and the driver cannot be installed yet.\n\n"
                    L"Try again? The computer restarts once more. When the menu appears, press 7.",
                    L"剛才重新開機時沒有選到「停用驅動程式強制簽章」（按 7），所以 Windows 還在檢查簽章，驅動程式現在還不能安裝。\n\n"
                    L"要再試一次嗎？電腦會再重新開機一次，出現選單時請按 7。"));
    buttons(T(L"Retry", L"重試"), T(L"Cancel", L"取消"), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_warn(void)
{
    text(ID_TITLE, T(L"Last step: install the driver", L"最後一步：安裝驅動程式"));
    text(ID_BODY, T(L"Driver signature checking is off for this start. Next, Windows will show a red security window like this one twice (once for each driver file): click \"Install this driver software anyway\" each time.",
                    L"這次開機已暫時關閉簽章檢查。接下來 Windows 會跳出像下面這樣的紅色安全性視窗兩次（兩個驅動程式檔各一次），請每次都按「仍然安裝此驅動程式軟體」。"));
    show_image(1);
    buttons(T(L"Install", L"安裝"), T(L"Cancel", L"取消"), NULL);
    if (wz.note[0]) status(wz.note, TRUE);
}

static void enter_drv_install(void)
{
    text(ID_TITLE, T(L"Installing the driver", L"正在安裝驅動程式"));
    text(ID_BODY, T(L"Installing, please wait.\n\nIf Windows shows a red warning, choose \"Install this driver software anyway\".\nIf Windows asks whether to allow changes, click \"Yes\".",
                    L"正在安裝，請稍候。\n\n如果 Windows 跳出紅色警告，請選「仍然安裝此驅動程式軟體」。\nWindows 詢問「是否允許變更」時，請按「是」。"));
    progress(TRUE);
    buttons(NULL, NULL, NULL);
}

static void enter_drv_done(void)
{
    wz.result = RC_OK;                                   // the driver is installed (or was already)
    text(ID_TITLE, wz.drv_have ? T(L"The driver is already installed", L"驅動程式已安裝") : T(L"The driver is installed", L"驅動程式安裝完成"));
    text(ID_BODY, wz.drv_have
        ? T(L"The Dyna Pro USB driver is already installed on this computer. Nothing more to do.",
            L"這台電腦已經安裝過 Dyna Pro USB 驅動程式，不需要再裝。")
        : T(L"Connect the Dyna Pro dynamometer by USB and switch it on: Windows recognizes it by itself.\n\n"
            L"Driver signature checking comes back by itself the next time you restart the computer normally. Nothing else to set.",
            L"請用 USB 接上 Dyna Pro 馬力機並開機，Windows 會自動認得它。\n\n"
            L"驅動程式簽章檢查會在下次正常重新開機後自動恢復，不需要再做任何設定。"));
    if (can_start_dynarun()) buttons(T(L"Start DynaRun", L"開始使用 DynaRun"), T(L"Close", L"關閉"), NULL);   // only after a success (this page); a failure shows P_DRV_ERROR
    else buttons(T(L"Close", L"關閉"), NULL, NULL);
}

static void enter_drv_error(void)
{
    wz.result = RC_FAIL;                                 // the driver step failed
    text(ID_TITLE, T(L"The driver was not installed", L"驅動程式沒有安裝"));
    text(ID_BODY, wz.note);
    buttons(T(L"Close", L"關閉"), NULL, T(L"Open the manual steps", L"開啟手動安裝說明"));
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
        lstrcpyW(wz.note, T(L"The downloaded file is not the one this program was made for (its SHA-256 differs), so it was not installed. Dyna Pro may have changed the file.",
                           L"下載到的檔案和這個程式預期的不一樣（SHA-256 不符），為了安全沒有安裝。可能是 Dyna Pro 更新了檔案。"));
        break;
    case DRV_E_BCD: lstrcpyW(wz.note, T(L"Windows could not be told to show its start-up menu at the next start (bcdedit failed).", L"無法設定「下次開機顯示進階選單」（bcdedit 失敗）。")); break;
    case DRV_E_TASK: lstrcpyW(wz.note, T(L"The task that continues the installation after the restart could not be created.", L"無法建立重新開機後繼續安裝的排程工作。")); break;
    case DRV_E_DIR: lstrcpyW(wz.note, T(L"The working folder %ProgramData%\\DynaRunFix could not be made safe (an old folder is still in use, or it can be changed by other users). Nothing was installed. Close other programs, restart the PC and try again.",
                                       L"無法把工作資料夾 %ProgramData%\\DynaRunFix 設為安全狀態（舊資料夾仍在使用中，或其他使用者可以修改它）。沒有安裝任何東西。請關閉其他程式、重新啟動電腦後再試一次。")); break;
    case DRV_E_PNP: lstrcpyW(wz.note, T(L"Windows did not accept the driver. Driver signature checking may still have been on.", L"Windows 沒有接受這個驅動程式，可能是簽章檢查還開著。")); break;
    default: wsprintfW(wz.note, T(L"The driver could not be prepared (code %d).", L"無法準備驅動程式（代碼 %d）。"), rc); break;
    }
    lstrcatW(wz.note, T(L"\n\nYou can install the driver by hand instead: click \"Open the manual steps\".", L"\n\n也可以改用手動方式安裝，請按「開啟手動安裝說明」。"));
    if (wz.fix_done) lstrcatW(wz.note, T(L"\n\nDynaRun's fix itself is installed. To try the driver again later, use \"Install Dyna Pro USB driver\" in the Start menu.",
                                       L"\n\nDynaRun 的修正本身已經安裝好了。之後想再試一次驅動程式，可以用開始功能表裡的「安裝 Dyna Pro USB 驅動程式」。"));
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
        lstrcpyW(wz.note, T(L"This version of Windows is too old for the automatic installation (it needs Windows 10 or later). Install the driver by hand:\n\n",
                           L"這個 Windows 版本太舊，無法自動安裝（需要 Windows 10 或更新版本）。請手動安裝：\n\n"));
        if (b >= 9200)
            lstrcatW(wz.note, T(L"Windows 8 / 8.1: hold Shift and click Restart, then Troubleshoot > Advanced options > Startup Settings > Restart, and press 7 (Disable driver signature enforcement; 32-bit editions do not enforce driver signing and only show the warning, so they can skip this). "
                               L"Then install the driver in Device Manager (Update driver > Browse my computer) and choose \"Install this driver software anyway\" on the red warning.",
                               L"Windows 8 / 8.1：按住 Shift 再按「重新啟動」，依序選 疑難排解 > 進階選項 > 啟動設定 > 重新啟動，然後按 7（停用驅動程式強制簽章；32 位元版不強制驅動程式簽章、只會顯示警告，可略過這一步）。"
                               L"接著在裝置管理員安裝驅動程式（更新驅動程式 > 瀏覽電腦上的驅動程式），並在紅色警告選「仍然安裝此驅動程式軟體」。"));
        else if (b >= 6000)
            lstrcatW(wz.note, T(L"Windows Vista / 7: restart and press F8 repeatedly before the Windows logo appears, choose \"Disable Driver Signature Enforcement\" in Advanced Boot Options "
                               L"(32-bit editions do not enforce driver signing and only show the warning, so they can skip this). Then in Device Manager use Update Driver > Browse my computer > Have Disk, and choose \"Install this driver software anyway\" on the red warning.",
                               L"Windows Vista / 7：重新開機，在出現 Windows 標誌之前連續按 F8，在「進階開機選項」選「停用驅動程式強制簽章」（32 位元版不強制驅動程式簽章、只會顯示警告，可略過這一步）。"
                               L"接著在裝置管理員用 更新驅動程式 > 瀏覽電腦 > 從磁片安裝，並在紅色警告選「仍然安裝此驅動程式軟體」。"));
        else
            lstrcatW(wz.note, T(L"Windows XP does not enforce driver signatures. Install the driver in Device Manager; when Windows says the software has not passed Windows Logo testing, click \"Continue Anyway\".",
                               L"Windows XP 不強制驅動程式簽章。請在裝置管理員安裝驅動程式；Windows 說軟體未通過 Windows 標誌測試時，按「仍然繼續」。"));
        lstrcatW(wz.note, T(L"\n\nThe manual steps have the details: click \"Open the manual steps\".", L"\n\n詳細說明請按「開啟手動安裝說明」。"));
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
    lstrcpyW(wz.note, ((wz.drv_how & DRV_SRC_LOCAL) && !(wz.drv_how & DRV_SRC_PICKED)) ? T(L"Cancelled: the driver was not installed. Trying again downloads it from the Dyna Pro website.", L"已取消，沒有安裝驅動程式。再試一次會從 Dyna Pro 官網下載。")
                                                 : T(L"Cancelled: the driver was not installed.", L"已取消，沒有安裝驅動程式。"));
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
        lstrcpyW(wz.note, T(L"Nothing was changed: Windows needs your \"Yes\" first.", L"沒有做任何變更：需要在 Windows 詢問時按「是」。"));
        set_page(wz.flow == FLOW_DRIVER_CONTINUE ? P_DRV_RETRY : P_DRV_PRESS7);
        return;
    }
    drv_error(rc);
}

static void jd_drv_install(int job, int rc)
{
    (void)job;
    if (rc == DRV_SUCCESS) { wz.drv_have = FALSE; set_page(P_DRV_DONE); return; }
    if (rc == RC_CANCEL) { lstrcpyW(wz.note, T(L"Nothing was installed: Windows needs your \"Yes\" first.", L"沒有安裝：需要在 Windows 詢問時按「是」。")); set_page(P_DRV_WARN); return; }
    if (rc == DRV_E_ENFORCED) {                        // this start still checks signatures: nothing was installed, the restart stays armed
        lstrcpyW(wz.note, T(L"Driver signature checking is still on in this Windows session, so nothing was installed. The restart with key 7 is needed first.", L"這次 Windows 啟動時簽章檢查還開著，所以沒有安裝任何東西。需要先重新開機並按 7。"));
        set_page(P_DRV_RETRY); return;
    }
    drv_error(rc);
}

static void jd_drv_cancel(int job, int rc)
{
    (void)job; (void)rc;
    DestroyWindow(g_hwnd);                           // cancelled (or not allowed to: nothing else can be done from here)
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
