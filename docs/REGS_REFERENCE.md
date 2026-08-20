# Referência de registradores para rings

## Estado da validação

A tabela em `src/gfx/bc250_gfx_regs.h` é uma **referência de port** GC10/SDMA5. Os offsets continuam sendo candidatos de família e `BC250_GFX_OFFSETS_VALIDATED` fica em `0` até existir confirmação específica da revisão Cyan Skillfish2. A rotina pode preparar memória e registrar informações, mas não deve escrever MMIO de engine só porque a tabela genérica existe.

## GC/GFX

Os candidatos `CP_RB0_BASE`, `CP_RB0_BASE_HI`, `CP_RB0_CNTL`, `CP_RB0_RPTR`, `CP_RB0_WPTR` e `CP_RB0_WPTR_HI` foram transcritos do header público `gc_10_1_0_offset.h`, usado como a família GC10.1 mais próxima disponível no snapshot. Os valores no header Linux são índices de registradores; o driver Windows converte índices em offsets de bytes multiplicando por quatro.

A fonte pública é o arquivo [gc_10_1_0_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h), mantido no repositório do kernel Linux. A própria implementação do amdgpu usa os registradores `mmCP_RB0_*` durante a inicialização do ring, mas o port não deve assumir que a tabela GC genérica substitui a tabela `cyan_skillfish_ip_offset.h`.

## SDMA

Os candidatos de SDMA0/SDMA1 usam os grupos de registradores RLC0/RLC1 publicados no header `sdma5_4_2_2_offset.h`. Essa correspondência é uma aproximação de IP SDMA5 e ainda precisa de confirmação específica para o IP `5.0` declarado pela BC-250. A fonte pública é [sdma5_4_2_2_offset.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/include/asic_reg/sdma5/sdma5_4_2_2_offset.h).

## PSP KM/GPCOM

O módulo `src/firmware/bc250_psp.h` registra os offsets observados no caminho PSP KM/GPCOM da BC-250 real analisada externamente. A base MP0 é `0x58000` em bytes dentro da BAR5, derivada do IP discovery `0x16000` em DWORDs. O mapa usado pelo ring é:

| Registro | Offset BAR5 em bytes | Uso |
|---|---:|---|
| `C2PMSG_33` | `0x58184` | Estado de inicialização PSP, somente referência |
| `C2PMSG_35/36/37` | `0x5818C/0x58190/0x58194` | Comandos antigos de bootloader, não usados pelo ring |
| `C2PMSG_64` | `0x58200` | Tipo de ring, TOS-ready e resposta |
| `C2PMSG_67` | `0x5820C` | WPTR do ring |
| `C2PMSG_69/70/71` | `0x58214/0x58218/0x5821C` | Endereço físico baixo/alto e tamanho do ring |
| `C2PMSG_81` | `0x58244` | Estado do SOS |
| `C2PMSG_101` | `0x58294` | Scratch, somente referência |

Esses offsets estão implementados como dados de protocolo, mas `BC250_PSP_RING_VALIDATED=0` mantém as escritas bloqueadas. O módulo não considera a criação do ring suficiente para `PspReady`: primeiro precisa receber uma fence e uma resposta `SUCCESS` de `GET_FW_ATTESTATION`. O flush HDP está em gate separado (`BC250_PSP_HDP_OFFSETS_VALIDATED=0`) porque a referência externa usa `HDP_MEM_COHERENCY_FLUSH_CNTL` e `HDP_DEBUG0`, mas esses offsets ainda não foram validados nesta árvore.

Os valores externos de TMR — endereço GPU/MC próximo de `0xF40F800000`, offset de aperture `0x0F800000` e tamanho padrão de 4 MiB — ficam apenas como referências de teste. `Bc250PspSetupTmr` exige que o chamador forneça endereços GPU/MC e físico de sistema diferentes; ele não fixa esses valores no hardware.

## Estado atual do bring-up

A presença dos offsets permite manter a tabela pronta para comparação e preparar rings/fences em memória, mas não habilita a escrita MMIO. `BC250_GFX_OFFSETS_VALIDATED=0`, `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=0`, `BC250_PSP_RING_VALIDATED=0` e `BC250_PSP_HDP_OFFSETS_VALIDATED=0` em Debug e Release; o ISR não toca os offsets zero de status/ack e o PSP não escreve C2PMSG. O firmware também precisa estar efetivamente carregado antes de `Bc250ProgramGfxRings` poder operar. Se a tabela genérica não corresponder à revisão física, o driver deve permanecer em estado não pronto em vez de arriscar TDR ou corrupção.
