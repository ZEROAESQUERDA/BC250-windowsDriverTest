# Steam Deck/Van Gogh como referência

A Valve publicou um pacote oficial de drivers Windows para o Steam Deck em 2022: https://store.steampowered.com/news/app/1675200/view/3131696199122435099

O valor dessa fonte é confirmar que existe uma implementação Windows funcional para uma APU AMD com memória UMA e GPU RDNA, não fornecer código-fonte do KMD/UMD. O pacote Windows é específico do SoC Van Gogh/Aerith e contém binários/INF/firmware de uma plataforma diferente; não deve ser tratado como driver genérico para Cyan Skillfish.

A referência deve ser usada no nível de arquitetura: Windows pode operar uma APU AMD com um KMD WDDM, UMD DirectX, memória compartilhada, power management e firmware. A implementação do BC-250 ainda precisa substituir os contratos de hardware, IP blocks, PCI IDs, firmware, MMIO e tabelas SMU pelos equivalentes Cyan Skillfish.

## Comparação de camadas

| Camada | Steam Deck / Van Gogh | BC-250 / Cyan Skillfish | Valor como referência |
| --- | --- | --- | --- |
| CPU/APU | SoC customizado com CPU Zen 2 e GPU integrada RDNA2 | APU customizada com CPU Zen 2 e GPU integrada/compartilhada | Alto para modelo UMA e sincronização CPU/GPU |
| Memória | LPDDR5 compartilhada no SoC, com política UMA definida pela plataforma | GDDR6 compartilhada na placa, com reservas/split e firmware próprios | Médio-alto para VidMm; baixo para tamanhos/flags exatos |
| GPU ISA | GFX10.3/Van Gogh, normalmente identificado como `gfx1033` | GFX10.1.3/Cyan Skillfish, `gfx1013` | Médio para conceitos PM4/VM; baixo para offsets e firmware |
| IP discovery | Tabelas e versões Van Gogh específicas | IP discovery e blocos Cyan Skillfish específicos | Alto como método, não como dados |
| SMU/DPM | `vangogh_ppt.c` usa SMU v11.5, tabelas WATERMARKS/DPMCLOCKS/CUSTOM_DPM e muitos comandos de power | `cyan_skillfish_ppt.c` usa SMU v11.8, mapa de mensagens menor e tabela SMU_METRICS; Skillfish2 exige inicialização própria | Alto para arquitetura de mailbox/tabelas; baixo para IDs e layouts |
| GFXOFF | Van Gogh implementa comandos explícitos Enable/Allow/Disallow GFXOFF | Cyan Skillfish2 usa o bloco SMU correspondente e precisa evitar GFXOFF antes de submeter | Alto para a máquina de estados; comandos diferem |
| Windows | Valve publicou pacote Windows funcional para o Steam Deck | Não há pacote AMD/Valve equivalente para BC-250 | Alto como prova de viabilidade do modelo WDDM/UMA; não fornece código |
| KMD/UMD | Binários proprietários do pacote Windows | KMD/UMD próprio ainda precisa ser construído | Alto para definir interfaces; impossível copiar sem fonte |

## Conclusão de engenharia

O Steam Deck é uma referência válida para o **desenho do sistema**, especialmente para a política UMA: uma região reservada/visível quando existir, um aperture/GART para páginas do sistema, GPU virtual address, paging e sincronização. Ele também mostra que um mesmo conceito AMD pode ter uma implementação Windows WDDM distinta da implementação Linux `amdgpu`.

A parte que não pode ser transplantada diretamente é a camada de hardware: Van Gogh usa IP `gfx10.3`, firmware, tabelas SMU e offsets próprios. O BC-250 usa `gfx10.1.3`, Cyan Skillfish/Skillfish2, firmware e mapa de registradores diferentes. A tabela de mensagens `vangogh_message_map` é muito maior e inclui comandos como `EnableGfxOff`, `AllowGfxOff`, `SetHardMinGfxClk` e `GetGfxOffStatus`; o mapa Cyan Skillfish upstream é menor e usa `RequestGfxclk`, `SetDriverTableDramAddrHigh/Low`, `TransferTableDram2Smu`, `GetEnabledSmuFeatures` e métricas.

A estratégia correta é, portanto, **portar a arquitetura de controle**, não o driver binário:

1. reproduzir no KMD BC-250 a máquina de estados `power-on → firmware/SMU → DPM → GFXOFF control → VM → rings`;
2. usar o modelo de segmentos UMA do WDDM demonstrado pela classe de APU do Steam Deck;
3. substituir todos os registros, IP versions, firmware e mensagens pelos dados Cyan Skillfish obtidos do `amdgpu`;
4. criar um UMD com as interfaces WDDM/DirectX correspondentes, pois o pacote Windows do Steam Deck não fornece código reutilizável;
5. validar cada etapa no BC-250 antes de anunciar capacidades ao runtime.

## Referências

[8]: https://store.steampowered.com/news/app/1675200/view/3131696199122435099 "Valve — Steam Deck Windows drivers"

[9]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu11/vangogh_ppt.c "Linux amdgpu — Van Gogh power management"

[10]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c "Linux amdgpu — Cyan Skillfish power management"

[11]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments "Microsoft Learn — GPU segments"
