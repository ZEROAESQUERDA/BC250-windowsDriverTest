# Referência de registradores para rings

## Estado da validação

A tabela em `src/gfx/bc250_gfx_regs.h` é uma **referência de port** GC10/SDMA5. O macro `BC250_GFX_OFFSETS_VALIDATED` está em `1` no modo experimental e a rotina de programação MMIO pode escrever esses offsets durante o bring-up. Eles continuam sendo candidatos de família, não uma confirmação específica de cada revisão Cyan Skillfish2; o objetivo desta configuração é tentar a aceleração sem manter o gate desabilitado.

## GC/GFX

Os candidatos `CP_RB0_BASE`, `CP_RB0_BASE_HI`, `CP_RB0_CNTL`, `CP_RB0_RPTR`, `CP_RB0_WPTR` e `CP_RB0_WPTR_HI` foram transcritos do header público `gc_10_1_0_offset.h`, usado como a família GC10.1 mais próxima disponível no snapshot. Os valores no header Linux são índices de registradores; o driver Windows converte índices em offsets de bytes multiplicando por quatro.

A fonte pública é o arquivo [gc_10_1_0_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h), mantido no repositório do kernel Linux. A própria implementação do amdgpu usa os registradores `mmCP_RB0_*` durante a inicialização do ring, mas o port não deve assumir que a tabela GC genérica substitui a tabela `cyan_skillfish_ip_offset.h`.

## SDMA

Os candidatos de SDMA0/SDMA1 usam os grupos de registradores RLC0/RLC1 publicados no header `sdma5_4_2_2_offset.h`. Essa correspondência é uma aproximação de IP SDMA5 e ainda precisa de confirmação específica para o IP `5.0` declarado pela BC-250. A fonte pública é [sdma5_4_2_2_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/sdma5/sdma5_4_2_2_offset.h).

## Modo experimental ativado

A presença dos offsets agora habilita a sequência de bring-up: descobrir o BAR físico do SMN separadamente do BAR de registradores; consultar firmware/SMU quando disponível; alocar memória não paginável; programar base, RPTR e WPTR; e permitir `StartDevice`, `SubmitCommand` e `BuildPagingBuffer`. Nenhuma etapa adicional de bloqueio por validação foi mantida. Se a tabela genérica não corresponder à revisão física, o resultado esperado é falha de inicialização, TDR ou comportamento incorreto, motivo pelo qual a configuração deve ser testada primeiro em uma instalação de laboratório.
