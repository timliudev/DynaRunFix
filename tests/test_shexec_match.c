// Tests for setup_helper_match (src\setup_match.h). Build and run with tests\build-tests.cmd.
// (The exe name must not contain "setup": Windows installer detection would demand elevation.)
#include <stdio.h>
#include "../src/setup_match.h"
#include "../src/install_chain.h"

static int g_fail, g_run;

static const char *DIRS[2] = {
    "C:\\Program Files (x86)\\Dyna Pro Dynamometers\\System Data",
    "C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data",
};
#define APP "C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data\\"
#define PF  "C:\\Program Files (x86)\\Dyna Pro Dynamometers\\System Data\\"

static void check(const char *file, BOOL want)
{
    BOOL got = setup_helper_match(file, DIRS, 2);
    g_run++;
    if (got != want) { g_fail++; printf("FAIL: %-90s want %d got %d\n", file ? file : "(null)", want, got); }
}

int main(void)
{
    // genuine
    check(APP "Setup_114.exe", TRUE);
    check(APP "Setup_103.exe", TRUE);
    check(PF "Setup_1.exe", TRUE);
    check(APP "Setup_0000123456.exe", TRUE);
    // case, slashes, quotes, dot segments, trailing separators in the folder
    check(APP "SETUP_114.EXE", TRUE);
    check(APP "setup_114.Exe", TRUE);
    check("c:\\users\\U\\appdata\\roaming\\dyna pro dynamometers\\dyna run v3\\system data\\Setup_114.exe", TRUE);
    check("C:/Users/u/AppData/Roaming/Dyna Pro Dynamometers/Dyna Run V3/System Data/Setup_114.exe", TRUE);
    check("\"" APP "Setup_114.exe\"", TRUE);
    check("  \"" APP "Setup_114.exe\"  ", TRUE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\.\\System Data\\Setup_114.exe", TRUE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\x\\..\\System Data\\Setup_114.exe", TRUE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\\\System Data\\Setup_114.exe", TRUE);
    // wrong leaf
    check(APP "Setup_.exe", FALSE);
    check(APP "Setup_12a.exe", FALSE);
    check(APP "Setup_a12.exe", FALSE);
    check(APP "Setup_123.exe.txt", FALSE);
    check(APP "Setup_123.exe ", TRUE);                       // trailing blank: Windows strips it too
    check(APP "Setup_123.txt", FALSE);
    check(APP "Setup_123", FALSE);
    check(APP "Setup_123.exe.exe", FALSE);
    check(APP "Setup_Guide.pdf", FALSE);
    check(APP "Setup.exe", FALSE);
    check(APP "Setup_ 1.exe", FALSE);
    check(APP "xSetup_1.exe", FALSE);
    check(APP "Setup_1\\x.exe", FALSE);
    check(APP, FALSE);
    // wrong folder
    check("C:\\Setup_123.exe", FALSE);
    check("C:\\Users\\u\\Downloads\\Setup_123.exe", FALSE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\Setup_123.exe", FALSE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data\\sub\\Setup_123.exe", FALSE);
    check("C:\\x\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data\\Setup_123.exe", FALSE);
    check("C:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data2\\Setup_123.exe", FALSE);
    check("D:\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data\\Setup_123.exe", FALSE);
    check("C:\\Program Files (x86)\\Dyna Pro Dynamometers\\Setup_123.exe", FALSE);
    // a directory named Setup_1 in the path
    check(APP "Setup_1\\Setup_114.exe", FALSE);
    check(APP "Setup_1\\readme.txt", FALSE);
    check("C:\\Setup_1\\System Data\\Setup_114.exe", FALSE);
    check("C:\\Setup_1\\", FALSE);
    check("C:\\Setup_1", FALSE);
    // relative / odd forms
    check("Setup_114.exe", FALSE);
    check("System Data\\Setup_114.exe", FALSE);
    check(".\\Setup_114.exe", FALSE);
    check("..\\System Data\\Setup_114.exe", FALSE);
    check("\\Users\\u\\AppData\\Roaming\\Dyna Pro Dynamometers\\Dyna Run V3\\System Data\\Setup_114.exe", FALSE);
    check("C:Setup_114.exe", FALSE);
    check("\\\\server\\share\\Dyna Run V3\\System Data\\Setup_114.exe", FALSE);
    check("\\\\?\\" APP "Setup_114.exe", FALSE);
    check("\"" APP "Setup_114.exe", FALSE);                  // unbalanced quote
    check("\"" APP "Setup_114.exe\" /x", FALSE);
    check(APP "Setup_114.exe\"", FALSE);
    check(APP "Setup_*.exe", FALSE);
    check("https://example.com/Setup_114.exe", FALSE);
    check("", FALSE);
    check("   ", FALSE);
    check(NULL, FALSE);
    // no folder given
    g_run++;
    if (setup_helper_match(APP "Setup_114.exe", NULL, 0)) { g_fail++; puts("FAIL: no folders"); }
    {   // NULL / junk entries are skipped, a later entry still matches
        const char *d[3] = { NULL, "relative", DIRS[1] };
        g_run++;
        if (!setup_helper_match(APP "Setup_114.exe", d, 3)) { g_fail++; puts("FAIL: skipped entries"); }
    }
    {   // drive-root folder
        const char *d[1] = { "C:\\" };
        g_run++;
        if (!setup_helper_match("C:\\Setup_5.exe", d, 1) || setup_helper_match("C:\\x\\Setup_5.exe", d, 1)) { g_fail++; puts("FAIL: root folder"); }
    }
    {   // in_install_chain (src\install_chain.h): the programs whose windows the wizard stays behind while the elevated part works
        static const struct { const WCHAR *name; BOOL want; } t[] = {
            { L"msiexec.exe", TRUE }, { L"MSIEXEC.EXE", TRUE }, { L"consent.exe", TRUE }, { L"drvinst.exe", TRUE }, { L"pnputil.exe", TRUE }, { L"dpinst.exe", TRUE },
            { L"DynaRunFix-Setup.exe", TRUE }, { L"dynarunfix-setup.exe", TRUE }, { L"DynaRunFix-Setup-1234-5678-9.exe", TRUE }, { L"DynaRunFix-Setup", TRUE },
            { L"explorer.exe", FALSE }, { L"chrome.exe", FALSE }, { L"", FALSE }, { L"a", FALSE },
            { L"msiexec", FALSE }, { L"msiexec.exe.bak", FALSE }, { L"xmsiexec.exe", FALSE }, { L"consent.exe ", FALSE },
            { L"DynaRunFix-Setu", FALSE }, { L"DynaRunFix-Setu.exe", FALSE }, { L"DynaRunFix.exe", FALSE }, { L"xDynaRunFix-Setup.exe", FALSE },
            { L"DynaRunFix-Launcher.exe", FALSE }, { L"DynaRun V3.exe", FALSE },
        };
        int i;
        for (i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++) {
            g_run++;
            if (in_install_chain(t[i].name) != t[i].want) { g_fail++; printf("FAIL: in_install_chain(%ls) want %d\n", t[i].name, t[i].want); }
        }
        g_run++;
        if (in_install_chain(NULL)) { g_fail++; puts("FAIL: in_install_chain(NULL)"); }
    }
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
