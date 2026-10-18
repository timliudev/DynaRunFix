// Part of setup.c (user stage and install entry points): included by it, in one compilation unit; never compiled on its own.
// Not elevated: the current user's shortcuts; a desktop shortcut when there is none at all.
static void user_stage(const install_opts *o, install_result *res)
{
    static WCHAR p[MAX_PATH]; HKEY b, s;
    res->pin = PIN_NOT_ASKED;
    if (!app_key_create(APP_USER, &b)) return;
    // another version is installed now: what update.c noted no longer applies; the first start of DynaRun looks again
    RegDeleteValueW(b, L"UpdateChecked"); RegDeleteValueW(b, L"UpdateVersion"); RegDeleteValueW(b, L"UpdateSha256"); RegDeleteValueW(b, L"UpdateAsk"); RegDeleteValueW(b, L"UpdateSkip");
    if (!RegCreateKeyExW(b, L"Shortcuts", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL)) {
        int desk = fix_shortcuts(FALSE, s);
        if (!desk && sf_path(SF_DESKTOP, TRUE, p, 0))
            desk = scan(p, FALSE, s);   // already handled by the machine stage; only counted here
        if (!desk && !o->keep && sf_path(SF_DESKTOP, FALSE, p, SF_LNK_LEN) &&
            save_link(lstrcatW(p, L"\\DynaRun V3.lnk"), g_launcher, g_exe, 0, 0, 0, FALSE))
            RegSetValueExW(s, p, 0, REG_BINARY, (const BYTE *)"", 0);
        if (o->pin && !o->keep) {
            res->pin = PIN_FAILED;
            if (desktop_link(p, s) && pin_taskbar(p, &res->pin) && res->pin == PIN_FAILED) res->pin = PIN_DONE;
        }
        RegCloseKey(s);
    }
    RegCloseKey(b);
    if (!o->keep && !RegCreateKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL)) {   // start at sign-in
        // with the driver step ahead the value is NOT written now: the sign-in after the driver restart would start DynaRun while the
        // driver is being installed. It is only noted (AutostartPending) and registered by autostart_flush when the driver steps are over.
        if (o->autostart && !o->autostart_later) set_str(s, L"DynaRunFix", cat3(p, L"\"", g_launcher, L"\" /autostart"));
        else RegDeleteValueW(s, L"DynaRunFix");
        RegCloseKey(s);
        if (app_key_create(APP_USER, &b)) {
            if (o->autostart && o->autostart_later) set_dword(b, L"AutostartPending", 1);
            else RegDeleteValueW(b, L"AutostartPending");
            RegCloseKey(b);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

// The end of the driver steps (installed, failed, cancelled, skipped, wizard closed): the sign-in start the user asked for is registered now,
// whatever the driver's outcome. Not called while a restart for the driver is pending (wizard.c). Uninstall removes the flag with HKCU\Software\DynaRunFix.
void autostart_flush(void)
{
    static WCHAR p[MAX_PATH]; HKEY k, r; DWORD v = 0, n = 4, t = 0; BOOL pending;
    if (!app_key_open(APP_USER, KEY_ALL_ACCESS, &k)) return;
    pending = RegQueryValueExW(k, L"AutostartPending", NULL, &t, (BYTE *)&v, &n) == ERROR_SUCCESS && t == REG_DWORD && v;
    if (pending) RegDeleteValueW(k, L"AutostartPending");
    RegCloseKey(k);
    if (pending && exists(g_launcher) && !RegCreateKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &r, NULL)) {
        set_str(r, L"DynaRunFix", cat3(p, L"\"", g_launcher, L"\" /autostart"));
        RegCloseKey(r);
    }
}

// The fix is installed and is this version: the uninstall entry names our version and the launcher and the dll are in the install folder. (A dev build
// shows its commit as the version, so a rebuilt one is "not current" until it has been installed once.)
BOOL fix_current(void)
{
    static WCHAR v[64], d[MAX_PATH];
    if (!reg_str(HKEY_LOCAL_MACHINE, UNINSTKEY, L"DisplayVersion", v, 64, 0) || lstrcmpW(v, WIDEN(DRF_DISPLAY))) return FALSE;
    return exists(g_launcher) && exists(cat3(d, g_dir, L"\\dynafix.dll", NULL));
}

// The wizard's one elevated stage may do the driver's preparation too (zip != NULL; see driver_prep_stage): the fix and the driver then share ONE UAC prompt.
// The result: the fix's failure code, or, when the fix is installed (user stage done), RC_OK or the driver's code (DRV_E_*, DRV_W_BDE: >= DRV_E_FIRST). FIX_DONE (errors.h) tells which.

static void add_driver_args(WCHAR *args, const WCHAR *zip, const WCHAR *accept)   // the command line of the elevated stage
{
    if (!zip) return;
    lstrcatW(args, L" /with-driver \""); lstrcatW(args, zip); lstrcatW(args, L"\"");
    if (accept && accept[0]) { lstrcatW(args, L" /unverified "); lstrcatW(args, accept); }
}

// The elevated stage that runs Dyna Pro's MSI gets the SHA-256 the user saw on the check page (/msi-sha256) and compares its staged copy with it.
// A setup without a readable hash is not installed: msi_sha_usable tells the user and says FALSE before anything is started.
static BOOL msi_sha_usable(const WCHAR *sha)
{
    if (hex64_ok(sha)) return TRUE;
    msg(TS(ST_THE_DYNARUN_V3_SETUP_DIFFERS), MB_ICONERROR);
    return FALSE;
}
static void add_msi_sha_arg(WCHAR *args, const WCHAR *sha) { lstrcatW(args, L" /msi-sha256 "); lstrcatW(args, sha); }

int install_fix_driver(const install_opts *o, install_result *res, const WCHAR *zip, const WCHAR *accept)
{
    static WCHAR sid[200], args[2 * MAX_PATH + 400]; int rc;
    if (!user_sid(sid, 200)) return RC_FAIL;
    if (running_early(o, L"install (before elevation)")) return RC_RUNNING;   // before the UAC prompt; /quiet without /close ends here
    CoInitialize(NULL);
    if (is_admin()) { rc = machine_install(o, sid); if (!rc && zip) rc = driver_prep_stage(sid, zip, accept); }
    else {
        wsprintfW(args, L"/machine %s \"%s\"", sid, g_exe);
        add_driver_args(args, zip, accept);
        if ((rc = run_elevated(args, NULL, o)) == RC_CANCEL) { msg(no_admin(), MB_ICONWARNING); return RC_CANCEL; }
    }
    if (!FIX_DONE(rc)) return rc;
    user_stage(o, res);
    return rc;
}

int install_fix(const install_opts *o, install_result *res) { return install_fix_driver(o, res, NULL, NULL); }

int install_all_driver(const install_opts *o, install_result *res, const WCHAR *msi, const WCHAR *msi_sha, const WCHAR *zip, const WCHAR *accept)
{
    static WCHAR sid[200], args[2 * MAX_PATH + 400]; int rc;
    if (!user_sid(sid, 200)) return RC_FAIL;
    if (!msi_sha_usable(msi_sha)) return RC_MSI_FAILED;
    if (running_early(o, L"full install (before elevation)")) return RC_RUNNING;
    CoInitialize(NULL);
    pkg_prepare_documents(msi);         // as this user, before the SYSTEM-side install (error 1305 otherwise)
    if (is_admin()) { rc = machine_full(o, sid, msi, msi_sha); if (!rc && zip) rc = driver_prep_stage(sid, zip, accept); }
    else {
        wsprintfW(args, L"/full %s \"%s\"", sid, msi);
        add_msi_sha_arg(args, msi_sha);
        add_driver_args(args, zip, accept);
        rc = run_elevated(args, NULL, o);
    }
    if (rc == RC_OVER_SHOULDER) {                      // over-the-shoulder elevation: setup as this user, then the fix (and the driver)
        rc = run_msiexec(msi);
        if (rc == ERROR_INSTALL_USEREXIT) return RC_CANCEL;
        if (rc && rc != ERROR_SUCCESS_REBOOT_REQUIRED) return RC_MSI_FAILED;
        if (!locate_dynarun()) return RC_NO_DYNARUN;
        return install_fix_driver(o, res, zip, accept);
    }
    if (rc == RC_CANCEL) return RC_CANCEL;
    if (!FIX_DONE(rc)) return rc;
    locate_dynarun();
    user_stage(o, res);
    return rc;
}

int install_all(const install_opts *o, install_result *res, const WCHAR *msi, const WCHAR *msi_sha) { return install_all_driver(o, res, msi, msi_sha, NULL, NULL); }

// Dyna Pro's setup alone (a DynaRun that is not the tested V3, see dynarun_ver_class): the MSI in the elevated stage, no fix, no shortcuts, no driver. RC_OK, RC_CANCEL, RC_*.
int install_msi_only(const install_opts *o, const WCHAR *msi, const WCHAR *msi_sha)
{
    static WCHAR sid[200], args[2 * MAX_PATH + 160]; int rc;
    if (!user_sid(sid, 200)) return RC_FAIL;
    if (!msi_sha_usable(msi_sha)) return RC_MSI_FAILED;
    if (running_early(o, L"install of Dyna Pro's setup only (before elevation)")) return RC_RUNNING;
    CoInitialize(NULL);
    pkg_prepare_documents(msi);         // as this user, before the SYSTEM-side install (error 1305 otherwise)
    if (is_admin()) rc = machine_msi_only(o, sid, msi, msi_sha);
    else {
        wsprintfW(args, L"/msi-only %s \"%s\"", sid, msi);
        add_msi_sha_arg(args, msi_sha);
        rc = run_elevated(args, NULL, o);
    }
    if (rc == RC_OVER_SHOULDER) {                      // over-the-shoulder elevation: the setup as this user
        rc = run_msiexec(msi);
        if (rc == ERROR_INSTALL_USEREXIT) return RC_CANCEL;
        return rc && rc != ERROR_SUCCESS_REBOOT_REQUIRED ? RC_MSI_FAILED : RC_OK;
    }
    return rc;
}

