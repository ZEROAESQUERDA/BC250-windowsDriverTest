# Referência de registradores para rings

## Estado da validação

A tabela em `src/gfx/bc250_gfx_regs.h` é uma **referência de port** GC10/SDMA5. Os offsets continuam sendo candidatos de família e `BC250_GFX_OFFSETS_VALIDATED` fica em `0` até existir confirmação específica da revisão Cyan Skillfish2. A rotina pode preparar memória e registrar informações, mas não deve escrever MMIO de engine só porque a tabela genérica existe.

## GC/GFX

Os candidatos `CP_RB0_BASE`, `CP_RB0_BASE_HI`, `CP_RB0_CNTL`, `CP_RB0_RPTR`, `CP_RB0_WPTR` e `CP_RB0_WPTR_HI` foram transcritos do header público `gc_10_1_0_offset.h`, usado como a família GC10.1 mais próxima disponível no snapshot. Os valores no header Linux são índices de registradores; o driver Windows converte índices em offsets de bytes multiplicando por quatro.

A fonte pública é o arquivo [gc_10_1_0_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h), mantido no repositório do kernel Linux. A própria implementação do amdgpu usa os registradores `mmCP_RB0_*` durante a inicialização do ring, mas o port não deve assumir que a tabela GC genérica substitui a tabela `cyan_skillfish_ip_offset.h`.

## SDMA

Os candidatos de SDMA0/SDMA1 usam os grupos de registradores RLC0/RLC1 publicados no header `sdma5_4_2_2_offset.h`. Essa correspondência é uma aproximação de IP SDMA5 e ainda precisa de confirmação específica para o IP `5.0` declarado pela BC-250. A fonte pública é [sdma5_4_2_2_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/sdma5/sdma5_4_2_2_offset.h).

## Estado atual do bring-up

A presença dos offsets permite manter a tabela pronta para comparação e preparar rings/fences em memória, mas não habilita a escrita MMIO. `BC250_GFX_OFFSETS_VALIDATED=0` e `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0` em Debug e Release; o ISR não toca os offsets zero de status/ack. O firmware também precisa estar efetivamente carregado antes de `Bc250ProgramGfxRings` poder operar. Se a tabela genérica não corresponder à revisão física, o driver deve permanecer em estado não pronto em vez de arriscar TDR ou corrupção.
