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
#include <d3d10umddi.h>
#include <d3d11umddi.h>
#include <dxgiddi.h>

#include "../../inc/amdbc250_hw.h"
#include "../../inc/amdbc250_dxgi.h"
#include "../../inc/amdbc250_d3d10.h"
#include "../../inc/amdbc250_d3d11.h"
#include "../../inc/amdbc250_d3d12.h"

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
  Forward declarations (D3D9 DDI - existing)
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

// Entry point for DXGI DDI
HRESULT APIENTRY OpenAdapter10(
    D3D10DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    // This function is called by the D3D10 runtime to get the DXGI DDI functions.
    // We will fill in the DXGI_DDI_BASE_FUNCTIONS table here.
    pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfo = Bc250DxgiQueryAdapterInfo;
    pOpenAdapter->pAdapterCallbacks->pfnCreateDevice = Bc250DxgiCreateDevice;
    pOpenAdapter->pAdapterCallbacks->pfnDestroyDevice = Bc250DxgiDestroyDevice;
    pOpenAdapter->pAdapterCallbacks->pfnPresent = Bc250DxgiPresent;
    pOpenAdapter->pAdapterCallbacks->pfnGetScanLine = Bc250DxgiGetScanLine;
    pOpenAdapter->pAdapterCallbacks->pfnSetResourcePriority = Bc250DxgiSetResourcePriority;
    pOpenAdapter->pAdapterCallbacks->pfnQueryResourceResidency = Bc250DxgiQueryResourceResidency;
    pOpenAdapter->pAdapterCallbacks->pfnRotatePresent = Bc250DxgiRotatePresent;
    pOpenAdapter->pAdapterCallbacks->pfnSetDisplayMode = Bc250DxgiSetDisplayMode;
    pOpenAdapter->pAdapterCallbacks->pfnSetGamma = Bc250DxgiSetGamma;
    pOpenAdapter->pAdapterCallbacks->pfnSetVidPnSourceAddress = Bc250DxgiSetVidPnSourceAddress;
    pOpenAdapter->pAdapterCallbacks->pfnWaitForVerticalBlank = Bc250DxgiWaitForVerticalBlank;
    pOpenAdapter->pAdapterCallbacks->pfnOfferResources = Bc250DxgiOfferResources;
    pOpenAdapter->pAdapterCallbacks->pfnReclaimResources = Bc250DxgiReclaimResources;
    pOpenAdapter->pAdapterCallbacks->pfnGetDeviceRemovalReason = Bc250DxgiGetDeviceRemovalReason;
    pOpenAdapter->pAdapterCallbacks->pfnBlt = Bc250DxgiBlt;
    pOpenAdapter->pAdapterCallbacks->pfnResolveSharedResource = Bc250DxgiResolveSharedResource;
    pOpenAdapter->pAdapterCallbacks->pfnGetForcedPresentInterval = Bc250DxgiGetForcedPresentInterval;
    pOpenAdapter->pAdapterCallbacks->pfnPresentMultiPlaneOverlay = Bc250DxgiPresentMultiPlaneOverlay;
    pOpenAdapter->pAdapterCallbacks->pfnGetMultiPlaneOverlayCaps = Bc250DxgiGetMultiPlaneOverlayCaps;
    pOpenAdapter->pAdapterCallbacks->pfnCheckMultiPlaneOverlaySupport = Bc250DxgiCheckMultiPlaneOverlaySupport;
    pOpenAdapter->pAdapterCallbacks->pfnPresentMultiPlaneOverlay2 = Bc250DxgiPresentMultiPlaneOverlay2;
    pOpenAdapter->pAdapterCallbacks->pfnCheckMultiPlaneOverlaySupport2 = Bc250DxgiCheckMultiPlaneOverlaySupport2;
    pOpenAdapter->pAdapterCallbacks->pfnPresentMultiPlaneOverlay3 = Bc250DxgiPresentMultiPlaneOverlay3;
    pOpenAdapter->pAdapterCallbacks->pfnCheckMultiPlaneOverlaySupport3 = Bc250DxgiCheckMultiPlaneOverlaySupport3;

    return S_OK;
}

// Entry point for D3D10 DDI
HRESULT APIENTRY OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    // This function is called by the D3D10.1/10.2 runtime to get the D3D10 DDI functions.
    // We will fill in the D3D10DDI_DEVICEFUNCS table here.
    pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfo = Bc250DxgiQueryAdapterInfo;
    pOpenAdapter->pAdapterCallbacks->pfnCreateDevice = Bc250D3D10CreateDevice;
    pOpenAdapter->pAdapterCallbacks->pfnDestroyDevice = Bc250D3D10DestroyDevice;

    return S_OK;
}

// Entry point for D3D11 DDI
HRESULT APIENTRY OpenAdapter11(
    D3D11DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    // This function is called by the D3D11 runtime to get the D3D11 DDI functions.
    // We will fill in the D3D11DDI_DEVICEFUNCS table here.
    pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfo = Bc250DxgiQueryAdapterInfo;
    pOpenAdapter->pAdapterCallbacks->pfnCreateDevice = Bc250D3D11CreateDevice;
    pOpenAdapter->pAdapterCallbacks->pfnDestroyDevice = Bc250D3D11DestroyDevice;

    return S_OK;
}

// Entry point for D3D12 DDI
HRESULT APIENTRY OpenAdapter12(
    D3D12DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    // This function is called by the D3D12 runtime to get the D3D12 DDI functions.
    // We will fill in the D3D12DDI_DEVICE_FUNCS table here.
    pOpenAdapter->pAdapterCallbacks->pfnQueryAdapterInfo = Bc250DxgiQueryAdapterInfo;
    pOpenAdapter->pAdapterCallbacks->pfnCreateDevice = Bc250D3D12CreateDevice;
    pOpenAdapter->pAdapterCallbacks->pfnDestroyDevice = Bc250D3D12DestroyDevice;

    return S_OK;
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

//=============================================================================
// DXGI DDI Function Stubs
//=============================================================================

NTSTATUS APIENTRY Bc250DxgiQueryAdapterInfo(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGI_DDI_ARG_QUERYADAPTERINFO* pQueryAdapterInfo
    ) { UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(pQueryAdapterInfo); return STATUS_SUCCESS; }

NTHRESULT APIENTRY Bc250D3D10CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* pCreateDevice
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    // Fill in D3D10 DDI device functions
    D3D10DDI_DEVICEFUNCS* pDeviceFuncs10 = pCreateDevice->pDeviceFuncs;
    pDeviceFuncs10->pfnCreateBlendState = Bc250D3D10CreateBlendState;
    pDeviceFuncs10->pfnDestroyBlendState = Bc250D3D10DestroyBlendState;
    pDeviceFuncs10->pfnCreateRasterizerState = Bc250D3D10CreateRasterizerState;
    pDeviceFuncs10->pfnDestroyRasterizerState = Bc250D3D10DestroyRasterizerState;
    pDeviceFuncs10->pfnCreateDepthStencilState = Bc250D3D10CreateDepthStencilState;
    pDeviceFuncs10->pfnDestroyDepthStencilState = Bc250D3D10DestroyDepthStencilState;
    pDeviceFuncs10->pfnCreateGeometryShader = Bc250D3D10CreateGeometryShader;
    pDeviceFuncs10->pfnDestroyShader = Bc250D3D10DestroyShader;
    pDeviceFuncs10->pfnCreateVertexShader = Bc250D3D10CreateVertexShader;
    pDeviceFuncs10->pfnCreatePixelShader = Bc250D3D10CreatePixelShader;
    pDeviceFuncs10->pfnCreateInputLayout = Bc250D3D10CreateInputLayout;
    pDeviceFuncs10->pfnDestroyInputLayout = Bc250D3D10DestroyInputLayout;
    pDeviceFuncs10->pfnCreateBuffer = Bc250D3D10CreateBuffer;
    pDeviceFuncs10->pfnCreateTexture2D = Bc250D3D10CreateTexture2D;
    pDeviceFuncs10->pfnDestroyResource = Bc250D3D10DestroyResource;
    pDeviceFuncs10->pfnSetViewports = Bc250D3D10SetViewports;
    pDeviceFuncs10->pfnSetScissorRects = Bc250D3D10SetScissorRects;
    pDeviceFuncs10->pfnSetBlendState = Bc250D3D10SetBlendState;
    pDeviceFuncs10->pfnSetRasterizerState = Bc250D3D10SetRasterizerState;
    pDeviceFuncs10->pfnSetDepthStencilState = Bc250D3D10SetDepthStencilState;
    pDeviceFuncs10->pfnDrawIndexed = Bc250D3D10DrawIndexed;
    pDeviceFuncs10->pfnDraw = Bc250D3D10Draw;
    pDeviceFuncs10->pfnSetVertexBuffers = Bc250D3D10SetVertexBuffers;
    pDeviceFuncs10->pfnSetIndexBuffer = Bc250D3D10SetIndexBuffer;
    pDeviceFuncs10->pfnSetInputLayout = Bc250D3D10SetInputLayout;
    pDeviceFuncs10->pfnSetVertexShader = Bc250D3D10SetVertexShader;
    pDeviceFuncs10->pfnSetPixelShader = Bc250D3D10SetPixelShader;
    pDeviceFuncs10->pfnSetGeometryShader = Bc250D3D10SetGeometryShader;
    pDeviceFuncs10->pfnSetConstantBuffers = Bc250D3D10SetConstantBuffers;
    pDeviceFuncs10->pfnSetShaderResources = Bc250D3D10SetShaderResources;
    pDeviceFuncs10->pfnSetSamplers = Bc250D3D10SetSamplers;
    pDeviceFuncs10->pfnSetRenderTargets = Bc250D3D10SetRenderTargets;
    pDeviceFuncs10->pfnClearRenderTargetView = Bc250D3D10ClearRenderTargetView;
    pDeviceFuncs10->pfnClearDepthStencilView = Bc250D3D10ClearDepthStencilView;
    pDeviceFuncs10->pfnFlush = Bc250D3D10Flush;
    pDeviceFuncs10->pfnGenMips = Bc250D3D10GenMips;
    pDeviceFuncs10->pfnResourceMap = Bc250D3D10ResourceMap;
    pDeviceFuncs10->pfnResourceUnmap = Bc250D3D10ResourceUnmap;
    pDeviceFuncs10->pfnSetConstantBufferOffset = Bc250D3D10SetConstantBufferOffset;
    pDeviceFuncs10->pfnSetConstantBuffersScaled = Bc250D3D10SetConstantBuffersScaled;

    return S_OK;
}UCCESS; }

VOID APIENTRY Bc250DxgiDestroyDevice(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

NTSTATUS APIENTRY Bc250DxgiPresent(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENT* pPresent
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pPresent); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiGetScanLine(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETSCANLINE* pGetScanLine
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pGetScanLine); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiSetResourcePriority(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETRESOURCEPRIORITY* pSetResourcePriority
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pSetResourcePriority); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiQueryResourceResidency(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_QUERYRESOURCERESIDENCY* pQueryResourceResidency
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pQueryResourceResidency); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiRotatePresent(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_ROTATEPRESENT* pRotatePresent
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pRotatePresent); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiSetDisplayMode(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETDISPLAYMODE* pSetDisplayMode
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pSetDisplayMode); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiSetGamma(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETGAMMA* pSetGamma
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pSetGamma); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiSetVidPnSourceAddress(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pSetVidPnSourceAddress); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiWaitForVerticalBlank(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_WAITFORVERTICALBLANK* pWaitForVerticalBlank
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pWaitForVerticalBlank); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiOfferResources(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_OFFERRESOURCES* pOfferResources
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pOfferResources); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiReclaimResources(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_RECLAIMRESOURCES* pReclaimResources
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pReclaimResources); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiGetDeviceRemovalReason(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETDEVICEREMOVALREASON* pGetDeviceRemovalReason
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pGetDeviceRemovalReason); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiBlt(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_BLT* pBlt
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pBlt); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiResolveSharedResource(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pResolveSharedResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pResolveSharedResource); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiGetForcedPresentInterval(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETFORCEDPRESENTINTERVAL* pGetForcedPresentInterval
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pGetForcedPresentInterval); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiPresentMultiPlaneOverlay(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMultiPlaneOverlay
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pPresentMultiPlaneOverlay); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiGetMultiPlaneOverlayCaps(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS* pGetMultiPlaneOverlayCaps
    ) { UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(pGetMultiPlaneOverlayCaps); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiCheckMultiPlaneOverlaySupport(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT* pCheckMultiPlaneOverlaySupport
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiPresentMultiPlaneOverlay2(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY2* pPresentMultiPlaneOverlay2
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pPresentMultiPlaneOverlay2); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiCheckMultiPlaneOverlaySupport2(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT2* pCheckMultiPlaneOverlaySupport2
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport2); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiPresentMultiPlaneOverlay3(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY3* pPresentMultiPlaneOverlay3
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pPresentMultiPlaneOverlay3); return STATUS_SUCCESS; }

NTSTATUS APIENTRY Bc250DxgiCheckMultiPlaneOverlaySupport3(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT3* pCheckMultiPlaneOverlaySupport3
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCheckMultiPlaneOverlaySupport3); return STATUS_SUCCESS; }

//=============================================================================
// D3D10 DDI Function Stubs
//=============================================================================

HRESULT APIENTRY Bc250D3D10CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* pCreateDevice
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    // Fill in D3D10 DDI device functions
    D3D10DDI_DEVICEFUNCS* pDeviceFuncs10 = pCreateDevice->pDeviceFuncs;
    pDeviceFuncs10->pfnCreateBlendState = Bc250D3D10CreateBlendState;
    pDeviceFuncs10->pfnDestroyBlendState = Bc250D3D10DestroyBlendState;
    pDeviceFuncs10->pfnCreateRasterizerState = Bc250D3D10CreateRasterizerState;
    pDeviceFuncs10->pfnDestroyRasterizerState = Bc250D3D10DestroyRasterizerState;
    pDeviceFuncs10->pfnCreateDepthStencilState = Bc250D3D10CreateDepthStencilState;
    pDeviceFuncs10->pfnDestroyDepthStencilState = Bc250D3D10DestroyDepthStencilState;
    pDeviceFuncs10->pfnCreateGeometryShader = Bc250D3D10CreateGeometryShader;
    pDeviceFuncs10->pfnDestroyShader = Bc250D3D10DestroyShader;
    pDeviceFuncs10->pfnCreateVertexShader = Bc250D3D10CreateVertexShader;
    pDeviceFuncs10->pfnCreatePixelShader = Bc250D3D10CreatePixelShader;
    pDeviceFuncs10->pfnCreateInputLayout = Bc250D3D10CreateInputLayout;
    pDeviceFuncs10->pfnDestroyInputLayout = Bc250D3D10DestroyInputLayout;
    pDeviceFuncs10->pfnCreateBuffer = Bc250D3D10CreateBuffer;
    pDeviceFuncs10->pfnCreateTexture2D = Bc250D3D10CreateTexture2D;
    pDeviceFuncs10->pfnDestroyResource = Bc250D3D10DestroyResource;
    pDeviceFuncs10->pfnSetViewports = Bc250D3D10SetViewports;
    pDeviceFuncs10->pfnSetScissorRects = Bc250D3D10SetScissorRects;
    pDeviceFuncs10->pfnSetBlendState = Bc250D3D10SetBlendState;
    pDeviceFuncs10->pfnSetRasterizerState = Bc250D3D10SetRasterizerState;
    pDeviceFuncs10->pfnSetDepthStencilState = Bc250D3D10SetDepthStencilState;
    pDeviceFuncs10->pfnDrawIndexed = Bc250D3D10DrawIndexed;
    pDeviceFuncs10->pfnDraw = Bc250D3D10Draw;
    pDeviceFuncs10->pfnSetVertexBuffers = Bc250D3D10SetVertexBuffers;
    pDeviceFuncs10->pfnSetIndexBuffer = Bc250D3D10SetIndexBuffer;
    pDeviceFuncs10->pfnSetInputLayout = Bc250D3D10SetInputLayout;
    pDeviceFuncs10->pfnSetVertexShader = Bc250D3D10SetVertexShader;
    pDeviceFuncs10->pfnSetPixelShader = Bc250D3D10SetPixelShader;
    pDeviceFuncs10->pfnSetGeometryShader = Bc250D3D10SetGeometryShader;
    pDeviceFuncs10->pfnSetConstantBuffers = Bc250D3D10SetConstantBuffers;
    pDeviceFuncs10->pfnSetShaderResources = Bc250D3D10SetShaderResources;
    pDeviceFuncs10->pfnSetSamplers = Bc250D3D10SetSamplers;
    pDeviceFuncs10->pfnSetRenderTargets = Bc250D3D10SetRenderTargets;
    pDeviceFuncs10->pfnClearRenderTargetView = Bc250D3D10ClearRenderTargetView;
    pDeviceFuncs10->pfnClearDepthStencilView = Bc250D3D10ClearDepthStencilView;
    pDeviceFuncs10->pfnFlush = Bc250D3D10Flush;
    pDeviceFuncs10->pfnGenMips = Bc250D3D10GenMips;
    pDeviceFuncs10->pfnResourceMap = Bc250D3D10ResourceMap;
    pDeviceFuncs10->pfnResourceUnmap = Bc250D3D10ResourceUnmap;
    pDeviceFuncs10->pfnSetConstantBufferOffset = Bc250D3D10SetConstantBufferOffset;
    pDeviceFuncs10->pfnSetConstantBuffersScaled = Bc250D3D10SetConstantBuffersScaled;

    return S_OK;
}

VOID APIENTRY Bc250D3D10DestroyDevice(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

VOID APIENTRY Bc250D3D10CreateBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEBLENDSTATE* pCreateBlendState,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ D3D10DDI_HRTBLENDSTATE hRTBlendState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateBlendState); UNREFERENCED_PARAMETER(hBlendState); UNREFERENCED_PARAMETER(hRTBlendState); }

VOID APIENTRY Bc250D3D10DestroyBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hBlendState); }

VOID APIENTRY Bc250D3D10CreateRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATERASTERIZERSTATE* pCreateRasterizerState,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    _In_ D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateRasterizerState); UNREFERENCED_PARAMETER(hRasterizerState); UNREFERENCED_PARAMETER(hRTRasterizerState); }

VOID APIENTRY Bc250D3D10DestroyRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRasterizerState); }

VOID APIENTRY Bc250D3D10CreateDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILSTATE* pCreateDepthStencilState,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateDepthStencilState); UNREFERENCED_PARAMETER(hDepthStencilState); UNREFERENCED_PARAMETER(hRTDepthStencilState); }

VOID APIENTRY Bc250D3D10DestroyDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilState); }

VOID APIENTRY Bc250D3D10CreateGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGeometryShaderWithStreamOutput
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); UNREFERENCED_PARAMETER(pCreateGeometryShaderWithStreamOutput); }

VOID APIENTRY Bc250D3D10DestroyShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D10CreateVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); }

VOID APIENTRY Bc250D3D10CreatePixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); }

VOID APIENTRY Bc250D3D10CreateInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEINPUTLAYOUT* pCreateInputLayout,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout,
    _In_ D3D10DDI_HRTINPUTLAYOUT hRTInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateInputLayout); UNREFERENCED_PARAMETER(hInputLayout); UNREFERENCED_PARAMETER(hRTInputLayout); }

VOID APIENTRY Bc250D3D10DestroyInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hInputLayout); }

VOID APIENTRY Bc250D3D10CreateBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEBUFFER* pCreateBuffer,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateBuffer); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(hRTResource); }

VOID APIENTRY Bc250D3D10CreateTexture2D(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATETEXTURE2D* pCreateTexture2D,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateTexture2D); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(hRTResource); }

VOID APIENTRY Bc250D3D10DestroyResource(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); }

VOID APIENTRY Bc250D3D10SetViewports(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumViewports,
    _In_ UINT FirstViewport,
    _In_ CONST D3D10_DDI_VIEWPORT* pViewports
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumViewports); UNREFERENCED_PARAMETER(FirstViewport); UNREFERENCED_PARAMETER(pViewports); }

VOID APIENTRY Bc250D3D10SetScissorRects(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumRects,
    _In_ UINT FirstRect,
    _In_ CONST RECT* pRects
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(FirstRect); UNREFERENCED_PARAMETER(pRects); }

VOID APIENTRY Bc250D3D10SetBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ CONST FLOAT BlendFactor[4],
    _In_ UINT SampleMask
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hBlendState); UNREFERENCED_PARAMETER(BlendFactor); UNREFERENCED_PARAMETER(SampleMask); }

VOID APIENTRY Bc250D3D10SetRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRasterizerState); }

VOID APIENTRY Bc250D3D10SetDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ UINT StencilRef
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilState); UNREFERENCED_PARAMETER(StencilRef); }

VOID APIENTRY Bc250D3D10DrawIndexed(
    _In_ CONST HANDLE hDevice,
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(IndexCount); UNREFERENCED_PARAMETER(StartIndexLocation); UNREFERENCED_PARAMETER(BaseVertexLocation); }

VOID APIENTRY Bc250D3D10Draw(
    _In_ CONST HANDLE hDevice,
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(VertexCount); UNREFERENCED_PARAMETER(StartVertexLocation); }

VOID APIENTRY Bc250D3D10SetVertexBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phVertexBuffers,
    _In_ CONST UINT* pStrides,
    _In_ CONST UINT* pOffsets
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phVertexBuffers); UNREFERENCED_PARAMETER(pStrides); UNREFERENCED_PARAMETER(pOffsets); }

VOID APIENTRY Bc250D3D10SetIndexBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hIndexBuffer); UNREFERENCED_PARAMETER(Format); UNREFERENCED_PARAMETER(Offset); }

VOID APIENTRY Bc250D3D10SetInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hInputLayout); }

VOID APIENTRY Bc250D3D10SetVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D10SetPixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D10SetGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D10SetConstantBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phConstantBuffers); }

VOID APIENTRY Bc250D3D10SetShaderResources(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartView,
    _In_ UINT NumViews,
    _In_ CONST D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartView); UNREFERENCED_PARAMETER(NumViews); UNREFERENCED_PARAMETER(phShaderResourceViews); }

VOID APIENTRY Bc250D3D10SetSamplers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSampler,
    _In_ UINT NumSamplers,
    _In_ CONST D3D10DDI_HSAMPLER* phSamplers
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartSampler); UNREFERENCED_PARAMETER(NumSamplers); UNREFERENCED_PARAMETER(phSamplers); }

VOID APIENTRY Bc250D3D10SetRenderTargets(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(phRenderTargetViews); UNREFERENCED_PARAMETER(RTVCount); UNREFERENCED_PARAMETER(hDepthStencilView); }

VOID APIENTRY Bc250D3D10ClearRenderTargetView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    _In_ CONST FLOAT ColorRGBA[4]
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRenderTargetView); UNREFERENCED_PARAMETER(ColorRGBA); }

VOID APIENTRY Bc250D3D10ClearDepthStencilView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilView); UNREFERENCED_PARAMETER(ClearFlags); UNREFERENCED_PARAMETER(Depth); UNREFERENCED_PARAMETER(Stencil); }

VOID APIENTRY Bc250D3D10Flush(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

VOID APIENTRY Bc250D3D10GenMips(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderResourceView); }

VOID APIENTRY Bc250D3D10ResourceMap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource,
    _In_ D3D10DDI_MAP MapType,
    _In_ UINT MapFlags,
    _Inout_ D3D10DDIARG_MAP* pMap
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(Subresource); UNREFERENCED_PARAMETER(MapType); UNREFERENCED_PARAMETER(MapFlags); UNREFERENCED_PARAMETER(pMap); }

VOID APIENTRY Bc250D3D10ResourceUnmap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(Subresource); }

VOID APIENTRY Bc250D3D10SetConstantBufferOffset(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hConstantBuffer,
    _In_ UINT Offset
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hConstantBuffer); UNREFERENCED_PARAMETER(Offset); }

VOID APIENTRY Bc250D3D10SetConstantBuffersScaled(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers,
    _In_ CONST UINT* pOffsets,
    _In_ CONST UINT* pSizes
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phConstantBuffers); UNREFERENCED_PARAMETER(pOffsets); UNREFERENCED_PARAMETER(pSizes); }

//=============================================================================
// D3D11 DDI Function Stubs
//=============================================================================

HRESULT APIENTRY Bc250D3D11CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D11DDIARG_CREATEDEVICE* pCreateDevice
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    // Fill in D3D11 DDI device functions
    D3D11DDI_DEVICEFUNCS* pDeviceFuncs11 = pCreateDevice->pDeviceFuncs;
    pDeviceFuncs11->pfnCreateBlendState = Bc250D3D11CreateBlendState;
    pDeviceFuncs11->pfnDestroyBlendState = Bc250D3D11DestroyBlendState;
    pDeviceFuncs11->pfnCreateRasterizerState = Bc250D3D11CreateRasterizerState;
    pDeviceFuncs11->pfnDestroyRasterizerState = Bc250D3D11DestroyRasterizerState;
    pDeviceFuncs11->pfnCreateDepthStencilState = Bc250D3D11CreateDepthStencilState;
    pDeviceFuncs11->pfnDestroyDepthStencilState = Bc250D3D11DestroyDepthStencilState;
    pDeviceFuncs11->pfnCreateGeometryShader = Bc250D3D11CreateGeometryShader;
    pDeviceFuncs11->pfnDestroyShader = Bc250D3D11DestroyShader;
    pDeviceFuncs11->pfnCreateVertexShader = Bc250D3D11CreateVertexShader;
    pDeviceFuncs11->pfnCreatePixelShader = Bc250D3D11CreatePixelShader;
    pDeviceFuncs11->pfnCreateInputLayout = Bc250D3D11CreateInputLayout;
    pDeviceFuncs11->pfnDestroyInputLayout = Bc250D3D11DestroyInputLayout;
    pDeviceFuncs11->pfnCreateBuffer = Bc250D3D11CreateBuffer;
    pDeviceFuncs11->pfnCreateTexture2D = Bc250D3D11CreateTexture2D;
    pDeviceFuncs11->pfnDestroyResource = Bc250D3D11DestroyResource;
    pDeviceFuncs11->pfnSetViewports = Bc250D3D11SetViewports;
    pDeviceFuncs11->pfnSetScissorRects = Bc250D3D11SetScissorRects;
    pDeviceFuncs11->pfnSetBlendState = Bc250D3D11SetBlendState;
    pDeviceFuncs11->pfnSetRasterizerState = Bc250D3D11SetRasterizerState;
    pDeviceFuncs11->pfnSetDepthStencilState = Bc250D3D11SetDepthStencilState;
    pDeviceFuncs11->pfnDrawIndexed = Bc250D3D11DrawIndexed;
    pDeviceFuncs11->pfnDraw = Bc250D3D11Draw;
    pDeviceFuncs11->pfnSetVertexBuffers = Bc250D3D11SetVertexBuffers;
    pDeviceFuncs11->pfnSetIndexBuffer = Bc250D3D11SetIndexBuffer;
    pDeviceFuncs11->pfnSetInputLayout = Bc250D3D11SetInputLayout;
    pDeviceFuncs11->pfnSetVertexShader = Bc250D3D11SetVertexShader;
    pDeviceFuncs11->pfnSetPixelShader = Bc250D3D11SetPixelShader;
    pDeviceFuncs11->pfnSetGeometryShader = Bc250D3D11SetGeometryShader;
    pDeviceFuncs11->pfnSetConstantBuffers = Bc250D3D11SetConstantBuffers;
    pDeviceFuncs11->pfnSetShaderResources = Bc250D3D11SetShaderResources;
    pDeviceFuncs11->pfnSetSamplers = Bc250D3D11SetSamplers;
    pDeviceFuncs11->pfnSetRenderTargets = Bc250D3D11SetRenderTargets;
    pDeviceFuncs11->pfnClearRenderTargetView = Bc250D3D11ClearRenderTargetView;
    pDeviceFuncs11->pfnClearDepthStencilView = Bc250D3D11ClearDepthStencilView;
    pDeviceFuncs11->pfnFlush = Bc250D3D11Flush;
    pDeviceFuncs11->pfnGenMips = Bc250D3D11GenMips;
    pDeviceFuncs11->pfnResourceMap = Bc250D3D11ResourceMap;
    pDeviceFuncs11->pfnResourceUnmap = Bc250D3D11ResourceUnmap;
    pDeviceFuncs11->pfnSetConstantBufferOffset = Bc250D3D11SetConstantBufferOffset;
    pDeviceFuncs11->pfnSetConstantBuffersScaled = Bc250D3D11SetConstantBuffersScaled;
    pDeviceFuncs11->pfnCreateComputeShader = Bc250D3D11CreateComputeShader;
    pDeviceFuncs11->pfnSetComputeShader = Bc250D3D11SetComputeShader;
    pDeviceFuncs11->pfnDispatch = Bc250D3D11Dispatch;
    pDeviceFuncs11->pfnCreateResourceView = Bc250D3D11CreateResourceView;
    pDeviceFuncs11->pfnDestroyResourceView = Bc250D3D11DestroyResourceView;
    pDeviceFuncs11->pfnCreateUnorderedAccessView = Bc250D3D11CreateUnorderedAccessView;
    pDeviceFuncs11->pfnDestroyUnorderedAccessView = Bc250D3D11DestroyUnorderedAccessView;
    pDeviceFuncs11->pfnSetUnorderedAccessViews = Bc250D3D11SetUnorderedAccessViews;
    pDeviceFuncs11->pfnSetRenderTargetsScaled = Bc250D3D11SetRenderTargetsScaled;

    return S_OK;
}

VOID APIENTRY Bc250D3D11DestroyDevice(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

VOID APIENTRY Bc250D3D11CreateBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEBLENDSTATE* pCreateBlendState,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ D3D10DDI_HRTBLENDSTATE hRTBlendState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateBlendState); UNREFERENCED_PARAMETER(hBlendState); UNREFERENCED_PARAMETER(hRTBlendState); }

VOID APIENTRY Bc250D3D11DestroyBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hBlendState); }

VOID APIENTRY Bc250D3D11CreateRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATERASTERIZERSTATE* pCreateRasterizerState,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    _In_ D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateRasterizerState); UNREFERENCED_PARAMETER(hRasterizerState); UNREFERENCED_PARAMETER(hRTRasterizerState); }

VOID APIENTRY Bc250D3D11DestroyRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRasterizerState); }

VOID APIENTRY Bc250D3D11CreateDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEDEPTHSTENCILSTATE* pCreateDepthStencilState,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateDepthStencilState); UNREFERENCED_PARAMETER(hDepthStencilState); UNREFERENCED_PARAMETER(hRTDepthStencilState); }

VOID APIENTRY Bc250D3D11DestroyDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilState); }

VOID APIENTRY Bc250D3D11CreateGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGeometryShaderWithStreamOutput
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); UNREFERENCED_PARAMETER(pCreateGeometryShaderWithStreamOutput); }

VOID APIENTRY Bc250D3D11DestroyShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D11CreateVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); }

VOID APIENTRY Bc250D3D11CreatePixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); }

VOID APIENTRY Bc250D3D11CreateInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEINPUTLAYOUT* pCreateInputLayout,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout,
    _In_ D3D10DDI_HRTINPUTLAYOUT hRTInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateInputLayout); UNREFERENCED_PARAMETER(hInputLayout); UNREFERENCED_PARAMETER(hRTInputLayout); }

VOID APIENTRY Bc250D3D11DestroyInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hInputLayout); }

VOID APIENTRY Bc250D3D11CreateBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEBUFFER* pCreateBuffer,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateBuffer); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(hRTResource); }

VOID APIENTRY Bc250D3D11CreateTexture2D(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATETEXTURE2D* pCreateTexture2D,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateTexture2D); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(hRTResource); }

VOID APIENTRY Bc250D3D11DestroyResource(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); }

VOID APIENTRY Bc250D3D11SetViewports(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumViewports,
    _In_ UINT FirstViewport,
    _In_ CONST D3D10_DDI_VIEWPORT* pViewports
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumViewports); UNREFERENCED_PARAMETER(FirstViewport); UNREFERENCED_PARAMETER(pViewports); }

VOID APIENTRY Bc250D3D11SetScissorRects(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumRects,
    _In_ UINT FirstRect,
    _In_ CONST RECT* pRects
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(FirstRect); UNREFERENCED_PARAMETER(pRects); }

VOID APIENTRY Bc250D3D11SetBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ CONST FLOAT BlendFactor[4],
    _In_ UINT SampleMask
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hBlendState); UNREFERENCED_PARAMETER(BlendFactor); UNREFERENCED_PARAMETER(SampleMask); }

VOID APIENTRY Bc250D3D11SetRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRasterizerState); }

VOID APIENTRY Bc250D3D11SetDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ UINT StencilRef
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilState); UNREFERENCED_PARAMETER(StencilRef); }

VOID APIENTRY Bc250D3D11DrawIndexed(
    _In_ CONST HANDLE hDevice,
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(IndexCount); UNREFERENCED_PARAMETER(StartIndexLocation); UNREFERENCED_PARAMETER(BaseVertexLocation); }

VOID APIENTRY Bc250D3D11Draw(
    _In_ CONST HANDLE hDevice,
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(VertexCount); UNREFERENCED_PARAMETER(StartVertexLocation); }

VOID APIENTRY Bc250D3D11SetVertexBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phVertexBuffers,
    _In_ CONST UINT* pStrides,
    _In_ CONST UINT* pOffsets
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phVertexBuffers); UNREFERENCED_PARAMETER(pStrides); UNREFERENCED_PARAMETER(pOffsets); }

VOID APIENTRY Bc250D3D11SetIndexBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hIndexBuffer); UNREFERENCED_PARAMETER(Format); UNREFERENCED_PARAMETER(Offset); }

VOID APIENTRY Bc250D3D11SetInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hInputLayout); }

VOID APIENTRY Bc250D3D11SetVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D11SetPixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D11SetGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D11SetConstantBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phConstantBuffers); }

VOID APIENTRY Bc250D3D11SetShaderResources(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartView,
    _In_ UINT NumViews,
    _In_ CONST D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartView); UNREFERENCED_PARAMETER(NumViews); UNREFERENCED_PARAMETER(phShaderResourceViews); }

VOID APIENTRY Bc250D3D11SetSamplers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSampler,
    _In_ UINT NumSamplers,
    _In_ CONST D3D10DDI_HSAMPLER* phSamplers
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartSampler); UNREFERENCED_PARAMETER(NumSamplers); UNREFERENCED_PARAMETER(phSamplers); }

VOID APIENTRY Bc250D3D11SetRenderTargets(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(phRenderTargetViews); UNREFERENCED_PARAMETER(RTVCount); UNREFERENCED_PARAMETER(hDepthStencilView); }

VOID APIENTRY Bc250D3D11ClearRenderTargetView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    _In_ CONST FLOAT ColorRGBA[4]
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRenderTargetView); UNREFERENCED_PARAMETER(ColorRGBA); }

VOID APIENTRY Bc250D3D11ClearDepthStencilView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDepthStencilView); UNREFERENCED_PARAMETER(ClearFlags); UNREFERENCED_PARAMETER(Depth); UNREFERENCED_PARAMETER(Stencil); }

VOID APIENTRY Bc250D3D11Flush(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

VOID APIENTRY Bc250D3D11GenMips(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderResourceView); }

VOID APIENTRY Bc250D3D11ResourceMap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource,
    _In_ D3D10DDI_MAP MapType,
    _In_ UINT MapFlags,
    _Inout_ D3D10DDIARG_MAP* pMap
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(Subresource); UNREFERENCED_PARAMETER(MapType); UNREFERENCED_PARAMETER(MapFlags); UNREFERENCED_PARAMETER(pMap); }

VOID APIENTRY Bc250D3D11ResourceUnmap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); UNREFERENCED_PARAMETER(Subresource); }

VOID APIENTRY Bc250D3D11SetConstantBufferOffset(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hConstantBuffer,
    _In_ UINT Offset
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hConstantBuffer); UNREFERENCED_PARAMETER(Offset); }

VOID APIENTRY Bc250D3D11SetConstantBuffersScaled(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers,
    _In_ CONST UINT* pOffsets,
    _In_ CONST UINT* pSizes
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartBuffer); UNREFERENCED_PARAMETER(NumBuffers); UNREFERENCED_PARAMETER(phConstantBuffers); UNREFERENCED_PARAMETER(pOffsets); UNREFERENCED_PARAMETER(pSizes); }

VOID APIENTRY Bc250D3D11CreateComputeShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pShaderCode); UNREFERENCED_PARAMETER(hShader); UNREFERENCED_PARAMETER(hRTShader); }

VOID APIENTRY Bc250D3D11SetComputeShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShader); }

VOID APIENTRY Bc250D3D11Dispatch(
    _In_ CONST HANDLE hDevice,
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(ThreadGroupCountX); UNREFERENCED_PARAMETER(ThreadGroupCountY); UNREFERENCED_PARAMETER(ThreadGroupCountZ); }

VOID APIENTRY Bc250D3D11CreateResourceView(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATERESOURCEVIEW* pCreateResourceView,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    _In_ D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateResourceView); UNREFERENCED_PARAMETER(hShaderResourceView); UNREFERENCED_PARAMETER(hRTShaderResourceView); }

VOID APIENTRY Bc250D3D11DestroyResourceView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderResourceView); }

VOID APIENTRY Bc250D3D11CreateUnorderedAccessView(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView,
    _In_ D3D11DDI_HRTUNORDEREDACCESSVIEW hRTUnorderedAccessView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateUnorderedAccessView); UNREFERENCED_PARAMETER(hUnorderedAccessView); UNREFERENCED_PARAMETER(hRTUnorderedAccessView); }

VOID APIENTRY Bc250D3D11DestroyUnorderedAccessView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hUnorderedAccessView); }

VOID APIENTRY Bc250D3D11SetUnorderedAccessViews(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_ CONST D3D11DDI_HUNORDEREDACCESSVIEW* phUnorderedAccessViews,
    _In_ CONST UINT* pUAVInitialCounts
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartSlot); UNREFERENCED_PARAMETER(NumViews); UNREFERENCED_PARAMETER(phUnorderedAccessViews); UNREFERENCED_PARAMETER(pUAVInitialCounts); }

VOID APIENTRY Bc250D3D11SetRenderTargetsScaled(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    _In_ CONST D3D11DDIARG_SETRENDERTARGETS_SCALED* pSetRenderTargetsScaled
    ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(phRenderTargetViews); UNREFERENCED_PARAMETER(RTVCount); UNREFERENCED_PARAMETER(hDepthStencilView); UNREFERENCED_PARAMETER(pSetRenderTargetsScaled); }

//=============================================================================
// D3D12 DDI Function Stubs
//=============================================================================

HRESULT APIENTRY Bc250D3D12CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D12DDIARG_CREATEDEVICE* pCreateDevice
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    // Fill in D3D12 DDI device functions
    D3D12DDI_DEVICE_FUNCS_CORE_0070* pDeviceFuncs12 = pCreateDevice->pDeviceFuncs;

    pDeviceFuncs12->pfnSetCommandList = Bc250D3D12SetCommandList;
    pDeviceFuncs12->pfnCreateCommandQueue = Bc250D3D12CreateCommandQueue;
    pDeviceFuncs12->pfnDestroyCommandQueue = Bc250D3D12DestroyCommandQueue;
    pDeviceFuncs12->pfnCreateCommandList = Bc250D3D12CreateCommandList;
    pDeviceFuncs12->pfnDestroyCommandList = Bc250D3D12DestroyCommandList;
    pDeviceFuncs12->pfnCloseCommandList = Bc250D3D12CloseCommandList;
    pDeviceFuncs12->pfnExecuteCommandLists = Bc250D3D12ExecuteCommandLists;
    pDeviceFuncs12->pfnCreateFence = Bc250D3D12CreateFence;
    pDeviceFuncs12->pfnDestroyFence = Bc250D3D12DestroyFence;
    pDeviceFuncs12->pfnSetFenceValue = Bc250D3D12SetFenceValue;
    pDeviceFuncs12->pfnWaitForFence = Bc250D3D12WaitForFence;
    pDeviceFuncs12->pfnMakeResident = Bc250D3D12MakeResident;
    pDeviceFuncs12->pfnEvict = Bc250D3D12Evict;
    pDeviceFuncs12->pfnCreateHeap = Bc250D3D12CreateHeap;
    pDeviceFuncs12->pfnDestroyHeap = Bc250D3D12DestroyHeap;
    pDeviceFuncs12->pfnCreateResource = Bc250D3D12CreateResource;
    pDeviceFuncs12->pfnDestroyResource = Bc250D3D12DestroyResource;
    pDeviceFuncs12->pfnCreateCommandSignature = Bc250D3D12CreateCommandSignature;
    pDeviceFuncs12->pfnDestroyCommandSignature = Bc250D3D12DestroyCommandSignature;
    pDeviceFuncs12->pfnCreatePipelineState = Bc250D3D12CreatePipelineState;
    pDeviceFuncs12->pfnDestroyPipelineState = Bc250D3D12DestroyPipelineState;
    pDeviceFuncs12->pfnCreateRootSignature = Bc250D3D12CreateRootSignature;
    pDeviceFuncs12->pfnDestroyRootSignature = Bc250D3D12DestroyRootSignature;
    pDeviceFuncs12->pfnCreateDescriptorHeap = Bc250D3D12CreateDescriptorHeap;
    pDeviceFuncs12->pfnDestroyDescriptorHeap = Bc250D3D12DestroyDescriptorHeap;
    pDeviceFuncs12->pfnCreateShaderResourceView = Bc250D3D12CreateShaderResourceView;
    pDeviceFuncs12->pfnCreateConstantBufferView = Bc250D3D12CreateConstantBufferView;
    pDeviceFuncs12->pfnCreateSampler = Bc250D3D12CreateSampler;
    pDeviceFuncs12->pfnCreateUnorderedAccessView = Bc250D3D12CreateUnorderedAccessView;
    pDeviceFuncs12->pfnCreateRenderTargetView = Bc250D3D12CreateRenderTargetView;
    pDeviceFuncs12->pfnCreateDepthStencilView = Bc250D3D12CreateDepthStencilView;
    pDeviceFuncs12->pfnSetDescriptorHeaps = Bc250D3D12SetDescriptorHeaps;
    pDeviceFuncs12->pfnSetGraphicsRootSignature = Bc250D3D12SetGraphicsRootSignature;
    pDeviceFuncs12->pfnSetComputeRootSignature = Bc250D3D12SetComputeRootSignature;
    pDeviceFuncs12->pfnSetPipelineState = Bc250D3D12SetPipelineState;
    pDeviceFuncs12->pfnSetGraphicsRootDescriptorTable = Bc250D3D12SetGraphicsRootDescriptorTable;
    pDeviceFuncs12->pfnSetComputeRootDescriptorTable = Bc250D3D12SetComputeRootDescriptorTable;
    pDeviceFuncs12->pfnSetGraphicsRoot32BitConstant = Bc250D3D12SetGraphicsRoot32BitConstant;
    pDeviceFuncs12->pfnSetComputeRoot32BitConstant = Bc250D3D12SetComputeRoot32BitConstant;
    pDeviceFuncs12->pfnSetGraphicsRoot32BitConstants = Bc250D3D12SetGraphicsRoot32BitConstants;
    pDeviceFuncs12->pfnSetComputeRoot32BitConstants = Bc250D3D12SetComputeRoot32BitConstants;
    pDeviceFuncs12->pfnSetGraphicsRootConstantBufferView = Bc250D3D12SetGraphicsRootConstantBufferView;
    pDeviceFuncs12->pfnSetComputeRootConstantBufferView = Bc250D3D12SetComputeRootConstantBufferView;
    pDeviceFuncs12->pfnSetGraphicsRootShaderResourceView = Bc250D3D12SetGraphicsRootShaderResourceView;
    pDeviceFuncs12->pfnSetComputeRootShaderResourceView = Bc250D3D12SetComputeRootShaderResourceView;
    pDeviceFuncs12->pfnSetGraphicsRootUnorderedAccessView = Bc250D3D12SetGraphicsRootUnorderedAccessView;
    pDeviceFuncs12->pfnSetComputeRootUnorderedAccessView = Bc250D3D12SetComputeRootUnorderedAccessView;
    pDeviceFuncs12->pfnIASetPrimitiveTopology = Bc250D3D12IASetPrimitiveTopology;
    pDeviceFuncs12->pfnIASetVertexBuffers = Bc250D3D12IASetVertexBuffers;
    pDeviceFuncs12->pfnIASetIndexBuffer = Bc250D3D12IASetIndexBuffer;
    pDeviceFuncs12->pfnDrawInstanced = Bc250D3D12DrawInstanced;
    pDeviceFuncs12->pfnDrawIndexedInstanced = Bc250D3D12DrawIndexedInstanced;
    pDeviceFuncs12->pfnDispatch = Bc250D3D12Dispatch;
    pDeviceFuncs12->pfnResourceBarrier = Bc250D3D12ResourceBarrier;
    pDeviceFuncs12->pfnClearRenderTargetView = Bc250D3D12ClearRenderTargetView;
    pDeviceFuncs12->pfnClearDepthStencilView = Bc250D3D12ClearDepthStencilView;
    pDeviceFuncs12->pfnPresent = Bc250D3D12Present;
    pDeviceFuncs12->pfnSetViewport = Bc250D3D12SetViewport;
    pDeviceFuncs12->pfnSetScissorRect = Bc250D3D12SetScissorRect;
    pDeviceFuncs12->pfnSetBlendFactor = Bc250D3D12SetBlendFactor;
    pDeviceFuncs12->pfnSetStencilRef = Bc250D3D12SetStencilRef;

    return S_OK;
}

VOID APIENTRY Bc250D3D12DestroyDevice(
    _In_ CONST HANDLE hDevice
    ) { UNREFERENCED_PARAMETER(hDevice); }

//=============================================================================
// D3D12 DDI Function Stubs
//
// IMPORTANT: These are placeholder implementations (stubs) only.
// A full implementation of D3D12 DDI requires deep knowledge of the RDNA2
// architecture and extensive graphics hardware programming. This is beyond
// the scope of an automated AI agent. Each function would need to be
// implemented to translate D3D12 commands into GPU-specific instructions
// and manage GPU resources (memory, command buffers, shaders, etc.).
//=============================================================================

// D3D12 DDI Function Stubs (placeholder implementations)

VOID APIENTRY Bc250D3D12SetCommandList(HANDLE hDevice, HANDLE hCommandList) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hCommandList); }
VOID APIENTRY Bc250D3D12CreateCommandQueue(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDQUEUE* pCreateCommandQueue, D3D12DDI_HCOMMANDQUEUE hCommandQueue) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateCommandQueue); UNREFERENCED_PARAMETER(hCommandQueue); }
VOID APIENTRY Bc250D3D12DestroyCommandQueue(HANDLE hDevice, D3D12DDI_HCOMMANDQUEUE hCommandQueue) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hCommandQueue); }
VOID APIENTRY Bc250D3D12CreateCommandList(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDLIST* pCreateCommandList, D3D12DDI_HCOMMANDLIST hCommandList) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateCommandList); UNREFERENCED_PARAMETER(hCommandList); }
VOID APIENTRY Bc250D3D12DestroyCommandList(HANDLE hDevice, D3D12DDI_HCOMMANDLIST hCommandList) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hCommandList); }
VOID APIENTRY Bc250D3D12CloseCommandList(HANDLE hDevice, D3D12DDI_HCOMMANDLIST hCommandList) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hCommandList); }
VOID APIENTRY Bc250D3D12ExecuteCommandLists(HANDLE hDevice, UINT NumCommandLists, CONST D3D12DDI_HCOMMANDLIST* phCommandLists) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumCommandLists); UNREFERENCED_PARAMETER(phCommandLists); }
VOID APIENTRY Bc250D3D12CreateFence(HANDLE hDevice, CONST D3D12DDIARG_CREATEFENCE* pCreateFence, D3D12DDI_HFENCE hFence) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateFence); UNREFERENCED_PARAMETER(hFence); }
VOID APIENTRY Bc250D3D12DestroyFence(HANDLE hDevice, D3D12DDI_HFENCE hFence) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hFence); }
VOID APIENTRY Bc250D3D12SetFenceValue(HANDLE hDevice, D3D12DDI_HFENCE hFence, UINT64 Value) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hFence); UNREFERENCED_PARAMETER(Value); }
VOID APIENTRY Bc250D3D12WaitForFence(HANDLE hDevice, D3D12DDI_HFENCE hFence, UINT64 Value) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hFence); UNREFERENCED_PARAMETER(Value); }
VOID APIENTRY Bc250D3D12MakeResident(HANDLE hDevice, UINT NumObjects, CONST D3D12DDI_HANDLE* phObjects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumObjects); UNREFERENCED_PARAMETER(phObjects); }
VOID APIENTRY Bc250D3D12Evict(HANDLE hDevice, UINT NumObjects, CONST D3D12DDI_HANDLE* phObjects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumObjects); UNREFERENCED_PARAMETER(phObjects); }
VOID APIENTRY Bc250D3D12CreateHeap(HANDLE hDevice, CONST D3D12DDIARG_CREATEHEAP* pCreateHeap, D3D12DDI_HHEAP hHeap) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateHeap); UNREFERENCED_PARAMETER(hHeap); }
VOID APIENTRY Bc250D3D12DestroyHeap(HANDLE hDevice, D3D12DDI_HHEAP hHeap) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hHeap); }
VOID APIENTRY Bc250D3D12CreateResource(HANDLE hDevice, CONST D3D12DDIARG_CREATERESOURCE* pCreateResource, D3D12DDI_HRESOURCE hResource) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateResource); UNREFERENCED_PARAMETER(hResource); }
VOID APIENTRY Bc250D3D12DestroyResource(HANDLE hDevice, D3D12DDI_HRESOURCE hResource) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hResource); }
VOID APIENTRY Bc250D3D12CreateCommandSignature(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDSIGNATURE* pCreateCommandSignature, D3D12DDI_HCOMMANDSIGNATURE hCommandSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateCommandSignature); UNREFERENCED_PARAMETER(hCommandSignature); }
VOID APIENTRY Bc250D3D12DestroyCommandSignature(HANDLE hDevice, D3D12DDI_HCOMMANDSIGNATURE hCommandSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hCommandSignature); }
VOID APIENTRY Bc250D3D12CreatePipelineState(HANDLE hDevice, CONST D3D12DDIARG_CREATEPIPELINESTATE* pCreatePipelineState, D3D12DDI_HPIPELINESTATE hPipelineState) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreatePipelineState); UNREFERENCED_PARAMETER(hPipelineState); }
VOID APIENTRY Bc250D3D12DestroyPipelineState(HANDLE hDevice, D3D12DDI_HPIPELINESTATE hPipelineState) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hPipelineState); }
VOID APIENTRY Bc250D3D12CreateRootSignature(HANDLE hDevice, CONST D3D12DDIARG_CREATEROOTSINGATURE* pCreateRootSignature, D3D12DDI_HROOTSIGNATURE hRootSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateRootSignature); UNREFERENCED_PARAMETER(hRootSignature); }
VOID APIENTRY Bc250D3D12DestroyRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRootSignature); }
VOID APIENTRY Bc250D3D12CreateDescriptorHeap(HANDLE hDevice, CONST D3D12DDIARG_CREATEDESCRIPTORHEAP* pCreateDescriptorHeap, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateDescriptorHeap); UNREFERENCED_PARAMETER(hDescriptorHeap); }
VOID APIENTRY Bc250D3D12DestroyDescriptorHeap(HANDLE hDevice, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hDescriptorHeap); }
VOID APIENTRY Bc250D3D12CreateShaderResourceView(HANDLE hDevice, CONST D3D12DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateShaderResourceView); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12CreateConstantBufferView(HANDLE hDevice, CONST D3D12DDIARG_CREATECONSTANTBUFFERVIEW* pCreateConstantBufferView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateConstantBufferView); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12CreateSampler(HANDLE hDevice, CONST D3D12DDIARG_CREATESAMPLER* pCreateSampler, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateSampler); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12CreateUnorderedAccessView(HANDLE hDevice, CONST D3D12DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateUnorderedAccessView); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12CreateRenderTargetView(HANDLE hDevice, CONST D3D12DDIARG_CREATERENDERTARGETVIEW* pCreateRenderTargetView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateRenderTargetView); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12CreateDepthStencilView(HANDLE hDevice, CONST D3D12DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pCreateDepthStencilView); UNREFERENCED_PARAMETER(hDescriptorHeap); UNREFERENCED_PARAMETER(Offset); }
VOID APIENTRY Bc250D3D12SetDescriptorHeaps(HANDLE hDevice, UINT NumDescriptorHeaps, CONST D3D12DDI_HDESCRIPTORHEAP* phDescriptorHeaps) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumDescriptorHeaps); UNREFERENCED_PARAMETER(phDescriptorHeaps); }
VOID APIENTRY Bc250D3D12SetGraphicsRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRootSignature); }
VOID APIENTRY Bc250D3D12SetComputeRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hRootSignature); }
VOID APIENTRY Bc250D3D12SetPipelineState(HANDLE hDevice, D3D12DDI_HPIPELINESTATE hPipelineState) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hPipelineState); }
VOID APIENTRY Bc250D3D12SetGraphicsRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BaseDescriptor); }
VOID APIENTRY Bc250D3D12SetComputeRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BaseDescriptor); }
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Value); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Value); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Num32BitValuesToSet); UNREFERENCED_PARAMETER(pSrcData); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Num32BitValuesToSet); UNREFERENCED_PARAMETER(pSrcData); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetGraphicsRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetGraphicsRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetGraphicsRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12IASetPrimitiveTopology(HANDLE hDevice, D3D12DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(PrimitiveTopology); }
VOID APIENTRY Bc250D3D12IASetVertexBuffers(HANDLE hDevice, UINT StartSlot, UINT NumViews, CONST D3D12DDI_VERTEX_BUFFER_VIEW* pVertexBufferViews) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartSlot); UNREFERENCED_PARAMETER(NumViews); UNREFERENCED_PARAMETER(pVertexBufferViews); }
VOID APIENTRY Bc250D3D12IASetIndexBuffer(HANDLE hDevice, CONST D3D12DDI_INDEX_BUFFER_VIEW* pIndexBufferView) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pIndexBufferView); }
VOID APIENTRY Bc250D3D12DrawInstanced(HANDLE hDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(VertexCountPerInstance); UNREFERENCED_PARAMETER(InstanceCount); UNREFERENCED_PARAMETER(StartVertexLocation); UNREFERENCED_PARAMETER(StartInstanceLocation); }
VOID APIENTRY Bc250D3D12DrawIndexedInstanced(HANDLE hDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(IndexCountPerInstance); UNREFERENCED_PARAMETER(InstanceCount); UNREFERENCED_PARAMETER(StartIndexLocation); UNREFERENCED_PARAMETER(BaseVertexLocation); UNREFERENCED_PARAMETER(StartInstanceLocation); }
VOID APIENTRY Bc250D3D12Dispatch(HANDLE hDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(ThreadGroupCountX); UNREFERENCED_PARAMETER(ThreadGroupCountY); UNREFERENCED_PARAMETER(ThreadGroupCountZ); }
VOID APIENTRY Bc250D3D12ResourceBarrier(HANDLE hDevice, UINT NumBarriers, CONST D3D12DDI_RESOURCE_BARRIER* pBarriers) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumBarriers); UNREFERENCED_PARAMETER(pBarriers); }
VOID APIENTRY Bc250D3D12ClearRenderTargetView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE RenderTargetView, CONST FLOAT ColorRGBA[4], UINT NumRects, CONST D3D12DDI_RECT* pRects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RenderTargetView); UNREFERENCED_PARAMETER(ColorRGBA); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(pRects); }
VOID APIENTRY Bc250D3D12ClearDepthStencilView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12DDI_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, CONST D3D12DDI_RECT* pRects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(DepthStencilView); UNREFERENCED_PARAMETER(ClearFlags); UNREFERENCED_PARAMETER(Depth); UNREFERENCED_PARAMETER(Stencil); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(pRects); }
VOID APIENTRY Bc250D3D12Present(HANDLE hDevice, CONST D3D12DDI_PRESENT_ARGS* pArgs) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs); }
VOID APIENTRY Bc250D3D12SetViewport(HANDLE hDevice, CONST D3D12DDI_VIEWPORT* pViewport) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pViewport); }
VOID APIENTRY Bc250D3D12SetScissorRect(HANDLE hDevice, CONST D3D12DDI_RECT* pRect) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pRect); }
VOID APIENTRY Bc250D3D12SetBlendFactor(HANDLE hDevice, CONST FLOAT BlendFactor[4]) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(BlendFactor); }
VOID APIENTRY Bc250D3D12SetStencilRef(HANDLE hDevice, UINT StencilRef) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StencilRef); }
VOID APIENTRY Bc250D3D12SetGraphicsRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BaseDescriptor); }
VOID APIENTRY Bc250D3D12SetComputeRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BaseDescriptor); }
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Value); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Value); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Num32BitValuesToSet); UNREFERENCED_PARAMETER(pSrcData); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(Num32BitValuesToSet); UNREFERENCED_PARAMETER(pSrcData); UNREFERENCED_PARAMETER(DestOffsetIn32BitValues); }
VOID APIENTRY Bc250D3D12SetGraphicsRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetGraphicsRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetGraphicsRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12SetComputeRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RootParameterIndex); UNREFERENCED_PARAMETER(BufferLocation); }
VOID APIENTRY Bc250D3D12IASetPrimitiveTopology(HANDLE hDevice, D3D12DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(PrimitiveTopology); }
VOID APIENTRY Bc250D3D12IASetVertexBuffers(HANDLE hDevice, UINT StartSlot, UINT NumViews, CONST D3D12DDI_VERTEX_BUFFER_VIEW* pVertexBufferViews) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StartSlot); UNREFERENCED_PARAMETER(NumViews); UNREFERENCED_PARAMETER(pVertexBufferViews); }
VOID APIENTRY Bc250D3D12IASetIndexBuffer(HANDLE hDevice, CONST D3D12DDI_INDEX_BUFFER_VIEW* pIndexBufferView) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pIndexBufferView); }
VOID APIENTRY Bc250D3D12DrawInstanced(HANDLE hDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(VertexCountPerInstance); UNREFERENCED_PARAMETER(InstanceCount); UNREFERENCED_PARAMETER(StartVertexLocation); UNREFERENCED_PARAMETER(StartInstanceLocation); }
VOID APIENTRY Bc250D3D12DrawIndexedInstanced(HANDLE hDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(IndexCountPerInstance); UNREFERENCED_PARAMETER(InstanceCount); UNREFERENCED_PARAMETER(StartIndexLocation); UNREFERENCED_PARAMETER(BaseVertexLocation); UNREFERENCED_PARAMETER(StartInstanceLocation); }
VOID APIENTRY Bc250D3D12Dispatch(HANDLE hDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(ThreadGroupCountX); UNREFERENCED_PARAMETER(ThreadGroupCountY); UNREFERENCED_PARAMETER(ThreadGroupCountZ); }
VOID APIENTRY Bc250D3D12ResourceBarrier(HANDLE hDevice, UINT NumBarriers, CONST D3D12DDI_RESOURCE_BARRIER* pBarriers) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumBarriers); UNREFERENCED_PARAMETER(pBarriers); }
VOID APIENTRY Bc250D3D12ClearRenderTargetView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE RenderTargetView, CONST FLOAT ColorRGBA[4], UINT NumRects, CONST D3D12DDI_RECT* pRects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(RenderTargetView); UNREFERENCED_PARAMETER(ColorRGBA); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(pRects); }
VOID APIENTRY Bc250D3D12ClearDepthStencilView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12DDI_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, CONST D3D12DDI_RECT* pRects) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(DepthStencilView); UNREFERENCED_PARAMETER(ClearFlags); UNREFERENCED_PARAMETER(Depth); UNREFERENCED_PARAMETER(Stencil); UNREFERENCED_PARAMETER(NumRects); UNREFERENCED_PARAMETER(pRects); }
VOID APIENTRY Bc250D3D12Present(HANDLE hDevice, CONST D3D12DDI_PRESENT_ARGS* pArgs) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs); }
VOID APIENTRY Bc250D3D12SetViewport(HANDLE hDevice, CONST D3D12DDI_VIEWPORT* pViewport) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pViewport); }
VOID APIENTRY Bc250D3D12SetScissorRect(HANDLE hDevice, CONST D3D12DDI_RECT* pRect) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pRect); }
VOID APIENTRY Bc250D3D12SetBlendFactor(HANDLE hDevice, CONST FLOAT BlendFactor[4]) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(BlendFactor); }
VOID APIENTRY Bc250D3D12SetStencilRef(HANDLE hDevice, UINT StencilRef) { UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(StencilRef); }
