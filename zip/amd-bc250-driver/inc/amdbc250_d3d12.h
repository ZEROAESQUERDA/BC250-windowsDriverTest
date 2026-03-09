/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_d3d12.h

Abstract:
    Definitions for the Microsoft Direct3D version 12 DDI.

    This header defines the structures and function prototypes required for the
    User-Mode Display Driver (UMD) to implement Direct3D 12 functionality.

Environment:
    User mode

--*/

#ifndef _AMDBC250_D3D12_H_
#define _AMDBC250_D3D12_H_

#include <d3d12umddi.h>

// Forward declaration of the UMD device context
typedef struct _AMDBC250_UMD_DEVICE *PAMDBC250_UMD_DEVICE;

//=============================================================================
// D3D12 DDI Functions
//=============================================================================

HRESULT APIENTRY
Bc250D3D12CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D12DDIARG_CREATEDEVICE* pCreateDevice
    );

VOID APIENTRY
Bc250D3D12DestroyDevice(
    _In_ CONST HANDLE hDevice
    );

// D3D12 DDI Function Prototypes

VOID APIENTRY Bc250D3D12SetCommandList(HANDLE hDevice, HANDLE hCommandList);
VOID APIENTRY Bc250D3D12CreateCommandQueue(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDQUEUE* pCreateCommandQueue, D3D12DDI_HCOMMANDQUEUE hCommandQueue);
VOID APIENTRY Bc250D3D12DestroyCommandQueue(HANDLE hDevice, D3D12DDI_HCOMMANDQUEUE hCommandQueue);
VOID APIENTRY Bc250D3D12CreateCommandList(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDLIST* pCreateCommandList, D3D12DDI_HCOMMANDLIST hCommandList);
VOID APIENTRY Bc250D3D12DestroyCommandList(HANDLE hDevice, D3D12DDI_HCOMMANDLIST hCommandList);
VOID APIENTRY Bc250D3D12CloseCommandList(HANDLE hDevice, D3D12DDI_HCOMMANDLIST hCommandList);
VOID APIENTRY Bc250D3D12ExecuteCommandLists(HANDLE hDevice, UINT NumCommandLists, CONST D3D12DDI_HCOMMANDLIST* phCommandLists);
VOID APIENTRY Bc250D3D12CreateFence(HANDLE hDevice, CONST D3D12DDIARG_CREATEFENCE* pCreateFence, D3D12DDI_HFENCE hFence);
VOID APIENTRY Bc250D3D12DestroyFence(HANDLE hDevice, D3D12DDI_HFENCE hFence);
VOID APIENTRY Bc250D3D12SetFenceValue(HANDLE hDevice, D3D12DDI_HFENCE hFence, UINT64 Value);
VOID APIENTRY Bc250D3D12WaitForFence(HANDLE hDevice, D3D12DDI_HFENCE hFence, UINT64 Value);
VOID APIENTRY Bc250D3D12MakeResident(HANDLE hDevice, UINT NumObjects, CONST D3D12DDI_HANDLE* phObjects);
VOID APIENTRY Bc250D3D12Evict(HANDLE hDevice, UINT NumObjects, CONST D3D12DDI_HANDLE* phObjects);
VOID APIENTRY Bc250D3D12CreateHeap(HANDLE hDevice, CONST D3D12DDIARG_CREATEHEAP* pCreateHeap, D3D12DDI_HHEAP hHeap);
VOID APIENTRY Bc250D3D12DestroyHeap(HANDLE hDevice, D3D12DDI_HHEAP hHeap);
VOID APIENTRY Bc250D3D12CreateResource(HANDLE hDevice, CONST D3D12DDIARG_CREATERESOURCE* pCreateResource, D3D12DDI_HRESOURCE hResource);
VOID APIENTRY Bc250D3D12DestroyResource(HANDLE hDevice, D3D12DDI_HRESOURCE hResource);
VOID APIENTRY Bc250D3D12CreateCommandSignature(HANDLE hDevice, CONST D3D12DDIARG_CREATECOMMANDSIGNATURE* pCreateCommandSignature, D3D12DDI_HCOMMANDSIGNATURE hCommandSignature);
VOID APIENTRY Bc250D3D12DestroyCommandSignature(HANDLE hDevice, D3D12DDI_HCOMMANDSIGNATURE hCommandSignature);
VOID APIENTRY Bc250D3D12CreatePipelineState(HANDLE hDevice, CONST D3D12DDIARG_CREATEPIPELINESTATE* pCreatePipelineState, D3D12DDI_HPIPELINESTATE hPipelineState);
VOID APIENTRY Bc250D3D12DestroyPipelineState(HANDLE hDevice, D3D12DDI_HPIPELINESTATE hPipelineState);
VOID APIENTRY Bc250D3D12CreateRootSignature(HANDLE hDevice, CONST D3D12DDIARG_CREATEROOTSINGATURE* pCreateRootSignature, D3D12DDI_HROOTSIGNATURE hRootSignature);
VOID APIENTRY Bc250D3D12DestroyRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature);
VOID APIENTRY Bc250D3D12CreateDescriptorHeap(HANDLE hDevice, CONST D3D12DDIARG_CREATEDESCRIPTORHEAP* pCreateDescriptorHeap, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap);
VOID APIENTRY Bc250D3D12DestroyDescriptorHeap(HANDLE hDevice, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap);
VOID APIENTRY Bc250D3D12CreateShaderResourceView(HANDLE hDevice, CONST D3D12DDIARG_CREATESHADERRESOURCEVIEW* pCreateShaderResourceView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12CreateConstantBufferView(HANDLE hDevice, CONST D3D12DDIARG_CREATECONSTANTBUFFERVIEW* pCreateConstantBufferView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12CreateSampler(HANDLE hDevice, CONST D3D12DDIARG_CREATESAMPLER* pCreateSampler, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12CreateUnorderedAccessView(HANDLE hDevice, CONST D3D12DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12CreateRenderTargetView(HANDLE hDevice, CONST D3D12DDIARG_CREATERENDERTARGETVIEW* pCreateRenderTargetView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12CreateDepthStencilView(HANDLE hDevice, CONST D3D12DDIARG_CREATEDEPTHSTENCILVIEW* pCreateDepthStencilView, D3D12DDI_HDESCRIPTORHEAP hDescriptorHeap, UINT Offset);
VOID APIENTRY Bc250D3D12SetDescriptorHeaps(HANDLE hDevice, UINT NumDescriptorHeaps, CONST D3D12DDI_HDESCRIPTORHEAP* phDescriptorHeaps);
VOID APIENTRY Bc250D3D12SetGraphicsRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature);
VOID APIENTRY Bc250D3D12SetComputeRootSignature(HANDLE hDevice, D3D12DDI_HROOTSIGNATURE hRootSignature);
VOID APIENTRY Bc250D3D12SetPipelineState(HANDLE hDevice, D3D12DDI_HPIPELINESTATE hPipelineState);
VOID APIENTRY Bc250D3D12SetGraphicsRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
VOID APIENTRY Bc250D3D12SetComputeRootDescriptorTable(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues);
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstant(HANDLE hDevice, UINT RootParameterIndex, UINT Value, UINT DestOffsetIn32BitValues);
VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues);
VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstants(HANDLE hDevice, UINT RootParameterIndex, UINT Num32BitValuesToSet, CONST VOID* pSrcData, UINT DestOffsetIn32BitValues);
VOID APIENTRY Bc250D3D12SetGraphicsRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12SetComputeRootConstantBufferView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12SetGraphicsRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12SetComputeRootShaderResourceView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12SetGraphicsRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12SetComputeRootUnorderedAccessView(HANDLE hDevice, UINT RootParameterIndex, D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation);
VOID APIENTRY Bc250D3D12IASetPrimitiveTopology(HANDLE hDevice, D3D12DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology);
VOID APIENTRY Bc250D3D12IASetVertexBuffers(HANDLE hDevice, UINT StartSlot, UINT NumViews, CONST D3D12DDI_VERTEX_BUFFER_VIEW* pVertexBufferViews);
VOID APIENTRY Bc250D3D12IASetIndexBuffer(HANDLE hDevice, CONST D3D12DDI_INDEX_BUFFER_VIEW* pIndexBufferView);
VOID APIENTRY Bc250D3D12DrawInstanced(HANDLE hDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
VOID APIENTRY Bc250D3D12DrawIndexedInstanced(HANDLE hDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
VOID APIENTRY Bc250D3D12Dispatch(HANDLE hDevice, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
VOID APIENTRY Bc250D3D12ResourceBarrier(HANDLE hDevice, UINT NumBarriers, CONST D3D12DDI_RESOURCE_BARRIER* pBarriers);
VOID APIENTRY Bc250D3D12ClearRenderTargetView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE RenderTargetView, CONST FLOAT ColorRGBA[4], UINT NumRects, CONST D3D12DDI_RECT* pRects);
VOID APIENTRY Bc250D3D12ClearDepthStencilView(HANDLE hDevice, D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12DDI_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, CONST D3D12DDI_RECT* pRects);
VOID APIENTRY Bc250D3D12Present(HANDLE hDevice, CONST D3D12DDI_PRESENT_ARGS* pArgs);
VOID APIENTRY Bc250D3D12SetViewport(HANDLE hDevice, CONST D3D12DDI_VIEWPORT* pViewport);
VOID APIENTRY Bc250D3D12SetScissorRect(HANDLE hDevice, CONST D3D12DDI_RECT* pRect);
VOID APIENTRY Bc250D3D12SetBlendFactor(HANDLE hDevice, CONST FLOAT BlendFactor[4]);
VOID APIENTRY Bc250D3D12SetStencilRef(HANDLE hDevice, UINT StencilRef);

#endif // _AMDBC250_D3D12_H_
