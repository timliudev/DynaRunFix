// package.c declarations that only the tests (tests/test_driver.c, test_hardening.c, test_update.c) use besides package.c itself.
#pragma once
#include "drf_package.h"

BOOL pkg_url_ok(const WCHAR *u, const WCHAR *hosts);   // https and on one of the ';'-separated hosts (NULL = any)
BOOL pkg_redirect_target(const WCHAR *cur, const WCHAR *loc, const WCHAR *host, WCHAR *out, int cch);   // where a redirect from cur to loc goes, if allowed
int ver_compare(const WCHAR *a, const WCHAR *b);              // <0 / 0 / >0: a older / the same / newer than b (not versions: 0)
BOOL pkg_page_version(const char *html, DWORD n, WCHAR *out, int cch);   // the DynaRun V3 version the Dyna Pro software page names
// Every entry of the central directory, in order: fn(name as stored (not 0-terminated), its length, entry, ctx). fn returns FALSE to stop.
// PK_OK: all visited; PK_CANCEL: fn stopped it; PK_FORMAT: not a (supported, no Zip64) zip; PK_IO. e->data is valid (local header read).
typedef BOOL (*zip_visit_fn)(const char *name, DWORD nlen, const zipent *e, void *ctx);
int zip_walk(const WCHAR *zip, zip_visit_fn fn, void *ctx);
// An entry name that may be created below a folder: relative, '/' separators, no "..", ".", empty, "x:" / backslash / control or
// reserved characters, no device names, no trailing dot or space. *isdir: the name ends in '/'.
BOOL zip_name_safe(const char *name, DWORD nlen, BOOL *isdir);
BOOL pkg_prepare_target(const WCHAR *path);   // free a path that is going to be written (a plain file is removed, a link removed itself, a folder refused)
