# Roadmap do KMD BC-250

## Marco 0 — Build e instalação segura

O projeto precisa compilar no Windows 10 x64 com Visual Studio e WDK. Nesta etapa, o driver é instalado somente em modo de teste, com uma BC-250 secundária e captura de logs. O objetivo é validar o pacote `.sys`/`.inf`, não ativar aceleração.

## Marco 1 — PnP e identificação

Implementar o acesso seguro aos recursos fornecidos por `DxgkDdiStartDevice`/interfaces do display port, confirmar o ID PCI real e registrar a revisão. O driver deve recusar IDs desconhecidos e nunca assumir BAR, tamanho de framebuffer ou quantidade de SDMA.

## Marco 2 — MMIO e IP blocks

Mapear apenas os BARs informados pelo PnP. Reproduzir, de forma independente, a classificação Cyan Skillfish do `amdgpu`, as versões GC/SDMA e o caminho de firmware adequado. Cada leitura de registrador deve ter timeout/validação; nenhuma rotina de bring-up pode esperar indefinidamente por hardware.

## Marco 3 — Memória UMA

Modelar os segmentos visíveis ao Windows e a GPU virtual address. O BC-250 não deve ser tratado como RX 5700 XT: a alocação, visibilidade CPU/GPU e paginação precisam refletir a memória compartilhada e os carveouts da placa real.

## Marco 4 — Fila GFX e sincronização

Criar uma fila mínima, um ring/queue controlado, fences e uma operação copy/clear. A submissão deve ser serializada até que interrupts e DPCs estejam validados. O primeiro teste deve usar comandos pequenos e verificáveis, não workloads complexos.

## Marco 5 — Reset/TDR e display

Implementar detecção de hang, reset controlado, restauração de estado e callbacks de power. Em paralelo, implementar VidPn, EDID, connector e present apenas quando o caminho de memória for confiável.

## Marco 6 — UMD Vulkan

Conectar um UMD Vulkan derivado do RADV, com o BC-250 anunciado como `gfx1013`. O UMD deve usar exclusivamente a interface KMD definida por este projeto, sem depender de estruturas privadas do KMD Adrenalin.

## Marco 7 — DirectX

Escolher entre um UMD D3D12 próprio ou uma estratégia de tradução. Direct3D 11/12 não é consequência automática de ter uma fila GFX; requer implementação de recursos, heaps, shaders, state tracking, sincronização e apresentação conforme as DDIs WDDM.

## Critérios de parada

Qualquer bugcheck, timeout sem recuperação, acesso MMIO inválido, corrupção de memória ou reset que exija desligamento deve bloquear o avanço para a etapa seguinte. A compatibilidade final somente pode ser declarada depois de testes em hardware BC-250 real e em uma instalação Windows 10 x64 reproduzível.

## Contrato WDDM confirmado para aceleração

A documentação Microsoft descreve uma sequência que o BC-250 terá de cumprir: `DxgkDdiCreateDevice` fornece informações de DMA; o UMD cria contextos; recursos passam por `DxgkDdiCreateAllocation`; o UMD entrega render/present; o KMD valida e produz DMA buffers; `DxgkDdiBuildPagingBuffer` prepara movimentações; `DxgkDdiPatch` aplica endereços físicos; `DxgkDdiSubmitCommand` enfileira o buffer; e a interrupção lê a fence e chama `DxgkCbNotifyInterrupt`/`DxgkCbQueueDpc` [1].

Segmentos WDDM são a descrição do espaço de endereços da GPU para o VidMm. `DxgkDdiQueryAdapterInfo` é chamado duas vezes com `DXGKQAITYPE_QUERYSEGMENT` ou `DXGKQAITYPE_QUERYSEGMENT3`: primeiro para contar segmentos, depois para preencher `DXGK_SEGMENTDESCRIPTOR`/`DXGK_SEGMENTDESCRIPTOR3` [2]. A implementação atual do projeto ainda não fornece esses dados; portanto, não deve anunciar aceleração.

Referências:

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow "Microsoft Learn — WDDM operation flow"

[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-use-of-memory-segments "Microsoft Learn — Initializing use of memory segments"

## Detalhes da DDI de segmentos

`DXGKARG_QUERYADAPTERINFO` contém `Type`, `pInputData`, `InputDataSize`, `pOutputData` e `OutputDataSize`. Para segmentos, o tipo é `DXGKQAITYPE_QUERYSEGMENT` ou `DXGKQAITYPE_QUERYSEGMENT3`, com entrada `DXGK_QUERYSEGMENTIN` e saída `DXGK_QUERYSEGMENTOUT3` [3].

`DXGK_QUERYSEGMENTOUT3` exige `NbSegment`, ponteiro para `DXGK_SEGMENTDESCRIPTOR3`, `PagingBufferSegmentId`, `PagingBufferSize` e `PagingBufferPrivateDataSize`. Cada descritor contém flags, `BaseAddress`, `CpuTranslatedAddress`, `Size`, `CommitLimit` e campos de banking/preservação [4]. Para o BC-250, não se deve preencher `Size` com uma constante de RX 5700 XT; o tamanho de UMA e a visibilidade precisam vir da placa/firmware e dos recursos WDDM reais.

Referências:

[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_queryadapterinfo "Microsoft Learn — DXGKARG_QUERYADAPTERINFO"

[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout3 "Microsoft Learn — DXGK_QUERYSEGMENTOUT3"

[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor3 "Microsoft Learn — DXGK_SEGMENTDESCRIPTOR3"
