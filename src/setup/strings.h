// Every text the setup (not the wizard: src\wizard\strings.h) shows in English and Chinese, in one table. The code says TS(ST_...) where it used to say
// T(L"English", L"中文"). The same pair is in the table once. A new text: a name in the enum, a row in SETUP_STR, TS(name) where it is shown.

enum {
    ST_DYNARUNFIX_SETUP, ST_ANOTHER_DYNARUNFIX_SETUP_IS_RUNNING, ST_THE_MODIFIED_TIME_OF_DYNARUN, ST_DYNARUN_WAS_NOT_STARTED_FROM,
    ST_THE_USER_ACCOUNT_IS_NOT, ST_DYNARUN_V3_EXE_WAS_NOT, ST_CANNOT_WRITE_THE_PROGRAM_FILES, ST_CANNOT_COPY_THE_UNINSTALLER,
    ST_THE_MANIFEST_FILE_NEXT_TO, ST_ADMINISTRATOR_PERMISSION_WAS_NOT_GRANTED, ST_THE_DYNARUN_V3_SETUP_COULD, ST_THIS_IS_NOT_THE_DYNARUN,
    ST_THE_DYNARUN_V3_SETUP_FAILED, ST_ANOTHER_INSTALLATION_IS_RUNNING_WAIT, ST_DYNARUN_V3_WAS_INSTALLED_BUT, ST_FIX_ONLY, ST_REMOVE_ALL,
    ST_DYNARUNFIX_IS_NOT_INSTALLED, ST_UNINSTALL_DYNARUNFIX, ST_UNINSTALL_DYNARUNFIX_FIX_ONLY_THE, ST_DYNARUN_IS_RUNNING_AND_MUST,
    ST_THE_DYNARUNFIX_FOLDER_CAN_BE, ST_THE_UNINSTALL_DID_NOT_FINISH, ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_DYNARUN,
    ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_BUT, ST_DYNARUNFIX_AND_DYNARUN_V3_HAVE, ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED,
    ST_COUNT };

typedef struct { const WCHAR *en, *zh; } ui_str;
static const ui_str SETUP_STR[ST_COUNT] = {
    // defs.h
    [ST_DYNARUNFIX_SETUP] = { L"DynaRunFix Setup",
                              L"DynaRunFix 安裝程式" },
    // stage.h
    [ST_ANOTHER_DYNARUNFIX_SETUP_IS_RUNNING] = { L"Another DynaRunFix setup is running. Wait until it finishes and try again.",
                                                 L"另一個 DynaRunFix 安裝程式正在執行，請等它完成後再試一次。" },
    // exetime.h
    [ST_THE_MODIFIED_TIME_OF_DYNARUN] = { L"The modified time of DynaRun V3.exe could not be put back as it was. It is kept in the registry and put back the next time DynaRunFix is installed or uninstalled.",
                                          L"DynaRun V3.exe 的修改時間無法還原。已記錄在登錄檔，下次安裝或解除安裝 DynaRunFix 時會再還原。" },
    // taskbar.h
    [ST_DYNARUN_WAS_NOT_STARTED_FROM] = { L"DynaRun was not started from here. Please start it with the \"DynaRun V3\" icon on the desktop.",
                                          L"這裡沒有啟動 DynaRun，請改用桌面上的「DynaRun V3」圖示開啟。" },
    // machine.h
    [ST_THE_USER_ACCOUNT_IS_NOT] = { L"The user account is not valid.",
                                     L"使用者帳戶無效。" },
    [ST_DYNARUN_V3_EXE_WAS_NOT] = { L"DynaRun V3.exe was not found at the given path.",
                                    L"在指定的路徑找不到 DynaRun V3.exe。" },
    [ST_CANNOT_WRITE_THE_PROGRAM_FILES] = { L"Cannot write the program files. Close DynaRun and try again.",
                                            L"無法寫入程式檔案。請關閉 DynaRun 後再試一次。" },
    [ST_CANNOT_COPY_THE_UNINSTALLER] = { L"Cannot copy the uninstaller.",
                                         L"無法複製解除安裝程式。" },
    [ST_THE_MANIFEST_FILE_NEXT_TO] = { L"The manifest file next to DynaRun V3.exe is in use and will be deleted at the next restart.",
                                       L"DynaRun V3.exe 旁的 manifest 檔正在使用中，將在下次重新啟動時刪除。" },
    // msi_stage.h
    [ST_ADMINISTRATOR_PERMISSION_WAS_NOT_GRANTED] = { L"Administrator permission was not granted. Nothing was changed.",
                                                      L"未取得系統管理員權限，沒有做任何變更。" },
    [ST_THE_DYNARUN_V3_SETUP_COULD] = { L"The DynaRun V3 setup could not be copied for installation.",
                                        L"無法複製 DynaRun V3 安裝檔以進行安裝。" },
    [ST_THIS_IS_NOT_THE_DYNARUN] = { L"This is not the DynaRun V3 setup, so it will not be installed.",
                                     L"這不是 DynaRun V3 的安裝檔，所以不會安裝它。" },
    [ST_THE_DYNARUN_V3_SETUP_FAILED] = { L"The DynaRun V3 setup failed (Windows Installer error %d).",
                                         L"DynaRun V3 安裝失敗（Windows Installer 錯誤 %d）。" },
    [ST_ANOTHER_INSTALLATION_IS_RUNNING_WAIT] = { L"\n\nAnother installation is running. Wait until it finishes and try again.",
                                                  L"\n\n有其他程式正在安裝，請等它完成後再試一次。" },
    [ST_DYNARUN_V3_WAS_INSTALLED_BUT] = { L"DynaRun V3 was installed, but DynaRun V3.exe was not found.",
                                          L"DynaRun V3 已安裝，但找不到 DynaRun V3.exe。" },
    // uninstall.h
    [ST_FIX_ONLY] = { L"&Fix only",
                      L"只移除修正(&F)" },
    [ST_REMOVE_ALL] = { L"&Remove all",
                        L"全部移除(&R)" },
    [ST_DYNARUNFIX_IS_NOT_INSTALLED] = { L"DynaRunFix is not installed.",
                                         L"DynaRunFix 尚未安裝。" },
    [ST_UNINSTALL_DYNARUNFIX] = { L"Uninstall DynaRunFix?",
                                  L"要解除安裝 DynaRunFix 嗎？" },
    [ST_UNINSTALL_DYNARUNFIX_FIX_ONLY_THE] = { L"Uninstall DynaRunFix?\n\n"
                                               L"Fix only: the DynaRun shortcuts start the original DynaRun again; DynaRun V3 stays installed.\n\n"
                                               L"Remove all: DynaRun V3 is then uninstalled too, by Dyna Pro's own uninstaller.",
                                               L"要解除安裝 DynaRunFix 嗎？\n\n"
                                               L"只移除修正：DynaRun 的捷徑改回開啟原版，DynaRun V3 保留。\n\n"
                                               L"全部移除：接著再用 Dyna Pro 自己的解除安裝程式移除 DynaRun V3。" },
    [ST_DYNARUN_IS_RUNNING_AND_MUST] = { L"DynaRun is running and must be closed to uninstall DynaRunFix.\n\n"
                                         L"OK closes DynaRun now: a test run in progress stops, unsaved data is lost.",
                                         L"解除安裝 DynaRunFix 前必須關閉 DynaRun。\n\n"
                                         L"按「確定」會立即關閉 DynaRun，正在進行的測試會中斷，沒存檔的資料會遺失。" },
    [ST_THE_DYNARUNFIX_FOLDER_CAN_BE] = { L"The DynaRunFix folder can be changed by ordinary users, so its uninstaller is not run with administrator rights. Remove DynaRunFix as an administrator.",
                                          L"DynaRunFix 資料夾可被一般使用者修改，因此不會以系統管理員權限執行其中的解除安裝程式。請以系統管理員身分移除 DynaRunFix。" },
    [ST_THE_UNINSTALL_DID_NOT_FINISH] = { L"The uninstall did not finish (code %d). Nothing else was changed.",
                                          L"解除安裝沒有完成（代碼 %d）。沒有做其他變更。" },
    [ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_DYNARUN] = { L"DynaRunFix has been uninstalled. DynaRun V3 was kept (its uninstall was cancelled).",
                                                     L"DynaRunFix 已解除安裝。DynaRun V3 的解除安裝已取消，DynaRun V3 保留。" },
    [ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED_BUT] = { L"DynaRunFix has been uninstalled, but uninstalling DynaRun V3 failed (Windows Installer error %d).",
                                                 L"DynaRunFix 已解除安裝，但 DynaRun V3 解除安裝失敗（Windows Installer 錯誤 %d）。" },
    [ST_DYNARUNFIX_AND_DYNARUN_V3_HAVE] = { L"DynaRunFix and DynaRun V3 have been uninstalled.",
                                            L"DynaRunFix 和 DynaRun V3 都已解除安裝。" },
    [ST_DYNARUNFIX_HAS_BEEN_UNINSTALLED] = { L"DynaRunFix has been uninstalled.",
                                             L"DynaRunFix 已解除安裝。" },
};
#define TS(id) T(SETUP_STR[id].en, SETUP_STR[id].zh)
