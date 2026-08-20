# Referência para ativação full-WDDM

A documentação do WDK confirma que `DXGKARG_CREATECONTEXT` fornece `hContext`, `NodeOrdinal`, `EngineAffinity`, flags, dados privados e `DXGK_CONTEXTINFO`; o KMD deve retornar o handle de contexto em `hContext`. `DXGKARG_CREATEALLOCATION` fornece dados privados, número de alocações, array `DXGK_ALLOCATIONINFO`, handle de recurso e flags; o KMD deve preencher os handles privados de alocação nesse array.

`DXGKARG_RENDER` recebe o comando privado do UMD, um DMA buffer de saída, listas de alocações/patches e o endereço físico do DMA buffer. A documentação exige validação do comando antes de gerar o buffer DMA. `DXGKARG_BUILDPAGINGBUFFER` descreve operações de transferência, fill, map/unmap de aperture, atualização de tabelas e outras operações de gerenciamento de memória; não é correto tratar todas as operações como um único memcpy sem interpretar a união correspondente.

Para esta ativação, a implementação deve começar com um contrato privado explícito e limitado: alocações system/UMA, comandos PM4 validados, uma engine GFX ou SDMA escolhida, fence monotônica e operações de paging que possam ser representadas no ring. Declarar D3D12 completo exige também uma fronteira UMD D3D12 real, sincronização, recursos, heaps, root signatures, PSO e validação de comandos; apenas alterar macros não produz suporte D3D12.

## Referências

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createallocation "DXGKARG_CREATEALLOCATION — Microsoft Learn"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createcontext "DXGKARG_CREATECONTEXT — Microsoft Learn"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_render "DXGKARG_RENDER — Microsoft Learn"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_buildpagingbuffer "DXGKARG_BUILDPAGINGBUFFER — Microsoft Learn"

`DXGKARG_CREATEDEVICE` contém `hDevice` de entrada/saída, uma união de flags ou ponteiro para `DXGK_DEVICEINFO`, além de PASID e processo KMD. `DXGK_CONTEXTINFO` deve informar tamanho de DMA buffer, segmentos, dados privados, listas de alocações e patches. `DXGKARG_SUBMITCOMMAND` fornece handle de contexto/dispositivo, endereço físico e tamanho do DMA buffer, offsets da submissão, dados privados, `SubmissionFenceId`, engine e node.

A ativação de `CreateDevice` sem preencher `DXGK_DEVICEINFO`, `CreateContext` sem `ContextInfo` coerente ou `SubmitCommand` sem copiar/validar o DMA buffer faria o Windows aceitar uma ABI incompleta. Por isso, o modo solicitado será implementado como um caminho experimental explícito, não como uma alegação de compatibilidade certificada.

`DXGK_DEVICEINFO` exige informar tamanho do DMA buffer, segmento do DMA buffer, tamanho dos dados privados, lista de alocações, lista de patches e flags. `DXGK_ALLOCATIONINFO` exige que o KMD preencha tamanho, alinhamento, conjuntos de segmentos e `hAllocation`; `DXGKARG_DESCRIBEALLOCATION` devolve largura, altura, formato, multisampling, refresh rate e atributos privados. Essas estruturas permitem um caminho inicial de alocações UMA/system-memory, mas não constituem por si só uma implementação de recursos GPU completos.

[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_deviceinfo "DXGK_DEVICEINFO — Microsoft Learn"
[6]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo "DXGK_ALLOCATIONINFO — Microsoft Learn"
[7]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_describeallocation "DXGKARG_DESCRIBEALLOCATION — Microsoft Learn"

`DXGKARG_OPENALLOCATION` contém array de `DXGK_OPENALLOCATIONINFO`; cada elemento recebe `hDeviceSpecificAllocation`. `DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA` exige distinguir o tipo de alocação padrão e devolver tamanhos ou dados privados; a implementação inicial não deve alegar suporte a superfícies padrão sem preencher a união correspondente.

[8]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_openallocation "DXGKARG_OPENALLOCATION — Microsoft Learn"
[9]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_openallocationinfo "DXGK_OPENALLOCATIONINFO — Microsoft Learn"
[10]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_getstandardallocationdriverdata "DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA — Microsoft Learn"

A enumeração WDK usa `DXGK_OPERATION_TRANSFER=0`, `FILL=1`, `DISCARD_CONTENT=2`, `READ_PHYSICAL=3`, `WRITE_PHYSICAL=4`, `MAP_APERTURE_SEGMENT=5` e `UNMAP_APERTURE_SEGMENT=6`; operações virtuais e de page table possuem valores posteriores e requisitos de versão. O primeiro caminho de paging pode tratar Transfer/Fill/Discard e map/unmap de aperture, deixando operações posteriores explicitamente sem suporte até que os headers Windows 10 usados pelo build confirmem os campos.

[11]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_buildpagingbuffer_operation "DXGK_BUILDPAGINGBUFFER_OPERATION — Microsoft Learn"
