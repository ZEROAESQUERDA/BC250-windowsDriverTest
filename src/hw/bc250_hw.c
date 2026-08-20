#include "bc250_hw.h"

static const USHORT g_Bc250DeviceIds[] = {
    BC250_DEVICE_ID_13DB,
    BC250_DEVICE_ID_13F9,
    BC250_DEVICE_ID_13FA,
    BC250_DEVICE_ID_13FB,
    BC250_DEVICE_ID_13FC,
    BC250_DEVICE_ID_13FE,
    BC250_DEVICE_ID_143F,
};

BOOLEAN
Bc250IsSupportedDevice(
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId
    )
{
    SIZE_T index;

    if (VendorId != BC250_VENDOR_ID) {
        return FALSE;
    }

    for (index = 0; index < RTL_NUMBER_OF(g_Bc250DeviceIds); ++index) {
        if (g_Bc250DeviceIds[index] == DeviceId) {
            return TRUE;
        }
    }

    return FALSE;
}

VOID
Bc250InitializeHwState(
    _Out_ BC250_HW_STATE* HwState,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _In_ UCHAR RevisionId
    )
{
    RtlZeroMemory(HwState, sizeof(*HwState));

    HwState->VendorId = VendorId;
    HwState->DeviceId = DeviceId;
    HwState->RevisionId = RevisionId;
    HwState->IsCyanSkillfish = Bc250IsSupportedDevice(VendorId, DeviceId);
    HwState->Variant = (DeviceId == BC250_DEVICE_ID_13FE ||
                        DeviceId == BC250_DEVICE_ID_143F)
        ? Bc250VariantCyanSkillfish2
        : (HwState->IsCyanSkillfish
            ? Bc250VariantCyanSkillfish
            : Bc250VariantUnknown);
    HwState->GfxIpVersion = (BC250_GFX_MAJOR << 16) |
                            (BC250_GFX_MINOR << 8) |
                            BC250_GFX_REVISION;
    HwState->SdmaIpVersion = (BC250_SDMA_MAJOR << 8) | BC250_SDMA_MINOR;
    HwState->SdmaInstances = 2;
}

NTSTATUS
Bc250MapMmioBar(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG BarIndex,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Length
    )
{
    PVOID virtualAddress;

    if (HwState == NULL || BarIndex >= BC250_MAX_BARS || Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (HwState->Bars[BarIndex].Mapped) {
        return STATUS_DEVICE_BUSY;
    }

    virtualAddress = MmMapIoSpaceEx(
        PhysicalAddress,
        Length,
        PAGE_READWRITE | PAGE_NOCACHE);

    if (virtualAddress == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    HwState->Bars[BarIndex].PhysicalAddress = PhysicalAddress;
    HwState->Bars[BarIndex].Length = Length;
    HwState->Bars[BarIndex].VirtualAddress = virtualAddress;
    HwState->Bars[BarIndex].Mapped = TRUE;
    ++HwState->MappedBarCount;

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250MapTranslatedResources(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ PCM_RESOURCE_LIST ResourceList
    )
{
    ULONG resourceIndex;
    ULONG barIndex = 0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR resource;
    PCM_PARTIAL_RESOURCE_LIST partialList;

    if (HwState == NULL || ResourceList == NULL || ResourceList->Count == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    partialList = &ResourceList->List[0].PartialResourceList;

    for (resourceIndex = 0;
         resourceIndex < partialList->Count && barIndex < BC250_MAX_BARS;
         ++resourceIndex) {
        resource = &partialList->PartialDescriptors[resourceIndex];

        if (resource->Type != CmResourceTypeMemory ||
            resource->u.Memory.Length == 0) {
            continue;
        }

        NTSTATUS status = Bc250MapMmioBar(
            HwState,
            barIndex,
            resource->u.Memory.Start,
            resource->u.Memory.Length);

        if (!NT_SUCCESS(status)) {
            Bc250UnmapMmioBars(HwState);
            return status;
        }

        ++barIndex;
    }

    return barIndex != 0 ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

VOID
Bc250UnmapMmioBars(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    ULONG index;

    if (HwState == NULL) {
        return;
    }

    for (index = 0; index < BC250_MAX_BARS; ++index) {
        if (HwState->Bars[index].Mapped) {
            MmUnmapIoSpace(
                HwState->Bars[index].VirtualAddress,
                HwState->Bars[index].Length);
            RtlZeroMemory(&HwState->Bars[index], sizeof(HwState->Bars[index]));
            if (HwState->MappedBarCount != 0) {
                --HwState->MappedBarCount;
            }
        }
    }
}


NTSTATUS
Bc250DiscoverRuntimeHardware(
    _Inout_ BC250_HW_STATE* HwState
    )
{
    ULONG index;
    ULONG candidateCount = 0;

    if (HwState == NULL || !HwState->IsCyanSkillfish) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    /*
     * BAR ordering is not assumed. The first sufficiently large memory BAR is
     * retained as a candidate register aperture; BAR5/PSP offsets are only
     * used after the runtime resource parser confirms their range.
     */
    HwState->RegisterBarIndex = MAXULONG;
    HwState->SmnBarIndex = MAXULONG;
    HwState->PspBarIndex = MAXULONG;
    for (index = 0; index < BC250_MAX_BARS; ++index) {
        ULONGLONG physicalAddress;

        if (!HwState->Bars[index].Mapped) {
            continue;
        }

        physicalAddress = (ULONGLONG)HwState->Bars[index].PhysicalAddress.QuadPart;
        if (physicalAddress == BC250_EXPECTED_BAR5_PHYSICAL &&
            HwState->Bars[index].Length >= BC250_EXPECTED_BAR5_LENGTH) {
            HwState->SmnBarIndex = index;
        }

        if (HwState->Bars[index].Length >= 0x100000u &&
            physicalAddress != BC250_EXPECTED_BAR5_PHYSICAL &&
            HwState->RegisterBarIndex == MAXULONG) {
            HwState->RegisterBarIndex = index;
        }

        if (HwState->Bars[index].Length >= 0x60000u &&
            HwState->PspBarIndex == MAXULONG) {
            HwState->PspBarIndex = index;
        }

        ++candidateCount;
    }

    /*
     * The resource parser cannot infer the original PCI BAR number from a
     * generic CM_RESOURCE_LIST. Refuse SMN access unless the known BAR5
     * physical range is confirmed, avoiding writes into an arbitrary BAR.
     */
    if (candidateCount == 0 ||
        HwState->SmnBarIndex == MAXULONG ||
        HwState->RegisterBarIndex == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }

    HwState->RuntimeDiscovered = TRUE;
    HwState->FirmwareReady = FALSE;
    HwState->PspReady = FALSE;
    HwState->SmuReady = FALSE;
    HwState->GfxReady = FALSE;
    HwState->VmReady = FALSE;
    HwState->AllowRegisterWrites = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250ReadRegister32(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
    )
{
    volatile ULONG* registerAddress;

    if (HwState == NULL || Value == NULL ||
        HwState->RegisterBarIndex >= BC250_MAX_BARS ||
        !HwState->Bars[HwState->RegisterBarIndex].Mapped ||
        HwState->Bars[HwState->RegisterBarIndex].Length < sizeof(ULONG) ||
        Offset > HwState->Bars[HwState->RegisterBarIndex].Length - sizeof(ULONG)) {
        return STATUS_INVALID_PARAMETER;
    }

    registerAddress = (volatile ULONG*)((PUCHAR)
        HwState->Bars[HwState->RegisterBarIndex].VirtualAddress + Offset);
    *Value = READ_REGISTER_ULONG(registerAddress);
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250WriteRegister32(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    volatile ULONG* registerAddress;

    if (HwState == NULL ||
        HwState->RegisterBarIndex >= BC250_MAX_BARS ||
        !HwState->Bars[HwState->RegisterBarIndex].Mapped ||
        HwState->Bars[HwState->RegisterBarIndex].Length < sizeof(ULONG) ||
        Offset > HwState->Bars[HwState->RegisterBarIndex].Length - sizeof(ULONG)) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!HwState->AllowRegisterWrites) {
        return STATUS_DEVICE_NOT_READY;
    }

    registerAddress = (volatile ULONG*)((PUCHAR)
        HwState->Bars[HwState->RegisterBarIndex].VirtualAddress + Offset);
    WRITE_REGISTER_ULONG(registerAddress, Value);
    return STATUS_SUCCESS;
}
