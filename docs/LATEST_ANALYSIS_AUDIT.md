# Auditoria da análise histórica mais recente

## Minha leitura do estado atual

Eu revisei a análise que percorreu a cadeia de commits desde o protótipo antigo até o estado atual. A conclusão faz sentido: o projeto passou a ter uma infraestrutura de bring-up muito mais séria, mas ainda não atravessou a etapa mais importante, que é provar a execução do CP/firmware em uma BC-250 física.

O commit grande que substituiu o esboço antigo trouxe uma organização melhor de hardware, GFX, memória, SMU, firmware, KMD, UMD e referências do amdgpu. Ainda assim, nomes de commits e quantidade de arquivos não são prova de funcionalidade. Eu considero a separação entre `Present`, `Valid`, `Loaded` e `Ready` uma das correções mais importantes porque impede que o KMD confunda arquivo presente com microcode executando.

## O que já está em uma posição útil

Os rings e fences têm memória contígua, endereços físicos e estruturas de leitura monotônica. O submit prepara um `INDIRECT_BUFFER` seguido de `WRITE_DATA`. O controle do `WRITE_DATA` agora usa `WRITE_DATA_DST_SEL(5) | WR_CONFIRM`, seguindo o formato observado no teste de fence GFX10 do amdgpu. A fence ainda é candidata até a BC-250 executar o pacote, mas o CPU não inventa mais a conclusão.

O modelo de memória também está mais correto. A árvore não transforma automaticamente os 16 GB compartilhados em VRAM local. Ela começa com aperture/UMA conservadora e só cria uma faixa local quando uma faixa GPU-visible real é descoberta. O que ainda falta é GPUVM: page tables, VMID, GPUVA, residency, eviction, mapping, invalidação de TLB e coerência de cache.

## A última barreira real

A barreira principal continua sendo o carregamento de CP/SDMA. Os blobs públicos estão no repositório, mas o KMD ainda não os transfere pelo PSP/CP. Eu acrescentei `Bc250FirmwareMarkLoaded` para que um loader futuro só marque uma imagem depois de uma transferência bem-sucedida. `Bc250FirmwareCommitReady` continua exigindo que todos os bits obrigatórios estejam em `LoadedMask`.

Sem uma BC-250, não seria correto escrever uma rotina que apenas copie blobs para memória e declare que o CP saiu do halt. O carregamento depende da sequência de PSP/boot firmware, endereços, estado do SMU e registradores específicos. O próximo teste correto é confirmar essa sequência no hardware, não preencher a flag manualmente.

## Correção dos offsets de interrupção

Encontrei também uma inconsistência importante: os offsets de status/ack do IH estavam zerados, mas o projeto Debug/Release ainda compilava com `BC250_GFX_INTERRUPT_OFFSETS_VALIDATED=1`. Corrigi isso. Agora os offsets GC/SDMA e IH são tratados como candidatos, os dois gates ficam em zero e o ISR não toca o registrador 0. A infraestrutura de ISR/DPC permanece no código para receber a tabela real quando ela for confirmada.

## MVP D3D11

A análise recomenda um MVP D3D11, e eu concordo com a ordem, mas não existe base suficiente para fingir que ele está implementado. O export D3D11 continua retornando `E_NOTIMPL` porque ainda faltam a ABI/tabela D3D11 correta, allocations GPUVA, recursos, shaders, state tracking, command lists, present e sincronização.

O marco intermediário que eu considero implementável é menor: CP carregado, NOP, `WRITE_DATA` GPU-side, fence observável, EOP/IH, reset e um buffer/clear simples no KMD. Depois disso, um UMD D3D11 limitado pode ser construído com headers WDK corretos e somente os callbacks que tiverem suporte real no KMD.

## Display e power

DisplayPort, EDID, HPD, link training, modeset, page flip, VSync e Present continuam fora do que está comprovado. O power management também fica restrito a D0; D1/D2/D3 retornam não suportado até que a sequência SMU/GFXOFF seja validada.

## Conclusão

A análise histórica está correta ao dizer que o projeto deixou de ser apenas um conjunto de stubs e virou uma base séria de bring-up. Ela também está correta ao dizer que a próxima fronteira é Windows → KMD → MMIO → CP → PM4 → GPU → memória → interrupção → WDDM.

As correções desta rodada tornam essa fronteira mais honesta e tecnicamente segura: gates de offsets inválidos foram desativados, o controle do pacote de fence foi aproximado do amdgpu GFX10 e o ciclo de firmware carregado ganhou uma API explícita. Sem hardware real, não vou declarar que o CP está executando nem que D3D11 está funcionando.

**Autor:** ZEROAESQUERDA
