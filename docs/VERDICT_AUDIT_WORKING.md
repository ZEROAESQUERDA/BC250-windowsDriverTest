# Notas de auditoria do veredito externo

## Escopo da comparação

O veredito anexado parece ter analisado a versão antiga do repositório, porque cita `amdbc250kmd.c`, `amdbc250_umd.c`, `amdbc250_hw_init.c` e headers `amdbc250_d3d*.h`. A árvore publicada mais recente usa `src/kmd/bc250_kmd.cxx`, `src/kmd/bc250_full_wddm.cxx`, `src/gfx/bc250_gfx.c`, `src/umd/bc250_umd.c`, `src/hw`, `src/memory`, `src/smu` e `src/firmware`. Portanto, as conclusões conceituais continuam úteis, mas as referências de arquivo precisam ser recalculadas.

## Conclusões preliminares

A crítica central é válida: ativar uma DDI e escrever `INDIRECT_BUFFER` não prova que um aplicativo D3D consiga renderizar. O KMD atual tem um caminho experimental de submit/ring/fence, porém não possui gerenciamento completo de GPU virtual address, page tables, VMID, residency/eviction, relocations, cache management ou validação abrangente de PM4.

A crítica ao UMD também é válida. `src/umd/bc250_umd.c` exporta as fronteiras OpenAdapter D3D9/D3D10/D3D11/D3D12 e cria estruturas privadas mínimas, mas não implementa as tabelas extensas de recursos, shaders, estados, command lists, heaps, PSO, root signatures e sincronização necessárias para compatibilidade de jogos.

A crítica à memória é válida com uma correção de contexto. O projeto atual não deve ser descrito como anunciando automaticamente dois segmentos de VRAM de 16 GB + 4 GB; ele usa um modelo UMA/aperture conservador e aloca backing system-memory para o caminho experimental. Mesmo assim, isso ainda está distante do VidMm/GPUVM completo exigido por uma GPU moderna.

A inconsistência sobre microcode é real no material antigo, mas a documentação mais nova deve distinguir claramente entre blobs públicos incluídos no repositório e carregamento efetivo pelo PSP/CP. Os blobs estão presentes em `firmware/amdgpu/`; o fluxo completo de carga de firmware ainda não está integrado ao StartDevice.

A parte de display do veredito não corresponde totalmente à árvore nova. O KMD full-WDDM atual não contém uma implementação completa de VidPn/DisplayPort/CRTC; vários callbacks display-only são mínimos ou no-op. Portanto, alegar que o projeto já possui display 1080p60, HPD e VSYNC funcionais seria incorreto.

A avaliação geral do veredito — estrutura de pesquisa interessante, mas ainda distante de executar jogos reais — é tecnicamente correta. O próximo passo deve ser corrigir fatos e nomes no diagnóstico, não mascarar as lacunas como suporte DirectX completo.
