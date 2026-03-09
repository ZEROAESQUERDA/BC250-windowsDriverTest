/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_umd.c

Abstract:
    User-Mode Display Driver (UMD) for the AMD BC-250 APU.

    The UMD is a DLL loaded by the Direct3D runtime. It implements the
    D3D DDI (Device Driver Interface) for user-mode rendering operations,
    including:
      - Adapter opening and device creation
      - Resource (texture, buffer) creation and management
      - Command buffer (DMA buffer) building for rendering
      - State management (render states, shader binding)
      - Present operations (flip/blit to display)

    The UMD communicates with the KMD via the D3DKMTxxx thunk functions
    provided by gdi32.dll / dxgkrnl.sys.

Environment:
    User mode (ring 3), loaded by d3d9.dll / d3d10.dll / d3d11.dll / d3d12.dll

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dumddi.h>
#include <d3dkmthk.h>
#include "../../inc/amdbc250_hw.h"

/*===========================================================================
  UMD Device Context
  Allocated per D3D device (one per application using the GPU)
===========================================================================*/

typedef struct _AMDBC250_UMD_DEVICE {
    HANDLE              hDevice;            /* KMD device handle             */
    HANDLE              hAdapter;           /* Adapter handle                */
    UINT                Width;              /* Current render target width   */
    UINT                Height;             /* Current render target height  */
    D3DDDIFORMAT        Format;             /* Current render target format  */
    UINT                CommandBufferSize;  /* DMA buffer size               */
    PVOID               pCommandBuffer;     /* Current DMA buffer pointer    */
    UINT                CommandBufferUsed;  /* Bytes used in current buffer  */
    UINT64              FenceValue;         /* Current fence value           */
    CRITICAL_SECTION    DeviceLock;         /* Thread safety lock            */
} AMDBC250_UMD_DEVICE, *PAMDBC250_UMD_DEVICE;

/*===========================================================================
  UMD Adapter Context
===========================================================================*/

typedef struct _AMDBC250_UMD_ADAPTER {
    D3DDDI_ADAPTERCALLBACKS Callbacks;      /* D3D runtime callbacks         */
    UINT                VendorId;           /* PCI Vendor ID                 */
    UINT                DeviceId;           /* PCI Device ID                 */
    UINT                SubSysId;           /* PCI Subsystem ID              */
    UINT                Revision;           /* PCI Revision                  */
} AMDBC250_UMD_ADAPTER, *PAMDBC250_UMD_ADAPTER;

/*===========================================================================
  Forward declarations
===========================================================================*/

static HRESULT APIENTRY UmdOpenAdapter(D3DDDIARG_OPENADAPTER *pOpenAdapter);
static HRESULT APIENTRY UmdCloseAdapter(HANDLE hAdapter);
static HRESULT APIENTRY UmdCreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pCreateData);
static HRESULT APIENTRY UmdDestroyDevice(HANDLE hDevice);
static HRESULT APIENTRY UmdCreateResource(HANDLE hDevice, D3DDDIARG_CREATERESOURCE *pResource);
static HRESULT APIENTRY UmdDestroyResource(HANDLE hDevice, HANDLE hResource);
static HRESULT APIENTRY UmdSetRenderState(HANDLE hDevice, CONST D3DDDIARG_RENDERSTATE *pData);
static HRESULT APIENTRY UmdDrawPrimitive(HANDLE hDevice, CONST D3DDDIARG_DRAWPRIMITIVE *pData, CONST UINT *pFlagBuffer);
static HRESULT APIENTRY UmdPresent(HANDLE hDevice, CONST D3DDDIARG_PRESENT *pData);
static HRESULT APIENTRY UmdFlush(HANDLE hDevice);
static HRESULT APIENTRY UmdLock(HANDLE hDevice, D3DDDIARG_LOCK *pData);
static HRESULT APIENTRY UmdUnlock(HANDLE hDevice, CONST D3DDDIARG_UNLOCK *pData);
static HRESULT APIENTRY UmdSetStreamSource(HANDLE hDevice, CONST D3DDDIARG_SETSTREAMSOURCE *pData);
static HRESULT APIENTRY UmdSetIndices(HANDLE hDevice, CONST D3DDDIARG_SETINDICES *pData);
static HRESULT APIENTRY UmdSetViewport(HANDLE hDevice, CONST D3DDDIARG_VIEWPORTINFO *pData);
static HRESULT APIENTRY UmdSetScissorRect(HANDLE hDevice, CONST RECT *pRect);
static HRESULT APIENTRY UmdSetRenderTarget(HANDLE hDevice, CONST D3DDDIARG_SETRENDERTARGET *pData);
static HRESULT APIENTRY UmdClear(HANDLE hDevice, CONST D3DDDIARG_CLEAR *pData, UINT NumRect, CONST RECT *pRect);
static HRESULT APIENTRY UmdCreateVertexShaderDecl(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERDECL *pData, CONST D3DDDIVERTEXELEMENT *pVertexElements);
static HRESULT APIENTRY UmdSetVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeleteVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdCreateVertexShaderFunc(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERFUNC *pData, CONST UINT *pCode);
static HRESULT APIENTRY UmdSetVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeleteVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdCreatePixelShader(HANDLE hDevice, D3DDDIARG_CREATEPIXELSHADER *pData, CONST UINT *pCode);
static HRESULT APIENTRY UmdSetPixelShader(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeletePixelShader(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdSetTexture(HANDLE hDevice, UINT Stage, HANDLE hTexture);
static HRESULT APIENTRY UmdSetTextureStageState(HANDLE hDevice, CONST D3DDDIARG_TEXTURESTAGESTATE *pData);
static HRESULT APIENTRY UmdSetSamplerState(HANDLE hDevice, UINT Sampler, D3DDDITEXTUREFILTERTYPE State, UINT Value);
static HRESULT APIENTRY UmdQueryGetData(HANDLE hDevice, D3DDDIARG_QUERYGETDATA *pData);
static HRESULT APIENTRY UmdIssueQuery(HANDLE hDevice, CONST D3DDDIARG_ISSUEQUERY *pData);
static HRESULT APIENTRY UmdCreateQuery(HANDLE hDevice, D3DDDIARG_CREATEQUERY *pData);
static HRESULT APIENTRY UmdDestroyQuery(HANDLE hDevice, HANDLE hQuery);

/*===========================================================================
  DllMain - DLL entry point
===========================================================================*/

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,
    DWORD     fdwReason,
    LPVOID    lpvReserved
    )
{
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(lpvReserved);

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

/*===========================================================================
  OpenAdapter - Entry point called by D3D runtime to open the GPU adapter.
  This is the first function called; it fills in the DDI function table.
===========================================================================*/

HRESULT APIENTRY OpenAdapter(
    D3DDDIARG_OPENADAPTER *pOpenAdapter
    )
{
    return UmdOpenAdapter(pOpenAdapter);
}

static HRESULT APIENTRY
UmdOpenAdapter(
    D3DDDIARG_OPENADAPTER *pOpenAdapter
    )
{
    PAMDBC250_UMD_ADAPTER pAdapter;

    if (pOpenAdapter == NULL) {
        return E_INVALIDARG;
    }

    /* Allocate adapter context */
    pAdapter = (PAMDBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_ADAPTER)
        );

    if (pAdapter == NULL) {
        return E_OUTOFMEMORY;
    }

    /* Save D3D runtime callbacks */
    pAdapter->Callbacks = *pOpenAdapter->pAdapterCallbacks;

    /* Store adapter handle */
    pOpenAdapter->hAdapter = (HANDLE)pAdapter;

    /*
     * Fill in the D3D DDI function table.
     * The runtime will call these functions for all rendering operations.
     */
    pOpenAdapter->pAdapterFuncs->pfnGetCaps        = NULL;  /* Use D3D9 caps */
    pOpenAdapter->pAdapterFuncs->pfnCreateDevice   = UmdCreateDevice;
    pOpenAdapter->pAdapterFuncs->pfnCloseAdapter   = UmdCloseAdapter;

    return S_OK;
}

/*===========================================================================
  CloseAdapter - Called when the D3D runtime releases the adapter.
===========================================================================*/

static HRESULT APIENTRY
UmdCloseAdapter(
    HANDLE hAdapter
    )
{
    PAMDBC250_UMD_ADAPTER pAdapter = (PAMDBC250_UMD_ADAPTER)hAdapter;

    if (pAdapter != NULL) {
        HeapFree(GetProcessHeap(), 0, pAdapter);
    }

    return S_OK;
}

/*===========================================================================
  CreateDevice - Creates a D3D device (per-application rendering context).
===========================================================================*/

static HRESULT APIENTRY
UmdCreateDevice(
    HANDLE hAdapter,
    D3DDDIARG_CREATEDEVICE *pCreateData
    )
{
    PAMDBC250_UMD_DEVICE pDevice;

    UNREFERENCED_PARAMETER(hAdapter);

    pDevice = (PAMDBC250_UMD_DEVICE)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_DEVICE)
        );

    if (pDevice == NULL) {
        return E_OUTOFMEMORY;
    }

    InitializeCriticalSection(&pDevice->DeviceLock);
    pDevice->hDevice  = pCreateData->hDevice;
    pDevice->hAdapter = (HANDLE)hAdapter;
    pDevice->FenceValue = 0;

    /* Fill in device DDI function table */
    pCreateData->pDeviceFuncs->pfnDestroyDevice         = UmdDestroyDevice;
    pCreateData->pDeviceFuncs->pfnCreateResource         = UmdCreateResource;
    pCreateData->pDeviceFuncs->pfnDestroyResource        = UmdDestroyResource;
    pCreateData->pDeviceFuncs->pfnSetRenderState         = UmdSetRenderState;
    pCreateData->pDeviceFuncs->pfnDrawPrimitive          = UmdDrawPrimitive;
    pCreateData->pDeviceFuncs->pfnPresent                = UmdPresent;
    pCreateData->pDeviceFuncs->pfnFlush                  = UmdFlush;
    pCreateData->pDeviceFuncs->pfnLock                   = UmdLock;
    pCreateData->pDeviceFuncs->pfnUnlock                 = UmdUnlock;
    pCreateData->pDeviceFuncs->pfnSetStreamSource        = UmdSetStreamSource;
    pCreateData->pDeviceFuncs->pfnSetIndices             = UmdSetIndices;
    pCreateData->pDeviceFuncs->pfnSetViewport            = UmdSetViewport;
    pCreateData->pDeviceFuncs->pfnSetScissorRect         = UmdSetScissorRect;
    pCreateData->pDeviceFuncs->pfnSetRenderTarget        = UmdSetRenderTarget;
    pCreateData->pDeviceFuncs->pfnClear                  = UmdClear;
    pCreateData->pDeviceFuncs->pfnCreateVertexShaderDecl = UmdCreateVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnSetVertexShaderDecl    = UmdSetVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnDeleteVertexShaderDecl = UmdDeleteVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnCreateVertexShaderFunc = UmdCreateVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnSetVertexShaderFunc    = UmdSetVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnDeleteVertexShaderFunc = UmdDeleteVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnCreatePixelShader      = UmdCreatePixelShader;
    pCreateData->pDeviceFuncs->pfnSetPixelShader         = UmdSetPixelShader;
    pCreateData->pDeviceFuncs->pfnDeletePixelShader      = UmdDeletePixelShader;
    pCreateData->pDeviceFuncs->pfnSetTexture             = UmdSetTexture;
    pCreateData->pDeviceFuncs->pfnSetTextureStageState   = UmdSetTextureStageState;
    pCreateData->pDeviceFuncs->pfnSetSamplerState        = UmdSetSamplerState;
    pCreateData->pDeviceFuncs->pfnQueryGetData           = UmdQueryGetData;
    pCreateData->pDeviceFuncs->pfnIssueQuery             = UmdIssueQuery;
    pCreateData->pDeviceFuncs->pfnCreateQuery            = UmdCreateQuery;
    pCreateData->pDeviceFuncs->pfnDestroyQuery           = UmdDestroyQuery;

    pCreateData->hDevice = (HANDLE)pDevice;

    return S_OK;
}

/*===========================================================================
  DestroyDevice - Destroys a D3D device context.
===========================================================================*/

static HRESULT APIENTRY
UmdDestroyDevice(
    HANDLE hDevice
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;

    if (pDevice != NULL) {
        DeleteCriticalSection(&pDevice->DeviceLock);
        HeapFree(GetProcessHeap(), 0, pDevice);
    }

    return S_OK;
}

/*===========================================================================
  CreateResource - Allocates a GPU resource (texture, vertex buffer, etc.)
===========================================================================*/

static HRESULT APIENTRY
UmdCreateResource(
    HANDLE hDevice,
    D3DDDIARG_CREATERESOURCE *pResource
    )
{
    UNREFERENCED_PARAMETER(hDevice);

    /*
     * In a full implementation, this function would:
     * 1. Calculate the required size and alignment for the resource
     * 2. Call D3DKMTCreateAllocation to allocate GPU memory via the KMD
     * 3. Map the allocation to a GPU virtual address
     * 4. Return the allocation handle in pResource->hResource
     *
     * The KMD's DxgkDdiCreateAllocation will be called as a result of
     * D3DKMTCreateAllocation.
     */

    pResource->hResource = (HANDLE)(ULONG_PTR)0x1;  /* Placeholder handle */
    return S_OK;
}

/*===========================================================================
  DestroyResource - Frees a GPU resource.
===========================================================================*/

static HRESULT APIENTRY
UmdDestroyResource(
    HANDLE hDevice,
    HANDLE hResource
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(hResource);
    return S_OK;
}

/*===========================================================================
  DrawPrimitive - Emits draw commands into the DMA command buffer.
  This is the core rendering path.
===========================================================================*/

static HRESULT APIENTRY
UmdDrawPrimitive(
    HANDLE hDevice,
    CONST D3DDDIARG_DRAWPRIMITIVE *pData,
    CONST UINT *pFlagBuffer
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    PULONG pCmd;

    UNREFERENCED_PARAMETER(pFlagBuffer);

    if (pDevice->pCommandBuffer == NULL) {
        return E_FAIL;
    }

    pCmd = (PULONG)((PUCHAR)pDevice->pCommandBuffer + pDevice->CommandBufferUsed);

    /*
     * Emit PM4 DRAW_INDEX_AUTO packet for non-indexed draws.
     * This is the RDNA2 draw call packet format.
     *
     * Packet format:
     *   DW0: PM4 header (type 3, opcode DRAW_INDEX_AUTO, count=2)
     *   DW1: Vertex count
     *   DW2: Draw initiator flags
     */
    pCmd[0] = PM4_TYPE3_HDR(PM4_IT_DRAW_INDEX_AUTO, 3);
    pCmd[1] = pData->NumVertices;
    pCmd[2] = (ULONG)pData->PrimitiveType | (1 << 8);  /* USE_OPAQUE flag */

    pDevice->CommandBufferUsed += 3 * sizeof(ULONG);

    return S_OK;
}

/*===========================================================================
  Present - Flips/blits the back buffer to the display.
===========================================================================*/

static HRESULT APIENTRY
UmdPresent(
    HANDLE hDevice,
    CONST D3DDDIARG_PRESENT *pData
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    D3DKMT_PRESENT PresentData = {0};

    UNREFERENCED_PARAMETER(pData);

    /* First flush pending commands */
    UmdFlush(hDevice);

    /* Submit present to KMD via D3DKMT thunk */
    PresentData.hDevice          = pDevice->hDevice;
    PresentData.hWindow          = NULL;
    PresentData.BroadcastContextCount = 0;
    PresentData.Flags.Blt        = 1;

    D3DKMTPresent(&PresentData);

    return S_OK;
}

/*===========================================================================
  Flush - Submits the current command buffer to the GPU.
===========================================================================*/

static HRESULT APIENTRY
UmdFlush(
    HANDLE hDevice
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    D3DKMT_SUBMITCOMMAND SubmitData = {0};

    if (pDevice->CommandBufferUsed == 0) {
        return S_OK;
    }

    /* Submit command buffer to KMD */
    SubmitData.Commands          = (D3DGPU_VIRTUAL_ADDRESS)(ULONG_PTR)pDevice->pCommandBuffer;
    SubmitData.CommandLength     = pDevice->CommandBufferUsed;
    SubmitData.NumPrimaries      = 0;

    D3DKMTSubmitCommand(&SubmitData);

    pDevice->CommandBufferUsed = 0;
    pDevice->FenceValue++;

    return S_OK;
}

/*===========================================================================
  Lock / Unlock - CPU access to GPU resources (textures, buffers).
===========================================================================*/

static HRESULT APIENTRY
UmdLock(
    HANDLE hDevice,
    D3DDDIARG_LOCK *pData
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pData);
    /* In a full implementation: map GPU memory to CPU-accessible address */
    return S_OK;
}

static HRESULT APIENTRY
UmdUnlock(
    HANDLE hDevice,
    CONST D3DDDIARG_UNLOCK *pData
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pData);
    return S_OK;
}

/*===========================================================================
  State-setting DDI stubs
  These functions update GPU render state by emitting PM4 state packets.
===========================================================================*/

static HRESULT APIENTRY UmdSetRenderState(HANDLE hDevice, CONST D3DDDIARG_RENDERSTATE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetStreamSource(HANDLE hDevice, CONST D3DDDIARG_SETSTREAMSOURCE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetIndices(HANDLE hDevice, CONST D3DDDIARG_SETINDICES *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetViewport(HANDLE hDevice, CONST D3DDDIARG_VIEWPORTINFO *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetScissorRect(HANDLE hDevice, CONST RECT *pRect)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pRect); return S_OK; }

static HRESULT APIENTRY UmdSetRenderTarget(HANDLE hDevice, CONST D3DDDIARG_SETRENDERTARGET *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdClear(HANDLE hDevice, CONST D3DDDIARG_CLEAR *pData, UINT NumRect, CONST RECT *pRect)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); UNREFERENCED_PARAMETER(NumRect); UNREFERENCED_PARAMETER(pRect); return S_OK; }

static HRESULT APIENTRY UmdCreateVertexShaderDecl(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERDECL *pData, CONST D3DDDIVERTEXELEMENT *pVertexElements)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pVertexElements); pData->ShaderHandle = (HANDLE)1; return S_OK; }

static HRESULT APIENTRY UmdSetVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeleteVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdCreateVertexShaderFunc(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERFUNC *pData, CONST UINT *pCode)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCode); pData->ShaderHandle = (HANDLE)1; return S_OK; }

static HRESULT APIENTRY UmdSetVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeleteVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdCreatePixelShader(HANDLE hDevice, D3DDDIARG_CREATEPIXELSHADER *pData, CONST UINT *pCode)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCode); pData->ShaderHandle = (HANDLE)1; return S_OK; }

static HRESULT APIENTRY UmdSetPixelShader(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeletePixelShader(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdSetTexture(HANDLE hDevice, UINT Stage, HANDLE hTexture)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Stage); UNREFERENCED_PARAMETER(hTexture); return S_OK; }

static HRESULT APIENTRY UmdSetTextureStageState(HANDLE hDevice, CONST D3DDDIARG_TEXTURESTAGESTATE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetSamplerState(HANDLE hDevice, UINT Sampler, D3DDDITEXTUREFILTERTYPE State, UINT Value)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Sampler); UNREFERENCED_PARAMETER(State); UNREFERENCED_PARAMETER(Value); return S_OK; }

static HRESULT APIENTRY UmdQueryGetData(HANDLE hDevice, D3DDDIARG_QUERYGETDATA *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdIssueQuery(HANDLE hDevice, CONST D3DDDIARG_ISSUEQUERY *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdCreateQuery(HANDLE hDevice, D3DDDIARG_CREATEQUERY *pData)
{ UNREFERENCED_PARAMETER(hDevice); pData->hQuery = (HANDLE)1; return S_OK; }

static HRESULT APIENTRY UmdDestroyQuery(HANDLE hDevice, HANDLE hQuery)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hQuery); return S_OK; }
