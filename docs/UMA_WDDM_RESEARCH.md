# Pesquisa UMA/WDDM

Fonte: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments

A documentação Microsoft informa que, a partir do WDDM 2.0, o KMD descreve os recursos físicos da GPU enumerando segmentos e o VidMm administra esses segmentos. Existem três tipos: memory segments, aperture segments e system memory segments.

Um memory segment pode representar VRAM dedicada em uma GPU discreta ou memória reservada pelo firmware/driver em uma GPU integrada. O VidMm administra esse segmento como um pool de páginas físicas de 4 KiB ou 64 KiB. O CPU pode acessar diretamente um segmento visível no espaço físico ou por uma CPU host aperture programável.

Um aperture segment é uma tabela global que faz páginas descontíguas de memória do sistema parecerem contíguas para a GPU. No WDDM 2.0, um único aperture segment deve ser reportado. O system memory segment é implícito, recebe `SegmentId==0` e não é enumerado diretamente pelo KMD; para colocar uma alocação na memória do sistema, o KMD usa o ID do aperture segment.

Implicação para o BC-250: a lógica correta não é anunciar todo o pool GDDR6 como VRAM local. É necessário descrever um segmento de memória reservado/visível se houver uma reserva real, um aperture segment para páginas do sistema e deixar o VidMm administrar o segmento implícito do sistema. A capacidade efetiva deve ser calculada a partir do BIOS/firmware/IP discovery e das reservas, não de uma constante fixa.
## Contrato de segmentos WDDM

A Microsoft define `DXGK_QUERYSEGMENTIN` com `AgpApertureBase`, `AgpApertureSize` e `AgpFlags`. A saída `DXGK_QUERYSEGMENTOUT` contém `NbSegment`, um array de `DXGK_SEGMENTDESCRIPTOR`, `PagingBufferSegmentId`, `PagingBufferSize` e `PagingBufferPrivateDataSize`. Cada descritor contém `BaseAddress`, `CpuTranslatedAddress`, `Size`, `NbOfBanks`, `pBankRangeTable`, `CommitLimit` e `Flags`.

Para o BC-250, o primeiro segmento pode ser o aperture fornecido pelo próprio WDDM (`AgpApertureBase`/`AgpApertureSize`), marcado com as flags recebidas. Um segmento adicional somente deve ser criado quando o firmware/IP discovery fornecer uma faixa física GPU-visível real. O driver não deve inventar `BaseAddress` nem transformar a capacidade nominal da placa em uma faixa local sem evidência.

Referências: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentin ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor
