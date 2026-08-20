#include "bc250_gfx.h"
#include "bc250_gfx_regs.h"

static BC250_GFX_ENGINE*
Bc250FindEngine(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BC250_ENGINE_KIND EngineKind
    )
{
    ULONG index;

    if (GfxState == NULL) {
        return NULL;
    }

    for (index = 0; index < GfxState->EngineCount; ++index) {
        if (GfxState->Engines[index].Kind == EngineKind) {
            return &GfxState->Engines[index];
        }
    }

    return NULL;
}

static PHYSICAL_ADDRESS
Bc250PhysicalAddressOf(
    _In_ PVOID Address
    )
{
    if (Address == NULL) {
        PHYSICAL_ADDRESS invalid;
        invalid.QuadPart = -1;
        return invalid;
    }

    return MmGetPhysicalAddress(Address);
}

NTSTATUS
Bc250InitializeGfxState(
    _Out_ BC250_GFX_STATE* GfxState
    )
{
    ULONG index;

    if (GfxState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(GfxState, sizeof(*GfxState));

    GfxState->EngineCount = BC250_GFX_MAX_ENGINES;
    GfxState->RingAlignment = PAGE_SIZE;
    GfxState->FenceAlignment = sizeof(ULONG64);

    GfxState->Engines[0].Kind = Bc250EngineGfx;
    GfxState->Engines[1].Kind = Bc250EngineCompute;
    GfxState->Engines[2].Kind = Bc250EngineSdma0;
    GfxState->Engines[3].Kind = Bc250EngineSdma1;

    for (index = 0; index < GfxState->EngineCount; ++index) {
        BC250_GFX_ENGINE* engine = &GfxState->Engines[index];

        engine->Instance = index;
        engine->RingSize = BC250_GFX_RING_DWORDS * sizeof(ULONG);
        engine->RingWritePointer = 0;
        engine->RingReadPointer = 0;
        engine->OffsetsValidated = FALSE;
        engine->ResourcesAllocated = FALSE;
        engine->HardwareEnabled = FALSE;
        engine->Initialized = FALSE;
    }

    GfxState->FirmwareLoaded = FALSE;
    GfxState->InterruptsEnabled = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250AllocateGfxResources(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ SIZE_T RingBytes,
    _In_ SIZE_T FenceBytes
    )
{
    PHYSICAL_ADDRESS lowest;
    PHYSICAL_ADDRESS highest;
    PHYSICAL_ADDRESS boundary;
    ULONG index;

    if (GfxState == NULL || RingBytes == 0 || FenceBytes < sizeof(ULONG64) ||
        RingBytes > (SIZE_T)MAXULONG * 16u) {
        return STATUS_INVALID_PARAMETER;
    }

    lowest.QuadPart = 0;
    highest.QuadPart = MAXULONG64;
    boundary.QuadPart = 0;

    for (index = 0; index < GfxState->EngineCount; ++index) {
        BC250_GFX_ENGINE* engine = &GfxState->Engines[index];
        PVOID ring;
        PVOID fence;

        if (engine->ResourcesAllocated) {
            continue;
        }

        ring = MmAllocateContiguousMemorySpecifyCache(
            RingBytes,
            lowest,
            highest,
            boundary,
            MmNonCached);
        if (ring == NULL) {
            Bc250FreeGfxResources(GfxState);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        fence = MmAllocateContiguousMemorySpecifyCache(
            FenceBytes,
            lowest,
            highest,
            boundary,
            MmNonCached);
        if (fence == NULL) {
            MmFreeContiguousMemory(ring);
            Bc250FreeGfxResources(GfxState);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(ring, RingBytes);
        RtlZeroMemory(fence, FenceBytes);

        engine->RingCpuAddress = (PULONG)ring;
        engine->RingPhysicalAddress = Bc250PhysicalAddressOf(ring);
        engine->RingSize = RingBytes;
        engine->Fence.CpuAddress = (volatile ULONG64*)fence;
        engine->Fence.PhysicalAddress = Bc250PhysicalAddressOf(fence);
        engine->Fence.NextValue = 1;
        engine->Fence.LastCompleted = 0;
        engine->RingWritePointer = 0;
        engine->RingReadPointer = 0;
        engine->ResourcesAllocated = TRUE;
    }

    return STATUS_SUCCESS;
}

VOID
Bc250FreeGfxResources(
    _Inout_ BC250_GFX_STATE* GfxState
    )
{
    ULONG index;

    if (GfxState == NULL) {
        return;
    }

    for (index = 0; index < GfxState->EngineCount; ++index) {
        BC250_GFX_ENGINE* engine = &GfxState->Engines[index];

        if (engine->RingCpuAddress != NULL) {
            MmFreeContiguousMemory(engine->RingCpuAddress);
        }
        if (engine->Fence.CpuAddress != NULL) {
            MmFreeContiguousMemory((PVOID)engine->Fence.CpuAddress);
        }

        engine->RingCpuAddress = NULL;
        engine->Fence.CpuAddress = NULL;
        engine->RingPhysicalAddress.QuadPart = 0;
        engine->Fence.PhysicalAddress.QuadPart = 0;
        engine->Fence.NextValue = 0;
        engine->Fence.LastCompleted = 0;
        engine->ResourcesAllocated = FALSE;
        engine->HardwareEnabled = FALSE;
        engine->Initialized = FALSE;
    }
}

NTSTATUS
Bc250InitializeGfxRings(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BOOLEAN AllowHardwareWrites
    )
{
    ULONG index;

    if (GfxState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (index = 0; index < GfxState->EngineCount; ++index) {
        BC250_GFX_ENGINE* engine = &GfxState->Engines[index];

        if (!engine->ResourcesAllocated || engine->RingCpuAddress == NULL ||
            engine->Fence.CpuAddress == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }

        RtlZeroMemory(engine->RingCpuAddress, engine->RingSize);
        *engine->Fence.CpuAddress = 0;
        engine->RingWritePointer = 0;
        engine->RingReadPointer = 0;
        engine->Fence.NextValue = 1;
        engine->Fence.LastCompleted = 0;
        engine->OffsetsValidated = (BC250_GFX_OFFSETS_VALIDATED != 0);
        engine->HardwareEnabled = AllowHardwareWrites && engine->OffsetsValidated;
        engine->Initialized = TRUE;
    }

    /* This function only prepares memory. MMIO programming is phase-gated. */
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250ReadCompletedFence(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BC250_ENGINE_KIND EngineKind,
    _Out_ ULONG64* Value
    )
{
    BC250_GFX_ENGINE* engine;
    ULONG index;
    ULONG64 observed;

    if (GfxState == NULL || Value == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    engine = NULL;
    for (index = 0; index < GfxState->EngineCount; ++index) {
        if (GfxState->Engines[index].Kind == EngineKind) {
            engine = &GfxState->Engines[index];
            break;
        }
    }

    if (engine == NULL || !engine->ResourcesAllocated ||
        engine->Fence.CpuAddress == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    observed = (ULONG64)InterlockedCompareExchange64(
        (volatile LONG64*)engine->Fence.CpuAddress,
        0,
        0);

    if (observed < engine->Fence.LastCompleted) {
        observed = engine->Fence.LastCompleted;
    }

    engine->Fence.LastCompleted = observed;
    *Value = observed;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250SubmitNoop(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BC250_ENGINE_KIND EngineKind
    )
{
    BC250_GFX_ENGINE* engine;

    if (HwState == NULL || GfxState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    engine = Bc250FindEngine(GfxState, EngineKind);
    if (engine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!engine->Initialized || !engine->ResourcesAllocated) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (!engine->HardwareEnabled || engine->WptrOffset == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    engine->RingCpuAddress[engine->RingWritePointer &
                           (BC250_GFX_RING_DWORDS - 1u)] =
        0xC0001000u;
    engine->RingWritePointer =
        (engine->RingWritePointer + 1u) & (BC250_GFX_RING_DWORDS - 1u);
    KeMemoryBarrier();
    return Bc250WriteRegister32(
        HwState,
        engine->WptrOffset,
        engine->RingWritePointer);
}


static NTSTATUS
Bc250ProgramEngineRing(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_ENGINE* Engine
    )
{
    ULONG baseLo;
    ULONG baseHi;
    NTSTATUS status;

    if (HwState == NULL || Engine == NULL || !Engine->ResourcesAllocated ||
        Engine->RingCpuAddress == NULL ||
        Engine->RingPhysicalAddress.QuadPart == -1) {
        return STATUS_INVALID_PARAMETER;
    }

    baseLo = (ULONG)((ULONGLONG)Engine->RingPhysicalAddress.QuadPart >> 8);
    baseHi = (ULONG)((ULONGLONG)Engine->RingPhysicalAddress.QuadPart >> 40);

    switch (Engine->Kind) {
    case Bc250EngineGfx:
    case Bc250EngineCompute:
        Engine->BaseLoOffset = BC250_GC_CP_RB0_BASE;
        Engine->BaseHiOffset = BC250_GC_CP_RB0_BASE_HI;
        Engine->RptrOffset = BC250_GC_CP_RB0_RPTR;
        Engine->WptrOffset = BC250_GC_CP_RB0_WPTR;
        Engine->WptrHiOffset = BC250_GC_CP_RB0_WPTR_HI;
        Engine->ControlOffset = BC250_GC_CP_RB0_CNTL;
        break;
    case Bc250EngineSdma0:
        Engine->BaseLoOffset = BC250_SDMA0_RB_BASE;
        Engine->BaseHiOffset = BC250_SDMA0_RB_BASE_HI;
        Engine->RptrOffset = BC250_SDMA0_RB_RPTR;
        Engine->WptrOffset = BC250_SDMA0_RB_WPTR;
        Engine->WptrHiOffset = BC250_SDMA0_RB_WPTR_HI;
        Engine->ControlOffset = BC250_SDMA0_RB_CNTL;
        break;
    case Bc250EngineSdma1:
        Engine->BaseLoOffset = BC250_SDMA1_RB_BASE;
        Engine->BaseHiOffset = BC250_SDMA1_RB_BASE_HI;
        Engine->RptrOffset = BC250_SDMA1_RB_RPTR;
        Engine->WptrOffset = BC250_SDMA1_RB_WPTR;
        Engine->WptrHiOffset = BC250_SDMA1_RB_WPTR_HI;
        Engine->ControlOffset = BC250_SDMA1_RB_CNTL;
        break;
    default:
        Engine->BaseLoOffset = BC250_GC_CP_RB0_BASE;
        Engine->BaseHiOffset = BC250_GC_CP_RB0_BASE_HI;
        Engine->RptrOffset = BC250_GC_CP_RB0_RPTR;
        Engine->WptrOffset = BC250_GC_CP_RB0_WPTR;
        Engine->WptrHiOffset = BC250_GC_CP_RB0_WPTR_HI;
        Engine->ControlOffset = BC250_GC_CP_RB0_CNTL;
        break;
    }

    status = Bc250WriteRegister32(HwState, Engine->BaseLoOffset, baseLo);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = Bc250WriteRegister32(HwState, Engine->BaseHiOffset, baseHi);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = Bc250WriteRegister32(HwState, Engine->RptrOffset, 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = Bc250WriteRegister32(HwState, Engine->WptrOffset, 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = Bc250WriteRegister32(HwState, Engine->WptrHiOffset, 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Experimental mode uses the family ring layout and default control state. */
    Engine->HardwareEnabled = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250ProgramGfxRings(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState
    )
{
    ULONG index;
    NTSTATUS status;

    if (HwState == NULL || GfxState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!GfxState->FirmwareLoaded || !HwState->FirmwareReady ||
        !HwState->RuntimeDiscovered) {
        return STATUS_DEVICE_NOT_READY;
    }

    for (index = 0; index < GfxState->EngineCount; ++index) {
        if (GfxState->Engines[index].Kind == Bc250EngineCompute) {
            /* The experimental port maps compute work to the primary GFX node. */
            GfxState->Engines[index].HardwareEnabled =
                GfxState->Engines[0].HardwareEnabled;
            continue;
        }

        status = Bc250ProgramEngineRing(HwState, &GfxState->Engines[index]);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    HwState->GfxReady = TRUE;
    return STATUS_SUCCESS;
}


NTSTATUS
Bc250SubmitDmaBuffer(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ PHYSICAL_ADDRESS DmaBufferPhysicalAddress,
    _In_ ULONG SubmissionStartOffset,
    _In_ ULONG SubmissionEndOffset,
    _In_ ULONG FenceId
    )
{
    BC250_GFX_ENGINE* engine;
    ULONG writePointer;
    ULONG payloadDwords;
    ULONG fenceValue;
    ULONG packet[4];
    NTSTATUS status;

    if (HwState == NULL || GfxState == NULL ||
        SubmissionEndOffset <= SubmissionStartOffset ||
        DmaBufferPhysicalAddress.QuadPart == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    engine = Bc250FindEngine(GfxState, Bc250EngineGfx);
    if (engine == NULL || !engine->ResourcesAllocated ||
        engine->RingCpuAddress == NULL || !engine->HardwareEnabled) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (engine->WptrOffset == 0 || engine->BaseLoOffset == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    payloadDwords = (SubmissionEndOffset - SubmissionStartOffset + 3u) / 4u;
    if (payloadDwords == 0 || payloadDwords > 0x000FFFFFu) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    /* PM4 PACKET3(INDIRECT_BUFFER, 2): base low, base high and control. */
    packet[0] = 0xC0000000u | (0x3Fu << 8) | 2u;
    packet[1] = (ULONG)((ULONGLONG)DmaBufferPhysicalAddress.QuadPart +
                        SubmissionStartOffset) >> 2;
    packet[2] = (ULONG)(((ULONGLONG)DmaBufferPhysicalAddress.QuadPart +
                        SubmissionStartOffset) >> 34);
    packet[3] = payloadDwords;

    writePointer = engine->RingWritePointer & (BC250_GFX_RING_DWORDS - 1u);
    engine->RingCpuAddress[writePointer] = packet[0];
    engine->RingCpuAddress[(writePointer + 1u) & (BC250_GFX_RING_DWORDS - 1u)] = packet[1];
    engine->RingCpuAddress[(writePointer + 2u) & (BC250_GFX_RING_DWORDS - 1u)] = packet[2];
    engine->RingCpuAddress[(writePointer + 3u) & (BC250_GFX_RING_DWORDS - 1u)] = packet[3];
    KeMemoryBarrier();

    engine->RingWritePointer =
        (writePointer + RTL_NUMBER_OF(packet)) & (BC250_GFX_RING_DWORDS - 1u);
    status = Bc250WriteRegister32(
        HwState,
        engine->WptrOffset,
        engine->RingWritePointer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    fenceValue = FenceId != 0 ? FenceId : (ULONG)engine->Fence.NextValue;
    engine->Fence.NextValue = (ULONG64)fenceValue + 1;
    if (engine->Fence.CpuAddress != NULL) {
        InterlockedExchange64(
            (volatile LONG64*)engine->Fence.CpuAddress,
            (LONG64)fenceValue);
    }
    return STATUS_SUCCESS;
}
