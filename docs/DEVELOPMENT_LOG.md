# Diário de desenvolvimento

## Como comecei

Eu comecei este projeto porque queria entender se seria possível usar a AMD BC-250 no Windows 10 x64 sem depender de um driver oficial da AMD. A primeira decisão foi não tratar a BC-250 como uma Navi 10 comum. O caminho mais útil foi estudar o `amdgpu` do Linux, acompanhar os patches públicos de Cyan Skillfish e comparar a organização de memória com APUs, especialmente o Steam Deck.

A investigação confirmou que a placa tem características próprias: IDs PCI específicos, GPU GFX1013, memória compartilhada e um conjunto de blocos IP/firmware que não aparece como uma placa Radeon de varejo normal. Também confirmei que a BAR5 usada pelo Skillfish2 para acesso ao SMN não deve ser confundida com a BAR de registradores GC/SDMA. Essa separação virou uma regra importante no código.

## Primeira base do port

A primeira versão era essencialmente uma estrutura KMDOD. Ela tinha `DriverEntry`, INF, identificação PCI, callbacks de PnP/display e os diretórios reservados para hardware, memória, GFX e UMD, mas não havia aceleração. Eu mantive essa base como referência de bring-up porque ela ajuda a validar a enumeração do dispositivo antes de tentar operar rings.

Depois acrescentei a classificação de variante Cyan Skillfish/Skillfish2, a descoberta runtime de BARs, o caminho de mailbox SMU, a política de firmware e a documentação da memória UMA. O modelo de memória foi mantido conservador: o WDDM recebe a aperture/segmento que consegue fornecer, e o driver não inventa que todos os 16 GB estão automaticamente disponíveis como VRAM local.

## Rings, fences e interrupções

O próximo passo foi alocar rings e fences em memória não paginável e fisicamente contígua. Cada engine passou a ter endereço virtual, endereço físico, ponteiros de leitura/escrita e uma fence monotônica. Em seguida adicionei a tabela de offsets candidatos de GC10.1 e SDMA5, usando os headers públicos gerados do kernel Linux como referência de família.

Nesta etapa eu optei conscientemente por ativar o bring-up experimental. `BC250_GFX_OFFSETS_VALIDATED=1` e `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=1` ficam ligados nos projetos Debug e Release. A rotina agora tenta programar bases e ponteiros dos rings, emite NOP/INDIRECT_BUFFER, atualiza o WPTR e acompanha fences. A interrupção agenda DPC e o DPC consulta as fences.

Isso não é a mesma coisa que validar a BC-250. Eu não tenho a placa disponível para confirmar cada offset, a sequência de firmware, o estado GFXOFF ou o comportamento de TDR. A escolha de ativar os writes foi feita para não deixar o projeto parado em gates, mas o risco de TDR ou bugcheck está documentado aqui e no README.

## full-WDDM e UMD

Com rings e fences preparados, implementei o registro full-WDDM via `DRIVER_INITIALIZATION_DATA`/`DxgkInitialize`. O KMD agora cria handles privados para device, context e allocation, preenche as estruturas `DXGK_DEVICEINFO`/`DXGK_CONTEXTINFO`, cria backing UMA/system-memory e expõe Render, Patch, paging, SubmitCommand, QueryCurrentFence, preempção e reset.

Também ativei `BC250_ENABLE_DX_UMD=1`. A DLL exporta as entradas D3D9, D3D10, D3D11 e D3D12 que fazem sentido para o bring-up. Eu não descrevo isso como uma implementação completa das APIs: as tabelas de device functions ainda são mínimas e não há, por enquanto, um compilador de shaders, um tradutor completo de bytecode, heaps/PSO/root signatures D3D12 ou todo o sistema de recursos necessário para aplicações reais.

## Limpeza do repositório

O repositório original tinha um esboço separado, com nomes `amdbc250_*`, dois ZIPs antigos, scripts e documentos que não correspondem à estrutura atual. Para evitar duplicação, a publicação final usa somente a árvore `driver_bc250`: solução, projetos WDK, fontes KMD/UMD, INF, firmware, documentação e ferramentas de validação.

Também retirei da árvore nova materiais que não participam do build atual, como os headers vazios de `src/diag`/`src/display` e o diretório comunitário de desbloqueio de CUs. O código upstream usado como referência continua em `docs/upstream`, porque ele faz parte da justificativa técnica do port; ele não é compilado como parte do driver Windows.

## Verificações feitas

Eu rodei uma validação estrutural que analisa o XML dos projetos, confere os arquivos críticos, verifica os gates ativos, confirma o registro do UMD no INF e executa `git diff --check`. Também conferi o manifesto dos blobs Cyan Skillfish2. O sandbox onde este trabalho foi preparado é Linux e não tem Visual Studio/WDK nem uma BC-250, portanto ainda não consegui compilar o `.sys`/`.dll` ou observar o comportamento real da GPU.

## Próxima etapa prática

A próxima etapa é compilar a solução em uma máquina Windows 10 x64 com WDK, instalar em modo de teste e capturar os primeiros logs de `StartDevice`, BARs, SMU, rings, WPTR/RPTR e fences. Se o driver conseguir inicializar, o trabalho seguinte será substituir as partes no-op do UMD por tabelas de recursos e comandos que o runtime realmente consiga usar, começando por um conjunto pequeno de operações de buffer/clear antes de ampliar a cobertura DirectX.

## Autor

**ZEROAESQUERDA**


## Revisão posterior da estratégia

Recebi uma segunda análise externa do projeto e usei o texto para conferir se a documentação estava prometendo mais do que o código entrega. A análise acertou ao recomendar um caminho menor: primeiro firmware/CP, ring, fence, interrupção, reset e GPUVM; depois um MVP D3D11 com buffer, clear e um triângulo simples; D3D12, Vulkan e power management avançado ficam para depois.

Também corrigi o roadmap para refletir essa ordem. Não considero a presença dos exports `OpenAdapter*`, dos headers ou dos arquivos de firmware uma prova de que DirectX, DisplayPort ou microcode já funcionam. Eu prefiro manter o repositório explícito sobre o que foi realmente implementado, porque isso torna os próximos testes muito mais fáceis de interpretar.

**Autor:** ZEROAESQUERDA
