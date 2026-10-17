// Shared declarations of DynaRunFix-Setup.exe, as one umbrella over the module headers (kept so that update.c, which includes only this, is unchanged).
// New code includes the module header it needs: drf_app.h (every module), drf_util.h, drf_package.h, drf_driver.h, drf_update.h, drf_wizard.h.
// The *_internal.h headers (used by the tests) are not part of it.
#pragma once
#include "drf_app.h"
#include "drf_util.h"
#include "drf_package.h"
#include "drf_driver.h"
#include "drf_update.h"
#include "drf_wizard.h"
