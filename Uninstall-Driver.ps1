#Requires -RunAsAdministrator
#Requires -Version 5.1

<#
.SYNOPSIS
    Uninstalls the AMD BC-250 WDDM display driver from Windows.

.DESCRIPTION
    Removes the AMD BC-250 driver service, driver files, and registry entries.

.EXAMPLE
    .\Uninstall-Driver.ps1
#>

[CmdletBinding(SupportsShouldProcess)]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SERVICE_NAME = "amdbc250kmd"
$KMD_FILENAME = "amdbc250kmd.sys"
$UMD_FILENAME = "amdbc250umd64.dll"
$UMD_FILENAME2 = "amdbc250umd.dll"

Write-Host ""
Write-Host "AMD BC-250 Driver Uninstaller" -ForegroundColor Cyan
Write-Host "=============================" -ForegroundColor Cyan
Write-Host ""

if ($PSCmdlet.ShouldProcess("AMD BC-250 Driver", "Uninstall")) {

    # Stop and remove service
    Write-Host "[*] Stopping driver service..." -ForegroundColor Yellow
    Stop-Service -Name $SERVICE_NAME -Force -ErrorAction SilentlyContinue
    & sc.exe delete $SERVICE_NAME 2>&1 | Out-Null
    Write-Host "[+] Service removed" -ForegroundColor Green

    # Remove driver files
    Write-Host "[*] Removing driver files..." -ForegroundColor Yellow
    $filesToRemove = @(
        "$env:SystemRoot\System32\drivers\$KMD_FILENAME",
        "$env:SystemRoot\System32\$UMD_FILENAME",
        "$env:SystemRoot\System32\$UMD_FILENAME2"
    )

    foreach ($file in $filesToRemove) {
        if (Test-Path $file) {
            Remove-Item $file -Force -ErrorAction SilentlyContinue
            Write-Host "[+] Removed: $file" -ForegroundColor Green
        }
    }

    # Remove from driver store
    Write-Host "[*] Removing from driver store..." -ForegroundColor Yellow
    $infFiles = & pnputil /enum-drivers 2>&1 | Select-String "amdbc250"
    if ($infFiles) {
        foreach ($inf in $infFiles) {
            $infName = ($inf -split "\s+")[2]
            & pnputil /delete-driver $infName /uninstall 2>&1 | Out-Null
        }
    }

    Write-Host ""
    Write-Host "[+] Uninstallation complete. Please reboot." -ForegroundColor Green
    Write-Host ""
}
