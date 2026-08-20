# Estado atual do driver BC-250

## Modo de ativação

O projeto está configurado para o **modo experimental acelerado**. `BC250_ENABLE_FULL_WDDM=1`, `BC250_ENABLE_DX_UMD=1`, `BC250_GFX_OFFSETS_VALIDATED=1` e `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=1` estão ativos nas configurações Debug e Release do projeto Windows 10 x64. O KMD registra a tabela `DRIVER_INITIALIZATION_DATA` full-WDDM, mantém as BARs traduzidas mapeadas durante a vida do adaptador e anuncia o caminho de criação de device/contexto, alocações, Render, Patch, paging, SubmitCommand, fences, preempção e reset.

Essa ativação atende ao requisito de não deixar a aceleração bloqueada por macros, mas é deliberadamente um **bring-up experimental**: os offsets de registradores foram derivados das tabelas de família GC10/SDMA5 documentadas em `docs/REGS_REFERENCE.md`, e não foram confirmados em uma BC-250 física. O driver agora pode escrever os registradores candidatos quando o Windows inicializa o dispositivo; isso pode falhar, travar o engine ou provocar TDR em hardware incompatível.

## Componentes implementados

O KMD possui descoberta de BAR5 para o mailbox SMN e separação da BAR candidata de registradores GC/SDMA. As consultas SMU continuam sendo tentadas; se falharem, o modo experimental prossegue com estado zerado em vez de abortar o StartDevice. O modelo de memória continua UMA/aperture, usando os segmentos reportados pelo WDDM e sem afirmar uma faixa de VRAM local que não tenha sido descoberta.

O módulo GFX aloca rings e fences em memória fisicamente contígua não paginável, configura bases e ponteiros de rings para GFX/Compute experimental e SDMA0/SDMA1, emite um pacote PM4 `INDIRECT_BUFFER` para DMA buffers submetidos e atualiza a fence monotônica. O caminho de `SubmitNoop` emite um PM4 NOP. A rotina de reset limpa rings e fences, enquanto `QueryCurrentFence` lê o valor observado no buffer de fence.

As DDIs full-WDDM agora criam handles privados para device/context/allocation, preenchem `DXGK_DEVICEINFO`/`DXGK_CONTEXTINFO`, alocam backing contíguo UMA/system-memory para allocations, respondem a Describe/OpenAllocation, copiam comandos para DMA buffers, aceitam Patch no caminho linear, submetem buffers ao ring GFX, tratam Transfer/Fill/Discard/Map/Unmap de paging e retornam sucesso para operações experimentais ainda sem tradução específica. Preempção e reset possuem caminhos mínimos para limpar o estado do engine.

O UMD exporta e ativa `OpenAdapter`, `OpenAdapter10`, `OpenAdapter10_2`, `OpenAdapter11` e `OpenAdapter12`. O D3D9/DDI base mantém um device privado persistente; D3D10 e D3D11 usam a tabela mínima de adapter/device functions disponível no header; D3D12 fornece cálculo de tamanho, criação, fechamento, caps, versões suportadas e destruição de device. O INF principal do KMD agora copia `bc250_umd.dll` e registra `UserModeDriverName`; o INF UMD separado também está ativo.

## Limitações funcionais que permanecem

A ativação dos entry points não equivale a uma implementação completa das APIs DirectX. O UMD ainda não preenche as extensas tabelas de funções de recursos, shaders, estados, command lists, heaps, root signatures, PSO, sincronização e apresentação exigidas pelos runtimes D3D10/D3D11/D3D12. O caminho KMD copia e submete buffers, mas não implementa um compilador de shaders, tradução de bytecode, gerenciamento completo de GPU virtual address, page tables, relocations ou validação de todos os pacotes PM4.

A BC-250 real não está disponível para teste, o sandbox Linux não contém Visual Studio/WDK/MSBuild e não é possível confirmar aqui a compilação do `.sys`/`.dll`, a enumeração PnP, o comportamento do PSP/SMU, os offsets específicos do Cyan Skillfish2, os doorbells, o ring wrap, as interrupções, a estabilidade térmica ou o TDR. Portanto, esta entrega configura todos os caminhos para tentar acelerar, mas não deve ser descrita como um driver certificado ou como compatibilidade garantida com DX9–DX12.

| Área | Estado ativado | Risco/limitação atual |
|---|---|---|
| Full-WDDM | Ativo em Debug e Release | A ABI só poderá ser confirmada com WDK Windows 10 |
| Firmware/SMU | Blobs e consultas presentes; bring-up prossegue em falha SMU | Carregamento PSP/CP e sequência de power ainda não são equivalentes ao amdgpu |
| Rings | GFX/Compute experimental, SDMA0/SDMA1 e fences alocados/programáveis | Tabela de offsets é candidata, não validada em BC-250 física |
| Interrupções | ISR/DPC e notify DPC registrados | Status/ack candidatos podem gerar falsos eventos ou TDR |
| VidMm/paging | Transfer, Fill, Discard e aperture aceitos | Várias operações são no-op e não realizam page-table/GPU-VA completo |
| DirectX 9 | Fronteira D3D9 ativa | Tabela de device functions ainda é mínima |
| DirectX 10/11 | OpenAdapter e CreateDevice mínimos ativos | Recursos, shaders e execução de comandos ainda não estão implementados |
| DirectX 12 | OpenAdapter12 e caps/versões mínimos ativos | Tabelas opcionais e DDI completa D3D12 ainda não estão preenchidas |
| Instalação | KMD INF copia e registra o UMD | Assinatura, catálogo e teste de instalação dependem do Windows |

## Referências

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-the-display-miniport-driver "Initializing the display miniport driver — Microsoft Learn"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_driver_initialization_data "DRIVER_INITIALIZATION_DATA — Microsoft Learn"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout "DXGK_QUERYSEGMENTOUT — Microsoft Learn"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-a-user-mode-display-driver "Loading a user-mode display driver — Microsoft Learn"
[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createallocation "DXGKARG_CREATEALLOCATION — Microsoft Learn"
[6]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_render "DXGKARG_RENDER — Microsoft Learn"
[7]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommand "DXGKARG_SUBMITCOMMAND — Microsoft Learn"
[8]: https://github.com/tpn/winsdk-10/tree/master/Include/10.0.10240.0/um "Windows SDK UMD headers snapshot"

## Arquivos de referência do projeto

`docs/REGS_REFERENCE.md` documenta os offsets candidatos e suas limitações. `docs/WDDM_ENABLE_REFERENCE.md` registra os campos WDDM confirmados nas estruturas da Microsoft. `docs/UMD_REFERENCE.md` registra as estruturas UMD observadas nos headers públicos do Windows SDK. `firmware/manifest.txt` lista os blobs Cyan Skillfish2 e seus hashes SHA-256.
