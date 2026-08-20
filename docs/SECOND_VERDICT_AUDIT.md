# Auditoria da segunda análise externa

## Resumo

A segunda análise entende corretamente que o projeto está mais organizado, mas ainda não executa um triângulo D3D11 em uma BC-250. Ela também acerta ao separar arquitetura proposta de funcionalidade comprovada. Eu confirmei, porém, que vários nomes e descrições da análise pertencem ao esboço antigo (`amdbc250_*`) e não à árvore atual (`src/kmd`, `src/gfx`, `src/umd`, `src/memory`, `src/smu` e `src/firmware`).

## Pontos confirmados

A contradição entre “UMD constrói PM4” e “D3D10/D3D11/D3D12 ainda precisam de implementação” foi corrigida na documentação mais nova. Os exports OpenAdapter existem, mas as tabelas completas de recursos, shaders, draw, present, command lists, heaps e sincronização não existem. A presença de headers ou exports não é prova de suporte DirectX.

A questão do CP firmware também é válida. Os blobs públicos estão incluídos e podem ser validados, mas não são carregados pelo KMD atual através do PSP/CP. O `StartDevice` agora deixa `FirmwareLoaded` e `FirmwareReady` falsos até que exista carregamento efetivo. A documentação passou a distinguir “blob presente”, “imagem validada” e “microcode carregado”.

A crítica à memória UMA permanece válida em nível de GPUVM. O código atual não anuncia simplesmente os 16 GB como VRAM local; começa com perfil aperture-only porque `DXGK_DEVICE_INFO` não informa sozinho a reserva real do firmware. Ainda faltam page tables, VMID, GPU virtual address, residency, eviction, mapeamento e coerência de cache.

A recomendação de começar por um MVP D3D11 antes de D3D12/Vulkan é a direção mais razoável. Mesmo assim, o MVP deve ser precedido por uma prova de KMD: firmware/CP, NOP, fence GPU-side, EOP/IH, reset e uma operação simples de buffer/clear.

## Pontos desatualizados ou superestimados

A análise cita arquivos `amdbc250_kmd.c`, `amdbc250_hw_init.c`, `amdbc250_umd.c` e `amdbc250_d3d*.h`. Esses arquivos pertenciam ao esboço antigo e foram removidos do repositório publicado. A árvore atual tem fontes diferentes e mais novas.

A descrição de 1 MB de GFX ring, 256 KB de SDMA, DCN 2.01, timing 1920x1080 e ULPS não deve ser tratada como estado funcional atual. O KMD atual tem estruturas e callbacks, mas não implementa modeset, EDID, HPD, DP link training, page flip ou VSYNC reais. O display deve ser documentado como estrutura inicial, não como suporte operacional.

A sugestão de “desabilitar power management” não exige uma mudança ampla nesta rodada porque o código atual não possui um caminho completo de D0–D3/ULPS funcionando como o texto antigo alegava. A decisão correta é manter power management não anunciado como funcional e não afirmar clocks/ULPS sem logs do SMU.

## Correções desta rodada

Eu vou manter o foco em consistência e não em aumentar a quantidade de stubs. O README e o diário devem dizer que o MVP D3D11 ainda é uma meta, não uma capacidade presente. A documentação deve deixar explícito que DisplayPort, power, shader compiler, GPUVM e D3D11 ainda não estão operacionais.

Quando houver correção de código suportável sem hardware, cada mudança será publicada em commit separado ou em grupo pequeno relacionado. Os títulos serão curtos e descritivos, por exemplo:

- `docs: alinhar status do MVP D3D11`
- `firmware: separar imagem validada de microcode carregado`
- `gfx: manter fence dependente de conclusão GPU`
- `memory: documentar perfil UMA sem VRAM inventada`
- `display: remover alegação de VidPn funcional`

Todos os commits e textos serão atribuídos a **ZEROAESQUERDA**.

## Autor

**ZEROAESQUERDA**


## Ajustes aplicados nesta rodada

Eu deixei o callback de power aceitar somente D0. Transições D1, D2 e D3 agora retornam `STATUS_NOT_SUPPORTED` porque ainda não existe uma sequência SMU/GFXOFF validada para suspender e restaurar a GPU. Isso evita que o Windows receba uma confirmação de power state que o hardware não executou.

Também removi o cast que reinterpretava `D3D11DDIARG_OPENADAPTER` como uma estrutura D3D10. O export `OpenAdapter11` continua presente para deixar a fronteira identificável, mas retorna `E_NOTIMPL` até existir uma tabela baseada na ABI D3D11 correta. Um cast entre DDIs diferentes poderia corromper campos e tornar o diagnóstico muito mais difícil.

**Autor:** ZEROAESQUERDA
