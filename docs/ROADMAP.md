# Roadmap do KMD BC-250

## Onde estou agora

Eu considero o projeto um bring-up de KMD/UMD, não um driver de jogos. A árvore já tem a separação full-WDDM, o modelo UMA/aperture, rings, fences, firmware público, mailbox SMU e a fronteira UMD, mas ainda não há uma BC-250 real disponível para confirmar os offsets, o microcode, o IH/EOP ou o comportamento do TDR.

## Marco 0 — Build no Windows 10 x64

Compilar a solução com Visual Studio, WDK e MSBuild em Windows 10 x64. Instalar apenas em uma máquina de teste com assinatura apropriada, captura de logs e uma forma de recuperação. O script `tools/validate_project.py` cobre XML, arquivos críticos e configuração, mas não substitui o build do WDK.

## Marco 1 — PnP, BARs e descoberta

Confirmar os IDs PCI, a revisão e os recursos traduzidos por PnP. Manter a BAR5 do mailbox SMN separada da BAR candidata de registradores GC/SDMA. Registrar tamanho/endereço dos recursos e recusar acessos fora das regiões fornecidas pelo Windows.

## Marco 2 — Firmware e estado D0

Integrar o carregamento efetivo de CP/SDMA através da sequência suportada pelo PSP/boot firmware, ou demonstrar por registradores e logs que o firmware já está carregado. Sair de GFXOFF de forma observável, manter a placa em D0 estável e só então permitir programação de engine. D1/D2/D3, ULPS e power gating ficam fora deste marco.

## Marco 3 — Ring e fence mínimos

Executar primeiro um teste pequeno no KMD: NOP, `WRITE_DATA` GPU-side para uma fence, leitura CPU do buffer, interrupção EOP/IH e repetição. A fence só pode ser considerada concluída quando a GPU escrever o valor. O reset precisa invalidar o estado e impedir que o scheduler veja progresso falso.

## Marco 4 — GPUVM e memória UMA mínima

Começar com uma política simples de system-memory/aperture, um VMID e page tables suficientes para um buffer linear. Só anunciar uma faixa UMA-like local quando a reserva física e a visibilidade GPU forem descobertas. Residency, eviction, TLB invalidation, cache management e GPU virtual address precisam ser implementados antes de workloads maiores.

## Marco 5 — Operações de buffer no KMD

Implementar e testar uma operação pequena de copy/clear usando allocations, endereços e fences reais. `Render`, `Patch`, `BuildPagingBuffer` e `SubmitCommand` devem traduzir apenas comandos que o KMD consegue validar e executar. Operações ainda não implementadas devem falhar explicitamente, e não retornar sucesso vazio.

## Marco 6 — MVP D3D11 limitado

Depois do KMD, construir um UMD D3D11 pequeno e coerente para buffers lineares, upload/copy, um clear simples, resource views mínimos e sincronização. O objetivo de saída deste marco é um triângulo simples ou, antes dele, um clear verificável em uma BC-250 real. A tabela DDI precisa corresponder ao header WDK do Windows 10 alvo; casts entre D3D11 e D3D10 não substituem a ABI correta.

## Marco 7 — DXGI e apresentação

Implementar adapter enumeration, formatos realmente suportados, swapchain, Present, page flip, VSync, EDID, HPD e modeset somente depois que memória e fences estiverem estáveis. A árvore atual possui callbacks de display, mas isso não deve ser confundido com DisplayPort funcional.

## Marco 8 — D3D12

Deixar D3D12 para depois de GPUVM, command queues, fences, heaps, resource states, device removal e sincronização estarem maduros. O export `OpenAdapter12` sozinho não constitui suporte D3D12.

## Marco 9 — Vulkan e power avançado

Vulkan, VCN, múltiplos monitores, hotplug sofisticado, D3D12 completo, D1/D2/D3 e ULPS só entram depois do MVP D3D11. A possibilidade de estudar NIR e o backend AMDGPU/Mesa é interessante, mas ainda seria necessário construir a camada UMD Windows e validar a ISA/ABI específica da GPU.

## Critérios para avançar

Eu só avanço de marco depois de compilar, instalar e observar o comportamento em hardware real. Bugcheck, timeout sem recuperação, acesso MMIO inválido, fence que avança sem a GPU, corrupção de memória ou reset que exige desligamento bloqueiam o próximo marco.

## Referências

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow "Microsoft Learn — WDDM operation flow"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-use-of-memory-segments "Microsoft Learn — Initializing use of memory segments"
[3]: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd "amdgpu no kernel Linux"
[4]: https://docs.mesa3d.org/ "Documentação do Mesa"

## Autor

**ZEROAESQUERDA**
