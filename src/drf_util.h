// Helpers of setup.c that package.c, driver.c and the fragments share: path join, process start, directory walk, the app key, the elevated stage.
#pragma once
#include "drf_app.h"
// shared helpers (setup.c): used by setup.c, package.c, driver.c
// out = "dir\name", when the result is short enough for a buffer of `limit` characters (it must be at least 2 characters shorter than limit: the old
// `lstrlenW(dir) + lstrlenW(name) + 2 >= limit` test); FALSE and out untouched otherwise. out may be dir itself (not name).
BOOL path_join(WCHAR *out, int limit, const WCHAR *dir, const WCHAR *name);
// Starts exe with the command line cmd (a writable buffer; creation flags `flags`, working folder cwd or NULL, window hidden when `hide`) and waits for it.
// PROC_EXITED: it ended, *code is its exit code (left alone if the code cannot be read); PROC_TIMEOUT: still running after ms (INFINITE never), it was
// terminated (exit code 1), *code is left alone; PROC_NOSTART: CreateProcess failed, GetLastError() is its error. Handles are not inherited.
enum { PROC_NOSTART = 0, PROC_EXITED = 1, PROC_TIMEOUT = 2 };
int run_process(const WCHAR *exe, WCHAR *cmd, const WCHAR *cwd, DWORD flags, BOOL hide, DWORD ms, DWORD *code);
// Calls fn(path, fd, ctx) for each entry of dir; names starting with '.' and entries that do not pass path_join(.., limit, ..) are skipped.
// path is "dir\name" in buf (the caller's buffer of bufcch characters, which first holds the pattern "dir\*"). fn may call dir_walk again for a subfolder,
// with a buffer of its own: that is how the walks recurse. fn returns FALSE to stop. WALK_DONE: all entries visited; WALK_STOPPED: fn stopped it;
// WALK_NOOPEN: the folder cannot be read (or the pattern does not fit in buf).
enum { WALK_DONE = 0, WALK_STOPPED = 1, WALK_NOOPEN = 2 };
typedef BOOL (*walk_fn)(const WCHAR *path, const WIN32_FIND_DATAW *fd, void *ctx);
int dir_walk(const WCHAR *dir, WCHAR *buf, int bufcch, int limit, walk_fn fn, void *ctx);
// HKCU\Software\DynaRunFix (APP_USER) or HKLM\SOFTWARE\DynaRunFix (APP_MACHINE): TRUE and *k open (KEY_ALL_ACCESS when created), else FALSE. The caller closes *k.
#define APP_USER FALSE
#define APP_MACHINE TRUE
BOOL app_key_open(BOOL machine, REGSAM sam, HKEY *k);
BOOL app_key_create(BOOL machine, HKEY *k);
// shared with driver.c
void delete_tree(const WCHAR *dir, const WCHAR *keep, int depth);   // a link is removed itself, never followed; in-use files go at the next restart
BOOL same_file(const WCHAR *a, const WCHAR *b);
int run_elevated(const WCHAR *args, const WCHAR *exe_path, const install_opts *o);   // "<exe or this program> <args>[ /quiet][ /close][ /pin][ /keep]" (from *o) with one UAC prompt; its exit code, RC_CANCEL = cancelled
BOOL valid_user_sid(const WCHAR *sid);      // a SID of a real account with a profile here
BOOL path_user_writable(const WCHAR *path, const WCHAR *sid);   // could ordinary users (or the user sid) change what is at path (or is that unknown)
BOOL enable_privilege(const WCHAR *name);
