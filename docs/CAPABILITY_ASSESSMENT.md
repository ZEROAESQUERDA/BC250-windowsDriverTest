# Avaliação de capacidade possível com os dados públicos

## Resposta curta

Os dados públicos são suficientes para compilar uma **base de KMD BC-250 muito mais concreta** do que um KMD genérico. Também permitem implementar, em código, a descrição de memória UMA, a inicialização de uma engine GFX/SDMA, fences, reset e testes de clear/copy. Eles não são suficientes para declarar, sem hardware, um driver Windows com **Direct3D funcional e estável**.

## Matriz de viabilidade

| Capacidade | É possível compilar agora? | É possível funcionar? | O que falta |
| --- | --- | --- | --- |
| Identificação PCI e `StartDevice` | Sim | Alta probabilidade | Build WDK e teste PnP |
| Leitura/mapeamento de BARs | Sim | Alta probabilidade | Validar lista de recursos e BAR5 real |
| Mostrar a capacidade de memória | Sim | Parcial | A capacidade é UMA/BIOS-dependente; não usar 16 GB fixos sem confirmar split |
| Expor 16 GB como VRAM local | Sim, sintaticamente | Não é seguro | WDDM precisa de segmentos corretos; BC-250 compartilha o pool com CPU |
| GFX ring/PM4 | Sim | Possível, mas condicionado | Firmware, SMU, GFXOFF, clocks, VM, ring e interrupt precisam funcionar |
| SDMA copy/fill | Sim | Possível | Firmware SDMA correto, endereços GPU e fence |
| Fence/interrupção/TDR | Sim | Possível | IH ring, DPC, QueryCurrentFence e reset precisam ser validados |
| Vulkan via ICD próprio | Sim, em princípio | Mais viável que DirectX | UMD/ICD completo e comunicação KMD/UMD |
| Direct3D 9 | Sim, em princípio | POC mais simples | UMD DDI compatível e caminho de alocação/present |
| Direct3D 11 | Sim, em princípio | Projeto grande | UMD D3D11 real, shaders, recursos, sincronização e validação |
| Direct3D 12 | Sim, em princípio | Projeto muito grande | UMD D3D12, heaps, command lists, residency, fences e KMD WDDM coerente |
| Encode/decode VCN | Não é prioridade viável | Não presumir suporte | Firmware/restrições do BC-250 documentados como limitantes |

## VRAM: o ponto que costuma ser confundido

O BC-250 possui memória GDDR6 fisicamente compartilhada pelo CPU e GPU. A documentação comunitária descreve 16 GB totais e splits de BIOS/dinâmicos que podem deixar uma parte do pool visível ao GPU. Portanto, “16 GB de VRAM” não deve ser codificado como uma VRAM local equivalente à de uma RX 5700 XT.

No WDDM, o KMD deve responder às consultas de segmentos por `DxgkDdiQueryAdapterInfo`, especialmente `DXGKQAITYPE_QUERYSEGMENT`/`QUERYSEGMENT3`, preenchendo descritores com base, tamanho, visibilidade e limite de commit [1] [2]. O modelo correto para uma primeira versão é uma **aperture/UMA system-memory segment** cuja capacidade seja obtida no bring-up, e não uma constante de 16 GB. Se o BIOS reservar 8 GB para o GPU, anunciar 16 GB pode permitir alocações que não existem e provocar corrupção ou TDR.

## Aceleração: o que precisa acontecer no hardware

Para a aceleração sair do papel, o KMD precisa inicializar uma engine real, reservar um ring buffer acessível pela GPU, produzir PM4 válido, submeter o buffer e observar uma fence através de interrupção/DPC. A documentação WDDM descreve a sequência `DxgkDdiRender`/`Present` → DMA buffer → `DxgkDdiPatch` → `DxgkDdiSubmitCommand` → interrupção → `DxgkCbNotifyInterrupt`/DPC [3].

No BC-250B/Skillfish2, a documentação pública indica que o SMU deve ser acordado e receber as tabelas de DPM; caso o GFX permaneça em GFXOFF, os registradores podem parecer legíveis mas a engine não processa rings. A rota SMU registrada publicamente usa acesso SMN por `BAR5+0x38/0x3C`, não acesso direto a endereços físicos SMN. Essa etapa deve preceder qualquer tentativa de medir aceleração [4].

## DirectX: por que compilar não é o mesmo que suportar

Um `.sys` compilado e um INF aceito não fornecem DirectX. O runtime precisa criar o dispositivo no KMD, o UMD precisa criar contextos, recursos e command buffers, o KMD precisa validar e traduzir esses buffers para PM4, e o VidMm precisa controlar segmentos, paginação, residency e endereços GPU. A ausência de um UMD AMD oficial para o BC-250 é o maior bloqueio de software.

A rota de menor risco é começar com **um único caminho de compute/copy validado**, depois um UMD mínimo para clear/copy, e somente então D3D9 ou D3D11. D3D12 deve ficar por último, pois exige uma superfície ampla de DDIs e um contrato KMD/UMD mais completo. O projeto público Keshas-dev demonstra que há uma base experimental de KMD/UMD e alguns testes, mas o branch observado também usa um caminho display-only/IOCTL e documenta componentes ainda incompletos [5].

## Marco realmente compilável recomendado

O próximo marco deve ser um pacote WDK que compile e contenha:

1. classificação `0x13FE`/`0x143F` como candidatos Skillfish2;
2. registro dos seis BARs traduzidos, incluindo endereço e tamanho, sem hardcode de endereço físico;
3. consulta segura da variante/IP e do estado SMU;
4. um segmento UMA conservador, inicialmente sem anunciar VRAM local falsa;
5. uma engine GFX ou SDMA isolada, com ring, fence, timeout e reset;
6. um teste de no-op/clear/copy executado somente depois da validação da fence;
7. logs persistentes para diferenciar falha de PnP, MMIO, firmware, SMU, ring, interrupt, VidMm e UMD.

Depois desse marco, será possível decidir com evidência se vale conectar um UMD Direct3D ou priorizar Vulkan/compute.

## Referências

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-use-of-memory-segments "Microsoft Learn — Initializing use of memory segments"

[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout3 "Microsoft Learn — DXGK_QUERYSEGMENTOUT3"

[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow "Microsoft Learn — WDDM operation flow"

[4]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver "Keshas-dev — public BC-250 Windows driver research"

[5]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/tree/main/wddm-ps5 "Keshas-dev — wddm-ps5 reference tree"
