#pragma once

#include <ntddk.h>
#include <dispmprt.h>
#include <d3dkmddi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The project builds this path in experimental accelerated mode. The DDI
 * implementation is intentionally minimal and must be tested on Windows 10
 * x64 before production deployment.
 */
#ifndef BC250_ENABLE_FULL_WDDM
#define BC250_ENABLE_FULL_WDDM 1
#endif

NTSTATUS APIENTRY Bc250FullDdiCreateDevice(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_CREATEDEVICE* CreateDevice);
NTSTATUS APIENTRY Bc250FullDdiDestroyDevice(_In_ CONST HANDLE Device);
NTSTATUS APIENTRY Bc250FullDdiCreateContext(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_CREATECONTEXT* CreateContext);
NTSTATUS APIENTRY Bc250FullDdiDestroyContext(_In_ CONST HANDLE Context);
NTSTATUS APIENTRY Bc250FullDdiCreateAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_CREATEALLOCATION* CreateAllocation);
NTSTATUS APIENTRY Bc250FullDdiDestroyAllocation(
    _In_ CONST HANDLE Device,
    _In_ CONST HANDLE Allocation);
NTSTATUS APIENTRY Bc250FullDdiDescribeAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_DESCRIBEALLOCATION* DescribeAllocation);
NTSTATUS APIENTRY Bc250FullDdiGetStandardAllocationDriverData(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA* StandardAllocation);
NTSTATUS APIENTRY Bc250FullDdiOpenAllocation(
    _In_ CONST HANDLE Device,
    _Inout_ DXGKARG_OPENALLOCATION* OpenAllocation);
NTSTATUS APIENTRY Bc250FullDdiCloseAllocation(
    _In_ CONST HANDLE Device,
    _In_ CONST HANDLE Allocation);
NTSTATUS APIENTRY Bc250FullDdiRender(
    _In_ CONST HANDLE Context,
    _Inout_ DXGKARG_RENDER* Render);
NTSTATUS APIENTRY Bc250FullDdiPatch(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_PATCH* Patch);
NTSTATUS APIENTRY Bc250FullDdiBuildPagingBuffer(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER* BuildPagingBuffer);
NTSTATUS APIENTRY Bc250FullDdiSubmitCommand(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND* SubmitCommand);
NTSTATUS APIENTRY Bc250FullDdiPreemptCommand(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND* PreemptCommand);
NTSTATUS APIENTRY Bc250FullDdiQueryCurrentFence(
    _In_ CONST HANDLE Adapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE* QueryCurrentFence);
NTSTATUS APIENTRY Bc250FullDdiResetFromTimeout(
    _In_ CONST HANDLE Adapter);
NTSTATUS APIENTRY Bc250FullDdiRestartFromTimeout(
    _In_ CONST HANDLE Adapter);
NTSTATUS APIENTRY Bc250FullDdiSetVidPnSourceAddress(
    _In_ CONST HANDLE Adapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS* SetVidPnSourceAddress);

#ifdef __cplusplus
}
#endif
