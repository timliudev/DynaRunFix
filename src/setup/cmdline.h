// Part of setup.c (command line and verb table): included by it, in one compilation unit; never compiled on its own.
// the command line after its first n arguments, as typed (the launcher's arguments for /update)
static const WCHAR *after_args(int n)
{
    const WCHAR *c = GetCommandLineW(); BOOL q = FALSE;
    while (n--) {
        while (*c == ' ' || *c == '\t') c++;
        for (; *c && (q || (*c != ' ' && *c != '\t')); c++) if (*c == '"') q = !q;
    }
    while (*c == ' ' || *c == '\t') c++;
    return c;
}

// The /unverified rule of both stages: argv[i] is /unverified and a value of exactly 64 characters follows it, else NULL. The elevated install stage
// (/machine, /full) looks for it at every position from argv[4] on (cmd_parse), /driver-prep only at argv[4]: the two differ for a hand-typed
// "/driver-prep <sid> <zip> /quiet /unverified <sha256>" (see cmd_parse).
static const WCHAR *unverified_value(int argc, WCHAR **argv, int i)
{
    return i + 1 < argc && !lstrcmpiW(argv[i], L"/unverified") && lstrlenW(argv[i + 1]) == 64 ? argv[i + 1] : NULL;
}

// The switches of the command line, at any position (tests/test_hardening.c and test_driver.c lock the rules). c->opts is set to the defaults
// (install_opts_default), then quiet, close, keep are set and pin, autostart cleared by what the command line says;
// c->pin: /pin seen; c->dzip, c->daccept, c->daccept4: the values of /with-driver and /unverified (only from argv[4] on, after the verb, the SID and the path).
// /with-driver and /unverified take the next argument whatever it is; an /unverified value that is not 64 characters long is dropped. daccept is the last
// valid /unverified at index >= 4 (the install stage's rule), daccept4 the one at index 4 (the driver prep's rule); they differ when
//  - a valid /unverified comes later (argv[5] on, e.g. after /quiet): daccept has it, daccept4 is NULL;
//  - two valid ones are given: daccept is the later one, daccept4 the one at argv[4].
// Each verb keeps using the rule it always had (/machine, /full: daccept; /driver-prep: daccept4), so no command line behaves differently from before.
static void cmd_parse(cmdline *c)
{
    int i; const WCHAR *v;
    install_opts_default(&c->opts);
    c->pin = FALSE; c->dzip = c->daccept = c->daccept4 = NULL;
    for (i = 1; i < c->argc; i++) {
        WCHAR *a = c->argv[i];
        if (!lstrcmpiW(a, L"/quiet")) c->opts.quiet = TRUE;
        else if (!lstrcmpiW(a, L"/close")) c->opts.close = TRUE;
        else if (!lstrcmpiW(a, L"/notaskbar")) c->opts.pin = FALSE;
        else if (!lstrcmpiW(a, L"/noautostart")) c->opts.autostart = FALSE;
        else if (!lstrcmpiW(a, L"/pin")) c->pin = TRUE;
        else if (!lstrcmpiW(a, L"/keep")) c->opts.keep = TRUE;
        else if (i >= 4 && !lstrcmpiW(a, L"/with-driver") && i + 1 < c->argc) c->dzip = c->argv[++i];                              // after the SID and the path: see install_fix_driver
        else if (i >= 4 && !lstrcmpiW(a, L"/unverified") && i + 1 < c->argc) {
            if ((v = unverified_value(c->argc, c->argv, i)) != NULL) { c->daccept = v; if (i == 4) c->daccept4 = v; }
            i++;
        }
    }
}

/* ---------- the verbs: argv[1] ---------- */

static int cmd_machine(cmdline *c)                  // /machine <sid> <DynaRun exe> [/with-driver <zip> [/unverified <sha256>]]: the elevated install stage
{
    int rc;
    lstrcpynW(g_exe, c->argv[3], MAX_PATH);
    CoInitialize(NULL);
    rc = machine_install(&c->opts, c->argv[2]);
    if (!rc && c->dzip) rc = driver_prep_stage(c->argv[2], c->dzip, c->daccept);
    return rc;
}

static int cmd_full(cmdline *c)                     // /full <sid> <msi> [/with-driver ...]: the same, with the DynaRun MSI first
{
    int rc;
    CoInitialize(NULL);
    rc = machine_full(&c->opts, c->argv[2], c->argv[3]);
    if (!rc && c->dzip) rc = driver_prep_stage(c->argv[2], c->dzip, c->daccept);
    return rc;
}

static int cmd_msi_only(cmdline *c)                 // /msi-only <sid> <msi>: the elevated stage that installs Dyna Pro's setup and nothing else
{
    CoInitialize(NULL);
    return machine_msi_only(&c->opts, c->argv[2], c->argv[3]);
}

static int cmd_unmachine(cmdline *c)                // /unmachine [<sid>]: the elevated uninstall stage
{
    reg_str(HKEY_LOCAL_MACHINE, APPKEY, L"DynaRunExe", g_exe, MAX_PATH, 0);
    CoInitialize(NULL);
    return machine_uninstall(&c->opts, c->argc >= 3 && c->argv[2][0] == 'S' && c->argv[2][1] == '-' ? c->argv[2] : NULL);
}

static int cmd_uninstall(cmdline *c) { return uninstall(&c->opts); }
static int cmd_checkupdate(cmdline *c) { (void)c; return update_check(); }
static int cmd_update(cmdline *c) { (void)c; return update_run(after_args(2)); }
static int cmd_offer(cmdline *c) { (void)c; return update_offer(); }

// min_args: the verb is only taken with that many arguments (the program counts); with fewer, the command line is no verb and the wizard (or /quiet install) runs.
// The elevated driver verbs take 2 and check their own arguments (a /driver-prep without them is an error exit, not the wizard).
static const verb VERBS[] = {
    { L"/machine", 4, cmd_machine },
    { L"/full", 4, cmd_full },
    { L"/msi-only", 4, cmd_msi_only },
    { L"/unmachine", 2, cmd_unmachine },
    { L"/driver", 2, cmd_driver },                  // driver.c
    { L"/driver-continue", 2, cmd_driver_continue },
    { L"/driver-prep", 2, cmd_driver_prep },
    { L"/driver-install", 2, cmd_driver_install },
    { L"/driver-cancel", 2, cmd_driver_cancel },
    { L"/uninstall", 2, cmd_uninstall },
    { L"/checkupdate", 2, cmd_checkupdate },
    { L"/update", 2, cmd_update },
    { L"/offer", 2, cmd_offer },
};

static const verb *verb_find(int argc, WCHAR **argv)
{
    int i;
    if (argc < 2) return NULL;
    for (i = 0; i < (int)(sizeof(VERBS) / sizeof(VERBS[0])); i++)
        if (argc >= VERBS[i].min_args && !lstrcmpiW(argv[1], VERBS[i].name)) return &VERBS[i];
    return NULL;
}

// Runs the verb of the command line: its exit code, or DRV_NOVERB when argv[1] is none (the caller then installs or opens the wizard).
static int verb_run(cmdline *c)
{
    const verb *v = verb_find(c->argc, c->argv);
    return v ? v->run(c) : DRV_NOVERB;
}

