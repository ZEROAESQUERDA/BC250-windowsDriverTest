# Coleta de diagnóstico do BC-250 no Windows 10 x64.
# Execute em PowerShell elevado em uma máquina de teste.

$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $PWD "captures\$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null

"=== PnP display devices ===" | Out-File (Join-Path $out 'pnp-display.txt')
Get-PnpDevice -Class Display -PresentOnly |
    Format-List * |
    Out-File (Join-Path $out 'pnp-display.txt') -Append

"=== Hardware IDs and locations ===" | Out-File (Join-Path $out 'hardware-ids.txt')
Get-PnpDevice -Class Display -PresentOnly | ForEach-Object {
    "InstanceId: $($_.InstanceId)" | Out-File (Join-Path $out 'hardware-ids.txt') -Append
    Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_HardwareIds' |
        Format-List * | Out-File (Join-Path $out 'hardware-ids.txt') -Append
    Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_LocationInfo' |
        Format-List * | Out-File (Join-Path $out 'hardware-ids.txt') -Append
}

"=== Win32 video controller ===" | Out-File (Join-Path $out 'video-controller.txt')
Get-CimInstance Win32_VideoController |
    Format-List * |
    Out-File (Join-Path $out 'video-controller.txt')

"=== DirectX diagnostic ===" | Out-File (Join-Path $out 'dxdiag.txt')
& dxdiag.exe /whql:off /t (Join-Path $out 'dxdiag.txt') | Out-Null

"=== Driver store display packages ===" | Out-File (Join-Path $out 'pnputil-display.txt')
pnputil.exe /enum-drivers |
    Select-String -Pattern 'Display|AMD|Provider|Published Name|Original Name' |
    Out-File (Join-Path $out 'pnputil-display.txt')

"=== SetupAPI tail ===" | Out-File (Join-Path $out 'setupapi-tail.txt')
Get-Content "$env:windir\inf\setupapi.dev.log" -Tail 500 |
    Out-File (Join-Path $out 'setupapi-tail.txt')

"=== WDDM registry values ===" | Out-File (Join-Path $out 'wddm-registry.txt')
Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' |
    ForEach-Object { Get-ItemProperty $_.PSPath } |
    Out-File (Join-Path $out 'wddm-registry.txt')

Write-Host "Coleta concluída em $out"
