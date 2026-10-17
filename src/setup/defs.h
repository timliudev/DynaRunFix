// Part of setup.c (shared constants, globals, trust_ctx): included by it, in one compilation unit; never compiled on its own.
#ifndef SLDF_RUNAS_USER
#define SLDF_RUNAS_USER 0x2000
#endif
#define WOW KEY_WOW64_64KEY       // DynaRun's COM keys: physical view, like 64-bit reg.exe sees them
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

static const WCHAR APPKEY[] = L"SOFTWARE\\DynaRunFix";
static const WCHAR USERKEY[] = L"Software\\DynaRunFix";
static const WCHAR RUNKEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR POLKEY[] = L"Software\\Policies\\Microsoft\\Windows\\Explorer";
static const WCHAR APPID[] = L"DynaRunFix.DynaRunV3";           // AppUserModelID of the launcher shortcuts and of DynaRun (dynafix.c)
static const PROPERTYKEY PKEY_AUMID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
// technique described by Gee Law (geelaw.blog/entries/msedge-pins/), also used by Firefox's PinToTaskbar plugin (MPL 2.0): calling the
// undocumented IPinnedList3::Modify of Explorer's taskband. The GUIDs and the vtable layout are facts of that interface; the code is
// our own (compared with Firefox's PinToTaskbar.cpp: different structure and wording, nothing copied).
static const GUID CLSID_TaskbandPin = { 0x90aa3a4e, 0x1cba, 0x4233, { 0xb8, 0xbb, 0x53, 0x57, 0x73, 0xd4, 0x84, 0x49 } };
static const GUID IID_IPinnedList3 = { 0x0dd79ae2, 0xd156, 0x45d4, { 0x9e, 0xeb, 0x3b, 0x54, 0x97, 0x69, 0xe9, 0x40 } };
typedef struct IPinnedList3 { const struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(struct IPinnedList3 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(struct IPinnedList3 *);
    ULONG (STDMETHODCALLTYPE *Release)(struct IPinnedList3 *);
    void *others[13];
    HRESULT (STDMETHODCALLTYPE *Modify)(struct IPinnedList3 *, PCIDLIST_ABSOLUTE unpin, PCIDLIST_ABSOLUTE pin, int caller);
} *lpVtbl; } IPinnedList3;
static const WCHAR UNINSTKEY[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DynaRunFix";
static const WCHAR DYNARUN_REL[] = L"\\Dyna Pro Dynamometers\\DynaRun V3.exe";

BOOL g_zh;
int g_drvmode;                    // DRV_MODE_*: the wizard starts at the driver pages (driver.c, wizard.c)
HWND g_hwnd;
WCHAR g_self[MAX_PATH], g_dir[MAX_PATH], g_launcher[MAX_PATH], g_exe[MAX_PATH];
// What the ACL decisions of a stage depend on, passed to the functions that decide (never kept in a global): each stage makes one.
typedef struct {
    WCHAR user_sid[200];          // the user who started the setup (elevated stage: from its command line); "" = none known
    BOOL exe_trusted;             // set by check_exe: DynaRun's exe and folder cannot be changed by ordinary users
} trust_ctx;
#define TITLE T(L"DynaRunFix Setup", L"DynaRunFix 安裝程式")

