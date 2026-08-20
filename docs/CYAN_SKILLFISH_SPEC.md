# Especificação de implementação Cyan Skillfish/BC-250

## Objetivo

Este documento congela os fatos de hardware usados pelo KMD BC-250. O `amdgpu` Linux é a especificação executável; as interfaces Linux não são copiadas para Windows, mas seus fatos de hardware são reimplementados através das DDIs DXGK.

## Identificação

| Campo | Valor | Fonte/uso |
| --- | --- | --- |
| Vendor PCI | `0x1002` | AMD |
| IDs PCI cobertos | `0x13DB`, `0x13F9`, `0x13FA`, `0x13FB`, `0x13FC`, `0x13FE`, `0x143F` | tabela pública observada no `amdgpu` |
| Família AMD | Cyan Skillfish | seleção do ASIC no kernel |
| GPU target | `gfx1013` | Mesa/LLVM/RADV |
| GC | 10.1.3 | bloco gráfico |
| SDMA | 5.0.x | cópia/paging e transferência |
| Memória | UMA/GDDR6 compartilhada | BC-250; tamanho efetivo deve ser descoberto |
| Variante Skillfish2 | IDs `0x13FE` e `0x143F` | muda seleção de firmware/quirks |

## Firmware

Para Skillfish2, o snapshot upstream registra os nomes `cyan_skillfish2_ce.bin`, `cyan_skillfish2_pfp.bin`, `cyan_skillfish2_me.bin`, `cyan_skillfish2_mec.bin`, `cyan_skillfish2_mec2.bin`, `cyan_skillfish2_rlc.bin`, `cyan_skillfish2_sdma.bin` e `cyan_skillfish2_sdma1.bin`. A família também declara `amdgpu/cyan_skillfish_gpu_info.bin` para informação do ASIC. A seleção do driver Windows deve ser feita pelo ID/revisão e pela assinatura/tamanho do blob, nunca apenas pela semelhança com Navi 10.

O caminho de carregamento deve ser:

1. localizar o firmware no Driver Store/arquivo de recursos;
2. validar tamanho, alinhamento, versão e assinatura/formato AMD esperado;
3. mapear a memória firmware apenas em buffer não paginável;
4. carregar PSP/SMU antes de iniciar MEC/PFP/ME/RLC/SDMA;
5. registrar quais IP blocks foram aceitos;
6. impedir submissão se qualquer firmware obrigatório estiver ausente ou inconsistente.

## SMU/DPM/GFXOFF

O caminho Cyan Skillfish usa SMU v11.8 e um mapa específico de mensagens. O conjunto mínimo observado inclui teste/versão (`TestMessage`, `GetSmuVersion`, `GetDriverIfVersion`), controle de GFXOFF (`EnableGfxOff`, `AllowGfxOff`, `DisallowGfxOff`, `GetGfxOffStatus`), tabelas (`SetDriverTableDramAddrHigh`, `SetDriverTableDramAddrLow`, `TransferTableSmu2Dram`, `TransferTableDram2Smu`), reset (`GfxDeviceDriverReset`), relógios (`RequestGfxclk`, `GetGfxclkFrequency`, `SetHardMinGfxClk`, `SetSoftMaxGfxClk`) e métricas (`GetEnabledSmuFeatures`, temperatura, potência e tensão).

O KMD deve manter uma máquina de estados explícita: `Unknown → FirmwareReady → SmuReady → ClocksReady → GfxAwake → VmReady → RingReady`. GFXOFF só pode ser permitido depois de filas e fences funcionarem; durante reset, paging ou submit, deve ser bloqueado.

## VM, rings e fences

O bloco GC 10.1.3 deve ser tratado como `gfx1013`, sem reutilizar offsets Navi 10. A sequência de bring-up é: configurar VM hub/GART com endereço GPU virtual conhecido, alocar ring/control blocks em memória UMA válida, inicializar PFP/ME/MEC/RLC e SDMA, configurar read/write pointers, habilitar interrupção, inserir fence PM4 e aguardar atualização monotônica.

Nenhum comando vindo do UMD pode ser enviado diretamente ao hardware. `DxgkDdiRender` deve validar o command buffer e produzir DMA/patch locations; `DxgkDdiSubmitCommand` deve aceitar somente DMA já validado e escrever o fence no ring; `DxgkDdiQueryCurrentFence` deve ler o fence real; qualquer timeout deve entrar em reset/TDR.

## Memória UMA/WDDM

O segmento de sistema é implícito no VidMm. O KMD deve reportar um aperture segment usando `AgpApertureBase`, `AgpApertureSize` e `AgpFlags` fornecidos por `DXGK_QUERYSEGMENTIN`. Um segmento local/UMA adicional só é permitido após a descoberta de uma faixa física GPU-visível e de uma reserva firmware real. A capacidade nominal de 16 GB não deve ser usada diretamente como `Size` de VRAM.

## Mapeamento para o KMD Windows

| amdgpu Linux | KMD BC-250 |
| --- | --- |
| tabela PCI/`CHIP_CYAN_SKILLFISH` | INF + validação PnP + seleção Skillfish2 |
| IP discovery | parser binário validado e tabela de IP runtime |
| `request_firmware` | arquivo/recursos do Driver Store com validação |
| PSP/SMU | mailbox MMIO e estados de firmware |
| `gmc`/VM/GART | segmentos VidMm, paging buffer e page tables |
| `gfx_v10_0`/`gfx_v10_3` adaptado | ring, PM4, fences e reset |
| `sdma_v5_0` | engine de paging/copy |
| IRQ/DPC | `DxgkDdiInterruptRoutine`/DPC |
| DRM/KMS | VidPn/present/display |
| Mesa/RADV | UMD DirectX/Vulkan compatível com o contrato KMD |

## Limites

O snapshot upstream fornece a lógica e os valores necessários para escrever o código, mas não prova que todos os BC-250 tenham a mesma revisão de BIOS, split de memória, BARs ou blobs. Por isso, o driver continua usando descoberta runtime e gates fail-closed. A compilação em Windows requer Visual Studio/WDK; a validação de PM4, firmware, memória e TDR requer uma placa física.

## Referências

[1]: https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/amdgpu "Linux amdgpu"
[2]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c "Cyan Skillfish SMU"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments "Microsoft WDDM GPU segments"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nf-dispmprt-dxgkinitialize "Microsoft DxgkInitialize"

## Mapeamento de IP blocks

A rotina upstream `cyan_skillfish_reg_base_init` inicializa apenas os blocos necessários ao driver e associa instâncias aos grupos GC, HDP, MMHUB, ATHUB, NBIO, MP0, MP1, VCN, DF, DCE/DMU, OSSSYS, SDMA0/1, SMUIO, THM e CLK. Ela também define `adev->gfx.xcc_mask = 1`.

No KMD Windows, essa tabela deve ser representada por um descritor de IP runtime. Os offsets concretos devem vir do equivalente `cyan_skillfish_ip_offset.h` e da versão descoberta, sendo usados somente depois de validar o bloco e seu BAR. O driver não deve reutilizar a tabela de offsets de `gfx_v10_0`/Navi 10 por analogia de ISA.

A rotina inicial de Windows pode usar o mesmo conceito de máscara: uma instância GC/XCC para o BC-250 e engines SDMA separadas apenas se a descoberta/firmware informar que elas estão presentes. Cada IP deve ter estado `Present`, `FirmwareReady`, `MmioReady` e `Enabled`; qualquer dependência ausente impede o próximo estágio.

## Lista linux-firmware verificada

A árvore pública atual do `linux-firmware/amdgpu` contém os seguintes blobs Skillfish2:

```text
cyan_skillfish2_ce.bin
cyan_skillfish2_me.bin
cyan_skillfish2_mec.bin
cyan_skillfish2_mec2.bin
cyan_skillfish2_pfp.bin
cyan_skillfish2_rlc.bin
cyan_skillfish2_sdma.bin
cyan_skillfish2_sdma1.bin
```

A listagem foi consultada diretamente na API pública do repositório `kernel-firmware/linux-firmware`. A lista não inclui um blob de firmware Windows; ela fornece nomes e artefatos para a tabela de seleção do projeto. O loader Windows deverá copiar esses arquivos para o Driver Store ou incorporá-los como recursos, respeitando as licenças e redistribuição do linux-firmware.
