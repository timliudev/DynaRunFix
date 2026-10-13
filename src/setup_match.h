// setup_match.h - decides whether a ShellExecuteA target is DynaRun's first-run helper.
//
// Pure helper (Win32 path APIs only, no state, no CRT) so tests\test_shexec_match.c can compile it standalone.
//
// The helper is exactly  <dir>\Setup_<one or more ASCII digits>.exe  where <dir> is one of the given
// "System Data" folders. Everything else (Setup_Guide.pdf, Setup_1\x.txt, Setup_.exe, Setup_12a.exe,
// the same name in another folder, relative paths, ...) is not the helper and must pass through untouched.
#ifndef SETUP_MATCH_H
#define SETUP_MATCH_H
#include <windows.h>

// Normalises a path: surrounding quotes and blanks removed, '/' -> '\', made absolute and canonical
// (GetFullPathName: "." / ".." / duplicate separators), 8.3 names expanded when the path exists.
// Only drive-absolute paths ("X:\...") are accepted: a relative path depends on the current directory,
// UNC and \\?\ forms are not what DynaRun uses. Returns the length, 0 if rejected.
static int sm_normalize(const char *in, char *out, int size)
{
    char tmp[MAX_PATH + 2]; char *full = out; int n, i;
    DWORD r;
    if (!in || size < MAX_PATH) return 0;
    while (*in == ' ') in++;
    n = lstrlenA(in);
    while (n > 0 && in[n - 1] == ' ') n--;
    if (n >= 2 && in[0] == '"' && in[n - 1] == '"') { in++; n -= 2; }
    if (n < 3 || n > MAX_PATH - 1) return 0;
    for (i = 0; i < n; i++) {
        char c = in[i];
        if (c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) return 0;
        tmp[i] = (c == '/') ? '\\' : c;
    }
    tmp[n] = 0;
    if (!((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z')) || tmp[1] != ':' || tmp[2] != '\\') return 0;
    r = GetFullPathNameA(tmp, size, full, NULL);
    if (!r || r >= (DWORD)size) return 0;
    {   // expand 8.3 components if the path exists; keep the plain form otherwise
        char lng[MAX_PATH + 2]; DWORD l = GetLongPathNameA(full, lng, sizeof(lng));
        if (l && l < sizeof(lng)) { lstrcpyA(full, lng); r = l; }
    }
    while (r > 3 && full[r - 1] == '\\') full[--r] = 0;
    return (int)r;
}

static BOOL sm_leaf_is_setup(const char *leaf)
{
    int i, d = 0;
    if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, leaf, 6, "Setup_", 6) != CSTR_EQUAL) return FALSE;
    for (i = 6; leaf[i] >= '0' && leaf[i] <= '9'; i++) d++;
    if (!d) return FALSE;
    return lstrlenA(leaf + i) == 4 && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, leaf + i, 4, ".exe", 4) == CSTR_EQUAL;
}

// file: the lpFile of ShellExecuteA. dirs[0..ndirs): folders the helper may live in (any form sm_normalize
// accepts; NULL entries are skipped). Case-insensitive full-path comparison of the folder.
static BOOL setup_helper_match(const char *file, const char *const *dirs, int ndirs)
{
    char full[MAX_PATH + 2], d[MAX_PATH + 2]; int n, i, leaf;
    if (!file || !(n = sm_normalize(file, full, sizeof(full)))) return FALSE;
    for (leaf = n; leaf > 0 && full[leaf - 1] != '\\'; leaf--);
    if (leaf < 3 || !sm_leaf_is_setup(full + leaf)) return FALSE;
    for (i = 0; i < ndirs; i++) {
        int dn;
        if (!dirs[i] || !(dn = sm_normalize(dirs[i], d, sizeof(d)))) continue;
        if (dn == leaf - 1 && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, full, dn, d, dn) == CSTR_EQUAL) return TRUE;
        if (leaf == 3 && dn == 2 + 1 && CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, full, 3, d, 3) == CSTR_EQUAL) return TRUE;   // drive root
    }
    return FALSE;
}

#endif
