#include <dispmprt.h>

static SIZE_T
Bc250AlignDownSize(
    _In_ SIZE_T Value,
    _In_ SIZE_T Alignment
    )
{
    if (Alignment == 0) {
        return Value;
    }

    return Value - (Value % Alignment);
}

static SIZE_T
Bc250MinSize(
    _In_ SIZE_T Left,
    _In_ SIZE_T Right
    )
{
    return Left < Right ? Left : Right;
}

NTSTATUS
Bc250BuildUmaProfile(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const BC250_UMA_DISCOVERY_INPUT* Input
    )
{
    SIZE_T pageSize;
    SIZE_T systemBytes;
    SIZE_T reservedBytes = 0;
    SIZE_T visibleBytes = 0;
    SIZE_T apertureBytes;
    SIZE_T maxGpuBytes;
    ULONG segmentIndex = 0;

    if (MemoryState == NULL || Input == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(MemoryState, sizeof(*MemoryState));
    MemoryState->Uma = TRUE;
    MemoryState->GpuVaBits = 48;

    pageSize = (Input->PageSize == 65536u || Input->PageSize == 4096u)
        ? Input->PageSize
        : 4096u;
    MemoryState->PageSize = (ULONG)pageSize;

    if (!Input->SystemRamKnown || Input->SystemRamBytes == 0) {
        MemoryState->Conservative = TRUE;
        return STATUS_DEVICE_NOT_READY;
    }

    systemBytes = Bc250AlignDownSize(Input->SystemRamBytes, pageSize);
    MemoryState->TotalSystemBytes = systemBytes;

    if (Input->FirmwareReservationKnown &&
        Input->FirmwareReservedBytes != 0 &&
        Input->FirmwareReservedBytes <= systemBytes) {
        reservedBytes = Bc250AlignDownSize(Input->FirmwareReservedBytes, pageSize);
    }
    MemoryState->FirmwareReservedBytes = reservedBytes;

    /*
     * A local UMA-like segment is advertised only if the firmware/runtime
     * supplied an actual GPU-visible range. We never convert the product's
     * nominal 16 GB into a guessed VRAM segment.
     */
    if (Input->GpuVisibleRangeKnown && Input->GpuVisibleBytes != 0) {
        visibleBytes = Bc250AlignDownSize(Input->GpuVisibleBytes, pageSize);
        maxGpuBytes = systemBytes;
        if (reservedBytes != 0) {
            maxGpuBytes = Bc250MinSize(maxGpuBytes, reservedBytes);
        }
        visibleBytes = Bc250MinSize(visibleBytes, maxGpuBytes);
    }

    if (visibleBytes != 0 && segmentIndex < RTL_NUMBER_OF(MemoryState->Segments)) {
        MemoryState->Segments[segmentIndex].Kind = Bc250MemoryLocalUma;
        MemoryState->Segments[segmentIndex].Source =
            Input->FirmwareReservationKnown
                ? Bc250MemorySourceFirmware
                : Bc250MemorySourceRuntimeQuery;
        MemoryState->Segments[segmentIndex].BaseAddress.QuadPart =
            (LONGLONG)Input->GpuVisibleBase;
        MemoryState->Segments[segmentIndex].Size = visibleBytes;
        MemoryState->Segments[segmentIndex].CpuVisibleSize =
            Input->GpuVisibleCpuAccessible ? visibleBytes : 0;
        MemoryState->Segments[segmentIndex].CpuVisible =
            Input->GpuVisibleCpuAccessible;
        MemoryState->Segments[segmentIndex].GpuVisible = TRUE;
        MemoryState->Segments[segmentIndex].IsAperture = FALSE;
        ++segmentIndex;
    }

    /*
     * WDDM's aperture segment represents discontinuous system pages exposed
     * contiguously through the GPU page tables. If the platform supplies a
     * CPU host aperture, prefer it; otherwise use the discovered aperture.
     */
    apertureBytes = Input->CpuHostApertureBytes != 0
        ? Input->CpuHostApertureBytes
        : Input->ApertureBytes;
    apertureBytes = Bc250AlignDownSize(apertureBytes, pageSize);
    if (apertureBytes == 0) {
        MemoryState->Conservative = TRUE;
        return STATUS_DEVICE_NOT_READY;
    }

    if (segmentIndex >= RTL_NUMBER_OF(MemoryState->Segments)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    MemoryState->Segments[segmentIndex].Kind = Bc250MemoryGart;
    MemoryState->Segments[segmentIndex].Source = Bc250MemorySourceRuntimeQuery;
    MemoryState->Segments[segmentIndex].BaseAddress.QuadPart = 0;
    MemoryState->Segments[segmentIndex].Size = apertureBytes;
    MemoryState->Segments[segmentIndex].CpuVisibleSize = 0;
    MemoryState->Segments[segmentIndex].CpuVisible = FALSE;
    MemoryState->Segments[segmentIndex].GpuVisible = TRUE;
    MemoryState->Segments[segmentIndex].IsAperture = TRUE;
    ++segmentIndex;

    MemoryState->SegmentCount = segmentIndex;
    MemoryState->ApertureBytes = apertureBytes;
    MemoryState->EffectiveGpuBytes = visibleBytes != 0
        ? visibleBytes
        : apertureBytes;
    MemoryState->Conservative = !(Input->FirmwareReservationKnown &&
                                  Input->GpuVisibleRangeKnown);
    MemoryState->ReadyForVidMm = TRUE;

    UNREFERENCED_PARAMETER(Input->FirmwareReservedBase);
    UNREFERENCED_PARAMETER(Input->FirmwareReservedByBios);
    UNREFERENCED_PARAMETER(Input->IsSkillfish2);

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250InitializeMemoryState(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const DXGK_DEVICE_INFO* DeviceInfo
    )
{
    BC250_UMA_DISCOVERY_INPUT input;

    if (MemoryState == NULL || DeviceInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&input, sizeof(input));
    input.SystemRamKnown = TRUE;
    input.SystemRamBytes = (SIZE_T)DeviceInfo->SystemMemorySize.QuadPart;
    input.PageSize = 4096u;

    /*
     * DeviceInfo alone does not reveal the BC-250 firmware carve-out or the
     * real GPU-visible range. Start with an aperture-only profile; a later
     * IP-discovery/firmware query can fill the optional fields before VidMm.
     */
    input.ApertureBytes = input.SystemRamBytes;
    input.FirmwareReservationKnown = FALSE;
    input.GpuVisibleRangeKnown = FALSE;
    input.GpuVisibleCpuAccessible = FALSE;

    return Bc250BuildUmaProfile(MemoryState, &input);
}
