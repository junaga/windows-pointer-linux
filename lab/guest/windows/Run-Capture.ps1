$ErrorActionPreference = "Stop"

$root = "C:\pointer-lab"
$capture = "${root}\capture"
$launcherLog = "${root}\launcher.log"
$duration = 30
Set-Content $launcherLog "capture launcher started $(Get-Date -Format o)"

try {
    if (Test-Path "${root}\duration.txt") {
        $requested = [int](Get-Content "${root}\duration.txt" -TotalCount 1)
        if ($requested -ge 1 -and $requested -le 3600) {
            $duration = $requested
        }
    }

    Remove-Item "${root}\ready.txt" -Force -ErrorAction SilentlyContinue
    Remove-Item "${root}\complete.txt" -Force -ErrorAction SilentlyContinue
    Remove-Item $capture -Recurse -Force -ErrorAction SilentlyContinue

    & "${root}\windows-pointer-windows-capture.exe" `
        --output $capture `
        --ready-file "${root}\ready.txt" `
        --seconds $duration

    if ($LASTEXITCODE -ne 0) {
        throw "capture exited with code $LASTEXITCODE"
    }
    Set-Content "${root}\complete.txt" "complete"
    Add-Content $launcherLog "capture completed $(Get-Date -Format o)"
} catch {
    Add-Content $launcherLog ($_ | Format-List * -Force | Out-String)
    throw
}
