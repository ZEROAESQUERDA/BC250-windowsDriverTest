# Auditoria do veredito externo sobre o driver BC-250

## Resumo executivo

O veredito recebido está **correto na conclusão principal**: o projeto atual é uma base de bring-up full-WDDM experimental, não um driver pronto para executar jogos DirectX. A arquitetura KMD → Dxgkrnl → UMD → GPU está no caminho certo, mas várias rotinas foram implementadas apenas no nível mínimo necessário para abrir a fronteira WDDM. Ainda faltam GPUVM, page tables, residency, relocations, tradução completa de comandos, firmware carregado de verdade, conclusão de fence pelo hardware, tabelas completas do UMD e apresentação DXGI.

Há, porém, uma diferença importante: o veredito parece ter analisado uma versão antiga do repositório. Ele cita `amdbc250kmd.c`, `amdbc250_umd.c`, `amdbc250_hw_init.c` e headers `amdbc250_d3d*.h`. A árvore atual usa `src/kmd/bc250_kmd.cxx`, `src/kmd/bc250_full_wddm.cxx`, `src/gfx/bc250_gfx.c`, `src/umd/bc250_umd.c`, `src/memory`, `src/firmware` e `src/smu`. Portanto, os nomes dos arquivos e algumas descrições de memória/display precisam ser atualizados, mas o diagnóstico de maturidade continua válido.

> **Conclusão direta:** o projeto consegue demonstrar uma tentativa real de KMD/UMD/ring/PM4, mas ainda não pode ser chamado de driver DirectX funcional para jogos. O bloqueio mais grave não é apenas a falta de tabelas do UMD; é o fato de `Bc250SubmitDmaBuffer` gravar a fence no CPU imediatamente depois de escrever o WPTR, sem esperar uma conclusão EOP real da GPU (`src/gfx/bc250_gfx.c:477-483`).

## Comparação ponto a ponto

| Tema do veredito | Situação na árvore atual | Avaliação |
|---|---|---|
| KMD full-WDDM | `DRIVER_INITIALIZATION_DATA` e `DxgkInitialize` estão registrados em `src/kmd/bc250_kmd.cxx:5-45` | Correto como arquitetura, mas não prova funcionalidade |
| Rings GFX/SDMA | Memória contígua e offsets candidatos existem; `Bc250ProgramGfxRings` escreve bases/pointers candidatos | Parcial e de alto risco sem BC-250 real |
| SubmitCommand | Emite `INDIRECT_BUFFER` e atualiza WPTR em `src/gfx/bc250_gfx.c:452-472` | Há um caminho de hardware tentativo, mas não uma cadeia completa de execução |
| Fences | O CPU grava a fence imediatamente em `src/gfx/bc250_gfx.c:477-483` | Crítico: conclusão é simulada, não produzida por EOP da GPU |
| Memória UMA | Perfil conservador; sem faixa local inventada quando não há descoberta real (`src/memory/bc250_memory.c:67-79`) | O veredito antigo sobre “segmento VRAM fixo + 4 GB” está desatualizado, mas a crítica à GPUVM continua correta |
| CreateAllocation | Aloca memória contígua CPU-visível e marca segmentos básicos (`src/kmd/bc250_full_wddm.cxx:234-274`) | Não é gerenciamento de VRAM/GPUVM completo |
| GPUVM | Não há implementação de page tables, VMID, mappings, residency, eviction ou cache management | Bloqueador real para cargas modernas |
| Paging | Transfer CPU/MDL é tratado em um caso; Fill, Discard, Map e Unmap têm caminhos de sucesso/no-op (`src/kmd/bc250_full_wddm.cxx:453-492`) | Não é paging SDMA real |
| Render | Apenas copia bytes de `pCommand` para `pDmaBuffer` (`src/kmd/bc250_full_wddm.cxx:394-419`) | Não traduz/valida comandos UMD nem gera patch locations |
| Patch | Retorna sucesso sem relocations (`src/kmd/bc250_full_wddm.cxx:421-437`) | Válido apenas para um caminho linear experimental |
| UMD D3D9 | `OpenAdapter` e CreateDevice mínimos existem | Não há tabela de operações de desenho/recursos suficiente para jogos |
| UMD D3D10/D3D11 | `OpenAdapter10`, `OpenAdapter10_2` e `OpenAdapter11` existem, mas usam tabelas mínimas; D3D11 é adaptado por cast para o helper D3D10 (`src/umd/bc250_umd.c:253-276`) | Fronteira experimental, com risco de ABI e sem funcionalidade gráfica completa |
| UMD D3D12 | `OpenAdapter12` existe, mas CreateDevice retorna sucesso sem criar estado (`src/umd/bc250_umd.c:297-305`), DestroyDevice é no-op (`:342-348`) e caps são zeradas (`:308-321`) | Não é suporte D3D12 funcional |
| Firmware | Blobs estão no repositório e há validação de nomes/tamanhos; não há fluxo completo PSP/CP que carregue os blobs no hardware | A observação do veredito sobre microcode é válida; a documentação deve distinguir “incluído” de “carregado” |
| Interrupções | ISR lê/ack de offsets candidatos e agenda DPC (`src/kmd/bc250_kmd.cxx:440-523`) | Não há evidência suficiente de IH ring/EOP real; deve ser tratado como experimental |
| Display/VidPn | Os callbacks atuais zeram estruturas ou retornam sucesso; `QueryChildRelations` não descreve uma saída DP real (`src/kmd/bc250_kmd.cxx:386-422`) | A descrição antiga de HPD/VSYNC/1080p60 não corresponde ao projeto atual |
| Reset/TDR | Reset zera ring/fence em software (`src/kmd/bc250_full_wddm.cxx:571-608`) | Não é reset de engine/hardware nem recuperação TDR completa |

## O problema mais grave: fence falsa

O veredito identificou corretamente que escrever um pacote `INDIRECT_BUFFER` não garante que a GPU executou o comando. A árvore atual permite uma conclusão ainda mais precisa. O código calcula o endereço físico e o tamanho do DMA buffer, grava quatro DWORDs do pacote no ring e atualiza `CP_RB0_WPTR` ou o registrador candidato correspondente. Isso é uma tentativa real de submissão, não apenas um comentário.

O problema aparece imediatamente depois: `FenceId` é escolhido e escrito pelo próprio CPU no endereço `engine->Fence.CpuAddress`. Assim, `QueryCurrentFence` pode observar uma fence maior antes de a GPU ter lido o ring, executado o indirect buffer ou sinalizado um evento EOP. Qualquer scheduler que confie nesse valor pode liberar recursos cedo demais, reusar memória ainda em uso ou mascarar uma falha do engine.

A correção prioritária é remover essa escrita CPU-side como “completion”. A submissão deve gravar um pacote de write-data/EOP que faça a GPU escrever a fence em memória, configurar a interrupção correspondente, reconhecer o evento no IH e somente então atualizar `LastCompleted`. Enquanto isso não existir, o caminho pode continuar útil como bring-up de MMIO, mas não deve ser considerado sincronização funcional.

## Memória: o veredito precisa ser atualizado, mas não descartado

A análise externa descreve um projeto antigo que anunciava um segmento de VRAM e outro de sistema com tamanhos fixos. Isso não descreve a árvore atual. `Bc250BuildUmaProfile` explicitamente não transforma os 16 GB nominais em VRAM local: só cria uma faixa UMA-like quando existe uma faixa GPU-visible descoberta; sem isso, usa uma aperture/GART derivada do host (`src/memory/bc250_memory.c:67-125`). `Bc250InitializeMemoryState` começa com `GpuVisibleRangeKnown=FALSE` (`:143-170`).

Isso é uma melhora de honestidade, não uma solução completa. `CreateAllocation` ainda aloca memória contígua CPU-visível e armazena um endereço físico (`src/kmd/bc250_full_wddm.cxx:243-274`). Não há no projeto uma implementação equivalente à camada GPUVM do amdgpu: page tables, VMID, GPU virtual address, residency, eviction, BO lifetime, invalidation de TLB, coerência de cache e proteção de acesso ainda precisam ser construídos. O veredito acerta ao classificar GPU memory/GPU virtual memory como bloqueadores, mas erra ao atribuir à árvore atual o modelo fixo do esboço antigo.

## Firmware: arquivos presentes não significam microcode executado

Os blobs `cyan_skillfish2_ce.bin`, `pfp.bin`, `me.bin`, `mec.bin`, `mec2.bin`, `rlc.bin`, `sdma.bin` e `sdma1.bin` estão no repositório e o módulo de firmware sabe selecionar seus nomes (`src/firmware/bc250_firmware.c:13-55`). A validação também confere alinhamento, tamanho e conteúdo não nulo (`:86-115`).

O módulo não carrega essas imagens sozinho no PSP/CP. A função `Bc250FirmwareCommitReady` apenas marca `HwState->FirmwareReady=TRUE` e controla a flag de writes (`src/firmware/bc250_firmware.c:191-207`). Além disso, o `StartDevice` atual força `context->Gfx.FirmwareLoaded=TRUE` e `context->Hw.FirmwareReady=TRUE` antes de existir um carregamento real (`src/kmd/bc250_kmd.cxx:298-305`). Esse é um ponto mais sério que a inconsistência textual observada no README antigo: o estado do driver pode afirmar prontidão que o hardware ainda não recebeu.

A documentação correta deve dizer: **firmware público incluído e validável; carregamento efetivo no PSP/CP/SDMA ainda não implementado**. O próximo trabalho precisa integrar a sequência de firmware ou demonstrar, por logs e registradores, que o boot firmware já deixou os engines prontos.

## UMD: fronteira aberta não é D3D funcional

O veredito está correto ao separar exports de adapter de suporte de aplicação. Na árvore atual, D3D10 e D3D11 compartilham um conjunto mínimo de callbacks; `OpenAdapter11` faz cast da estrutura D3D11 para o helper D3D10 (`src/umd/bc250_umd.c:269-276`). Não existem tabelas completas para recursos, buffers, texturas, shaders, draw, copy, state, query, synchronization, present e comandos de execução.

D3D12 é ainda mais claro: `Bc250UmdCreateDevice12` ignora os argumentos e retorna `S_OK`, `Bc250UmdDestroyDevice12` não libera estado, `Bc250UmdGetCaps12` apenas zera o buffer e `Bc250UmdGetSupportedVersions12` anuncia uma versão mínima (`src/umd/bc250_umd.c:286-348`). Isso pode demonstrar que o export é encontrado, mas não é uma base suficiente para o runtime criar uma GPU D3D12 utilizável.

A recomendação do veredito de priorizar D3D11 antes de D3D12 é tecnicamente sensata, mas ainda precisa de um marco intermediário: primeiro provar um caminho de buffer/clear/copy com sincronização real; depois construir um UMD D3D11 pequeno e coerente; só então adicionar shaders, render targets e swapchain. D3D12 deve ficar depois de GPUVM e sincronização maduras.

## Display: a análise antiga superestima a árvore atual

O veredito fala em VidPN source, DisplayPort, HPD, VSYNC, CRTC e timing 1080p60. Isso pode ter vindo do esboço antigo, mas não deve ser atribuído ao projeto publicado atual. `QueryChildRelations` apenas zera o buffer, `QueryChildStatus` retorna `STATUS_NO_MORE_ENTRIES`, os callbacks VidPn usam macros que retornam sucesso sem processar os argumentos, e `SetVidPnSourceAddress` também retorna sucesso sem modeset (`src/kmd/bc250_kmd.cxx:386-422`, `:604-611`; `src/kmd/bc250_full_wddm.cxx:610-623`).

A classificação correta para display é “estrutura de callbacks presente, display real ainda não implementado”. Isso é menos avançado do que o veredito sugeriu, mas torna o diagnóstico mais confiável.

## Classificação revisada

| Área | Classificação atual | Motivo |
|---|---:|---|
| PCI/PnP e mapeamento inicial | Amarelo/verde | Estrutura existe, mas precisa de WDK e hardware |
| MMIO | Amarelo | BARs são mapeadas, mas offsets de engine são candidatos |
| Firmware/SMU | Vermelho | Blobs/queries existem; carregamento e sequência de power não estão fechados |
| Ring GFX/SDMA | Laranja | Ring e MMIO são tentados, mas sem validação ASIC/firmware |
| Fence/interrupt | Vermelho | Fence de conclusão é escrita pelo CPU; EOP/IH real não está provado |
| Memória UMA/aperture | Laranja | Modelo conservador existe; GPUVM completo não existe |
| Render/Submit | Vermelho | Há PM4 `INDIRECT_BUFFER`, mas cadeia de execução e sincronização não são confiáveis |
| Display/VidPn | Vermelho | Callbacks mínimos/no-op, sem HPD/modeset/present real |
| D3D9 UMD | Laranja/vermelho | Fronteira mínima, sem tabelas de operações suficientes |
| D3D10/D3D11 UMD | Vermelho | Tabelas mínimas e sem recursos/shaders/commands |
| D3D12 UMD | Vermelho | CreateDevice/DestroyDevice/caps ainda são essencialmente placeholders |
| Jogos Windows | Vermelho | Não há base para afirmar compatibilidade real |

## Plano de correção recomendado

### 1. Corrigir a semântica de fence antes de ampliar DirectX

A primeira correção deve impedir que uma fence seja considerada concluída por uma escrita CPU-side. Deve ser criado um caminho GPU-side de write-data/EOP para o endereço físico da fence, e o `QueryCurrentFence` deve refletir somente valores confirmados pelo hardware. Sem isso, qualquer avanço no UMD apenas aumenta o risco de corrupção de memória.

### 2. Integrar firmware e estado de engine

O `StartDevice` não deve marcar `FirmwareLoaded`, `FirmwareReady` e `GfxReady` como verdadeiros apenas para liberar o bring-up. O código deve carregar ou detectar explicitamente CP/SDMA/PSP, acordar GFXOFF via SMU e verificar clocks/estado do engine antes do primeiro submit.

### 3. Provar um teste mínimo de hardware

Antes de D3D11, o KMD deve executar uma sequência pequena e observável: NOP, write-data GPU para fence, readback por CPU, interrupção EOP, timeout/reset e repetição. Esse teste precisa produzir logs de RPTR/WPTR, fence antes/depois, status IH e resultado de reset.

### 4. Construir GPUVM/VidMm mínimo real

Depois da fence, o port precisa escolher uma política de memória. A opção mais simples é começar com system-memory/aperture fixa, page tables mínimas e um único VMID, sem alegar VRAM local. Só depois devem entrar residency, eviction, GPU virtual address e múltiplos segmentos.

### 5. Fazer um UMD D3D11 pequeno e coerente

O primeiro UMD útil deve limitar explicitamente o conjunto de recursos: buffers lineares, upload/copy, um clear simples e sincronização. Cada callback precisa gerar comandos válidos e usar allocations/handles que o KMD realmente reconhece. A tabela deve ser específica para o header WDK alvo; casts entre D3D11 e D3D10 não devem ser usados como substituto da ABI correta.

### 6. Implementar DXGI/present e deixar D3D12 por último

Swapchain, formatos, present, resource views e sincronização devem ser implementados somente depois de buffer/clear/copy estáveis. D3D12 deve vir após GPUVM, fences, command queues, heaps, resource states e device removal estarem funcionando.

## Veredito final

A análise recebida foi boa como avaliação de maturidade e acertou o ponto essencial: o projeto é um esqueleto avançado de pesquisa, não um driver de jogos acabado. Ela precisa ser corrigida em três pontos: foi baseada em nomes do repositório antigo; descreveu uma política de memória anterior à atual; e atribuiu ao projeto atual um caminho de display mais completo do que os callbacks realmente implementam.

Ao mesmo tempo, a auditoria do código atual encontrou um problema mais grave que deve ser tratado antes de qualquer promessa de aceleração: a fence é sinalizada pelo CPU logo após a submissão. Enquanto esse comportamento não for substituído por uma conclusão real da GPU, o projeto deve ser considerado **bring-up de MMIO/ring**, e não aceleração DirectX funcional.

## Referências externas

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi-submitcommand "DxgkDdiSubmitCommand — Microsoft Learn"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi-querycurrentfence "DxgkDdiQueryCurrentFence — Microsoft Learn"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi-buildpagingbuffer "DxgkDdiBuildPagingBuffer — Microsoft Learn"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi-render "DxgkDdiRender — Microsoft Learn"
[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-a-user-mode-display-driver "Loading a user-mode display driver — Microsoft Learn"
[6]: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd "amdgpu no kernel Linux"
[7]: https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/include/asic_reg "Headers públicos de registradores AMD no Linux"


## Correções aplicadas após esta auditoria

Depois de confirmar os pontos acima, eu corrigi o caminho de submit para colocar um `WRITE_DATA` GPU-side depois do `INDIRECT_BUFFER` e removi a escrita CPU-side que fazia a fence parecer concluída cedo demais. Também removi as flags falsas de firmware/GFX/interrupção do `StartDevice`: os rings podem ser preparados, mas `FirmwareReady` e `GfxReady` só devem ser afirmados depois de carregamento e programação reais.

As operações de paging que ainda eram apenas sucesso/no-op passaram a retornar `STATUS_NOT_SUPPORTED` até que exista implementação SDMA/page-table. O reset invalida o engine e o restart não promete recuperação sem firmware pronto. No UMD, CreateDevice/caps ainda incompletos passaram a retornar `E_NOTIMPL` de maneira explícita. Essas mudanças não “fabricam” suporte DirectX, mas tornam o estado do driver compatível com o que realmente foi implementado.

A correção mais importante agora está no código, em `src/gfx/bc250_gfx.c:470-501`: a fence é destinada a ser escrita pela GPU através do pacote de comando, enquanto `QueryCurrentFence` apenas observa o buffer. Essa sequência ainda precisa ser validada com os offsets e o opcode exatos do Cyan Skillfish2 em hardware real.

**Autor:** ZEROAESQUERDA
