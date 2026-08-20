#include "bc250_firmware.h"

#define BC250_FIRMWARE_MAX_IMAGE_SIZE (16u * 1024u * 1024u)

static ULONG
Bc250FirmwareBit(
    _In_ BC250_FIRMWARE_KIND Kind
    )
{
    return 1u << (ULONG)Kind;
}

PCWSTR
Bc250FirmwareGetName(
    _In_ const BC250_HW_STATE* HwState,
    _In_ BC250_FIRMWARE_KIND Kind
    )
{
    if (HwState == NULL) {
        return NULL;
    }

    if (Kind == Bc250FirmwareGpuInfo) {
        return L"amdgpu\\cyan_skillfish_gpu_info.bin";
    }
    if (Kind == Bc250FirmwareIpDiscovery) {
        return L"amdgpu\\ip_discovery.bin";
    }

    if (HwState->Variant != Bc250VariantCyanSkillfish2) {
        return NULL;
    }

    switch (Kind) {
    case Bc250FirmwareCe:
        return L"amdgpu\\cyan_skillfish2_ce.bin";
    case Bc250FirmwarePfp:
        return L"amdgpu\\cyan_skillfish2_pfp.bin";
    case Bc250FirmwareMe:
        return L"amdgpu\\cyan_skillfish2_me.bin";
    case Bc250FirmwareMec:
        return L"amdgpu\\cyan_skillfish2_mec.bin";
    case Bc250FirmwareMec2:
        return L"amdgpu\\cyan_skillfish2_mec2.bin";
    case Bc250FirmwareRlc:
        return L"amdgpu\\cyan_skillfish2_rlc.bin";
    case Bc250FirmwareSdma:
        return L"amdgpu\\cyan_skillfish2_sdma.bin";
    case Bc250FirmwareSdma1:
        return L"amdgpu\\cyan_skillfish2_sdma1.bin";
    case Bc250FirmwarePsp:
    case Bc250FirmwareSmu:
    default:
        return NULL;
    }
}

static BOOLEAN
Bc250FirmwareIsRequired(
    _In_ const BC250_HW_STATE* HwState,
    _In_ BC250_FIRMWARE_KIND Kind
    )
{
    UNREFERENCED_PARAMETER(HwState);

    switch (Kind) {
    case Bc250FirmwareCe:
    case Bc250FirmwarePfp:
    case Bc250FirmwareMe:
    case Bc250FirmwareMec:
    case Bc250FirmwareRlc:
    case Bc250FirmwareSdma:
        return TRUE;
    case Bc250FirmwareMec2:
    case Bc250FirmwareSdma1:
    case Bc250FirmwareGpuInfo:
    case Bc250FirmwareIpDiscovery:
    case Bc250FirmwarePsp:
    case Bc250FirmwareSmu:
    default:
        /* PSP/SMU/IP-discovery may already be supplied by boot firmware. */
        return FALSE;
    }
}

NTSTATUS
Bc250FirmwareValidateImage(
    _In_ const BC250_FIRMWARE_IMAGE* Image
    )
{
    SIZE_T index;
    BOOLEAN nonZero = FALSE;

    if (Image == NULL || Image->Data == NULL || Image->Size == 0 ||
        Image->Size > BC250_FIRMWARE_MAX_IMAGE_SIZE ||
        Image->Kind >= Bc250FirmwareCount) {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Image->Size & (sizeof(ULONG) - 1u)) != 0) {
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    for (index = 0; index < Image->Size; ++index) {
        if (Image->Data[index] != 0) {
            nonZero = TRUE;
            break;
        }
    }

    if (!nonZero) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250FirmwareValidateSet(
    _In_ const BC250_HW_STATE* HwState,
    _In_reads_(ImageCount) const BC250_FIRMWARE_IMAGE* Images,
    _In_ ULONG ImageCount,
    _Out_ BC250_FIRMWARE_STATE* FirmwareState
    )
{
    ULONG index;
    ULONG kindIndex;

    if (HwState == NULL || Images == NULL || FirmwareState == NULL ||
        ImageCount > Bc250FirmwareCount) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(FirmwareState, sizeof(*FirmwareState));
    FirmwareState->VariantMatches = HwState->Variant != Bc250VariantUnknown;
    FirmwareState->ImageCount = ImageCount;

    for (kindIndex = 0; kindIndex < Bc250FirmwareCount; ++kindIndex) {
        if (Bc250FirmwareIsRequired(
                HwState,
                (BC250_FIRMWARE_KIND)kindIndex)) {
            FirmwareState->RequiredMask |= Bc250FirmwareBit(
                (BC250_FIRMWARE_KIND)kindIndex);
        }
    }

    for (index = 0; index < ImageCount; ++index) {
        const BC250_FIRMWARE_IMAGE* image = &Images[index];
        NTSTATUS status;
        ULONG bit;

        if (image->Kind >= Bc250FirmwareCount) {
            return STATUS_INVALID_PARAMETER;
        }

        bit = Bc250FirmwareBit(image->Kind);
        if ((FirmwareState->PresentMask & bit) != 0) {
            return STATUS_OBJECT_NAME_COLLISION;
        }
        FirmwareState->PresentMask |= bit;

        status = Bc250FirmwareValidateImage(image);
        if (NT_SUCCESS(status)) {
            FirmwareState->ValidMask |= bit;
        } else if ((FirmwareState->RequiredMask & bit) != 0) {
            return status;
        }
    }

    FirmwareState->AllRequiredPresent =
        (FirmwareState->PresentMask & FirmwareState->RequiredMask) ==
        FirmwareState->RequiredMask;
    FirmwareState->AllRequiredValid =
        (FirmwareState->ValidMask & FirmwareState->RequiredMask) ==
        FirmwareState->RequiredMask;
    FirmwareState->DiscoveryValid =
        ((FirmwareState->ValidMask & Bc250FirmwareBit(Bc250FirmwareGpuInfo)) != 0) ||
        ((FirmwareState->ValidMask & Bc250FirmwareBit(Bc250FirmwareIpDiscovery)) != 0) ||
        (HwState->Variant == Bc250VariantCyanSkillfish ||
         HwState->Variant == Bc250VariantCyanSkillfish2);

    if (!FirmwareState->VariantMatches ||
        !FirmwareState->AllRequiredPresent ||
        !FirmwareState->AllRequiredValid) {
        return STATUS_DEVICE_NOT_READY;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Bc250FirmwareCommitReady(
    _Inout_ BC250_HW_STATE* HwState,
    _In_ const BC250_FIRMWARE_STATE* FirmwareState
    )
{
    if (HwState == NULL || FirmwareState == NULL ||
        !FirmwareState->VariantMatches ||
        !FirmwareState->AllRequiredPresent ||
        !FirmwareState->AllRequiredValid ||
        (FirmwareState->LoadedMask & FirmwareState->RequiredMask) !=
            FirmwareState->RequiredMask ||
        !FirmwareState->DiscoveryValid) {
        return STATUS_DEVICE_NOT_READY;
    }

    HwState->FirmwareReady = TRUE;
    HwState->AllowRegisterWrites = FALSE;
    return STATUS_SUCCESS;
}
