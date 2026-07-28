$ErrorActionPreference = "Stop"
$destination = "C:\pointer-lab"

& icacls.exe $destination /grant "lab:(OI)(CI)F" /T | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "icacls exited with code $LASTEXITCODE"
}

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

Enable-PSRemoting -SkipNetworkProfileCheck -Force
Set-Service WinRM -StartupType Automatic
Start-Service WinRM
Set-Content "${destination}\repair-complete.txt" "ready"
