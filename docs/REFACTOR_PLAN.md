# 重構紀錄（已完成，2026-10-18）

依據：2026-10-17 逐行讀完 `src/` 全部模組、`tests/`、`build.cmd` 後擬定的計畫；本文件記錄實際結果。已合併進 `develop`。

## 原則（執行時維持）

- **行為不變**：每一步都是純重構；疑似 bug 另案驗證、另開 commit。
- **小步、可驗證**：每步單獨編譯、跑單元測試與回歸清單（`tests/VM_CHECKLIST.md`）、單獨 commit。
- **跨行程 ABI 不動**：elevated stage 的命令列動詞與參數位置、行程結束碼的數值、log mutex 名稱與格式、`CwpProc` 匯出、`setup.rc` 資源 ID、`DRF_VERSION` 巨集全部維持原樣，只加命名常數。
- **測試仍可 include**：測試靠 `#include "../src/setup.c"`、`driver.c` 取用 static，所以拆檔後各 `.c` 仍是單一編譯單元。

## 決定

維護者的決定：

1. 範圍：P0–P6 全部做，並行進行。
2. 結果碼：只加名稱，數值不變（跨行程 ABI 不變）。
3. 疑似 bug：先驗證，確認有問題的另開 commit 修，不混在重構 commit 裡。
4. 死碼：移到 tests，不留在正式程式碼。

## 根本問題（「改 A 動 B」的來源）

1. 流程靠全域旗標傳遞（`g_close`、`g_pin`、`g_keep`、`g_autostart(_later)`、`g_quiet`），`run_elevated` 暗中依它們附加參數。
2. 隱藏狀態：`g_user_sid`、`g_exe_trusted`、`pMsi*`、`g_tool_ok_code`、`g_other_dir` 等側通道。
3. 結果碼互相重疊（PK_*、RC_*、DRV_E_*、裸數字）。
4. 分派散落：精靈 22 頁、7 個 job，新增一頁要改約 9 處；命令列在 setup.c 與 driver.c 各分派一次。
5. 模式旗標 `g_pre`、`g_driver_only`、`g_fix_done`、`g_drvmode` 交叉決定頁面。
6. 重複邏輯要手動同步（捷徑目錄清單、目錄走訪、啟動行程並等待、log mutex SDDL、zip EOCD 掃描、`update_run`／`update_offer`）。
7. `setup.c` 混了約 8 個領域，`setup.h` 讓所有模組看到全部宣告。

## 實際結果（模組配置）

- **P0 安全網**：`tests/VM_CHECKLIST.md`（VM 回歸清單）、特性測試鎖住命令列解析、`run_elevated` 參數字串與結果碼判斷。
- **P1 基礎層**：`src/errors.h`（結果碼命名，數值不變）；`src/setup/util.h`（路徑組合、`run_process`、`dir_walk`、app key 開啟）；`src/applog.h`（launcher 與 update 共用 log mutex、SDDL、時鐘與 `Update*` 登錄值名稱）。
- **P2 setup**：`install_opts` 取代全域旗標；`cmdline.h` 動詞表（併入 driver 的動詞）；`shortcuts.h` 單一捷徑目錄清單；`trust_ctx` 與 `msi_api_init`。`src/setup.c` 保留入口並 include `src/setup/` 下分檔：`defs.h`、`strings.h`、`util.h`、`trust.h`、`exetime.h`、`procs.h`、`shortcuts.h`、`taskbar.h`、`elevate.h`、`machine.h`、`msi_stage.h`、`userstage.h`、`users.h`、`stage.h`、`uninstall.h`、`cmdline.h`。
- **P3 package / driver**：`package.c` 單一 central directory 讀取器；`driver.c` 側通道改成參數、`prep_locked` 單一出口。`setup.h` 拆成 `drf_app.h`、`drf_util.h`、`drf_package.h`、`drf_driver.h`、`drf_update.h`、`drf_wizard.h`，測試專用宣告在 `drf_package_internal.h`、`drf_driver_internal.h`；`setup.h` 保留為總括 header，`setup_match.h` 為比對輔助。
- **P4 wizard**：job 表、單一 flow 值、頁面表（每頁一個 `enter_*`）、明確的 `wz.result`（結果碼）、`wizard_state` 與 worker 輸入輸出結構、en/zh 字串集中。`src/wizard.c` include `src/wizard/` 下的 `main.h`、`acquire.h`、`driver.h`、`layout.h`、`strings.h`。
- **P5 update**：`update_prompt(policy)` 取代 `update_run`／`update_offer`；launcher 入口拆成 `parse_args`、`elevation_gate`、`start_le`、`start_plain`、`hook_and_finish`。
- **P6 dynafix**：資料表、提示 UI、選項、畫面、PEGRC 移到 `src/dynafix_tables.inc.c`、`dynafix_hint.inc.c`、`dynafix_options.inc.c`、`dynafix_screen.inc.c`、`dynafix_pegrc.inc.c`，由 `src/dynafix.c` include。hook 引擎、`seh_run`、`patch_slot`、`ldr_note`、USBFIX 狀態機、`cwp_work` 順序未動。
- **死碼移入 tests**：`promote_com_in` 等測試專用 COM 輔助（0609115、e66d23d）移到 `tests/promote_com.h`，`zip_extract_all` 移到 `tests/zip_extract_all.h`。

### 與計畫的差異

- P1.3 `applog` 為純 header（`src/applog.h`），`build.cmd` 不需改。
- dynafix 維持單一編譯單元（`.inc.c`），`logtest.c` 與 `build.cmd` 不動；dynafix 的 log 未併入 `applog.h`。
- `setup.c` 仍是單一編譯單元，include 分檔，測試照舊 include 它。

## 疑似 bug：結論

1. `zip_extract` 未呼叫 `crc_init`：**不是 bug**。`zipent` 只來自 `zip_open`／`zip_walk`，它們已呼叫 `crc_init`；仍把 `crc_init` 改成可重複呼叫並由 `zip_extract` 呼叫作為防禦（P3.1）。
2. `g_pkg_log` 常開：**不是 bug**。只有 `driver_fetch` 路徑會寫入；已改成參數（P3.2）。
3. `classify` 的 `pMsi*` 為 NULL：**不是 bug**。`restore`／`unretarget` 只用 `LNK_OURS` 判斷，在 MSI 查詢之前就決定；仍新增 `msi_api_init`（P2.4，由 `classify` 呼叫，只執行一次），所以各路徑（含 uninstall）都會載入 msi.dll。
4. `prep_locked` 提早 return 未清理：**已修**，948ee1f（準備失敗時清除已暫存檔案與 `accepted.txt`）。
5. `peg_ldr_note` 在 loader lock 下呼叫 `logf`／`patch_base`：依維護者決定**不修**。沒有死鎖（具名 mutex 最多等 2 秒＋檔案 I/O，不呼叫 LoadLibrary），且 patch 不能延後。但發現相關競態並修正，cb1aa28（loader callback 以非阻塞方式取得 `peg_check` 的 busy 旗標，拿不到就延後）。
6. worker 無鎖寫入 `g_drv_*`、`g_neterr`、`g_hint_*`：**不是 bug**，`job_done` 在 UI 讀取前已 join worker；唯一的良性競態是 `g_lastpct`，改成只有 worker 寫入，1c8498e。
7. `g_close` 設成 TRUE 後不再恢復：**已修**，2a1faa5（「關閉 DynaRun 並安裝」的同意只涵蓋一次安裝 job）。
8. 死碼：移到 tests，0609115（另 e66d23d 移走其餘測試專用 COM 輔助）。
- 額外修正：0791e0e，APPDATA 路徑過長時捷徑／釘選程式碼會溢位，改為略過該資料夾。

## 合併與驗證

- 重構分三條並行線（主程式／setup 與 wizard、dynafix、update），皆從 main 912e3d4 分出，依序合併進 `refactor/solid`（merge commit ad34711、117eb7b、0582a1a）；各線主要 commit 為 c860db5、cb1aa28、b4a59b6。125d521 移除合併後重複的 `driver_prep_stage` stub；3923d63 文件對齊。
- 最終驗證（3923d63）：單元測試與 dynafix 測試通過，`tests/VM_CHECKLIST.md` 回歸清單通過；其中需手動或需特殊環境的項目未執行（見下）。
- 未驗證：標準使用者相關的手動章節、7.5 redirect（需受信任的自簽憑證）、BitLocker、Win8／7／XP、合併後版本的更新檢查（§8；已在 update 分支驗證）。
