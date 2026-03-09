# Uninstall-Driver.ps1
# Script para desinstalar o driver AMD BC-250 do Windows

param(
    [string]$DriverInfName = "amdbc250.inf"
)

Write-Host "Iniciando a desinstalação do driver AMD BC-250..."

# Desinstalar o driver usando pnputil
Write-Host "Deletando o pacote do driver: $DriverInfName"
pnputil /delete-driver $DriverInfName /uninstall /force

if ($LASTEXITCODE -eq 0) {
    Write-Host "Driver desinstalado com sucesso!"
} else {
    Write-Error "Falha ao desinstalar o driver. Código de erro: $LASTEXITCODE"
}

Write-Host "Desinstalação concluída."
