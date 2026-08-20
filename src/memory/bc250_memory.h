#pragma once

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _BC250_MEMORY_SEGMENT_KIND {
    Bc250MemorySystem = 0,
    Bc250MemoryLocalUma,
    Bc250MemoryGart,
} BC250_MEMORY_SEGMENT_KIND;

typedef enum _BC250_MEMORY_SOURCE {
    Bc250MemorySourceUnknown = 0,
    Bc250MemorySourceFirmware,
    Bc250MemorySourcePciResource,
    Bc250MemorySourceRuntimeQuery,
} BC250_MEMORY_SOURCE;

/*
 * Inputs collected by the KMD before QueryAdapterInfo. None of these values
 * may be guessed from the product name. Unknown values remain unknown and
 * cause the profile to fall back to the aperture-only UMA model.
 */
typedef struct _BC250_UMA_DISCOVERY_INPUT {
    SIZE_T SystemRamBytes;
    SIZE_T FirmwareReservedBytes;
    SIZE_T FirmwareReservedBase;
    SIZE_T GpuVisibleBytes;
    SIZE_T GpuVisibleBase;
    SIZE_T ApertureBytes;
    SIZE_T CpuHostApertureBytes;
    ULONG PageSize;
    BOOLEAN SystemRamKnown;
    BOOLEAN FirmwareReservationKnown;
    BOOLEAN GpuVisibleRangeKnown;
    BOOLEAN GpuVisibleCpuAccessible;
    BOOLEAN FirmwareReservedByBios;
    BOOLEAN IsSkillfish2;
} BC250_UMA_DISCOVERY_INPUT;

typedef struct _BC250_MEMORY_SEGMENT {
    BC250_MEMORY_SEGMENT_KIND Kind;
    BC250_MEMORY_SOURCE Source;
    PHYSICAL_ADDRESS BaseAddress;
    SIZE_T Size;
    SIZE_T CpuVisibleSize;
    BOOLEAN CpuVisible;
    BOOLEAN GpuVisible;
    BOOLEAN IsAperture;
} BC250_MEMORY_SEGMENT;

typedef struct _BC250_MEMORY_STATE {
    BC250_MEMORY_SEGMENT Segments[3];
    ULONG SegmentCount;
    ULONG_PTR GpuVaBits;
    ULONG PageSize;
    SIZE_T TotalSystemBytes;
    SIZE_T EffectiveGpuBytes;
    SIZE_T FirmwareReservedBytes;
    SIZE_T ApertureBytes;
    BOOLEAN Uma;
    BOOLEAN Conservative;
    BOOLEAN ReadyForVidMm;
} BC250_MEMORY_STATE;

struct _DXGK_DEVICE_INFO;

NTSTATUS
Bc250InitializeMemoryState(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const struct _DXGK_DEVICE_INFO* DeviceInfo
    );

NTSTATUS
Bc250BuildUmaProfile(
    _Out_ BC250_MEMORY_STATE* MemoryState,
    _In_ const BC250_UMA_DISCOVERY_INPUT* Input
    );

#ifdef __cplusplus
}
#endif
