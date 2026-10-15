// dfutil.h - small pure helpers of dynafix.dll (no state, no CRT), so tests\test_dfutil.c can compile them standalone.
#ifndef DFUTIL_H
#define DFUTIL_H
#include <windows.h>

// GetEnvironmentVariableA(name, v, sz) returned n. When the value does not fit, n is the size it needs and v is undefined; when
// the variable is not set, n is 0. A switch is on only when the whole value is the single character `want` ("1", not "10", "1x"
// or a long value that happens to start with 1).
static int df_env_flag(const char *v, DWORD n, DWORD sz, char want)
{
    return n == 1 && n < sz && v[0] == want;
}

// A decimal number (digits only, at most 9 of them) stored in the variable. 0 if unset, too long for v, empty or not all digits.
static int df_env_num(const char *v, DWORD n, DWORD sz, long *out)
{
    DWORD i; long r = 0;
    if (n == 0 || n >= sz || n > 9) return 0;
    for (i = 0; i < n; i++) { if (v[i] < '0' || v[i] > '9') return 0; r = r * 10 + (v[i] - '0'); }
    *out = r;
    return 1;
}

// The value is exactly `word` (case-insensitive) and fitted in v.
static int df_env_word(const char *v, DWORD n, DWORD sz, const char *word)
{
    return n > 0 && n < sz && !lstrcmpiA(v, word);
}

// Is the module path `mod` (GetModuleFileNameA(..., size) returned n) the file dynapro.dll, by its file name: "x\dynapro.dll"
// or "dynapro.dll", not "xdynapro.dll". n == size means the path was cut to fit (and is not terminated): not matched.
static int df_is_dynapro(const char *mod, DWORD n, DWORD size)
{
    static const char name[] = "dynapro.dll";
    if (n < 11 || n >= size) return 0;
    if (n > 11 && mod[n - 12] != '\\' && mod[n - 12] != '/') return 0;
    return !lstrcmpiA(mod + n - 11, name);
}

#endif
