# Installing the Dyna Pro USB driver

[繁體中文在下方](#繁體中文)

Do this once on a PC that has never been connected to a Dyna Pro dynamometer. Without the driver, Windows lists the
device under *Other devices* and DynaRun cannot talk to it. DynaRunFix does not bundle the driver; it comes from
Dyna Pro. The user-facing summary is in the [README](../README.md#usage).

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

### 2. Only if the PC uses BitLocker or device encryption: suspend it for one restart

The start-up menu in step 3 asks for the BitLocker recovery key on encrypted PCs. Avoid that by suspending BitLocker
for one restart first. Open **PowerShell as administrator** (right-click Start, *Terminal (Admin)*) and run:

```powershell
Suspend-BitLocker -MountPoint "C:" -RebootCount 1
```

Protection resumes by itself after that restart. If the recovery key is asked anyway, you can find it at
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

A red *Windows Security* window appears. Click **Install this driver software anyway**.

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

#### 2. 只有電腦使用 BitLocker 或裝置加密時:先暫停一次

加密的電腦在步驟 3 的啟動選單會要求輸入 BitLocker 復原金鑰。先暫停 BitLocker 一次重新啟動就能避免。
以**系統管理員身分開啟 PowerShell**(對開始按鈕按右鍵 →「終端機 (系統管理員)」),執行:

```powershell
Suspend-BitLocker -MountPoint "C:" -RebootCount 1
```

那次重新啟動之後保護會自動恢復。如果還是被要求輸入復原金鑰,可以到 <https://aka.ms/myrecoverykey> 查詢(登入與這台電腦綁定的 Microsoft 帳號)。

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

會出現紅色的「Windows 安全性」視窗,按 **仍然安裝此驅動程式軟體**。

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
