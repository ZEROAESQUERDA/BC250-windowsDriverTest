#include "bc250_psp.h"

static BOOLEAN
Bc250PspBarReady(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _In_ SIZE_T Width
    )
{
    ULONG barIndex;

    if (HwState == NULL || HwState->SmnBarIndex >= BC250_MAX_BARS) {
        return FALSE;
    }

    /* PSP GPCOM is on the confirmed BAR5/SMN mapping, never on a size-only candidate. */
    barIndex = HwState->SmnBarIndex;
    return HwState->Bars[barIndex].Mapped &&
           HwState->Bars[barIndex].VirtualAddress != NULL &&
           Offset <= HwState->Bars[barIndex].Length &&
           Width <= HwState->Bars[barIndex].Length - Offset;
}

static NTSTATUS
Bc250PspReadRegister(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
    )
{
    volatile ULONG* address;
    ULONG barIndex;

    if (Value == NULL || !Bc250PspBarReady(HwState, Offset, sizeof(ULONG))) {
        return STATUS_INVALID_PARAMETER;
    }

    barIndex = HwState->PspBarIndex;
    address = (volatile ULONG*)((PUCHAR)
        HwState->Bars[barIndex].VirtualAddress + Offset);
    *Value = READ_REGISTER_ULONG(address);
    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250PspWriteRegister(
    _In_ BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    volatile ULONG* address;
    ULONG barIndex;

#if !BC250_PSP_RING_VALIDATED
    UNREFERENCED_PARAMETER(HwState);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_NOT_SUPPORTED;
#else
    if (!Bc250PspBarReady(HwState, Offset, sizeof(ULONG))) {
        return STATUS_INVALID_PARAMETER;
    }

    barIndex = HwState->PspBarIndex;
    address = (volatile ULONG*)((PUCHAR)
        HwState->Bars[barIndex].VirtualAddress + Offset);
    WRITE_REGISTER_ULONG(address, Value);
    return STATUS_SUCCESS;
#endif
}

static VOID
Bc250PspFlushHdp(
    _In_ BC250_HW_STATE* HwState
    )
{
#if BC250_PSP_HDP_OFFSETS_VALIDATED
    if (Bc250PspBarReady(HwState,
                         BC250_PSP_HDP_DEBUG0_REGISTER,
                         sizeof(ULONG))) {
        (VOID)Bc250PspWriteRegister(
            HwState,
            BC250_PSP_HDP_FLUSH_REGISTER,
            BC250_PSP_HDP_FLUSH_CACHE);
        (VOID)Bc250PspWriteRegister(
            HwState,
            BC250_PSP_HDP_DEBUG0_REGISTER,
            BC250_PSP_HDP_INVALIDATE_CACHE);
    }
#else
    UNREFERENCED_PARAMETER(HwState);
#endif

    KeMemoryBarrier();
}

static PVOID
Bc250PspAllocateLowMemory(
    _In_ SIZE_T Size,
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress
    )
{
    PHYSICAL_ADDRESS lowest;
    PHYSICAL_ADDRESS highest;
    PHYSICAL_ADDRESS boundary;
    PVOID virtualAddress;

    if (PhysicalAddress == NULL || Size == 0) {
        return NULL;
    }

    lowest.QuadPart = 0;
    highest.QuadPart = 0xFFFFFFFFULL;
    boundary.QuadPart = 0;
    virtualAddress = MmAllocateContiguousMemorySpecifyCache(
        Size,
        lowest,
        highest,
        boundary,
        MmNonCached);
    if (virtualAddress == NULL) {
        return NULL;
    }

    RtlZeroMemory(virtualAddress, Size);
    *PhysicalAddress = MmGetPhysicalAddress(virtualAddress);
    if (PhysicalAddress->QuadPart == 0) {
        MmFreeContiguousMemory(virtualAddress);
        PhysicalAddress->QuadPart = 0;
        return NULL;
    }

    return virtualAddress;
}

static NTSTATUS
Bc250PspEnsureBuffers(
    _Inout_ BC250_PSP_STATE* PspState
    )
{
    if (PspState->CommandVirtualAddress == NULL) {
        PspState->CommandVirtualAddress = Bc250PspAllocateLowMemory(
            BC250_PSP_COMMAND_BUFFER_SIZE,
            &PspState->CommandPhysicalAddress);
        if (PspState->CommandVirtualAddress == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (PspState->FenceVirtualAddress == NULL) {
        PspState->FenceVirtualAddress = Bc250PspAllocateLowMemory(
            BC250_PSP_COMMAND_BUFFER_SIZE,
            &PspState->FencePhysicalAddress);
        if (PspState->FenceVirtualAddress == NULL) {
            MmFreeContiguousMemory(PspState->CommandVirtualAddress);
            PspState->CommandVirtualAddress = NULL;
            PspState->CommandPhysicalAddress.QuadPart = 0;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return STATUS_SUCCESS;
}

static ULONG
Bc250PspNextFence(
    _Inout_ BC250_PSP_STATE* PspState
    )
{
    ++PspState->FenceValue;
    if (PspState->FenceValue == 0) {
        PspState->FenceValue = 1;
    }
    return PspState->FenceValue;
}

static ULONG
Bc250PspFirmwareTypeFromKind(
    _In_ BC250_FIRMWARE_KIND Kind
    )
{
    switch (Kind) {
    case Bc250FirmwareMe:
        return BC250_PSP_FW_TYPE_ME;
    case Bc250FirmwarePfp:
        return BC250_PSP_FW_TYPE_PFP;
    case Bc250FirmwareCe:
        return BC250_PSP_FW_TYPE_CE;
    case Bc250FirmwareMec:
        return BC250_PSP_FW_TYPE_MEC;
    case Bc250FirmwareRlc:
        return BC250_PSP_FW_TYPE_RLC;
    case Bc250FirmwareSdma:
        return BC250_PSP_FW_TYPE_SDMA;
    case Bc250FirmwareSdma1:
        return BC250_PSP_FW_TYPE_SDMA1;
    case Bc250FirmwareSmu:
        return BC250_PSP_FW_TYPE_SMU;
    default:
        return 0;
    }
}

VOID
Bc250PspInitializeState(
    _Out_ BC250_PSP_STATE* PspState
    )
{
    if (PspState == NULL) {
        return;
    }

    RtlZeroMemory(PspState, sizeof(*PspState));
    ExInitializeFastMutex(&PspState->Mutex);
    PspState->RingSize = BC250_PSP_RING_SIZE;
    PspState->LastTransportStatus = STATUS_DEVICE_NOT_READY;
    PspState->Initialized = TRUE;
}

VOID
Bc250PspReleaseState(
    _Inout_ BC250_PSP_STATE* PspState
    )
{
    if (PspState == NULL) {
        return;
    }

    if (PspState->RingVirtualAddress != NULL) {
        MmFreeContiguousMemory(PspState->RingVirtualAddress);
    }
    if (PspState->CommandVirtualAddress != NULL) {
        MmFreeContiguousMemory(PspState->CommandVirtualAddress);
    }
    if (PspState->FenceVirtualAddress != NULL) {
        MmFreeContiguousMemory(PspState->FenceVirtualAddress);
    }
    if (PspState->DeferredFirmwareVirtualAddress != NULL) {
        /*
         * A timed-out PSP command may still DMA-read this staging buffer.
         * ReleaseState is called after StopDevice/reset has quiesced the
         * adapter; only then is it safe to release the deferred buffer.
         */
        MmFreeContiguousMemory(PspState->DeferredFirmwareVirtualAddress);
    }

    RtlZeroMemory(PspState, sizeof(*PspState));
}

NTSTATUS
Bc250PspCreateRing(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState
    )
{
    ULONG c2pmsg64;
    ULONG c2pmsg81;
    ULONG waited;
    NTSTATUS status;

    if (HwState == NULL || PspState == NULL || !PspState->Initialized) {
        return STATUS_INVALID_PARAMETER;
    }

#if !BC250_PSP_RING_VALIDATED
    UNREFERENCED_PARAMETER(HwState);
    return STATUS_NOT_SUPPORTED;
#else
    if (!HwState->RuntimeDiscovered || !Bc250PspBarReady(
            HwState,
            BC250_PSP_C2PMSG_81,
            sizeof(ULONG))) {
        return STATUS_DEVICE_NOT_READY;
    }

    ExAcquireFastMutex(&PspState->Mutex);

    if (PspState->RingCreated) {
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_SUCCESS;
    }

    status = Bc250PspReadRegister(HwState, BC250_PSP_C2PMSG_81, &c2pmsg81);
    if (!NT_SUCCESS(status)) {
        ExReleaseFastMutex(&PspState->Mutex);
        return status;
    }

    status = Bc250PspReadRegister(HwState, BC250_PSP_C2PMSG_64, &c2pmsg64);
    if (!NT_SUCCESS(status)) {
        ExReleaseFastMutex(&PspState->Mutex);
        return status;
    }

    for (waited = 0; waited < BC250_PSP_TIMEOUT_MS; ++waited) {
        if ((c2pmsg64 & BC250_PSP_C2PMSG_TOS_READY) != 0) {
            break;
        }
        KeStallExecutionProcessor(1000);
        status = Bc250PspReadRegister(
            HwState,
            BC250_PSP_C2PMSG_64,
            &c2pmsg64);
        if (!NT_SUCCESS(status)) {
            ExReleaseFastMutex(&PspState->Mutex);
            return status;
        }
    }

    if ((c2pmsg64 & BC250_PSP_C2PMSG_TOS_READY) == 0) {
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_IO_TIMEOUT;
    }

    PspState->RingVirtualAddress = Bc250PspAllocateLowMemory(
        BC250_PSP_RING_SIZE,
        &PspState->RingPhysicalAddress);
    if (PspState->RingVirtualAddress == NULL) {
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = Bc250PspWriteRegister(
        HwState,
        BC250_PSP_C2PMSG_69,
        (ULONG)(PspState->RingPhysicalAddress.QuadPart & 0xFFFFFFFFu));
    if (NT_SUCCESS(status)) {
        status = Bc250PspWriteRegister(
            HwState,
            BC250_PSP_C2PMSG_70,
            (ULONG)(PspState->RingPhysicalAddress.QuadPart >> 32));
    }
    if (NT_SUCCESS(status)) {
        status = Bc250PspWriteRegister(
            HwState,
            BC250_PSP_C2PMSG_71,
            BC250_PSP_RING_SIZE);
    }
    if (NT_SUCCESS(status)) {
        status = Bc250PspWriteRegister(
            HwState,
            BC250_PSP_C2PMSG_64,
            BC250_PSP_RING_TYPE_KM << 16);
    }

    if (!NT_SUCCESS(status)) {
        MmFreeContiguousMemory(PspState->RingVirtualAddress);
        PspState->RingVirtualAddress = NULL;
        PspState->RingPhysicalAddress.QuadPart = 0;
        ExReleaseFastMutex(&PspState->Mutex);
        return status;
    }

    KeStallExecutionProcessor(20000);
    c2pmsg64 = 0;
    for (waited = 0; waited < BC250_PSP_TIMEOUT_MS; ++waited) {
        status = Bc250PspReadRegister(
            HwState,
            BC250_PSP_C2PMSG_64,
            &c2pmsg64);
        if (!NT_SUCCESS(status)) {
            break;
        }
        if ((c2pmsg64 & BC250_PSP_C2PMSG_TOS_READY) != 0) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }

    if (!NT_SUCCESS(status) || (c2pmsg64 & BC250_PSP_C2PMSG_TOS_READY) == 0) {
        MmFreeContiguousMemory(PspState->RingVirtualAddress);
        PspState->RingVirtualAddress = NULL;
        PspState->RingPhysicalAddress.QuadPart = 0;
        ExReleaseFastMutex(&PspState->Mutex);
        return NT_SUCCESS(status) ? STATUS_IO_TIMEOUT : status;
    }

    PspState->RingCreated = TRUE;
    PspState->RingWritePointer = 0;
    PspState->FenceValue = 0;
    PspState->LastCommandId = 0;
    PspState->LastResponseStatus = BC250_PSP_RESPONSE_SUCCESS;
    PspState->LastTransportStatus = STATUS_SUCCESS;
    HwState->PspReady = FALSE;

    UNREFERENCED_PARAMETER(c2pmsg81);
    ExReleaseFastMutex(&PspState->Mutex);
    return STATUS_SUCCESS;
#endif
}

NTSTATUS
Bc250PspAttest(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    )
{
    NTSTATUS status;

    if (Result == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = Bc250PspSubmit(
        HwState,
        PspState,
        BC250_PSP_COMMAND_GET_FW_ATTESTATION,
        NULL,
        0,
        Result);
    if (NT_SUCCESS(status) && Result->FenceStatus != 0 &&
        Result->ResponseStatus == BC250_PSP_RESPONSE_SUCCESS) {
        HwState->PspReady = TRUE;
    }
    return status;
}

NTSTATUS
Bc250PspSubmit(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _In_ ULONG CommandId,
    _In_reads_bytes_opt_(CommandDataSize) const UCHAR* CommandData,
    _In_ ULONG CommandDataSize,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    )
{
    ULONG* command;
    PUCHAR frame;
    ULONG fenceValue;
    ULONG ringDwords;
    ULONG waitIndex;
    ULONG responseStatus = 0xFFFFFFFFu;
    NTSTATUS status;

    if (Result == NULL || HwState == NULL || PspState == NULL ||
        CommandDataSize > BC250_PSP_MAX_COMMAND_DATA ||
        (CommandDataSize != 0 && CommandData == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Result, sizeof(*Result));
    Result->TransportStatus = STATUS_DEVICE_NOT_READY;

#if !BC250_PSP_RING_VALIDATED
    UNREFERENCED_PARAMETER(HwState);
    UNREFERENCED_PARAMETER(PspState);
    UNREFERENCED_PARAMETER(CommandId);
    UNREFERENCED_PARAMETER(CommandData);
    return STATUS_NOT_SUPPORTED;
#else
    ExAcquireFastMutex(&PspState->Mutex);

    if (!PspState->RingCreated || PspState->RingVirtualAddress == NULL) {
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_DEVICE_NOT_READY;
    }
    if (PspState->SubmissionFaulted) {
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_DEVICE_HUNG;
    }

    status = Bc250PspEnsureBuffers(PspState);
    if (!NT_SUCCESS(status)) {
        ExReleaseFastMutex(&PspState->Mutex);
        return status;
    }

    command = (ULONG*)PspState->CommandVirtualAddress;
    RtlZeroMemory(command, BC250_PSP_COMMAND_BUFFER_SIZE);
    command[0] = BC250_PSP_COMMAND_BUFFER_BYTES;
    command[1] = 1;
    command[2] = CommandId;
    if (CommandDataSize != 0) {
        RtlCopyMemory((PUCHAR)command + 28, CommandData, CommandDataSize);
    }

    fenceValue = Bc250PspNextFence(PspState);
    InterlockedExchange((volatile LONG*)PspState->FenceVirtualAddress, 0);

    ringDwords = PspState->RingSize / sizeof(ULONG);
    if (PspState->RingWritePointer >= ringDwords ||
        ringDwords < BC250_PSP_RING_FRAME_DWORDS) {
        PspState->SubmissionFaulted = TRUE;
        ExReleaseFastMutex(&PspState->Mutex);
        return STATUS_INVALID_BUFFER_SIZE;
    }

    frame = (PUCHAR)PspState->RingVirtualAddress +
            (PspState->RingWritePointer * sizeof(ULONG));
    RtlZeroMemory(frame, BC250_PSP_RING_FRAME_BYTES);
    *(ULONG*)(frame + 0) =
        (ULONG)(PspState->CommandPhysicalAddress.QuadPart & 0xFFFFFFFFu);
    *(ULONG*)(frame + 4) =
        (ULONG)(PspState->CommandPhysicalAddress.QuadPart >> 32);
    *(ULONG*)(frame + 8) = BC250_PSP_COMMAND_BUFFER_BYTES;
    *(ULONG*)(frame + 12) =
        (ULONG)(PspState->FencePhysicalAddress.QuadPart & 0xFFFFFFFFu);
    *(ULONG*)(frame + 16) =
        (ULONG)(PspState->FencePhysicalAddress.QuadPart >> 32);
    *(ULONG*)(frame + 20) = fenceValue;
    KeMemoryBarrier();

    PspState->RingWritePointer =
        (PspState->RingWritePointer + BC250_PSP_RING_FRAME_DWORDS) %
        ringDwords;
    status = Bc250PspWriteRegister(
        HwState,
        BC250_PSP_C2PMSG_67,
        PspState->RingWritePointer);
    if (!NT_SUCCESS(status)) {
        PspState->SubmissionFaulted = TRUE;
        ExReleaseFastMutex(&PspState->Mutex);
        return status;
    }

    for (waitIndex = 0; waitIndex < BC250_PSP_TIMEOUT_MS; ++waitIndex) {
        Bc250PspFlushHdp(HwState);
        if (*(volatile ULONG*)PspState->FenceVirtualAddress == fenceValue) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }

    Result->Result = 1;
    Result->FenceStatus =
        (*(volatile ULONG*)PspState->FenceVirtualAddress == fenceValue) ? 1u : 0u;
    if (Result->FenceStatus != 0) {
        KeStallExecutionProcessor(1000);
        responseStatus = command[BC250_PSP_RESPONSE_OFFSET / sizeof(ULONG)];
        Result->ResponseStatus = responseStatus;
        Result->ResponseFirmwareAddressLow = command[(BC250_PSP_RESPONSE_OFFSET / sizeof(ULONG)) + 2];
        Result->ResponseFirmwareAddressHigh = command[(BC250_PSP_RESPONSE_OFFSET / sizeof(ULONG)) + 3];
        Result->ResponseTmrSize = command[(BC250_PSP_RESPONSE_OFFSET / sizeof(ULONG)) + 4];
        Result->TransportStatus = STATUS_SUCCESS;
    } else {
        Result->ResponseStatus = 0xFFFFFFFFu;
        Result->TransportStatus = STATUS_IO_TIMEOUT;
        PspState->SubmissionFaulted = TRUE;
    }

    PspState->LastCommandId = CommandId;
    PspState->LastResponseStatus = Result->ResponseStatus;
    PspState->LastTransportStatus = Result->TransportStatus;
    ExReleaseFastMutex(&PspState->Mutex);
    return Result->TransportStatus;
#endif
}

NTSTATUS
Bc250PspLoadFirmware(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _Inout_ BC250_FIRMWARE_STATE* FirmwareState,
    _In_ const BC250_FIRMWARE_IMAGE* Image
    )
{
    ULONG firmwareType;
    PHYSICAL_ADDRESS firmwarePhysical;
    PVOID firmwareVirtual;
    BC250_PSP_COMMAND_RESULT result;
    UCHAR payload[sizeof(ULONG) * 4];
    NTSTATUS status;

    if (HwState == NULL || PspState == NULL || FirmwareState == NULL ||
        Image == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = Bc250FirmwareValidateImage(Image);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    firmwareType = Bc250PspFirmwareTypeFromKind(Image->Kind);
    if (firmwareType == 0) {
        return STATUS_NOT_SUPPORTED;
    }

#if !BC250_PSP_RING_VALIDATED
    UNREFERENCED_PARAMETER(HwState);
    UNREFERENCED_PARAMETER(PspState);
    UNREFERENCED_PARAMETER(FirmwareState);
    UNREFERENCED_PARAMETER(firmwareType);
    return STATUS_NOT_SUPPORTED;
#else
    firmwareVirtual = Bc250PspAllocateLowMemory(Image->Size, &firmwarePhysical);
    if (firmwareVirtual == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(firmwareVirtual, Image->Data, Image->Size);
    KeMemoryBarrier();

    *(ULONG*)&payload[0] = (ULONG)(firmwarePhysical.QuadPart & 0xFFFFFFFFu);
    *(ULONG*)&payload[4] = (ULONG)(firmwarePhysical.QuadPart >> 32);
    *(ULONG*)&payload[8] = (ULONG)Image->Size;
    *(ULONG*)&payload[12] = firmwareType;

    status = Bc250PspSubmit(
        HwState,
        PspState,
        BC250_PSP_COMMAND_LOAD_IP_FW,
        payload,
        sizeof(payload),
        &result);
    if (NT_SUCCESS(status) && result.FenceStatus != 0 &&
        result.ResponseStatus == BC250_PSP_RESPONSE_SUCCESS) {
        status = Bc250FirmwareMarkLoaded(FirmwareState, Image->Kind);
        MmFreeContiguousMemory(firmwareVirtual);
        return status;
    }

    if (status == STATUS_IO_TIMEOUT &&
        PspState->DeferredFirmwareVirtualAddress == NULL) {
        PspState->DeferredFirmwareVirtualAddress = firmwareVirtual;
        PspState->DeferredFirmwarePhysicalAddress = firmwarePhysical;
        PspState->DeferredFirmwareSize = Image->Size;
        return status;
    }

    MmFreeContiguousMemory(firmwareVirtual);
    return NT_SUCCESS(status) ? STATUS_DEVICE_NOT_READY : status;
#endif
}

NTSTATUS
Bc250PspSetupTmr(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _In_ PHYSICAL_ADDRESS GpuMemoryAddress,
    _In_ PHYSICAL_ADDRESS SystemPhysicalAddress,
    _In_ ULONG TmrSize,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    )
{
    UCHAR payload[sizeof(ULONG) * 6];
    NTSTATUS status;

    if (HwState == NULL || PspState == NULL || Result == NULL ||
        GpuMemoryAddress.QuadPart == 0 ||
        SystemPhysicalAddress.QuadPart == 0 ||
        GpuMemoryAddress.QuadPart == SystemPhysicalAddress.QuadPart) {
        return STATUS_INVALID_PARAMETER;
    }

    if (TmrSize == 0) {
        TmrSize = BC250_PSP_TMR_DEFAULT_SIZE;
    }
    if (TmrSize < BC250_PSP_TMR_MIN_SIZE ||
        TmrSize > BC250_PSP_TMR_MAX_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }
    TmrSize = (TmrSize + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);

    *(ULONG*)&payload[0] =
        (ULONG)(GpuMemoryAddress.QuadPart & 0xFFFFFFFFu);
    *(ULONG*)&payload[4] =
        (ULONG)(GpuMemoryAddress.QuadPart >> 32);
    *(ULONG*)&payload[8] = TmrSize;
    *(ULONG*)&payload[12] = 0x2u;
    *(ULONG*)&payload[16] =
        (ULONG)(SystemPhysicalAddress.QuadPart & 0xFFFFFFFFu);
    *(ULONG*)&payload[20] =
        (ULONG)(SystemPhysicalAddress.QuadPart >> 32);

    status = Bc250PspSubmit(
        HwState,
        PspState,
        BC250_PSP_COMMAND_SETUP_TMR,
        payload,
        sizeof(payload),
        Result);
    if (NT_SUCCESS(status) && Result->FenceStatus != 0 &&
        Result->ResponseStatus == BC250_PSP_RESPONSE_SUCCESS) {
        PspState->TmrConfigured = TRUE;
    }

    return status;
}
