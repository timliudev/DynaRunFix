// dynafix_hint.inc.c - the small hint windows (device in use, USB call stuck, restart failed) and the Restart Manager lookup of who holds the device.
// Included by dynafix.c (a single compilation unit, also pulled in by tests\logtest.c); not compiled on its own.

typedef DWORD (WINAPI *RmStart_t)(DWORD *, DWORD, WCHAR *);
typedef DWORD (WINAPI *RmReg_t)(DWORD, UINT, LPCWSTR *, UINT, RM_UNIQUE_PROCESS *, UINT, LPCWSTR *);
typedef DWORD (WINAPI *RmGet_t)(DWORD, UINT *, UINT *, RM_PROCESS_INFO *, LPDWORD);
typedef DWORD (WINAPI *RmEnd_t)(DWORD);

// Who holds the file/device `path`? Restart Manager answers for files; for device paths it may know nothing.
static BOOL find_holder(const char *path, WCHAR *name, int n, DWORD *pid)
{
    HMODULE rm = load_sys("rstrtmgr.dll"); BOOL found = FALSE;
    RmStart_t st; RmReg_t reg; RmGet_t get; RmEnd_t end;
    if (!rm) return FALSE;
    st = (RmStart_t)GetProcAddress(rm, "RmStartSession"); reg = (RmReg_t)GetProcAddress(rm, "RmRegisterResources");
    get = (RmGet_t)GetProcAddress(rm, "RmGetList"); end = (RmEnd_t)GetProcAddress(rm, "RmEndSession");
    if (st && reg && get && end) {
        DWORD ses; WCHAR key[CCH_RM_SESSION_KEY + 1]; WCHAR wp[MAX_PATH]; LPCWSTR files[1];
        MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
        files[0] = wp;
        if (!st(&ses, 0, key)) {
            RM_PROCESS_INFO *pi = (RM_PROCESS_INFO *)HeapAlloc(GetProcessHeap(), 0, 8 * sizeof(*pi));
            if (pi && !reg(ses, 1, files, 0, NULL, 0, NULL)) {
                UINT need = 0, cnt = 8; DWORD reasons = 0;
                if (!get(ses, &need, &cnt, pi, &reasons) && cnt > 0) {
                    lstrcpynW(name, pi[0].strAppName, n);
                    *pid = pi[0].Process.dwProcessId;
                    found = TRUE;
                }
            }
            if (pi) HeapFree(GetProcessHeap(), 0, pi);
            end(ses);
        }
    }
    FreeLibrary(rm);
    return found;
}

static LRESULT CALLBACK HintProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    hintwin_t *d;
    if (m == WM_NCCREATE) SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA *)l)->lpCreateParams);
    d = (hintwin_t *)GetWindowLongPtrA(h, GWLP_USERDATA);
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: case WM_TIMER: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; HGDIOBJ o;
        GetClientRect(h, &r);
        FillRect(dc, &r, GetSysColorBrush(COLOR_INFOBK));
        if (d) {
            o = SelectObject(dc, d->font);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
            InflateRect(&r, -12, -10);
            DrawTextW(dc, d->text, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, o);
        }
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcA(h, m, w, l);
}

static void show_hint(const WCHAR *text)
{
    static const char cls[] = "dynafix.hint";
    WNDCLASSA wc; hintwin_t d; HWND h; HDC dc; RECT r, wa; MSG msg; NONCLIENTMETRICSW ncm; HGDIOBJ o;
    SecureZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = HintProc; wc.hInstance = g_self; wc.lpszClassName = cls; wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    lstrcpynW(d.text, text, 320);
    SecureZeroMemory(&ncm, sizeof(ncm)); ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        ncm.cbSize -= sizeof(int);   // before Vista: no iPaddedBorderWidth
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) ncm.lfMessageFont.lfHeight = -12;
    }
    d.font = CreateFontIndirectW(&ncm.lfMessageFont);
    dc = GetDC(NULL);
    o = SelectObject(dc, d.font);
    r.left = r.top = 0; r.right = 380 * GetDeviceCaps(dc, LOGPIXELSX) / 96; r.bottom = 0;
    DrawTextW(dc, d.text, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, o);
    ReleaseDC(NULL, dc);
    r.right += 24; r.bottom += 20;
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) { wa.left = wa.top = 0; wa.right = GetSystemMetrics(SM_CXSCREEN); wa.bottom = GetSystemMetrics(SM_CYSCREEN); }
    h = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "DynaRunFix", WS_POPUP | WS_BORDER,
                        wa.right - r.right - 18, wa.bottom - r.bottom - 18, r.right + 2, r.bottom + 2, NULL, NULL, g_self, &d);
    if (h) {
        SetTimer(h, 1, 10000, NULL);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    if (d.font) DeleteObject(d.font);
    UnregisterClassA(cls, g_self);
}

static DWORD WINAPI HintThread(LPVOID p)
{
    hintreq_t *q = (hintreq_t *)p; WCHAR t[320], who[128]; DWORD pid = 0; BOOL zh = zh_ui();
    if (q->kind == 1) {
        BOOL known = find_holder(q->path, who, 128, &pid);
        if (known) {
            char a[128];
            WideCharToMultiByte(CP_ACP, 0, who, -1, a, sizeof(a), NULL, NULL);
            logf("device holder: pid %u, %s %u\r\n", pid, (DWORD)(UINT_PTR)a, 0);
        } else logf("device holder: not found (Restart Manager does not know this device) %u %u %u\r\n", 0, 0, 0);
        if (!g_hints) goto done;
        if (known) wsprintfW(t, zh ? L"Dyna Pro 裝置正被其他程式使用(%s,PID %u)。\n關閉該程式後,DynaRun 會自動重新連線。"
                                   : L"The Dyna Pro device is in use by another program (%s, PID %u).\nClose it and DynaRun will reconnect.", who, pid);
        else lstrcpyW(t, zh ? L"Dyna Pro 裝置正被其他程式使用。\n關閉該程式後,DynaRun 會自動重新連線。"
                            : L"The Dyna Pro device is in use by another program.\nClose it and DynaRun will reconnect.");
    } else if (q->kind == 3) {
        if (!g_hints) goto done;
        lstrcpyW(t, zh ? L"與 Dyna Pro 的 USB 資料呼叫長時間沒有回應,DynaRun 的資料寫入已被略過。\n請關閉並重新啟動 DynaRun。"
                        : L"A USB data call to the Dyna Pro has not returned for a long time, so DynaRun's data writes are being skipped.\nPlease close and restart DynaRun.");
    } else if (q->kind == 4) {
        lstrcpyW(t, zh ? L"無法自動重新啟動 DynaRun。\n請關閉後再手動啟動 DynaRun,以載入新的設定。"
                        : L"DynaRun could not be restarted automatically.\nPlease close it and start it again to load the new configuration.");
    } else {
        if (!g_hints) goto done;
        lstrcpyW(t, zh ? L"無法連線到 Dyna Pro。\n請檢查 USB 線與裝置電源。"
                        : L"Cannot connect to the Dyna Pro.\nCheck the USB cable and the device's power.");
    }
    show_hint(t);
done:
    HeapFree(GetProcessHeap(), 0, q);
    InterlockedExchange(&g_hintbusy, 0);
    return 0;
}

// one hint at a time; never on the caller's thread
static void start_hint(int kind, DWORD err, const char *path)
{
    hintreq_t *q; HANDLE t;
    if (InterlockedCompareExchange(&g_hintbusy, 1, 0)) return;
    q = (hintreq_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (q) { q->kind = kind; q->err = err; if (path) lstrcpynA(q->path, path, MAX_PATH); }
    t = q ? CreateThread(NULL, 0, HintThread, q, 0, NULL) : NULL;
    if (t) CloseHandle(t);
    else { if (q) HeapFree(GetProcessHeap(), 0, q); InterlockedExchange(&g_hintbusy, 0); }
}
