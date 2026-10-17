// Part of setup.c (sign-in start, taskbar pin, unelevated start): included by it, in one compilation unit; never compiled on its own.
/* ---------- taskbar pin and sign-in start ---------- */

static void drop_run(HKEY hive, BOOL only_ours)     // the "Run" value; of other accounts only when it starts our launcher
{
    static WCHAR v[MAX_PATH]; HKEY k;
    if (only_ours && !(reg_str(hive, RUNKEY, L"DynaRunFix", v, MAX_PATH, 0) && contains_i(v, g_launcher))) return;
    if (!RegOpenKeyExW(hive, RUNKEY, 0, KEY_SET_VALUE, &k)) { RegDeleteValueW(k, L"DynaRunFix"); RegCloseKey(k); }
}

static WCHAR *layout_xml(WCHAR *out) { return cat3(out, g_dir, L"\\TaskbarLayout.xml", NULL); }

static void drop_policy(HKEY hive)                  // the Windows 11 taskbar layout policy, when it is still ours
{
    static WCHAR v[MAX_PATH], x[MAX_PATH]; HKEY k;
    if (reg_str(hive, POLKEY, L"StartLayoutFile", v, MAX_PATH, 0) && !lstrcmpiW(v, layout_xml(x)) &&
        !RegOpenKeyExW(hive, POLKEY, 0, KEY_SET_VALUE, &k)) {
        RegDeleteValueW(k, L"StartLayoutFile"); RegDeleteValueW(k, L"LockedStartLayout"); RegCloseKey(k);
    }
}

static const WCHAR XML_HEAD[] =
    L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    L"<LayoutModificationTemplate xmlns=\"http://schemas.microsoft.com/Start/2014/LayoutModification\" "
    L"xmlns:defaultlayout=\"http://schemas.microsoft.com/Start/2014/FullDefaultLayout\" "
    L"xmlns:start=\"http://schemas.microsoft.com/Start/2014/StartLayout\" "
    L"xmlns:taskbar=\"http://schemas.microsoft.com/Start/2014/TaskbarLayout\" Version=\"1\">\n"
    L"  <CustomTaskbarLayoutCollection>\n    <defaultlayout:TaskbarLayout>\n      <taskbar:TaskbarPinList>\n"
    L"        <taskbar:DesktopApp DesktopApplicationLinkPath=\"";

// Windows 11 (elevated stage): no program may pin itself any more; Microsoft's taskbar layout policy
// (learn.microsoft.com/windows/configuration/taskbar/pinned-apps) adds the pin at the user's next sign-in.
// The policy is left alone when an organization has already set a layout file.
static void taskbar_policy(const install_opts *o, const WCHAR *sid, HKEY app)
{
    static WCHAR x[MAX_PATH], l[MAX_PATH], xml[2400], path[300], v[MAX_PATH]; static char u[7200];
    DWORD ma, mi, b, ubr = 0, n = 4; HKEY k; BOOL gen;
    os_ver(&ma, &mi, &b);
    if (o->keep) return;
    if (!o->pin) {                                       // reinstalled without the pin: take our policy back
        if (reg_str(app, NULL, L"TaskbarPolicySid", v, MAX_PATH, 0) && !RegOpenKeyExW(HKEY_USERS, v, 0, KEY_READ | KEY_WRITE, &k)) { drop_policy(k); RegCloseKey(k); }
        RegDeleteValueW(app, L"TaskbarPolicy"); RegDeleteValueW(app, L"TaskbarPolicySid");
        return;
    }
    if (ma < 10 || b < 22000) return;
    cat3(path, sid, L"\\", POLKEY);
    layout_xml(x);
    if (reg_str(HKEY_USERS, path, L"StartLayoutFile", v, MAX_PATH, 0) && lstrcmpiW(v, x)) return;
    if (!save_link(cat3(l, g_dir, L"\\DynaRun V3.lnk", NULL), g_launcher, g_exe, 0, 0, 0, FALSE)) return;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE | WOW, &k)) {
        RegQueryValueExW(k, L"UBR", NULL, NULL, (BYTE *)&ubr, &n); RegCloseKey(k);
    }
    // PinGeneration only on builds that know it: elsewhere it makes Windows ignore the pin
    gen = b >= 26200 || (b == 26100 && ubr >= 4484) || (b == 22631 && ubr >= 5549);
    lstrcpyW(xml, XML_HEAD); lstrcatW(xml, l);
    lstrcatW(xml, gen ? L"\" PinGeneration=\"1\"/>\n" : L"\"/>\n");
    lstrcatW(xml, L"      </taskbar:TaskbarPinList>\n    </defaultlayout:TaskbarLayout>\n  </CustomTaskbarLayoutCollection>\n</LayoutModificationTemplate>\n");
    n = WideCharToMultiByte(CP_UTF8, 0, xml, -1, u, sizeof(u), NULL, NULL);
    if (!n || !write_file(x, u, n - 1)) return;
    if (RegCreateKeyExW(HKEY_USERS, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return;
    RegSetValueExW(k, L"StartLayoutFile", 0, REG_EXPAND_SZ, (const BYTE *)x, (lstrlenW(x) + 1) * sizeof(WCHAR));
    set_dword(k, L"LockedStartLayout", 1);
    RegCloseKey(k);
    set_dword(app, L"TaskbarPolicy", 1); set_str(app, L"TaskbarPolicySid", sid);
}

static BOOL pinned_ours(void)       // a shortcut to the launcher among the taskbar's pinned items (Windows 7+)
{
    static WCHAR d[MAX_PATH], f[MAX_PATH];
    if (!sf_path(SF_QUICKLAUNCH, FALSE, d)) return FALSE;
    lstrcatW(d, L"\\User Pinned\\TaskBar");
    return find_ours(d, FALSE, f);
}

static BOOL wait_pinned(void)       // Explorer writes the shortcut a moment after the call returned
{
    int i;
    for (i = 0; i < 30; i++) { if (pinned_ours()) return TRUE; Sleep(100); }
    return FALSE;
}

typedef HRESULT (WINAPI *SHParseDisplayName_t)(PCWSTR, IBindCtx *, PIDLIST_ABSOLUTE *, ULONG, ULONG *);
typedef HRESULT (WINAPI *SHBindToParent_t)(PCIDLIST_ABSOLUTE, REFIID, void **, PCUITEMID_CHILD *);

static PIDLIST_ABSOLUTE item_id(const WCHAR *p)     // not on XP's shell32: looked up
{
    SHParseDisplayName_t f = (SHParseDisplayName_t)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHParseDisplayName"); PIDLIST_ABSOLUTE id = NULL;
    if (!f || FAILED(f(p, NULL, &id, 0, NULL))) return NULL;
    return id;
}

static BOOL pin_verb(const WCHAR *lnk)              // Windows 7 .. Windows 10 1803: the shell's "Pin to taskbar" verb
{
    SHBindToParent_t bind = (SHBindToParent_t)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHBindToParent");
    PIDLIST_ABSOLUTE id = item_id(lnk); IShellFolder *sf; PCUITEMID_CHILD ch; IContextMenu *cm; HMENU m; CMINVOKECOMMANDINFO ci; BOOL r = FALSE;
    if (id && bind && SUCCEEDED(bind(id, &IID_IShellFolder, (void **)&sf, &ch))) {
        if (SUCCEEDED(IShellFolder_GetUIObjectOf(sf, NULL, 1, &ch, &IID_IContextMenu, NULL, (void **)&cm))) {
            m = CreatePopupMenu();
            if (SUCCEEDED(IContextMenu_QueryContextMenu(cm, m, 0, 1, 0x7fff, CMF_NORMAL))) {
                zero(&ci, sizeof(ci)); ci.cbSize = sizeof(ci); ci.lpVerb = "taskbarpin"; ci.nShow = SW_HIDE;
                r = SUCCEEDED(IContextMenu_InvokeCommand(cm, &ci));
            }
            DestroyMenu(m); IContextMenu_Release(cm);
        }
        IShellFolder_Release(sf);
    }
    CoTaskMemFree(id);
    return r;
}

static BOOL pin_list(const WCHAR *lnk)              // Windows 10 1809 and later: Explorer's IPinnedList3
{
    IPinnedList3 *pl; PIDLIST_ABSOLUTE id = item_id(lnk); BOOL r = FALSE;
    if (id && SUCCEEDED(CoCreateInstance(&CLSID_TaskbandPin, NULL, CLSCTX_INPROC_SERVER, &IID_IPinnedList3, (void **)&pl))) {
        r = SUCCEEDED(pl->lpVtbl->Modify(pl, NULL, id, 0x7fffffff));
        pl->lpVtbl->Release(pl);
    }
    CoTaskMemFree(id);
    return r;
}

// Pins DynaRun (the shortcut lnk, which starts the launcher) to the taskbar of the current user. TRUE: done, or
// on Windows 11 under way (*res = PIN_NEXT_SIGNIN: the policy set by the machine stage applies at the next sign-in).
static BOOL pin_taskbar(const WCHAR *lnk, int *res)
{
    static WCHAR d[MAX_PATH], v[MAX_PATH], sub[64]; DWORD ma, mi, b; HKEY k;
    os_ver(&ma, &mi, &b);
    if (ma == 5 || (ma == 6 && !mi)) {                  // XP, Vista: the Quick Launch toolbar
        if (!sf_path(SF_QUICKLAUNCH, FALSE, d)) return FALSE;
        CreateDirectoryW(d, NULL);
        if (!save_link(lstrcatW(d, L"\\DynaRun V3.lnk"), g_launcher, g_exe, 0, 0, 0, FALSE)) return FALSE;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, cat3(sub, USERKEY, L"\\Shortcuts", NULL), 0, KEY_SET_VALUE, &k)) {
            RegSetValueExW(k, d, 0, REG_BINARY, (const BYTE *)"", 0);       // a shortcut we created: uninstall deletes it
            RegCloseKey(k);
        }
        return TRUE;
    }
    if (ma >= 10 && b >= 22000) {                       // Windows 11: see taskbar_policy
        if (reg_str(HKEY_CURRENT_USER, POLKEY, L"StartLayoutFile", v, MAX_PATH, 0) && !lstrcmpiW(v, layout_xml(d))) *res = PIN_NEXT_SIGNIN;
        return *res == PIN_NEXT_SIGNIN;
    }
    if (pinned_ours()) return TRUE;
    if (ma >= 10 && b >= 17763) pin_list(lnk); else pin_verb(lnk);
    return wait_pinned();
}

static BOOL desktop_link(WCHAR *out, HKEY backup)   // one of our shortcuts to pin: desktop, Start menu, else a new one
{
    WCHAR d[MAX_PATH];
    if (find_our_shortcut(out)) return TRUE;
    if (!sf_path(SF_OWN, FALSE, d)) return FALSE;
    CreateDirectoryW(d, NULL);
    if (!save_link(cat3(out, d, L"\\DynaRun V3.lnk", NULL), g_launcher, g_exe, 0, 0, 0, FALSE)) return FALSE;
    RegSetValueExW(backup, out, 0, REG_BINARY, (const BYTE *)"", 0);
    return TRUE;
}

// Opens file the way a double click does, but as the signed-in user's normal (non-elevated) process: the desktop's shell (Explorer, which runs
// with the normal token) is asked to do it - IShellWindows -> the desktop's shell view -> its Application (IShellDispatch2) -> ShellExecute.
// FALSE when any step fails (no Explorer, no desktop: e.g. a session without one).
static BOOL shell_execute_unelevated(const WCHAR *file, const WCHAR *dir)
{
    IShellWindows *sw = NULL; IDispatch *disp = NULL, *dv = NULL, *app = NULL; IServiceProvider *sp = NULL; IShellBrowser *sb = NULL; IShellView *view = NULL;
    IShellFolderViewDual *fvd = NULL; IShellDispatch2 *sd = NULL; HRESULT hrc; VARIANT loc, root, a, d, o, s; LONG hwnd = 0; BSTR bf = NULL, bd = NULL; BOOL ok = FALSE;
    hrc = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);                       // RPC_E_CHANGED_MODE: already initialised another way, usable
    if (FAILED(hrc) && hrc != RPC_E_CHANGED_MODE) return FALSE;
    VariantInit(&loc); VariantInit(&root); VariantInit(&a); VariantInit(&d); VariantInit(&o); VariantInit(&s);
    loc.vt = VT_I4; loc.lVal = CSIDL_DESKTOP;
    if (FAILED(CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, &IID_IShellWindows, (void **)&sw))) goto done;
    if (FAILED(IShellWindows_FindWindowSW(sw, &loc, &root, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp)) || !disp) goto done;
    if (FAILED(IDispatch_QueryInterface(disp, &IID_IServiceProvider, (void **)&sp))) goto done;
    if (FAILED(IServiceProvider_QueryService(sp, &SID_STopLevelBrowser, &IID_IShellBrowser, (void **)&sb))) goto done;
    if (FAILED(IShellBrowser_QueryActiveShellView(sb, &view)) || !view) goto done;
    if (FAILED(IShellView_GetItemObject(view, SVGIO_BACKGROUND, &IID_IDispatch, (void **)&dv)) || !dv) goto done;
    if (FAILED(IDispatch_QueryInterface(dv, &IID_IShellFolderViewDual, (void **)&fvd))) goto done;
    if (FAILED(IShellFolderViewDual_get_Application(fvd, &app)) || !app) goto done;
    if (FAILED(IDispatch_QueryInterface(app, &IID_IShellDispatch2, (void **)&sd))) goto done;
    if (!(bf = SysAllocString(file))) goto done;
    if (dir && dir[0] && (bd = SysAllocString(dir))) { d.vt = VT_BSTR; d.bstrVal = bd; }
    s.vt = VT_I4; s.lVal = SW_SHOWNORMAL;
    ok = SUCCEEDED(IShellDispatch2_ShellExecute(sd, bf, a, d, o, s));
done:
    SysFreeString(bf); SysFreeString(bd);
    if (sd) IShellDispatch2_Release(sd);
    if (app) IDispatch_Release(app);
    if (fvd) IShellFolderViewDual_Release(fvd);
    if (dv) IDispatch_Release(dv);
    if (view) IShellView_Release(view);
    if (sb) IShellBrowser_Release(sb);
    if (sp) IServiceProvider_Release(sp);
    if (disp) IDispatch_Release(disp);
    if (sw) IShellWindows_Release(sw);
    if (SUCCEEDED(hrc)) CoUninitialize();
    return ok;
}

// DynaRun is never started with more rights than a normal start from the desktop: an elevated wizard (setup run as administrator; the sign-in task
// of the driver installation, which runs with the user's full token) asks the shell to start it as the normal user. If that is impossible it is not
// started elevated: the user is told to use the desktop icon. Which way was used goes to the log.
static void launch_normal(const WCHAR *file, const WCHAR *dir)
{
    if (!is_admin()) {                                                         // already a normal process: a plain ShellExecute is the normal start
        ShellExecuteW(g_hwnd, NULL, file, NULL, dir, SW_SHOWNORMAL);
        run_log(L"start DynaRun: ShellExecute (this process is not elevated)");
        return;
    }
    if (shell_execute_unelevated(file, dir)) { run_log(L"start DynaRun: through the desktop shell, as the normal user (this process is elevated)"); return; }
    run_log(L"start DynaRun: not started, this process is elevated and the desktop shell could not be asked");
    msg(T(L"DynaRun was not started from here. Please start it with the \"DynaRun V3\" icon on the desktop.", L"這裡沒有啟動 DynaRun，請改用桌面上的「DynaRun V3」圖示開啟。"), MB_ICONINFORMATION);
}

// The wizard's "Start DynaRun": started the way a double click on the shortcut does it (the shortcut gives the
// process the AppUserModelID that groups it with the pinned icon, its working folder and show command), not by
// running the launcher directly. Falls back to the launcher with DynaRun's folder as working directory.
void start_dynarun(void)
{
    static WCHAR lnk[MAX_PATH], dir[MAX_PATH];
    WCHAR *q; BOOL have = find_our_shortcut(lnk);
    if (!have && exists(cat3(lnk, g_dir, L"\\DynaRun V3.lnk", NULL))) have = TRUE;
    if (have) { launch_normal(lnk, NULL); return; }
    lstrcpyW(dir, g_exe); q = dir + lstrlenW(dir); while (q > dir && *q != '\\') q--; *q = 0;
    launch_normal(g_launcher, dir[0] ? dir : NULL);
}

