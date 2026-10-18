// dynafix_pegrc.inc.c - PEGRC32D.dll language resource redirect for the graph control, and clear_runasinvoker.
// Included by dynafix.c (a single compilation unit, also pulled in by tests\logtest.c); not compiled on its own.

// ---- PEGRC32D.dll: the graph control's language resource ----
// The graph control (Gigasoft ProEssentials: Pesgo32e.ocx on PEGRP32E.dll) shows its popup menu in the language of a resource file
// "PEGRC32D.dll" that it looks up by name: DynaRun's folder, then the system folder. DynaRun's installer puts the copy for the chosen
// language in %APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\Language\<language>\ and, at every start, tries to copy it into the system
// folder; without administrator rights that is refused and the menu stays English. Here the control's request for that name is answered
// with that per-user file instead, nothing is written anywhere. Only a resource-only load (LOAD_LIBRARY_AS_DATAFILE / AS_IMAGE_RESOURCE,
// which is what the control asks for) is redirected: the user-writable file is never run as code; a request that loads code is left alone.
// Only when DynaRun is not elevated (elevated, DynaRun's own copy works), and only for this one file name: the path is built here from
// the profile and the language value, checked (regular file, no reparse point below the profile) and never taken from the request.
// DYNAFIX_PEGRC=0 switches it off. The imports of the control's two modules are patched from the loader's notification (before the
// control's entry point runs: it asks for the file while loading), and from peg_check, which looks for them as DynaRun's windows come.
static BOOL g_pegrc = TRUE;
static int g_pegel = -1;                      // 1 elevated, 0 not, set once
static FARPROC r_pLLxA, r_pLLxW;           // the control's LoadLibraryEx imports
static HMODULE g_pegmod[2];
static BOOL g_pegmsg;

static BOOL peg_elevated(void)
{
    if (g_pegel < 0) {
        HANDLE t; DWORD el = 0, n = 0; BOOL ok = FALSE;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
            ok = GetTokenInformation(t, (TOKEN_INFORMATION_CLASS)20 /* TokenElevation */, &el, sizeof(el), &n);
            CloseHandle(t);
        }
        // Before Vista there is no UAC and no TokenElevation: not "elevated". On Vista and later a failed query counts as elevated
        // (no redirect): the safe side.
        if (ok) g_pegel = el ? 1 : 0;
        else {
#pragma warning(suppress: 4996)
            DWORD ver = GetVersion();      // only the major version is needed (before Vista = 5 or less)
            g_pegel = LOBYTE(LOWORD(ver)) < 6 ? 0 : 1;
        }
    }
    return g_pegel == 1;
}

typedef HRESULT (WINAPI *gfpw_t)(HWND, int, HANDLE, DWORD, LPWSTR);

// Fills path with the validated per-user file, or returns FALSE (reason in *why).
static BOOL peg_path(WCHAR *path, const char **why)
{
    static const WCHAR tail[] = L"\\Dyna Pro Dynamometers\\Dyna Run V3\\Language\\";
    WCHAR prof[MAX_PATH], app[MAX_PATH], lang[40], tmp[MAX_PATH], root[MAX_PATH]; DWORD t = 0, n = sizeof(lang) - 2; HKEY k; HMODULE sh; gfpw_t gfp;
    int i, rl, bad = 0; DWORD a;
    *why = "no profile folder";
    sh = GetModuleHandleA("shell32.dll");
    gfp = sh ? (gfpw_t)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
    if (!gfp || gfp(NULL, 0x001a /* CSIDL_APPDATA */, NULL, 0, app) != S_OK || gfp(NULL, 0x0028 /* CSIDL_PROFILE */, NULL, 0, prof) != S_OK) return FALSE;
    *why = "no Default_Language";
    lang[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"" LANG_KEY, 0, KEY_QUERY_VALUE, &k)) return FALSE;
    if (RegQueryValueExW(k, L"Default_Language", NULL, &t, (BYTE *)lang, &n) || t != REG_SZ) lang[0] = 0;
    RegCloseKey(k);
    lang[n / 2 < 39 ? n / 2 : 39] = 0;
    if (!lang[0]) return FALSE;
    for (i = 0; lang[i]; i++)                 // a folder name, nothing else
        if (!((lang[i] >= L'A' && lang[i] <= L'Z') || (lang[i] >= L'a' && lang[i] <= L'z') || (lang[i] >= L'0' && lang[i] <= L'9') || lang[i] == L'_' || lang[i] == L' ' || lang[i] == L'-')) bad = 1;
    *why = "Default_Language is not a plain folder name";
    if (bad) return FALSE;
    *why = "path too long";
    if (lstrlenW(app) + lstrlenW(tail) + lstrlenW(lang) + 14 >= MAX_PATH) return FALSE;
    lstrcpyW(tmp, app); lstrcatW(tmp, tail); lstrcatW(tmp, lang); lstrcatW(tmp, L"\\PEGRC32D.dll");
    *why = "path not canonical";
    if (!GetFullPathNameW(tmp, MAX_PATH, path, NULL) || lstrcmpiW(tmp, path)) return FALSE;
    // reparse points are checked below the profile (or below AppData when that lies elsewhere)
    rl = lstrlenW(prof);
    if (CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, path, rl, prof, rl) == CSTR_EQUAL && path[rl] == L'\\') lstrcpynW(root, prof, MAX_PATH);
    else lstrcpynW(root, app, MAX_PATH);
    rl = lstrlenW(root);
    *why = "a folder on the way is missing or a reparse point";
    for (i = rl + 1; path[i]; i++) {
        if (path[i] != L'\\') continue;
        lstrcpynW(tmp, path, i + 1);
        a = GetFileAttributesW(tmp);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return FALSE;
    }
    *why = "file missing, not a regular file, or a reparse point";
    a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return FALSE;
    *why = "";
    return TRUE;
}

// The request is for PEGRC32D.dll by bare name or with the system folder's path.
static BOOL peg_name(const WCHAR *n)
{
    WCHAR sys[MAX_PATH + 16]; int l;
    if (!n || (UINT_PTR)n < 0x10000) return FALSE;
    if (!lstrcmpiW(n, L"PEGRC32D.dll")) return TRUE;
    l = lstrlenW(n);
    if (l < 14 || lstrcmpiW(n + l - 13, L"\\PEGRC32D.dll")) return FALSE;
    {   // the same file name behind a path: only the system folders (what the control would find by itself)
        typedef UINT (WINAPI *gsw_t)(LPWSTR, UINT);
        gsw_t g = (gsw_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemWow64DirectoryW");
        int k;
        for (k = 0; k < 2; k++) {
            UINT m = k == 0 ? GetSystemDirectoryW(sys, MAX_PATH) : (g ? g(sys, MAX_PATH) : 0);
            if (m && m < MAX_PATH && l == (int)m + 13 && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, n, m, sys, m) == CSTR_EQUAL) return TRUE;
        }
    }
    return FALSE;
}

// The load of PEGRC32D.dll asked as (api, name, flags): the handle of the per-user file, or NULL to go on with the original call.
static HMODULE peg_load(const char *api, const WCHAR *name, DWORD flags)
{
    WCHAR path[MAX_PATH]; const char *why = ""; char u[MAX_PATH * 2], msg[MAX_PATH * 3 + 200]; HMODULE h;
    typedef HMODULE (WINAPI *llx_t)(LPCWSTR, HANDLE, DWORD);
    w2u(u, sizeof(u), name);
    if (peg_elevated()) {
        if (!g_pegmsg) { g_pegmsg = TRUE; wsprintfA(msg, "PEGRC32D: %s('%s', flags %08X): elevated, left alone (DynaRun's own copy works)", api, u, flags); logf("%s\r\n", (DWORD)(UINT_PTR)msg, 0, 0); }
        return NULL;
    }
    if (!(flags & (0x00000002 /* AS_DATAFILE */ | 0x00000020 /* AS_IMAGE_RESOURCE */ | 0x00000040 /* AS_DATAFILE_EXCLUSIVE */))) {
        wsprintfA(msg, "PEGRC32D: %s('%s', flags %08X): not redirected, the caller loads code, not just resources", api, u, flags);
        logf("%s\r\n", (DWORD)(UINT_PTR)msg, 0, 0);
        return NULL;
    }
    if (!peg_path(path, &why)) {
        wsprintfA(msg, "PEGRC32D: %s('%s', flags %08X): not redirected, %s", api, u, flags, why);
        logf("%s\r\n", (DWORD)(UINT_PTR)msg, 0, 0);
        return NULL;
    }
    // The per-user file is only ever loaded as a resource file, whatever the caller asked for.
    h = ((llx_t)r_pLLxW)(path, NULL, 0x00000002 /* LOAD_LIBRARY_AS_DATAFILE */ | 0x00000020 /* LOAD_LIBRARY_AS_IMAGE_RESOURCE */);
    w2u(u, sizeof(u), path);
    wsprintfA(msg, "PEGRC32D: %s redirected to '%s' (asked flags %08X, loaded as a resource file only): %s", api, u, flags, h ? "loaded" : "failed, the original call goes on");
    logf("%s\r\n", (DWORD)(UINT_PTR)msg, 0, 0);
    return h;
}

static HMODULE WINAPI H_pLLxW(LPCWSTR n, HANDLE f, DWORD fl)
{
    HMODULE h = NULL;
    if (g_pegrc && !f && peg_name(n)) h = peg_load("LoadLibraryExW", n, fl);
    return h ? h : ((HMODULE (WINAPI *)(LPCWSTR, HANDLE, DWORD))r_pLLxW)(n, f, fl);
}
static HMODULE peg_a(const char *api, LPCSTR n, DWORD fl)
{
    WCHAR w[MAX_PATH];
    if (!g_pegrc || !n || (UINT_PTR)n < 0x10000 || !MultiByteToWideChar(CP_ACP, 0, n, -1, w, MAX_PATH) || !peg_name(w)) return NULL;
    return peg_load(api, w, fl);
}
static HMODULE WINAPI H_pLLxA(LPCSTR n, HANDLE f, DWORD fl)
{
    HMODULE h = f ? NULL : peg_a("LoadLibraryExA", n, fl);
    return h ? h : ((HMODULE (WINAPI *)(LPCSTR, HANDLE, DWORD))r_pLLxA)(n, f, fl);
}

static hook_t g_peg_hooks[] = {
    { "LoadLibraryExA", &r_pLLxA, (FARPROC)H_pLLxA, NULL, "kernel32" },
    { "LoadLibraryExW", &r_pLLxW, (FARPROC)H_pLLxW, NULL, "kernel32" },
    { 0 }
};

static const char *g_pegmods[2] = { "Pesgo32e.ocx", "PEGRP32E.dll" };

// The control may ask for the file while it is being loaded (its entry point), i.e. before the load call returns: the loader's
// notification for the module comes before that, so the control's imports are patched from there too (nothing but plain memory
// and the log are touched; the log is written with the names already in hand).
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } peg_us;
static volatile LONG g_pegready;
static BOOL g_pegnote;
static volatile LONG g_pegbusy;               // 1 while hook_t's raw/chain are being patched (peg_check or the notification)
static volatile LONG g_pegdefer;              // a notification found g_pegbusy taken: peg_check patches the modules again
static VOID NTAPI peg_ldr_note(ULONG reason, const ldrnote_t *d, PVOID ctx)
{
    WCHAR n[32]; const peg_us *u; int i, k;
    (void)ctx;
    if (reason != 1 /* LOADED */ || !d || !g_pegready) return;
    u = (const peg_us *)d->BaseDllName;
    if (!u || !u->Buffer || u->Length / 2 >= 32) return;
    for (i = 0; i < (int)(u->Length / 2); i++) n[i] = u->Buffer[i];
    n[i] = 0;
    for (k = 0; k < 2; k++) {
        WCHAR w[32]; int j;
        for (j = 0; g_pegmods[k][j]; j++) w[j] = (WCHAR)g_pegmods[k][j];
        w[j] = 0;
        if (!lstrcmpiW(n, w)) {
            // under the loader lock: the busy flag is never waited for - when peg_check is patching (on any thread) it is left to peg_check, no log here.
            // The branch that wins the flag does wait, but only a bounded time: logf takes the log mutex (at most 2 s) and patch_base reads and patches memory.
            if (InterlockedCompareExchange(&g_pegbusy, 1, 0)) { InterlockedExchange(&g_pegdefer, 1); continue; }
            logf("PEGRC32D: loader notification for %s %u %u\r\n", (DWORD)(UINT_PTR)g_pegmods[k], 0, 0);
            patch_base((BYTE *)d->DllBase, g_pegmods[k], "peg-note", g_peg_hooks);
            InterlockedExchange(&g_pegbusy, 0);
        }
    }
}

// Patch the control's modules once they are loaded (also the fallback when no loader notifications exist); idempotent.
static void peg_check(void)
{
    static BOOL resolved, off;
    int i;
    if (!g_pegrc || off || !GetModuleHandleA("MSVBVM60.DLL")) return;
    if (InterlockedCompareExchange(&g_pegbusy, 1, 0)) return;         // not from inside our own patching
    if (!resolved) {
        if (peg_elevated()) { off = TRUE; logf("PEGRC32D: DynaRun is elevated: no redirect %u %u %u\r\n", 0, 0, 0); InterlockedExchange(&g_pegbusy, 0); return; }
        resolved = TRUE; resolve(g_peg_hooks);
        {   // see peg_ldr_note
            HMODULE nt = GetModuleHandleA("ntdll.dll"); ldrreg_t rg = nt ? (ldrreg_t)GetProcAddress(nt, "LdrRegisterDllNotification") : NULL; PVOID ck;
            if (rg && !g_pegnote) { g_pegnote = TRUE; InterlockedExchange(&g_pegready, 1); if (rg(0, peg_ldr_note, NULL, &ck)) InterlockedExchange(&g_pegready, 0); }
        }
    }
    if (InterlockedExchange(&g_pegdefer, 0)) {   // a notification was skipped meanwhile: patch whatever is loaded now
        g_pegmod[0] = g_pegmod[1] = NULL;
        logf("PEGRC32D: loader notification came while patching, checking the modules again %u %u %u\r\n", 0, 0, 0);
    }
    for (i = 0; i < 2; i++) {
        HMODULE m = GetModuleHandleA(g_pegmods[i]);
        if (m && m != g_pegmod[i]) {
            g_pegmod[i] = m;
            logf("PEGRC32D: patching the LoadLibrary imports of %s %u %u\r\n", (DWORD)(UINT_PTR)g_pegmods[i], 0, 0);
            patch_base((BYTE *)m, g_pegmods[i], "peg", g_peg_hooks);
        } else if (!m) g_pegmod[i] = NULL;
    }
    InterlockedExchange(&g_pegbusy, 0);
}

// The launcher starts DynaRun with "RunAsInvoker" appended to __COMPAT_LAYER (so that DynaRun itself never runs elevated). That
// environment variable is inherited by DynaRun's own child processes; DynaRun is started that way only so that it never runs
// elevated itself, and its other children should not silently lose a UAC prompt they need, so here the word is taken out again
// (what was there before stays; the variable is removed when nothing else is left). The first-start Setup helper, the one child
// that is wanted without elevation, gets the layer for its own start only (shexec_as_invoker in dynafix.c). The launcher process
// itself (which loads this dll too) is left alone.
// no CRT in this DLL: stop newer MSVC from turning the copy loops below into memcpy calls
#pragma optimize("", off)
static void clear_runasinvoker(void)
{
    char exe[MAX_PATH], v[512], o[512], msg[400], *p, *q; DWORD n;
    n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n && n < MAX_PATH) {
        for (p = exe + n; p > exe && p[-1] != '\\'; p--);
        if (!lstrcmpiA(p, "DynaRunFix.exe")) return;
    }
    n = GetEnvironmentVariableA("__COMPAT_LAYER", v, sizeof(v));
    if (!n || n >= sizeof(v)) return;
    o[0] = 0; q = o; p = v;
    while (*p) {
        char *e; int len, keep; char tok[16];
        while (*p == ' ') p++;
        if (!*p) break;
        for (e = p; *e && *e != ' '; e++);
        len = (int)(e - p);
        keep = 1;
        if (len == 12) { int c; for (c = 0; c < 12; c++) tok[c] = p[c]; tok[12] = 0; keep = lstrcmpiA(tok, "RunAsInvoker") != 0; }
        if (keep) {
            if (q != o) *q++ = ' ';
            while (p < e) *q++ = *p++;
        }
        p = e;
    }
    *q = 0;
    if (lstrcmpA(o, v)) {
        SetEnvironmentVariableA("__COMPAT_LAYER", o[0] ? o : NULL);
        wsprintfA(msg, "RunAsInvoker taken out of __COMPAT_LAYER for DynaRun's child processes (left: '%.200s')\r\n", o);
        logf("%s", (DWORD)(UINT_PTR)msg, 0, 0);
    }
}
#pragma optimize("", on)
