# DynaRunFix

[![build](https://github.com/timliudev/DynaRunFix/actions/workflows/build.yml/badge.svg)](https://github.com/timliudev/DynaRunFix/actions/workflows/build.yml)

A small compatibility shim that stops the **main-screen flicker** of the *DynaRun V3* dynamometer
software (Dyna Pro Dynamometers, S68 and similar rigs) when it runs **natively on Windows 10 / 11**.

[繁體中文說明在下方](#繁體中文)

> This project is not affiliated with or endorsed by Dyna Pro Dynamometers Ltd or THB Componentware.
> It does not contain, modify or redistribute any of their files, and it does not touch licensing or
> copy-protection in any way. You need your own legally installed copy of DynaRun V3.

## Symptom

On Windows 10/11 the DynaRun V3 main dashboard flashes continuously (the whole window disappears and
reappears several times per second). The same installation is stable on Windows XP and Windows 7.
DPI settings, compatibility modes and whether the USB dyno is connected make no difference.

## Root cause (short version)

DynaRun V3 is a Visual Basic 6 application that uses the third-party *THBResize 2.5* control
(`THBRes25.dll`) to scale its forms.

1. THBResize reacts to every `WM_SIZE` on a form by posting itself a private message (`0x591`).
2. DynaRun's handler for that message hides the main form, loads a blank form, shows the main form
   maximized again and unloads the blank form.
3. **Windows 7/XP** do not send `WM_SIZE` when a hidden, already-maximized window is shown again with an
   unchanged size, so the sequence runs once and stops.
4. **Windows 10/11** send `WM_SIZE (SIZE_MAXIMIZED)` again in that situation (same size, the window-position
   change carries the "state changed" flag), so THBResize posts `0x591` again → endless hide/show loop.

Full analysis, message traces and the tools used: [docs/ROOT_CAUSE.md](docs/ROOT_CAUSE.md).

## The fix

`dynafix.dll` is loaded into the running DynaRun process by the `DynaRunFix.exe` launcher. Inside that
process only, it redirects THBRes25.dll's import of `PostMessageA` and drops the `0x591` message when it
was triggered by a `WM_SIZE` that is identical to the previous one for the same form. That restores the
Windows 7 behaviour; genuine size changes still go through. Nothing is written to disk except a small
log in `%TEMP%\dynafix.log`.

## Usage

1. Download **`DynaRunFix-Setup.exe`** from [Releases](https://github.com/timliudev/DynaRunFix/releases)
   and double-click it. Windows SmartScreen may say *"Windows protected your PC"* because the file is not
   code-signed: click **More info → Run anyway**.
2. Follow the window. If DynaRun V3 is not installed yet, it downloads the DynaRun V3 setup from
   [Dyna Pro's website](https://dynapro.co.uk/Software_Release.htm) (or uses a
   `Dyna Pro Dynamometers.zip` already in *Downloads* / on the desktop), asks for the **setup password you got
   from Dyna Pro**, shows Dyna Pro's license and installs DynaRun V3 and the fix. If DynaRun V3 is already
   installed, it only installs the fix. Windows asks once for permission: click **Yes**.
3. Start DynaRun with its usual **DynaRun V3** icon. On the very first start (system selection) Windows asks
   once more for permission for Dyna Pro's configuration helper: click **Yes**; DynaRun then restarts by itself.

Nothing of Dyna Pro's is included in DynaRunFix: the setup comes from Dyna Pro's site and the password from
Dyna Pro. The DynaRun setup runs with basic UI (`msiexec /qb`), so its wizard pages, the only part of it
that uses VBScript, are not shown. The password is used only to open the zip and is not stored.
The setup is recognised by its content, not its file name: only an MSI whose UpgradeCode is DynaRun's
(`{4787E5B2-F7CE-45B9-8D1D-68E167D06DF7}`, the same in every version) is ever installed; for the zip this is
checked after the password has opened it. Any other file is refused with a clear message.

What the installer does for the fix:
- installs `DynaRunFix.exe` and `dynafix.dll` to `Program Files\DynaRunFix`;
- points the existing DynaRun V3 shortcuts (desktop, Start menu, pinned taskbar, all users and current
  user) to the launcher, keeping their name, icon and *Run as administrator* setting; creates a desktop
  shortcut if there is none;
- makes DynaRun's ActiveX controls visible to elevated processes (what `tools/register-machine-wide.ps1`
  does, see [below](#run-as-administrator-stops-at-system-initializing-please-wait-115));
- only if Windows' *UTF-8 for worldwide language support* option is on: adds the
  [code-page manifest](#garbled-chinese-or-other-dbcs-text-with-windows-utf-8-option) next to `DynaRun V3.exe`;
- puts [Locale Emulator](#garbled-chinese-or-other-dbcs-text-with-windows-utf-8-option) in `Program Files\DynaRunFix\le`
  (used only with the UTF-8 option on a Traditional Chinese system);
- registers an uninstaller in *Programs and Features* / *Installed apps*.

Uninstalling restores the original shortcut files and removes the manifest it added. The machine-wide
ActiveX registrations are kept (removing them would break elevated DynaRun again). DynaRun's own files
and your data files are never changed. Windows XP, 7, 10 and 11 are supported; options: `/quiet`,
`/uninstall`. `/quiet` installs only the fix and needs DynaRun V3 installed.

If *Documents* is in OneDrive and an earlier copy of DynaRun's manuals or example files there is
"online-only", the installer reads those files first so that OneDrive downloads them; Windows Installer
cannot do that itself and would stop with error 1305. File contents and OneDrive settings are not changed.

The launcher elevates itself when DynaRun has to run elevated (compatibility setting *Run as
administrator*, or an elevated DynaRun is already running), so the shortcut does not need to.

### Manual use (zip)

The zip on the Releases page contains the same files for manual use: keep `DynaRunFix.exe` and
`dynafix.dll` **in the same folder** and start DynaRun through `DynaRunFix.exe`
(`DynaRunFix.exe "D:\path\to\DynaRun V3.exe"` for another location). If DynaRun is already running, the
launcher attaches the fix to it.

Check `%TEMP%\dynafix.log`: it should contain `patched THBRes25 PostMessageA`.

## Other Windows 10/11 problems

### "Run as administrator" stops at "System Initializing. Please Wait. 115"

DynaRun's setup registers several ActiveX controls (MSComm, MSCOMCTL, MSHFlexGrid, ...) only for the
current user (`HKCU\Software\Classes`) when it runs without elevation. Elevated processes ignore per-user
COM registrations, so MSComm cannot be created and initialisation stops silently at step 115. The
settings/initialisation screens need administrator rights, so this matters. The same pattern hits many
XP-era ActiveX programs; see the playbook [docs/ELEVATED_COM.md](docs/ELEVATED_COM.md).

`DynaRunFix-Setup.exe` does this automatically. Manual fix, from an **elevated** PowerShell:

```
powershell -ExecutionPolicy Bypass -File tools\register-machine-wide.ps1          # add -WhatIf to preview
```

It copies the affected CLSID/ProgID/TypeLib keys to `HKLM` (with a backup and a log). `regsvr32` alone is not enough: while a class key exists
under HKCU, writes through HKCR land in HKCU again.

To get both the flicker fix and administrator rights, start `DynaRunFix.exe` as administrator (the
installed shortcut: right-click → *Run as administrator*).

### First-time setup hangs after "OK" (system selection window stays, one CPU core at 100%)

On the first start DynaRun runs `%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\System Data\Setup_<nnn>.exe`
(e.g. `Setup_114.exe` for the S68) with `ShellExecute`. These helpers have no manifest and "Setup" in their
name and description, so Windows' installer detection runs them elevated. The helper does its work, but the
non-elevated DynaRun keeps showing the system selection and only picks up the new configuration on its next
start.

`dynafix.dll` handles this in memory: DynaRun's `ShellExecuteA` (a VB *Declare*, resolved through
`GetProcAddress`) is wrapped, waits for `Setup_<nnn>.exe` to finish (one UAC prompt) and then restarts
DynaRun through `DynaRunFix.exe /restart <pid>`. DynaRun then comes up with the chosen system. Verified on
Windows 11 with DynaRun 3.26.0.

### Garbled Chinese text with a Traditional Chinese system locale

When Windows was installed in English and the system locale changed to Chinese (Taiwan) later, the
`HKLM\SYSTEM\CurrentControlSet\Control\FontAssoc\Associated Charset` key may lack `ANSI(00)=YES`. GDI
then draws DynaRun's Big5 labels in ANSI-charset fonts with code page 1252, which shows Latin letters
(`Aw³ï¥B·P...`). The launcher detects this and `dynafix.dll` creates those fonts with the Big5 charset (and
Microsoft JhengHei UI) inside DynaRun only (`DYNAFIX_CHARSET`). Nothing in the registry is changed.
If the system locale is not Chinese at all, DynaRun's Chinese cannot be shown: set *Language for non-Unicode
programs* to Chinese (Traditional, Taiwan); the installer says so when it detects it.

### Garbled Chinese (or other DBCS) text with Windows' UTF-8 option

With *Region → Administrative → Change system locale → "Beta: Use Unicode UTF-8 for worldwide language
support"* enabled, the system ANSI code page is 65001 and VB6 programs show garbled DBCS text.

Copy [`manifest/DynaRun V3.exe.manifest`](manifest/DynaRun%20V3.exe.manifest) next to `DynaRun V3.exe`
(administrator rights needed in Program Files). It sets `activeCodePage=Legacy`, so DynaRun uses the system
locale's legacy code page (950 for zh-TW) while the rest of the system stays UTF-8. Windows caches the
"no manifest" result per executable, so after copying the file update the exe's modification time once
(elevated PowerShell):

```
(Get-Item 'C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe').LastWriteTime = Get-Date
```

`%TEMP%\dynafix.log` then shows `ansi-codepage=950`. This fixes text converted inside the process (menus,
text boxes, message boxes, charts). Labels, graphic buttons and some captions are drawn through Windows
with the system code page and stay garbled; for those, DynaRunFix runs DynaRun through
[Locale Emulator](https://github.com/xupefei/Locale-Emulator) (LGPL-3.0):

- Put LE in a `le` folder next to `DynaRunFix.exe` (`DynaRunFix-Setup.exe` and the release zip already do; to fetch it yourself run
  `tools\fetch-le.ps1 -OutDir <folder>\le`). It is not installed: no context menu, nothing in the GAC.
  `le\LEConfig.xml` holds DynaRunFix's zh-TW profile.
- When the system code page is UTF-8, the system locale is Traditional Chinese (legacy code page 950) and
  `le\LEProc.exe` exists, `DynaRunFix.exe` starts DynaRun through LE and attaches the flicker fix right
  away. Otherwise (no UTF-8 option, or another locale, which gets just the manifest) it starts DynaRun
  directly, as before.
- Keep the manifest as well: only the combination shows all text correctly (manifest alone: labels
  garbled; LE alone: menus and message boxes garbled).
- Under LE fonts are created with the Big5 charset, so Windows draws Arial and similar faces with MingLiU.
  The launcher sets `DYNAFIX_FONT=Microsoft JhengHei UI` and `dynafix.dll` swaps Arial, Times New Roman,
  MS Sans Serif and MingLiU for it in DynaRun, its OCX controls and the chart. JhengHei's line height is 1.27
  em versus MingLiU's 1.0, so text of 16 px and up is scaled to 90 % to keep two-line labels fitting
  (`DYNAFIX_FONT_SCALE`, 50-150). Set `DYNAFIX_FONT` yourself to pick another face.

Verified on Windows 11 (UTF-8 option on) with DynaRun 3.26.0: menus, dialogs, labels, buttons, the
viewer and files in folders with Chinese names all work. Switching the UTF-8 option off also fixes
everything without LE. On systems without the UTF-8 option none of this is needed. `DynaRunFix-Setup.exe` adds the manifest
(and removes it on uninstall) only when the option is on.

### Some .Dpr files will not open (empty File Run Properties, no curves)

Files stored in OneDrive and marked *Always keep on this device* carry the attribute `0x80000`
(`FILE_ATTRIBUTE_PINNED`), which does not exist on Windows 7/XP. DynaRun checks the selected file with VB's
`GetAttr`, does not recognise the value as a normal file and silently gives up; Process Monitor shows it
querying the attributes but never opening the file for reading. The file's contents (e.g. the `#2025-05-01#`
date written by older versions) are not the cause: a byte-identical copy without the attribute opens fine.

`dynafix.dll` fixes this in memory: the file-attribute APIs imported by `MSVBVM60.DLL` and `scrrun.dll`
return the attributes without the cloud bits (pinned, unpinned, recall-on-open, recall-on-data-access).
Files and OneDrive settings are not changed. The log shows `cleared cloud attributes 00080020 in
GetFileAttributesA` when it applies. Verified on Windows 11 with DynaRun 3.26.0.

## Building

Requires Visual Studio 2019 or newer with the C++ desktop workload. Run:

```
build.cmd
```

Output goes to `build\` (`DynaRunFix-Setup.exe` embeds `DynaRunFix.exe`, `dynafix.dll` and the manifest;
set `DRF_VERSION` for its version string); it uses [miniz](https://github.com/richgel999/miniz) 3.1.2 (MIT, `third_party/miniz`) to unpack the DynaRun setup). The binaries are 32-bit, have no C runtime dependency and target Windows XP and later.

## Diagnostic tool: msgspy

`tools/msgspy` is the window-message logger used to find the cause. It hooks DynaRun's GUI thread and
writes every relevant message (with decoded `WM_SIZE`, `WM_WINDOWPOSCHANGING/CHANGED`, `WM_GETMINMAXINFO`,
`WM_CREATE`, `WM_SETTEXT`) to `msgspy_<COMPUTERNAME>.log` next to the executable. It runs unchanged on
XP, 7 and 11, which makes side-by-side comparison with a VM easy.

```
msgspy.exe <seconds> [message-to-post-hex]
msgspy.exe 3          # log 3 seconds
msgspy.exe 4 591      # log 4 seconds and post 0x591 once to the maximized form
```

## License

MIT, see [LICENSE](LICENSE).

---

## 繁體中文

讓 *DynaRun V3* 馬力機軟體(Dyna Pro Dynamometers,S68 等機型)**原生在 Windows 10/11 執行時主畫面不再閃爍**的小型相容性修正。

> 本專案與 Dyna Pro Dynamometers Ltd、THB Componentware 無任何關係,不包含、不修改、不散布原廠任何檔案,
> 也完全不碰授權或防拷機制。你必須自備合法安裝的 DynaRun V3。

### 症狀
Win10/11 上主儀表板每秒閃好幾次(整個視窗消失又出現);同一套安裝在 XP / Win7 正常。
改 DPI、相容模式、有無接 USB 馬力機都沒差。

### 原因
1. DynaRun 用第三方 VB6 元件 THBResize 2.5(`THBRes25.dll`)做表單縮放,它每收到一次 `WM_SIZE` 就 post 一個私有訊息 `0x591`。
2. DynaRun 處理 `0x591` 時會:隱藏主表單 → 載入空白表單 → 主表單最大化顯示 → 卸載空白表單。
3. Win7/XP 重新顯示「已最大化、尺寸沒變」的視窗時**不送** `WM_SIZE`,跑一輪就停。
4. Win10/11 在同樣情況**照送** `WM_SIZE`,於是又觸發 `0x591` → 無限迴圈 → 閃爍。

詳細分析與訊息追蹤紀錄見 [docs/ROOT_CAUSE.md](docs/ROOT_CAUSE.md)。

### 修正方式
啟動器 `DynaRunFix.exe` 把 `dynafix.dll` 載入 DynaRun 行程,只在記憶體中把 THBRes25 對 `PostMessageA` 的呼叫導向修正函式:
若這次 `0x591` 是由「與上一次完全相同的 `WM_SIZE`」引起的就不送出,行為就跟 Win7 一樣。真正的尺寸變化照常處理。
除了 `%TEMP%\dynafix.log` 不寫任何檔案。同一個 dll 也修正 OneDrive 檔案打不開的問題(見下方)。

### 使用方式
1. 從 [Releases](https://github.com/timliudev/DynaRunFix/releases) 下載 **`DynaRunFix-Setup.exe`**，雙擊執行。
   因為檔案沒有數位簽章，Windows SmartScreen 可能顯示「Windows 已保護您的電腦」：請按 **其他資訊 → 仍要執行**。
2. 照畫面操作。還沒安裝 DynaRun V3 時，會從 [Dyna Pro 官網](https://dynapro.co.uk/Software_Release.htm)下載安裝檔
   （「下載」或桌面已經有 `Dyna Pro Dynamometers.zip` 就直接用），請你輸入 **Dyna Pro 給的安裝密碼**，
   顯示 Dyna Pro 的授權合約，然後安裝 DynaRun V3 和修正。已經裝好 DynaRun V3 時，只會安裝修正。
   Windows 會詢問一次是否允許變更，請按 **是**。
3. 以後照常點 **DynaRun V3** 圖示啟動。第一次啟動（選擇系統）時，Windows 會再問一次是否允許 Dyna Pro 的設定程式變更，
   請按 **是**，DynaRun 會自己重新啟動。

DynaRunFix 不包含任何 Dyna Pro 的檔案：安裝檔來自 Dyna Pro 官網，密碼由 Dyna Pro 提供。DynaRun 安裝檔以基本介面
（`msiexec /qb`）執行，所以不會出現它的精靈頁面（安裝檔裡唯一用到 VBScript 的部分）。密碼只用來打開 zip，不會被儲存。
安裝檔是依內容辨識，不看檔名：只有 UpgradeCode 是 DynaRun 的（`{4787E5B2-F7CE-45B9-8D1D-68E167D06DF7}`，每個版本都相同）
MSI 才會被安裝；zip 要等輸入密碼打開後才能檢查。其他檔案會被拒絕，並清楚告訴你原因。

安裝修正時會做這些事：
- 把 `DynaRunFix.exe`、`dynafix.dll` 安裝到 `Program Files\DynaRunFix`；
- 把現有的 DynaRun V3 捷徑（桌面、開始功能表、釘選到工作列；所有使用者與目前使用者）改為經由啟動器執行，名稱、圖示和「以系統管理員身分執行」設定都保留；沒有桌面捷徑時會建立一個；
- 讓 DynaRun 的 ActiveX 元件在系統管理員模式下也能使用（等同 `tools/register-machine-wide.ps1`，見下方）；
- 只有開啟 Windows「使用 Unicode UTF-8 提供全球語言支援」時，才在 `DynaRun V3.exe` 旁加上字碼頁 manifest（見下方）；
- 把 Locale Emulator 放到 `Program Files\DynaRunFix\le`（只有繁中系統開了 UTF-8 選項時才會用到，見下方）；
- 在「程式和功能」／「已安裝的應用程式」登錄解除安裝項目。

解除安裝會把捷徑檔還原成原本的內容，並移除它加上的 manifest。系統層級的 ActiveX 註冊會保留（移除的話，以系統管理員執行 DynaRun 又會壞掉）。
不會修改 DynaRun 本身的檔案和你的資料檔。支援 XP、7、10、11；參數：`/quiet`、`/uninstall`（`/quiet` 只安裝修正，需要已經裝好 DynaRun V3）。

如果「文件」放在 OneDrive，而之前留下的 DynaRun 手冊或範例檔是「只在線上」，安裝程式會先讀取這些檔案讓 OneDrive 下載下來；
Windows Installer 自己做不到，會出現錯誤 1305。不會改變檔案內容或 OneDrive 設定。

DynaRun 需要以系統管理員執行時（相容性設定勾了「以系統管理員身分執行」，或已有一個以系統管理員執行中的 DynaRun），啟動器會自己提升權限，捷徑不需要另外設定。

#### 手動使用（zip）
Releases 的 zip 內含同樣的檔案：把 `DynaRunFix.exe` 和 `dynafix.dll` 放在**同一個資料夾**，從 `DynaRunFix.exe` 啟動
（裝在其他路徑：`DynaRunFix.exe "D:\路徑\DynaRun V3.exe"`）。DynaRun 若已在執行，會直接套用修正。

`%TEMP%\dynafix.log` 出現 `patched THBRes25 PostMessageA` 即代表生效。

### 以系統管理員執行卡在「System Initializing. Please Wait. 115」
原廠安裝程式以一般權限執行時,MSComm、MSCOMCTL、MSHFlexGrid 等 ActiveX 元件只註冊在目前使用者(HKCU)。
以系統管理員執行的程式會忽略 HKCU 的 COM 註冊,所以建立 MSComm 失敗,初始化就停在 115。
進入語言或初始化設定畫面都需要系統管理員權限,所以這個問題一定得處理。很多 XP 時代的 ActiveX 程式都有同樣的問題,通用的診斷與修正流程見 [docs/ELEVATED_COM.md](docs/ELEVATED_COM.md)。

`DynaRunFix-Setup.exe` 會自動處理。手動修正:以**系統管理員**開 PowerShell,執行 `tools\register-machine-wide.ps1`(加 `-WhatIf` 可以先預覽),
它會把相關的 CLSID/ProgID 複製到 HKLM。只跑 `regsvr32` 沒用,因為機碼已經存在於 HKCU 時,寫入會落回 HKCU。
要同時有防閃爍修正和系統管理員權限,請以系統管理員身分執行 `DynaRunFix.exe`
(在安裝好的捷徑上按右鍵 →「以系統管理員身分執行」)。

### 首次設定按「OK」後卡住(系統選擇視窗不消失、CPU 一核 100%)
首次啟動時,DynaRun 會用 `ShellExecute` 執行 `%APPDATA%\Dyna Pro Dynamometers\Dyna Run V3\System Data\Setup_<編號>.exe`(S68 是 `Setup_114.exe`)。
這些程式沒有 manifest,檔名和描述又含「Setup」,Windows 的安裝程式偵測會以系統管理員執行它們。設定程式本身會完成,
但一般權限的 DynaRun 會一直停在系統選擇畫面,要下次啟動才會讀到新的設定。

`dynafix.dll` 只在記憶體中處理:包裝 DynaRun 的 `ShellExecuteA`(VB 的 *Declare*,經由 `GetProcAddress` 取得),
等 `Setup_<編號>.exe` 執行完(一次 UAC),再透過 `DynaRunFix.exe /restart <pid>` 重新啟動 DynaRun,就會直接進入選好的系統。
已在 Win11 + DynaRun 3.26.0 驗證。

### 系統地區是繁體中文仍然亂碼
Windows 以英文安裝、之後才把系統地區改成中文(台灣)時,`HKLM\SYSTEM\CurrentControlSet\Control\FontAssoc\Associated Charset`
可能沒有 `ANSI(00)=YES`。GDI 會用字碼頁 1252 繪製 ANSI 字元集字型裡的 Big5 標籤,變成 `Aw³ï¥B·P...` 這類拉丁字母。
啟動器偵測到這種情況時,`dynafix.dll` 只在 DynaRun 內把這些字型改用 Big5 字元集(以及微軟正黑體)建立(`DYNAFIX_CHARSET`),不改登錄。
系統地區根本不是中文時無法顯示 DynaRun 的中文,請把「非 Unicode 程式的語言」設為「中文(繁體,台灣)」;安裝程式偵測到時會提示。

### 開啟系統 UTF-8 選項時中文亂碼
「地區 → 系統管理 → 變更系統地區設定 → Beta:使用 Unicode UTF-8 提供全球語言支援」開啟時,系統 ANSI 字碼頁是 65001,VB6 程式的中文會變亂碼。
把 [`manifest/DynaRun V3.exe.manifest`](manifest/DynaRun%20V3.exe.manifest) 複製到 `DynaRun V3.exe` 旁邊(Program Files 需要系統管理員權限),
DynaRun 就會改用系統地區的舊字碼頁(zh-TW 是 950),其他程式維持 UTF-8。Windows 會快取「這支 exe 沒有 manifest」的判斷,
複製完要用系統管理員 PowerShell 更新一次 exe 的修改時間:
`(Get-Item 'C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe').LastWriteTime = Get-Date`。
之後 `%TEMP%\dynafix.log` 會顯示 `ansi-codepage=950`。選單、文字框、訊息框、圖表都會正常;
標籤、圖形按鈕和部分標題是經由 Windows 用系統字碼頁繪製的,仍會亂碼,這部分由 DynaRunFix 透過
[Locale Emulator](https://github.com/xupefei/Locale-Emulator)(LGPL-3.0)啟動 DynaRun 來解決:

- 把 LE 放在 `DynaRunFix.exe` 旁邊的 `le` 資料夾(`DynaRunFix-Setup.exe` 會自動安裝,release zip 也已附;要自己下載就執行 `tools\fetch-le.ps1 -OutDir <資料夾>\le`)。
  不需要安裝:不加右鍵選單、不寫入 GAC。`le\LEConfig.xml` 是 DynaRunFix 的 zh-TW 設定。
- 系統字碼頁是 UTF-8、系統地區是繁體中文(舊字碼頁 950),且有 `le\LEProc.exe` 時,`DynaRunFix.exe` 會透過 LE 啟動 DynaRun,並立刻掛上防閃爍修正;
  否則(沒開 UTF-8,或其他地區,只用 manifest)照舊直接啟動。
- manifest 也要保留:兩者一起才會全部正常(只有 manifest:標籤亂碼;只有 LE:選單和訊息框亂碼)。
- 透過 LE 時字型會以 Big5 字元集建立,Arial 等字型會被 Windows 換成細明體。啟動器會設定
  `DYNAFIX_FONT=Microsoft JhengHei UI`,`dynafix.dll` 在 DynaRun、OCX 元件和圖表裡把 Arial、Times New Roman、
  MS Sans Serif、細明體換成它。正黑體行高是 1.27 em(細明體 1.0),所以 16 px 以上的字縮為 90%,兩行的標籤才放得下
  (`DYNAFIX_FONT_SCALE`,50–150)。要用別的字型可以自行設定 `DYNAFIX_FONT`。

已在 Win11(開啟 UTF-8 選項)+ DynaRun 3.26.0 驗證:選單、對話框、標籤、按鈕、看圖程式、中文資料夾裡的檔案都正常。
關閉 UTF-8 選項也能全部正常,不需要 LE。沒開 UTF-8 選項的電腦完全不需要這一步。`DynaRunFix-Setup.exe` 只在開啟該選項時才加上 manifest(解除安裝時移除)。

### 部分 .Dpr 打不開(File Run Properties 全空、沒有曲線)
放在 OneDrive 且設成「永遠保留在此裝置」的檔案帶有屬性 `0x80000`(`FILE_ATTRIBUTE_PINNED`),這是 Win7/XP 沒有的屬性。
DynaRun 會用 VB 的 `GetAttr` 檢查選取的檔案,不認得這個值就當成「不是一般檔案」,而且不顯示錯誤;
Process Monitor 可以看到它只查了屬性,完全沒開檔讀取。跟檔案內容無關(例如舊版寫入的 `#2025-05-01#` 日期):
內容完全相同、只是沒有這個屬性的副本就能正常開啟。

`dynafix.dll` 只在記憶體中處理:`MSVBVM60.DLL` 和 `scrrun.dll` 匯入的檔案屬性 API 回傳時,會去掉雲端相關位元
(pinned、unpinned、recall-on-open、recall-on-data-access)。不修改任何檔案,也不改 OneDrive 設定。
生效時 log 會出現 `cleared cloud attributes 00080020 in GetFileAttributesA`。已在 Win11 + DynaRun 3.26.0 驗證。

### 建置
安裝 Visual Studio 2019 以上(含 C++ 桌面開發),執行 `build.cmd`,產物在 `build\`(`DynaRunFix-Setup.exe` 內含 `DynaRunFix.exe`、`dynafix.dll` 和 manifest;版本字串用環境變數 `DRF_VERSION` 指定;解開 DynaRun 安裝檔用的是 [miniz](https://github.com/richgel999/miniz) 3.1.2,MIT 授權,在 `third_party/miniz`)。32 位元、不依賴 C 執行階段、XP 以上皆可執行。
