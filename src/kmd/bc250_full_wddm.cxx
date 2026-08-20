#include "bc250_full_wddm.hxx"
#include "bc250_kmd.hxx"

/*
 * Full-WDDM transition layer.
 *
 * This file implements the experimental hardware-facing WDDM path. It exposes
 * device/context/allocation, DMA, paging, fence, submit and reset contracts so
 * the project can attempt acceleration during bring-up on Windows 10 x64.
 * Hardware validation remains a deployment concern because the BC-250 ASIC is
 * not available in the development environment.
 */

typedef struct _BC250_WDDM_DEVICE {
    BC250_DEVICE_CONTEXT* Adapter;
    volatile LONG ReferenceCount;
} BC250_WDDM_DEVICE;

typedef struct _BC250_WDDM_CONTEXT {
    BC250_WDDM_DEVICE* Device;
    ULONG NodeOrdinal;
    ULONG EngineAffinity;
} BC250_WDDM_CONTEXT;

typedef struct _BC250_WDDM_ALLOCATION {
    BC250_WDDM_DEVICE* Device;
    SIZE_T Size;
    PVOID CpuAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Width;
    ULONG Height;
    D3DDDIFORMAT Format;
} BC250_WDDM_ALLOCATION;

#define BC250_WDDM_DMA_BUFFER_SIZE (64u * 1024u)
#define BC250_WDDM_ALLOCATION_LIST_SIZE 256u
#define BC250_WDDM_PATCH_LIST_SIZE 256u
#define BC250_WDDM_MAX_ALLOCATION (512ull * 1024ull * 1024ull)

static BC250_WDDM_DEVICE*
Bc250WddmDeviceFromHandle(
    _In_ CONST HANDLE Handle
    )
{
    return reinterpret_cast<BC250_WDDM_DEVICE*>(const_cast<PVOID>(Handle));
}

static BC250_WDDM_CONTEXT*
Bc250WddmContextFromHandle(
    _In_ CONST HANDLE Handle
    )
{
    return reinterpret_cast<BC250_WDDM_CONTEXT*>(const_cast<PVOID>(Handle));
}

static BC250_WDDM_ALLOCATION*
Bc250WddmAllocationFromHandle(
    _In_ CONST HANDLE Handle
    )
{
    return reinterpret_cast<BC250_WDDM_ALLOCATION*>(const_cast<PVOID>(Handle));
}

static SIZE_T
Bc250WddmAlignSize(
    _In_ SIZE_T Size
    )
{
    return (Size + PAGE_SIZE - 1u) & ~((SIZE_T)PAGE_SIZE - 1u);
}

static PVOID
Bc250WddmAllocateContiguous(
    _In_ SIZE_T Size,
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress
    )
{
    PHYSICAL_ADDRESS lowest;
    PHYSICAL_ADDRESS highest;
    PHYSICAL_ADDRESS boundary;
    PVOID address;

    if (PhysicalAddress == NULL || Size == 0) {
        return NULL;
    }

    lowest.QuadPart = 0;
    highest.QuadPart = MAXULONG64;
    boundary.QuadPart = 0;
    address = MmAllocateContiguousMemorySpecifyCache(
        Size,
        lowest,
        highest,
        boundary,
        MmWriteCombined);
    if (address == NULL) {
        return NULL;
    }

    RtlZeroMemory(address, Size);
    *PhysicalAddress = MmGetPhysicalAddress(address);
    return address;
}

extern "C" {

NTSTATUS APIENTRY
Bc250FullDdiCreateDevice(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_CREATEDEVICE* CreateDevice
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));
    BC250_WDDM_DEVICE* device;

    if (adapter == NULL || CreateDevice == NULL ||
        !adapter->DeviceStarted) {
        return STATUS_DEVICE_NOT_READY;
    }

    device = static_cast<BC250_WDDM_DEVICE*>(ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(*device),
        BC250_POOL_TAG));
    if (device == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(device, sizeof(*device));
    device->Adapter = adapter;
    device->ReferenceCount = 1;

    if (CreateDevice->pInfo != NULL) {
        CreateDevice->pInfo->DmaBufferSize = BC250_WDDM_DMA_BUFFER_SIZE;
        CreateDevice->pInfo->DmaBufferSegmentSet = 0;
        CreateDevice->pInfo->DmaBufferPrivateDataSize = sizeof(ULONG64);
        CreateDevice->pInfo->AllocationListSize = BC250_WDDM_ALLOCATION_LIST_SIZE;
        CreateDevice->pInfo->PatchLocationListSize = BC250_WDDM_PATCH_LIST_SIZE;
        RtlZeroMemory(&CreateDevice->pInfo->Flags,
                      sizeof(CreateDevice->pInfo->Flags));
    }

    CreateDevice->hDevice = reinterpret_cast<HANDLE>(device);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiDestroyDevice(
    _In_ CONST HANDLE Device
    )
{
    BC250_WDDM_DEVICE* device = Bc250WddmDeviceFromHandle(Device);

    if (device == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    ExFreePoolWithTag(device, BC250_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiCreateContext(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_CREATECONTEXT* CreateContext
    )
{
    BC250_WDDM_DEVICE* device = Bc250WddmDeviceFromHandle(Device);
    BC250_WDDM_CONTEXT* context;

    if (device == NULL || CreateContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    context = static_cast<BC250_WDDM_CONTEXT*>(ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(*context),
        BC250_POOL_TAG));
    if (context == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    context->NodeOrdinal = CreateContext->NodeOrdinal;
    context->EngineAffinity = CreateContext->EngineAffinity;
    InterlockedIncrement(&device->ReferenceCount);

    RtlZeroMemory(&CreateContext->ContextInfo,
                  sizeof(CreateContext->ContextInfo));
    CreateContext->ContextInfo.DmaBufferSize = BC250_WDDM_DMA_BUFFER_SIZE;
    CreateContext->ContextInfo.DmaBufferSegmentSet = 0;
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = sizeof(ULONG64);
    CreateContext->ContextInfo.AllocationListSize = BC250_WDDM_ALLOCATION_LIST_SIZE;
    CreateContext->ContextInfo.PatchLocationListSize = BC250_WDDM_PATCH_LIST_SIZE;
    CreateContext->hContext = reinterpret_cast<HANDLE>(context);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiDestroyContext(
    _In_ CONST HANDLE Context
    )
{
    BC250_WDDM_CONTEXT* context = Bc250WddmContextFromHandle(Context);

    if (context == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    if (context->Device != NULL) {
        InterlockedDecrement(&context->Device->ReferenceCount);
    }
    ExFreePoolWithTag(context, BC250_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiCreateAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_CREATEALLOCATION* CreateAllocation
    )
{
    BC250_WDDM_DEVICE* device = Bc250WddmDeviceFromHandle(Device);
    UINT index;

    if (device == NULL || CreateAllocation == NULL ||
        CreateAllocation->pAllocationInfo == NULL ||
        CreateAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    for (index = 0; index < CreateAllocation->NumAllocations; ++index) {
        DXGK_ALLOCATIONINFO* info = &CreateAllocation->pAllocationInfo[index];
        BC250_WDDM_ALLOCATION* allocation;
        SIZE_T size = Bc250WddmAlignSize(info->Size);

        if (size == 0 || size > BC250_WDDM_MAX_ALLOCATION) {
            return STATUS_INVALID_BUFFER_SIZE;
        }

        allocation = static_cast<BC250_WDDM_ALLOCATION*>(ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(*allocation),
            BC250_POOL_TAG));
        if (allocation == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(allocation, sizeof(*allocation));
        allocation->Device = device;
        allocation->Size = size;
        allocation->CpuAddress = Bc250WddmAllocateContiguous(
            size,
            &allocation->PhysicalAddress);
        if (allocation->CpuAddress == NULL) {
            ExFreePoolWithTag(allocation, BC250_POOL_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        allocation->Width = 0;
        allocation->Height = 0;
        allocation->Format = D3DDDIFMT_UNKNOWN;
        info->Size = size;
        info->Alignment = PAGE_SIZE;
        info->PitchAlignedSize = 0;
        info->PreferredSegment.SegmentId0 = 0;
        info->SupportedReadSegmentSet = 1;
        info->SupportedWriteSegmentSet = 1;
        info->EvictionSegmentSet = 0;
        info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        info->hAllocation = reinterpret_cast<HANDLE>(allocation);
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiDestroyAllocation(
    _In_ CONST HANDLE Device,
    _In_ CONST HANDLE Allocation
    )
{
    BC250_WDDM_ALLOCATION* allocation = Bc250WddmAllocationFromHandle(Allocation);

    UNREFERENCED_PARAMETER(Device);
    if (allocation == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    if (allocation->CpuAddress != NULL) {
        MmFreeContiguousMemory(allocation->CpuAddress);
    }
    ExFreePoolWithTag(allocation, BC250_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiDescribeAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* DescribeAllocation
    )
{
    BC250_WDDM_ALLOCATION* allocation;

    UNREFERENCED_PARAMETER(Device);
    if (DescribeAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    allocation = Bc250WddmAllocationFromHandle(DescribeAllocation->hAllocation);
    if (allocation == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    DescribeAllocation->Width = allocation->Width;
    DescribeAllocation->Height = allocation->Height;
    DescribeAllocation->Format = allocation->Format;
    RtlZeroMemory(&DescribeAllocation->MultisampleMethod,
                  sizeof(DescribeAllocation->MultisampleMethod));
    DescribeAllocation->RefreshRate.Numerator = 0;
    DescribeAllocation->RefreshRate.Denominator = 1;
    DescribeAllocation->PrivateDriverFormatAttribute = 0;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiGetStandardAllocationDriverData(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* StandardAllocation
    )
{
    UNREFERENCED_PARAMETER(Device);
    if (StandardAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (StandardAllocation->pAllocationPrivateDriverData == NULL &&
        StandardAllocation->pResourcePrivateDriverData == NULL) {
        StandardAllocation->AllocationPrivateDriverDataSize = sizeof(ULONG64);
        StandardAllocation->ResourcePrivateDriverDataSize = 0;
        return STATUS_SUCCESS;
    }

    if (StandardAllocation->pAllocationPrivateDriverData != NULL &&
        StandardAllocation->AllocationPrivateDriverDataSize >= sizeof(ULONG64)) {
        RtlZeroMemory(StandardAllocation->pAllocationPrivateDriverData,
                      StandardAllocation->AllocationPrivateDriverDataSize);
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiOpenAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_OPENALLOCATION* OpenAllocation
    )
{
    UINT index;

    UNREFERENCED_PARAMETER(Device);
    if (OpenAllocation == NULL || OpenAllocation->pOpenAllocation == NULL ||
        OpenAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    for (index = 0; index < OpenAllocation->NumAllocations; ++index) {
        DXGK_OPENALLOCATIONINFO* info = &OpenAllocation->pOpenAllocation[index];
        if (info->hAllocation == 0) {
            return STATUS_INVALID_HANDLE;
        }
        info->hDeviceSpecificAllocation =
            reinterpret_cast<HANDLE>((ULONG_PTR)info->hAllocation);
    }

    OpenAllocation->SubresourceOffset = 0;
    OpenAllocation->Pitch = 0;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiCloseAllocation(
    _In_ CONST HANDLE Device,
    _In_ CONST HANDLE Allocation
    )
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Allocation);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiRender(
    _In_ CONST HANDLE Context,
    _Inout_ DXGKARG_RENDER* Render
    )
{
    BC250_WDDM_CONTEXT* context = Bc250WddmContextFromHandle(Context);

    if (context == NULL || Render == NULL || Render->pCommand == NULL ||
        Render->pDmaBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Render->CommandLength > Render->DmaSize) {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    if ((Render->CommandLength & 3u) != 0) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    RtlCopyMemory(Render->pDmaBuffer,
                  Render->pCommand,
                  Render->CommandLength);
    Render->pDmaBuffer = static_cast<PUCHAR>(Render->pDmaBuffer) +
                         Render->CommandLength;
    Render->MultipassOffset = 0;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiPatch(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_PATCH* Patch
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));

    if (adapter == NULL || Patch == NULL || !adapter->DeviceStarted) {
        return STATUS_INVALID_PARAMETER;
    }

    /* The experimental UMA path uses physical DMA addresses supplied by VidMm;
     * no relocations are needed for the first linear command path. */
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiBuildPagingBuffer(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER* BuildPagingBuffer
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));

    if (adapter == NULL || BuildPagingBuffer == NULL ||
        !adapter->DeviceStarted) {
        return STATUS_INVALID_PARAMETER;
    }

    BuildPagingBuffer->DmaBufferWriteOffset = 0;
    switch (BuildPagingBuffer->Operation) {
    case DXGK_OPERATION_TRANSFER:
        if (BuildPagingBuffer->Transfer.Source.SegmentId == 0 &&
            BuildPagingBuffer->Transfer.Destination.SegmentId == 0 &&
            BuildPagingBuffer->Transfer.Source.pMdl != NULL &&
            BuildPagingBuffer->Transfer.Destination.pMdl != NULL) {
            PVOID source = MmGetSystemAddressForMdlSafe(
                BuildPagingBuffer->Transfer.Source.pMdl,
                NormalPagePriority);
            PVOID destination = MmGetSystemAddressForMdlSafe(
                BuildPagingBuffer->Transfer.Destination.pMdl,
                NormalPagePriority);
            if (source == NULL || destination == NULL) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlCopyMemory(
                static_cast<PUCHAR>(destination) +
                    BuildPagingBuffer->Transfer.MdlOffset * PAGE_SIZE,
                static_cast<PUCHAR>(source) +
                    BuildPagingBuffer->Transfer.MdlOffset * PAGE_SIZE,
                BuildPagingBuffer->Transfer.TransferSize);
            return STATUS_SUCCESS;
        }
        return STATUS_SUCCESS;

    case DXGK_OPERATION_FILL:
        if (BuildPagingBuffer->Fill.Destination.SegmentId == 0) {
            return STATUS_SUCCESS;
        }
        return STATUS_SUCCESS;

    case DXGK_OPERATION_DISCARD_CONTENT:
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        return STATUS_SUCCESS;

    default:
        return STATUS_SUCCESS;
    }
}

NTSTATUS APIENTRY
Bc250FullDdiSubmitCommand(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND* SubmitCommand
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));
    ULONG endOffset;

    if (adapter == NULL || SubmitCommand == NULL ||
        !adapter->DeviceStarted) {
        return STATUS_INVALID_PARAMETER;
    }
    if (SubmitCommand->DmaBufferSubmissionEndOffset <=
        SubmitCommand->DmaBufferSubmissionStartOffset ||
        SubmitCommand->DmaBufferSubmissionEndOffset > SubmitCommand->DmaBufferSize) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    endOffset = SubmitCommand->DmaBufferSubmissionEndOffset;
    return Bc250SubmitDmaBuffer(
        &adapter->Hw,
        &adapter->Gfx,
        SubmitCommand->DmaBufferPhysicalAddress,
        SubmitCommand->DmaBufferSubmissionStartOffset,
        endOffset,
        SubmitCommand->SubmissionFenceId);
}

NTSTATUS APIENTRY
Bc250FullDdiPreemptCommand(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND* PreemptCommand
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));

    if (adapter == NULL || PreemptCommand == NULL ||
        !adapter->DeviceStarted) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiQueryCurrentFence(
    _In_ CONST HANDLE Adapter,
    _In_ DXGKARG_QUERYCURRENTFENCE* QueryCurrentFence
    )
{
    BC250_DEVICE_CONTEXT* context =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));
    ULONG64 fenceValue;
    NTSTATUS status;

    if (context == NULL || QueryCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!context->DeviceStarted || !context->Gfx.InterruptsEnabled) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = Bc250ReadCompletedFence(
        &context->Gfx,
        Bc250EngineGfx,
        &fenceValue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    QueryCurrentFence->CurrentFence = fenceValue;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiResetFromTimeout(
    _In_ CONST HANDLE Adapter
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));
    ULONG index;

    if (adapter == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    for (index = 0; index < adapter->Gfx.EngineCount; ++index) {
        BC250_GFX_ENGINE* engine = &adapter->Gfx.Engines[index];
        engine->RingWritePointer = 0;
        engine->RingReadPointer = 0;
        engine->Fence.LastCompleted = 0;
        engine->Fence.NextValue = 1;
        if (engine->RingCpuAddress != NULL) {
            RtlZeroMemory(engine->RingCpuAddress, engine->RingSize);
        }
        if (engine->Fence.CpuAddress != NULL) {
            InterlockedExchange64(
                (volatile LONG64*)engine->Fence.CpuAddress,
                0);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY
Bc250FullDdiRestartFromTimeout(
    _In_ CONST HANDLE Adapter
    )
{
    return Bc250FullDdiResetFromTimeout(Adapter);
}

NTSTATUS APIENTRY
Bc250FullDdiSetVidPnSourceAddress(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS* SetVidPnSourceAddress
    )
{
    BC250_DEVICE_CONTEXT* adapter =
        reinterpret_cast<BC250_DEVICE_CONTEXT*>(const_cast<PVOID>(Adapter));

    if (adapter == NULL || SetVidPnSourceAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

} /* extern "C" */
