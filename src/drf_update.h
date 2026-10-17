// update.c: the update check and offer (run by the launcher).
#pragma once
#include "drf_app.h"

int update_check(void);                     // /checkupdate: notes a newer release in HKCU\Software\DynaRunFix; no UI
int update_offer(void);                       // /offer: the launcher started DynaRun; asks beside it, installs once it is closed
int update_run(const WCHAR *args);          // /update <launcher args>: asks, installs a noted release, starts the launcher
