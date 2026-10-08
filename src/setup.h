// Shared declarations of DynaRunFix-Setup.exe (setup.c, wizard.c, package.c).
#include <windows.h>

extern BOOL g_zh, g_quiet, g_close;         // g_close: close a running DynaRun before installing (/close)
extern BOOL g_pin, g_autostart;                         // pin DynaRun to the taskbar / start it at sign-in (default on; /notaskbar, /noautostart)
extern BOOL g_keep;                                     // /keep: leave the pin, the sign-in start and the desktop shortcut as they are
extern int g_pinresult;                                 // after the install: 0 not asked, 1 pinned, 2 appears after the next sign-in, 3 could not pin
extern HWND g_hwnd;                                     // wizard window, owner of UAC prompts
extern WCHAR g_self[MAX_PATH], g_dir[MAX_PATH], g_launcher[MAX_PATH], g_exe[MAX_PATH];
#define T(en, zh) (g_zh ? (zh) : (en))

/* setup.c */
void zero(void *p, SIZE_T n);
void *alloc(SIZE_T n);
void release(void *p);
BOOL exists(const WCHAR *p);
int msg(const WCHAR *text, UINT flags);
BOOL is_admin(void);
DWORD os_build(void);                       // Windows build number (real, not the compatibility-shimmed one)
BOOL user_sid(WCHAR *out, int cch);
BOOL locate_dynarun(void);                  // fills g_exe from the registry or the default folders; no UI
void start_dynarun(void);                  // the wizard's "Start DynaRun": through our shortcut, like a double click
int install_fix(void);                      // DynaRun is installed: elevated stage + user shortcuts. 0 ok, 2 cancelled
int install_all(const WCHAR *msi);          // runs the DynaRun MSI, then install_fix's work. 0 ok, 2 cancelled
#define RC_RUNNING 6                        // exit code: DynaRun (or the launcher / LEProc) still runs, nothing was changed
enum { RUN_DYNARUN = 1, RUN_LAUNCHER = 2, RUN_LEPROC = 4 };
int running_programs(void);                 // RUN_* bits of the programs that hold files the setup replaces

/* update.c */
int update_check(void);                     // /checkupdate: notes a newer release in HKCU\Software\DynaRunFix; no UI
int update_run(const WCHAR *args);          // /update <launcher args>: asks, installs a noted release, starts the launcher

/* wizard.c */
int wizard(void);

/* package.c */
#define DYNAPRO_PAGE L"https://dynapro.co.uk/Software_Release.htm"
#define DYNAPRO_ZIP  L"https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20Dynamometers.zip"
typedef void (*progress_fn)(DWORD done, DWORD total);
enum { PK_OK, PK_CANCEL, PK_NET, PK_HTTP, PK_IO, PK_FORMAT, PK_PASSWORD };

typedef struct {
    WCHAR path[MAX_PATH];
    DWORD data;                             // offset of the (encrypted) entry data
    DWORD csize, usize, crc, flags, method, dostime;
} zipent;

BOOL pkg_find_local(WCHAR *out);            // a DynaRun MSI, or a zip with an .msi inside, in Downloads/Desktop/Documents or next to us
void pkg_reject(const WCHAR *path);         // not the DynaRun setup after all: pkg_find_local skips it
int pkg_download(WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
int pkg_fetch(const WCHAR *url, const WCHAR *name, const WCHAR *etagval, WCHAR *out, progress_fn cb, volatile LONG *cancel, DWORD *err);
int zip_open(const WCHAR *zip, zipent *e);  // first *.msi entry
BOOL zip_password_plausible(const zipent *e, const char *pw);
int zip_extract(const zipent *e, const char *pw, const WCHAR *out, progress_fn cb, volatile LONG *cancel);
char *msi_license_rtf(const WCHAR *msi);    // RTF of the setup's license page (heap) or NULL
enum { MSI_NONE, MSI_OTHER, MSI_DYNARUN };
int msi_check(const WCHAR *msi, WCHAR *version, int cch);   // MSI_NONE: not an MSI at all. DynaRun: by UpgradeCode
int run_msiexec(const WCHAR *msi);          // msiexec exit code
BOOL dynarun_product(WCHAR *out);           // product code of the installed DynaRun V3 (39 chars)
int remove_dynarun(void);                   // Dyna Pro's uninstall of DynaRun V3; msiexec exit code, -1 none
void pkg_temp_dir(WCHAR *out);              // %TEMP%\DynaRunFix (created)
int pkg_prepare_documents(const WCHAR *msi);   // download OneDrive "online-only" copies the setup overwrites
