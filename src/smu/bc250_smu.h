#pragma once

#include <ntddk.h>
#include "../hw/bc250_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_SMN_MP1_PUBLIC              0x03B00000u
#define BC250_SMN_MP1_SRAM                0x03C00004u
#define BC250_SMN_MP1_FIRMWARE_FLAGS      0x03B10024u
#define BC250_SMN_MP1_PUB_CTRL            0x03B10B14u
#define BC250_SMN_C2PMSG_66               0x03B10A08u
#define BC250_SMN_C2PMSG_82               0x03B10A48u
#define BC250_SMN_C2PMSG_90               0x03B10A68u

#define BC250_SMU_RESULT_OK                0x1u
#define BC250_SMU_RESULT_FAILED            0xFFu
#define BC250_SMU_RESULT_UNKNOWN_CMD       0xFEu
#define BC250_SMU_RESULT_REJECTED_PREREQ   0xFDu
#define BC250_SMU_RESULT_REJECTED_BUSY     0xFCu

#define BC250_SMU_MSG_TEST                  0x1u
#define BC250_SMU_MSG_GET_VERSION           0x2u
#define BC250_SMU_MSG_GET_DRIVER_IF        0x3u
#define BC250_SMU_MSG_REQUEST_GFXCLK       0xEu
#define BC250_SMU_MSG_QUERY_GFXCLK         0xFu
#define BC250_SMU_MSG_QUERY_ACTIVE_WGP     0x1Eu
#define BC250_SMU_MSG_GET_GFX_FREQUENCY    0x37u
#define BC250_SMU_MSG_GET_GFX_VID          0x38u
#define BC250_SMU_MSG_GET_ENABLED_FEATURES 0x3Du
#define BC250_SMU_MSG_UNFORCE_GFX_FREQ     0x3Au
#define BC250_SMU_MSG_UNFORCE_GFX_VID      0x3Cu

#define BC250_SMU_READY_VALUE              0x1u
#define BC250_SMU_TIMEOUT_US               1000000u
#define BC250_SMU_POLL_US                  100u

typedef struct _BC250_SMU_STATE {
    ULONG Version;
    ULONG DriverInterfaceVersion;
    ULONG EnabledFeatures;
    ULONG LastMessage;
    ULONG LastResponse;
    ULONG CurrentGfxFrequency;
    ULONG CurrentGfxVid;
    ULONG ActiveWgp;
    BOOLEAN Ready;
    BOOLEAN GfxOffObserved;
    BOOLEAN QueryOnlyMode;
} BC250_SMU_STATE;

NTSTATUS
Bc250SmnRead32(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG SmnAddress,
    _Out_ ULONG* Value
    );

NTSTATUS
Bc250SmnWrite32(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG SmnAddress,
    _In_ ULONG Value
    );

NTSTATUS
Bc250SmuWaitReady(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG TimeoutUs
    );

NTSTATUS
Bc250SmuSendQuery(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_SMU_STATE* SmuState,
    _In_ ULONG Message,
    _In_ ULONG Argument,
    _Out_ ULONG* Response
    );

NTSTATUS
Bc250SmuInitializeQueries(
    _Inout_ BC250_HW_STATE* HwState,
    _Out_ BC250_SMU_STATE* SmuState
    );

#ifdef __cplusplus
}
#endif
