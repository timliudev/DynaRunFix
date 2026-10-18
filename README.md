# DynaRunFix: Windows 10/11 fix for DynaRun V3 (Dyna Pro dynamometer) / Dyna Pro 馬力機軟體 DynaRun V3 的 Win10/11 修正

[![build](https://github.com/timliudev/DynaRunFix/actions/workflows/build.yml/badge.svg)](https://github.com/timliudev/DynaRunFix/actions/workflows/build.yml)

DynaRunFix is an unofficial compatibility fix that stops the **main-screen flicker** of *DynaRun V3*, the Dyna Pro dynamometer software (S68 and similar rigs), when it runs **natively on Windows 10 / 11**.<br>
DynaRunFix 是非官方的相容性修正，讓 Dyna Pro 馬力機的 *DynaRun V3* 軟體（S68 等機型）**原生在 Windows 10 / 11 執行時主畫面不再閃爍**。

Its installer can also set up DynaRun V3 and the Dyna Pro USB driver; it does not include DynaRun itself.<br>
安裝程式也能替你安裝 DynaRun V3 與 Dyna Pro USB 驅動程式；本專案不含 DynaRun 本體。

[繁體中文說明在下方](#繁體中文)

> Not affiliated with or endorsed by Dyna Pro Dynamometers Ltd or THB Componentware. DynaRunFix contains none of their files, modifies
> the content of no Dyna Pro or THB program file, and does not touch DynaRun's licence activation or copy protection. It changes
> DynaRun's behaviour **in memory, inside the DynaRun process only**; what exactly it does is listed in the
> [manual](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md). You need your own legally installed copy of DynaRun V3, and it is up to you to make sure that using this
> fix is consistent with your licence agreement with Dyna Pro.

## Requirements

- Windows 10 or 11 (the installer also runs on Windows XP and 7; the automatic driver installation needs Windows 10 or 11).
- A legally installed DynaRun V3, or the setup password you got from Dyna Pro (the installer can download and install DynaRun V3 for you).
- Administrator permission for the install. On an administrator account Windows asks once for the whole install, the driver included; a standard user types an administrator's password, and again after the restart when the driver is installed. DynaRun itself never runs with more rights than a normal start.

## Install

1. Download **`DynaRunFix-Setup.exe`** from [Releases](https://github.com/timliudev/DynaRunFix/releases) and double-click it.
   SmartScreen may say *"Windows protected your PC"* because the file is not code-signed: click **More info → Run anyway**.
2. Follow the wizard. It installs DynaRun V3 first if it is missing (asking for the setup password from Dyna Pro), then the fix, and points DynaRun's shortcuts at it.
3. **First time on this PC? Also install the Dyna Pro USB driver.** Leave **Also install the Dyna Pro USB driver (one restart needed)**
   ticked on the first page (Windows 10 / 11); it needs one restart, where you press **7**; Windows still asks for permission only once. Steps and problems:
   [manual: Installing the Dyna Pro USB driver](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#installing-the-dyna-pro-usb-driver) and the [driver guide](https://github.com/timliudev/DynaRunFix/blob/main/docs/DRIVER.md).

## Use

Start DynaRun with its usual **DynaRun V3** icon (the shortcuts now go through the fix). On the very first start Windows asks once more
for permission for Dyna Pro's configuration helper: click **Yes**. Uninstall from *Programs and Features* / *Installed apps*; it asks
whether to remove the fix only or DynaRun V3 too.

## More

- [Manual](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md): what the fix changes, what the installer does, command-line options and exit codes, updates, first-time setup, other Windows 10/11 problems, what is verified, building.
- [Driver guide](https://github.com/timliudev/DynaRunFix/blob/main/docs/DRIVER.md): installing the Dyna Pro USB driver, with screenshots, also by hand and on older Windows.
- [Changelog](https://github.com/timliudev/DynaRunFix/blob/main/CHANGELOG.md).
- Background: [root cause of the flicker](https://github.com/timliudev/DynaRunFix/blob/main/docs/ROOT_CAUSE.md), [first start](https://github.com/timliudev/DynaRunFix/blob/main/docs/FIRST_START.md), [VBScript warning](https://github.com/timliudev/DynaRunFix/blob/main/docs/VBSCRIPT.md).

## License

MIT, see [LICENSE](https://github.com/timliudev/DynaRunFix/blob/main/LICENSE). Third-party licences: [manual](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#third-party-licences).

---

## 繁體中文

讓 *DynaRun V3* 馬力機軟體(Dyna Pro Dynamometers,S68 等機型)**原生在 Windows 10/11 執行時主畫面不再閃爍**的小型相容性修正。

> 本專案與 Dyna Pro Dynamometers Ltd、THB Componentware 無任何關係,也未經其認可。DynaRunFix 不含它們的任何檔案,不修改任何 Dyna Pro 或 THB 程式檔案的內容,
> 也不碰 DynaRun 的授權啟用或防拷機制。它只在 DynaRun 行程**內、記憶體中**改變 DynaRun 的行為;詳細做了什麼請見[使用手冊](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#繁體中文)。
> 你需要自己合法安裝的 DynaRun V3,並自行確認使用本修正符合你與 Dyna Pro 的授權合約。

### 系統需求

- Windows 10 或 11(安裝程式也可在 Windows XP、7 執行;自動安裝驅動程式需要 Windows 10 或 11)。
- 合法安裝的 DynaRun V3,或你從 Dyna Pro 取得的安裝密碼(安裝程式可以替你下載並安裝 DynaRun V3)。
- 安裝時需要系統管理員權限。使用系統管理員帳戶時，整個安裝（包括驅動程式）Windows 只會問一次；標準使用者要輸入系統管理員密碼，安裝驅動程式時重新開機後還會再問一次。DynaRun 本身永遠不會以高於一般啟動的權限執行。

### 安裝

1. 從 [Releases](https://github.com/timliudev/DynaRunFix/releases) 下載 **`DynaRunFix-Setup.exe`**,雙擊執行。
   因為檔案沒有程式碼簽章,SmartScreen 可能顯示「Windows 已保護您的電腦」:按**其他資訊 → 仍要執行**。
2. 照精靈畫面操作。還沒安裝 DynaRun V3 時會先安裝(會要求 Dyna Pro 給你的安裝密碼),接著安裝修正,並把 DynaRun 的捷徑指向它。
3. **這台電腦第一次使用?還需要安裝 Dyna Pro USB 驅動程式。** 讓精靈第一頁的**同時安裝 Dyna Pro USB 驅動程式（需重新開機一次）**維持勾選(Windows 10 / 11);
   需要重新開機一次,並在選單按 **7**;Windows 仍然只會詢問一次是否允許變更。步驟與疑難排解:
   [使用手冊:安裝 Dyna Pro USB 驅動程式](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#安裝-dyna-pro-usb-驅動程式)與[驅動程式指南](https://github.com/timliudev/DynaRunFix/blob/main/docs/DRIVER.md#繁體中文)。

### 使用

照常點 **DynaRun V3** 圖示啟動(捷徑現在會經過修正)。第一次啟動時,Windows 會再問一次是否允許 Dyna Pro 的設定程式變更:按**是**。
解除安裝請用「程式和功能」/「已安裝的應用程式」;它會問只移除修正,還是連 DynaRun V3 一起移除。

### 更多

- [使用手冊](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#繁體中文):修正改了什麼、安裝程式做了什麼、命令列參數與結束代碼、更新、首次設定、其他 Windows 10/11 問題、驗證了什麼、建置。
- [驅動程式指南](https://github.com/timliudev/DynaRunFix/blob/main/docs/DRIVER.md#繁體中文):安裝 Dyna Pro USB 驅動程式(含畫面),也可手動、舊版 Windows。
- [更新紀錄](https://github.com/timliudev/DynaRunFix/blob/main/CHANGELOG.md#更新紀錄)。
- 背景資料:[閃爍的根本原因](https://github.com/timliudev/DynaRunFix/blob/main/docs/ROOT_CAUSE.md)、[首次啟動](https://github.com/timliudev/DynaRunFix/blob/main/docs/FIRST_START.md)、[VBScript 警告](https://github.com/timliudev/DynaRunFix/blob/main/docs/VBSCRIPT.md)。

### 授權

MIT,見 [LICENSE](https://github.com/timliudev/DynaRunFix/blob/main/LICENSE)。第三方授權:[使用手冊](https://github.com/timliudev/DynaRunFix/blob/main/docs/MANUAL.md#第三方授權)。
