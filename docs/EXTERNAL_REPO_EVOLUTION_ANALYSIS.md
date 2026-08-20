# Análise do repositório AMD-BC-250-Windows-Driver

## Conclusão executiva

Sim. A análise do repositório [AMD-BC-250-Windows-Driver](https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver) mostra uma evolução que pode melhorar de forma significativa o projeto `BC250-windowsDriverTest`, principalmente no caminho de inicialização do **PSP KM/GPCOM ring** e no carregamento de firmware por `LOAD_IP_FW`. Esse material é mais valioso para o KMD do que para o UMD DirectX.

A principal descoberta externa é que o autor relata ter criado, em uma BC-250 real, um ring PSP de 4 KiB usando a base MP0 `0x58000` dentro da BAR5, submetido comandos com fences e recebido sucesso de `GFX_CMD_ID_GET_FW_ATTESTATION`. O relatório do próprio repositório também registra `SETUP_TMR` aceito pelo SOS usando um endereço GPU/MC de VRAM e um endereço físico do sistema distintos. [1] [2]

Isso não prova automaticamente que todo o código externo esteja pronto para ser incorporado, nem libera os gates de GFX/IH do nosso projeto. Porém, fornece uma implementação de referência muito mais concreta para atacar o maior bloqueio atual: separar “firmware disponível” de “firmware efetivamente aceito pelo PSP/SOS”.

## O que o repositório externo acrescenta

| Área | Evidência/implementação externa | Valor para `BC250-windowsDriverTest` |
|---|---|---|
| PSP/GPCOM | Base MP0 `0x58000`; C2PMSG 64/67/69/70/71/81; criação e submissão de ring KM | **Muito alto**. É o caminho prioritário para testar PSP e firmware no Windows. |
| Fence PSP | Frame de 64 bytes, buffer de comando, fence GPU-visível, WPTR em dwords e polling com flush HDP | **Alto**. Serve como prova independente de que o PSP consumiu o comando. |
| `LOAD_IP_FW` | Firmware é lido no kernel, colocado em buffer contíguo GPU-visível e enviado pelo comando `GFX_CMD_ID_LOAD_IP_FW` | **Muito alto**, mas precisa ser integrado ao estado `Present → Valid → Loaded → Ready`. |
| `SETUP_TMR` | TMR aceito com endereço MC de VRAM e endereço físico de sistema separados; tamanho de 4 MiB documentado | **Alto**, pois dá um teste concreto para firmware/PSP e para o modelo de memória física. |
| GPUVM/GART | Page tables de quatro níveis, 48-bit GPUVA, VMIDs, GART e TLB invalidate | **Médio**. Os algoritmos são úteis, mas as escritas de ativação têm bloqueios específicos nessa unidade. |
| SMU | Mailbox, telemetria, frequência, tensão e sequência de governor | **Médio/alto**. Complementa o módulo SMU já existente, desde que mensagens e versão sejam confirmadas. |
| UMD | UMD D3D9 com várias funções e headers D3D10/11/12 | **Baixo/médio**. É referência de ABI, não uma implementação D3D11 completa. |
| `wddm-ps5` | Miniport WDDM/display-only com muitas DDIs e diagnóstico persistente | **Médio** para instrumentação/VidPN; **baixo** como merge direto. |
| Test tools | Probes de PSP, SMU, VM, rings e firmware | **Alto** para criar testes Windows focados depois que houver hardware. |

## PSP KM/GPCOM ring: a evolução mais importante

O código externo define a base MP0 como `0x58000` em bytes, derivada do IP discovery MP0 `0x16000` em unidades de DWORD. Os offsets usados são:

| Registro | Offset BAR5 em bytes | Função descrita |
|---|---:|---|
| `C2PMSG_64` | `0x58200` | Tipo de ring, estado TOS/response |
| `C2PMSG_67` | `0x5820C` | WPTR do ring |
| `C2PMSG_69` | `0x58214` | Endereço físico baixo do ring |
| `C2PMSG_70` | `0x58218` | Endereço físico alto do ring |
| `C2PMSG_71` | `0x5821C` | Tamanho do ring |
| `C2PMSG_81` | `0x58244` | Estado do SOS |

O fluxo externo é compatível conceitualmente com a separação que já adotamos no firmware: primeiro aguarda o estado de prontidão, depois cria o ring, envia uma frame PSP de 64 bytes, atualiza o WPTR e aguarda uma fence escrita pelo PSP. O `PSP_RING_SUBMIT` também limita o payload, serializa o acesso com mutex, verifica endereços físicos nulos e limpa os buffers antes do uso. [2] [3]

A adaptação correta não é copiar o KMD externo inteiro. É criar um módulo isolado no nosso projeto, por exemplo `src/firmware/bc250_psp.c` e `src/firmware/bc250_psp.h`, contendo as seguintes responsabilidades:

1. Criar e destruir o ring KM do PSP.
2. Montar a estrutura `psp_gfx_cmd_resp` com o payload no offset correto.
3. Montar a `psp_gfx_rb_frame` de 64 bytes.
4. Atualizar o WPTR com wrap validado.
5. Aguardar a fence com timeout e flush HDP.
6. Registrar cada resposta PSP no estado de firmware.
7. Liberar buffers somente quando houver fence ou quando o reset provar que o PSP não os usará mais.

A integração deve ficar inicialmente atrás de um gate próprio, por exemplo `BC250_PSP_RING_VALIDATED=0`, sem liberar `GfxReady` nem `AllowRegisterWrites`. O primeiro teste seguro seria apenas `GET_FW_ATTESTATION`, pois ele comprova a comunicação PSP sem tentar iniciar GFX, SDMA ou compute.

## `LOAD_IP_FW` e `SETUP_TMR`

O repositório externo relata que `LOAD_IP_FW` foi testado por um IOCTL que recebe o tipo de firmware e um caminho NT, lê o arquivo no kernel, aloca um buffer contíguo GPU-visível e envia o comando PSP. Um detalhe importante é que o caminho precisa ser no formato `\\SystemRoot\\...`, e não um caminho Win32 `C:\\...`, porque a leitura usa APIs nativas do kernel. [2] [3]

Também há um resultado externo relevante para `SETUP_TMR`: o SOS aceitou um endereço GPU/MC de VRAM próximo da região de VRAM da placa e um endereço físico de sistema diferente. O documento informa que tentativas usando o mesmo endereço de CPU para os dois campos falharam. A consequência para o nosso projeto é clara: antes de declarar `FirmwareReady`, precisamos distinguir endereço físico de CPU, endereço MC/GPU e endereço GPUVA/GART. Não é seguro reutilizar `MmGetPhysicalAddress` como se ele fosse automaticamente um endereço válido para todos os campos PSP.

O resultado externo de `LOAD_IP_FW` deve ser tratado com granularidade. O relatório registra sucesso para o firmware SMU, enquanto alguns tipos CP/SDMA/RLC retornam códigos de item ausente, comando não suportado ou firmware já carregado. Isso reforça a política que já adotamos: `Bc250FirmwareMarkLoaded` deve ser chamado somente quando a resposta do PSP confirmar aquele tipo específico, e não depois de um submit genérico ou da simples presença do arquivo.

## GPUVM/GART: aproveitar os algoritmos, não as escritas cegas

O repositório externo possui uma implementação de referência de GART, page tables de quatro níveis, VMIDs e invalidação de TLB. A organização é útil para evoluir nosso `BuildPagingBuffer`, especialmente na representação de páginas de 4 KiB, entradas de 64 bits, índices de PML4/PD/PT e operações de map/unmap. [4]

Ao mesmo tempo, o próprio repositório relata que, na unidade Windows testada, registradores como `KIQ_SIZE`, `CP_HQD_*`, `GCVM PT_BASE` e `GFX_RING0_BASE_LO` são somente leitura, bloqueados ou controlados pelo SOS. Por esse motivo, eu não recomendo copiar a sequência externa de escrita de MC/GART/GPUVM diretamente para o nosso KMD. O que pode ser portado agora é a camada de dados e o algoritmo de construção; a ativação MMIO deve continuar sob gates e depender de confirmação específica da BC-250.

## `wddm-ps5`: útil como referência de integração, não como substituto

O subprojeto `wddm-ps5` contém uma árvore WDDM mais extensa, callbacks de VidPN, diagnóstico persistente no registro, breadcrumbs de `StartDevice` e uma camada de UMD. Isso pode inspirar melhorias de observabilidade no nosso KMD, especialmente porque o build Release do Windows costuma esconder `KdPrint`.

Não recomendo fazer um merge direto desse subprojeto. O próprio repositório usa overrides fixos de VRAM/GTT, perfis de compatibilidade, modos seguros e caminhos display-only. Além disso, seu README o classifica como candidato ainda não verificado para produção. A nossa árvore atual é mais conservadora nos gates de offsets, firmware e interrupções; substituir essa política pelo perfil externo poderia reintroduzir as mesmas falsas prontidões que já removemos.

## O que não deve ser copiado

Não recomendo copiar diretamente os overrides de 8 GiB/4 GiB/16 GiB, as afirmações de 40 CU desbloqueáveis, as escritas de SPI_PG/WGP, o fluxo de KIQ/HQD, a ativação de GART ou os caminhos de GFX/SDMA. No repositório externo, parte dessas áreas é explicitamente descrita como bloqueada pelo SOS, somente leitura, dependente de EFI/Linux ou ainda não reproduzida em Windows. [1] [3]

Também não recomendo importar todos os quase 400 test-tools. A quantidade é grande, vários testes foram removidos no último cleanup e alguns experimentos históricos foram classificados como perigosos ou causadores de hang/TDR. O melhor caminho é selecionar apenas os probes PSP/GPCOM, `SETUP_TMR`, SMU e VM necessários para reproduzir resultados.

## Plano de evolução recomendado

| Fase | Implementação | Critério de aceitação |
|---|---|---|
| 1 | Adicionar módulo PSP GPCOM gated, sem liberar GFX | `GET_FW_ATTESTATION` retorna sucesso e a fence PSP é alcançada. |
| 2 | Adicionar `SETUP_TMR` com validação de endereços CPU/MC | Resposta PSP bem-sucedida e estado TMR registrado sem usar endereço CPU como MC. |
| 3 | Integrar `LOAD_IP_FW` por tipo de firmware | Cada tipo altera `LoadedMask` somente após resposta positiva ou estado explicitamente “já carregado”. |
| 4 | Atualizar telemetria e `StartDevice` | Logs persistentes mostram etapas, resposta, timeout e motivo de bloqueio. |
| 5 | Reavaliar GPUVM/GART | Só liberar page tables/MMIO depois de confirmar registradores e mecanismo de VM no hardware. |
| 6 | Retomar GFX/SDMA | Apenas após PSP, firmware, engine, IH/EOP e fences comprovados individualmente. |
| 7 | Evoluir UMD D3D11 | Depois do KMD básico, implementar tabelas D3D11 reais; a ISA externa pode servir como referência de validação. |

## Decisão

A decisão técnica é **incorporar o caminho PSP GPCOM como evolução prioritária**, mantendo o restante do projeto atual. O repositório externo não invalida a arquitetura full-WDDM já criada; ele fornece o elo que faltava entre firmware público e a possibilidade de o PSP aceitar comandos em uma BC-250 real.

A análise foi transformada em uma integração seletiva: o módulo PSP foi criado em `src/firmware/bc250_psp.c/.h`, seu estado foi incorporado ao contexto WDDM, os gates foram adicionados ao projeto, o teste offline de layout foi incluído e o README/status/roadmap passaram a documentar a nova ordem de bring-up. A licença declarada do repositório externo é MIT, mas qualquer cópia substancial deve preservar o aviso de copyright e a licença correspondente. [5]

## Referências

[1]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver "Repositório externo AMD-BC-250-Windows-Driver"
[2]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/docs/PSP-GPCOM-RING-WORKING.md "Documentação externa do PSP GPCOM ring"
[3]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/AGENTS.md "Registro externo de hardware, PSP e resultados"
[4]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/src/kmd/amdbc250_dream_vm.c "Implementação externa de GPUVM/GART"
[5]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/blob/main/LICENSE "Licença MIT do repositório externo"
[6]: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver/tree/main/wddm-ps5 "Subprojeto externo wddm-ps5"

## Autor

**ZEROAESQUERDA**
