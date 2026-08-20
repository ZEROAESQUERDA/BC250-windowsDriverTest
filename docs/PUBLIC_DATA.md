# Dados públicos do AMD BC-250/Cyan Skillfish

## Conclusão

Há dados públicos suficientes para avançar além de um KMD genérico. O principal material adicional é o projeto comunitário [Keshas-dev/AMD-BC-250-Windows-Driver](https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver), que contém um KMD experimental, headers de registradores, documentação de bring-up, ferramentas e um caminho UMD. Ele não deve ser tratado como um driver DirectX completo: o próprio código principal mostrado no branch observado usa `KMDDOD_INITIALIZATION_DATA` e `DxgkInitializeDisplayOnlyDriver`, enquanto parte da aceleração aparece em caminhos WDM/IOCTL separados.

## Tabela de confiança

| Dado | Valor público | Confiança | Fonte/uso |
| --- | --- | --- | --- |
| Vendor PCI | `0x1002` | Alta | Tabela PCI do `amdgpu` upstream |
| IDs Cyan Skillfish | `0x13DB`, `0x13F9`, `0x13FA`, `0x13FB`, `0x13FC`, `0x13FE`, `0x143F` | Alta | `amdgpu_drv.c` upstream |
| IDs tratados como Skillfish2 | `0x13FE` e `0x143F` | Alta | `amdgpu_device.c` upstream |
| GPU target | `GFX10.1.3` / `gfx1013` | Alta | Kernel/Mesa/RADV |
| Memória | 16 GB GDDR6 compartilhada | Média-alta | Documentação comunitária e observações de placa |
| Modelo de memória | UMA, com split BIOS/dinâmico dependente da placa | Média-alta | Documentação comunitária; validar no firmware específico |
| GC base | `0x1260` | Média-alta | `ip_discovery`/Linux e documentação comunitária |
| BAR5 como região crítica | BAR5; exemplos públicos usam base física `0xFE800000` e 512 KiB | Média | Projeto Keshas-dev; nunca hardcode no KMD, usar recursos PnP traduzidos |
| NBIO SMN index/data | BAR5 + `0x38`/`0x3C` | Média-alta | Descoberta comunitária alinhada ao mecanismo `WREG32_PCIE` do Linux |
| MP0/MP1 C2PMSG | Há versões/rotas documentadas em bases `0x16000` e em bloco efetivo PSP C2PMSG `0x58000` | Média | Acesso depende do bloco e do caminho; não misturar MP1 SMN com MP0 PSP |
| GFX ring | PM4/GFX10, com CP ring e fences | Média | Linux `gfx_v10_0.c` e projeto experimental Windows |
| SDMA | SDMA GFX10, blobs Skillfish2 ou alternativas reportadas | Média | `sdma_v5_0.c` e documentação comunitária; validar firmware |
| VCN | Bloqueado/indisponível no BC-250 público | Média-alta | Documentação RADV/comunidade; não prometer encode/decode |
| DirectX | Não existe pacote AMD oficial para BC-250 | Alta | AMD não lista o dispositivo como produto suportado; projeto comunitário é experimental |

## Dados de registradores úteis para o projeto

O projeto comunitário de 40 CU documenta duas escritas opcionais no caminho GFX:

| Registrador lógico | Offset/representação pública | Valores observados |
| --- | --- | --- |
| `CC_GC_SHADER_ARRAY_CONFIG` | Offset por instância `0x0226f` no patch Linux; header Windows experimental usa aliases que variam com o espaço de registradores | `0xFFF80000` stock → `0xFFE00000` para liberar enumeração |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x1277` em representação por índice de registrador no relatório comunitário | `0x07` stock → `0x1F` para liberar dispatch |
| `RLC_PG_ALWAYS_ON_WGP_MASK` | Usado pelo patch comunitário como etapa de power gating | `0x1F` no modo de desbloqueio |

Essas escritas pertencem ao desbloqueio opcional de CUs e não devem entrar no primeiro boot do KMD. O código deve primeiro ler, registrar e restaurar os valores em caso de falha.

## SMU/Skillfish2

O achado público mais importante para uma BC-250B/Cyan Skillfish2 é que o GFX pode permanecer em GFXOFF se o SMU não receber as tabelas de DPM. A rota documentada é acessar SMN por meio de `BAR5+0x38/0x3C`, não mapear endereços SMN físicos diretamente. A documentação comunitária registra C2PMSG_66 em `0x03B10A08`, C2PMSG_82 em `0x03B10A48` e C2PMSG_90 em `0x03B10A68` no espaço SMN, com protocolo de mailbox de espera/ack/mensagem/espera/resposta.

O kernel Linux seleciona o bloco SMU v11.0 para Skillfish2 quando a combinação de IP/flags correspondente é detectada. Por isso, o KMD Windows deve separar duas variantes:

1. **Cyan Skillfish original:** não presumir o bloco SMU completo.
2. **Cyan Skillfish2/BC-250B:** implementar uma camada SMN/SMU e validar GFXOFF antes de tocar em rings.

## Como esses dados serão usados

A próxima revisão do código deve adicionar uma tabela de hardware com variante e confiança, registrar todos os BARs traduzidos com endereço e tamanho, identificar o BAR5 por posição/recursos sem hardcode físico, e implementar somente consultas seguras do SMU antes de qualquer comando que altere clocks, voltage, WGPs ou power gating.

O projeto público Keshas-dev é uma referência de engenharia e de dados, não uma prova de que a aceleração DirectX está pronta. A reutilização direta de código deve aguardar confirmação da licença efetiva e revisão de segurança; o projeto atual usa apenas os fatos e a arquitetura como referência.

## Referências

[1]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c "Linux amdgpu PCI table"

[2]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_device.c "Linux amdgpu device/variant handling"

[3]: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c "Linux GFX10 implementation"

[4]: https://github.com/duggasco/bc250-40cu-unlock "BC-250 40 CU register-level patch"

[5]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver "Experimental BC-250 Windows KMD/UMD project"

[6]: https://elektricm.github.io/amd-bc250-docs/hardware/specifications/ "Community BC-250 hardware specifications"

[7]: https://elektricm.github.io/amd-bc250-docs/drivers/radv/ "Community BC-250 RADV documentation"
