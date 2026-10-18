# DynaRun V3 first start: system selection hangs after "OK"

[繁體中文在下方](#繁體中文)

Developer notes on DynaRun V3 3.26.0's first-start sequence, what goes wrong in it and what DynaRunFix does
about it. Findings from 2026-10-07. The user-facing summary is in the [manual](MANUAL.md#first-time-setup).

## Symptom

On the first start (no `HKCU\Software\DynaPro` yet) DynaRun shows a language dialog, then the system selection
window "Dyna Pro系統選擇" (model plus optional features). After **OK**:

* (before v1.8.0) Windows shows a UAC prompt for `Setup_<n>.exe` (since v1.8.0 the helper is started without elevation, see below);
* the selection window stays, DynaRun uses one CPU core at 100 %, and it never continues;
* after killing and restarting DynaRun the chosen model is active, but the language is English and the
  optional features are switched off.

(Screenshots of DynaRun's own windows are not included in this repository.)

## What happens on "OK" (as observed)

On OK, Process Monitor shows DynaRun storing the selection in the registry and starting
`%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\System Data\Setup_<n>.exe` — one helper per model, e.g.
`Setup_114.exe` for the S68, `Setup_103.exe` for the S125 (file description "Registry Setup <n>"). The helper
writes that model's defaults into `HKCU\Software\DynaPro` and exits.

Before v1.8.0, Windows asked for elevation when a helper was started (a UAC prompt was observed). The helpers have "Setup" in their file
name and description; installer detection is the likely reason, which was not checked further. That prompt was a side effect,
not the cause of the hang (below).

**Since v1.8.0 there is no prompt.** The helper needs no administrator rights: Process Monitor shows that it only writes `HKCU\Software\DynaPro` (and
files under `%APPDATA%`), which a non-elevated process can do (see "Ruled out: UAC virtualization" below). `dynafix.dll` therefore starts it with the compatibility
layer `RunAsInvoker` in `__COMPAT_LAYER`, set for that one `ShellExecuteExA` call and put back right after it, so Windows' installer detection does not ask for
elevation. Earlier versions (since v1.4.0) deliberately kept the prompt, by taking `RunAsInvoker` out of DynaRun's environment, so the helper ran exactly as without DynaRunFix;
the prompt turned out to be unnecessary. DynaRun's other child processes still get the unchanged environment.

## Ruled out: UAC virtualization

File/registry virtualization would hide the helper's writes from the non-elevated DynaRun only if they went to
virtualized locations. They do not: everything lives in `HKCU\Software\DynaPro` and `%APPDATA%`, which are never
virtualized, and an elevated process of the same administrator account writes the same `HKCU`. Process Monitor
confirms DynaRun reads the helper's values on its next start.

## Root causes

Found by observing DynaRun from the outside: Process Monitor traces and window behaviour on Win11, Win7 and XP VMs.

1. **DynaRun does not continue (its own behaviour).** After starting the helper, DynaRun never continues, even
   after the helper has exited: the selection window stays and one core runs at 100 %. Reproduced identically on a
   **fresh XP VM with the official installer** (administrator account, no UAC), on Win7 and on Win11 → not caused
   by UAC or by DynaRunFix. The new configuration is in effect after DynaRun is started again.
2. **The helper resets the language.** It writes `Operation_Data\Default_Language = "English"`, over the
   language DynaRun stored from the language dialog a moment earlier. It also writes
   some other placeholder values (`"0"`); DynaRun fills those in on its next start, so they need no fix.
3. **Optional features end up disabled.** The features picked on the selection screen are stored as models
   (`Calibration_Data\Auto_Climate_Model`, `Air_Fuel_Model`), but the matching `System_Setup` enable flags are
   never set — the helper writes them as `0`. The configuration dialog then shows the modules unchecked.

   In short: the optional features picked on the selection screen are stored as model choices, while the helper
   writes the matching enable flags under `System_Setup` as `0`. *Save and exit* in the configuration dialog writes
   `-1` for each ticked module, whatever model is chosen.

## Fixes in DynaRunFix

All in memory, no DynaRun file is changed.

| Part | Change | Status |
|---|---|---|
| `dynafix.dll` | Wraps the Windows function `ShellExecuteA` for DynaRun (it hooks the VB runtime's `GetProcAddress` import to hand out its wrapper). For `System Data\Setup_<digits>.exe` (exact match; any other file passes through untouched) it runs the helper with `ShellExecuteExA` without elevation (`__COMPAT_LAYER` has `RunAsInvoker` for that one call, no UAC prompt; the log says `setup helper started without elevation (RunAsInvoker)`), waits for it, puts back the `Default_Language` DynaRun had stored before the helper ran, and restarts DynaRun through `DynaRunFix.exe /restart <pid>` — so DynaRun never gets stuck. | Verified, Win11 zh-TW, DynaRun 3.26.0 (with the UAC prompt, up to v1.7.2); the start without elevation (v1.8.0): to be verified in a VM, `tests\VM_CHECKLIST.md` section 10 |
| `DynaRunFix.exe` (launcher) | Starts DynaRun with `CREATE_SUSPENDED`, resumes it and polls `SetWindowsHookEx(WH_CALLWNDPROC)` on its GUI thread until it succeeds (a thread can be hooked only once it has a message queue; before that the call fails with error 87). Through Locale Emulator it starts `LEProc.exe` suspended in a job object, is told at once when DynaRun's process is created and hooks DynaRun's first thread the same way. `dynafix.dll` is then loaded before the first window, so the splash screen and forms created at start-up are covered too. With `/restart <pid>` it first waits (up to 15 s) for the old DynaRun to end. `dynafix.dll` is loaded into the launcher *before* DynaRun is created: the first load of a just-installed dll took seconds on a fresh Win11, and when it happened after the resume DynaRun had already made its first windows and laid out its main screen for the full screen height. `hooked from start` only says the first thread was hooked; `windows DynaRun had before dynafix: 0` says the fix came in time. | Verified, Win11 (log: `hooked from start`), native zh-TW Win11; first start right after install verified on a fresh zh-TW Win11 VM (old order 4/4 wrong, new order 5/5 right) |
| `DynaRunFix-Setup.exe` | Elevation from a mapped network drive (e.g. a VM shared folder) first copies the setup to `%TEMP%` and elevates the copy — the elevated process cannot see the user's mapped drives. Before, no UAC prompt appeared and the install failed silently. | Verified, Win11 (UAC names the `%TEMP%` copy) |
| `DynaRunFix-Setup.exe` (wizard) | Comes back to the foreground after the elevated part (the elevated process calls `AllowSetForegroundWindow`; if msiexec's window still holds the foreground, the wizard is raised in z-order and activated through `AttachThreadInput`, else its taskbar button flashes and a notification appears). While the elevated part runs, a 300 ms timer keeps the wizard directly behind the foreground window of the install chain (msiexec, consent, drvinst, pnputil, dpinst, the elevated stage), never topmost; a foreground window of an unrelated program is left alone and the log says only that an unrelated window has the foreground (not whose it is). When a background step (download, extraction, install, driver step) ends and the wizard then waits for a click while another window is in front, the taskbar button flashes and a notification appears; not when the next step starts at once, while the window closes, or on the page that restarts the computer by itself. The tray icon of the notification is removed after 10 s, when the wizard is activated and whenever the wizard ends. | Verified, Win11 (final page in front of a maximized Explorer) |
| `dynafix.dll` (optional features) | While the selection screen is still open, reads which optional-feature buttons are green (controls inside the frame by position, hidden EB-150/EB-250 skipped). After the helper it writes the `System_Setup` flags that *存檔並離開* would write (`Auto_Climate_Enable` + `Property_Auto_Climate`, `Cooling_Fan_Enable`, `Int_AF_Ratio_Enable`, `Water_Cooler_Enable`) as `"-1"`, logs each value, then restarts DynaRun. Only buttons that are clearly read as green are written; if the buttons cannot be read reliably (or any colour is unclear), nothing is written and the log says so. | Verified, Win11 (cooling fan alone; climate + fan + AFR) and native zh-TW Win11 (v1.2.2 code); XP not tested |

After the restart DynaRun comes up with the chosen model and language. With the optional-feature fix the
configuration dialog shows the picked modules (here DP-1001, cooling fan, DP-AF-1000-D) and the main window
shows the AFR gauge and the climate strip. With the launcher hooking DynaRun from the start, the splash
screen's version and status line are readable (before, with only the `DYNAFIX_CHARSET` font replacement, they
were garbled; a Traditional Chinese locale now uses Locale Emulator instead, below). No screenshots of
DynaRun's windows are included.

**What the optional-feature step writes.** The `System_Setup` values are exactly the user's own choices on
DynaRun's first-start system selection screen, and the same values DynaRun itself saves with *存檔並離開*. Nothing
is written beyond what that screen lets the user pick; unpicked features stay off and the greyed-out water
cooler cannot be picked.

### v1.2.2 on a fresh Windows 11

Recorded on a fresh native Traditional Chinese Windows 11 with `DynaRunFix-Setup.exe` 1.2.2 (DynaRun 3.26.0
downloaded and installed by the setup): Chinese picked, S68 with climate monitor and AFR analyser, one UAC prompt for
`Setup_114.Exe` (v1.2.2 behaviour; no prompt since v1.8.0), automatic restart. Splash screen and main window in Microsoft JhengHei UI, status bar above the taskbar.

(Screenshots of DynaRun's windows are not included.) Result: the main window came up in Chinese with the
climate strip and the AFR gauge.

## Native Traditional Chinese Windows 11 (comparison)

A Win11 installed in Traditional Chinese (ANSI code page 950), fresh DynaRun from the official installer:

* Without DynaRunFix the first start never completes: the helper's UAC prompt is accepted, the selection screen
  keeps coming back and the registry stays at the placeholder values → the hang is not tied to a changed
  system locale.
* With DynaRunFix everything worked (language kept, model, splash/RPM dialog/viewer in Chinese, no garbled text).
* `FontAssoc\Associated CharSet` does not exist at all there (no `ANSI(00)=YES`), and no charset handling is needed (the UI-font swap of v1.2.2 still applies, see the
  README's *Fonts* section).

## Garbled text after the locale was changed to Chinese (final fix: Locale Emulator)

Not a first-start problem, but found in the same tests. On a Windows installed in English whose system locale
was changed to Chinese (Taiwan) later, `FontAssoc\Associated CharSet` lacks `ANSI(00)=YES`, and DynaRun's Big5
text in ANSI-charset fonts is drawn as Latin letters.

* First attempt (**dropped**): `dynafix.dll` switched the fonts created by OCX
  controls and `comctl32` to the Big5 charset. RPM dialog and viewer labels became Chinese, but the toolbar
  tooltips of the viewer stayed garbled.

* **Final fix** (v1.2.2): for a Traditional Chinese locale without `ANSI(00)=YES` the launcher starts
  DynaRun through Locale Emulator, with `dynafix.dll` attached as usual. Verified on Win11: splash, dialogs,
  viewer and tooltips all Chinese, no flicker. `DYNAFIX_CHARSET` (the charset `dynafix.dll` gives the fonts it
  swaps) remains only for other DBCS locales, or when LE is missing.
* A natively Traditional Chinese Windows needs no charset handling, only the first-start fix; the UI-font swap
  (README, *Fonts*) applies there as everywhere else.

## How this was tested

* Virtual machines: Windows 11 (locale changed to zh-TW), Windows 11 installed in zh-TW, Windows 7, Windows XP
  (fresh install with the official DynaRun installer).
  The VMs were driven through the virtual machine tools' keyboard/mouse input and screenshots.
* Process Monitor captures filtered to `DynaPro` / `Setup_` events, registry dumps of `HKCU\Software\DynaPro`
  before and after the helper.

---

## 繁體中文

DynaRun V3 3.26.0 首次啟動流程的開發紀錄:哪裡出錯、DynaRunFix 怎麼處理。2026-10-07 的調查結果。
使用者版說明見 [使用手冊](MANUAL.md#首次設定)。

### 症狀

首次啟動(還沒有 `HKCU\Software\DynaPro`)時,DynaRun 先顯示語言對話框,再顯示「Dyna Pro系統選擇」(機型與選購功能)。按 **OK** 後:

* (v1.8.0 以前)出現 `Setup_<編號>.exe` 的 UAC 提示(v1.8.0 起以不提升權限的方式啟動,見下);
* 選擇視窗不消失、DynaRun 佔滿一個 CPU 核心,永遠不會繼續;
* 強制結束再啟動後機型正確,但語言變回英文,選購功能都是關閉的。

### 按 OK 後發生什麼事(觀察結果)

按下 OK 後,Process Monitor 可以看到 DynaRun 把選擇存進登錄,然後啟動
`%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\System Data\Setup_<編號>.exe`。每個機型一個(S68 是 `Setup_114.exe`、
S125 是 `Setup_103.exe`…,檔案描述為「Registry Setup <編號>」),它把該機型的預設值寫進 `HKCU\Software\DynaPro` 後結束。這些程式的名稱與描述含「Setup」;v1.8.0 以前啟動時 Windows 會要求提升權限(觀察到 UAC 提示),可能的原因是安裝程式偵測,未進一步確認。
UAC 只是附帶現象,不是卡住的原因。

**v1.8.0 起沒有提示。** 這個設定程式不需要系統管理員權限:Process Monitor 顯示它只寫入 `HKCU\Software\DynaPro`(以及 `%APPDATA%` 下的檔案),未提升權限的行程就能做到(見下方「已排除:UAC 虛擬化」)。
所以 `dynafix.dll` 在啟動它的那一次 `ShellExecuteExA` 呼叫前,把相容層 `RunAsInvoker` 加進 `__COMPAT_LAYER`,呼叫後立刻還原,Windows 的安裝程式偵測就不會要求提升權限。
先前的版本(自 v1.4.0 起)刻意保留這個提示,方法是把 `RunAsInvoker` 從 DynaRun 的環境清掉,讓設定程式的行為與沒有 DynaRunFix 時完全一樣;事後證明這個提示並不必要。DynaRun 其他子行程的環境維持不變。

### 已排除:UAC 虛擬化

所有設定都在 `HKCU\Software\DynaPro` 和 `%APPDATA%`,這兩處不會被虛擬化;同一個管理員帳號提升權限後寫的也是同一個 HKCU。
Process Monitor 也確認 DynaRun 下次啟動會讀到設定程式寫的值。

### 根本原因

依據從外部觀察 DynaRun 的結果(Win11/Win7/XP 虛擬機的 Process Monitor 紀錄與視窗行為):

1. **DynaRun 不會繼續(它本身的行為)。** 啟動設定程式後,DynaRun 永遠不會繼續,設定程式結束了也一樣:
   選擇視窗不消失、CPU 一核 100%。在**全新 XP 虛擬機 + 官方安裝程式**(管理員帳號、沒有 UAC)、Win7、Win11 上都一模一樣
   → 是 DynaRun 本身的行為,與 UAC 或 DynaRunFix 無關。新設定要到下次啟動才會讀取。
2. **設定程式把語言改回英文。** 它寫入 `Operation_Data\Default_Language = "English"`,蓋掉 DynaRun 剛從語言對話框存下的選擇。
   另外還寫了一些佔位值(`"0"`),DynaRun 下次啟動會自己補上,不需處理。
3. **選購功能被關閉。** 選擇畫面勾的功能會存成型號(`Calibration_Data\Auto_Climate_Model`、`Air_Fuel_Model`),
   但對應的 `System_Setup` 啟用旗標從沒被設定(設定程式寫成 `0`),所以系統組態設定裡這些模組都沒勾。
   簡單說:選擇畫面勾的選購功能存成型號選擇,但設定程式把 `System_Setup` 底下對應的啟用旗標寫成 `0`;
   在系統組態設定按「存檔並離開」則會對勾選的模組寫 `-1`,與型號無關。

### DynaRunFix 的修正

全部在記憶體中處理,不修改 DynaRun 的任何檔案。

選購功能這一步寫入的 `System_Setup` 值,就是使用者自己在 DynaRun 首次啟動的系統選擇畫面上的選擇,
與 DynaRun 自己按「存檔並離開」存下的值相同;不會寫入該畫面讓使用者選的範圍以外的東西(沒選的功能維持關閉,灰色的水冷箱不能選)。

| 元件 | 修改 | 狀態 |
|---|---|---|
| `dynafix.dll` | 為 DynaRun 包裝 Windows 的 `ShellExecuteA`(hook VB 執行階段的 `GetProcAddress` 匯入,交出包裝函式):遇到 `System Data\Setup_<數字>.exe`(完整路徑吻合才算,其他檔案原樣放行)時以不提升權限的方式用 `ShellExecuteExA` 執行(只在這一次呼叫的 `__COMPAT_LAYER` 加上 `RunAsInvoker`,沒有 UAC 提示;log 會寫 `setup helper started without elevation (RunAsInvoker)`)並等待,寫回 DynaRun 先前存的 `Default_Language`,再以 `DynaRunFix.exe /restart <pid>` 重新啟動 DynaRun,DynaRun 就不會卡住。 | 已驗證(Win11 繁中、3.26.0;v1.7.2 以前含 UAC 提示);不提升權限的啟動方式(v1.8.0)待在虛擬機驗證,見 `tests\VM_CHECKLIST.md` 第 10 節 |
| `DynaRunFix.exe` | 以 `CREATE_SUSPENDED` 啟動 DynaRun,恢復執行後輪詢 `SetWindowsHookEx`,直到其 GUI 執行緒有訊息佇列(之前會回傳錯誤 87)。經 Locale Emulator 時則把 `LEProc.exe` 暫停啟動並放進 job 物件,DynaRun 一建立就收到通知,再以同樣方式掛上它的第一個執行緒。這樣 `dynafix.dll` 在第一個視窗之前就載入,啟動畫面和一開始建立的表單也都涵蓋。`/restart <pid>` 時先等舊的 DynaRun 結束(最多 15 秒)。`dynafix.dll` 在建立 DynaRun 之前就先載入啟動器:剛安裝的 dll 第一次載入在全新 Win11 上要好幾秒,若在 DynaRun 開始執行後才載入,DynaRun 已建立最初的視窗並照整個螢幕高度排好主畫面。log 的 `hooked from start` 只代表掛上了第一個執行緒,`windows DynaRun had before dynafix: 0` 才代表來得及。 | 已驗證(Win11、原生繁中 Win11) |
| `DynaRunFix-Setup.exe` | 從網路磁碟機(例如虛擬機共用資料夾)提升權限時,先複製到 `%TEMP%` 再提升(提升後的行程看不到使用者對應的網路磁碟機)。修正前不會出現 UAC,安裝無聲失敗。 | 已驗證(Win11) |
| 安裝精靈 | 提升權限的部分結束後回到前景(`AllowSetForegroundWindow`;若前景仍被 msiexec 視窗佔住,改用 z-order + `AttachThreadInput`,再不行就閃爍工作列按鈕並顯示通知)。提升權限的部分執行時,300 毫秒的計時器會讓精靈緊貼在安裝流程中取得前景的視窗(msiexec、consent、drvinst、pnputil、dpinst、提升權限的階段本身)後面,但不置頂;前景是無關程式的視窗時不干擾,log 只寫「有無關的視窗在前景」,不寫是誰的。背景步驟(下載、解壓縮、安裝、驅動程式步驟)結束、精靈接著等你點擊而別的視窗在前景時,工作列按鈕閃爍並顯示通知;下一個步驟馬上開始、視窗正在關閉、自動重新開機的那一頁都不通知。通知的系統匣圖示在 10 秒後、精靈被啟用時,以及精靈結束時移除。 | 已驗證(Win11) |
| `dynafix.dll`(選購功能) | 選擇畫面還開著時讀出哪些選購功能按鈕是綠色(依位置、略過隱藏的 EB-150/EB-250),設定程式結束後寫入「存檔並離開」會寫的 `"-1"` 旗標並記錄,再重新啟動 DynaRun。只寫入明確讀到是綠色的按鈕;讀不準(或顏色不明確)時什麼都不寫,並在 log 說明。 | 已驗證(Win11;原生繁中 Win11 以 v1.2.2 的程式碼測試);XP 未測 |

v1.2.2 在全新原生繁中 Win11 上的紀錄(由 `DynaRunFix-Setup.exe` 1.2.2 下載並安裝 DynaRun 3.26.0;選中文、S68、勾選大氣監測與空燃比分析儀,
`Setup_114.Exe` 一次 UAC(v1.2.2 的行為,v1.8.0 起沒有提示),自動重新啟動;啟動畫面與主畫面都是 Microsoft JhengHei UI,狀態列在工作列上方):

(不附 DynaRun 視窗截圖。)結果:主畫面為中文,有大氣資訊與空燃比錶。

### 原生繁中 Win11 對照

以繁體中文安裝的 Win11(字碼頁 950)、官方安裝程式全新安裝 DynaRun:沒有 DynaRunFix 時首次啟動過不去(接受 UAC 後選擇畫面一直回來,登錄停在預設佔位值),
所以卡住與「後來才改系統地區」無關;有 DynaRunFix 時全部正常、沒有亂碼。該系統根本沒有 `FontAssoc\Associated CharSet`(沒有 `ANSI(00)=YES`),也不需要任何字元集處理(v1.2.2 的介面字型替換仍會套用,見 README「字型」)。

### 地區後來才改成中文時的亂碼(最終解法:Locale Emulator)

不是首次啟動的問題,但在同一批測試中發現。英文安裝、之後才把系統地區改成中文(台灣)的 Windows,`FontAssoc\Associated CharSet` 缺 `ANSI(00)=YES`,
DynaRun 在 ANSI 字元集字型裡的 Big5 文字會變成拉丁字母。

* 第一個做法(**已放棄**):`dynafix.dll` 把 OCX 與 `comctl32` 建立的字型改成 Big5 字元集。
  轉速錶設定與看圖程式的標籤變成中文,但看圖程式的工具列 tooltip 仍是亂碼。
* **最終解法**(v1.2.2):繁中地區且缺 `ANSI(00)=YES` 時,啟動器改用 Locale Emulator 啟動 DynaRun(照常掛上 `dynafix.dll`)。
  已在 Win11 驗證:啟動畫面、對話框、看圖程式、tooltip 全部是中文,不閃爍。`DYNAFIX_CHARSET`(`dynafix.dll` 替換字型時使用的字元集)只留給其他 DBCS 地區或找不到 LE 時。
* 原生繁中 Windows 不需要字元集處理,只需要首次啟動的修正;介面字型替換(README「字型」)在這裡也一樣套用。

### 測試方法

虛擬機(Win11 改繁中地區、原生繁中 Win11、Win7、全新 XP),透過虛擬機工具的鍵盤/滑鼠操作與截圖;
Process Monitor 只篩選 `DynaPro`/`Setup_` 事件;設定程式執行前後匯出 `HKCU\Software\DynaPro`。
