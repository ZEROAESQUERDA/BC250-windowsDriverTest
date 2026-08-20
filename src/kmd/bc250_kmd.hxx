#pragma once

extern "C" {
#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
}

#include "../hw/bc250_hw.h"
#include "../memory/bc250_memory.h"
#include "../gfx/bc250_gfx.h"
#include "../smu/bc250_smu.h"
#include "../firmware/bc250_firmware.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC250_LOG0(_level, _format) \
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, (_level), "BC250KMD: " _format "\n")

#define BC250_LOG1(_level, _format, _arg1) \
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, (_level), "BC250KMD: " _format "\n", (_arg1))

#define BC250_LOG2(_level, _format, _arg1, _arg2) \
    DbgPrintEx(DPFLTR_IHVVIDEO_ID, (_level), "BC250KMD: " _format "\n", (_arg1), (_arg2))

typedef struct _BC250_DEVICE_CONTEXT {
    PDEVICE_OBJECT PhysicalDeviceObject;
    DXGKRNL_INTERFACE DxgkInterface;
    DXGK_START_INFO StartInfo;
    DXGK_DEVICE_INFO DeviceInfo;
    BC250_HW_STATE Hw;
    BC250_MEMORY_STATE Memory;
    BC250_GFX_STATE Gfx;
    BC250_SMU_STATE Smu;
    BC250_FIRMWARE_STATE Firmware;
    volatile LONG InterruptPending;
    ULONG PendingEngineMask;
    ULONG LastInterruptMessage;
    BOOLEAN DeviceStarted;
} BC250_DEVICE_CONTEXT;

DRIVER_INITIALIZE DriverEntry;

VOID
Bc250DdiUnload(
    VOID
    );

NTSTATUS
Bc250DdiAddDevice(
    _In_ DEVICE_OBJECT* PhysicalDeviceObject,
    _Outptr_ PVOID* DeviceContext
    );

NTSTATUS
Bc250DdiRemoveDevice(
    _In_ PVOID DeviceContext
    );

NTSTATUS
Bc250DdiStartDevice(
    _In_ PVOID DeviceContext,
    _In_ DXGK_START_INFO* StartInfo,
    _In_ DXGKRNL_INTERFACE* DxgkInterface,
    _Out_ ULONG* NumberOfViews,
    _Out_ ULONG* NumberOfChildren
    );

NTSTATUS
Bc250DdiStopDevice(
    _In_ PVOID DeviceContext
    );

VOID
Bc250DdiResetDevice(
    _In_ PVOID DeviceContext
    );

NTSTATUS
Bc250DdiDispatchIoRequest(
    _In_ PVOID DeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ VIDEO_REQUEST_PACKET* VideoRequestPacket
    );

NTSTATUS
Bc250DdiSetPowerState(
    _In_ PVOID DeviceContext,
    _In_ ULONG HardwareUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType
    );

NTSTATUS
Bc250DdiQueryChildRelations(
    _In_ PVOID DeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* ChildRelations,
    _In_ ULONG ChildRelationsSize
    );

NTSTATUS
Bc250DdiQueryChildStatus(
    _In_ PVOID DeviceContext,
    _Inout_ DXGK_CHILD_STATUS* ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly
    );

NTSTATUS
Bc250DdiQueryDeviceDescriptor(
    _In_ PVOID DeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* DeviceDescriptor
    );

BOOLEAN
Bc250DdiInterruptRoutine(
    _In_ PVOID DeviceContext,
    _In_ ULONG MessageNumber
    );

VOID
Bc250DdiDpcRoutine(
    _In_ PVOID DeviceContext
    );

NTSTATUS
APIENTRY
Bc250DdiQueryAdapterInfo(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* QueryAdapterInfo
    );

NTSTATUS
APIENTRY
Bc250DdiSetPointerPosition(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* PointerPosition
    );

NTSTATUS
APIENTRY
Bc250DdiSetPointerShape(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* PointerShape
    );

NTSTATUS
APIENTRY
Bc250DdiPresentDisplayOnly(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* PresentDisplayOnly
    );

NTSTATUS
APIENTRY
Bc250DdiIsSupportedVidPn(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* IsSupportedVidPn
    );

NTSTATUS
APIENTRY
Bc250DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* RecommendFunctionalVidPn
    );

NTSTATUS
APIENTRY
Bc250DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* EnumVidPnCofuncModality
    );

NTSTATUS
APIENTRY
Bc250DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* SetVidPnSourceVisibility
    );

NTSTATUS
APIENTRY
Bc250DdiCommitVidPn(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CommitVidPn
    );

NTSTATUS
APIENTRY
Bc250DdiRecommendMonitorModes(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* RecommendMonitorModes
    );

NTSTATUS
APIENTRY
Bc250DdiQueryVidPnHWCapability(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* QueryVidPnHWCaps
    );

NTSTATUS
APIENTRY
Bc250DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* UpdateActivePath
    );

NTSTATUS
Bc250DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION* DisplayInfo
    );

NTSTATUS
APIENTRY
Bc250DdiSystemDisplayEnable(
    _In_ PVOID DeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat
    );

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
    );

#ifdef __cplusplus
}
#endif
