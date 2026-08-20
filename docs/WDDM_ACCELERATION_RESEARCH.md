# WDDM acelerado e KMDOD

Fonte Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/samples/video-driver-samples

A Microsoft descreve o KMDOD como um sample que implementa as interfaces de um miniport display-only e informa que o código é útil para entender tanto dispositivos display-only quanto o desenvolvimento de um driver WDDM completo. Isso confirma a decisão arquitetural do projeto: a base KMDOD é adequada para PnP/display e diagnóstico, mas não constitui por si só um KMD com renderização DirectX.

Para aceleração será necessário evoluir para o contrato completo: QueryAdapterInfo/segmentos, VidMm/paging, criação de contexto, Render/Patch/SubmitCommand, fence/interrupt/DPC, reset/TDR e um UMD Direct3D. A implementação desses contratos deve continuar baseada nos registros/IP/firmware Cyan Skillfish, não nos dados Van Gogh do Steam Deck.
Fonte Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-the-display-miniport-driver

Para um miniport WDDM completo, `DriverEntry` deve preencher `DRIVER_INITIALIZATION_DATA` e chamar `DxgkInitialize`. O projeto atual usa `KMDDOD_INITIALIZATION_DATA` e `DxgkInitializeDisplayOnlyDriver`; portanto, antes de tentar DirectX é necessário criar uma configuração full-WDDM e registrar as DDIs de renderização, memória, engine e sincronização. O KMDOD continua útil como base de PnP/display, mas não pode ser o registro final do driver acelerado.
## DDIs mínimas para o primeiro caminho acelerado

As fontes Microsoft confirmam os contratos mínimos relevantes:

| DDI | Função | Condição operacional |
| --- | --- | --- |
| `DxgkDdiCreateContext` | Criar contexto GPU | Deve aceitar múltiplos contextos; somente falta de memória deve impedir a criação normal |
| `DxgkDdiRender` | Traduzir comando do UMD para DMA/PM4 | Deve validar buffers de usuário com `__try/__except`, rejeitar instruções privilegiadas e produzir patch locations |
| `DxgkDdiBuildPagingBuffer` | Gerar DMA de transfer/fill/discard/map/unmap | Necessário para VidMm mover alocações entre segmentos/aper­ture |
| `DxgkDdiSubmitCommand` | Enviar DMA ao engine físico | Executa em `DISPATCH_LEVEL`; retornar erro pode causar bugcheck 0x119 |
| `DxgkDdiQueryCurrentFence` | Expor progresso do engine | Deve refletir a fence real produzida pelo ring |

Referências: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_render ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommand ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createcontext

A migração full-WDDM deste projeto registra `DRIVER_INITIALIZATION_DATA`/`DxgkInitialize` e, no modo experimental solicitado, deixa as capacidades DirectX ativas. Render, PagingBuffer, SubmitCommand, fences e reset possuem caminhos mínimos implementados; operações ainda sem tradução específica retornam sucesso/no-op para permitir o bring-up. Um caminho que anuncia aceleração sem testar a ABI, os rings e a memória continua sujeito a TDR ou bugcheck, por isso o estado deve ser tratado como experimental até validação em Windows 10 x64 com a placa.
Fonte Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-a-user-mode-display-driver

O INF copia a DLL UMD para `%systemroot%\\system32` e registra `UserModeDriverName` na seção `AddReg`; o INF principal do KMD também copia e registra `bc250_umd.dll` no modo experimental ativo. O projeto UMD BC-250 permanece uma DLL separada do KMD, com exports D3D9/D3D10/D3D11/D3D12 mínimos para o bring-up.
