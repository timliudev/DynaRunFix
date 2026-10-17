// dynafix_screen.inc.c - work area of a monitor and the patch of the VB6 runtime's screen size values.
// Included by dynafix.c (a single compilation unit, also pulled in by tests\logtest.c); not compiled on its own.

// Work area of monitor m (and its full rect), less 2 px on every edge that has an auto-hide taskbar.
static BOOL work_area(HMONITOR m, RECT *wa, RECT *mon)
{
    static SHAppBarMessage_t sab; static BOOL tried;
    MONITORINFO mi; APPBARDATA ab; UINT e;
    mi.cbSize = sizeof(mi);
    if (!m || !GetMonitorInfoA(m, &mi)) return FALSE;
    *wa = mi.rcWork; *mon = mi.rcMonitor;
    if (!tried) { HMODULE s = LoadLibraryA("shell32.dll"); tried = TRUE; if (s) sab = (SHAppBarMessage_t)GetProcAddress(s, "SHAppBarMessage"); }
    if (!sab) return TRUE;
    for (e = ABE_LEFT; e <= ABE_BOTTOM; e++) {
        HWND bar;
        SecureZeroMemory(&ab, sizeof(ab)); ab.cbSize = sizeof(ab); ab.uEdge = e; ab.rc = mi.rcMonitor;
        bar = (HWND)sab(ABM_GETAUTOHIDEBAREX, &ab);                   // Vista and later: per monitor
        if (!bar && (mi.dwFlags & MONITORINFOF_PRIMARY)) {            // XP: primary monitor only
            SecureZeroMemory(&ab, sizeof(ab)); ab.cbSize = sizeof(ab); ab.uEdge = e;
            bar = (HWND)sab(ABM_GETAUTOHIDEBAR, &ab);
        }
        if (!bar) continue;
        if (e == ABE_LEFT   && wa->left   - mon->left   < 2) wa->left   = mon->left + 2;
        if (e == ABE_TOP    && wa->top    - mon->top    < 2) wa->top    = mon->top + 2;
        if (e == ABE_RIGHT  && mon->right  - wa->right  < 2) wa->right  = mon->right - 2;
        if (e == ABE_BOTTOM && mon->bottom - wa->bottom < 2) wa->bottom = mon->bottom - 2;
    }
    return TRUE;
}

// This adjusts the VB6 runtime's in-memory screen size values (the ones behind Screen.Width / Height) to the primary monitor's
// work area, so that DynaRun lays its dashboard out for that area. The runtime (MSVBVM60.DLL, Microsoft's) fills them from the
// screen size at start-up, before dynafix is loaded. The values are located in the loaded runtime and changed only if they equal
// the real screen size; otherwise they are left alone. Nothing is written to disk.
typedef struct { BYTE *base; const RECT *wa; int scx, scy; } vbwalk_t;
static void patch_vb_screen_walk(BYTE *base, const RECT *wa, int scx, int scy);
static void __stdcall walk_vb(void *a) { vbwalk_t *w = (vbwalk_t *)a; patch_vb_screen_walk(w->base, w->wa, w->scx, w->scy); }

static void patch_vb_screen_walk(BYTE *base, const RECT *wa, int scx, int scy)
{
    IMAGE_NT_HEADERS *nt; IMAGE_SECTION_HEADER *sec; WORD i;
    if (!(nt = nt_of(base)) || nt->FileHeader.NumberOfSections > 96) return;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        BYTE *c, *e;
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (sec->Misc.VirtualSize < 24 || sec->VirtualAddress >= nt->OptionalHeader.SizeOfImage
            || sec->Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - sec->VirtualAddress) continue;
        c = base + sec->VirtualAddress;
        e = c + sec->Misc.VirtualSize - 24;
        for (; c < e; c++) {
            int *px, *py; DWORD old;
            if (c[0] != 0x6A || c[1] != 0x00 || c[2] != 0xA3 || c[7] != 0xFF || c[9] != 0x6A || c[10] != 0x01 || c[11] != 0xA3
                || c[16] != 0xFF || c[18] != 0x6A || c[19] != 0x20 || c[20] != 0xA3) continue;
            px = *(int **)(c + 12); py = *(int **)(c + 21);
            if ((BYTE *)px < base || (BYTE *)px >= base + nt->OptionalHeader.SizeOfImage
                || (BYTE *)py < base || (BYTE *)py >= base + nt->OptionalHeader.SizeOfImage) continue;
            if (*px != scx || *py != scy) {
                logf("VB's screen size %ux%u is not the screen size, left alone %u\r\n", *px, *py, 0);
                return;
            }
            VirtualProtect(px, sizeof(int), PAGE_READWRITE, &old); *px = wa->right - wa->left; VirtualProtect(px, sizeof(int), old, &old);
            VirtualProtect(py, sizeof(int), PAGE_READWRITE, &old); *py = wa->bottom - wa->top; VirtualProtect(py, sizeof(int), old, &old);
            {
                char t[60]; wsprintfA(t, "%ux%u -> %ux%u", scx, scy, *px, *py);
                logf("screen-size values adjusted (work area): %s %u %u\r\n", (DWORD)(UINT_PTR)t, 0, 0);
            }
            return;
        }
    }
    logf("VB's screen size not found in MSVBVM60.DLL %u %u %u\r\n", 0, 0, 0);
}
