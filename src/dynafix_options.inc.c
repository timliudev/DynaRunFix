// dynafix_options.inc.c - reading the optional features picked on the first-start system selection, and switching them on.
// Included by dynafix.c (a single compilation unit, also pulled in by tests\logtest.c); not compiled on its own.

typedef struct { HWND form, frame; RECT fr; int n; HWND btn[8]; } sel_t;

static void utf8_text(HWND h, char *a, int size)
{
    WCHAR w[60];
    w[0] = 0; GetWindowTextW(h, w, 60);
    w2u(a, size, w);
}

// The options frame is the lowest frame in the right half of the form.
static BOOL CALLBACK find_frame(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l; char cls[40]; RECT r, f;
    if (!GetClassNameA(h, cls, sizeof(cls)) || lstrcmpA(cls, "ThunderRT6Frame") || !IsWindowVisible(h)) return TRUE;
    GetWindowRect(h, &r); GetWindowRect(s->form, &f);
    if ((r.left + r.right) / 2 > (f.left + f.right) / 2 && (!s->frame || r.top > s->fr.top)) { s->frame = h; s->fr = r; }
    return TRUE;
}

// The buttons are the controls lying inside the frame: not all of them are its children (the cooling fan
// button is not), so all controls of the form are checked by position.
static BOOL CALLBACK find_buttons(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l; RECT r, q; POINT c; char cls[40]; int i;
    if (s->n >= 8 || !IsWindowVisible(h) || !GetClassNameA(h, cls, sizeof(cls)) || !lstrcmpA(cls, "ThunderRT6Frame")) return TRUE;
    GetWindowRect(h, &r);
    c.x = (r.left + r.right) / 2; c.y = (r.top + r.bottom) / 2;
    if (!PtInRect(&s->fr, c)) return TRUE;
    for (i = s->n++; i > 0; i--) {             // keep them sorted top to bottom
        GetWindowRect(s->btn[i - 1], &q);
        if (q.top <= r.top) break;
        s->btn[i] = s->btn[i - 1];
    }
    s->btn[i] = h;
    return TRUE;
}

static BOOL CALLBACK find_form(HWND h, LPARAM l)
{
    sel_t *s = (sel_t *)l;
    if (!IsWindowVisible(h)) return TRUE;
    s->form = h; s->frame = NULL;
    EnumChildWindows(h, find_frame, l);
    return !s->frame;                          // stop at the first form with a frame
}

// A sampled colour is "picked" (green), "plain" (no green to speak of) or in neither class (anything in between: a theme, high
// contrast or scaling made the colour unclear).
#define CLS_PICKED 1
#define CLS_PLAIN  0
#define CLS_UNSURE (-1)
static int colour_class(COLORREF c)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c), m = r > b ? r : b;
    if (g >= 160 && r < 100 && b < 100) return CLS_PICKED;
    if (g - m < 40) return CLS_PLAIN;      // grey, white, black, red, blue...: no green dominance
    return CLS_UNSURE;
}

// Which optional features are drawn as picked (green). Returns FALSE on any doubt (group not found, wrong number of buttons, a
// colour that is clearly neither class, samples of one button that disagree, high-contrast mode): the caller then writes nothing.
static BOOL read_options(BOOL *on)
{
    typedef COLORREF (WINAPI *gp_t)(HDC, int, int);
    gp_t gp = (gp_t)GetProcAddress(GetModuleHandleA("gdi32.dll"), "GetPixel");
    sel_t s; int i, k = 0; HIGHCONTRASTA hc;
    { volatile char *z = (volatile char *)&s; for (i = 0; i < (int)sizeof(s); i++) z[i] = 0; }
    EnumThreadWindows(GetCurrentThreadId(), find_form, (LPARAM)&s);
    if (!s.frame) { logf("optional features: selection form not found %u %u %u\r\n", 0, 0, 0); return FALSE; }
    hc.cbSize = sizeof(hc); hc.dwFlags = 0;
    if (SystemParametersInfoA(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
        logf("optional features: high-contrast mode is on, colours cannot be trusted %u %u %u\r\n", 0, 0, 0);
        return FALSE;
    }
    EnumChildWindows(s.form, find_buttons, (LPARAM)&s);
    { char a[150]; utf8_text(s.frame, a, sizeof(a)); logf("optional features group '%s' has %u controls %u\r\n", (DWORD)(UINT_PTR)a, s.n, 0); }
    for (i = 0; i < s.n; i++) {
        char cls[40], a[150]; RECT r; HDC dc; COLORREF c = CLR_INVALID, c2; BOOL g; int k1, k2, k3;
        GetClassNameA(s.btn[i], cls, sizeof(cls)); utf8_text(s.btn[i], a, sizeof(a));
        GetClientRect(s.btn[i], &r);
        k1 = k2 = k3 = CLS_UNSURE;
        if (gp && r.right >= 14 && r.bottom >= 9 && (dc = GetDC(s.btn[i]))) {
            c = gp(dc, 6, r.bottom / 2);
            // two more samples at other heights of the same column: all three must tell the same story
            c2 = gp(dc, 6, r.bottom / 4); if (c2 != CLR_INVALID) k2 = colour_class(c2);
            c2 = gp(dc, 6, r.bottom * 3 / 4); if (c2 != CLR_INVALID) k3 = colour_class(c2);
            ReleaseDC(s.btn[i], dc);
        }
        logf("  %s '%s' colour %06X", (DWORD)(UINT_PTR)cls, (DWORD)(UINT_PTR)a, c);
        // Controls of other groups (EB-150 / EB-250 engine dynamometers) also lie in this area, hidden
        // (clipped away): nothing of them can be read, so they are skipped.
        if (c == CLR_INVALID) { logf(" -> hidden, skipped %u %u %u\r\n", 0, 0, 0); continue; }
        k1 = colour_class(c);
        if (k1 == CLS_UNSURE || k1 != k2 || k1 != k3) {
            logf(" -> unclear (samples %d/%d/%d)\r\n", (DWORD)k1, (DWORD)k2, (DWORD)k3);
            logf("optional features: a button colour is not clearly picked or not picked %u %u %u\r\n", 0, 0, 0);
            return FALSE;
        }
        g = k1 == CLS_PICKED;
        if (k < N_OPT) on[k] = g;
        logf(" -> %s %s %u\r\n", (DWORD)(UINT_PTR)(k < N_OPT ? g_optname[k] : "?"), (DWORD)(UINT_PTR)(g ? "picked" : "not picked"), 0);
        k++;
    }
    if (k != N_OPT) { logf("optional features: expected %u buttons, found %u %u\r\n", N_OPT, k, 0); return FALSE; }
    return TRUE;
}

static void enable_options(const BOOL *on)
{
    HKEY k; int i, j;
    for (i = 0; i < N_OPT && !on[i]; i++);
    if (i == N_OPT) return;                    // nothing picked (or nothing readable): nothing is written
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SETUP_KEY, 0, KEY_SET_VALUE, &k)) {
        logf("optional features: cannot open System_Setup %u %u %u\r\n", 0, 0, 0);
        return;
    }
    for (i = 0; i < N_OPT; i++)
        for (j = 0; on[i] && g_optval[i][j]; j++)
            logf("enabled %s: System_Setup\\%s = \"-1\" (%s)\r\n", (DWORD)(UINT_PTR)g_optname[i], (DWORD)(UINT_PTR)g_optval[i][j],
                 (DWORD)(UINT_PTR)(RegSetValueExA(k, g_optval[i][j], 0, REG_SZ, (const BYTE *)"-1", 3) ? "failed" : "ok"));
    RegCloseKey(k);
}
