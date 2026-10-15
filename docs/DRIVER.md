# Installing the Dyna Pro USB driver

[繁體中文在下方](#繁體中文)

Do this once on a PC that has never been connected to a Dyna Pro dynamometer. Without the driver, Windows lists the
device under *Other devices* and DynaRun cannot talk to it. DynaRunFix does not bundle the driver; it comes from
Dyna Pro. The user-facing summary is in the [README](../README.md#usage).

## Quick way: use the setup wizard

The DynaRunFix setup can do the steps below for you. Use it unless it tells you it cannot; the [manual method](#steps) stays
available.

* In the main installer, leave **Install the Dyna Pro USB driver** ticked (it is ticked by default and hidden if the driver is
  already installed or Windows is older than 10). Or, any time later, use the Start menu shortcut **安裝 Dyna Pro USB 驅動程式**
  or run `DynaRunFix-Setup.exe /driver`.
* The wizard downloads the official "Dyna Pro USB.zip" from dynapro.co.uk (it is not bundled) and checks its SHA-256. If the
  file on the site has changed, it refuses to install it; use the manual method then.
* Windows asks for administrator permission once (UAC). If BitLocker protection is on, the wizard suspends it until the driver is installed (at most 3 restarts as a safety net, then it is resumed automatically), sets the one-time
  advanced boot options and registers a task that continues the wizard when you sign in.
* After the restart, press **7** in the start-up menu (*Disable driver signature enforcement*). The wizard shows the page below;
  photographing it with your phone first is a good idea.

  | Press 7 |
  |---|
  | ![Startup Settings: press 7, Disable driver signature enforcement](img/driver-press-7.png) |

* If you miss 7, the wizard says so after you sign in and offers **再試一次** (Try again).
* Then Windows shows its red warning **twice** (once for each of the two driver files, ftdibus and ftdiport): choose
  **仍然安裝此驅動程式軟體** (*Install this driver software anyway*) each time. Driver signature enforcement comes back at the next normal restart.

Notes: Windows 10 or 11 is required. The driver files go to `%ProgramData%\DynaRunFix\driver` and the log is
`%ProgramData%\DynaRunFix\driver.log`. Uninstalling DynaRunFix does not remove the driver. Tested in a Windows 11 VM (zh-TW) with a simulated
device: the Startup Settings list appears directly after the restart, the retry after missing 7 works, the driver installs into the
driver store with no device present, and a device added later binds to it once signature enforcement is back on. Not yet tested on a
physical PC with real hardware, or with BitLocker.

If something goes wrong: choosing **Cancel** on the wizard's retry or install page takes back the logon task, the boot option and
the BitLocker suspension. If you closed the wizard some other way and want those undone, the manual steps below do not depend on
it; you can also run `DynaRunFix-Setup.exe /driver-cancel` (asks for administrator permission; it only removes the task, the
one-time boot option and the BitLocker suspension, it does not install or remove a driver).

## What you need

* The driver zip from Dyna Pro's site:
  [Dyna Pro USB.zip](https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20USB.zip)
* About 10 minutes and two restarts (one into the special start-up menu, one back to normal).

## Why the extra steps

The driver is FTDI's FTDIBUS/FTDIPORT driver (2.08.24, 2012) with its INF edited to add the Dyna Pro device ID
`USB\VID_0403&PID_FDBC` ("DynaPro Dynamometer"). Microsoft signed the driver catalog before that edit, so Windows
refuses the edited INF: *"The hash for the file is not present in the specified catalog file"*. To install it,
driver signature enforcement has to be turned off for one start of Windows.

## Steps

### 1. Download and unzip

Download the zip above and unzip it (right-click, *Extract All...*), for example to
`C:\Users\<you>\Downloads\Dyna Pro USB`. Remember the folder; you pick it in step 4.

### 2. Only if the PC uses BitLocker or device encryption: suspend it until the driver is installed

The start-up menu in step 3 asks for the BitLocker recovery key on encrypted PCs. Avoid that by suspending BitLocker
first. Open **PowerShell as administrator** (right-click Start, *Terminal (Admin)*) and run:

```powershell
Suspend-BitLocker -MountPoint "C:" -RebootCount 3
```

Protection resumes by itself after at most 3 restarts; once the driver is installed you can resume it right away with `Resume-BitLocker -MountPoint "C:"`. If the recovery key is asked anyway, you can find it at
<https://aka.ms/myrecoverykey> (sign in with the Microsoft account the PC is linked to).

If you are not sure whether the PC is encrypted, running the command does no harm.

### 3. Restart with driver signature enforcement disabled

Open *Start*, click the power button, hold **Shift** and click **Restart**. Then:

1. *Choose an option* → **Troubleshoot**
2. **Advanced options** → **Startup Settings** → **Restart**
3. After the restart, press **7** (*Disable driver signature enforcement*).

| 1. Choose an option → Troubleshoot | 2. Advanced options | 3. Startup Settings → Restart |
|---|---|---|
| ![Choose an option](img/driver-choose-an-option.png) | ![Advanced options](img/driver-advanced-options.png) | ![Startup Settings, Restart button](img/driver-startup-settings.png) |

| Press 7 |
|---|
| ![Startup Settings: press 7, Disable driver signature enforcement](img/driver-press-7.png) |

This setting lasts only for that one start of Windows, which is enough: the install in step 4 needs it once.

### 4. Plug in the Dyna Pro and point Windows at the driver folder

1. Connect the Dyna Pro by USB and switch it on.
2. Press **Win + X** and open **Device Manager**.
3. Under **Other devices**, right-click the Dyna Pro device and choose **Update driver**.
4. Choose **Browse my computer for drivers**.
5. Click *Browse...* and pick the unzipped **Dyna Pro USB** folder; make sure **Include subfolders** is ticked.
6. Click **Next**.

| Right-click, Update driver | Browse my computer for drivers | Folder, Include subfolders, Next |
|---|---|---|
| ![Device Manager, right-click Update driver](img/driver-update-driver.png) | ![How do you want to search for drivers](img/driver-browse-my-computer.png) | ![Folder path with Include subfolders ticked](img/driver-folder-path.png) |

### 5. "Windows can't verify the publisher"

A red *Windows Security* window appears, and it appears **twice** (once for each of the two driver files, ftdibus and ftdiport). Click **Install this driver software anyway** each time.

| Install this driver software anyway |
|---|
| ![Windows Security: Install this driver software anyway](img/driver-install-anyway.png) |

### 6. Done

When Windows reports that the driver was installed, the device is listed under **Universal Serial Bus controllers**
as **DynaPro Dynamometer**.

| The device in Device Manager |
|---|
| ![DynaPro Dynamometer under Universal Serial Bus controllers](img/driver-device-installed.png) |

*Windows 11 (Traditional Chinese) in a VM with a simulated device. The device name under Other devices may differ on a real PC.*

## Troubleshooting

* **"The hash for the file is not present in the specified catalog file"** Step 3 was skipped, or Windows was restarted normally before step 4. Repeat step 3 and do step 4
  in that same session.

  | The error |
  |---|
  | ![Windows error: the hash for the file is not present in the specified catalog file](img/driver-hash-error.png) |

* **Startup Settings asks for a BitLocker recovery key.** Enter it (<https://aka.ms/myrecoverykey>), or restart
  normally, do step 2 and try again.
* **The device is not under *Other devices*.** Check the USB cable and that the Dyna Pro is switched on; unplug and
  plug it in again.

---

## 繁體中文

[English above](#installing-the-dyna-pro-usb-driver)

第一次把這台電腦接上 Dyna Pro 測功機時做一次。沒有驅動程式的話,Windows 會把裝置列在「其他裝置」,DynaRun 無法與它通訊。
DynaRunFix 不附驅動程式,驅動程式由 Dyna Pro 提供。使用者版說明見 [README](../README.md#使用方式)。

### 快速方式:使用安裝精靈

DynaRunFix 安裝程式可以幫你做完下面的步驟。除非它說做不到,建議直接用它;[手動方式](#步驟)仍然可用。

* 在主安裝程式中,保持勾選 **安裝 Dyna Pro USB 驅動程式**(預設勾選;驅動程式已安裝或 Windows 低於 10 時會隱藏)。
  之後隨時也可以用開始功能表捷徑 **安裝 Dyna Pro USB 驅動程式**,或執行 `DynaRunFix-Setup.exe /driver`。
* 精靈會從 dynapro.co.uk 下載官方的「Dyna Pro USB.zip」(不內附)並檢查 SHA-256。網站上的檔案若有變動就拒絕安裝,這時請改用手動方式。
* Windows 只會要求一次系統管理員權限(UAC)。若 BitLocker 保護為開啟,精靈會暫停它直到驅動程式安裝完成(最多 3 次重新啟動作為保險,之後自動恢復),設定一次性的進階開機選項,並登錄一個工作,在你登入後讓精靈繼續。
* 重新啟動後,在啟動選單按 **7**(停用驅動程式強制簽章)。精靈會顯示下面這個畫面,建議先用手機拍下來。

  | 按 7 |
  |---|
  | ![啟動設定:按 7,停用驅動程式強制簽章](img/driver-press-7.png) |

* 沒按到 7 的話,登入後精靈會告知,並提供 **再試一次**。
* 接著 Windows 會跳出紅色警告,請選 **仍然安裝此驅動程式軟體**。驅動程式強制簽章在下次正常重新啟動後恢復。

附註:需要 Windows 10 或 11。驅動程式檔案放在 `%ProgramData%\DynaRunFix\driver`,記錄檔是 `%ProgramData%\DynaRunFix\driver.log`。
解除安裝 DynaRunFix 不會移除驅動程式。已在 Windows 11(繁體中文)虛擬機器中以模擬裝置測試:重新啟動後直接出現「啟動設定」清單、錯過 7 後重試可正常運作、沒有裝置時驅動程式也能安裝到驅動程式存放區、之後新增的裝置在簽章檢查恢復後會自動套用該驅動程式。尚未在實體電腦、真實硬體或啟用 BitLocker 的環境測試。

如果出了問題:在精靈的重試或安裝頁面按 **取消**,會撤回登入工作、開機選項與 BitLocker 暫停。若是用其他方式關掉精靈、想把這些還原,
可以執行 `DynaRunFix-Setup.exe /driver-cancel`(會要求系統管理員權限;只移除工作、一次性開機選項與 BitLocker 暫停,不會安裝或移除驅動程式)。
下面的手動步驟不依賴精靈。

### 需要準備

* Dyna Pro 官網的驅動程式 zip:
  [Dyna Pro USB.zip](https://dynapro.co.uk/Site/graphics/Technical_Info/Software/Dyna%20Pro%20USB.zip)
* 約 10 分鐘與兩次重新啟動(一次進特殊啟動選單,一次回到正常模式)。

### 為什麼要多這幾步

這是 FTDI 的 FTDIBUS/FTDIPORT 驅動程式(2.08.24,2012 年),INF 被修改過,加入了 Dyna Pro 的裝置 ID
`USB\VID_0403&PID_FDBC`(「DynaPro Dynamometer」)。Microsoft 是在修改之前簽署驅動程式目錄的,
所以 Windows 會拒絕修改過的 INF:「指定的類別目錄檔案中沒有該檔案的雜湊」。要安裝就得在 Windows 啟動一次的期間停用驅動程式強制簽章。

### 步驟

#### 1. 下載並解壓縮

下載上面的 zip 並解壓縮(右鍵 →「全部解壓縮…」),例如解壓到 `C:\Users\<你>\Downloads\Dyna Pro USB`。
記住這個資料夾,步驟 4 要選它。

#### 2. 只有電腦使用 BitLocker 或裝置加密時:先暫停到驅動程式安裝完成

加密的電腦在步驟 3 的啟動選單會要求輸入 BitLocker 復原金鑰。先暫停 BitLocker 就能避免。
以**系統管理員身分開啟 PowerShell**(對開始按鈕按右鍵 →「終端機 (系統管理員)」),執行:

```powershell
Suspend-BitLocker -MountPoint "C:" -RebootCount 3
```

最多 3 次重新啟動之後保護會自動恢復;驅動程式安裝完成後也可以用 `Resume-BitLocker -MountPoint "C:"` 立刻恢復。如果還是被要求輸入復原金鑰,可以到 <https://aka.ms/myrecoverykey> 查詢(登入與這台電腦綁定的 Microsoft 帳號)。

不確定有沒有加密的話,直接執行這行指令也無妨。

#### 3. 停用驅動程式強制簽章後重新啟動

按「開始」→ 電源按鈕,按住 **Shift** 再按 **重新啟動**。然後:

1. 「選擇選項」→ **疑難排解**
2. **進階選項** → **啟動設定** → **重新啟動**
3. 重新啟動後按 **7**(停用驅動程式強制簽章)。

| 1. 選擇選項 → 疑難排解 | 2. 進階選項 | 3. 啟動設定 → 重新啟動 |
|---|---|---|
| ![選擇選項](img/driver-choose-an-option.png) | ![進階選項](img/driver-advanced-options.png) | ![啟動設定與「重新啟動」按鈕](img/driver-startup-settings.png) |

| 按 7 |
|---|
| ![啟動設定:按 7,停用驅動程式強制簽章](img/driver-press-7.png) |

這個設定只在這一次的 Windows 啟動期間有效,這樣就夠了:步驟 4 的安裝只需要它一次。

#### 4. 接上 Dyna Pro,指定驅動程式資料夾

1. 用 USB 接上 Dyna Pro 並開機。
2. 按 **Win + X**,開啟 **裝置管理員**。
3. 在 **其他裝置** 底下對 Dyna Pro 裝置按右鍵,選 **更新驅動程式**。
4. 選 **瀏覽電腦上的驅動程式**。
5. 按「瀏覽…」選剛解壓縮的 **Dyna Pro USB** 資料夾,確認已勾選 **包括子資料夾**。
6. 按 **下一步**。

| 按右鍵 → 更新驅動程式 | 瀏覽電腦上的驅動程式 | 資料夾、包括子資料夾、下一步 |
|---|---|---|
| ![裝置管理員,右鍵更新驅動程式](img/driver-update-driver.png) | ![您要如何搜尋驅動程式](img/driver-browse-my-computer.png) | ![資料夾路徑與已勾選的「包括子資料夾」](img/driver-folder-path.png) |

#### 5. 「Windows 無法驗證此驅動程式軟體的發行者」

會出現紅色的「Windows 安全性」視窗,而且會出現兩次(兩個驅動程式檔 ftdibus 與 ftdiport 各一次),每次都按 **仍然安裝此驅動程式軟體**。

| 仍然安裝此驅動程式軟體 |
|---|
| ![Windows 安全性:仍然安裝此驅動程式軟體](img/driver-install-anyway.png) |

#### 6. 完成

Windows 顯示驅動程式安裝完成後,裝置會列在 **通用序列匯流排控制器** 底下,名稱為 **DynaPro Dynamometer**。

| 裝置管理員裡的裝置 |
|---|
| ![通用序列匯流排控制器底下的 DynaPro Dynamometer](img/driver-device-installed.png) |

*虛擬機裡的 Win11(繁體中文),裝置為模擬。真實電腦上「其他裝置」底下的裝置名稱可能不同。*

### 疑難排解

* **「指定的類別目錄檔案中沒有該檔案的雜湊…」** 表示跳過了步驟 3,
  或是在步驟 4 之前已經正常重新啟動過。重做步驟 3,並在同一次啟動內做步驟 4。

  | 錯誤畫面 |
  |---|
  | ![Windows 錯誤:指定的類別目錄檔案中沒有該檔案的雜湊](img/driver-hash-error.png) |

* **啟動設定要求輸入 BitLocker 復原金鑰。** 輸入金鑰(<https://aka.ms/myrecoverykey>),或正常重新啟動、做完步驟 2 再試一次。
* **「其他裝置」底下沒有裝置。** 檢查 USB 線與 Dyna Pro 是否已開機,拔掉再插一次。
