// Part of setup.c (stage lock and setup.log): included by it, in one compilation unit; never compiled on its own.
/* ---------- elevated stage: one at a time, trust checks, the borrowed time stamp ---------- */

// The elevated stages (install, uninstall, MSI) of two setups must not interleave. Recursive for the owning thread.
static HANDLE g_stage_mx; static int g_stage_depth;

static BOOL stage_lock(void)
{
    DWORD r;
    if (!g_stage_mx && !(g_stage_mx = CreateMutexW(NULL, FALSE, L"Global\\DynaRunFix-machine-stage"))) return FALSE;
    r = WaitForSingleObject(g_stage_mx, 60000);
    if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) return FALSE;     // an abandoned mutex (a setup that was killed) is ours now
    g_stage_depth++;
    return TRUE;
}

static void stage_unlock(void) { if (g_stage_depth > 0) { g_stage_depth--; ReleaseMutex(g_stage_mx); } }

static const WCHAR *busy_text(void)
{ return TS(ST_ANOTHER_DYNARUNFIX_SETUP_IS_RUNNING); }

// A line in <install folder>\setup.log (only the elevated stage writes there; the folder is administrators-only)
static void setup_log(const WCHAR *line)
{
    static WCHAR p[MAX_PATH];
    if (g_dir[0] && exists(g_dir)) append_line(cat3(p, g_dir, L"\\setup.log", NULL), line);
}

static void warn(const install_opts *o, const WCHAR *text) { setup_log(text); if (!o->quiet) msg(text, MB_ICONWARNING); }

