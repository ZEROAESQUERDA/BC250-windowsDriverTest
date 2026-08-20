#pragma once

#include <ntddk.h>
#include "../hw/bc250_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _BC250_FIRMWARE_KIND {
    Bc250FirmwareGpuInfo = 0,
    Bc250FirmwareIpDiscovery,
    Bc250FirmwarePsp,
    Bc250FirmwareSmu,
    Bc250FirmwareCe,
    Bc250FirmwarePfp,
    Bc250FirmwareMe,
    Bc250FirmwareMec,
    Bc250FirmwareMec2,
    Bc250FirmwareRlc,
    Bc250FirmwareSdma,
    Bc250FirmwareSdma1,
    Bc250FirmwareCount,
} BC250_FIRMWARE_KIND;

typedef struct _BC250_FIRMWARE_IMAGE {
    BC250_FIRMWARE_KIND Kind;
    const UCHAR* Data;
    SIZE_T Size;
    ULONG Version;
    BOOLEAN Required;
} BC250_FIRMWARE_IMAGE;

typedef struct _BC250_FIRMWARE_STATE {
    ULONG PresentMask;
    ULONG ValidMask;
    ULONG RequiredMask;
    ULONG LoadedMask;
    ULONG ImageCount;
    BOOLEAN DiscoveryValid;
    BOOLEAN VariantMatches;
    BOOLEAN AllRequiredPresent;
    BOOLEAN AllRequiredValid;
} BC250_FIRMWARE_STATE;

PCWSTR
Bc250FirmwareGetName(
    _In_ const BC250_HW_STATE* HwState,
    _In_ BC250_FIRMWARE_KIND Kind
    );

NTSTATUS
Bc250FirmwareValidateImage(
    _In_ const BC250_FIRMWARE_IMAGE* Image
    );

NTSTATUS
Bc250FirmwareValidateSet(
    _In_ const BC250_HW_STATE* HwState,
    _In_reads_(ImageCount) const BC250_FIRMWARE_IMAGE* Images,
    _In_ ULONG ImageCount,
    _Out_ BC250_FIRMWARE_STATE* FirmwareState
    );

NTSTATUS
Bc250FirmwareMarkLoaded(
    _Inout_ BC250_FIRMWARE_STATE* FirmwareState,
    _In_ BC250_FIRMWARE_KIND Kind
    );

NTSTATUS
Bc250FirmwareCommitReady(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ const BC250_FIRMWARE_STATE* FirmwareState
    );

#ifdef __cplusplus
}
#endif
