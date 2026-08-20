# Driver Windows para AMD BC-250

Este repositório reúne o trabalho que estou fazendo para levar a AMD BC-250 para o Windows 10 x64 com um driver WDDM próprio. A BC-250 não recebeu um driver oficial da AMD para Windows, então uso o `amdgpu` do Linux como referência de hardware e reimplemento os contratos necessários no modelo WDDM, em vez de tentar reaproveitar diretamente código de kernel Linux. [1]

Meu objetivo é chegar a um caminho de aceleração que permita ao Windows enxergar a GPU, reconhecer corretamente a memória UMA compartilhada e conversar com os engines GFX/SDMA por meio de rings, fences e comandos DirectX. A configuração atual já contém o caminho full-WDDM experimental, a fronteira UMD e o modelo de memória, mas ainda precisa ser compilada no WDK e testada em uma BC-250 real antes de qualquer uso normal.

> **Estado honesto:** eu deixei os caminhos de aceleração e as entradas DirectX ativos para continuar o bring-up, mas não trato offsets candidatos, firmware presente ou exports de DLL como aceleração comprovada. Os gates de offsets GFX/SDMA e de interrupções IH continuam em `0`; o KMD inicia com firmware e engine marcados como não prontos. Portanto, este repositório contém uma base funcional de desenvolvimento, não um driver acabado para aplicações DirectX.

## O que existe hoje

O KMD registra `DRIVER_INITIALIZATION_DATA` e chama `DxgkInitialize`. Ele descobre os recursos PCI traduzidos, mapeia as BARs, mantém a BAR5 documentada para o caminho do mailbox SMN, executa consultas básicas ao SMU, inicializa o modelo UMA/aperture e prepara as estruturas de rings e fences para GFX/Compute/SDMA0/SDMA1. As DDIs full-WDDM de criação de device, contexto e allocation, `Render`, `Patch`, paging, `SubmitCommand`, `QueryCurrentFence`, preempção, reset e TDR estão registradas para continuar o desenvolvimento do port.

Os rings e fences são alocados em memória não paginável fisicamente contígua. O caminho de submit prepara um `PACKET3(INDIRECT_BUFFER)` seguido de um `PACKET3(WRITE_DATA)`. Para a fence GPU-side, o controle usado é `WRITE_DATA_DST_SEL(5) | WR_CONFIRM`, exposto no código como `BC250_PM4_WRITE_DATA_FENCE_CONTROL`, seguindo a forma observada no teste de fence GFX10 do amdgpu. O CPU não escreve uma conclusão artificial logo depois do WPTR: a conclusão só deve ser observada quando a própria GPU escrever o valor da fence.

A preparação dos rings ainda não significa que o engine esteja ativo. O KMD mantém `FirmwareLoaded`, `FirmwareReady`, `GfxReady` e as interrupções desabilitados no `StartDevice`. A programação MMIO e o submit efetivo continuam condicionados ao carregamento real do firmware e à confirmação dos offsets específicos da revisão Cyan Skillfish2.

## Firmware e estado de prontidão

Os blobs públicos Cyan Skillfish2 usados como referência ficam em `firmware/amdgpu/`, acompanhados por `firmware/manifest.txt` com tamanho e SHA-256. O código separa explicitamente quatro estados que antes poderiam ser confundidos:

| Estado | Significado no projeto |
|---|---|
| `Present` | O blob está disponível na lista de imagens fornecida ao validador. |
| `Valid` | A imagem passou a validação de formato, tamanho e conteúdo esperado. |
| `Loaded` | O carregamento efetivo daquele microcode no hardware foi confirmado pelo caminho de firmware; é registrado por `Bc250FirmwareMarkLoaded`. |
| `Ready` | Todos os componentes obrigatórios estão presentes, válidos e carregados, permitindo que `Bc250FirmwareCommitReady` marque o hardware como pronto. |

A presença dos blobs no repositório, sozinha, não marca o KMD como pronto. `Bc250FirmwareMarkLoaded` só aceita uma imagem que já passou pela validação, e o commit para `FirmwareReady` exige o `LoadedMask` completo dos componentes obrigatórios. A integração real do carregamento pelo PSP/CP/SDMA ainda precisa ser concluída e confirmada em hardware.

## PSP GPCOM e evolução do carregamento de firmware

Eu incorporei ao projeto a referência técnica mais importante encontrada no repositório externo da BC-250: o caminho **PSP KM/GPCOM ring**. A documentação externa relata uma BC-250 real aceitando `GET_FW_ATTESTATION` pelo ring, com fence alcançada e avanço do WPTR. A base MP0 documentada é `0x58000` em bytes dentro da BAR5; os offsets C2PMSG usados pelo módulo estão reunidos em `src/firmware/bc250_psp.h`. [8] [9]

O novo módulo `bc250_psp.c` não substitui o KMD nem habilita GFX automaticamente. Ele separa a preparação do ring, a submissão da frame PSP de 64 bytes, a fence, `SETUP_TMR` e `LOAD_IP_FW`. O estado mantém os buffers contíguos, o WPTR, o valor de fence, a última resposta, o timeout e uma área de firmware que não pode ser liberada prematuramente caso o PSP ainda esteja fazendo DMA.

| Gate | Estado atual | Consequência |
|---|---:|---|
| `BC250_PSP_RING_VALIDATED` | `0` | O StartDevice prepara a estrutura PSP, mas não escreve C2PMSG nem tenta criar o ring. |
| `BC250_PSP_HDP_OFFSETS_VALIDATED` | `0` | O flush HDP não escreve os offsets candidatos; somente barreira de memória é aplicada. |
| `BC250_GFX_OFFSETS_VALIDATED` | `0` | O ring PSP não libera os engines GFX/SDMA. |
| `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED` | `0` | O ring PSP não libera ISR/IH/EOP. |

A sequência planejada é `GET_FW_ATTESTATION`, depois `SETUP_TMR` e só então `LOAD_IP_FW` por tipo de firmware. `Bc250FirmwareMarkLoaded` continua sendo chamado somente depois de uma resposta PSP positiva para a imagem específica; um blob presente ou uma submissão aceita não é confundido com firmware carregado. No caso de `SETUP_TMR`, a API exige que o endereço GPU/MC e o endereço físico de sistema sejam fornecidos separadamente e sejam diferentes. Eu não transformei o endereço de VRAM observado externamente em uma constante universal, porque a descoberta runtime e o orçamento UMA precisam ser confirmados na placa usada.

O relatório externo também mostra que o SOS pode responder “unknown command”, “not supported” ou “already loaded” para tipos diferentes. Por isso, o módulo registra a resposta sem marcar todos os componentes como prontos. A existência da implementação PSP melhora o próximo bring-up, mas ainda não significa que o KMD atual carregue CP/SDMA durante o StartDevice.

## GFX, offsets e interrupções

A tabela de registradores em `src/gfx/bc250_gfx_regs.h` é uma referência de família GC10/SDMA5 baseada no amdgpu, não uma confirmação da revisão física da BC-250. Por isso, os dois gates ficam explicitamente desativados nas configurações Debug e Release:

| Gate | Estado atual | Consequência |
|---|---:|---|
| `BC250_GFX_OFFSETS_VALIDATED` | `0` | Os offsets de GFX/SDMA permanecem candidatos; o driver pode preparar memória, mas não deve tratar a tabela genérica como confirmação de MMIO. |
| `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED` | `0` | Os offsets de IH/EOP permanecem zerados e o caminho de ISR não é habilitado. |

Os callbacks de ISR/DPC continuam compilados para preservar o contrato WDDM, mas o ISR retorna sem processar a interrupção enquanto os offsets IH, a prontidão do GFX e o gate de validação não estiverem confirmados. Eu mantive essa barreira para não transformar um offset apenas provável em escrita MMIO, acknowledge de interrupção ou reset incorreto.

## Memória UMA e hardware tratado

O driver reconhece os IDs PCI Cyan Skillfish usados no projeto (`13DB`, `13F9`, `13FA`, `13FB`, `13FC`, `13FE` e `143F`). A implementação parte da família GC10/GFX10 mais próxima disponível no código público do amdgpu e trata a BC-250 como uma GPU integrada em arquitetura UMA. [1] [2] [3]

Os 16 GB citados nas documentações da placa não são anunciados automaticamente como uma faixa de VRAM local. Durante a inicialização, o WDDM recebe o segmento e a aperture que o sistema consegue fornecer, enquanto o modelo de memória mantém a distinção entre memória do sistema compartilhada e memória local que ainda não foi comprovada. A BAR5 física documentada para o Skillfish2 é tratada como caminho do SMN, usando os offsets de índice/dados do mailbox; a BAR candidata de registradores GC/SDMA permanece separada para não confundir o espaço do SMN com a aperture dos rings.

## Fronteira DirectX

Também existe um UMD separado, registrado pelo INF do KMD com `UserModeDriverName=bc250_umd.dll`. A DLL exporta `OpenAdapter`, `OpenAdapter10`, `OpenAdapter10_2`, `OpenAdapter11` e `OpenAdapter12`, criando a fronteira mínima para continuar o bring-up com o KMD full-WDDM. A existência desses exports não equivale a suporte completo das APIs DirectX.

No estado atual, `OpenAdapter11` retorna `E_NOTIMPL` porque D3D11 possui uma ABI própria e não deve ser reinterpretado como D3D10. As rotinas de criação de device, caps e tabelas que ainda não estão implementadas também retornam `E_NOTIMPL` em vez de criar um device falso. As tabelas completas de recursos, shaders, estados, command lists, heaps, root signatures, PSO, sincronização e apresentação ainda precisam ser preenchidas para que o driver possa oferecer compatibilidade real com aplicações D3D9–D3D12. [4] [5] [6]

## Limites atuais do KMD

Eu prefiro que cada DDI anuncie o que realmente consegue executar. O caminho `DXGK_OPERATION_TRANSFER` para transferência CPU/MDL possui tratamento limitado; `Fill`, `Discard`, `Map` e `Unmap` de paging retornam `STATUS_NOT_SUPPORTED` enquanto não existir uma execução real por SDMA e page tables. Do mesmo modo, `D1`, `D2` e `D3` retornam `STATUS_NOT_SUPPORTED`; somente `D0` é aceito, porque a sequência validada de SMU/GFXOFF para transição de energia ainda não foi implementada.

Reset e TDR também permanecem conservadores. O reset invalida o engine e as interrupções, e o restart retorna `STATUS_DEVICE_NOT_READY` quando o firmware não foi carregado e o hardware não está efetivamente pronto. `QueryCurrentFence` não deve ser interpretado como prova de atividade da GPU enquanto o caminho de firmware, engine e interrupção permanecer bloqueado.

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
│   ├── firmware/        # firmware, validação, estados e PSP GPCOM gated
│   ├── smu/             # mailbox SMN e consultas SMU
│   ├── memory/          # perfil UMA/aperture e VidMm
│   ├── gfx/             # engines, rings, fences e offsets candidatos
│   ├── kmd/             # callbacks WDDM do kernel driver
│   └── umd/             # DLL user-mode DirectX
├── firmware/amdgpu/     # blobs Cyan Skillfish2 públicos
├── docs/                # especificações, pesquisas e histórico
└── tools/               # validação estrutural do projeto
```

## Como compilar no Windows 10 x64

Eu não consigo executar o build do WDK dentro deste sandbox Linux. Para compilar, uso uma máquina Windows 10 x64 com Visual Studio e WDK instalados, abro o Developer Command Prompt e executo:

```powershell
cd driver_bc250
msbuild bc250_kmd.sln /m /p:Configuration=Release /p:Platform=x64
```

Os projetos mantêm `BC250_ENABLE_FULL_WDDM=1` e `BC250_ENABLE_DX_UMD=1` em Debug e Release. Já `BC250_GFX_OFFSETS_VALIDATED=0`, `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0`, `BC250_PSP_RING_VALIDATED=0` e `BC250_PSP_HDP_OFFSETS_VALIDATED=0` permanecem em zero nas duas configurações. O script `tools/validate_project.py` verifica o XML, os arquivos críticos, os gates e o registro do UMD, mas não substitui a compilação do WDK.

Para instalar em uma máquina de teste, primeiro é necessário assinar os binários de acordo com a política do Windows 10, habilitar test signing quando apropriado e instalar o INF com privilégios administrativos. Eu não recomendo instalar o driver em um sistema de trabalho antes de ter uma forma de recuperar o dispositivo caso o bring-up gere TDR.

## O que eu verifico antes de considerar um avanço

A primeira verificação é estrutural: XML dos projetos, `git diff --check`, presença dos blobs e hashes do manifesto. A segunda é a compilação real no Visual Studio/WDK. A terceira só acontece em uma BC-250: BARs traduzidas, leitura dos registradores, estado do SMU, clocks fora de GFXOFF, carregamento do microcode do CP/SDMA, programação do ring, wrap do WPTR, fence escrita pela GPU, interrupção EOP/IH e reset/TDR.

Depois do hardware básico, ainda preciso validar o caminho de GPUVM/page tables, relocations, sincronização, shaders, tradução de comandos e as tabelas completas do UMD. Só então faz sentido avaliar aplicações DirectX. Até lá, considero o port corrigido no nível de semântica e estrutura, mas ainda não comprovado como acelerador real.

## Referências que usei

[1]: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd "Código amdgpu no kernel Linux"
[2]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h "Offsets públicos GC 10.1.0"
[3]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/include/asic_reg/sdma5/sdma5_4_2_2_offset.h "Offsets públicos SDMA5"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ "Referência D3DKMDDI do WDK"
[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ "Referência D3D10UMDDI do WDK"
[6]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ "Referência D3D12UMDDI do WDK"
[7]: https://github.com/tpn/winsdk-10/tree/master/Include/10.0.10240.0/um "Snapshot público de headers UMD do Windows SDK"
[8]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/docs/PSP-GPCOM-RING-WORKING.md "Referência externa do PSP KM/GPCOM ring"
[9]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/AGENTS.md "Resultados externos de PSP, firmware e SETUP_TMR"

## Autor

**ZEROAESQUERDA**
