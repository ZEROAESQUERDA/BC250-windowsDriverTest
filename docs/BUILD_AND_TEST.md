# Build e teste no Windows 10 x64

## Pré-requisitos

Use uma máquina de teste dedicada com Windows 10 x64, Visual Studio 2022, Windows Driver Kit compatível com o SDK instalado, símbolos de depuração e uma BC-250 cujo ID PCI tenha sido capturado previamente. O sistema deve possuir uma forma de recuperação caso o KMD cause bugcheck ou deixe o sistema sem vídeo.

O sandbox Linux não fornece MSBuild, WDK, Dxgkrnl ou uma BC-250 física. Portanto, este repositório pode ser revisado e empacotado aqui, mas a compilação e a validação do `.sys` precisam ocorrer no Windows.

## Build

Abra o **Developer Command Prompt for VS** com o ambiente do WDK e execute:

```bat
cd driver_bc250
msbuild bc250_kmd.sln /p:Configuration=Debug /p:Platform=x64 /m
```

O pacote esperado deve conter o `bc250_kmd.sys`, o INF e os artefatos de assinatura gerados pelo processo de driver package. Se o WDK rejeitar algum campo do projeto, preserve a estrutura do sample KMDOD oficial e ajuste apenas a versão de ferramentas instalada.

## Instalação de teste

Ative test-signing somente em uma instalação de teste e reinicie:

```bat
bcdedit /set testsigning on
shutdown /r /t 0
```

Instale o pacote pelo Device Manager ou com `pnputil`. Antes de substituir qualquer driver, crie um ponto de restauração e mantenha uma GPU de recuperação/console remoto disponível. O INF deste projeto associa apenas os IDs Cyan Skillfish conhecidos e não deve ser aplicado a uma GPU AMD genérica.

## Diagnóstico

Após cada tentativa, execute `tools\collect_windows_bringup.ps1` como administrador. Preserve `setupapi.dev.log`, Event Viewer/System, o estado do Device Manager e a saída do `dxdiag`. Registre o ID PCI, revisão, BARs, código de erro e o último estágio do bring-up.

O primeiro marco do código atual ainda retorna `STATUS_NOT_SUPPORTED` ao final do `StartDevice` de propósito. Isso permite revisar o caminho de recursos e logs sem anunciar ao Dxgkrnl um adaptador acelerado cujas filas, memória e reset ainda não existem. O próximo patch deve somente mudar esse retorno depois de integrar a validação de ID, firmware e estados de IP.

## Critério para habilitar aceleração

Não habilite DirectX apenas porque o dispositivo aparece no Device Manager. O driver só deve avançar quando houver, em ordem, recursos MMIO válidos, inicialização de firmware, memória UMA modelada, uma fila GFX, fence monotônica, interrupção/DPC, reset/TDR e um teste de clear/copy verificado. Depois disso, o UMD Direct3D/Vulkan poderá ser conectado.
