#include "bc250_smu.h"

static BOOLEAN
Bc250SmuMessageIsSafeQuery(
    _In_ ULONG Message
    )
{
    switch (Message) {
    case BC250_SMU_MSG_TEST:
    case BC250_SMU_MSG_GET_VERSION:
    case BC250_SMU_MSG_GET_DRIVER_IF:
    case BC250_SMU_MSG_QUERY_GFXCLK:
    case BC250_SMU_MSG_QUERY_ACTIVE_WGP:
    case BC250_SMU_MSG_GET_GFX_FREQUENCY:
    case BC250_SMU_MSG_GET_GFX_VID:
    case BC250_SMU_MSG_GET_ENABLED_FEATURES:
    case BC250_SMU_MSG_UNFORCE_GFX_FREQ:
    case BC250_SMU_MSG_UNFORCE_GFX_VID:
        return TRUE;
    default:
        return FALSE;
    }
}

static NTSTATUS
Bc250SmnGetBar(
    _In_ const BC250_HW_STATE* HwState,
    _Out_ const BC250_MMIO_BAR** Bar
    )
{
    if (HwState == NULL || Bar == NULL || !HwState->AllowSmnMailbox ||
        HwState->SmnBarIndex >= BC250_MAX_BARS ||
        !HwState->Bars[HwState->SmnBarIndex].Mapped) {
        return STATUS_DEVICE_NOT_READY;
    }

    *Bar = &HwState->Bars[HwState->SmnBarIndex];
    if ((*Bar)->Length <= BC250_NBIO_SMN_DATA_OFFSET + sizeof(ULONG)) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250SmnRead32(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG SmnAddress,
    _Out_ ULONG* Value
    )
{
    const BC250_MMIO_BAR* bar;
    volatile ULONG* indexRegister;
    volatile ULONG* dataRegister;
    NTSTATUS status;

    if (Value == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = Bc250SmnGetBar(HwState, &bar);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    indexRegister = (volatile ULONG*)((PUCHAR)bar->VirtualAddress +
                                      BC250_NBIO_SMN_INDEX_OFFSET);
    dataRegister = (volatile ULONG*)((PUCHAR)bar->VirtualAddress +
                                     BC250_NBIO_SMN_DATA_OFFSET);

    WRITE_REGISTER_ULONG(indexRegister, SmnAddress);
    KeMemoryBarrier();
    *Value = READ_REGISTER_ULONG(dataRegister);
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250SmnWrite32(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ ULONG SmnAddress,
    _In_ ULONG Value
    )
{
    const BC250_MMIO_BAR* bar;
    volatile ULONG* indexRegister;
    volatile ULONG* dataRegister;
    NTSTATUS status;

    status = Bc250SmnGetBar(HwState, &bar);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    indexRegister = (volatile ULONG*)((PUCHAR)bar->VirtualAddress +
                                      BC250_NBIO_SMN_INDEX_OFFSET);
    dataRegister = (volatile ULONG*)((PUCHAR)bar->VirtualAddress +
                                     BC250_NBIO_SMN_DATA_OFFSET);

    WRITE_REGISTER_ULONG(indexRegister, SmnAddress);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(dataRegister, Value);
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
Bc250SmuWaitReady(
    _In_ const BC250_HW_STATE* HwState,
    _In_ ULONG TimeoutUs
    )
{
    ULONG elapsed = 0;
    ULONG value = 0;
    NTSTATUS status;

    if (TimeoutUs == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    while (elapsed < TimeoutUs) {
        status = Bc250SmnRead32(HwState, BC250_SMN_C2PMSG_90, &value);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (value == BC250_SMU_READY_VALUE) {
            return STATUS_SUCCESS;
        }

        KeStallExecutionProcessor(BC250_SMU_POLL_US);
        elapsed += BC250_SMU_POLL_US;
    }

    return STATUS_IO_TIMEOUT;
}

NTSTATUS
Bc250SmuSendQuery(
    _Inout_ BC250_HW_STATE* HwState,
    _Inout_ BC250_SMU_STATE* SmuState,
    _In_ ULONG Message,
    _In_ ULONG Argument,
    _Out_ ULONG* Response
    )
{
    NTSTATUS status;
    ULONG response;

    if (HwState == NULL || SmuState == NULL || Response == NULL ||
        !Bc250SmuMessageIsSafeQuery(Message)) {
        return STATUS_INVALID_PARAMETER;
    }

    status = Bc250SmuWaitReady(HwState, BC250_SMU_TIMEOUT_US);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmnWrite32(HwState, BC250_SMN_C2PMSG_90, 0);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmnWrite32(HwState, BC250_SMN_C2PMSG_82, Argument);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmnWrite32(HwState, BC250_SMN_C2PMSG_66, Message);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuWaitReady(HwState, BC250_SMU_TIMEOUT_US);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmnRead32(HwState, BC250_SMN_C2PMSG_82, &response);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    SmuState->LastMessage = Message;
    SmuState->LastResponse = response;
    *Response = response;

    if (response == BC250_SMU_RESULT_UNKNOWN_CMD ||
        response == BC250_SMU_RESULT_REJECTED_PREREQ ||
        response == BC250_SMU_RESULT_REJECTED_BUSY ||
        response == BC250_SMU_RESULT_FAILED) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250SmuInitializeQueries(
    _Inout_ BC250_HW_STATE* HwState,
    _Out_ BC250_SMU_STATE* SmuState
    )
{
    ULONG response;
    NTSTATUS status;

    if (HwState == NULL || SmuState == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(SmuState, sizeof(*SmuState));
    if (HwState->Variant != Bc250VariantCyanSkillfish2) {
        SmuState->QueryOnlyMode = TRUE;
        return STATUS_SUCCESS;
    }
    SmuState->QueryOnlyMode = TRUE;

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_TEST, 0, &response);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_GET_VERSION, 0,
                               &SmuState->Version);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_GET_DRIVER_IF, 0,
                               &SmuState->DriverInterfaceVersion);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_GET_ENABLED_FEATURES, 0,
                               &SmuState->EnabledFeatures);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_QUERY_GFXCLK, 0,
                               &SmuState->CurrentGfxFrequency);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_GET_GFX_VID, 0,
                               &SmuState->CurrentGfxVid);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = Bc250SmuSendQuery(HwState, SmuState,
                               BC250_SMU_MSG_QUERY_ACTIVE_WGP, 0,
                               &SmuState->ActiveWgp);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    SmuState->GfxOffObserved =
        (SmuState->ActiveWgp == 0 || SmuState->CurrentGfxFrequency <= 15);
    SmuState->Ready = TRUE;
    HwState->SmuReady = TRUE;
    HwState->AllowRegisterWrites = FALSE;
    return STATUS_SUCCESS;
}
