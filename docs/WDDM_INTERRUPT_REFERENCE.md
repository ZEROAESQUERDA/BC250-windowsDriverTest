# Referência WDDM para interrupções e DPC

O contrato Windows para `DxgkDdiInterruptRoutine` exige que o ISR determine se o adaptador gerou a interrupção, descarte/acknowledge o evento, conclua ou enfileire o trabalho e retorne rapidamente. O ISR executa em IRQL elevado e não pode acessar memória paginável. A documentação também permite chamar, no ISR, `DxgkCbQueueDpc` e `DxgkCbNotifyInterrupt`, mas não outros callbacks `DxgkCbXxx`.

A interface `DXGKRNL_INTERFACE` fornecida em `DxgkDdiStartDevice` contém `DxgkCbQueueDpc`, `DxgkCbNotifyInterrupt` e `DxgkCbNotifyDpc`. `DxgkCbQueueDpc` recebe o `DeviceHandle` e agenda o `DxgkDdiDpcRoutine`, com no máximo um DPC pendente por dispositivo.

Para a BC-250, os offsets de status de interrupção ainda não foram validados para Cyan Skillfish2. Por isso, o ISR deve retornar `FALSE` enquanto `BC250_GFX_OFFSETS_VALIDATED` ou a flag de interrupções não estiver habilitada; não é permitido ler um registrador candidato e tratá-lo como status confirmado. O DPC pode ler os buffers de fence não pagináveis apenas quando o ISR tiver marcado um evento real.

## Referências

[1]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_interrupt_routine "DXGKDDI_INTERRUPT_ROUTINE — Microsoft Learn"
[2]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_queue_dpc "DXGKCB_QUEUE_DPC — Microsoft Learn"
[3]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_notify_interrupt "DXGKCB_NOTIFY_INTERRUPT — Microsoft Learn"
[4]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_dxgkrnl_interface "DXGKRNL_INTERFACE — Microsoft Learn"

`DxgkCbNotifyDpc` recebe somente o `DeviceHandle` e deve ser chamado pelo `DxgkDdiDpcRoutine` em `DISPATCH_LEVEL` para informar o scheduler sobre a atualização de fence. O exemplo oficial confirma a sequência `DxgkCbQueueDpc` no ISR e `DxgkCbNotifyDpc` no DPC.

[5]: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_notify_dpc "DXGKCB_NOTIFY_DPC — Microsoft Learn"
