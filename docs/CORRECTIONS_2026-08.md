# Correções aplicadas depois da auditoria

Eu revisei o código depois de receber uma análise externa do repositório. A análise acertou ao apontar que alguns caminhos estavam retornando sucesso antes de realmente executar a operação. Corrigi isso sem tentar esconder as partes que ainda dependem de uma BC-250 real.

## Fence de conclusão

Antes desta revisão, `Bc250SubmitDmaBuffer` escrevia o valor da fence diretamente pelo CPU logo depois de atualizar o WPTR. Isso fazia parecer que a GPU tinha terminado mesmo quando ela ainda nem tinha lido o ring. Removi essa escrita CPU-side.

Agora o submit monta um pacote `INDIRECT_BUFFER` seguido de um pacote PM4 `WRITE_DATA` candidato para a GPU escrever a fence na memória. `QueryCurrentFence` e o DPC só observam o valor que aparecer no buffer. A sequência ainda precisa ser validada no hardware Cyan Skillfish2, mas a semântica do código não afirma mais uma conclusão que o CPU inventou.

## Firmware e estado de prontidão

Os blobs públicos continuam no repositório, mas o KMD não os carregava pelo PSP/CP. Por isso retirei do `StartDevice` as atribuições que marcavam `FirmwareLoaded`, `FirmwareReady`, `GfxReady` e interrupções como ativas apenas porque a memória do ring tinha sido alocada.

Também endureci `Bc250FirmwareCommitReady`: validar imagens não basta; `LoadedMask` precisa cobrir a máscara obrigatória antes de `FirmwareReady` poder ser afirmado. O driver agora deixa o adaptador enumerado, registra que os rings foram preparados e mantém a aceleração pendente até existir carregamento efetivo do microcode.

## Paging, reset e restart

O caminho de Transfer CPU/MDL continua aceito somente quando os dois lados são system-memory com MDLs válidos. Fill, Discard, Map e Unmap, quando ainda não são executados por SDMA ou page tables, passaram a retornar `STATUS_NOT_SUPPORTED` em vez de sucesso vazio.

O reset agora invalida explicitamente `GfxReady`, interrupções e writes MMIO, limpa rings e fences e marca o device como parado. `RestartFromTimeout` não promete recuperação se firmware e GFX não estiverem prontos; nesse caso ele retorna `STATUS_DEVICE_NOT_READY`.

## UMD DirectX

As fronteiras OpenAdapter continuam disponíveis para o bring-up, mas CreateDevice e capabilities que ainda não possuem tabelas completas passaram a retornar `E_NOTIMPL` explicitamente. Isso vale para o caminho base, D3D10 e D3D12. Eu prefiro que o runtime falhe de forma identificável a anunciar uma GPU que não consegue executar recursos, shaders, estados, command lists, heaps, PSO ou sincronização.

## Resultado prático

Estas correções tornam o estado reportado pelo código mais confiável. O projeto continua sendo uma base de pesquisa WDDM, não um driver pronto para jogos. O próximo marco de hardware permanece: carregar firmware, executar NOP/WRITE_DATA por GPU, observar EOP/IH, validar fence e resetar o engine sem corromper memória.

## Autor

**ZEROAESQUERDA**
