$ErrorActionPreference = "Stop"

$toolsVolume = Get-Volume -FileSystemLabel "WPLAB_TOOLS"
$source = "$($toolsVolume.DriveLetter):\"
$destination = "C:\pointer-lab"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item "${source}windows-pointer-windows-capture.exe" $destination -Force
Copy-Item "${source}Run-Capture.ps1" $destination -Force
Copy-Item "${source}Console.ps1" $destination -Force
Copy-Item "${source}Repair-PointerLab.ps1" $destination -Force
& icacls.exe $destination /grant "lab:(OI)(CI)F" /T | Out-Null

Set-ItemProperty "HKCU:\Control Panel\Mouse" -Name MouseSensitivity -Value "10"
Set-ItemProperty "HKCU:\Control Panel\Mouse" -Name MouseSpeed -Value "1"
Set-ItemProperty "HKCU:\Control Panel\Mouse" -Name MouseThreshold1 -Value "6"
Set-ItemProperty "HKCU:\Control Panel\Mouse" -Name MouseThreshold2 -Value "10"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PointerLabSystemParameters {
    [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW", SetLastError = true)]
    public static extern bool SetMouse(
        uint action, uint parameter, int[] values, uint flags);
    [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW", SetLastError = true)]
    public static extern bool SetSpeed(
        uint action, uint parameter, IntPtr value, uint flags);
}
"@
$mouse = [int[]](6, 10, 1)
if (-not [PointerLabSystemParameters]::SetMouse(0x0004, 0, $mouse, 3)) {
    throw "SPI_SETMOUSE failed"
}
if (-not [PointerLabSystemParameters]::SetSpeed(0x0071, 0, [IntPtr]10, 3)) {
    throw "SPI_SETMOUSESPEED failed"
}

powercfg.exe /change monitor-timeout-ac 0
powercfg.exe /change standby-timeout-ac 0

New-Item `
    "HKLM:\SYSTEM\CurrentControlSet\Control\BitLocker" `
    -Force | Out-Null
Set-ItemProperty `
    "HKLM:\SYSTEM\CurrentControlSet\Control\BitLocker" `
    -Name PreventDeviceEncryption `
    -Type DWord `
    -Value 1

$winlogon = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon"
Set-ItemProperty $winlogon -Name AutoAdminLogon -Value "1"
Set-ItemProperty $winlogon -Name ForceAutoLogon -Value "1"
Set-ItemProperty $winlogon -Name DefaultUserName -Value "lab"
Set-ItemProperty $winlogon -Name DefaultPassword -Value "PointerLab1!"
Remove-ItemProperty $winlogon -Name AutoLogonCount -ErrorAction SilentlyContinue

$startup = [Environment]::GetFolderPath("Startup")
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut("${startup}\Pointer Lab Console.lnk")
$shortcut.TargetPath = "C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"
$shortcut.Arguments = "-NoExit -ExecutionPolicy Bypass -File C:\pointer-lab\Console.ps1"
$shortcut.WorkingDirectory = $destination
$shortcut.Save()
New-Item `
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
    -Force | Out-Null
Set-ItemProperty `
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
    -Name PointerLabConsole `
    -Value 'powershell.exe -NoExit -ExecutionPolicy Bypass -File C:\pointer-lab\Console.ps1'

@"
Pointer Lab

This disposable VM is controlled by the Debian host.
The visible Pointer Lab PowerShell window receives automated capture commands.
No host USB controller is required for deterministic tests.
"@ | Set-Content "${destination}\README.txt"

Enable-PSRemoting -SkipNetworkProfileCheck -Force
Set-Service WinRM -StartupType Automatic
Remove-NetFirewallRule `
    -DisplayName "Pointer Lab WinRM" `
    -ErrorAction SilentlyContinue
New-NetFirewallRule `
    -DisplayName "Pointer Lab WinRM" `
    -Direction Inbound `
    -Action Allow `
    -Protocol TCP `
    -LocalPort 5985 `
    -Profile Any | Out-Null

Set-Content "${destination}\setup-complete.txt" "ready"
