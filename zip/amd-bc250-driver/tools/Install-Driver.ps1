# Install-Driver.ps1
# Script para instalar o driver AMD BC-250 no Windows

param(
    [string]$DriverPath = ".\inf\amdbc250.inf"
)

Write-Host "Iniciando a instalação do driver AMD BC-250..."

# Verificar se o test signing está habilitado
$testSigningStatus = bcdedit /enum {current} | Select-String "testsigning"
if ($testSigningStatus -notlike "*Yes*") {
    Write-Warning "O Test Signing não está habilitado. O driver não assinado pode não ser carregado."
    Write-Warning "Para habilitar: bcdedit /set testsigning on e reinicie o sistema."
}

# Instalar o driver usando pnputil
Write-Host "Adicionando o pacote do driver: $DriverPath"
pnputil /add-driver $DriverPath /install

if ($LASTEXITCODE -eq 0) {
    Write-Host "Driver instalado com sucesso!"
} else {
    Write-Error "Falha ao instalar o driver. Código de erro: $LASTEXITCODE"
}

Write-Host "Instalação concluída."
