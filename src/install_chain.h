// install_chain.h - which programs' windows belong to the install chain the wizard stays behind (keep_up in src\wizard\layout.h).
//
// Pure helper (Win32 string API only, no state, no CRT) so tests\test_shexec_match.c can compile it standalone.
#ifndef INSTALL_CHAIN_H
#define INSTALL_CHAIN_H
#include <windows.h>

// n: the image name (no path) of the process that owns a window. The programs of the chain: the UAC prompt (consent.exe), Dyna Pro's setup (msiexec.exe, whose
// progress window says "Please wait while Windows configures ..."), the driver install (drvinst.exe / pnputil.exe / dpinst.exe: the red Windows Security warning)
// and the elevated stage of this setup itself (its message boxes): DynaRunFix-Setup.exe, or the copy made for a network drive, DynaRunFix-Setup-<n>.exe.
// The names are compared whole (any case); the setup by its prefix "DynaRunFix-Setup" (the copy for a network drive adds -<n>.exe). A name shorter than the
// prefix never matches, and nothing is read past its end.
static BOOL in_install_chain(const WCHAR *n)
{
    static const WCHAR *const names[] = { L"msiexec.exe", L"consent.exe", L"drvinst.exe", L"pnputil.exe", L"dpinst.exe", NULL };
    static const WCHAR setup[] = L"DynaRunFix-Setup";
    const int plen = (int)(sizeof(setup) / sizeof(setup[0])) - 1;
    int i;
    if (!n) return FALSE;
    for (i = 0; names[i]; i++) if (!lstrcmpiW(n, names[i])) return TRUE;
    return lstrlenW(n) >= plen && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, n, plen, setup, plen) == CSTR_EQUAL;
}

#endif
