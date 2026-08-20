#pragma once

#include <ntddk.h>
#include <d3dkmddi.h>

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
Bc250QueryUmaSegments(
    _In_ const DXGK_QUERYSEGMENTIN* QueryIn,
    _Inout_ DXGK_QUERYSEGMENTOUT* QueryOut,
    _In_ SIZE_T OutputDataSize
    );

#ifdef __cplusplus
}
#endif
