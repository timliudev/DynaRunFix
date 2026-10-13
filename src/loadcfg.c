// Load configuration for the exes (DynaRunFix.exe, DynaRunFix-Setup.exe), which are built without the C runtime.
//
// build.cmd links both with /DEPENDENTLOADFLAG:0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32: the DLLs the exe imports are loaded from the
// system folder only). The linker writes that flag into the image's load configuration; the C runtime normally supplies the symbol
// (_load_config_used), so without it the flag would not be applied (warning LNK4266). All other fields stay zero: no security
// cookie (the code is built with /GS-), no control-flow-guard tables.
#include <windows.h>

const IMAGE_LOAD_CONFIG_DIRECTORY32 _load_config_used = { sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32) };

// delayimp.lib (the delay-load helper of the installer) is built with control-flow guard checks and calls through this pointer;
// without the C runtime nothing defines it. A function that checks nothing.
static void __fastcall guard_check_nop(void *target) { (void)target; }
void *__guard_check_icall_fptr = (void *)guard_check_nop;
