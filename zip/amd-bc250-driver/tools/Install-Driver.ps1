#Requires -RunAsAdministrator
#Requires -Version 5.1

<#
.SYNOPSIS
    Installs the AMD BC-250 WDDM display driver on Windows.

.DESCRIPTION
    This script automates the installation of the AMD BC-250 Kernel-Mode
    Display Miniport Driver (amdbc250kmd.sys) and User-Mode Display Driver
    (amdbc250umd64.dll) using Windows PnP utilities.

    IMPORTANT: This driver is experimental and unsigned. You must enable
    test signing mode before installation, or sign the driver with a valid
    certificate.

.PARAMETER DriverPath
    Path to the directory containing the driver files (.sys, .dll, .inf).
    Defaults to the directory containing this script.

.PARAMETER EnableTestSigning
    If specified, enables Windows test signing mode (requires reboot).

.PARAMETER Force
    Forces reinstallation even if the driver is already installed.

.EXAMPLE
    # Enable test signing and install
    .\Install-Driver.ps1 -EnableTestSigning -Force

.EXAMPLE
    # Install from a specific path
    .\Install-Driver.ps1 -DriverPath "C:\AMD-BC250-Driver"

.NOTES
    Author: AMD BC-250 Driver Project
    Version: 1.0.100.0
    Date: 2026-03-09

    Requirements:
    - Windows 10 (1607 / Anniversary Update) or Windows 11
    - x64 (AMD64) architecture
    - AMD BC-250 APU installed and recognized by Windows PnP
    - Administrator privileges
    - Test signing enabled (for unsigned driver) or valid WHQL signature
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $false)]
    [string]$DriverPath = $PSScriptRoot,

    [Parameter(Mandatory = $false)]
    [switch]$EnableTestSigning,

    [Parameter(Mandatory = $false)]
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ============================================================================
# Constants
# ============================================================================

$DRIVER_NAME        = "AMD BC-250 WDDM Display Driver"
$DRIVER_VERSION     = "1.0.100.0"
$KMD_FILENAME       = "amdbc250kmd.sys"
$UMD_FILENAME       = "amdbc250umd64.dll"
$INF_FILENAME       = "amdbc250.inf"
$SERVICE_NAME       = "amdbc250kmd"

# AMD BC-250 PCI Hardware IDs
$HARDWARE_IDS = @(
    "PCI\VEN_1002&DEV_13FE",
    "PCI\VEN_1002&DEV_143F",
    "PCI\VEN_1002&DEV_13DB",
    "PCI\VEN_1002&DEV_13F9",
    "PCI\VEN_1002&DEV_13FA",
    "PCI\VEN_1002&DEV_13FB",
    "PCI\VEN_1002&DEV_13FC"
)

# ============================================================================
# Helper Functions
# ============================================================================

function Write-Header {
    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host "  $DRIVER_NAME v$DRIVER_VERSION" -ForegroundColor Cyan
    Write-Host "  AMD BC-250 APU (Cyan Skillfish / RDNA2)" -ForegroundColor Cyan
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step {
    param([string]$Message)
    Write-Host "[*] $Message" -ForegroundColor Yellow
}

function Write-Success {
    param([string]$Message)
    Write-Host "[+] $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "[-] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor Gray
}

function Test-AdminPrivileges {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-WindowsVersion {
    $os = Get-CimInstance Win32_OperatingSystem
    return @{
        Caption = $os.Caption
        Build   = [int]$os.BuildNumber
        Version = $os.Version
    }
}

function Test-TestSigningEnabled {
    $bcdedit = & bcdedit /enum "{current}" 2>&1
    return ($bcdedit | Select-String "testsigning\s+Yes" -Quiet)
}

function Enable-TestSigning {
    Write-Step "Enabling Windows test signing mode..."
    & bcdedit /set testsigning on | Out-Null
    Write-Success "Test signing enabled. A reboot is required."
    Write-Host ""
    Write-Host "  WARNING: Test signing allows loading of unsigned drivers." -ForegroundColor Yellow
    Write-Host "  This reduces system security. Only use for testing purposes." -ForegroundColor Yellow
    Write-Host ""
}

function Find-BC250Device {
    Write-Step "Scanning for AMD BC-250 APU..."

    foreach ($hwId in $HARDWARE_IDS) {
        $devices = Get-PnpDevice | Where-Object {
            $_.HardwareID -like "$hwId*"
        }

        if ($devices) {
            foreach ($device in $devices) {
                Write-Success "Found: $($device.FriendlyName)"
                Write-Info "  Instance ID: $($device.InstanceId)"
                Write-Info "  Status:      $($device.Status)"
                Write-Info "  Hardware ID: $($device.HardwareID[0])"
            }
            return $devices
        }
    }

    Write-Failure "No AMD BC-250 APU found in the system."
    Write-Info "Make sure the BC-250 board is properly installed and powered."
    Write-Info "Check Device Manager for unknown PCI devices with VEN_1002."
    return $null
}

function Test-DriverFiles {
    param([string]$Path)

    Write-Step "Verifying driver files..."

    $requiredFiles = @($KMD_FILENAME, $UMD_FILENAME, $INF_FILENAME)
    $allFound = $true

    foreach ($file in $requiredFiles) {
        $fullPath = Join-Path $Path $file
        if (Test-Path $fullPath) {
            $fileInfo = Get-Item $fullPath
            Write-Success "Found: $file ($([math]::Round($fileInfo.Length / 1KB, 1)) KB)"
        } else {
            Write-Failure "Missing: $file"
            $allFound = $false
        }
    }

    return $allFound
}

function Copy-DriverFiles {
    param([string]$SourcePath)

    Write-Step "Copying driver files to system directories..."

    $system32 = "$env:SystemRoot\System32"
    $drivers  = "$env:SystemRoot\System32\drivers"

    # Copy KMD
    $kmdSrc = Join-Path $SourcePath $KMD_FILENAME
    $kmdDst = Join-Path $drivers $KMD_FILENAME
    Copy-Item -Path $kmdSrc -Destination $kmdDst -Force
    Write-Success "Copied $KMD_FILENAME to $drivers"

    # Copy UMD
    $umdSrc = Join-Path $SourcePath $UMD_FILENAME
    $umdDst = Join-Path $system32 $UMD_FILENAME
    Copy-Item -Path $umdSrc -Destination $umdDst -Force
    Write-Success "Copied $UMD_FILENAME to $system32"

    # Also copy as amdbc250umd.dll (32-bit name alias)
    $umdDst32 = Join-Path $system32 "amdbc250umd.dll"
    Copy-Item -Path $umdSrc -Destination $umdDst32 -Force
    Write-Success "Copied amdbc250umd.dll to $system32"
}

function Install-DriverService {
    Write-Step "Installing kernel-mode driver service..."

    $driverPath = "$env:SystemRoot\System32\drivers\$KMD_FILENAME"

    # Remove existing service if present
    $existingService = Get-Service -Name $SERVICE_NAME -ErrorAction SilentlyContinue
    if ($existingService) {
        Write-Info "Removing existing service..."
        Stop-Service -Name $SERVICE_NAME -Force -ErrorAction SilentlyContinue
        & sc.exe delete $SERVICE_NAME | Out-Null
        Start-Sleep -Seconds 2
    }

    # Create new service
    & sc.exe create $SERVICE_NAME `
        type= kernel `
        start= demand `
        error= ignore `
        binpath= $driverPath `
        displayname= "AMD BC-250 Kernel Mode Driver" | Out-Null

    if ($LASTEXITCODE -eq 0) {
        Write-Success "Service '$SERVICE_NAME' created successfully"
    } else {
        Write-Failure "Failed to create service (error $LASTEXITCODE)"
        throw "Service creation failed"
    }
}

function Install-DriverINF {
    param([string]$InfPath)

    Write-Step "Installing driver via INF file..."

    # Use pnputil to install the driver package
    $result = & pnputil /add-driver $InfPath /install 2>&1

    if ($LASTEXITCODE -eq 0) {
        Write-Success "Driver package installed via PnPUtil"
    } else {
        Write-Failure "PnPUtil installation failed (error $LASTEXITCODE)"
        Write-Info "Output: $result"
        Write-Info "Trying manual installation..."

        # Fallback: use devcon or manual registry
        Install-DriverService
    }
}

function Register-UserModeDriver {
    Write-Step "Registering user-mode driver in registry..."

    # Find the device class registry key
    $classKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}"

    # Find the BC-250 device instance key
    $deviceKeys = Get-ChildItem $classKey -ErrorAction SilentlyContinue |
        Where-Object {
            $driverDesc = (Get-ItemProperty $_.PSPath -Name "DriverDesc" -ErrorAction SilentlyContinue).DriverDesc
            $driverDesc -like "*BC-250*" -or $driverDesc -like "*Cyan Skillfish*"
        }

    if ($deviceKeys) {
        foreach ($key in $deviceKeys) {
            Set-ItemProperty -Path $key.PSPath -Name "UserModeDriverName" -Value "amdbc250umd.dll"
            Set-ItemProperty -Path $key.PSPath -Name "UserModeDriverNameWow" -Value "amdbc250umd.dll"
            Write-Success "Registered UMD for device at: $($key.PSPath)"
        }
    } else {
        Write-Info "Device registry key not found yet (will be created on first boot)"
    }
}

function Start-DriverService {
    Write-Step "Starting driver service..."

    try {
        Start-Service -Name $SERVICE_NAME -ErrorAction Stop
        Write-Success "Driver service started successfully"
    } catch {
        Write-Failure "Failed to start service: $_"
        Write-Info "The service may start automatically on next reboot."
    }
}

function Show-DeviceStatus {
    Write-Step "Checking device status after installation..."

    $devices = Find-BC250Device
    if ($devices) {
        foreach ($device in $devices) {
            $status = $device.Status
            if ($status -eq "OK") {
                Write-Success "Device is working properly!"
            } elseif ($status -eq "Error") {
                Write-Failure "Device has an error. Check Device Manager for details."
            } else {
                Write-Info "Device status: $status"
            }
        }
    }
}

# ============================================================================
# Main Installation Flow
# ============================================================================

function Main {
    Write-Header

    # Check prerequisites
    if (-not (Test-AdminPrivileges)) {
        Write-Failure "This script must be run as Administrator."
        Write-Info "Right-click PowerShell and select 'Run as Administrator'"
        exit 1
    }

    $winVer = Get-WindowsVersion
    Write-Info "OS: $($winVer.Caption) (Build $($winVer.Build))"

    if ($winVer.Build -lt 14393) {
        Write-Failure "Windows 10 Anniversary Update (Build 14393) or later is required."
        exit 1
    }

    Write-Success "Prerequisites check passed"
    Write-Host ""

    # Enable test signing if requested
    if ($EnableTestSigning) {
        if (-not (Test-TestSigningEnabled)) {
            Enable-TestSigning
            Write-Host ""
            $reboot = Read-Host "Reboot now to apply test signing? (Y/N)"
            if ($reboot -eq "Y" -or $reboot -eq "y") {
                Restart-Computer -Force
                return
            }
        } else {
            Write-Success "Test signing is already enabled"
        }
    } else {
        if (-not (Test-TestSigningEnabled)) {
            Write-Host ""
            Write-Host "  NOTICE: Test signing is not enabled." -ForegroundColor Yellow
            Write-Host "  The driver is not WHQL-signed and may fail to load." -ForegroundColor Yellow
            Write-Host "  Run with -EnableTestSigning to enable test signing." -ForegroundColor Yellow
            Write-Host ""
        }
    }

    # Find BC-250 device
    $device = Find-BC250Device
    if (-not $device -and -not $Force) {
        Write-Host ""
        Write-Failure "Aborting installation: AMD BC-250 not found."
        Write-Info "Use -Force to install anyway (for pre-staging)."
        exit 1
    }
    Write-Host ""

    # Verify driver files
    if (-not (Test-DriverFiles -Path $DriverPath)) {
        Write-Failure "Required driver files are missing from: $DriverPath"
        Write-Info "Please build the driver first or specify the correct path."
        exit 1
    }
    Write-Host ""

    # Install driver
    if ($PSCmdlet.ShouldProcess("AMD BC-250 Driver", "Install")) {

        Copy-DriverFiles -SourcePath $DriverPath

        $infPath = Join-Path $DriverPath $INF_FILENAME
        Install-DriverINF -InfPath $infPath

        Register-UserModeDriver

        Start-DriverService

        Write-Host ""
        Show-DeviceStatus
    }

    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Success "Installation complete!"
    Write-Host ""
    Write-Info "If the GPU is not working, try rebooting the system."
    Write-Info "Check Device Manager > Display Adapters for the BC-250."
    Write-Info "For issues, see the driver documentation in docs\README.md"
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host ""
}

# Run main function
Main
