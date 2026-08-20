#ifndef BC250_PSP_H
#define BC250_PSP_H

#include <ntddk.h>
#include "../hw/bc250_hw.h"
#include "bc250_firmware.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BC250_PSP_RING_VALIDATED
#define BC250_PSP_RING_VALIDATED 0
#endif

#ifndef BC250_PSP_HDP_OFFSETS_VALIDATED
#define BC250_PSP_HDP_OFFSETS_VALIDATED 0
#endif

/*
 * These byte offsets come from the verified PSP KM/GPCOM probe in the
 * external BC-250 research repository. They are deliberately gated: the
 * existence of a candidate offset must not enable writes during StartDevice.
 */
#define BC250_PSP_MP0_BASE                 0x00058000u
#define BC250_PSP_C2PMSG_33                0x00058184u
#define BC250_PSP_C2PMSG_35                0x0005818Cu
#define BC250_PSP_C2PMSG_36                0x00058190u
#define BC250_PSP_C2PMSG_37                0x00058194u
#define BC250_PSP_C2PMSG_64                0x00058200u
#define BC250_PSP_C2PMSG_67                0x0005820Cu
#define BC250_PSP_C2PMSG_69                0x00058214u
#define BC250_PSP_C2PMSG_70                0x00058218u
#define BC250_PSP_C2PMSG_71                0x0005821Cu
#define BC250_PSP_C2PMSG_81                0x00058244u
#define BC250_PSP_C2PMSG_101               0x00058294u
#define BC250_PSP_C2PMSG_TOS_READY         0x80000000u

/* Observed hardware values only; never used as universal constants. */
#define BC250_PSP_OBSERVED_TMR_GPU_ADDRESS 0xF40F800000ULL
#define BC250_PSP_OBSERVED_TMR_APER_OFFSET 0x00F80000ULL

/* HDP offsets are kept separate from the PSP mailbox offsets. */
#define BC250_PSP_HDP_FLUSH_REGISTER       0x000012A0u
#define BC250_PSP_HDP_DEBUG0_REGISTER      0x000012B0u
#define BC250_PSP_HDP_FLUSH_CACHE          0x00000001u
#define BC250_PSP_HDP_INVALIDATE_CACHE     0x00000001u

#define BC250_PSP_RING_SIZE                0x00001000u
#define BC250_PSP_RING_TYPE_KM             2u
#define BC250_PSP_COMMAND_BUFFER_SIZE      0x00001000u
#define BC250_PSP_COMMAND_BUFFER_BYTES     0x00000400u
#define BC250_PSP_RING_FRAME_BYTES         64u
#define BC250_PSP_RING_FRAME_DWORDS        16u
#define BC250_PSP_RESPONSE_OFFSET          864u
#define BC250_PSP_MAX_COMMAND_DATA        512u
#define BC250_PSP_TIMEOUT_MS               500u
#define BC250_PSP_TMR_DEFAULT_SIZE         0x00400000u
#define BC250_PSP_TMR_MIN_SIZE             0x00001000u
#define BC250_PSP_TMR_MAX_SIZE             (64u * 1024u * 1024u)

#define BC250_PSP_RESPONSE_SUCCESS         0x00000000u
#define BC250_PSP_RESPONSE_UNKNOWN_COMMAND 0x00000100u
#define BC250_PSP_COMMAND_SETUP_TMR        0x05u
#define BC250_PSP_COMMAND_LOAD_IP_FW       0x06u
#define BC250_PSP_COMMAND_GET_FW_ATTESTATION 0x0Fu

#define BC250_PSP_FW_TYPE_ME               1u
#define BC250_PSP_FW_TYPE_PFP              2u
#define BC250_PSP_FW_TYPE_CE               3u
#define BC250_PSP_FW_TYPE_MEC              4u
#define BC250_PSP_FW_TYPE_RLC              8u
#define BC250_PSP_FW_TYPE_SDMA             9u
#define BC250_PSP_FW_TYPE_SDMA1            10u
#define BC250_PSP_FW_TYPE_SMU              18u

typedef struct _BC250_PSP_COMMAND_RESULT {
    ULONG Result;
    ULONG FenceStatus;
    ULONG ResponseStatus;
    ULONG ResponseFirmwareAddressLow;
    ULONG ResponseFirmwareAddressHigh;
    ULONG ResponseTmrSize;
    NTSTATUS TransportStatus;
} BC250_PSP_COMMAND_RESULT;

typedef struct _BC250_PSP_STATE {
    FAST_MUTEX Mutex;
    PVOID RingVirtualAddress;
    PHYSICAL_ADDRESS RingPhysicalAddress;
    ULONG RingSize;
    ULONG RingWritePointer;
    PVOID CommandVirtualAddress;
    PHYSICAL_ADDRESS CommandPhysicalAddress;
    PVOID FenceVirtualAddress;
    PHYSICAL_ADDRESS FencePhysicalAddress;
    ULONG FenceValue;
    PVOID DeferredFirmwareVirtualAddress;
    PHYSICAL_ADDRESS DeferredFirmwarePhysicalAddress;
    SIZE_T DeferredFirmwareSize;
    ULONG LastCommandId;
    ULONG LastResponseStatus;
    NTSTATUS LastTransportStatus;
    BOOLEAN RingCreated;
    BOOLEAN SubmissionFaulted;
    BOOLEAN TmrConfigured;
    BOOLEAN Initialized;
} BC250_PSP_STATE;

VOID
Bc250PspInitializeState(
    _Out_ BC250_PSP_STATE* PspState
    );

VOID
Bc250PspReleaseState(
    _Inout_ BC250_PSP_STATE* PspState
    );

NTSTATUS
Bc250PspCreateRing(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState
    );

NTSTATUS
Bc250PspAttest(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    );

NTSTATUS
Bc250PspSubmit(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _In_ ULONG CommandId,
    _In_reads_bytes_opt_(CommandDataSize) const UCHAR* CommandData,
    _In_ ULONG CommandDataSize,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    );

NTSTATUS
Bc250PspLoadFirmware(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _Inout_ BC250_FIRMWARE_STATE* FirmwareState,
    _In_ const BC250_FIRMWARE_IMAGE* Image
    );

NTSTATUS
Bc250PspSetupTmr(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_PSP_STATE* PspState,
    _In_ PHYSICAL_ADDRESS GpuMemoryAddress,
    _In_ PHYSICAL_ADDRESS SystemPhysicalAddress,
    _In_ ULONG TmrSize,
    _Out_ BC250_PSP_COMMAND_RESULT* Result
    );

#ifdef __cplusplus
}
#endif

#endif
