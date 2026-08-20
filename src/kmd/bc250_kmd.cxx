#include "bc250_kmd.hxx"
#include "bc250_full_wddm.hxx"
#include "../memory/bc250_vidmm.h"

#if BC250_ENABLE_FULL_WDDM
static NTSTATUS
Bc250InitializeFullWddm(
    _In_ DRIVER_OBJECT* DriverObject,
    _In_ UNICODE_STRING* RegistryPath
    )
{
    DRIVER_INITIALIZATION_DATA initializationData = {};
    initializationData.Version = DXGKDDI_INTERFACE_VERSION;

    initializationData.DxgkDdiAddDevice = Bc250DdiAddDevice;
    initializationData.DxgkDdiStartDevice = Bc250DdiStartDevice;
    initializationData.DxgkDdiStopDevice = Bc250DdiStopDevice;
    initializationData.DxgkDdiRemoveDevice = Bc250DdiRemoveDevice;
    initializationData.DxgkDdiDispatchIoRequest = Bc250DdiDispatchIoRequest;
    initializationData.DxgkDdiInterruptRoutine = Bc250DdiInterruptRoutine;
    initializationData.DxgkDdiDpcRoutine = Bc250DdiDpcRoutine;
    initializationData.DxgkDdiResetDevice = Bc250DdiResetDevice;
    initializationData.DxgkDdiUnload = Bc250DdiUnload;
    initializationData.DxgkDdiQueryAdapterInfo = Bc250DdiQueryAdapterInfo;
    initializationData.DxgkDdiCreateDevice = Bc250FullDdiCreateDevice;
    initializationData.DxgkDdiDestroyDevice = Bc250FullDdiDestroyDevice;
    initializationData.DxgkDdiCreateContext = Bc250FullDdiCreateContext;
    initializationData.DxgkDdiDestroyContext = Bc250FullDdiDestroyContext;
    initializationData.DxgkDdiCreateAllocation = Bc250FullDdiCreateAllocation;
    initializationData.DxgkDdiDestroyAllocation = Bc250FullDdiDestroyAllocation;
    initializationData.DxgkDdiDescribeAllocation = Bc250FullDdiDescribeAllocation;
    initializationData.DxgkDdiGetStandardAllocationDriverData = Bc250FullDdiGetStandardAllocationDriverData;
    initializationData.DxgkDdiOpenAllocation = Bc250FullDdiOpenAllocation;
    initializationData.DxgkDdiCloseAllocation = Bc250FullDdiCloseAllocation;
    initializationData.DxgkDdiRender = Bc250FullDdiRender;
    initializationData.DxgkDdiPatch = Bc250FullDdiPatch;
    initializationData.DxgkDdiBuildPagingBuffer = Bc250FullDdiBuildPagingBuffer;
    initializationData.DxgkDdiSubmitCommand = Bc250FullDdiSubmitCommand;
    initializationData.DxgkDdiPreemptCommand = Bc250FullDdiPreemptCommand;
    initializationData.DxgkDdiQueryCurrentFence = Bc250FullDdiQueryCurrentFence;
    initializationData.DxgkDdiResetFromTimeout = Bc250FullDdiResetFromTimeout;
    initializationData.DxgkDdiRestartFromTimeout = Bc250FullDdiRestartFromTimeout;
    initializationData.DxgkDdiSetVidPnSourceAddress = Bc250FullDdiSetVidPnSourceAddress;

    return DxgkInitialize(DriverObject, RegistryPath, &initializationData);
}
#endif

extern "C"
NTSTATUS
DriverEntry(
    _In_ DRIVER_OBJECT* DriverObject,
    _In_ UNICODE_STRING* RegistryPath
    )
{
    PAGED_CODE();

#if BC250_ENABLE_FULL_WDDM
    return Bc250InitializeFullWddm(DriverObject, RegistryPath);
#else
    KMDDOD_INITIALIZATION_DATA initializationData = {};
    initializationData.Version = DXGKDDI_INTERFACE_VERSION;

    initializationData.DxgkDdiAddDevice = Bc250DdiAddDevice;
    initializationData.DxgkDdiStartDevice = Bc250DdiStartDevice;
    initializationData.DxgkDdiStopDevice = Bc250DdiStopDevice;
    initializationData.DxgkDdiResetDevice = Bc250DdiResetDevice;
    initializationData.DxgkDdiRemoveDevice = Bc250DdiRemoveDevice;
    initializationData.DxgkDdiDispatchIoRequest = Bc250DdiDispatchIoRequest;
    initializationData.DxgkDdiInterruptRoutine = Bc250DdiInterruptRoutine;
    initializationData.DxgkDdiDpcRoutine = Bc250DdiDpcRoutine;
    initializationData.DxgkDdiQueryChildRelations = Bc250DdiQueryChildRelations;
    initializationData.DxgkDdiQueryChildStatus = Bc250DdiQueryChildStatus;
    initializationData.DxgkDdiQueryDeviceDescriptor = Bc250DdiQueryDeviceDescriptor;
    initializationData.DxgkDdiSetPowerState = Bc250DdiSetPowerState;
    initializationData.DxgkDdiUnload = Bc250DdiUnload;
    initializationData.DxgkDdiQueryAdapterInfo = Bc250DdiQueryAdapterInfo;
    initializationData.DxgkDdiSetPointerPosition = Bc250DdiSetPointerPosition;
    initializationData.DxgkDdiSetPointerShape = Bc250DdiSetPointerShape;
    initializationData.DxgkDdiIsSupportedVidPn = Bc250DdiIsSupportedVidPn;
    initializationData.DxgkDdiRecommendFunctionalVidPn = Bc250DdiRecommendFunctionalVidPn;
    initializationData.DxgkDdiEnumVidPnCofuncModality = Bc250DdiEnumVidPnCofuncModality;
    initializationData.DxgkDdiSetVidPnSourceVisibility = Bc250DdiSetVidPnSourceVisibility;
    initializationData.DxgkDdiCommitVidPn = Bc250DdiCommitVidPn;
    initializationData.DxgkDdiUpdateActiveVidPnPresentPath = Bc250DdiUpdateActiveVidPnPresentPath;
    initializationData.DxgkDdiRecommendMonitorModes = Bc250DdiRecommendMonitorModes;
    initializationData.DxgkDdiQueryVidPnHWCapability = Bc250DdiQueryVidPnHWCapability;
    initializationData.DxgkDdiPresentDisplayOnly = Bc250DdiPresentDisplayOnly;
    initializationData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Bc250DdiStopDeviceAndReleasePostDisplayOwnership;
    initializationData.DxgkDdiSystemDisplayEnable = Bc250DdiSystemDisplayEnable;
    initializationData.DxgkDdiSystemDisplayWrite = Bc250DdiSystemDisplayWrite;

    NTSTATUS status = DxgkInitializeDisplayOnlyDriver(
        DriverObject,
        RegistryPath,
        &initializationData);

    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "DxgkInitializeDisplayOnlyDriver falhou: 0x%08X",
                   status);
    }

    return status;
#endif
}

VOID
Bc250DdiUnload(
    VOID
    )
{
    PAGED_CODE();
    BC250_LOG0(DPFLTR_INFO_LEVEL, "Unload");
}

NTSTATUS
Bc250DdiAddDevice(
    _In_ DEVICE_OBJECT* PhysicalDeviceObject,
    _Outptr_ PVOID* DeviceContext
    )
{
    BC250_DEVICE_CONTEXT* context;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL || DeviceContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *DeviceContext = NULL;

#pragma warning(push)
#pragma warning(disable: 4996)
    context = static_cast<BC250_DEVICE_CONTEXT*>(
        ExAllocatePoolWithTag(NonPagedPoolNx,
                              sizeof(BC250_DEVICE_CONTEXT),
                              BC250_POOL_TAG));
#pragma warning(pop)

    if (context == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(context, sizeof(*context));
    context->PhysicalDeviceObject = PhysicalDeviceObject;
    *DeviceContext = context;

    BC250_LOG0(DPFLTR_INFO_LEVEL, "AddDevice: contexto criado");
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DdiRemoveDevice(
    _In_ PVOID DeviceContext
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);

    PAGED_CODE();

    if (context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250FreeGfxResources(&context->Gfx);
    Bc250UnmapMmioBars(&context->Hw);
    ExFreePoolWithTag(context, BC250_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DdiStartDevice(
    _In_ PVOID DeviceContext,
    _In_ DXGK_START_INFO* StartInfo,
    _In_ DXGKRNL_INTERFACE* DxgkInterface,
    _Out_ ULONG* NumberOfViews,
    _Out_ ULONG* NumberOfChildren
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);

    PAGED_CODE();

    if (context == NULL || StartInfo == NULL || DxgkInterface == NULL ||
        NumberOfViews == NULL || NumberOfChildren == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * O port driver fornece o PDO, o caminho do registro e a lista de
     * recursos PCI traduzidos. Não assumimos BARs fixos nem tamanho de VRAM.
     */
    RtlCopyMemory(&context->StartInfo, StartInfo, sizeof(*StartInfo));
    RtlCopyMemory(&context->DxgkInterface, DxgkInterface, sizeof(*DxgkInterface));
    RtlZeroMemory(&context->DeviceInfo, sizeof(context->DeviceInfo));

    NTSTATUS status = context->DxgkInterface.DxgkCbGetDeviceInformation(
        context->DxgkInterface.DeviceHandle,
        &context->DeviceInfo);

    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "DxgkCbGetDeviceInformation falhou: 0x%08X",
                   status);
        return status;
    }

    if (context->DeviceInfo.TranslatedResourceList == NULL) {
        BC250_LOG0(DPFLTR_ERROR_LEVEL,
                   "Nenhuma lista de recursos traduzidos foi fornecida");
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * Nesta fase a placa ainda não foi validada pelo ID PCI real. O parser é
     * executado para validar o contrato de recursos, mas o driver não expõe
     * aceleração até os IP blocks e o modelo UMA estarem implementados.
     */
    status = Bc250MapTranslatedResources(
        &context->Hw,
        context->DeviceInfo.TranslatedResourceList);

    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Mapeamento inicial de recursos falhou: 0x%08X",
                   status);
        return status;
    }

    BC250_LOG1(DPFLTR_INFO_LEVEL,
               "Recursos MMIO mapeados; quantidade de BARs: %lu",
               context->Hw.MappedBarCount);

    status = Bc250DiscoverRuntimeHardware(&context->Hw);
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Descoberta runtime Cyan Skillfish falhou: 0x%08X",
                   status);
        Bc250UnmapMmioBars(&context->Hw);
        return status;
    }

    /* Only the confirmed BAR5 path may access the SMN mailbox. */
    context->Hw.AllowSmnMailbox = TRUE;
    status = Bc250SmuInitializeQueries(&context->Hw, &context->Smu);
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_WARNING_LEVEL,
                   "Consultas SMU Cyan Skillfish falharam; continuando em modo experimental: 0x%08X",
                   status);
        RtlZeroMemory(&context->Smu, sizeof(context->Smu));
        status = STATUS_SUCCESS;
    }

    status = Bc250InitializeMemoryState(&context->Memory, &context->DeviceInfo);
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Inicializacao do modelo UMA falhou: 0x%08X",
                   status);
        Bc250UnmapMmioBars(&context->Hw);
        return status;
    }

    status = Bc250InitializeGfxState(&context->Gfx);
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Inicializacao do estado GFX falhou: 0x%08X",
                   status);
        Bc250UnmapMmioBars(&context->Hw);
        return status;
    }

    status = Bc250AllocateGfxResources(
        &context->Gfx,
        BC250_GFX_RING_DWORDS * sizeof(ULONG),
        sizeof(ULONG64));
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Alocacao de rings/fences falhou: 0x%08X",
                   status);
        Bc250FreeGfxResources(&context->Gfx);
        Bc250UnmapMmioBars(&context->Hw);
        return status;
    }

    status = Bc250InitializeGfxRings(&context->Gfx, TRUE);
    if (!NT_SUCCESS(status)) {
        BC250_LOG1(DPFLTR_ERROR_LEVEL,
                   "Preparacao de rings/fences falhou: 0x%08X",
                   status);
        Bc250FreeGfxResources(&context->Gfx);
        Bc250UnmapMmioBars(&context->Hw);
        return status;
    }

    /* The public blobs are packaged and validated separately, but this KMD
     * does not yet load CP/SDMA firmware through PSP. Do not claim that the
     * engines or their fences are live merely because ring memory exists. */
    context->Gfx.FirmwareLoaded = FALSE;
    context->Hw.FirmwareReady = FALSE;
    context->Hw.AllowRegisterWrites = FALSE;
    context->Hw.GfxReady = FALSE;
    context->Gfx.InterruptsEnabled = FALSE;

    BC250_LOG0(DPFLTR_WARNING_LEVEL,
               "Rings preparados; firmware CP/SDMA ainda nao foi carregado, aceleracao permanece pendente");

    *NumberOfViews = 0;
    *NumberOfChildren = 0;
    context->DeviceStarted = TRUE;
    context->Hw.IsStarted = TRUE;

    /* Keep translated BARs mapped while full-WDDM callbacks are active. */
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DdiStopDevice(
    _In_ PVOID DeviceContext
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);

    PAGED_CODE();

    if (context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bc250FreeGfxResources(&context->Gfx);
    Bc250UnmapMmioBars(&context->Hw);
    context->DeviceStarted = FALSE;
    context->Hw.IsStarted = FALSE;
    return STATUS_SUCCESS;
}

VOID
Bc250DdiResetDevice(
    _In_ PVOID DeviceContext
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);

    /* Callback executável em contexto não paginável. Ainda não toca no GFX. */
    if (context != NULL) {
        context->DeviceStarted = FALSE;
        BC250_LOG0(DPFLTR_WARNING_LEVEL, "ResetDevice: reset real ainda não implementado");
    }
}

NTSTATUS
Bc250DdiDispatchIoRequest(
    _In_ PVOID DeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ VIDEO_REQUEST_PACKET* VideoRequestPacket
    )
{
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(VideoRequestPacket);
    PAGED_CODE();
    return DeviceContext != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
Bc250DdiSetPowerState(
    _In_ PVOID DeviceContext,
    _In_ ULONG HardwareUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType
    )
{
    UNREFERENCED_PARAMETER(HardwareUid);
    UNREFERENCED_PARAMETER(ActionType);
    PAGED_CODE();

    if (DeviceContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (DevicePowerState != PowerDeviceD0) {
        /* D1/D2/D3 require a validated SMU/GFXOFF sequence that is not
         * implemented yet. Never claim that a transition succeeded. */
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DdiQueryChildRelations(
    _In_ PVOID DeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* ChildRelations,
    _In_ ULONG ChildRelationsSize
    )
{
    UNREFERENCED_PARAMETER(DeviceContext);

    PAGED_CODE();

    if (ChildRelations == NULL || ChildRelationsSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250DdiQueryChildStatus(
    _In_ PVOID DeviceContext,
    _Inout_ DXGK_CHILD_STATUS* ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly
    )
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    PAGED_CODE();

    if (ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(ChildStatus, sizeof(*ChildStatus));
    return STATUS_NO_MORE_ENTRIES;
}

NTSTATUS
Bc250DdiQueryDeviceDescriptor(
    _In_ PVOID DeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* DeviceDescriptor
    )
{
    UNREFERENCED_PARAMETER(ChildUid);
    PAGED_CODE();
    if (DeviceContext == NULL || DeviceDescriptor == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(DeviceDescriptor, sizeof(*DeviceDescriptor));
    return STATUS_SUCCESS;
}

BOOLEAN
Bc250DdiInterruptRoutine(
    _In_ PVOID DeviceContext,
    _In_ ULONG MessageNumber
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);
    ULONG interruptStatus;
    NTSTATUS status;

    if (context == NULL || !context->DeviceStarted ||
        !context->Gfx.InterruptsEnabled || !context->Hw.GfxReady ||
        !BC250_GFX_INTERRUPT_OFFSETS_VALIDATED) {
        return FALSE;
    }

    status = Bc250ReadRegister32(
        &context->Hw,
        BC250_GFX_INTERRUPT_STATUS_OFFSET,
        &interruptStatus);
    if (!NT_SUCCESS(status) || interruptStatus == 0) {
        return FALSE;
    }

    /* Acknowledge only an ASIC-validated status register. */
    status = Bc250WriteRegister32(
        &context->Hw,
        BC250_GFX_INTERRUPT_ACK_OFFSET,
        interruptStatus);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }

    context->LastInterruptMessage = MessageNumber;
    InterlockedOr((volatile LONG*)&context->PendingEngineMask, 0x0F);
    InterlockedExchange(&context->InterruptPending, 1);

    if (context->DxgkInterface.DxgkCbQueueDpc == NULL ||
        !context->DxgkInterface.DxgkCbQueueDpc(
            context->DxgkInterface.DeviceHandle)) {
        return TRUE;
    }

    return TRUE;
}

VOID
Bc250DdiDpcRoutine(
    _In_ PVOID DeviceContext
    )
{
    BC250_DEVICE_CONTEXT* context =
        static_cast<BC250_DEVICE_CONTEXT*>(DeviceContext);
    ULONG engineIndex;
    ULONG pendingMask;
    ULONG64 fenceValue;

    if (context == NULL || InterlockedExchange(&context->InterruptPending, 0) == 0) {
        return;
    }

    pendingMask = (ULONG)InterlockedExchange(
        (volatile LONG*)&context->PendingEngineMask,
        0);

    for (engineIndex = 0; engineIndex < context->Gfx.EngineCount; ++engineIndex) {
        if ((pendingMask & (1u << engineIndex)) == 0) {
            continue;
        }

        if (NT_SUCCESS(Bc250ReadCompletedFence(
                &context->Gfx,
                context->Gfx.Engines[engineIndex].Kind,
                &fenceValue))) {
            context->Gfx.Engines[engineIndex].Fence.LastCompleted = fenceValue;
        }
    }

    if (context->DxgkInterface.DxgkCbNotifyDpc != NULL) {
        context->DxgkInterface.DxgkCbNotifyDpc(
            context->DxgkInterface.DeviceHandle);
    }
}

NTSTATUS
APIENTRY
Bc250DdiQueryAdapterInfo(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* QueryAdapterInfo
    )
{
    UNREFERENCED_PARAMETER(Adapter);

    if (QueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (QueryAdapterInfo->Type == DXGKQAITYPE_QUERYSEGMENT) {
        if (QueryAdapterInfo->pInputData == NULL ||
            QueryAdapterInfo->InputDataSize < sizeof(DXGK_QUERYSEGMENTIN) ||
            QueryAdapterInfo->pOutputData == NULL ||
            QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT)) {
            return STATUS_INVALID_PARAMETER;
        }

        return Bc250QueryUmaSegments(
            static_cast<const DXGK_QUERYSEGMENTIN*>(QueryAdapterInfo->pInputData),
            static_cast<DXGK_QUERYSEGMENTOUT*>(QueryAdapterInfo->pOutputData),
            QueryAdapterInfo->OutputDataSize - sizeof(DXGK_QUERYSEGMENTOUT));
    }

    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
APIENTRY
Bc250DdiSetPointerPosition(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* PointerPosition
    )
{
    UNREFERENCED_PARAMETER(PointerPosition);
    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
APIENTRY
Bc250DdiSetPointerShape(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* PointerShape
    )
{
    UNREFERENCED_PARAMETER(PointerShape);
    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
APIENTRY
Bc250DdiPresentDisplayOnly(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* PresentDisplayOnly
    )
{
    UNREFERENCED_PARAMETER(PresentDisplayOnly);
    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

#define BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(_name, _type) \
NTSTATUS APIENTRY _name(_In_ CONST HANDLE Adapter, _In_ CONST _type* Argument) \
{ \
    UNREFERENCED_PARAMETER(Adapter); \
    UNREFERENCED_PARAMETER(Argument); \
    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER; \
}

#define BC250_UNSUPPORTED_ADAPTER_INOUT_CALLBACK(_name, _type) \
NTSTATUS APIENTRY _name(_In_ CONST HANDLE Adapter, _Inout_ _type* Argument) \
{ \
    UNREFERENCED_PARAMETER(Adapter); \
    UNREFERENCED_PARAMETER(Argument); \
    return Adapter != NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER; \
}

BC250_UNSUPPORTED_ADAPTER_INOUT_CALLBACK(Bc250DdiIsSupportedVidPn, DXGKARG_ISSUPPORTEDVIDPN)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiRecommendFunctionalVidPn, DXGKARG_RECOMMENDFUNCTIONALVIDPN)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiEnumVidPnCofuncModality, DXGKARG_ENUMVIDPNCOFUNCMODALITY)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiSetVidPnSourceVisibility, DXGKARG_SETVIDPNSOURCEVISIBILITY)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiCommitVidPn, DXGKARG_COMMITVIDPN)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiRecommendMonitorModes, DXGKARG_RECOMMENDMONITORMODES)
BC250_UNSUPPORTED_ADAPTER_INOUT_CALLBACK(Bc250DdiQueryVidPnHWCapability, DXGKARG_QUERYVIDPNHWCAPABILITY)
BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK(Bc250DdiUpdateActiveVidPnPresentPath, DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH)

#undef BC250_UNSUPPORTED_ADAPTER_CONST_CALLBACK
#undef BC250_UNSUPPORTED_ADAPTER_INOUT_CALLBACK

NTSTATUS
Bc250DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION* DisplayInfo
    )
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(TargetId);

    if (DisplayInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSystemDisplayEnable(
    _In_ PVOID DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat
    )
{
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);
    if (DeviceContext == NULL || Width == NULL || Height == NULL ||
        ColorFormat == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Width = 0;
    *Height = 0;
    *ColorFormat = D3DDDIFMT_A8R8G8B8;
    return STATUS_SUCCESS;
}

VOID
APIENTRY
Bc250DdiSystemDisplayWrite(
    _In_ PVOID DeviceContext,
    _In_ PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ INT PositionX,
    _In_ INT PositionY
    )
{
    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(SourceWidth);
    UNREFERENCED_PARAMETER(SourceHeight);
    UNREFERENCED_PARAMETER(SourceStride);
    UNREFERENCED_PARAMETER(PositionX);
    UNREFERENCED_PARAMETER(PositionY);
}
