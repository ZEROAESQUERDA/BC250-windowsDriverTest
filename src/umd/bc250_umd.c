#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dumddi.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>

#ifndef BC250_ENABLE_DX_UMD
#define BC250_ENABLE_DX_UMD 1
#endif

/*
 * DirectX UMD boundary for the BC-250.
 *
 * This DLL is active in experimental mode. It exposes the D3D9, D3D10, D3D11
 * and D3D12 adapter entry points and the minimum device tables needed for
 * bring-up with the full-WDDM KMD. Complete runtime feature tables still need
 * to be filled for broad application compatibility.
 */

typedef struct _BC250_UMD_ADAPTER {
    D3DDDI_ADAPTERCALLBACKS AdapterCallbacks;
    UINT InterfaceVersion;
} BC250_UMD_ADAPTER, *PBC250_UMD_ADAPTER;

typedef struct _BC250_UMD_DEVICE {
    PBC250_UMD_ADAPTER Adapter;
    CRITICAL_SECTION Lock;
    UINT64 LastSubmittedFence;
} BC250_UMD_DEVICE, *PBC250_UMD_DEVICE;

static HRESULT APIENTRY
Bc250UmdGetCaps(
    _In_ HANDLE Adapter,
    _In_ CONST D3DDDIARG_GETCAPS* Caps
    )
{
    UNREFERENCED_PARAMETER(Adapter);

    if (Caps == NULL) {
        return E_INVALIDARG;
    }

    if (Caps->DataSize != 0 && Caps->pData == NULL) {
        return E_INVALIDARG;
    }

    UNREFERENCED_PARAMETER(Caps);
    return E_NOTIMPL;
}

static SIZE_T APIENTRY
Bc250UmdCalcPrivateDeviceSize(
    _In_ HANDLE Adapter,
    _In_ CONST D3DDDIARG_CALCPRIVATEDEVICESIZE* Data
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Data);
    return sizeof(BC250_UMD_DEVICE);
}

static HRESULT APIENTRY
Bc250UmdDestroyDevice(
    _In_ HANDLE Device
    )
{
    PBC250_UMD_DEVICE device = (PBC250_UMD_DEVICE)Device;

    if (device == NULL) {
        return E_INVALIDARG;
    }

    DeleteCriticalSection(&device->Lock);
    HeapFree(GetProcessHeap(), 0, device);
    return S_OK;
}

static HRESULT APIENTRY
Bc250UmdCreateDevice(
    _In_ HANDLE Adapter,
    _Inout_ D3DDDIARG_CREATEDEVICE* CreateData
    )
{
    PBC250_UMD_ADAPTER adapter = (PBC250_UMD_ADAPTER)Adapter;
    PBC250_UMD_DEVICE device;

    if (adapter == NULL || CreateData == NULL || CreateData->pDeviceFuncs == NULL) {
        return E_INVALIDARG;
    }

    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(CreateData);
    return E_NOTIMPL;
}

static VOID APIENTRY
Bc250UmdDestroyDevice10(
    _In_ D3D10DDI_HDEVICE Device
    )
{
    PBC250_UMD_DEVICE device = (PBC250_UMD_DEVICE)Device.pDrvPrivate;
    if (device != NULL) {
        DeleteCriticalSection(&device->Lock);
        HeapFree(GetProcessHeap(), 0, device);
    }
}

static SIZE_T APIENTRY
Bc250UmdCalcPrivateDeviceSize10(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE* Data
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Data);
    return sizeof(BC250_UMD_DEVICE);
}

static HRESULT APIENTRY
Bc250UmdCreateDevice10(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* CreateData
    )
{
    PBC250_UMD_DEVICE device;
    UNREFERENCED_PARAMETER(Adapter);

    if (CreateData == NULL || CreateData->pDeviceFuncs == NULL) {
        return E_INVALIDARG;
    }

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(CreateData);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Bc250UmdCloseAdapter10(
    _In_ D3D10DDI_HADAPTER Adapter
    )
{
    PBC250_UMD_ADAPTER adapter = (PBC250_UMD_ADAPTER)Adapter.pDrvPrivate;
    if (adapter == NULL) {
        return E_INVALIDARG;
    }
    HeapFree(GetProcessHeap(), 0, adapter);
    return S_OK;
}

static HRESULT APIENTRY
Bc250UmdOpenAdapter10Impl(
    _Inout_ D3D10DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    PBC250_UMD_ADAPTER adapter;
    if (OpenAdapterData == NULL || OpenAdapterData->pAdapterCallbacks == NULL ||
        OpenAdapterData->pAdapterFuncs == NULL) {
        return E_INVALIDARG;
    }

    adapter = (PBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) {
        return E_OUTOFMEMORY;
    }

    adapter->AdapterCallbacks = *OpenAdapterData->pAdapterCallbacks;
    adapter->InterfaceVersion = OpenAdapterData->Interface;
    OpenAdapterData->hAdapter.pDrvPrivate = adapter;
    OpenAdapterData->pAdapterFuncs->pfnCalcPrivateDeviceSize =
        Bc250UmdCalcPrivateDeviceSize10;
    OpenAdapterData->pAdapterFuncs->pfnCreateDevice = Bc250UmdCreateDevice10;
    OpenAdapterData->pAdapterFuncs->pfnCloseAdapter = Bc250UmdCloseAdapter10;
    return S_OK;
}

static HRESULT APIENTRY
Bc250UmdOpenAdapter12Impl(
    _Inout_ D3D12DDIARG_OPENADAPTER* OpenAdapterData
    );

static HRESULT APIENTRY
Bc250UmdCloseAdapter(
    _In_ HANDLE Adapter
    )
{
    PBC250_UMD_ADAPTER adapter = (PBC250_UMD_ADAPTER)Adapter;

    if (adapter == NULL) {
        return E_INVALIDARG;
    }

    HeapFree(GetProcessHeap(), 0, adapter);
    return S_OK;
}

static HRESULT APIENTRY
Bc250UmdOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* OpenAdapter
    )
{
    PBC250_UMD_ADAPTER adapter;

    if (OpenAdapter == NULL ||
        OpenAdapter->pAdapterCallbacks == NULL ||
        OpenAdapter->pAdapterFuncs == NULL) {
        return E_INVALIDARG;
    }

    adapter = (PBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) {
        return E_OUTOFMEMORY;
    }

    adapter->AdapterCallbacks = *OpenAdapter->pAdapterCallbacks;
    adapter->InterfaceVersion = OpenAdapter->Interface;
    OpenAdapter->hAdapter.pDrvPrivate = adapter;
    OpenAdapter->pAdapterFuncs->pfnCalcPrivateDeviceSize = Bc250UmdCalcPrivateDeviceSize;
    OpenAdapter->pAdapterFuncs->pfnCreateDevice = Bc250UmdCreateDevice;
    OpenAdapter->pAdapterFuncs->pfnCloseAdapter = Bc250UmdCloseAdapter;
    OpenAdapter->pAdapterFuncs->pfnGetCaps = Bc250UmdGetCaps;
    return S_OK;
}

__declspec(dllexport) HRESULT APIENTRY
OpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* OpenAdapterData
    )
{
    return Bc250UmdOpenAdapter(OpenAdapterData);
}

__declspec(dllexport) HRESULT APIENTRY
OpenAdapter10(
    _Inout_ D3D10DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    return Bc250UmdOpenAdapter10Impl(OpenAdapterData);
}

__declspec(dllexport) HRESULT APIENTRY
OpenAdapter10_2(
    _Inout_ D3D10DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    return Bc250UmdOpenAdapter10Impl(OpenAdapterData);
}

__declspec(dllexport) HRESULT APIENTRY
OpenAdapter11(
    _Inout_ D3D11DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    return Bc250UmdOpenAdapter10Impl(
        (D3D10DDIARG_OPENADAPTER*)OpenAdapterData);
}

__declspec(dllexport) HRESULT APIENTRY
OpenAdapter12(
    _Inout_ D3D12DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    return Bc250UmdOpenAdapter12Impl(OpenAdapterData);
}

static SIZE_T APIENTRY
Bc250UmdCalcPrivateDeviceSize12(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE* Data
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Data);
    return sizeof(BC250_UMD_DEVICE);
}

static HRESULT APIENTRY
Bc250UmdCreateDevice12(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ CONST D3D12DDIARG_CREATEDEVICE_0003* CreateData
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(CreateData);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Bc250UmdGetCaps12(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ CONST D3D12DDIARG_GETCAPS* Caps
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    if (Caps == NULL || (Caps->DataSize != 0 && Caps->pData == NULL)) {
        return E_INVALIDARG;
    }
    UNREFERENCED_PARAMETER(Caps);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Bc250UmdGetSupportedVersions12(
    _In_ D3D12DDI_HADAPTER Adapter,
    _Inout_ UINT32* EntryCount,
    _Out_writes_opt_(*EntryCount) UINT64* Versions
    )
{
    UNREFERENCED_PARAMETER(Adapter);
    if (EntryCount == NULL) {
        return E_INVALIDARG;
    }
    UNREFERENCED_PARAMETER(Versions);
    *EntryCount = 0;
    return E_NOTIMPL;
}

static VOID APIENTRY
Bc250UmdDestroyDevice12(
    _In_ D3D12DDI_HDEVICE Device
    )
{
    UNREFERENCED_PARAMETER(Device);
}

static HRESULT APIENTRY
Bc250UmdCloseAdapter12(
    _In_ D3D12DDI_HADAPTER Adapter
    )
{
    PBC250_UMD_ADAPTER adapter = (PBC250_UMD_ADAPTER)Adapter.pDrvPrivate;
    if (adapter == NULL) {
        return E_INVALIDARG;
    }
    HeapFree(GetProcessHeap(), 0, adapter);
    return S_OK;
}

static HRESULT APIENTRY
Bc250UmdOpenAdapter12Impl(
    _Inout_ D3D12DDIARG_OPENADAPTER* OpenAdapterData
    )
{
    PBC250_UMD_ADAPTER adapter;
    if (OpenAdapterData == NULL || OpenAdapterData->pAdapterCallbacks == NULL ||
        OpenAdapterData->pAdapterFuncs == NULL) {
        return E_INVALIDARG;
    }

    adapter = (PBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (adapter == NULL) {
        return E_OUTOFMEMORY;
    }

    adapter->AdapterCallbacks = *OpenAdapterData->pAdapterCallbacks;
    OpenAdapterData->hAdapter.pDrvPrivate = adapter;
    OpenAdapterData->pAdapterFuncs->pfnCalcPrivateDeviceSize =
        Bc250UmdCalcPrivateDeviceSize12;
    OpenAdapterData->pAdapterFuncs->pfnCreateDevice =
        Bc250UmdCreateDevice12;
    OpenAdapterData->pAdapterFuncs->pfnCloseAdapter =
        Bc250UmdCloseAdapter12;
    OpenAdapterData->pAdapterFuncs->pfnGetSupportedVersions =
        Bc250UmdGetSupportedVersions12;
    OpenAdapterData->pAdapterFuncs->pfnGetCaps = Bc250UmdGetCaps12;
    OpenAdapterData->pAdapterFuncs->pfnDestroyDevice =
        Bc250UmdDestroyDevice12;
    return S_OK;
}

BOOL WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved
    )
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}
