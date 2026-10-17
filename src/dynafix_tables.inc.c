// dynafix_tables.inc.c - static data tables of dynafix.c: optional-feature names and registry values, swapped font faces, font-import modules.
// Included by dynafix.c (a single compilation unit, also pulled in by tests\logtest.c); not compiled on its own.

static const char *g_optname[N_OPT] = { "auto climate", "water cooler", "cooling fans", "AFR" };
static const char *g_optval[N_OPT][3] = {
    { "Auto_Climate_Enable", "Property_Auto_Climate", 0 },
    { "Water_Cooler_Enable", 0 },
    { "Cooling_Fan_Enable", 0 },
    { "Int_AF_Ratio_Enable", 0 },
};

// Faces that are swapped, with their line height (ascent + descent) in 1/1000 em.
static const struct { const WCHAR *face; short line; } g_swap[] = {
    { L"Arial", 1117 }, { L"Times New Roman", 1107 }, { L"MS Sans Serif", 1180 }, { L"Microsoft Sans Serif", 1150 },
    { L"MS Shell Dlg", 1150 }, { L"MS Shell Dlg 2", 1207 }, { L"MS Serif", 1150 }, { L"Tahoma", 1207 },
    { L"Verdana", 1215 }, { L"Segoe UI", 1330 },
    { L"\x65B0\x7D30\x660E\x9AD4", 1000 }, { L"\x7D30\x660E\x9AD4", 1000 },               // 新細明體 細明體
    { L"PMingLiU", 1000 }, { L"MingLiU", 1000 }, { L"Microsoft JhengHei", 1250 }, { L"Microsoft JhengHei UI", 1270 },
    { L"SimSun", 1000 }, { L"\x5B8B\x4F53", 1000 }, { L"NSimSun", 1000 },                  // 宋体
    { L"Microsoft YaHei", 1320 }, { L"Microsoft YaHei UI", 1320 },
    { L"MS UI Gothic", 1000 }, { L"MS PGothic", 1000 }, { L"\xFF2D\xFF33 \xFF30\x30B4\x30B7\x30C3\x30AF", 1000 },  // ＭＳ Ｐゴシック
    { L"Meiryo UI", 1300 }, { L"Yu Gothic UI", 1330 },
    { L"Gulim", 1000 }, { L"\xAD74\xB9BC", 1000 }, { L"Dotum", 1000 }, { L"\xB3CB\xC6C0", 1000 },  // 굴림 돋움
    { L"Malgun Gothic", 1330 }, { 0 }
};

// Modules whose font imports are redirected: those in DynaRun's folder, OCX controls wherever they are,
// and these. Modules load later (an OCX with the first form that uses it), so the module list is checked
// again whenever a window is created. Not this dll's folder (Locale Emulator's dlls are there).
static const char *g_fontmods[] = {
    "MSVBVM60.DLL", "ole32.dll", "oleaut32.dll", "MFC42.DLL", "comctl32.dll", "THBRes25.dll", "PEGRP32E.dll", 0
};
