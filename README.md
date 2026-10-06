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

### 建置
安裝 Visual Studio 2019 以上(含 C++ 桌面開發),執行 `build.cmd`,產物在 `build\`。32 位元、不依賴 C 執行階段、XP 以上皆可執行。
