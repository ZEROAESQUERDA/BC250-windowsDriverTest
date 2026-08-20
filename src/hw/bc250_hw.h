#pragma once

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_VENDOR_ID 0x1002u
#define BC250_POOL_TAG  '052B'

/*
 * IDs Cyan Skillfish observados no amdgpu upstream.
 * O ID efetivo da placa deve ser confirmado no hardware antes da instalação.
 */
#define BC250_DEVICE_ID_13DB 0x13DBu
#define BC250_DEVICE_ID_13F9 0x13F9u
#define BC250_DEVICE_ID_13FA 0x13FAu
#define BC250_DEVICE_ID_13FB 0x13FBu
#define BC250_DEVICE_ID_13FC 0x13FCu
#define BC250_DEVICE_ID_13FE 0x13FEu
#define BC250_DEVICE_ID_143F 0x143Fu

#define BC250_GFX_MAJOR 10u
#define BC250_GFX_MINOR 1u
#define BC250_GFX_REVISION 3u
#define BC250_SDMA_MAJOR 5u
#define BC250_SDMA_MINOR 0u

#define BC250_MAX_BARS 6u

/* Valores públicos de referência; nunca substituem recursos PnP traduzidos. */
#define BC250_EXPECTED_BAR5_PHYSICAL 0xFE800000ULL
#define BC250_EXPECTED_BAR5_LENGTH   0x00080000ULL
#define BC250_NBIO_SMN_INDEX_OFFSET  0x00000038u
#define BC250_NBIO_SMN_DATA_OFFSET   0x0000003Cu
#define BC250_PSP_C2PMSG_BASE_OFFSET 0x00058000u
#define BC250_PSP_C2PMSG_64_OFFSET   0x00058200u
#define BC250_PSP_C2PMSG_67_OFFSET   0x0005820Cu
#define BC250_PSP_C2PMSG_69_OFFSET   0x00058214u
#define BC250_PSP_C2PMSG_70_OFFSET   0x00058218u
#define BC250_PSP_C2PMSG_71_OFFSET   0x0005821Cu
#define BC250_PSP_C2PMSG_81_OFFSET   0x00058244u

typedef enum _BC250_HW_VARIANT {
    Bc250VariantUnknown = 0,
    Bc250VariantCyanSkillfish,
    Bc250VariantCyanSkillfish2,
} BC250_HW_VARIANT;

typedef struct _BC250_PCI_ID {
    USHORT VendorId;
    USHORT DeviceId;
} BC250_PCI_ID;

typedef struct _BC250_MMIO_BAR {
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T Length;
    PVOID VirtualAddress;
    BOOLEAN Mapped;
} BC250_MMIO_BAR;

typedef struct _BC250_HW_STATE {
    USHORT VendorId;
    USHORT DeviceId;
    UCHAR RevisionId;
    BOOLEAN IsCyanSkillfish;
    BOOLEAN IsStarted;
    BC250_HW_VARIANT Variant;
    ULONG GfxIpVersion;
    ULONG SdmaIpVersion;
    ULONG SdmaInstances;
    ULONG MappedBarCount;
    ULONG RegisterBarIndex;
    ULONG SmnBarIndex;
    ULONG PspBarIndex;
    BOOLEAN RuntimeDiscovered;
    BOOLEAN FirmwareReady;
    BOOLEAN PspReady;
    BOOLEAN SmuReady;
    BOOLEAN GfxReady;
    BOOLEAN VmReady;
    BOOLEAN AllowRegisterWrites;
    BOOLEAN AllowSmnMailbox;
    BC250_MMIO_BAR Bars[BC250_MAX_BARS];
} BC250_HW_STATE;

BOOLEAN
Bc250IsSupportedDevice(
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId
    );

VOID
Bc250InitializeHwState(
    _Out_ BC250_HW_STATE* HwState,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _In_ UCHAR RevisionId
    );

NTSTATUS
Bc250MapMmioBar(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG BarIndex,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T Length
    );

NTSTATUS
Bc250MapTranslatedResources(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ PCM_RESOURCE_LIST ResourceList
    );

VOID
Bc250UnmapMmioBars(
    _Inout_ BC250_HW_STATE* HwState
    );

NTSTATUS
Bc250DiscoverRuntimeHardware(
    _Inout_ BC250_HW_STATE* HwState
    );

NTSTATUS
Bc250ReadRegister32(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
    );

NTSTATUS
Bc250WriteRegister32(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG Offset,
    _In_ ULONG Value
    );

#ifdef __cplusplus
}
#endif
