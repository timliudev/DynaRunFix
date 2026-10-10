# Changelog

[繁體中文在下方](#更新紀錄)

## v1.3.5 — 2026-10-10

- **Run viewer after a run did not fill the screen:** the maximized "Dyna Pro測試後看圖程式" window was clipped to
  the work area *and* kept its aspect ratio (meant only for the caption-less main dashboard), so it was ~76 px too
  narrow with the desktop showing on the right, and maximizing again did not help (log:
  `clipped to the work area 1936x1216 -> 1860x1168`). A form with a caption is now only clipped to the work area.

## v1.3.4 — 2026-10-10

- **Installer, finding the setup:** the search in *Downloads*, on the desktop, in *Documents* and next to the
  installer took any zip with an `.msi` inside, e.g. another vendor's `SpeedTuning_S_v10_ZH.zip`, unpacked it and
  then showed "This is not the DynaRun V3 setup". Dyna Pro's zip is password-protected, so the search now only takes
  password-protected zips (and DynaRun MSIs as before). A file the installer found by itself that turns out not to be
  DynaRun's is skipped quietly: the next one is tried, or the setup is downloaded. The error page is shown only for a
  file you chose with *Choose file...*, where any zip is still accepted.

## v1.3.3 — 2026-10-09

- **Taskbar (the actual cause):** since v1.3.0 `dynafix.dll` is loaded before DynaRun starts, and its `DllMain`
  looked up `SetCurrentProcessExplicitAppUserModelID` in a shell32 that was not loaded yet: the AppUserModelID was
  never set. The running DynaRun therefore never grouped under the pinned DynaRun icon and showed the exe's chart icon
  as a button of its own (v1.3.0 to v1.3.2, whatever the shortcuts looked like). The ID is now set when dynafix is
  first called in DynaRun, still before its first window, loading shell32 if needed; the log shows
  `AppUserModelID DynaRunFix.DynaRunV3 set: 00000000`. Verified on Windows 11: started from the pinned icon or the
  desktop shortcut, DynaRun shows as the pinned icon with DynaRun's logo, no extra button.

## v1.3.2 — 2026-10-09

- **Taskbar icon:** `DynaRun V3.exe` holds only the chart icon of its windows; DynaRun's blue logo is in the icon file
  of the shortcuts Dyna Pro's setup makes. A desktop shortcut the installer had created (when none existed) used the
  exe's icon, and as the running DynaRun takes the icon of a shortcut with its AppUserModelID, the taskbar could
  still show the chart after v1.3.1. Every shortcut of ours that would use the exe's icon now gets the logo from a
  DynaRun shortcut that has it. Install v1.3.2 (or let the update do it), then start DynaRun again.

## v1.3.1 — 2026-10-09

- **Taskbar icon after an update:** updating from v1.2.x left the existing DynaRun shortcuts (already pointing to
  DynaRunFix) without the AppUserModelID that v1.3.0 gives the running DynaRun, so Windows found no matching shortcut:
  the running DynaRun showed its window icon (a chart) instead of DynaRun's own icon, as a second taskbar button next
  to the pinned one. The installer now writes our shortcuts again with the ID (name, icon, hotkey and *Run as
  administrator* kept). Install v1.3.1 once over v1.3.0 (or let the update do it), then start DynaRun.
- **Docs:** README and changelog: a resolution change while DynaRun is open is DynaRun's own behaviour; the first-start
  layout fix explained.

## v1.3.0 — 2026-10-09

- **Taskbar:** the installer pins DynaRun to the taskbar (check box on the first page, on by default;
  `/notaskbar` turns it off). Windows 7 to 10 pin the launcher shortcut directly, Windows XP / Vista put it
  in Quick Launch, and Windows 11, which lets no program pin itself, gets it through Microsoft's taskbar layout
  policy (needs the administrator prompt the installer shows anyway; the icon appears after the next sign-in; an
  existing layout policy of an organization is left alone and the last page tells how to pin by hand). The
  running DynaRun groups under the pinned icon (explicit AppUserModelID on the launcher shortcuts and in DynaRun).
- **Start at sign-in:** second check box, on by default (`/noautostart` turns it off): a `Run` value for the
  current user starts DynaRun through the launcher. Uninstall removes it (also for other accounts).
- **Screen after a start at sign-in:** the sign-in start (`DynaRunFix.exe /autostart`) waits
  until the taskbar exists and the screen size and work area have been unchanged for 3 s (at most 60 s) before it
  starts DynaRun. If DynaRun's screen
  ever looks wrong, close DynaRun and start it again: the last page of the installer and the README say so.
- **Screen right after an install:** the first start of DynaRun after installing DynaRunFix (the installer's
  *Start DynaRun* button, or the first start at sign-in) could lay out the main screen for the full screen height:
  the right gauge cut off, the dashboard shifted; a second start was fine. The launcher let DynaRun run first and
  only then loaded `dynafix.dll`, and the very first load of a newly installed dll is slow (a few seconds on a fresh
  Windows 11), so DynaRun had made its first windows before the fix was there (all versions since v1.2.2). The
  launcher now loads `dynafix.dll` before DynaRun starts. The log shows `dynafix.dll loaded in … ms` and
  `windows DynaRun had before dynafix: N` (0 = in time). Verified on a fresh zh-TW Windows 11 VM.
- **Known issue (DynaRun's own behaviour):** changing the screen resolution while DynaRun is open leaves the main
  screen laid out for the old size (DynaRun lays out its fixed 4:3 screen once, at start); restart DynaRun.
- **Updates:** once a day, when DynaRun is started (and is not already running), the installed copy of the
  setup asks GitHub (`api.github.com/repos/timliudev/DynaRunFix/releases/latest`) whether there is a newer release;
  nothing else is sent. The launcher waits up to 5 s for the answer; with a newer release it asks *Update now?*
  before DynaRun opens (a slower answer is asked at the next start; logged in `%TEMP%\dynafix.log`). *Yes* downloads that
  release's `DynaRunFix-Setup.exe` from this repository only, checks it against the SHA-256 GitHub lists for it,
  installs it (one administrator prompt) and opens DynaRun; the taskbar pin, the start at sign-in and the desktop
  shortcut stay as they are (`/keep`). *No*, a cancelled prompt or a failed update asks again a day later. Offline,
  or on Windows XP (no TLS 1.2), nothing happens. Versions before this one do not check: install it once by hand.
- **Log:** every line in `%TEMP%\dynafix.log` now starts with the date and time (`YYYY-MM-DD HH:MM:SS.mmm`). The
  launcher logs how it was started (arguments, `/autostart`, `/restart` or plain, parent process, start-up flags
  and show command, working folder, DynaRunFix version); each DynaRun process logs a header line with the DynaRunFix
  version and commit, the version of `DynaRun V3.exe` and the Windows version with its update revision. The log is
  kept below 10 MB: the oldest lines are dropped first (the newest 8 MB stay), checked at each start.
- **Dev builds** show the short commit hash (`-dirty` with uncommitted changes) instead of `dev` in the installer
  title, Programs and Features and the log; they still never check for updates.
- `/quiet` applies both options unless switched off with `/notaskbar` / `/noautostart`. The first page and the
  window are a little taller; the last page says what was set up.

## v1.2.3 — 2026-10-07

- **Removed `tools/msi-novbs`:** DynaRunFix no longer ships anything that changes Dyna Pro's setup. If Windows
  has VBScript switched off, turn it back on under *Optional features*, or use `DynaRunFix-Setup.exe`, which runs
  Dyna Pro's setup without its VBScript wizard pages.
- **Licences:** the installer and the zip now include the miniz licence (`LICENSE-miniz.txt`); miniz is used by
  the installer to unpack Dyna Pro's setup.
- **Docs:** the first-start notes describe only what can be observed from outside DynaRun; the README asks users
  to check their licence agreement with Dyna Pro.

## v1.2.2 — 2026-10-07

- **First start:** the language picked on the first start and the optional features picked on the system
  selection (climate monitor, water cooler, cooling fans, AFR analyser) are kept; Dyna Pro's configuration
  helper used to reset them to English / off. ([details](README.md#first-time-setup))
- **Fonts:** all of DynaRun's text is drawn in Windows' own UI font for the language (Microsoft JhengHei UI on
  Traditional Chinese, Segoe UI on English Windows) instead of a mix of Arial, MS Sans Serif and MingLiU.
  ([details](README.md#fonts-windows-ui-font-everywhere))
- **Garbled text:** Windows installed in English with the locale changed to Traditional Chinese later
  (FontAssoc without `ANSI(00)=YES`) now also runs DynaRun through Locale Emulator: every window, tooltips
  included, shows Chinese. ([details](README.md#garbled-chinese-text-with-a-traditional-chinese-system-locale))
- **Status bar:** the taskbar no longer hides DynaRun's bottom status bar; DynaRun is laid out for the screen's
  work area. ([details](README.md#status-bar-hidden-behind-the-taskbar))
- **Launcher:** the fix is attached before DynaRun's first window, so the splash screen and the first forms are
  covered too.
- **Installer:**
  - DynaRun running: *Close DynaRun and install* instead of replacing files under a running DynaRun;
    `/quiet` exits with code 6, `/quiet /close` closes DynaRun first.
  - The wizard stays in front of Explorer while DynaRun's setup and the permission prompt run.
  - Works when started from a network drive.
  - New icon.
- **Tool (removed in v1.2.3):** `tools/msi-novbs`
  made Dyna Pro's `Setup.msi` installable without VBScript (for when Windows disables it, planned for about
  2027). `DynaRunFix-Setup.exe` does not need it: it runs that setup without its VBScript wizard pages.

## v1.2.1 — 2026-10-07

- Setup: shows Dyna Pro's download page while downloading, reports the real download size, and recognises the
  DynaRun setup by its UpgradeCode instead of the file name.

## v1.2.0 — 2026-10-07

- One-click setup: downloads and installs DynaRun V3 when it is missing (password and license from Dyna Pro).
- First start no longer hangs after the system selection.
- Windows' UTF-8 option: code-page manifest plus Locale Emulator for a Traditional Chinese system locale.

## v1.1.0 — 2026-10-07

- `DynaRunFix-Setup.exe`: one-click installer for the fix (shortcuts, machine-wide ActiveX registration,
  uninstaller).

## v1.0.0 — 2026-10-07

- Stops the main-screen flicker of DynaRun V3 on Windows 10/11.
- .Dpr files stored in OneDrive (*Always keep on this device*) open again.

---

## 更新紀錄

### v1.3.5 — 2026-10-10

- **跑完後的看圖視窗沒填滿螢幕:** 「Dyna Pro測試後看圖程式」視窗最大化時,為了讓開工作列而縮小的同時也維持了長寬比
  (這原本只該用在沒有標題列的主儀表板),寬度少了約 76 px,右邊露出桌面,再按最大化也一樣。有標題列的表單現在只縮到
  工作區大小,寬度填滿。

### v1.3.4 — 2026-10-10

- **安裝程式找安裝檔:** 在「下載」、桌面、「文件」和安裝程式旁邊搜尋時,原本只要 zip 裡有 `.msi` 就拿來用(例如別家的
  `SpeedTuning_S_v10_ZH.zip`),解開後才跳出「這不是 DynaRun V3 安裝檔」。Dyna Pro 的 zip 有密碼,所以現在只收有密碼的 zip
  (DynaRun 的 MSI 照舊)。安裝程式自己找到、但其實不是 DynaRun 的檔案會直接跳過,改試下一個或改從官網下載;錯誤頁只在你用
  「選擇檔案…」自己選的檔案不對時才出現,自己選的話任何 zip 都可以。

### v1.3.3 — 2026-10-09

- **工作列(真正的原因):** v1.3.0 起 `dynafix.dll` 在 DynaRun 啟動前就載入,而它在 `DllMain` 裡向還沒載入的 shell32 找
  `SetCurrentProcessExplicitAppUserModelID`,所以 AppUserModelID 從來沒設上。執行中的 DynaRun 因此不會合併到釘選的 DynaRun 圖示下,
  而是另外一個按鈕、顯示 exe 的表格圖示(v1.3.0 到 v1.3.2,不論捷徑怎麼設)。現在改在 dynafix 第一次在 DynaRun 裡被呼叫時設定
  (仍在第一個視窗之前),需要時先載入 shell32;log 會記 `AppUserModelID DynaRunFix.DynaRunV3 set: 00000000`。已在 Windows 11 驗證:
  從釘選的圖示或桌面捷徑開,DynaRun 就是那個釘選圖示、顯示 DynaRun 的 logo,沒有多出來的按鈕。

### v1.3.2 — 2026-10-09

- **工作列圖示:** `DynaRun V3.exe` 本身只有視窗用的表格圖示,藍色 logo 在 Dyna Pro 安裝程式建立的捷徑所用的圖示檔裡。
  安裝程式在沒有桌面捷徑時建立的捷徑用的是 exe 的圖示,而執行中的 DynaRun 會用帶有同一個 AppUserModelID 的捷徑圖示,
  所以 v1.3.1 之後工作列仍可能顯示表格。現在我們的捷徑若用的是 exe 的圖示,會改用 DynaRun 捷徑上的 logo。
  裝好 v1.3.2(或讓自動更新處理)後重開 DynaRun 即可。

### v1.3.1 — 2026-10-09

- **更新後的工作列圖示:** 從 v1.2.x 更新時,原本已經指向 DynaRunFix 的捷徑沒有補上 v1.3.0 給執行中 DynaRun 的 AppUserModelID,
  Windows 找不到對應的捷徑,執行中的 DynaRun 就顯示視窗圖示(表格圖案)而不是 DynaRun 原本的圖示,而且在釘選的圖示旁另外多一個按鈕。
  現在安裝程式會把我們的捷徑重新寫一次並加上 ID(名稱、圖示、快捷鍵、以系統管理員身分執行都保留)。在 v1.3.0 上再裝一次 v1.3.1
  (或讓自動更新處理),再開 DynaRun 即可。
- **文件:** README 與更新紀錄:DynaRun 開著時改解析度會變形是 DynaRun 本身的行為;說明首次開啟版面的修正。

### v1.3.0 — 2026-10-09

- **工作列:** 安裝程式會把 DynaRun 釘選到工作列(第一頁的勾選項,預設勾選;`/notaskbar` 可關閉)。Windows 7 到 10 直接釘選
  啟動器捷徑,Windows XP / Vista 放進「快速啟動」,Windows 11 不允許程式自行釘選,改用微軟的工作列配置原則
  (使用安裝程式本來就會出現的系統管理員確認;登出再登入後才會出現圖示;組織已設定配置原則時不會動它,最後一頁會說明如何手動釘選)。
  執行中的 DynaRun 會歸在釘選的圖示下(啟動器捷徑和 DynaRun 使用相同的明確 AppUserModelID)。
- **開機自動啟動:** 第二個勾選項,預設勾選(`/noautostart` 可關閉):在目前使用者的 `Run` 登錄值經由啟動器啟動 DynaRun。
  解除安裝時會移除(其他帳號也一併處理)。
- **登入啟動後的畫面:** 登入時的自動啟動(`DynaRunFix.exe /autostart`)會等到工作列出現、螢幕大小與工作區連續 3 秒沒變
  (最多 60 秒)才啟動 DynaRun。如果 DynaRun 的畫面顯示不正常,關掉 DynaRun 再開一次即可
  (安裝程式最後一頁和 README 都有說明)。
- **安裝後第一次開的畫面:** 裝好 DynaRunFix 後第一次開 DynaRun(安裝程式的「開始使用 DynaRun」,或第一次登入自動啟動)
  可能會照整個螢幕高度排版:右邊儀表被切掉、版面偏移,再開一次就正常。原因是啟動器先讓 DynaRun 開始跑才載入
  `dynafix.dll`,而剛安裝的 dll 第一次載入很慢(全新 Windows 11 上要好幾秒),DynaRun 在修正掛上前就建立了最初的視窗
  (v1.2.2 起各版都有)。現在啟動器在 DynaRun 啟動前就先載入 `dynafix.dll`。log 會記 `dynafix.dll loaded in … ms` 與
  `windows DynaRun had before dynafix: N`(0 = 來得及)。已在全新繁中 Windows 11 虛擬機驗證。
- **已知問題(DynaRun 本身的行為):** DynaRun 開著時改變螢幕解析度,主畫面仍照舊的大小排版(DynaRun 只在啟動時排一次固定的 4:3 版面);重開 DynaRun 即可。
- **更新:** 每天一次，開啟 DynaRun（而且它還沒在執行）時，安裝好的安裝程式會向 GitHub
  （`api.github.com/repos/timliudev/DynaRunFix/releases/latest`）查詢有沒有新版本，不會送出其他資料。啟動器最多等 5 秒，
  有新版就在 DynaRun 開啟前問「現在更新嗎？」（回應較慢時下次開啟再問；過程記錄在 `%TEMP%\dynafix.log`）。按「是」只會從本專案下載該版本的 `DynaRunFix-Setup.exe`，
  用 GitHub 列出的 SHA-256 核對後安裝（一次系統管理員確認），然後開啟 DynaRun；工作列釘選、開機自動啟動和桌面捷徑維持原樣（`/keep`）。
  按「否」、取消確認或更新失敗，隔天會再問。沒有網路或 Windows XP（不支援 TLS 1.2）時什麼都不做。這一版之前的版本不會檢查，需要手動安裝一次。
- **Log:** `%TEMP%\dynafix.log` 每行開頭都有日期和時間(`YYYY-MM-DD HH:MM:SS.mmm`);啟動器會記錄自己是怎麼被啟動的(參數、`/autostart`、`/restart` 或一般啟動、父行程、啟動旗標與顯示方式、工作資料夾、DynaRunFix 版本);每個 DynaRun 行程另有一行標頭:DynaRunFix 版本與 commit、`DynaRun V3.exe` 的版本、Windows 版本與更新修訂號。log 保持在 10 MB 以下,最舊的行先被丟掉(留下最新的 8 MB,每次啟動時檢查)。
- **開發版**在安裝程式標題、「程式和功能」與 log 顯示短 commit hash(有未提交變更時加 `-dirty`)而不是 `dev`;仍然不會檢查更新。
- `/quiet` 預設套用這兩項,可用 `/notaskbar`、`/noautostart` 關閉。第一頁和視窗稍微變高;最後一頁會說明設定了什麼。

### v1.2.3 — 2026-10-07

- **移除 `tools/msi-novbs`:** DynaRunFix 不再提供任何修改 Dyna Pro 安裝檔的工具。Windows 停用 VBScript 時,
  請到「選用功能」重新啟用,或使用 `DynaRunFix-Setup.exe`(它執行 Dyna Pro 安裝檔時不顯示用到 VBScript 的精靈頁面)。
- **授權:** 安裝程式與 zip 附上 miniz 的授權(`LICENSE-miniz.txt`);安裝程式用 miniz 解開 Dyna Pro 的安裝檔。
- **文件:** 首次啟動說明只描述從 DynaRun 外部觀察得到的行為;README 提醒使用者自行確認與 Dyna Pro 的授權條款。

### v1.2.2 — 2026-10-07

- **首次啟動:** 保留第一次啟動時選的語言,以及系統選擇畫面勾選的選購功能(大氣監測、水冷、冷卻風扇、空燃比分析儀);
  以前會被 Dyna Pro 的設定程式改回英文、關閉。([說明](README.md#首次設定))
- **字型:** DynaRun 所有文字改用 Windows 該語言的介面字型(繁中:Microsoft JhengHei UI;英文 Windows:Segoe UI),
  不再混用 Arial、MS Sans Serif、新細明體。([說明](README.md#字型全部使用-windows-介面字型))
- **亂碼:** 以英文安裝、之後才改成繁中地區的 Windows(FontAssoc 缺 `ANSI(00)=YES`)也改用 Locale Emulator 啟動 DynaRun,
  所有視窗(含工具提示)都是中文。([說明](README.md#系統地區是繁體中文仍然亂碼))
- **狀態列:** 工作列不再遮住 DynaRun 底部的狀態列,DynaRun 依螢幕的工作區排版。([說明](README.md#狀態列被工作列遮住))
- **啟動器:** 在 DynaRun 第一個視窗出現前就掛上修正,啟動畫面和最先載入的表單也涵蓋在內。
- **安裝程式:**
  - DynaRun 執行中:改為「關閉 DynaRun 並安裝」,不再在執行中的 DynaRun 底下換檔案;`/quiet` 以代碼 6 結束,`/quiet /close` 先關閉 DynaRun。
  - DynaRun 安裝程式和權限提示執行時,精靈保持在檔案總管前面。
  - 從網路磁碟機執行也能安裝。
  - 新圖示。
- **工具(v1.2.3 已移除):** `tools/msi-novbs` 讓 Dyna Pro 的
  `Setup.msi` 不需要 VBScript 也能安裝(微軟預計約 2027 年預設停用 VBScript)。`DynaRunFix-Setup.exe` 不需要它:
  它執行該安裝檔時本來就不顯示用到 VBScript 的精靈頁面。

### v1.2.1 — 2026-10-07

- 安裝程式:下載時顯示 Dyna Pro 下載頁、顯示實際下載大小,改用 UpgradeCode 而非檔名辨識 DynaRun 安裝檔。

### v1.2.0 — 2026-10-07

- 一鍵安裝:沒有 DynaRun V3 時自動下載安裝(密碼與授權來自 Dyna Pro)。
- 首次啟動選擇系統後不再卡住。
- Windows UTF-8 選項:字碼頁 manifest,繁中系統地區再加上 Locale Emulator。

### v1.1.0 — 2026-10-07

- `DynaRunFix-Setup.exe`:修正的一鍵安裝程式(捷徑、系統層級 ActiveX 註冊、解除安裝)。

### v1.0.0 — 2026-10-07

- 修正 DynaRun V3 在 Windows 10/11 主畫面閃爍。
- 存在 OneDrive(「永遠保留在此裝置」)的 .Dpr 檔可以正常開啟。
