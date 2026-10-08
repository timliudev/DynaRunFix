# Changelog

[繁體中文在下方](#更新紀錄)

## Unreleased

- **Taskbar:** the installer pins DynaRun to the taskbar (check box on the first page, on by default;
  `/notaskbar` turns it off). Windows 7 to 10 pin the launcher shortcut directly, Windows XP / Vista put it
  in Quick Launch, and Windows 11, which lets no program pin itself, gets it through Microsoft's taskbar layout
  policy (needs the administrator prompt the installer shows anyway; the icon appears after the next sign-in; an
  existing layout policy of an organization is left alone and the last page tells how to pin by hand). The
  running DynaRun groups under the pinned icon (explicit AppUserModelID on the launcher shortcuts and in DynaRun).
- **Start at sign-in:** second check box, on by default (`/noautostart` turns it off): a `Run` value for the
  current user starts DynaRun through the launcher. Uninstall removes it (also for other accounts).
- **Screen after a start at sign-in / display changes:** the sign-in start (`DynaRunFix.exe /autostart`) waits
  until the taskbar exists and the screen size and work area have been unchanged for 3 s (at most 60 s) before it
  starts DynaRun. While DynaRun runs, a change of resolution, DPI or work area makes its maximized main window fit the
  new work area again (once the changes stop for 0.5 s; a restored window is left alone). If DynaRun's screen
  ever looks wrong, close DynaRun and start it again: the last page of the installer and the README say so.
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

### 未發布

- **工作列:** 安裝程式會把 DynaRun 釘選到工作列(第一頁的勾選項,預設勾選;`/notaskbar` 可關閉)。Windows 7 到 10 直接釘選
  啟動器捷徑,Windows XP / Vista 放進「快速啟動」,Windows 11 不允許程式自行釘選,改用微軟的工作列配置原則
  (使用安裝程式本來就會出現的系統管理員確認;登出再登入後才會出現圖示;組織已設定配置原則時不會動它,最後一頁會說明如何手動釘選)。
  執行中的 DynaRun 會歸在釘選的圖示下(啟動器捷徑和 DynaRun 使用相同的明確 AppUserModelID)。
- **開機自動啟動:** 第二個勾選項,預設勾選(`/noautostart` 可關閉):在目前使用者的 `Run` 登錄值經由啟動器啟動 DynaRun。
  解除安裝時會移除(其他帳號也一併處理)。
- **登入啟動後與顯示變更的畫面:** 登入時的自動啟動(`DynaRunFix.exe /autostart`)會等到工作列出現、螢幕大小與工作區連續 3 秒沒變
  (最多 60 秒)才啟動 DynaRun。DynaRun 執行中如果解析度、DPI 或工作區改變,最大化的主視窗會重新貼合新的工作區
  (變更停止 0.5 秒後執行;使用者還原過的視窗不會被動)。如果 DynaRun 的畫面顯示不正常,關掉 DynaRun 再開一次即可
  (安裝程式最後一頁和 README 都有說明)。
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
