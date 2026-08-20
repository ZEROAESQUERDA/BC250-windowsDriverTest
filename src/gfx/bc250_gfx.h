#pragma once

#include <ntddk.h>
#include "../hw/bc250_hw.h"
#include "bc250_gfx_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_GFX_MAX_ENGINES 4u
#define BC250_GFX_RING_DWORDS 4096u

typedef enum _BC250_ENGINE_KIND {
    Bc250EngineGfx = 0,
    Bc250EngineCompute,
    Bc250EngineSdma0,
    Bc250EngineSdma1,
} BC250_ENGINE_KIND;

typedef struct _BC250_GFX_FENCE {
    volatile ULONG64* CpuAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG64 NextValue;
    ULONG64 LastCompleted;
} BC250_GFX_FENCE;

typedef struct _BC250_GFX_ENGINE {
    BC250_ENGINE_KIND Kind;
    ULONG Instance;
    ULONG BaseLoOffset;
    ULONG BaseHiOffset;
    ULONG RptrOffset;
    ULONG WptrOffset;
    ULONG WptrHiOffset;
    ULONG ControlOffset;
    PULONG RingCpuAddress;
    PHYSICAL_ADDRESS RingPhysicalAddress;
    SIZE_T RingSize;
    BC250_GFX_FENCE Fence;
    ULONG RingWritePointer;
    ULONG RingReadPointer;
    BOOLEAN OffsetsValidated;
    BOOLEAN ResourcesAllocated;
    BOOLEAN HardwareEnabled;
    BOOLEAN Initialized;
} BC250_GFX_ENGINE;

typedef struct _BC250_GFX_STATE {
    BC250_GFX_ENGINE Engines[BC250_GFX_MAX_ENGINES];
    ULONG EngineCount;
    ULONG RingAlignment;
    ULONG FenceAlignment;
    BOOLEAN FirmwareLoaded;
    BOOLEAN InterruptsEnabled;
} BC250_GFX_STATE;

NTSTATUS
Bc250AllocateGfxResources(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ SIZE_T RingBytes,
    _In_ SIZE_T FenceBytes
    );

VOID
Bc250FreeGfxResources(
    _Inout_ BC250_GFX_STATE* GfxState
    );

NTSTATUS
Bc250InitializeGfxRings(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BOOLEAN AllowHardwareWrites
    );

NTSTATUS
Bc250ReadCompletedFence(
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BC250_ENGINE_KIND EngineKind,
    _Out_ ULONG64* Value
    );

NTSTATUS
Bc250ProgramGfxRings(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState
    );

NTSTATUS
Bc250SubmitDmaBuffer(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ PHYSICAL_ADDRESS DmaBufferPhysicalAddress,
    _In_ ULONG SubmissionStartOffset,
    _In_ ULONG SubmissionEndOffset,
    _In_ ULONG FenceId
    );

NTSTATUS
Bc250InitializeGfxState(
    _Out_ BC250_GFX_STATE* GfxState
    );

NTSTATUS
Bc250SubmitNoop(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_GFX_STATE* GfxState,
    _In_ BC250_ENGINE_KIND EngineKind
    );

#ifdef __cplusplus
}
#endif
