# Mapa inicial de hardware — Cyan Skillfish / BC-250

## Princípio

O `amdgpu` Linux é usado como especificação executável do hardware. O objetivo não é copiar interfaces Linux para o WDDM, mas transportar os fatos de hardware e reimplementar o contrato usando as DDIs DXGK.

## Classificação

| Campo | Valor inicial | Origem |
| --- | --- | --- |
| Fabricante PCI | `0x1002` | AMD |
| IDs de dispositivo | `0x13DB`, `0x13F9`, `0x13FA`, `0x13FB`, `0x13FC`, `0x13FE`, `0x143F` | Tabela Cyan Skillfish observada no amdgpu |
| Família Mesa | GFX10.1 / RDNA1 | `amd_family.h` |
| Target de shader | `gfx1013` | Mesa/LLVM/RADV |
| GC | 10.1.3 | Caminho Cyan Skillfish do amdgpu |
| SDMA | 5.0.x | Caminho Cyan Skillfish do amdgpu |
| Memória | UMA/GDDR6 compartilhada | Característica da placa BC-250 |

## Correspondência com WDDM

| Linux/amdgpu | KMD WDDM BC-250 |
| --- | --- |
| PCI device table | INF + validação do dispositivo no `StartDevice` |
| `amdgpu_device` | `BC250_DEVICE_CONTEXT` |
| `amdgpu_ip_block` | Módulos de inicialização por IP block |
| BAR/MMIO mapping | Recursos PnP + `MmMapIoSpaceEx` |
| TTM/GTT/VM | VidMm callbacks + segmentos e GPU VA próprios |
| rings/queues | Engine nodes, contexts, command submission e fences |
| IRQ/DPC | `DxgkDdiInterruptRoutine` + DPC |
| GPU recovery | reset/TDR callbacks e reinitialização |
| DRM/KMS | VidPn, present, connector, EDID e display DDIs |
| RADV winsys | UMD Vulkan que conversa com o KMD próprio |

## Riscos de interpretação

O ID PCI não determina sozinho o tamanho ou a organização da memória. O BC-250 não deve usar constantes de VRAM de uma RX 5700 XT. O mesmo vale para VCN, SMU, display e firmware: qualquer capacidade precisa ser observada na placa real e comparada ao código Cyan Skillfish do kernel.

## Fontes

- Linux amdgpu: <https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/amdgpu>
- Mesa family enumeration: <https://raw.githubusercontent.com/mirror/mesa/main/src/amd/common/amd_family.h>
- Mesa RADV documentation: <https://docs.mesa3d.org/drivers/radv.html>
- Linux Cyan Skillfish support commit: <https://github.com/torvalds/linux/commit/9e6a5cf1a23bf575e93544ae05585659063b1c18>

## Snapshot upstream adicional

Foi capturada uma cópia dos arquivos `amdgpu_drv.c`, `amdgpu_device.c`, `gfx_v10_0.c`, `cyan_skillfish_reg_init.c` e `sdma_v5_0.c` em `docs/upstream/`. A tabela PCI do kernel atual associa os IDs à família `CHIP_CYAN_SKILLFISH|AMD_IS_APU`; os IDs `0x13FE` e `0x143F` ativam a flag `AMD_APU_IS_CYAN_SKILLFISH2`. O código declara `amdgpu/cyan_skillfish_gpu_info.bin` e, para a variante Skillfish2, blobs como `cyan_skillfish2_ce.bin`, `cyan_skillfish2_pfp.bin`, `cyan_skillfish2_me.bin`, `cyan_skillfish2_mec.bin`, `cyan_skillfish2_mec2.bin`, `cyan_skillfish2_rlc.bin`, `cyan_skillfish2_sdma.bin` e `cyan_skillfish2_sdma1.bin`.

Esses nomes são evidência de que a implementação Windows não deve selecionar firmware Navi 10 por proximidade de ISA. A seleção precisa depender do ID/revisão e de uma tabela Cyan Skillfish própria. O snapshot é apenas referência de código upstream; seus arquivos Linux não são compiláveis como KMD WDDM.
