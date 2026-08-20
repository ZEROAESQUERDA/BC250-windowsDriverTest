# Estado atual do driver BC-250

## Modo de ativação

O projeto está configurado para o **modo experimental de bring-up**. `BC250_ENABLE_FULL_WDDM=1` e `BC250_ENABLE_DX_UMD=1` continuam ativos nas configurações Debug e Release do projeto Windows 10 x64. Já `BC250_GFX_OFFSETS_VALIDATED=0`, `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0`, `BC250_PSP_RING_VALIDATED=0` e `BC250_PSP_HDP_OFFSETS_VALIDATED=0`: a tabela GC/SDMA permanece candidata e o ISR não toca offsets de IH zerados. O KMD registra a tabela `DRIVER_INITIALIZATION_DATA` full-WDDM, mantém as BARs traduzidas mapeadas durante a vida do adaptador e anuncia os contratos de criação de device/contexto, alocações, Render, Patch, paging, SubmitCommand, fences, preempção e reset, mas só permite execução de engine depois de firmware carregado e offsets confirmados.

A configuração mantém os contratos full-WDDM e UMD compiláveis, mas não trata offsets de família como validação ASIC. Os offsets foram derivados das tabelas GC10/SDMA5 documentadas em `docs/REGS_REFERENCE.md`; sem BC-250 física, o driver prepara memória e mantém o estado do engine não pronto, sem permitir que o Windows veja uma fence ou interrupção falsa.

## Componentes implementados

O KMD possui descoberta de BAR5 para o mailbox SMN e separação da BAR candidata de registradores GC/SDMA. As consultas SMU continuam sendo tentadas; se falharem, o modo experimental prossegue com estado zerado em vez de abortar o StartDevice. O modelo de memória continua UMA/aperture, usando os segmentos reportados pelo WDDM e sem afirmar uma faixa de VRAM local que não tenha sido descoberta.

O novo módulo `src/firmware/bc250_psp.c` incorpora a referência do PSP KM/GPCOM ring observada em uma BC-250 real: base MP0 `0x58000`, C2PMSG 64/67/69/70/71/81, ring contíguo de 4 KiB, frame PSP de 64 bytes, fence e respostas de `GET_FW_ATTESTATION`, `SETUP_TMR` e `LOAD_IP_FW`. O estado PSP é inicializado no AddDevice e liberado no Stop/RemoveDevice, mas a criação do ring retorna `STATUS_NOT_SUPPORTED` enquanto `BC250_PSP_RING_VALIDATED=0`. Os offsets de flush HDP também permanecem em `BC250_PSP_HDP_OFFSETS_VALIDATED=0`; nessa condição o módulo só aplica barreiras de memória e não faz escrita MMIO candidata.

O módulo GFX aloca rings e fences em memória fisicamente contígua não paginável e prepara os pacotes PM4 `INDIRECT_BUFFER` e `WRITE_DATA` com o controle de destino de memória assíncrono e `WR_CONFIRM` observado no amdgpu GFX10. O CPU não grava mais a conclusão logo após o WPTR. A programação MMIO, NOP e submit permanecem dependentes de firmware e offsets validados. A rotina de reset limpa rings e fences, enquanto `QueryCurrentFence` lê somente o valor observado no buffer de fence.

As DDIs full-WDDM criam handles privados para device/context/allocation, preenchem `DXGK_DEVICEINFO`/`DXGK_CONTEXTINFO`, alocam backing contíguo UMA/system-memory para allocations, respondem a Describe/OpenAllocation, copiam comandos para DMA buffers e submetem buffers ao ring GFX quando firmware e engine estão realmente prontos. Patch continua limitado ao caminho linear. Transfer CPU/MDL é aceito somente no caso implementado; Fill/Discard/Map/Unmap agora retornam `STATUS_NOT_SUPPORTED` até existir execução SDMA/page-table real. Reset invalida explicitamente o engine, e Restart retorna `STATUS_DEVICE_NOT_READY` sem firmware carregado.

O UMD exporta as fronteiras `OpenAdapter`, `OpenAdapter10`, `OpenAdapter10_2`, `OpenAdapter11` e `OpenAdapter12`. Como as tabelas completas de recursos, shaders, estados, command lists, heaps, PSO e sincronização ainda não existem, CreateDevice e capabilities incompletas retornam `E_NOTIMPL` em vez de criar um device falso. O INF principal do KMD copia `bc250_umd.dll` e registra `UserModeDriverName`; o INF UMD separado também está ativo.

## Limitações funcionais que permanecem

A ativação dos entry points não equivale a uma implementação completa das APIs DirectX. O UMD ainda não preenche as extensas tabelas de funções de recursos, shaders, estados, command lists, heaps, root signatures, PSO, sincronização e apresentação exigidas pelos runtimes D3D10/D3D11/D3D12. O caminho KMD copia e submete buffers, mas não implementa um compilador de shaders, tradução de bytecode, gerenciamento completo de GPU virtual address, page tables, relocations ou validação de todos os pacotes PM4.

A BC-250 real não está disponível para teste, o sandbox Linux não contém Visual Studio/WDK/MSBuild e não é possível confirmar aqui a compilação do `.sys`/`.dll`, a enumeração PnP, o comportamento do PSP/SMU, os offsets específicos do Cyan Skillfish2, os doorbells, o ring wrap, as interrupções, a estabilidade térmica ou o TDR. Portanto, esta entrega mantém a infraestrutura pronta para o próximo bring-up, mas não deve ser descrita como um driver certificado ou como compatibilidade garantida com DX9–DX12.

| Área | Estado ativado | Risco/limitação atual |
|---|---|---|
| Full-WDDM | Ativo em Debug e Release | A ABI só poderá ser confirmada com WDK Windows 10 |
| Firmware/PSP/SMU | Blobs, consultas e módulo PSP GPCOM presentes; `FirmwareReady` permanece falso sem `LoadedMask` completo | PSP ring, `SETUP_TMR` e `LOAD_IP_FW` estão integrados atrás de gates; ainda precisam de execução e confirmação na placa |
| Rings | GFX/Compute e SDMA0/SDMA1 alocados; programação MMIO aguarda gates | Tabela de offsets é candidata, não validada em BC-250 física |
| Interrupções | Callbacks ISR/DPC compilados, ISR desativado | Falta tabela IH/EOP real e confirmação de status/ack |
| VidMm/paging | Transfer CPU/MDL limitado; Fill/Discard/Map/Unmap retornam não suportado | Ainda não há page-table/GPU-VA/SDMA completo |
| DirectX 9 | Fronteira OpenAdapter disponível; criação incompleta retorna não implementado | Tabela de device functions ainda não é suficiente para jogos |
| DirectX 10/11 | OpenAdapter disponível; CreateDevice incompleto retorna não implementado | Recursos, shaders e execução de comandos ainda não estão implementados |
| DirectX 12 | OpenAdapter12 disponível; CreateDevice/caps retornam não implementado | Tabelas opcionais e DDI completa D3D12 ainda não estão preenchidas |
| Instalação | KMD INF copia e registra o UMD | Assinatura, catálogo e teste de instalação dependem do Windows |

## Referências

A referência externa do PSP GPCOM ring e os resultados de firmware/SETUP_TMR estão em [9] e [10]. Eles são evidências de outra árvore de desenvolvimento e não substituem a validação na BC-250 usada pelo nosso driver.

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-the-display-miniport-driver "Initializing the display miniport driver — Microsoft Learn"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_driver_initialization_data "DRIVER_INITIALIZATION_DATA — Microsoft Learn"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout "DXGK_QUERYSEGMENTOUT — Microsoft Learn"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-a-user-mode-display-driver "Loading a user-mode display driver — Microsoft Learn"
[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createallocation "DXGKARG_CREATEALLOCATION — Microsoft Learn"
[6]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_render "DXGKARG_RENDER — Microsoft Learn"
[7]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommand "DXGKARG_SUBMITCOMMAND — Microsoft Learn"
[8]: https://github.com/tpn/winsdk-10/tree/master/Include/10.0.10240.0/um "Windows SDK UMD headers snapshot"
[9]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/docs/PSP-GPCOM-RING-WORKING.md "Referência externa do PSP KM/GPCOM ring"
[10]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/AGENTS.md "Resultados externos de PSP, firmware e SETUP_TMR"

## Arquivos de referência do projeto

`docs/REGS_REFERENCE.md` documenta os offsets candidatos e suas limitações. `docs/EXTERNAL_REPO_EVOLUTION_ANALYSIS.md` consolida as descobertas externas e a decisão de port seletivo. `docs/WDDM_ENABLE_REFERENCE.md` registra os campos WDDM confirmados nas estruturas da Microsoft. `docs/UMD_REFERENCE.md` registra as estruturas UMD observadas nos headers públicos do Windows SDK. `firmware/manifest.txt` lista os blobs Cyan Skillfish2 e seus hashes SHA-256.
