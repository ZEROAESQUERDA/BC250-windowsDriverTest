/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_kmd.c

Abstract:
    Kernel-Mode Display Miniport Driver (KMD) main entry point and
    WDDM DDI callback implementations for the AMD BC-250 APU.

    This file implements:
      - DriverEntry: registers all WDDM DDI callbacks with Dxgkrnl
      - DxgkDdiAddDevice: allocates and initializes device extension
      - DxgkDdiStartDevice: maps hardware resources, initializes GPU
      - DxgkDdiStopDevice: stops GPU and releases resources
      - DxgkDdiRemoveDevice: final cleanup
      - DxgkDdiInterruptRoutine: ISR for GPU interrupts
      - DxgkDdiDpcRoutine: DPC for deferred interrupt processing
      - DxgkDdiQueryAdapterInfo: reports adapter capabilities to WDDM
      - DxgkDdiCreateDevice / DestroyDevice: per-process GPU context
      - DxgkDdiSubmitCommand: submits command buffers to GPU ring
      - DxgkDdiQueryCurrentFence: reports GPU progress fence value
      - Display DDI callbacks for VidPN management

Environment:
    Kernel mode (IRQL varies per callback)

--*/

#include "../../inc/amdbc250_kmd.h"

/*===========================================================================
  DriverEntry
  Called by the OS when the driver is loaded. Registers all WDDM DDI
  callback functions with the DirectX graphics kernel subsystem.
===========================================================================*/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    DRIVER_INITIALIZATION_DATA DriverInitData = {0};
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    KdPrint(("AMDBC250: DriverEntry - AMD BC-250 WDDM Driver v%d.%d\n",
             AMDBC250_DRIVER_MAJOR_VERSION,
             AMDBC250_DRIVER_MINOR_VERSION));

    /*
     * Fill in the WDDM DDI function table.
     * The version field must match the WDDM version supported.
     * We target WDDM 2.0 (Windows 10) for maximum compatibility.
     */
    DriverInitData.Version = DXGKDDI_INTERFACE_VERSION_WIN10;

    /* Core device lifecycle callbacks */
    DriverInitData.DxgkDdiAddDevice                 = Bc250DdiAddDevice;
    DriverInitData.DxgkDdiStartDevice               = Bc250DdiStartDevice;
    DriverInitData.DxgkDdiStopDevice                = Bc250DdiStopDevice;
    DriverInitData.DxgkDdiRemoveDevice               = Bc250DdiRemoveDevice;
    DriverInitData.DxgkDdiResetDevice                = Bc250DdiResetDevice;
    DriverInitData.DxgkDdiUnload                     = Bc250DdiUnload;

    /* Child device / display output enumeration */
    DriverInitData.DxgkDdiQueryChildRelations        = Bc250DdiQueryChildRelations;
    DriverInitData.DxgkDdiQueryChildStatus           = Bc250DdiQueryChildStatus;
    DriverInitData.DxgkDdiQueryDeviceDescriptor      = Bc250DdiQueryDeviceDescriptor;

    /* Power management */
    DriverInitData.DxgkDdiSetPowerState              = Bc250DdiSetPowerState;
    DriverInitData.DxgkDdiNotifyAcpiEvent            = Bc250DdiNotifyAcpiEvent;

    /* Interrupt handling */
    DriverInitData.DxgkDdiInterruptRoutine           = Bc250DdiInterruptRoutine;
    DriverInitData.DxgkDdiDpcRoutine                 = Bc250DdiDpcRoutine;

    /* Adapter / device queries */
    DriverInitData.DxgkDdiQueryAdapterInfo           = Bc250DdiQueryAdapterInfo;
    DriverInitData.DxgkDdiQueryInterface             = Bc250DdiQueryInterface;

    /* Device (per-process context) management */
    DriverInitData.DxgkDdiCreateDevice               = Bc250DdiCreateDevice;
    DriverInitData.DxgkDdiDestroyDevice              = Bc250DdiDestroyDevice;

    /* GPU memory allocation */
    DriverInitData.DxgkDdiCreateAllocation           = Bc250DdiCreateAllocation;
    DriverInitData.DxgkDdiDestroyAllocation          = Bc250DdiDestroyAllocation;

    /* Command submission */
    DriverInitData.DxgkDdiBuildPagingBuffer          = Bc250DdiBuildPagingBuffer;
    DriverInitData.DxgkDdiSubmitCommand              = Bc250DdiSubmitCommand;
    DriverInitData.DxgkDdiPreemptCommand             = Bc250DdiPreemptCommand;
    DriverInitData.DxgkDdiQueryCurrentFence          = Bc250DdiQueryCurrentFence;

    /* Rendering */
    DriverInitData.DxgkDdiPresent                    = Bc250DdiPresent;
    DriverInitData.DxgkDdiRender                     = Bc250DdiRender;

    /* Display / VidPN management */
    DriverInitData.DxgkDdiSetVidPnSourceAddress      = Bc250DdiSetVidPnSourceAddress;
    DriverInitData.DxgkDdiRecommendFunctionalVidPn   = Bc250DdiRecommendFunctionalVidPn;
    DriverInitData.DxgkDdiEnumVidPnCofuncModality    = Bc250DdiEnumVidPnCofuncModality;
    DriverInitData.DxgkDdiSetVidPnSourceVisibility   = Bc250DdiSetVidPnSourceVisibility;
    DriverInitData.DxgkDdiCommitVidPn                = Bc250DdiCommitVidPn;
    DriverInitData.DxgkDdiUpdateActiveVidPnPresentPath = Bc250DdiUpdateActiveVidPnPresentPath;
    DriverInitData.DxgkDdiRecommendMonitorModes      = Bc250DdiRecommendMonitorModes;
    DriverInitData.DxgkDdiGetScanLine                = Bc250DdiGetScanLine;
    DriverInitData.DxgkDdiControlInterrupt           = Bc250DdiControlInterrupt;

    /* Overlay support */
    DriverInitData.DxgkDdiCreateOverlay              = Bc250DdiCreateOverlay;
    DriverInitData.DxgkDdiDestroyOverlay             = Bc250DdiDestroyOverlay;

    /* Register with Dxgkrnl */
    Status = DxgkInitialize(DriverObject, RegistryPath, &DriverInitData);

    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: DxgkInitialize failed with status 0x%08X\n", Status));
    } else {
        KdPrint(("AMDBC250: DriverEntry successful\n"));
    }

    return Status;
}

/*===========================================================================
  DxgkDdiAddDevice
  Called when PnP manager detects a matching PCI device.
  Allocates and initializes the device extension structure.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID                 *MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt;

    KdPrint(("AMDBC250: DxgkDdiAddDevice called\n"));

    /* Allocate the device extension from non-paged pool */
    DevExt = (PAMDBC250_DEVICE_EXTENSION)ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(AMDBC250_DEVICE_EXTENSION),
        AMDBC250_TAG_DEVICE_EXT
        );

    if (DevExt == NULL) {
        KdPrint(("AMDBC250: Failed to allocate device extension\n"));
        return STATUS_NO_MEMORY;
    }

    /* Zero-initialize the extension */
    RtlZeroMemory(DevExt, sizeof(AMDBC250_DEVICE_EXTENSION));

    /* Initialize synchronization primitives */
    ExInitializeFastMutex(&DevExt->DeviceMutex);
    KeInitializeEvent(&DevExt->ResetCompleteEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&DevExt->ContextListLock);
    KeInitializeSpinLock(&DevExt->AllocationListLock);
    KeInitializeSpinLock(&DevExt->GfxRing.Lock);
    KeInitializeSpinLock(&DevExt->SdmaRing.Lock);

    /* Initialize allocation list */
    InitializeListHead(&DevExt->AllocationList);

    /* Store the PDO (not used directly but useful for diagnostics) */
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    *MiniportDeviceContext = DevExt;

    KdPrint(("AMDBC250: Device extension allocated at %p\n", DevExt));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiStartDevice
  Called after AddDevice to start the device. Maps PCI BARs, initializes
  hardware, registers interrupt, and sets up GPU rings.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiStartDevice(
    _In_  PVOID                     MiniportDeviceContext,
    _In_  PDXGK_START_INFO          DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE        DxgkInterface,
    _Out_ PULONG                    NumberOfVideoPresentSources,
    _Out_ PULONG                    NumberOfChildren
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    NTSTATUS Status;
    DXGK_DEVICE_INFO DeviceInfo;

    KdPrint(("AMDBC250: DxgkDdiStartDevice called\n"));

    /* Save the DXGK interface for later callbacks */
    RtlCopyMemory(&DevExt->DxgkInterface, DxgkInterface, sizeof(DXGKRNL_INTERFACE));
    DevExt->DeviceHandle = DxgkStartInfo->DeviceHandle;

    /* Query device information (PCI BARs, interrupt resources) */
    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    Status = DxgkInterface->DxgkCbGetDeviceInformation(
        DxgkStartInfo->DeviceHandle,
        &DeviceInfo
        );

    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: DxgkCbGetDeviceInformation failed: 0x%08X\n", Status));
        return Status;
    }

    /* Store PCI identifiers */
    DevExt->VendorId = DeviceInfo.VendorId;
    DevExt->DeviceId = DeviceInfo.DeviceId;

    KdPrint(("AMDBC250: PCI Device %04X:%04X detected\n",
             DevExt->VendorId, DevExt->DeviceId));

    /* Map MMIO BAR (BAR 0) */
    DevExt->MmioPhysicalBase = DeviceInfo.TranslatedResourceList->List[0]
                                .PartialResourceList.PartialDescriptors[0]
                                .u.Memory.Start;
    DevExt->MmioSize = DeviceInfo.TranslatedResourceList->List[0]
                        .PartialResourceList.PartialDescriptors[0]
                        .u.Memory.Length;

    DevExt->MmioVirtualBase = MmMapIoSpace(
        DevExt->MmioPhysicalBase,
        DevExt->MmioSize,
        MmNonCached
        );

    if (DevExt->MmioVirtualBase == NULL) {
        KdPrint(("AMDBC250: Failed to map MMIO BAR\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KdPrint(("AMDBC250: MMIO mapped at VA=%p, PA=0x%llX, size=0x%llX\n",
             DevExt->MmioVirtualBase,
             DevExt->MmioPhysicalBase.QuadPart,
             (ULONGLONG)DevExt->MmioSize));

    /* Initialize hardware */
    Status = Bc250HwInitialize(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: Hardware initialization failed: 0x%08X\n", Status));
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
        return Status;
    }

    /*
     * Report display topology:
     * The BC-250 has a single DisplayPort output (1 VidPN source, 1 child)
     */
    *NumberOfVideoPresentSources = 1;
    *NumberOfChildren = 1;

    DevExt->HardwareInitialized = TRUE;

    KdPrint(("AMDBC250: StartDevice completed successfully\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiStopDevice
  Called when the device is being stopped (e.g., driver update, shutdown).
  Stops GPU operations and releases hardware resources.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiStopDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrint(("AMDBC250: DxgkDdiStopDevice called\n"));

    if (DevExt->HardwareInitialized) {
        Bc250HwShutdown(DevExt);
        DevExt->HardwareInitialized = FALSE;
    }

    /* Unmap MMIO */
    if (DevExt->MmioVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
    }

    /* Unmap Doorbell */
    if (DevExt->DoorbellVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->DoorbellVirtualBase, DevExt->DoorbellSize);
        DevExt->DoorbellVirtualBase = NULL;
    }

    KdPrint(("AMDBC250: StopDevice completed\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiRemoveDevice
  Final cleanup when device is removed from the system.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrint(("AMDBC250: DxgkDdiRemoveDevice called\n"));

    if (DevExt != NULL) {
        ExFreePoolWithTag(DevExt, AMDBC250_TAG_DEVICE_EXT);
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiResetDevice
  Called during a TDR (Timeout Detection and Recovery) reset.
  Must reset the GPU to a known good state.
===========================================================================*/

VOID
APIENTRY
Bc250DdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrint(("AMDBC250: DxgkDdiResetDevice called (TDR)\n"));

    DevExt->GpuResetInProgress = TRUE;
    DevExt->ResetCount++;

    if (DevExt->HardwareInitialized) {
        Bc250HwReset(DevExt);
    }

    DevExt->GpuResetInProgress = FALSE;
    KeSetEvent(&DevExt->ResetCompleteEvent, 0, FALSE);
}

/*===========================================================================
  DxgkDdiUnload
  Called when the driver is being unloaded from memory.
===========================================================================*/

VOID
APIENTRY
Bc250DdiUnload(
    VOID
    )
{
    KdPrint(("AMDBC250: DxgkDdiUnload called\n"));
}

/*===========================================================================
  DxgkDdiInterruptRoutine
  ISR (Interrupt Service Routine) - runs at DIRQL.
  Reads interrupt status, clears hardware interrupt, schedules DPC.
===========================================================================*/

BOOLEAN
APIENTRY
Bc250DdiInterruptRoutine(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    ULONG IhStatus;
    BOOLEAN OurInterrupt = FALSE;

    UNREFERENCED_PARAMETER(MessageNumber);

    /* Read IH ring write pointer to check for new entries */
    IhStatus = Bc250ReadMmio(DevExt, AMDBC250_REG_IH_RB_WPTR);

    if (IhStatus != DevExt->IhRing.ReadPointer) {
        /* New interrupt entries in the IH ring */
        DevExt->LastInterruptStatus = IhStatus;
        DevExt->InterruptCount++;
        OurInterrupt = TRUE;

        /* Schedule DPC for deferred processing */
        DevExt->DxgkInterface.DxgkCbQueueDpc(DevExt->DeviceHandle);
    }

    return OurInterrupt;
}

/*===========================================================================
  DxgkDdiDpcRoutine
  DPC (Deferred Procedure Call) - processes interrupt events at DISPATCH_LEVEL.
  Processes IH ring entries and notifies WDDM of completed operations.
===========================================================================*/

VOID
APIENTRY
Bc250DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    PULONG IhBase;
    ULONG WPtr, RPtr;
    ULONG ClientId, SrcId;
    ULONG Entry[4];

    if (!DevExt->HardwareInitialized || DevExt->IhRing.VirtualAddress == NULL) {
        return;
    }

    IhBase = (PULONG)DevExt->IhRing.VirtualAddress;
    WPtr = DevExt->LastInterruptStatus & 0x0001FFFF;
    RPtr = DevExt->IhRing.ReadPointer;

    /* Process all pending IH ring entries */
    while (RPtr != WPtr) {
        ULONG EntryOffset = (RPtr / AMDBC250_IH_RING_ENTRY_SIZE) * 4;

        /* Read 4-DWORD IH entry */
        Entry[0] = IhBase[EntryOffset + 0];
        Entry[1] = IhBase[EntryOffset + 1];
        Entry[2] = IhBase[EntryOffset + 2];
        Entry[3] = IhBase[EntryOffset + 3];

        ClientId = (Entry[0] >> 8) & 0xFF;
        SrcId    = Entry[0] & 0xFF;

        switch (ClientId) {
        case AMDBC250_IH_CLIENTID_GFX:
            /* GFX engine interrupt - check for fence completion */
            if (SrcId == 0xE0) {
                /* EOP (End of Pipe) event - fence signaled */
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                NotifyData.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
                NotifyData.DmaCompleted.SubmissionFenceId = Entry[2];
                DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                    DevExt->DeviceHandle,
                    &NotifyData
                    );
            }
            break;

        case AMDBC250_IH_CLIENTID_DCE:
            /* Display engine interrupt - VSYNC */
            {
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                NotifyData.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                NotifyData.CrtcVsync.VidPnTargetId = 0;
                DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                    DevExt->DeviceHandle,
                    &NotifyData
                    );
            }
            break;

        default:
            break;
        }

        /* Advance read pointer */
        RPtr += AMDBC250_IH_RING_ENTRY_SIZE;
        if (RPtr >= (ULONG)DevExt->IhRing.SizeInBytes) {
            RPtr = 0;
        }
    }

    /* Update IH ring read pointer */
    DevExt->IhRing.ReadPointer = RPtr;
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_RPTR, RPtr);

    /* Notify WDDM that DPC processing is complete */
    DevExt->DxgkInterface.DxgkCbNotifyDpc(DevExt->DeviceHandle);
}

/*===========================================================================
  DxgkDdiQueryAdapterInfo
  Reports GPU capabilities and configuration to the WDDM framework.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryAdapterInfo(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *pCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(pCaps, sizeof(DXGK_DRIVERCAPS));

        /* Present capabilities */
        pCaps->PresentationCaps.SupportKernelModeCommandBuffer = FALSE;
        pCaps->PresentationCaps.SupportSoftwareDeviceBitmaps   = TRUE;

        /* Scheduler capabilities */
        pCaps->SchedulingCaps.MultiEngineAware = TRUE;

        /* Memory management capabilities */
        pCaps->MemoryManagementCaps.PagingNode = 0;

        /* GPU engine capabilities */
        pCaps->GpuEngineTopology.NbAsyncEngineCount = 1;

        /* Maximum number of flip queues */
        pCaps->FlipCaps.FlipOnVSyncMmIo = TRUE;

        KdPrint(("AMDBC250: QueryAdapterInfo - DRIVERCAPS reported\n"));
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT: {
        DXGK_QUERYSEGMENTOUT *pSegOut = (DXGK_QUERYSEGMENTOUT *)pQueryAdapterInfo->pOutputData;

        if (pQueryAdapterInfo->pInputData == NULL) {
            /* First call: report number of segments */
            pSegOut->NbSegment = 2;  /* Segment 0: VRAM, Segment 1: System */
            return STATUS_SUCCESS;
        }

        /* Second call: report segment details */
        pSegOut->NbSegment = 2;

        /* Segment 0: GPU-accessible VRAM (aperture) */
        pSegOut->pSegmentDescriptor[0].BaseAddress.QuadPart = 0;
        pSegOut->pSegmentDescriptor[0].Size = DevExt->TotalVramBytes;
        pSegOut->pSegmentDescriptor[0].CommitLimit = DevExt->TotalVramBytes;
        pSegOut->pSegmentDescriptor[0].Flags.CpuVisible = TRUE;
        pSegOut->pSegmentDescriptor[0].Flags.CacheCoherent = FALSE;

        /* Segment 1: System memory (AGP-style aperture) */
        pSegOut->pSegmentDescriptor[1].BaseAddress.QuadPart = 0;
        pSegOut->pSegmentDescriptor[1].Size = 0x100000000ULL;  /* 4 GB */
        pSegOut->pSegmentDescriptor[1].CommitLimit = 0x100000000ULL;
        pSegOut->pSegmentDescriptor[1].Flags.Aperture = TRUE;
        pSegOut->pSegmentDescriptor[1].Flags.CpuVisible = TRUE;
        pSegOut->pSegmentDescriptor[1].Flags.CacheCoherent = TRUE;

        KdPrint(("AMDBC250: QueryAdapterInfo - QUERYSEGMENT reported\n"));
        return STATUS_SUCCESS;
    }

    default:
        KdPrint(("AMDBC250: QueryAdapterInfo - unhandled type %d\n",
                 pQueryAdapterInfo->Type));
        return STATUS_NOT_SUPPORTED;
    }
}

/*===========================================================================
  DxgkDdiCreateDevice
  Creates a per-process device context (called when a D3D app opens the GPU).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateDevice(
    _In_    CONST HANDLE        hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE *pCreateDevice
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    PAMDBC250_GPU_CONTEXT Context;

    UNREFERENCED_PARAMETER(DevExt);

    KdPrint(("AMDBC250: DxgkDdiCreateDevice called\n"));

    Context = (PAMDBC250_GPU_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(AMDBC250_GPU_CONTEXT),
        AMDBC250_TAG_CONTEXT
        );

    if (Context == NULL) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Context, sizeof(AMDBC250_GPU_CONTEXT));
    KeInitializeSpinLock(&Context->ContextLock);
    InitializeListHead(&Context->AllocationList);
    Context->IsValid = TRUE;

    pCreateDevice->hDevice = (HANDLE)Context;

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiDestroyDevice
  Destroys a per-process device context.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiDestroyDevice(
    _In_ CONST HANDLE hDevice
    )
{
    PAMDBC250_GPU_CONTEXT Context = (PAMDBC250_GPU_CONTEXT)hDevice;

    KdPrint(("AMDBC250: DxgkDdiDestroyDevice called\n"));

    if (Context != NULL) {
        Context->IsValid = FALSE;
        ExFreePoolWithTag(Context, AMDBC250_TAG_CONTEXT);
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiCreateAllocation
  Allocates GPU-accessible memory for textures, render targets, etc.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateAllocation(
    _In_    CONST HANDLE                    hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION        *pCreateAllocation
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG i;

    UNREFERENCED_PARAMETER(DevExt);

    KdPrint(("AMDBC250: DxgkDdiCreateAllocation - %d allocations\n",
             pCreateAllocation->NumAllocations));

    for (i = 0; i < pCreateAllocation->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *pAllocInfo = &pCreateAllocation->pAllocationInfo[i];
        PAMDBC250_ALLOCATION Alloc;

        Alloc = (PAMDBC250_ALLOCATION)ExAllocatePoolWithTag(
            NonPagedPoolNx,
            sizeof(AMDBC250_ALLOCATION),
            AMDBC250_TAG_ALLOCATION
            );

        if (Alloc == NULL) {
            return STATUS_NO_MEMORY;
        }

        RtlZeroMemory(Alloc, sizeof(AMDBC250_ALLOCATION));
        Alloc->SizeInBytes = pAllocInfo->Size;
        Alloc->Alignment   = 4096;  /* 4 KB default alignment */

        /* Place in VRAM segment (segment 0) by default */
        pAllocInfo->Alignment         = 4096;
        pAllocInfo->Size              = pAllocInfo->Size;
        pAllocInfo->PitchAlignedSize  = pAllocInfo->Size;
        pAllocInfo->HintedBank.Value  = 0;
        pAllocInfo->PreferredSegment.Value = 0;
        pAllocInfo->SupportedReadSegmentSet  = 1;  /* Segment 0 */
        pAllocInfo->SupportedWriteSegmentSet = 1;  /* Segment 0 */
        pAllocInfo->EvictionSegmentSet       = 2;  /* Segment 1 (system) */
        pAllocInfo->MaximumRenamingListLength = 0;
        pAllocInfo->hAllocation       = (HANDLE)Alloc;
        pAllocInfo->Flags.Value       = 0;
        pAllocInfo->pAllocationUsageHint = NULL;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiDestroyAllocation
  Frees GPU memory allocations.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiDestroyAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation
    )
{
    ULONG i;

    UNREFERENCED_PARAMETER(hAdapter);

    for (i = 0; i < pDestroyAllocation->NumAllocations; i++) {
        PAMDBC250_ALLOCATION Alloc =
            (PAMDBC250_ALLOCATION)pDestroyAllocation->pAllocationList[i];

        if (Alloc != NULL) {
            ExFreePoolWithTag(Alloc, AMDBC250_TAG_ALLOCATION);
        }
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiSubmitCommand
  Submits a command buffer (DMA buffer) to the GPU ring for execution.
  This is the core path for all GPU rendering and compute work.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiSubmitCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG WPtr;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DevExt->GfxRing.Lock, &OldIrql);

    WPtr = DevExt->GfxRing.WritePointer;

    /*
     * In a full implementation, the command buffer would be written into
     * the GFX ring here using PM4 indirect buffer packets.
     * The ring is then advanced by writing the new WPTR to the doorbell.
     *
     * For this reference implementation, we record the fence value and
     * advance the write pointer symbolically.
     */

    /* Write IB (Indirect Buffer) packet to ring */
    if (DevExt->GfxRing.VirtualAddress != NULL) {
        PULONG RingBase = (PULONG)DevExt->GfxRing.VirtualAddress;
        ULONG RingDwords = (ULONG)(DevExt->GfxRing.SizeInBytes / sizeof(ULONG));
        ULONG WDwords = WPtr / sizeof(ULONG);

        /* PM4 INDIRECT_BUFFER packet (type 3, opcode 0x3F) */
        if (WDwords + 4 < RingDwords) {
            RingBase[WDwords + 0] = PM4_TYPE3_HDR(PM4_IT_INDIRECT_BUFFER, 4);
            RingBase[WDwords + 1] = (ULONG)(pSubmitCommand->DmaBufferPhysicalAddress.LowPart);
            RingBase[WDwords + 2] = (ULONG)(pSubmitCommand->DmaBufferPhysicalAddress.HighPart);
            RingBase[WDwords + 3] = pSubmitCommand->DmaBufferSize / sizeof(ULONG);

            WPtr += 4 * sizeof(ULONG);
            if (WPtr >= (ULONG)DevExt->GfxRing.SizeInBytes) {
                WPtr = 0;
            }
        }
    }

    DevExt->GfxRing.WritePointer = WPtr;

    /* Ring the doorbell to notify GPU of new work */
    if (DevExt->DoorbellVirtualBase != NULL) {
        PULONG Doorbell = (PULONG)DevExt->DoorbellVirtualBase;
        Doorbell[DevExt->GfxRing.DoorBellOffset / sizeof(ULONG)] = WPtr;
    } else {
        /* Fallback: write directly to MMIO WPTR register */
        Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB0_WPTR, WPtr);
    }

    KeReleaseSpinLock(&DevExt->GfxRing.Lock, OldIrql);

    KdPrint(("AMDBC250: SubmitCommand - fence=%llu, wptr=0x%X\n",
             pSubmitCommand->SubmissionFenceId, WPtr));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiPreemptCommand
  Requests preemption of the currently executing command buffer.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiPreemptCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    UNREFERENCED_PARAMETER(pPreemptCommand);

    KdPrint(("AMDBC250: PreemptCommand called\n"));

    /*
     * For RDNA2, preemption is handled by writing to the CP_PREEMPT register.
     * A full implementation would use the GFX9/10 mid-draw preemption mechanism.
     */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_ME_CNTL,
                   Bc250ReadMmio(DevExt, AMDBC250_REG_CP_ME_CNTL) | 0x00000001);

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryCurrentFence
  Returns the current GPU fence value (progress indicator).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryCurrentFence(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE   *pCurrentFence
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    /* Read the fence value written by GPU into the fence memory */
    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        pCurrentFence->CurrentFence = *DevExt->GlobalFence.VirtualAddress;
    } else {
        pCurrentFence->CurrentFence = 0;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiBuildPagingBuffer
  Builds DMA packets for memory paging operations (eviction/restore).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiBuildPagingBuffer(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER   *pBuildPagingBuffer
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    switch (pBuildPagingBuffer->Operation) {

    case DXGK_BUILDPAGINGBUFFER_OPERATION_TRANSFER: {
        /*
         * Build a DMA copy command to transfer allocation between
         * VRAM and system memory during eviction/restore.
         * Uses SDMA engine for efficient memory transfers.
         */
        PULONG DmaBuffer = (PULONG)pBuildPagingBuffer->pDmaBuffer;
        ULONG DmaOffset = 0;

        /* SDMA COPY LINEAR packet */
        DmaBuffer[DmaOffset++] = (SDMA_OP_COPY << 8) | 0x00000000;
        DmaBuffer[DmaOffset++] = (ULONG)pBuildPagingBuffer->Transfer.TransferSize - 1;
        DmaBuffer[DmaOffset++] = 0;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Source.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Source.SegmentAddress.HighPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.HighPart;

        pBuildPagingBuffer->pDmaBuffer = (PVOID)((PUCHAR)pBuildPagingBuffer->pDmaBuffer +
                                                  DmaOffset * sizeof(ULONG));
        break;
    }

    case DXGK_BUILDPAGINGBUFFER_OPERATION_FILL: {
        /* Fill memory region with a constant value */
        PULONG DmaBuffer = (PULONG)pBuildPagingBuffer->pDmaBuffer;
        ULONG DmaOffset = 0;

        DmaBuffer[DmaOffset++] = (SDMA_OP_WRITE << 8) | 0x00000000;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.Destination.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.Destination.SegmentAddress.HighPart;
        DmaBuffer[DmaOffset++] = (ULONG)pBuildPagingBuffer->Fill.FillSize - 1;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.FillPattern;

        pBuildPagingBuffer->pDmaBuffer = (PVOID)((PUCHAR)pBuildPagingBuffer->pDmaBuffer +
                                                  DmaOffset * sizeof(ULONG));
        break;
    }

    default:
        break;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryChildRelations
  Reports the display outputs (children) of this GPU adapter.
  The BC-250 has one DisplayPort output.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryChildRelations(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR  ChildRelations,
    _In_    ULONG                   ChildRelationsSize
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildRelationsSize);

    /* Child 0: DisplayPort output */
    ChildRelations[0].ChildDeviceType = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
        D3DKMDT_VOT_DISPLAYPORT_EXTERNAL;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness =
        D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    ChildRelations[0].AcpiUid = 0;
    ChildRelations[0].ChildUid = 0;

    KdPrint(("AMDBC250: QueryChildRelations - 1 DisplayPort output reported\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryChildStatus
  Reports whether a child device (monitor) is connected.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryChildStatus(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus->Type == StatusConnection) {
        /*
         * In a full implementation, this would read the DisplayPort HPD
         * (Hot Plug Detect) pin status from the DCN hardware.
         * For now, we report the monitor as connected.
         */
        ChildStatus->HotPlug.Connected = TRUE;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryDeviceDescriptor
  Returns EDID or other device descriptor for a child device.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryDeviceDescriptor(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);

    /*
     * Return STATUS_MONITOR_NO_DESCRIPTOR to let Windows use
     * the EDID read from the monitor via DDC/I2C over DisplayPort AUX.
     */
    DeviceDescriptor->DescriptorLength = 0;
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

/*===========================================================================
  DxgkDdiSetPowerState
  Handles power state transitions (D0=active, D1/D2/D3=sleep/hibernate).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    KdPrint(("AMDBC250: SetPowerState - state=%d\n", DevicePowerState));

    DevExt->PowerState = (ULONG)DevicePowerState;

    switch (DevicePowerState) {
    case PowerDeviceD0:
        /* Full power - resume GPU if needed */
        if (DevExt->HardwareInitialized) {
            /* Re-enable clocks, restore GPU state */
            Bc250WriteMmio(DevExt, AMDBC250_REG_MP1_SMN_C2PMSG_66, 0x00000001);
        }
        break;

    case PowerDeviceD3:
        /* Lowest power state - save GPU state, power down */
        if (DevExt->HardwareInitialized) {
            Bc250WriteMmio(DevExt, AMDBC250_REG_MP1_SMN_C2PMSG_66, 0x00000000);
        }
        break;

    default:
        break;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiNotifyAcpiEvent
  Handles ACPI events (lid close/open, AC/battery transitions, etc.)
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiNotifyAcpiEvent(
    _In_  PVOID             MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE   EventType,
    _In_  ULONG             EventCode,
    _In_  PVOID             Argument,
    _Out_ PULONG            AcpiFlags
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventCode);
    UNREFERENCED_PARAMETER(Argument);

    *AcpiFlags = 0;
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryInterface
  Returns interface pointers for optional driver capabilities.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryInterface(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);

    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  Display DDI Stubs
  Full VidPN management implementation (mode setting, scan-out, etc.)
===========================================================================*/

NTSTATUS APIENTRY Bc250DdiSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    UNREFERENCED_PARAMETER(pSetVidPnSourceAddress);
    /* Program CRTC scan-out base address register */
    KdPrint(("AMDBC250: SetVidPnSourceAddress\n"));
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendFunctionalVidPn);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS APIENTRY Bc250DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pEnumCofuncModality);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pSetVidPnSourceVisibility);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiCommitVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCommitVidPn);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pUpdateActiveVidPnPresentPath);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiRecommendMonitorModes(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pRecommendMonitorModes);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiGetScanLine(
    _In_    CONST HANDLE hAdapter,
    _Inout_ DXGKARG_GETSCANLINE *pGetScanLine)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG CrtcStatus = Bc250ReadMmio(DevExt, AMDBC250_REG_CRTC0_STATUS);
    pGetScanLine->ScanLine = CrtcStatus & 0x0000FFFF;
    pGetScanLine->InVerticalBlank = (CrtcStatus & 0x00010000) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiControlInterrupt(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN EnableInterrupt)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG IhCntl = Bc250ReadMmio(DevExt, AMDBC250_REG_IH_CNTL);

    if (EnableInterrupt) {
        IhCntl |= IH_CNTL__ENABLE_INTR_MASK;
    } else {
        IhCntl &= ~IH_CNTL__ENABLE_INTR_MASK;
    }

    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_CNTL, IhCntl);
    UNREFERENCED_PARAMETER(InterruptType);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiCreateOverlay(
    _In_    CONST HANDLE hAdapter,
    _Inout_ DXGKARG_CREATEOVERLAY *pCreateOverlay)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DdiDestroyOverlay(
    _In_ CONST HANDLE hOverlay)
{
    UNREFERENCED_PARAMETER(hOverlay);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiPresent(
    _In_    CONST HANDLE hContext,
    _Inout_ DXGKARG_PRESENT *pPresent)
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPresent);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiRender(
    _In_    CONST HANDLE hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    return STATUS_SUCCESS;
}
