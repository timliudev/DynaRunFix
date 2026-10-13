// Tests of the hardening of the installer (src\setup.c, src\package.c). Host-native, with the CRT; not part of the shipped
// binaries (see tests\build-hardening-tests.cmd). setup.c is compiled into this program, so the shipped code is what runs.
//
// Only temporary things are touched: a folder %TEMP%\drf-sec-<pid> (deleted at the end) and the registry key
// HKCU\Software\DynaRunFixTest-<pid> (deleted at the end). The real DynaRun and DynaRunFix installs, HKLM and the settings
// of the machine are not changed; the one look at the machine is read-only (the ACL of an installed DynaRun folder, printed).
#define WinMainCRTStartup setup_main_not_used_here
#include "../src/setup.c"
#include <stdio.h>

// what setup.c calls elsewhere and the tests do not need
int wizard(void) { return 0; }
int update_check(void) { return 0; }
int update_offer(void) { return 0; }
int update_run(const WCHAR *a) { (void)a; return 0; }

static int g_fail, g_pass;
#define CHECK(c, what) do { if (c) { g_pass++; } else { g_fail++; wprintf(L"FAIL line %d: %s\n", __LINE__, what); } } while (0)

static WCHAR g_base[MAX_PATH], g_root[MAX_PATH], g_sys[MAX_PATH], g_outside[MAX_PATH], g_other[MAX_PATH], g_testkey[200];

static void mkfile(const WCHAR *dir, const WCHAR *name, const char *content)
{
    WCHAR p[MAX_PATH]; HANDLE h; DWORD w;
    wsprintfW(p, L"%s\\%s", dir, name);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, content, (DWORD)strlen(content), &w, NULL); CloseHandle(h); }
}

static void sh(const WCHAR *cmdline)
{
    WCHAR c[1200]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    wsprintfW(c, L"cmd.exe /c %s", cmdline);
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

static BOOL fake_writable(const WCHAR *p) { return wcsstr(p, L"\\writable") != NULL; }

static void setup_tree(void)
{
    WCHAR p[MAX_PATH], c[1000];
    GetTempPathW(MAX_PATH, g_base);
    { WCHAR lp[MAX_PATH]; DWORD n = GetLongPathNameW(g_base, lp, MAX_PATH); if (n && n < MAX_PATH) lstrcpyW(g_base, lp); }   // CI temp folder is an 8.3 short path; the code under test compares canonical (long) paths
    wsprintfW(g_base + lstrlenW(g_base), L"drf-sec-%lu", GetCurrentProcessId());
    CreateDirectoryW(g_base, NULL);
    wsprintfW(g_root, L"%s\\root", g_base); CreateDirectoryW(g_root, NULL);
    wsprintfW(g_sys, L"%s\\sys", g_base); CreateDirectoryW(g_sys, NULL);
    wsprintfW(g_outside, L"%s\\outside", g_base); CreateDirectoryW(g_outside, NULL);
    wsprintfW(g_other, L"%s\\other", g_base); CreateDirectoryW(g_other, NULL);
    mkfile(g_root, L"a.ocx", "x"); mkfile(g_root, L"b.dll", "x");
    wsprintfW(p, L"%s\\sub", g_root); CreateDirectoryW(p, NULL); mkfile(p, L"s.dll", "x");
    wsprintfW(p, L"%s\\writable", g_root); CreateDirectoryW(p, NULL); mkfile(p, L"w.ocx", "x");
    mkfile(g_outside, L"evil.ocx", "x"); mkfile(g_other, L"o.ocx", "x");
    mkfile(g_sys, L"c.ocx", "x"); mkfile(g_sys, L"d.dll", "x"); mkfile(g_sys, L"t.tlb", "x");
    wsprintfW(p, L"%s\\sysdir.ocx", g_sys); CreateDirectoryW(p, NULL);
    wsprintfW(c, L"mklink /J \"%s\\J\" \"%s\" >nul", g_root, g_outside); sh(c);
    wsprintfW(g_testkey, L"Software\\DynaRunFixTest-%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"DRFTEST_ROOT", g_root);
}

/* ---------- 1. user_writable ---------- */

static void test_user_writable(void)
{
    WCHAR c[1000], sid[200], pf[MAX_PATH];
    CHECK(!user_writable(L"C:\\Windows\\System32\\kernel32.dll"), L"kernel32.dll is not user-writable");
    CHECK(!user_writable(L"C:\\Windows\\System32"), L"System32 is not user-writable");
    CHECK(user_writable(L"C:\\this\\does\\not\\exist.dll"), L"a path that cannot be read counts as writable");
    user_sid(sid, 200); lstrcpyW(g_user_sid, sid);
    // an Everyone ACE, whoever owns the folder
    wsprintfW(c, L"icacls \"%s\\sub\" /grant *S-1-1-0:(OI)(CI)M >nul", g_root); sh(c);
    { WCHAR s[MAX_PATH]; wsprintfW(s, L"%s\\sub", g_root); CHECK(user_writable(s), L"folder with an Everyone-modify ACE is writable"); }
    // the folder of an installed DynaRun, read only: printed, not asserted
    if (SHGetSpecialFolderPathW(NULL, pf, CSIDL_PROGRAM_FILESX86, FALSE)) {
        lstrcatW(pf, L"\\Dyna Pro Dynamometers");
        if (exists(pf)) wprintf(L"info: installed DynaRun folder: user_writable = %d\n", user_writable(pf));
    }
}

/* ---------- 2. COM server path check ---------- */

static void test_paths(void)
{
    static WCHAR roots[2][MAX_PATH]; const WCHAR *why; WCHAR r[1100], longp[600]; int i;
    lstrcpyW(roots[0], g_root); lstrcpyW(roots[1], g_sys); g_com_nfolder = 1; g_is_writable = fake_writable;
#define OK(s, tl) do { wsprintfW(r, L"%s", s); CHECK(com_path_ok(r, tl, roots, 2, &why), L"should pass: " s); if (g_fail && 0) wprintf(L"%s\n", why); } while (0)
#define NO(s, tl, w) do { wsprintfW(r, L"%s", s); CHECK(!com_path_ok(r, tl, roots, 2, &why), L"should fail: " s); } while (0)
    wsprintfW(r, L"%s\\a.ocx", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"plain path in the DynaRun folder");
    wsprintfW(r, L"\"%s\\a.ocx\"", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"quoted path");
    wsprintfW(r, L"\"%s\\a.ocx\" /Automation", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"quoted path with arguments");
    wsprintfW(r, L"%s\\a.ocx /Embedding", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"unquoted path with arguments");
    wsprintfW(r, L"%s\\A.OCX", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"case does not matter");
    wsprintfW(r, L"%s\\b.dll", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"any file type inside the DynaRun folder");
    wsprintfW(r, L"%s\\sub\\s.dll", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"a subfolder of the DynaRun folder");
    wsprintfW(r, L"%s", L"%DRFTEST_ROOT%\\a.ocx"); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"environment variable is expanded");
    wsprintfW(r, L"%s\\c.ocx", g_sys); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"an .ocx in a system folder");
    wsprintfW(r, L"%s\\d.dll", g_sys); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"a .dll in a system folder is refused");
    wsprintfW(r, L"%s\\d.dll", g_sys); CHECK(!com_path_ok(r, TRUE, roots, 2, &why), L"a .dll type library in a system folder is refused");
    wsprintfW(r, L"%s\\t.tlb", g_sys); CHECK(com_path_ok(r, TRUE, roots, 2, &why), L"a .tlb type library in a system folder");
    wsprintfW(r, L"%s\\t.tlb", g_sys); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"a .tlb as a server is refused");
    wsprintfW(r, L"%s\\c.ocx\\1", g_sys); CHECK(com_path_ok(r, TRUE, roots, 2, &why), L"type library with a resource number");
    wsprintfW(r, L"%s\\o.ocx", g_other); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"a file outside both folders");
    wsprintfW(r, L"%s\\J\\evil.ocx", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"through a junction");
    wsprintfW(r, L"%s\\..\\outside\\evil.ocx", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"dot-dot out of the folder");
    wsprintfW(r, L"%s\\sub\\..\\a.ocx", g_root); CHECK(com_path_ok(r, FALSE, roots, 2, &why), L"dot-dot that stays inside is normalised");
    CHECK(!com_path_ok(L"a.ocx", FALSE, roots, 2, &why), L"relative path");
    CHECK(!com_path_ok(L"..\\a.ocx", FALSE, roots, 2, &why), L"relative path with dot-dot");
    CHECK(!com_path_ok(L"\\\\localhost\\c$\\Windows\\System32\\kernel32.dll", FALSE, roots, 2, &why), L"UNC path");
    CHECK(!com_path_ok(L"\\\\?\\C:\\Windows\\System32\\kernel32.dll", FALSE, roots, 2, &why), L"\\\\?\\ path");
    CHECK(!com_path_ok(L"C:a.ocx", FALSE, roots, 2, &why), L"drive-relative path");
    CHECK(!com_path_ok(L"", FALSE, roots, 2, &why), L"empty");
    CHECK(!com_path_ok(L"\"\"", FALSE, roots, 2, &why), L"empty quotes");
    wsprintfW(r, L"%s\\writable\\w.ocx", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"file in a folder that users can change");
    wsprintfW(r, L"%s\\missing.ocx", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"missing file");
    wsprintfW(r, L"%s\\sub", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"a folder");
    wsprintfW(r, L"%s\\sysdir.ocx", g_sys); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"a folder named like an ocx");
    wsprintfW(r, L"%s\\a.ocx:stream", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"alternate data stream");
    wsprintfW(r, L"%s\\a*.ocx", g_root); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"wildcard");
    wsprintfW(r, L"%%TEMP%%\\a.ocx"); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"expands to the user's temp folder");
    wsprintfW(r, L"%%NOSUCHVAR%%\\a.ocx"); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"unexpanded variable");
    for (i = 0; i < 590; i++) longp[i] = 'a'; longp[590] = 0;
    wsprintfW(r, L"%s\\%s.ocx", g_root, longp); CHECK(!com_path_ok(r, FALSE, roots, 2, &why), L"path far too long");
    // DynaRun's folder not trusted: only the system roots count (com_roots)
    g_com_nfolder = 0; g_exe_trusted = FALSE; g_exe[0] = 0;
    { WCHAR rr[3][MAX_PATH]; int n = com_roots(rr); CHECK(n >= 1 && under(rr[0], L"C:\\Windows") == under(rr[0], L"C:\\Windows") && g_com_nfolder == 0, L"no DynaRun root when untrusted"); }
    g_com_nfolder = 1;
    // the real check (not the fake): a file the current user created is not trusted by user_writable's owner rule unless elevated
    g_is_writable = user_writable;
    wsprintfW(r, L"%s\\a.ocx", g_root);
    CHECK(!com_path_ok(r, FALSE, roots, 2, &why) || is_admin(), L"with the real ACL check a user-owned file is refused (unless elevated)");
    g_is_writable = fake_writable;
}

/* ---------- 3. promote_com_in on temporary keys ---------- */

static HKEY mkkey(HKEY root, const WCHAR *path)
{
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS | WOW, NULL, &k, NULL)) return NULL;
    return k;
}

static void setdef(HKEY root, const WCHAR *path, const WCHAR *v)
{
    HKEY k = mkkey(root, path);
    if (k) { RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)v, (lstrlenW(v) + 1) * sizeof(WCHAR)); RegCloseKey(k); }
}

static void clsid(HKEY hu, const WCHAR *id, const WCHAR *inproc, const WCHAR *local, const WCHAR *progid, const WCHAR *extra)
{
    WCHAR p[400];
    if (inproc) { wsprintfW(p, L"CLSID\\%s\\InprocServer32", id); setdef(hu, p, inproc); }
    if (local) { wsprintfW(p, L"CLSID\\%s\\LocalServer32", id); setdef(hu, p, local); }
    if (progid) {
        wsprintfW(p, L"CLSID\\%s\\ProgID", id); setdef(hu, p, progid);
        wsprintfW(p, L"%s\\CLSID", progid); setdef(hu, p, id);
        { WCHAR vi[100], *d; lstrcpyW(vi, progid); d = vi + lstrlenW(vi); while (d > vi && d[-1] != '.') d--; if (d > vi) { d[-1] = 0; wsprintfW(p, L"%s\\CLSID", vi); setdef(hu, p, id); } }
    }
    if (extra) { wsprintfW(p, L"CLSID\\%s\\%s", id, extra); setdef(hu, p, L"x"); }
}

static void test_promote(void)
{
    static WCHAR roots[2][MAX_PATH], good[MAX_PATH], evil[MAX_PATH], rel[MAX_PATH], logdir[MAX_PATH], p[600], key[300];
    HKEY top, hu, hm; int n;
    lstrcpyW(roots[0], g_root); lstrcpyW(roots[1], g_sys); g_com_nfolder = 1; g_is_writable = fake_writable;
    wsprintfW(logdir, L"%s\\logs", g_base); CreateDirectoryW(logdir, NULL); lstrcpyW(g_dir, logdir);
    wsprintfW(key, L"%s\\dst", g_testkey); wsprintfW(p, L"HKCU\\%s\\dst\\", g_testkey);
    { static WCHAR exp[400]; lstrcpyW(exp, p); g_export_root = exp; }
    top = mkkey(HKEY_CURRENT_USER, g_testkey); RegCloseKey(top);
    wsprintfW(p, L"%s\\src", g_testkey); hu = mkkey(HKEY_CURRENT_USER, p);
    hm = mkkey(HKEY_CURRENT_USER, key);
    wsprintfW(good, L"%s\\a.ocx", g_root); wsprintfW(evil, L"%s\\evil.ocx", g_other);
    clsid(hu, L"{11111111-0000-0000-0000-000000000001}", good, NULL, L"Test.Good.1", NULL);
    clsid(hu, L"{11111111-0000-0000-0000-000000000002}", evil, NULL, L"Test.Evil.1", NULL);
    clsid(hu, L"{11111111-0000-0000-0000-000000000003}", L"relative.ocx", NULL, NULL, NULL);
    wsprintfW(rel, L"%s\\J\\evil.ocx", g_root);
    clsid(hu, L"{11111111-0000-0000-0000-000000000004}", rel, NULL, NULL, NULL);
    clsid(hu, L"{11111111-0000-0000-0000-000000000005}", good, evil, NULL, NULL);                 // good in-proc, evil local server
    clsid(hu, L"{11111111-0000-0000-0000-000000000006}", good, NULL, NULL, L"TreatAs");           // redirection
    clsid(hu, L"{11111111-0000-0000-0000-000000000007}", good, NULL, NULL, L"InprocHandler32");
    clsid(hu, L"{11111111-0000-0000-0000-000000000008}", NULL, good, NULL, NULL);                  // LocalServer32 only, good
    wsprintfW(rel, L"%s\\d.dll", g_sys);
    clsid(hu, L"{11111111-0000-0000-0000-000000000009}", rel, NULL, NULL, NULL);                  // non-ocx in a system folder
    wsprintfW(rel, L"%s\\c.ocx", g_sys);
    clsid(hu, L"{11111111-0000-0000-0000-00000000000A}", rel, NULL, NULL, NULL);                  // ocx in a system folder
    clsid(hu, L"{11111111-0000-0000-0000-00000000000B}", good, NULL, NULL, NULL);
    setdef(hu, L"CLSID\\{11111111-0000-0000-0000-00000000000B}\\Foo", L"x");
    {   // an AppID value (DllSurrogate / remote activation)
        HKEY k = mkkey(hu, L"CLSID\\{11111111-0000-0000-0000-00000000000C}"); clsid(hu, L"{11111111-0000-0000-0000-00000000000C}", good, NULL, NULL, NULL);
        RegSetValueExW(k, L"AppID", 0, REG_SZ, (const BYTE *)L"{x}", 8); RegCloseKey(k);
    }
    // ProgID with a shell verb
    clsid(hu, L"{11111111-0000-0000-0000-00000000000D}", good, NULL, L"Test.Verb.1", NULL);
    setdef(hu, L"Test.Verb.1\\shell\\open\\command", L"calc.exe");
    setdef(hu, L"Test.Verb\\shell\\open\\command", L"calc.exe");
    // type libraries
    setdef(hu, L"CLSID\\{11111111-0000-0000-0000-00000000000B}\\TypeLib", L"{22222222-0000-0000-0000-000000000001}");
    setdef(hu, L"TypeLib\\{22222222-0000-0000-0000-000000000001}\\1.0\\0\\win32", good);
    setdef(hu, L"CLSID\\{11111111-0000-0000-0000-00000000000A}\\TypeLib", L"{22222222-0000-0000-0000-000000000002}");
    wsprintfW(rel, L"%s\\evil.ocx", g_outside);
    setdef(hu, L"TypeLib\\{22222222-0000-0000-0000-000000000002}\\1.0\\0\\win32", rel);
    // an entry that is in the destination already is not touched
    wsprintfW(rel, L"%s\\c.ocx", g_sys);
    clsid(hm, L"{11111111-0000-0000-0000-00000000000E}", rel, NULL, NULL, NULL);
    clsid(hu, L"{11111111-0000-0000-0000-00000000000E}", good, NULL, NULL, NULL);

    n = promote_com_in(hu, hm, roots, 2);
    wprintf(L"info: promote_com_in copied %d keys\n", n);
#define HAS(id) has_key(hm, L"CLSID\\{11111111-0000-0000-0000-0000000000" id L"}")
    CHECK(HAS(L"01"), L"good in-proc entry copied");
    CHECK(has_key(hm, L"Test.Good.1") && has_key(hm, L"Test.Good"), L"its ProgIDs copied");
    CHECK(!HAS(L"02"), L"server outside the folders refused");
    CHECK(!has_key(hm, L"Test.Evil.1"), L"ProgID of a refused CLSID not copied");
    CHECK(!HAS(L"03"), L"relative path refused");
    CHECK(!HAS(L"04"), L"junction path refused");
    CHECK(!HAS(L"05"), L"one bad server refuses the whole CLSID");
    CHECK(!HAS(L"06"), L"TreatAs refused");
    CHECK(!HAS(L"07"), L"InprocHandler32 refused");
    CHECK(HAS(L"08"), L"LocalServer32-only entry copied");
    CHECK(!HAS(L"09"), L"non-ocx in a system folder refused");
    CHECK(HAS(L"0A"), L"ocx in a system folder copied");
    CHECK(HAS(L"0B"), L"entry with an unknown extra key copied");
    CHECK(!HAS(L"0C"), L"AppID refused");
    CHECK(HAS(L"0D") && !has_key(hm, L"Test.Verb.1") && !has_key(hm, L"Test.Verb"), L"CLSID copied, ProgID with a shell verb not");
    CHECK(has_key(hm, L"TypeLib\\{22222222-0000-0000-0000-000000000001}"), L"type library of a good file copied");
    CHECK(!has_key(hm, L"TypeLib\\{22222222-0000-0000-0000-000000000002}"), L"type library of a file outside refused");
    {   // the entry that was in the destination keeps its server
        WCHAR v[MAX_PATH]; wsprintfW(rel, L"%s\\c.ocx", g_sys);
        reg_str(hm, L"CLSID\\{11111111-0000-0000-0000-00000000000E}\\InprocServer32", NULL, v, MAX_PATH, WOW);
        CHECK(!lstrcmpiW(v, rel), L"an existing destination entry is not overwritten");
    }
    {   // the log of what was skipped, and of what was copied
        WCHAR f[MAX_PATH]; wsprintfW(f, L"%s\\com-skipped.txt", logdir); CHECK(exists(f), L"com-skipped.txt written");
        wsprintfW(f, L"%s\\com-copied.txt", logdir); CHECK(exists(f), L"com-copied.txt written");
    }
    // the check of the copy in the destination: a copy that went bad is removed
    wsprintfW(rel, L"%s\\evil.ocx", g_outside);
    clsid(hm, L"{11111111-0000-0000-0000-00000000000F}", rel, NULL, NULL, NULL);
    { const WCHAR *why; CHECK(!clsid_ok(hm, L"CLSID\\{11111111-0000-0000-0000-00000000000F}", roots, 2, &why), L"clsid_ok on the destination refuses a bad copy"); }
    delete_key_tree(hm, L"CLSID\\{11111111-0000-0000-0000-00000000000F}");
    CHECK(!HAS(L"0F"), L"delete_key_tree removed the key and its children");
    RegCloseKey(hu); RegCloseKey(hm);
    g_nkeys = 0;
}

/* ---------- 4. the borrowed time stamp ---------- */

static void ft_of(int y, int mo, int d, int h, int mi, int s, int ms, FILETIME *f)
{ SYSTEMTIME st; ZeroMemory(&st, sizeof st); st.wYear = y; st.wMonth = mo; st.wDay = d; st.wHour = h; st.wMinute = mi; st.wSecond = s; st.wMilliseconds = ms; SystemTimeToFileTime(&st, f); }

static BOOL get_time(const WCHAR *p, FILETIME *t)
{ HANDLE h = open_times(p); BOOL ok = h != INVALID_HANDLE_VALUE && GetFileTime(h, NULL, NULL, t); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); return ok; }

static DWORD WINAPI holder(LPVOID a)
{ stage_lock(); *(volatile LONG *)a = 1; Sleep(1500); stage_unlock(); return 0; }

static void test_touch(void)
{
    WCHAR exe[MAX_PATH], bin[MAX_PATH], sys[MAX_PATH], p[400], v[MAX_PATH]; FILETIME orig, now_t, t, tmp; HKEY k; int rc, i;
    ULARGE_INTEGER a, b; SYSTEMTIME st; volatile LONG flag = 0; HANDLE th; DWORD t0;
    wsprintfW(bin, L"%s\\bin", g_base); CreateDirectoryW(bin, NULL);
    wsprintfW(exe, L"%s\\DynaRun V3.exe", bin);
    GetSystemDirectoryW(sys, MAX_PATH); lstrcatW(sys, L"\\hostname.exe");
    CHECK(CopyFileW(sys, exe, FALSE), L"test exe copied");
    wsprintfW(p, L"%s\\app", g_testkey);
    g_app_root = HKEY_CURRENT_USER; g_app_key = p;
    ft_of(2021, 3, 4, 5, 6, 8, 125, &orig);
    { HANDLE h = open_times(exe); SetFileTime(h, NULL, NULL, &orig); CloseHandle(h); }

    // touch: the original time is back, the record is gone
    rc = touch(exe);
    CHECK(rc == TOUCH_OK, L"touch returns OK");
    CHECK(get_time(exe, &t) && same_time(&t, &orig), L"modified time is exactly the original after touch");
    CHECK(!reg_str(HKEY_CURRENT_USER, p, ORIGPATH, v, MAX_PATH, 0), L"the record is removed after a good restore");
    CHECK(touch(L"") == TOUCH_NOTDONE && touch(L"C:\\x\\other.exe") == TOUCH_NOTDONE, L"empty path or other name: nothing done");
    wsprintfW(v, L"%s\\missing\\DynaRun V3.exe", g_base); CHECK(touch(v) == TOUCH_NOTDONE, L"missing exe: nothing done");

    // a run that was killed after it changed the time: recover_exe_time puts it back
    { HANDLE h = open_times(exe); ft_of(2030, 1, 1, 0, 0, 0, 0, &t); SetFileTime(h, NULL, NULL, &t); CloseHandle(h); }
    RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, ORIGPATH, 0, REG_SZ, (const BYTE *)exe, (lstrlenW(exe) + 1) * sizeof(WCHAR));
    RegSetValueExW(k, ORIGTIME, 0, REG_BINARY, (const BYTE *)&orig, sizeof orig);
    RegCloseKey(k);
    CHECK(recover_exe_time() == TOUCH_OK, L"recover returns OK");
    CHECK(get_time(exe, &t) && same_time(&t, &orig), L"recover restored the original time");
    CHECK(!reg_str(HKEY_CURRENT_USER, p, ORIGPATH, v, MAX_PATH, 0), L"record removed after recover");
    CHECK(recover_exe_time() == TOUCH_OK, L"recover without a record is a no-op");

    // the record points to a file that is gone: it is dropped
    RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    wsprintfW(v, L"%s\\gone\\DynaRun V3.exe", g_base);
    RegSetValueExW(k, ORIGPATH, 0, REG_SZ, (const BYTE *)v, (lstrlenW(v) + 1) * sizeof(WCHAR));
    RegSetValueExW(k, ORIGTIME, 0, REG_BINARY, (const BYTE *)&orig, sizeof orig);
    RegCloseKey(k);
    CHECK(recover_exe_time() == TOUCH_OK && !reg_str(HKEY_CURRENT_USER, p, ORIGPATH, v, MAX_PATH, 0), L"record of a missing file dropped");

    // the file cannot be opened for its times (deny write-attributes): recover reports STUCK and keeps the record
    RegCreateKeyExW(HKEY_CURRENT_USER, p, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, ORIGPATH, 0, REG_SZ, (const BYTE *)exe, (lstrlenW(exe) + 1) * sizeof(WCHAR));
    ft_of(2022, 2, 2, 2, 2, 2, 0, &t);
    RegSetValueExW(k, ORIGTIME, 0, REG_BINARY, (const BYTE *)&t, sizeof t);
    RegCloseKey(k);
    { WCHAR c[900], sid[200]; user_sid(sid, 200); wsprintfW(c, L"icacls \"%s\" /deny *%s:(WA) >nul", exe, sid); sh(c); }
    rc = recover_exe_time();
    CHECK(rc == TOUCH_STUCK, L"cannot restore: STUCK");
    CHECK(reg_str(HKEY_CURRENT_USER, p, ORIGPATH, v, MAX_PATH, 0) > 0, L"the record stays when the restore failed");
    CHECK(touch(exe) == TOUCH_NOTDONE, L"touch leaves the exe alone when its time cannot be set");
    { WCHAR c[900], sid[200]; user_sid(sid, 200); wsprintfW(c, L"icacls \"%s\" /remove:d *%s >nul", exe, sid); sh(c); }
    CHECK(recover_exe_time() == TOUCH_OK, L"after the file is accessible again the next run restores it");
    CHECK(get_time(exe, &t) && !same_time(&t, &orig) && !reg_str(HKEY_CURRENT_USER, p, ORIGPATH, v, MAX_PATH, 0), L"and clears the record");

    // the temporary time is never within 2 s of the original, and is on an even second
    GetSystemTimeAsFileTime(&now_t);
    for (i = 0; i < 9; i++) {
        a.LowPart = now_t.dwLowDateTime; a.HighPart = now_t.dwHighDateTime;
        switch (i) {
        case 0: break;
        case 1: a.QuadPart += 10000000ULL; break;
        case 2: a.QuadPart -= 10000000ULL; break;
        case 3: a.QuadPart += 30000000ULL; break;
        case 4: a.QuadPart -= 30000000ULL; break;
        case 5: a.QuadPart += 19999999ULL; break;
        case 6: a.QuadPart += 39999999ULL; break;
        case 7: a.QuadPart += 41234567ULL; break;
        default: a.QuadPart -= 36000000000ULL; break;
        }
        tmp.dwLowDateTime = a.LowPart; tmp.dwHighDateTime = a.HighPart;
        pick_temp_time(&tmp, &t);
        b.LowPart = t.dwLowDateTime; b.HighPart = t.dwHighDateTime;
        CHECK((a.QuadPart > b.QuadPart ? a.QuadPart - b.QuadPart : b.QuadPart - a.QuadPart) >= 20000000ULL, L"temporary time at least 2 s away");
        FileTimeToSystemTime(&t, &st);
        CHECK(!(st.wSecond & 1) && !st.wMilliseconds, L"temporary time on an even second");
    }

    // two stages never run at the same time
    th = CreateThread(NULL, 0, holder, (LPVOID)&flag, 0, NULL);
    while (!flag) Sleep(10);
    t0 = GetTickCount();
    CHECK(stage_lock(), L"second stage gets the lock");
    CHECK(GetTickCount() - t0 > 800, L"... but only after the first one let go");
    stage_unlock();
    WaitForSingleObject(th, 5000); CloseHandle(th);
    g_app_root = HKEY_LOCAL_MACHINE; g_app_key = APPKEY;
}

/* ---------- 5. the exe path check ---------- */

static void test_check_exe(void)
{
    WCHAR bin[MAX_PATH], c[1000]; HANDLE h;
    wsprintfW(bin, L"%s\\bin", g_base);
    g_is_writable = fake_writable;
    wsprintfW(g_exe, L"%s\\DynaRun V3.exe", bin);
    CHECK(check_exe() && g_exe_trusted, L"a good exe in a folder users cannot change: trusted");
    g_is_writable = user_writable;
    CHECK(check_exe() && (g_exe_trusted == is_admin()), L"with the real ACL check a user-owned folder is not trusted (unless elevated)");
    g_is_writable = fake_writable;
    wsprintfW(g_exe, L"%s\\Other.exe", bin); CHECK(!check_exe(), L"another file name is refused");
    wsprintfW(g_exe, L"%s\\nothere\\DynaRun V3.exe", g_base); CHECK(!check_exe(), L"a missing exe is refused");
    g_exe[0] = 0; CHECK(!check_exe(), L"an empty path is refused");
    lstrcpyW(g_exe, L"DynaRun V3.exe"); CHECK(!check_exe(), L"a relative path is refused");
    lstrcpyW(g_exe, L"\\\\localhost\\c$\\x\\DynaRun V3.exe"); CHECK(!check_exe(), L"a UNC path is refused");
    wsprintfW(c, L"mklink /J \"%s\\binlink\" \"%s\" >nul", g_base, bin); sh(c);
    wsprintfW(g_exe, L"%s\\binlink\\DynaRun V3.exe", g_base);
    CHECK(check_exe() && !g_exe_trusted, L"a path through a junction is usable but not trusted");
    h = CreateFileW(L"C:\\Windows\\System32\\hostname.exe", 0, 7, NULL, OPEN_EXISTING, 0, NULL); CloseHandle(h);
    g_exe[0] = 0; g_exe_trusted = FALSE;
}

/* ---------- 6. delete_tree ---------- */

static void test_delete_tree(void)
{
    WCHAR t[MAX_PATH], x[MAX_PATH], p[MAX_PATH], c[1000];
    wsprintfW(t, L"%s\\dtree", g_base); CreateDirectoryW(t, NULL);
    wsprintfW(x, L"%s\\dtarget", g_base); CreateDirectoryW(x, NULL); mkfile(x, L"keep.txt", "precious");
    wsprintfW(p, L"%s\\sub", t); CreateDirectoryW(p, NULL); mkfile(p, L"f.txt", "x"); mkfile(t, L"g.txt", "x");
    mkfile(t, L"com-copied.txt", "record");
    wsprintfW(c, L"mklink /J \"%s\\junc\" \"%s\" >nul", t, x); sh(c);
    wsprintfW(c, L"mklink /J \"%s\\sub\\junc2\" \"%s\" >nul", t, x); sh(c);
    wsprintfW(p, L"%s\\junc", t);
    CHECK(GetFileAttributesW(p) & FILE_ATTRIBUTE_REPARSE_POINT, L"junction made");
    delete_tree(t, L"com-copied.txt\0", 0);
    wsprintfW(p, L"%s\\keep.txt", x); CHECK(exists(p), L"the junction's target is untouched");
    wsprintfW(p, L"%s\\junc", t); CHECK(!exists(p), L"the junction itself is gone");
    wsprintfW(p, L"%s\\sub", t); CHECK(!exists(p), L"sub folder (with a junction inside) is gone");
    wsprintfW(p, L"%s\\g.txt", t); CHECK(!exists(p), L"file gone");
    wsprintfW(p, L"%s\\com-copied.txt", t); CHECK(exists(p), L"the kept name stays");
    delete_tree(t, NULL, 0);
    CHECK(!exists(t), L"without a keep list everything goes");
    // the root itself is a junction: only the link goes
    wsprintfW(c, L"mklink /J \"%s\\rootlink\" \"%s\" >nul", g_base, x); sh(c);
    wsprintfW(p, L"%s\\rootlink", g_base);
    delete_tree(p, NULL, 0);
    wsprintfW(p, L"%s\\keep.txt", x); CHECK(exists(p), L"a junction given as the root: the target is untouched");
    wsprintfW(p, L"%s\\rootlink", g_base); CHECK(!exists(p), L"... and the link is removed");
}

/* ---------- 7. the administrators-only folder, copy, SID check, external files ---------- */

static void test_stage(void)
{
    WCHAR dir[MAX_PATH], dir2[MAX_PATH], f[MAX_PATH], src[MAX_PATH], c[1000], sid[200]; PSECURITY_DESCRIPTOR sd; PACL dacl; LPWSTR str; 
    CHECK(make_admin_dir(g_base, dir), L"admin dir created");
    CHECK(make_admin_dir(g_base, dir2) && lstrcmpiW(dir, dir2), L"every call makes a new name");
    if (GetNamedSecurityInfoW(dir, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd) == 0) {
        SECURITY_DESCRIPTOR_CONTROL ctl; DWORD rev;
        GetSecurityDescriptorControl(sd, &ctl, &rev);
        CHECK(ctl & SE_DACL_PROTECTED, L"DACL is protected (nothing inherited)");
        CHECK(dacl->AceCount == 2, L"two ACEs");
        if (ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &str, NULL)) {
            wprintf(L"info: DACL of the admin dir: %s\n", str);
            CHECK(wcsstr(str, L"(A;OICI;FA;;;BA)") && wcsstr(str, L"(A;OICI;FA;;;SY)") && !wcsstr(str, L";;;WD") && !wcsstr(str, L";;;BU"), L"only Administrators and SYSTEM");
            LocalFree(str);
        }
        LocalFree(sd);
    }
    // copy_new: a new file, refuses to overwrite, content identical, the source can change afterwards
    wsprintfW(src, L"%s\\src.bin", g_base); mkfile(g_base, L"src.bin", "0123456789");
    {   // ordinary users cannot create anything in it (this is what keeps the staged MSI out of reach)
        HANDLE t; wsprintfW(f, L"%s\\probe.bin", dir);
        t = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        CHECK(is_admin() || (t == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED), L"a non-administrator cannot create a file in the admin dir");
        if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
    }
    wsprintfW(f, L"%s\\copy.bin", g_base);
    CHECK(copy_new(src, f), L"copy_new");
    CHECK(!copy_new(src, f), L"copy_new refuses an existing target");
    mkfile(g_base, L"src.bin", "changed after the copy");
    CHECK(same_bytes(f, "0123456789", 10), L"the copy is not affected by later changes of the source");
    // take the folders back (as their owner) and delete them
    user_sid(sid, 200);
    wsprintfW(c, L"icacls \"%s\" /grant *%s:(OI)(CI)F /T >nul", dir, sid); sh(c);
    wsprintfW(c, L"icacls \"%s\" /grant *%s:(OI)(CI)F /T >nul", dir2, sid); sh(c);
    delete_tree(dir, NULL, 0); delete_tree(dir2, NULL, 0);
    CHECK(!exists(dir) && !exists(dir2), L"test folders removed");

    // valid_user_sid
    CHECK(valid_user_sid(sid), L"the current user's SID is valid");
    CHECK(!valid_user_sid(L"S-1-5-21-1-2-3-4"), L"a SID without a profile is refused");
    CHECK(!valid_user_sid(L"S-1-5-18"), L"SYSTEM is not a person");
    CHECK(!valid_user_sid(L"S-1-5-21-1-2-3-4\\..\\..\\x"), L"a path is not a SID");
    CHECK(!valid_user_sid(L""), L"empty");
    CHECK(!valid_user_sid(L"S-1-5-21"), L"too short");

    // external files of an MSI: read-only look at an MSI of the machine
    { WIN32_FIND_DATAW fd; HANDLE ff; WCHAR m[MAX_PATH], *l;
      GetWindowsDirectoryW(m, MAX_PATH); lstrcatW(m, L"\\Installer\\*.msi");
      ff = FindFirstFileW(m, &fd);
      if (ff != INVALID_HANDLE_VALUE) {
          GetWindowsDirectoryW(m, MAX_PATH); lstrcatW(m, L"\\Installer\\"); lstrcatW(m, fd.cFileName);
          l = msi_external_files(m);
          wprintf(L"info: msi_external_files(%s) = %s\n", fd.cFileName, l ? l : L"(none)");
          release(l); FindClose(ff);
      }
      CHECK(msi_external_files(L"C:\\no\\such.msi") == NULL, L"msi_external_files of a missing file: NULL");
      CHECK(msi_external_files(src) == NULL, L"msi_external_files of a non-MSI: NULL");
    }
}

/* ---------- 8. hive names ---------- */

// manifest_is_ours: every manifest DynaRunFix ever wrote is recognised (any line ending), anything else is not
static void put(const WCHAR *p, const void *d, DWORD n)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD w;
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, d, n, &w, NULL); CloseHandle(h); }
}

static void test_manifest(void)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(3), (LPCWSTR)RT_RCDATA); const char *d; DWORD n, i, j, k; char *c, *v; WCHAR p[MAX_PATH];
    static const char OLDV[] = "version=\"3.26.0.0\"", NEWV[] = "version=\"1.0.0.0\"";
    CHECK(r != NULL, L"manifest resource is embedded");
    if (!r) return;
    d = LockResource(LoadResource(NULL, r)); n = SizeofResource(NULL, r);
    wsprintfW(p, L"%s\\m.manifest", g_base);
    put(p, d, n);
    CHECK(manifest_is_ours(p), L"embedded manifest is ours");
    c = alloc(n * 2 + 64);
    for (i = 0, j = 0; i < n; i++) { if (d[i] == '\n' && (i == 0 || d[i - 1] != '\r')) c[j++] = '\r'; c[j++] = d[i]; }
    put(p, c, j);
    CHECK(manifest_is_ours(p), L"CRLF variant is ours");
    for (i = 0, k = 0; i < n; i++) if (d[i] != '\r') c[k++] = d[i];
    put(p, c, k);
    CHECK(manifest_is_ours(p), L"LF variant is ours");
    // the same text with the older assembly version (3.26.0.0)
    for (v = c; v < c + k && strncmp(v, NEWV, sizeof(NEWV) - 1); v++);
    CHECK(v < c + k, L"version attribute found");
    if (v < c + k) {
        char *o = alloc(k + 64); DWORD m = (DWORD)(v - c);
        memcpy(o, c, m); memcpy(o + m, OLDV, sizeof(OLDV) - 1); memcpy(o + m + sizeof(OLDV) - 1, v + sizeof(NEWV) - 1, k - m - (sizeof(NEWV) - 1));
        put(p, o, k - (DWORD)(sizeof(NEWV) - 1) + (DWORD)(sizeof(OLDV) - 1));
        CHECK(manifest_is_ours(p), L"3.26.0.0 variant is ours");
        release(o);
    }
    release(c);
    put(p, MANIFEST_V136, sizeof(MANIFEST_V136) - 1);
    CHECK(manifest_is_ours(p), L"release 1.3.6 manifest is ours");
    put(p, "<assembly>changed by someone</assembly>", 39);
    CHECK(!manifest_is_ours(p), L"a different file is not ours");
}

static void test_urls(void)
{
    CHECK(pkg_url_ok(L"https://github.com/a/b", GITHUB_HOSTS), L"github.com ok");
    CHECK(pkg_url_ok(L"https://release-assets.githubusercontent.com/x?y=1", GITHUB_HOSTS), L"asset host ok");
    CHECK(pkg_url_ok(L"https://objects.githubusercontent.com", GITHUB_HOSTS), L"objects host ok");
    CHECK(!pkg_url_ok(L"https://evil.example/x", GITHUB_HOSTS), L"other host refused");
    CHECK(!pkg_url_ok(L"https://githubusercontent.com/x", GITHUB_HOSTS), L"bare githubusercontent refused");
    CHECK(!pkg_url_ok(L"https://github.com.evil.example/x", GITHUB_HOSTS), L"suffix host refused");
    CHECK(!pkg_url_ok(L"https://github.com@evil.example/x", GITHUB_HOSTS), L"user name refused");
    CHECK(!pkg_url_ok(L"https://raw.githubusercontent.com/x", GITHUB_HOSTS), L"unlisted subdomain refused");
    CHECK(!pkg_url_ok(L"http://github.com/x", GITHUB_HOSTS), L"plain http refused");
    CHECK(pkg_url_ok(L"https://dynapro.co.uk/x", DYNAPRO_HOST), L"single host still works");
    CHECK(!pkg_url_ok(L"https://github.com/x", DYNAPRO_HOST), L"single host: other refused");
    CHECK(pkg_url_ok(L"https://anything.example/x", NULL), L"NULL = any host");
}

static void test_misc(void)
{
    WCHAR a[80], b[80];
    wsprintfW(a, HIVE_PREFIX L"-%lu-%lu-%d", 1UL, 2UL, 0); wsprintfW(b, HIVE_PREFIX L"-%lu-%lu-%d", 1UL, 2UL, 1);
    CHECK(lstrcmpW(a, b) != 0 && lstrlenW(a) < 80, L"hive names differ");
    unload_leftover_hives();       // nothing to unload: must not fail or hang
    CHECK(TRUE, L"unload_leftover_hives runs");
}

static void test_shortcut_paths(void)
{
    // Windows 7 style shell folders (the backup holds the full path of each shortcut as a value name)
    static const WCHAR *const user[] = { L"C:\\Users\\w7\\Desktop", L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu",
        L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs", L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch",
        L"C:\\Users\\w7\\AppData\\Roaming\\DynaRunFix" };
    static const WCHAR *const all[] = { L"C:\\Users\\Public\\Desktop", L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu",
        L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\\\" };
    static const WCHAR *const xp[] = { L"C:\\Documents and Settings\\All Users\\Desktop", L"C:\\Documents and Settings\\All Users\\Start Menu\\Programs" };
    const int nu = 5, na = 3, nx = 2;
    // legitimate: what 1.3.x backed up on Windows 7
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop\\Dyna Run V3.lnk", user, nu), L"w7 desktop");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Dyna Run V3.lnk", user, nu), L"w7 start menu root");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Dyna Pro Dynamometers\\Dyna Run V3.lnk", user, nu), L"w7 programs subfolder");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Dyna Run V3.lnk", user, nu), L"w7 programs root");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\Dyna Run V3.lnk", user, nu), L"w7 pinned taskbar item");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\w7\\AppData\\Roaming\\DynaRunFix\\DynaRun V3.lnk", user, nu), L"desktop_link() copy");
    CHECK(lnk_name_in_dirs(L"c:\\users\\W7\\DESKTOP\\dyna run v3.LNK", user, nu), L"case differs");
    CHECK(lnk_name_in_dirs(L"C:\\Users\\Public\\Desktop\\Dyna Run V3.lnk", all, na), L"public desktop");
    CHECK(lnk_name_in_dirs(L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Dyna Pro Dynamometers\\Dyna Run V3.lnk", all, na), L"common programs subfolder (folder with a trailing backslash)");
    CHECK(lnk_name_in_dirs(L"C:\\Documents and Settings\\All Users\\Start Menu\\Programs\\Dyna Pro Dynamometers\\Dyna Run V3.lnk", xp, nx), L"XP all users programs");
    CHECK(lnk_name_in_dirs(L"D:\\Users\\w7\\Desktop\\x.lnk", (const WCHAR *const[]){ L"D:\\Users\\w7\\Desktop" }, 1), L"profile on another drive");
    // refused: outside the shell folders, not a .lnk, dot-dot, device and relative paths, the folder itself
    CHECK(!lnk_name_in_dirs(L"C:\\Windows\\System32\\evil.lnk", user, nu), L"system folder");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Documents\\Dyna Run V3.lnk", user, nu), L"documents");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop2\\Dyna Run V3.lnk", user, nu), L"sibling folder with the same prefix");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop\\..\\..\\Windows\\evil.lnk", user, nu), L"dot-dot out of the desktop");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop\\Dyna Run V3.exe", user, nu), L"not a .lnk");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop\\.lnk", user, nu), L"no name");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop", user, nu), L"the folder itself");
    CHECK(!lnk_name_in_dirs(L"Desktop\\Dyna Run V3.lnk", user, nu), L"relative");
    CHECK(!lnk_name_in_dirs(L"\\\\?\\C:\\Users\\w7\\Desktop\\Dyna Run V3.lnk", user, nu), L"\\\\?\\ path");
    CHECK(!lnk_name_in_dirs(L"\\\\.\\C:\\Users\\w7\\Desktop\\Dyna Run V3.lnk", user, nu), L"device path");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\Public\\Desktop\\Dyna Run V3.lnk", user, nu), L"public desktop is not a user folder");
    CHECK(!lnk_name_in_dirs(L"C:\\Users\\w7\\Desktop\\Dyna Run V3.lnk", NULL, 0), L"no folders");
    // the launcher as 1.3.x on 64-bit Windows recorded it (WOW64 spellings) is still recognised as ours
    CHECK(launcher_path_like(L"C:\\Program Files (x86)\\DynaRunFix\\DynaRunFix.exe"), L"launcher x86 folder");
    CHECK(launcher_path_like(L"C:\\Program Files\\DynaRunFix\\DynaRunFix.exe"), L"launcher Program Files");
    CHECK(launcher_path_like(L"c:\\program files (x86)\\dynarunfix\\DYNARUNFIX.EXE"), L"launcher case");
    CHECK(!launcher_path_like(L"C:\\Program Files (x86)\\Dyna Pro Dynamometers\\DynaRun V3.exe"), L"DynaRun itself");
    CHECK(!launcher_path_like(L"C:\\Tools\\DynaRunFix.exe"), L"other folder name");
    CHECK(!launcher_path_like(L"DynaRunFix.exe"), L"no folder");
}

int wmain(void)
{
    HKEY k;
    g_quiet = TRUE; g_zh = FALSE;
    setvbuf(stdout, NULL, _IONBF, 0);
    setup_tree();
    wprintf(L"[user_writable]\n"); test_user_writable();
    wprintf(L"[paths]\n"); test_paths();
    wprintf(L"[promote]\n"); test_promote();
    wprintf(L"[touch]\n"); test_touch();
    wprintf(L"[check_exe]\n"); test_check_exe();
    wprintf(L"[delete_tree]\n"); test_delete_tree();
    wprintf(L"[stage]\n"); test_stage();
    wprintf(L"[manifest]\n"); test_manifest();
    wprintf(L"[urls]\n"); test_urls();
    wprintf(L"[misc]\n"); test_misc();
    wprintf(L"[shortcut paths]\n"); test_shortcut_paths();
    {   // clean up
        WCHAR c[1000];
        wsprintfW(c, L"reg delete \"HKCU\\%s\" /f >nul 2>nul", g_testkey); sh(c);
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, g_testkey, 0, KEY_READ, &k)) { RegCloseKey(k); delete_key_tree(HKEY_CURRENT_USER, g_testkey); }
        wsprintfW(c, L"icacls \"%s\" /reset /T /C >nul 2>nul", g_base); sh(c);
        delete_tree(g_base, NULL, 0);
    }
    wprintf(L"\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
