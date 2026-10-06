# Playbook: a legacy program works normally but hangs or fails "as administrator"

[繁體中文在下方](#繁體中文)

This pattern applies to many VB5/VB6, MFC/ATL and other ActiveX-based programs from the XP era, not just
DynaRun V3.

## Symptom

* Started normally, the program works.
* Started with **Run as administrator** (often needed for setup or calibration screens), it stops during
  start-up, shows an empty form, or reports "ActiveX component can't create object" / error 429 — or, as
  DynaRun V3 does, swallows the error and waits forever ("System Initializing. Please Wait. 115").
* The same thing may happen on Windows 7 with UAC enabled; on XP (administrator account, no UAC) it does not.

## Cause

1. The installer ran **without elevation**, so `regsvr32`/`DllRegisterServer` wrote the controls' COM
   registration under `HKCU\Software\Classes` (per user) instead of `HKLM\SOFTWARE\Classes`.
2. A normal process sees `HKCR` = HKCU merged over HKLM, so it finds the classes.
3. An **elevated** process ignores per-user class registrations (a protection against elevation hijacking),
   so it sees only HKLM. The class is missing → object creation fails.

Two details make the obvious fixes fail:

* **Running `regsvr32` elevated does not help.** While the class key exists under HKCU, writes through HKCR go
  to HKCU again. VB6's runtime even re-registers missing controls automatically — into HKCU.
* **A key in HKLM is not enough.** HKLM may already contain the CLSID key from another product but without an
  `InprocServer32` path. Elevated processes then still fail. Check completeness, not existence.

## Diagnose

1. Process Monitor, filter *Process Name is <app>.exe*, capture one normal and one elevated start (both
   started by you from Explorer, not from inside a packaged/sandboxed app, which can redirect AppData).
2. In the elevated run find where activity stops. Shortly before that you will see
   `RegOpenKey HKCR\WOW6432Node\CLSID\{...}` → `NAME NOT FOUND` (or `InprocServer32\(Default)` → `NAME NOT FOUND`)
   for a CLSID that the normal run opened successfully under `HKCU\Software\Classes\...`.
3. Confirm: the CLSID exists under `HKCU\Software\Classes\WOW6432Node\CLSID\{...}` but
   `HKLM\SOFTWARE\Classes\WOW6432Node\CLSID\{...}\InprocServer32` has no default value.

## Fix

From an elevated PowerShell:

```
# preview
.\tools\register-machine-wide.ps1 -FromExe 'C:\Program Files (x86)\Vendor\App.exe' -WhatIf
# apply (writes a .reg backup of any HKLM key it overwrites and a log of copied keys)
.\tools\register-machine-wide.ps1 -FromExe 'C:\Program Files (x86)\Vendor\App.exe'
```

`-FromExe` lists the OCX/DLL files the executable references (VB form data names every control's file) and
skips Windows' own DLLs. For each per-user CLSID served by those files, the CLSID key, its ProgIDs and its
TypeLib are copied from HKCU to HKLM when HKLM lacks a complete registration. Alternatively pass the files
explicitly with `-Files a.ocx,b.ocx`.

Verify by starting the program as administrator again. If it still stops, capture again: the next missing
class will show up the same way.

## Undo

The script writes `hklm-classes-copied-<time>.txt` (keys it created or overwrote) and, if it overwrote existing
HKLM keys, `hklm-classes-backup-<time>.reg`. To undo: delete the listed keys with `reg delete`, then import the
backup with `reg import`.

## Why not reinstall elevated?

That is the cleanest fix when the original installer is available and behaves: run the installer with
**Run as administrator** (for MSI packages, `msiexec /i package.msi ALLUSERS=1`). The script is for when
reinstalling is not possible or would reset data/licensing.

---

## 繁體中文

### 典型症狀
程式一般執行正常,但**以系統管理員身分執行**(常常是進設定或校正畫面時需要)就卡在啟動階段、表單空白,
或出現「ActiveX 元件無法建立物件」/ 錯誤 429。DynaRun V3 的情況是程式吞掉錯誤後停在「Please Wait. 115」。
Win7 開著 UAC 也可能發生;XP 用管理員帳號、沒有 UAC 時則不會。

### 原因
1. 安裝程式以**一般權限**執行,所以元件的 COM 註冊寫在 `HKCU\Software\Classes`(只限目前使用者),沒有寫進 `HKLM`。
2. 一般權限的程式看到的 HKCR 是 HKCU 疊在 HKLM 上的合併結果,所以找得到元件。
3. **以系統管理員執行的程式會忽略 HKCU 的類別註冊**(防止被用來提權),只看 HKLM,所以找不到元件,建立物件失敗。

兩個常見的誤區:
* **以系統管理員身分跑 `regsvr32` 沒用**:只要 HKCU 已經有這個類別,透過 HKCR 的寫入就會落回 HKCU。VB6 執行階段自動重新註冊時也一樣寫進 HKCU。
* **HKLM 有機碼不代表註冊完整**:HKLM 可能有其他軟體留下、卻沒有 `InprocServer32` 路徑的同一個 CLSID,系統管理員程式照樣失敗。要檢查是否完整,不能只看機碼在不在。

### 診斷
用 Process Monitor 過濾該程式,一般權限和系統管理員各錄一次啟動(兩次都要從檔案總管自己啟動,不要從封裝或沙箱程式裡啟動,否則 AppData 可能被重新導向)。
找出系統管理員那次停止活動的位置,往前會看到某個 `CLSID\{...}` 回傳 `NAME NOT FOUND`,而一般權限那次是在 `HKCU\Software\Classes\...` 成功讀到的。

### 修正
以系統管理員開 PowerShell:
`.\tools\register-machine-wide.ps1 -FromExe '程式路徑.exe' -WhatIf` 先預覽,確認後拿掉 `-WhatIf` 執行。
腳本會從 exe 找出用到的 OCX/DLL(排除 Windows 自己的 DLL),把缺少或不完整的 CLSID、ProgID、TypeLib 從 HKCU 複製到 HKLM,
覆寫前會先匯出備份 `.reg`,並留下複製清單。修完後再以系統管理員執行;如果還卡,就再錄一次,下一個缺的元件會用同樣的方式出現。

### 還原
依照 `hklm-classes-copied-<時間>.txt` 用 `reg delete` 刪掉列出的機碼,再用 `reg import` 匯入 `hklm-classes-backup-<時間>.reg`(如果有的話)。

### 能重新安裝的話
原廠安裝程式還在而且正常的話,最乾淨的做法是**以系統管理員身分重新執行安裝程式**(MSI 套件:`msiexec /i 套件.msi ALLUSERS=1`)。
這支腳本是給無法重裝,或重裝會重置資料或授權的情況用的。
