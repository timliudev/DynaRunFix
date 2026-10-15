// Tests of src\dfutil.h (the DYNAFIX_* switch parsing and the dynapro.dll file name match of dynafix.dll). Host-native, with the CRT.
#include <windows.h>
#include <stdio.h>
#include "../src/dfutil.h"

static int g_pass, g_fail;
#define CHECK(c, what) do { if (c) g_pass++; else { g_fail++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

// what GetEnvironmentVariableA would give for value `val` into a buffer of sz bytes: n = length, or the size needed (with the terminator)
// when it does not fit - and then the buffer is not to be trusted (here: filled with '1')
static int flag(const char *val, DWORD sz, char want)
{
    char v[16]; DWORD len = lstrlenA(val), n, i;
    for (i = 0; i < sizeof(v); i++) v[i] = '1';
    if (len < sz) { lstrcpyA(v, val); n = len; } else n = len + 1;
    return df_env_flag(v, n, sz, want);
}

static int num(const char *val, DWORD sz, long *out)
{
    char v[16]; DWORD len = lstrlenA(val), n, i;
    for (i = 0; i < sizeof(v); i++) v[i] = '7';
    if (len < sz) { lstrcpyA(v, val); n = len; } else n = len + 1;
    return df_env_num(v, n, sz, out);
}

int main(void)
{
    long r = -1;
    CHECK(flag("1", 4, '1'), "1 switches on");
    CHECK(!flag("0", 4, '1'), "0 is off");
    CHECK(!flag("", 4, '1'), "empty is off");
    CHECK(!flag("10", 4, '1'), "10 is not 1");
    CHECK(!flag("1x", 4, '1'), "1x is not 1");
    CHECK(!flag("123", 4, '1'), "123 is not 1");
    CHECK(!flag("1234", 4, '1'), "value that does not fit: not read (the buffer holds only 1s)");
    CHECK(!flag("1111111", 4, '1'), "long value starting with 1");
    CHECK(flag("0", 4, '0'), "0 switches a 0-switch");
    CHECK(!flag("00", 4, '0'), "00 is not 0");
    { char v[4] = { '1', 0 }; CHECK(!df_env_flag(v, 0, 4, '1'), "unset (n = 0) is off even if the buffer says 1"); }

    CHECK(num("2000", 12, &r) && r == 2000, "2000");
    CHECK(num("0", 12, &r) && r == 0, "0");
    CHECK(!num("", 12, &r), "empty is not a number");
    CHECK(!num("12x", 12, &r), "12x is not a number");
    CHECK(!num("-5", 12, &r), "negative");
    CHECK(!num("1234567890", 12, &r), "10 digits refused (no overflow)");
    CHECK(num("123456789", 12, &r) && r == 123456789, "9 digits ok");
    CHECK(!num("12345678901234", 12, &r), "does not fit the buffer: refused");
    CHECK(!num("100", 3, &r), "does not fit: refused");

    { char v[8] = "clip"; CHECK(df_env_word(v, 4, 8, "clip"), "word clip"); CHECK(df_env_word("CLIP", 4, 8, "clip"), "case-insensitive");
      CHECK(!df_env_word(v, 9, 8, "clip"), "word that did not fit"); CHECK(!df_env_word(v, 0, 8, "clip"), "unset"); }

    // dynapro.dll by file name
    CHECK(df_is_dynapro("C:\\Program Files (x86)\\Dyna Pro\\dynapro.dll", lstrlenA("C:\\Program Files (x86)\\Dyna Pro\\dynapro.dll"), 260), "full path");
    CHECK(df_is_dynapro("C:\\x\\DYNAPRO.DLL", lstrlenA("C:\\x\\DYNAPRO.DLL"), 260), "case");
    CHECK(df_is_dynapro("dynapro.dll", lstrlenA("dynapro.dll"), 260), "bare name");
    CHECK(!df_is_dynapro("C:\\x\\xdynapro.dll", lstrlenA("C:\\x\\xdynapro.dll"), 260), "xdynapro.dll is another file");
    CHECK(!df_is_dynapro("C:\\x\\mydynapro.dll", lstrlenA("C:\\x\\mydynapro.dll"), 260), "prefix on the name");
    CHECK(!df_is_dynapro("C:\\x\\dynapro.dll.bak", lstrlenA("C:\\x\\dynapro.dll.bak"), 260), "other extension after");
    CHECK(!df_is_dynapro("C:\\x\\dynapro.dll", 0, 260), "call failed (0)");
    CHECK(!df_is_dynapro("C:\\x\\dynapro.dll", 16, 16), "path cut to the buffer size (n == size): not terminated, not matched");
    CHECK(!df_is_dynapro("xdynapro.dll", lstrlenA("xdynapro.dll"), 260), "bare xdynapro.dll");
    CHECK(df_is_dynapro("D:/tools/dynapro.dll", lstrlenA("D:/tools/dynapro.dll"), 260), "forward slash");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
