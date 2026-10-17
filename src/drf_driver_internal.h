// driver.c declarations that only tests/test_driver.c uses besides driver.c itself (pure helpers and the pinned file table).
#pragma once
#include "drf_driver.h"

int drv_file_count(void);                   // the files that get installed (pinned: name below "Dyna Pro USB\", size, SHA-256)
int drv_file_find(const WCHAR *rel);        // index of that name (any case, / or \), or -1
BOOL drv_file_info(int i, const WCHAR **rel, DWORD *size, const WCHAR **sha);
BOOL drv_file_match(int i, DWORD size, const WCHAR *sha);   // is a file of this size and SHA-256 the pinned file i?
int drv_check_dir(const WCHAR *dir, int *differ, int *missing);   // how many pinned files below dir\Dyna Pro USB\ are right; the others differ / are missing
BOOL drv_inf_matches(const BYTE *data, DWORD n);          // pure helpers, unit-tested (tests\test_driver.c)
BOOL drv_ci_enforced(DWORD code_integrity_options);
BOOL drv_bde_suspend(int protection_status, int conversion_status, int key_protectors);   // BDE_PS_* (driver.c), conversion status and protector count (-1 unknown): suspend BitLocker? A volume mid-conversion with protectors is suspended although it reads "off"
BOOL drv_bde_suspend_now(int protection_status, int conversion_status, int key_protectors, BOOL suspended_by_us);   // ... at prep time: also when our own flag says we suspended it earlier (a retry)
int drv_task_xml(WCHAR *out, int cch, const WCHAR *sid, const WCHAR *exe);
BOOL drv_sys_tool(const WCHAR *windir, BOOL wow64, const WCHAR *tool, WCHAR *out, int cch);
BOOL drv_pnputil_ok(DWORD code);
BOOL drv_pnputil_args(WCHAR *out, int cch, const WCHAR *inf);
