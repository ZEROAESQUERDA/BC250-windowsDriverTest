# Driver Windows para AMD BC-250

Este repositório reúne o trabalho que estou fazendo para tentar levar a AMD BC-250 para o Windows 10 x64 com um driver WDDM próprio. A BC-250 não recebeu um driver oficial da AMD para Windows, então estou usando o `amdgpu` do Linux como referência de hardware e reimplementando os contratos necessários no modelo WDDM, em vez de tentar reaproveitar diretamente código de kernel Linux.

O objetivo do projeto é chegar a um caminho de aceleração que permita ao Windows enxergar a GPU, reconhecer a memória UMA compartilhada e conversar com os engines GFX/SDMA por meio de rings, fences e comandos DirectX. A configuração atual já compila o projeto para o caminho full-WDDM experimental e instala o UMD junto com o KMD, mas ainda precisa ser compilada e testada em uma BC-250 real antes de qualquer uso normal.

> **Estado honesto:** eu ativei os caminhos de aceleração para continuar o bring-up, inclusive os offsets candidatos de GFX/SDMA. Isso significa que o driver tenta inicializar o hardware; não significa que a compatibilidade DX9, DX10, DX11 e DX12 esteja pronta para qualquer aplicação. O próprio README faz questão de deixar essa diferença registrada.

## O que existe hoje

O projeto contém um KMD Windows com `DRIVER_INITIALIZATION_DATA` e `DxgkInitialize`, descoberta de BARs, separação da BAR5 usada pelo mailbox SMN, consultas básicas ao SMU, modelo de memória UMA/aperture, alocação de rings e fences não pagináveis, programação experimental de GFX/SDMA, ISR/DPC, `QueryCurrentFence`, `Render`, `Patch`, `BuildPagingBuffer`, `SubmitCommand`, preempção e reset.

Também existe um UMD separado. Ele exporta as entradas `OpenAdapter`, `OpenAdapter10`, `OpenAdapter10_2`, `OpenAdapter11` e `OpenAdapter12`, com tabelas mínimas para permitir o bring-up da fronteira DirectX. As tabelas completas de recursos, shaders, estados, command lists, heaps, root signatures, PSO e sincronização ainda não estão implementadas; preencher apenas o nome do export não seria suficiente para oferecer uma implementação real de D3D10–D3D12.

Os blobs públicos Cyan Skillfish2 usados como referência ficam em `firmware/amdgpu/`, acompanhados por `firmware/manifest.txt` com tamanho e SHA-256. Depois da auditoria, retirei as flags que faziam o KMD tratar esses arquivos como carregados apenas porque estavam no repositório. A validação de imagem e o carregamento pelo PSP/CP são etapas diferentes; o fluxo efetivo de firmware ainda precisa ser integrado ao hardware.

Também corrigi a conclusão de fence. O submit agora coloca no ring um `INDIRECT_BUFFER` seguido de um `WRITE_DATA` candidato para que a própria GPU escreva a fence. O CPU não grava mais uma conclusão imediatamente após o WPTR. Fill/Discard/Map/Unmap de paging que ainda não possuem execução real retornam erro explícito, e os caminhos UMD que não têm tabelas completas retornam `E_NOTIMPL` em vez de anunciar uma capacidade que não conseguem cumprir.

## Organização do repositório

```text
driver_bc250/
├── README.md
├── bc250_kmd.sln
├── bc250_kmd.vcxproj
├── bc250_umd.vcxproj
├── inf/
│   ├── bc250_kmd.inf
│   └── bc250_umd.inf
├── src/
│   ├── hw/              # BARs, MMIO e descoberta runtime
│   ├── firmware/        # nomes, validação e política de blobs
│   ├── smu/             # mailbox SMN e consultas SMU
│   ├── memory/          # perfil UMA/aperture e VidMm
│   ├── gfx/             # engines, rings, fences e offsets candidatos
│   ├── kmd/             # callbacks WDDM do kernel driver
│   └── umd/             # DLL user-mode DirectX
├── firmware/amdgpu/     # blobs Cyan Skillfish2 públicos
├── docs/                # especificações, pesquisas e histórico
└── tools/               # validação estrutural do projeto
```

## Hardware tratado pelo port

O driver reconhece os IDs PCI Cyan Skillfish usados no projeto (`13DB`, `13F9`, `13FA`, `13FB`, `13FC`, `13FE` e `143F`). A implementação parte da família GC10/GFX10 mais próxima disponível no código público do amdgpu e trata a BC-250 como uma GPU integrada em arquitetura UMA. Os 16 GB citados nas documentações da placa não são anunciados automaticamente como uma faixa de VRAM local; o WDDM recebe o segmento/aperture que consegue fornecer durante a inicialização.

A BAR5 física documentada para o Skillfish2 é tratada como caminho do SMN, usando os offsets de índice/dados do mailbox. A BAR candidata de registradores GC/SDMA é mantida separada para evitar confundir o espaço do SMN com a aperture dos rings.

## Como compilar no Windows 10 x64

Eu não consigo executar o build do WDK dentro deste sandbox Linux. Para compilar, uso uma máquina Windows 10 x64 com Visual Studio e WDK instalados, abro o Developer Command Prompt e executo:

```powershell
cd driver_bc250
msbuild bc250_kmd.sln /m /p:Configuration=Release /p:Platform=x64
```

Os projetos já definem `BC250_ENABLE_FULL_WDDM=1`, `BC250_ENABLE_DX_UMD=1`, `BC250_GFX_OFFSETS_VALIDATED=1` e `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=1` em Debug e Release. O script `tools/validate_project.py` verifica XML, arquivos críticos, gates e registro do UMD, mas ele não substitui a compilação do WDK.

Para instalar em uma máquina de teste, primeiro é necessário assinar os binários de acordo com a política do Windows 10, habilitar test signing quando apropriado e instalar o INF com privilégios administrativos. Eu não recomendo instalar o driver em um sistema de trabalho antes de ter uma forma de recuperar o dispositivo caso a inicialização do ring gere TDR.

## O que eu verifico antes de considerar um avanço

A primeira verificação é estrutural: XML dos projetos, `git diff --check`, presença dos blobs e hashes do manifesto. A segunda é a compilação real no Visual Studio/WDK. A terceira acontece somente em uma BC-250: BARs traduzidas, leitura dos registradores, estado do SMU, clocks fora de GFXOFF, carregamento do microcode do CP, programação do ring, wrap do WPTR, fence escrita pela GPU, interrupção EOP/IH e reset/TDR. Sem esse teste, eu considero o port corrigido no nível de semântica e estrutura, mas ainda não comprovado como acelerador.

Só depois dessas etapas faz sentido avaliar aplicações DirectX. O UMD atual deixa a fronteira aberta para continuar o desenvolvimento, mas não deve anunciar uma implementação completa de D3D9–D3D12 enquanto as tabelas de funções e a tradução de comandos não estiverem preenchidas.

## Referências que usei

[1]: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd "Código amdgpu no kernel Linux"
[2]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h "Offsets públicos GC 10.1.0"
[3]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/asic_reg/sdma5/sdma5_4_2_2_offset.h "Offsets públicos SDMA5"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ "Referência D3DKMDDI do WDK"
[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ "Referência D3D10UMDDI do WDK"
[6]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ "Referência D3D12UMDDI do WDK"
[7]: https://github.com/tpn/winsdk-10/tree/master/Include/10.0.10240.0/um "Snapshot público de headers UMD do Windows SDK"

## Autor

**ZEROAESQUERDA**
