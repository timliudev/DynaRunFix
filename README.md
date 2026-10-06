# DynaRunFix

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

1. Install DynaRun V3 normally (use the original setup so its data files are in place).
2. Build (see below) or download `DynaRunFix.exe` and `dynafix.dll`, and keep them **in the same folder**
   (for example `%LOCALAPPDATA%\DynaRunFix`).
3. Start DynaRun through `DynaRunFix.exe` (create a desktop shortcut to it).
   - Default target: `C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe`
   - Other location: `DynaRunFix.exe "D:\path\to\DynaRun V3.exe"`
   - If DynaRun is already running, the launcher attaches the fix to it.
4. If you start DynaRun **as administrator**, start `DynaRunFix.exe` as administrator too
   (a non-elevated process cannot hook an elevated one).

Check `%TEMP%\dynafix.log`: it should contain `patched THBRes25 PostMessageA`.

## Other Windows 10/11 problems

### "Run as administrator" stops at "System Initializing. Please Wait. 115"

DynaRun's setup registers several ActiveX controls (MSComm, MSCOMCTL, MSHFlexGrid, ...) only for the
current user (`HKCU\Software\Classes`) when it runs without elevation. Elevated processes ignore per-user
COM registrations, so MSComm cannot be created and initialisation stops silently at step 115. The
settings/initialisation screens need administrator rights, so this matters.

Fix, from an **elevated** PowerShell:

```
powershell -ExecutionPolicy Bypass -File tools\register-machine-wide.ps1          # add -WhatIf to preview
```

It copies the affected CLSID/ProgID keys to `HKLM`. `regsvr32` alone is not enough: while a class key exists
under HKCU, writes through HKCR land in HKCU again.

To get both the flicker fix and administrator rights, start `DynaRunFix.exe` as administrator (for
example a shortcut with *Advanced → Run as administrator* ticked).

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
text boxes, charts). Some window captions, check boxes and labels are converted by Windows itself with the
system code page and can stay garbled while the UTF-8 option is on, and files in folders with non-ASCII names
may fail to open. Switching the option off (or running DynaRun through a locale emulator) fixes those too.
On systems without the UTF-8 option none of this is needed.

## Building

Requires Visual Studio 2019 or newer with the C++ desktop workload. Run:

```
build.cmd
```

Output goes to `build\`. The binaries are 32-bit, have no C runtime dependency and target Windows XP and later.

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
除了 `%TEMP%\dynafix.log` 不寫任何檔案。

### 使用方式
1. 用原廠 setup 正常安裝 DynaRun V3。
2. 把 `DynaRunFix.exe` 和 `dynafix.dll` 放在**同一個資料夾**(例如 `%LOCALAPPDATA%\DynaRunFix`)。
3. 以後都從 `DynaRunFix.exe` 啟動(可建桌面捷徑);DynaRun 若已在執行,會直接套用修正。
   安裝在其他路徑:`DynaRunFix.exe "D:\路徑\DynaRun V3.exe"`。
4. 若要**以系統管理員身分**執行 DynaRun,`DynaRunFix.exe` 也要以系統管理員身分執行。

`%TEMP%\dynafix.log` 出現 `patched THBRes25 PostMessageA` 即代表生效。

### 以系統管理員執行卡在「System Initializing. Please Wait. 115」
原廠安裝程式以一般權限執行時,MSComm、MSCOMCTL、MSHFlexGrid 等 ActiveX 元件只註冊在目前使用者(HKCU)。
以系統管理員執行的程式會忽略 HKCU 的 COM 註冊,所以建立 MSComm 失敗,初始化就停在 115。
進入語言或初始化設定畫面都需要系統管理員權限,所以這個問題一定得處理。

修正:以**系統管理員**開 PowerShell,執行 `tools\register-machine-wide.ps1`(加 `-WhatIf` 可以先預覽),
它會把相關的 CLSID/ProgID 複製到 HKLM。只跑 `regsvr32` 沒用,因為機碼已經存在於 HKCU 時,寫入會落回 HKCU。
要同時有防閃爍修正和系統管理員權限,請以系統管理員身分執行 `DynaRunFix.exe`
(捷徑 → 內容 → 進階 → 勾選「以系統管理員身分執行」)。

### 開啟系統 UTF-8 選項時中文亂碼
「地區 → 系統管理 → 變更系統地區設定 → Beta:使用 Unicode UTF-8 提供全球語言支援」開啟時,系統 ANSI 字碼頁是 65001,VB6 程式的中文會變亂碼。
把 [`manifest/DynaRun V3.exe.manifest`](manifest/DynaRun%20V3.exe.manifest) 複製到 `DynaRun V3.exe` 旁邊(Program Files 需要系統管理員權限),
DynaRun 就會改用系統地區的舊字碼頁(zh-TW 是 950),其他程式維持 UTF-8。Windows 會快取「這支 exe 沒有 manifest」的判斷,
複製完要用系統管理員 PowerShell 更新一次 exe 的修改時間:
`(Get-Item 'C:\Program Files (x86)\Dyna Pro Dynamometers\DynaRun V3.exe').LastWriteTime = Get-Date`。
之後 `%TEMP%\dynafix.log` 會顯示 `ansi-codepage=950`。選單、文字框、圖表都會正常;
部分視窗標題、核取方塊、按鈕和標籤是由 Windows 用系統字碼頁轉換的,開著 UTF-8 時仍會亂碼,含中文的資料夾路徑也可能無法開啟檔案;關閉該選項(或用 Locale Emulator 類工具啟動 DynaRun)即可完全正常。
沒開 UTF-8 選項的電腦完全不需要這一步。

### 建置
安裝 Visual Studio 2019 以上(含 C++ 桌面開發),執行 `build.cmd`,產物在 `build\`。32 位元、不依賴 C 執行階段、XP 以上皆可執行。
