#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


// Test data element count
#define TEST_DATA_COUNT     4096

// The compatible D3D12 device object
static ID3D12Device *s_device;

// The root signature for compute pipeline state object
static ID3D12RootSignature *s_computeRootSignature;

// The compute pipeline state object
static ID3D12PipelineState *s_computeState;

// The descriptor heap resource object. 
// In this sample, there're two slots in this heap. 
// The first slot stores the shader view resource descriptor, 
// and the second slot stores the unordered access view descriptor.
static ID3D12DescriptorHeap* s_heap;

// The destination buffer object with unordered access view type
static ID3D12Resource *s_dstDataBuffer;

// The source buffer object with shader source view type
static ID3D12Resource *s_srcDataBuffer;

// The intermediate buffer object used to copy the source data to the SRV buffer
static ID3D12Resource* s_uploadBuffer;

// The heap descriptor(of SRV, UAV and CBV type)  size
static size_t s_srvUavDescriptorSize;

// The command allocator object
static ID3D12CommandAllocator *s_computeAllocator;

// The command queue object
static ID3D12CommandQueue *s_computeCommandQueue;

// The command list object
static ID3D12GraphicsCommandList *s_computeCommandList;

// The function is used to amend ID3D12Resource::GetDesc bridged to COM API
static inline void GetResourceDesc(ID3D12Resource *resource, D3D12_RESOURCE_DESC *pDesc)
{
    void (WINAPI *pFunc)(ID3D12Resource*, D3D12_RESOURCE_DESC*) = (void (WINAPI *)(ID3D12Resource*, D3D12_RESOURCE_DESC*))resource->lpVtbl->GetDesc;
    pFunc(resource, pDesc);
}

// The function is used to amend ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart bridged to COM API
static inline void GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
    if (heap == NULL || pHandle == NULL)
        return;

    void (WINAPI* const pFunc)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*) = (void (WINAPI*)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*))heap->lpVtbl->GetCPUDescriptorHandleForHeapStart;
    pFunc(heap, pHandle);
}

// The function is used to amend ID3D12DescriptorHeap::GetGPUDescriptorHandleForHeapStart bridged to COM API
static inline void GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* heap, D3D12_GPU_DESCRIPTOR_HANDLE* pHandle)
{
    if (heap == NULL || pHandle == NULL)
        return;

    void (WINAPI* const pFunc)(ID3D12DescriptorHeap*, D3D12_GPU_DESCRIPTOR_HANDLE*) = (void (WINAPI*)(ID3D12DescriptorHeap*, D3D12_GPU_DESCRIPTOR_HANDLE*))heap->lpVtbl->GetGPUDescriptorHandleForHeapStart;
    pFunc(heap, pHandle);
}

//------------------------------------------------------------------------------------------------
// D3D12 exports a new method for serializing root signatures in the Windows 10 Anniversary Update.
// To help enable root signature 1.1 features when they are available and not require maintaining
// two code paths for building root signatures, this helper method reconstructs a 1.0 signature when
// 1.1 is not supported.
static inline HRESULT D3DX12SerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION MaxVersion,
    _Outptr_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    switch (MaxVersion)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        switch (pRootSignatureDesc->Version)
        {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return D3D12SerializeRootSignature(&pRootSignatureDesc->Desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);

        case D3D_ROOT_SIGNATURE_VERSION_1_1:
        {
            const D3D12_ROOT_SIGNATURE_DESC1* desc_1_1 = &pRootSignatureDesc->Desc_1_1;

            SIZE_T ParametersSize = sizeof(D3D12_ROOT_PARAMETER) * desc_1_1->NumParameters;
            void* pParameters = (ParametersSize > 0) ? HeapAlloc(GetProcessHeap(), 0, ParametersSize) : NULL;
            D3D12_ROOT_PARAMETER* pParameters_1_0 = (D3D12_ROOT_PARAMETER*)pParameters;

            for (UINT n = 0; n < desc_1_1->NumParameters; n++)
            {
                pParameters_1_0[n].ParameterType = desc_1_1->pParameters[n].ParameterType;
                pParameters_1_0[n].ShaderVisibility = desc_1_1->pParameters[n].ShaderVisibility;

                switch (desc_1_1->pParameters[n].ParameterType)
                {
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                    pParameters_1_0[n].Constants.Num32BitValues = desc_1_1->pParameters[n].Constants.Num32BitValues;
                    pParameters_1_0[n].Constants.RegisterSpace = desc_1_1->pParameters[n].Constants.RegisterSpace;
                    pParameters_1_0[n].Constants.ShaderRegister = desc_1_1->pParameters[n].Constants.ShaderRegister;
                    break;

                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                case D3D12_ROOT_PARAMETER_TYPE_UAV:
                    pParameters_1_0[n].Descriptor.RegisterSpace = desc_1_1->pParameters[n].Descriptor.RegisterSpace;
                    pParameters_1_0[n].Descriptor.ShaderRegister = desc_1_1->pParameters[n].Descriptor.ShaderRegister;
                    break;

                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                {
                    const D3D12_ROOT_DESCRIPTOR_TABLE1* table_1_1 = &desc_1_1->pParameters[n].DescriptorTable;

                    SIZE_T DescriptorRangesSize = sizeof(D3D12_DESCRIPTOR_RANGE) * table_1_1->NumDescriptorRanges;
                    void* pDescriptorRanges = (DescriptorRangesSize > 0) ? HeapAlloc(GetProcessHeap(), 0, DescriptorRangesSize) : NULL;
                    D3D12_DESCRIPTOR_RANGE* pDescriptorRanges_1_0 = (D3D12_DESCRIPTOR_RANGE*)pDescriptorRanges;

                    for (UINT x = 0; x < table_1_1->NumDescriptorRanges; x++)
                    {
                        pDescriptorRanges_1_0[x].BaseShaderRegister = table_1_1->pDescriptorRanges[x].BaseShaderRegister;
                        pDescriptorRanges_1_0[x].NumDescriptors = table_1_1->pDescriptorRanges[x].NumDescriptors;
                        pDescriptorRanges_1_0[x].OffsetInDescriptorsFromTableStart = table_1_1->pDescriptorRanges[x].OffsetInDescriptorsFromTableStart;
                        pDescriptorRanges_1_0[x].RangeType = table_1_1->pDescriptorRanges[x].RangeType;
                        pDescriptorRanges_1_0[x].RegisterSpace = table_1_1->pDescriptorRanges[x].RegisterSpace;
                    }

                    D3D12_ROOT_DESCRIPTOR_TABLE *table_1_0 = &pParameters_1_0[n].DescriptorTable;
                    table_1_0->NumDescriptorRanges = table_1_1->NumDescriptorRanges;
                    table_1_0->pDescriptorRanges = pDescriptorRanges_1_0;
                }
                }
            }

            D3D12_ROOT_SIGNATURE_DESC desc_1_0 = {
                desc_1_1->NumParameters, pParameters_1_0, desc_1_1->NumStaticSamplers, 
                desc_1_1->pStaticSamplers, desc_1_1->Flags
            };

            HRESULT hr = D3D12SerializeRootSignature(&desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);

            for (UINT n = 0; n < desc_1_0.NumParameters; n++)
            {
                if (desc_1_0.pParameters[n].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                {
                    HeapFree(GetProcessHeap(), 0, (LPVOID)pParameters_1_0[n].DescriptorTable.pDescriptorRanges);
                }
            }
            HeapFree(GetProcessHeap(), 0, pParameters);
            return hr;
        }
        }
        break;

    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        return D3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);
    }

    return E_INVALIDARG;
}

// Row-by-row memcpy
static void MemcpySubresource(
    _In_ const D3D12_MEMCPY_DEST* pDest,
    _In_ const D3D12_SUBRESOURCE_DATA* pSrc,
    SIZE_T RowSizeInBytes,
    UINT NumRows,
    UINT NumSlices)
{
    for (UINT z = 0; z < NumSlices; ++z)
    {
        BYTE* pDestSlice = (BYTE*)(pDest->pData) + pDest->SlicePitch * z;
        const BYTE* pSrcSlice = (const BYTE*)(pSrc->pData) + pSrc->SlicePitch * z;
        for (UINT y = 0; y < NumRows; ++y)
        {
            memcpy(pDestSlice + pDest->RowPitch * y,
                pSrcSlice + pSrc->RowPitch * y,
                RowSizeInBytes);
        }
    }
}

// Updates subresources, all the subresource arrays should be populated.
// This function is the C-style implementation translated from C++ style inline function in the D3DX12 library.
static size_t UpdateSubresources(
    _In_ ID3D12Device *device,
    _In_ ID3D12GraphicsCommandList *commandList,
    _In_ ID3D12Resource* pDestinationResource,
    _In_ ID3D12Resource* pIntermediate,
    UINT64 IntermediateOffset,
    _In_range_(0, 1) UINT FirstSubresource,
    _In_range_(0, 1 - FirstSubresource) UINT NumSubresources,
    _In_reads_(NumSubresources) D3D12_SUBRESOURCE_DATA* pSrcData)
{
    size_t RequiredSize = 0;
    size_t MemToAlloc = (sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
    if (MemToAlloc > SIZE_MAX)
        return 0;

    void* pMem = HeapAlloc(GetProcessHeap(), 0, MemToAlloc);
    if (pMem == NULL)
        return 0;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)pMem;
    size_t* pRowSizesInBytes = (size_t*)(pLayouts + NumSubresources);
    UINT* pNumRows = (UINT*)(pRowSizesInBytes + NumSubresources);

    D3D12_RESOURCE_DESC Desc;
    GetResourceDesc(pDestinationResource, &Desc);

    device->lpVtbl->GetCopyableFootprints(device, &Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

    // Minor validation
    D3D12_RESOURCE_DESC IntermediateDesc;
    GetResourceDesc(pIntermediate, &IntermediateDesc);

    D3D12_RESOURCE_DESC DestinationDesc;
    GetResourceDesc(pDestinationResource, &DestinationDesc);

    if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
        RequiredSize >(SIZE_T) - 1 ||
        (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
        (FirstSubresource != 0 || NumSubresources != 1)))
    {
        return 0;
    }

    BYTE* pData;
    HRESULT hr = pIntermediate->lpVtbl->Map(pIntermediate, 0, NULL, (void**)(&pData));
    if (FAILED(hr))
        return 0;

    for (UINT i = 0; i < NumSubresources; ++i)
    {
        if (pRowSizesInBytes[i] >(SIZE_T)-1) return 0;
        D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, 
                                        pLayouts[i].Footprint.RowPitch * pNumRows[i] };
        MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
    }
    pIntermediate->lpVtbl->Unmap(pIntermediate, 0, NULL);

    if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        commandList->lpVtbl->CopyBufferRegion(commandList,
            pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
    }
    else
    {
        for (UINT i = 0; i < NumSubresources; ++i)
        {
            D3D12_TEXTURE_COPY_LOCATION dst = { pDestinationResource, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            { .PlacedFootprint = i + FirstSubresource } };
            D3D12_TEXTURE_COPY_LOCATION src = { pIntermediate, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            { .PlacedFootprint = pLayouts[i] } };

            commandList->lpVtbl->CopyTextureRegion(commandList, &dst, 0, 0, 0, &src, NULL);
        }
    }
    
    HeapFree(GetProcessHeap(), 0, pMem);

    return RequiredSize;
}

// Wait for the whole command queue completed
static void SyncCommandQueue(ID3D12CommandQueue *commandQueue, ID3D12Device *device, UINT64 signalValue)
{
    ID3D12Fence *fence;
    if(device->lpVtbl->CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence)  < 0)
        return;

    // Add an instruction to the command queue to set a new fence point.  Because we 
    // are on the GPU timeline, the new fence point won't be set until the GPU finishes
    // processing all the commands prior to this Signal().
    if (commandQueue->lpVtbl->Signal(commandQueue, fence, signalValue) < 0)
        puts("Signal failed!");

    // Wait until the GPU has completed commands up to this fence point.
    HANDLE eventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (eventHandle == NULL)
    {
        puts("Failed to create event handle!");
        return;
    }

    // Fire event when GPU hits current fence.  
    if (fence->lpVtbl->SetEventOnCompletion(fence, signalValue, eventHandle) < 0)
        puts("Set event failed!");

    // Wait until the GPU hits current fence event is fired.
    WaitForSingleObject(eventHandle, INFINITE);
    CloseHandle(eventHandle);

    fence->lpVtbl->Release(fence);
}

// Create the Shader Resource View buffer object
static ID3D12Resource* CreateSRVBuffer(const void* inputData, size_t dataSize)
{
    ID3D12Resource *resultBuffer = NULL;
    HRESULT hr;

    do
    {
        D3D12_HEAP_PROPERTIES heapProperties = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
        D3D12_HEAP_PROPERTIES heapUploadProperties = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };

        D3D12_RESOURCE_DESC resourceDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, dataSize, 1, 1, 1,
            DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
        D3D12_RESOURCE_DESC uploadBufferDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, dataSize, 1, 1, 1,
            DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

        // Create the SRV buffer and make it as the copy destination.
        hr = s_device->lpVtbl->CreateCommittedResource(s_device, &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&resultBuffer);

        if (FAILED(hr))
            break;

        // Create the upload buffer and make it as the generic read intermediate.
        hr = s_device->lpVtbl->CreateCommittedResource(s_device, &heapUploadProperties, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&s_uploadBuffer);

        if (FAILED(hr))
            break;

        // Describe the data we want to copy into the SRV buffer.
        D3D12_SUBRESOURCE_DATA subResourceData = { 0 };
        subResourceData.pData = inputData;
        subResourceData.RowPitch = dataSize;
        subResourceData.SlicePitch = subResourceData.RowPitch;
        UpdateSubresources(s_device, s_computeCommandList, resultBuffer, s_uploadBuffer, 0, 0, 1, &subResourceData);

        // Insert a barrier to sync the copy operation, 
        // and transit the SRV buffer to non pixel shader resource state.
        D3D12_RESOURCE_BARRIER barrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = { resultBuffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, 
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE } };
        s_computeCommandList->lpVtbl->ResourceBarrier(s_computeCommandList, 1, &barrier);

        // Attention! None of the operations above has been executed.
        // They have just been put into the command list.
        // So the intermediate buffer s_uploadBuffer MUST NOT be released here.

        // Setup the SRV descriptor. This will be stored in the first slot of the heap.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { 0 };
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = TEST_DATA_COUNT;
        srvDesc.Buffer.StructureByteStride = sizeof(int);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        // Get the descriptor handle from the descriptor heap.
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
        GetCPUDescriptorHandleForHeapStart(s_heap, &srvHandle);

        // Create the SRV for the buffer with the descriptor handle
        s_device->lpVtbl->CreateShaderResourceView(s_device, resultBuffer, &srvDesc, srvHandle);

    } while (false);

    if (FAILED(hr))
    {
        puts("CreateSRVBuffer failed!");
        return NULL;
    }

    return resultBuffer;
}

// Create the Unordered Access View buffer object
static ID3D12Resource* CreateUAV_RWBuffer(const void* inputData, size_t dataSize)
{
    ID3D12Resource *resultBuffer = NULL;
    HRESULT hr;

    do
    {
        D3D12_HEAP_PROPERTIES heapProperties = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
        D3D12_RESOURCE_DESC resourceDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, dataSize, 1, 1, 1,
            DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };

        // Create the UAV buffer and make it in the unordered access state.
        hr = s_device->lpVtbl->CreateCommittedResource(s_device, &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&resultBuffer);

        if (FAILED(hr))
        {
            puts("Failed to create resultBuffer!");
            return NULL;
        }

        // Setup the UAV descriptor. This will be stored in the second slot of the heap.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = { 0 };
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = TEST_DATA_COUNT;
        uavDesc.Buffer.StructureByteStride = sizeof(int);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        // Get the descriptor handle from the descriptor heap.
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
        GetCPUDescriptorHandleForHeapStart(s_heap, &uavHandle);
        // It will occupy the second slot.
        uavHandle.ptr += 1 * s_srvUavDescriptorSize;

        s_device->lpVtbl->CreateUnorderedAccessView(s_device, resultBuffer, NULL, &uavDesc, uavHandle);

    } while (false);

    if (FAILED(hr))
    {
        puts("Create UAV Buffer failed!");
        return NULL;
    }

    return resultBuffer;
}

// Initialize all the necessary assets
static bool InitAssets(void)
{
    // ---- Load Pipeline ----

    // In debug mode
    ID3D12Debug *debugController;
    if(D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debugController) >= 0)
        debugController->lpVtbl->EnableDebugLayer(debugController);

    IDXGIFactory4 *factory;
    if (CreateDXGIFactory1(&IID_IDXGIFactory4, (void**)&factory) < 0)
        return false;

    // Here, we shall use a warp device
    IDXGIAdapter *warpAdapter;
    if (factory->lpVtbl->EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void**)&warpAdapter) < 0)
        return false;

    // Create the D3D12 device
    if (D3D12CreateDevice((IUnknown*)warpAdapter, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void**)&s_device) < 0)
        return false;

    // Check 4X MSAA quality support for our back buffer format.
    // All Direct3D 11 capable devices support 4X MSAA for all render 
    // target formats, so we only need to check quality support.
    // This step is optional.
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    HRESULT hr = s_device->lpVtbl->CheckFeatureSupport(s_device,
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &msQualityLevels,
        sizeof(msQualityLevels));
    if (hr < 0)
        return false;

    unsigned msaaQuality = msQualityLevels.NumQualityLevels;
    printf("msaaQuality: %u\n", msaaQuality);

    // ---- Create descriptor heaps. ----
    D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = { 0 };
    // There are two descriptors for the heap. One for SRV buffer, the other for UAV buffer
    srvUavHeapDesc.NumDescriptors = 2;
    srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = s_device->lpVtbl->CreateDescriptorHeap(s_device, &srvUavHeapDesc, &IID_ID3D12DescriptorHeap, (void**)&s_heap);
    if (FAILED(hr))
    {
        puts("Failed to create s_srvHeap!");
        return false;
    }

    s_heap->lpVtbl->SetName(s_heap, L"s_heap");
    // Get the size of each descriptor handle
    s_srvUavDescriptorSize = s_device->lpVtbl->GetDescriptorHandleIncrementSize(s_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ---- Load Assets ----

    // Create the root signatures.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = { 0 };

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if(s_device->lpVtbl->CheckFeatureSupport(s_device, D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)) < 0)
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

        // Compute root signature.
        {
            D3D12_DESCRIPTOR_RANGE1 ranges[2] = {
                {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
                {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}
            };

            D3D12_ROOT_PARAMETER1 rootParameters[2] = {
                {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = { 1, &ranges[0] }, D3D12_SHADER_VISIBILITY_ALL },
                {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = { 1, &ranges[1] }, D3D12_SHADER_VISIBILITY_ALL }
            };

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc = {
                D3D_ROOT_SIGNATURE_VERSION_1_1, 
                .Desc_1_1 = { _countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE }
            };

            ID3DBlob *signature;
            ID3DBlob *error;
            if (D3DX12SerializeVersionedRootSignature(&computeRootSignatureDesc, featureData.HighestVersion, &signature, &error) < 0)
            {
                puts("Failed to serialize versioned root signature");
                return false;
            }

            if (s_device->lpVtbl->CreateRootSignature(s_device, 0, signature->lpVtbl->GetBufferPointer(signature),
                signature->lpVtbl->GetBufferSize(signature), &IID_ID3D12RootSignature, &s_computeRootSignature) < 0)
            {
                puts("Failed to create root signature!");
                return false;
            }

            s_computeRootSignature->lpVtbl->SetName(s_computeRootSignature, L"s_computeRootSignature");
        }
    }

    // Create the pipeline states, which includes compiling and loading shaders.

    ID3DBlob* computeShader;

    // Enable better shader debugging with the graphics debugging tools.
    uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    // Load and compile the compute shader.
    // The comppute shader file 'compute.hlsl' is just located in the current working directory.
    if (D3DCompileFromFile(L"compute.hlsl", NULL, NULL, "CSMain", "cs_5_0", compileFlags, 0, &computeShader, NULL) < 0)
        return false;

    // Describe and create the compute pipeline state object (PSO).
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = { 0 };
    computePsoDesc.pRootSignature = s_computeRootSignature;
    computePsoDesc.CS = (D3D12_SHADER_BYTECODE){ computeShader->lpVtbl->GetBufferPointer(computeShader),
        computeShader->lpVtbl->GetBufferSize(computeShader) };
    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    hr = s_device->lpVtbl->CreateComputePipelineState(s_device, &computePsoDesc, &IID_ID3D12PipelineState,
        (void**)&s_computeState);
    if (FAILED(hr))
        return false;

    return true;
}

// Initialize the command list and the command queue
static bool InitComputeCommands(void)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE };
    if (s_device->lpVtbl->CreateCommandQueue(s_device, &queueDesc, &IID_ID3D12CommandQueue, (void**)&s_computeCommandQueue) < 0)
        return false;

    if (s_device->lpVtbl->CreateCommandAllocator(s_device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void**)&s_computeAllocator) < 0)
        return false;

    if (s_device->lpVtbl->CreateCommandList(s_device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_computeAllocator,
        NULL, &IID_ID3D12CommandList, (void**)&s_computeCommandList) < 0)
        return false;

    return true;
}

static int s_DataBuffer0[TEST_DATA_COUNT];

// Create the source buffer object and the destination buffer object.
// Initialize the SRV buffer object with the input buffer
static bool CreateBuffers(void)
{
    // 对数据资源做初始化
    for (int i = 0; i < TEST_DATA_COUNT; i++)
        s_DataBuffer0[i] = i + 1;

    // Create the compute shader's constant buffer.
    const uint32_t bufferSize = (uint32_t)sizeof(s_DataBuffer0);
    s_srcDataBuffer = CreateSRVBuffer(s_DataBuffer0, bufferSize);
    s_dstDataBuffer = CreateUAV_RWBuffer(NULL, bufferSize);

    return true;
}

// Do the compute operation and fetch the result
static void DoCompute(void)
{
    ID3D12Resource *readBackBuffer = NULL;
    D3D12_HEAP_PROPERTIES heapProperties = { D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
    D3D12_RESOURCE_DESC resourceDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(s_DataBuffer0), 1, 1, 1,
        DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

    // Create the read-back buffer object that will fetch the result from the UAV buffer object.
    // And make it as the copy destination.
    HRESULT hr = s_device->lpVtbl->CreateCommittedResource(s_device, &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&readBackBuffer);

    if (FAILED(hr))
        return;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    if(s_computeAllocator->lpVtbl->Reset(s_computeAllocator) < 0)
        return;

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    if(s_computeCommandList->lpVtbl->Reset(s_computeCommandList, s_computeAllocator, s_computeState) < 0)
        return;

    s_computeCommandList->lpVtbl->SetComputeRootSignature(s_computeCommandList, s_computeRootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { s_heap };
    s_computeCommandList->lpVtbl->SetDescriptorHeaps(s_computeCommandList, _countof(ppHeaps), ppHeaps);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle;
    // Get the SRV GPU descriptor handle from the descriptor heap
    GetGPUDescriptorHandleForHeapStart(s_heap, &srvHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle;
    // Get the UAV GPU descriptor handle from the descriptor heap
    GetGPUDescriptorHandleForHeapStart(s_heap, &uavHandle);
    uavHandle.ptr += 1 * s_srvUavDescriptorSize;

    s_computeCommandList->lpVtbl->SetComputeRootDescriptorTable(s_computeCommandList, 0, srvHandle);
    s_computeCommandList->lpVtbl->SetComputeRootDescriptorTable(s_computeCommandList, 1, uavHandle);

    // Dispatch the GPU threads
    s_computeCommandList->lpVtbl->Dispatch(s_computeCommandList, 4, 1, 1);

    // Insert a barrier command to sync the dispatch operation, 
    // and make the UAV buffer object as the copy source.
    const D3D12_RESOURCE_BARRIER barrier = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = { s_dstDataBuffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE } };
    s_computeCommandList->lpVtbl->ResourceBarrier(s_computeCommandList, 1, &barrier);

    // Copy data from the UAV buffer object to the read-back buffer object.
    s_computeCommandList->lpVtbl->CopyResource(s_computeCommandList, readBackBuffer, s_dstDataBuffer);

    s_computeCommandList->lpVtbl->Close(s_computeCommandList);

    s_computeCommandQueue->lpVtbl->ExecuteCommandLists(s_computeCommandQueue, 1, 
        (ID3D12CommandList* const[]) { (ID3D12CommandList*)s_computeCommandList });

    SyncCommandQueue(s_computeCommandQueue, s_device, 2);

    void* pData;
    const D3D12_RANGE range = { 0, TEST_DATA_COUNT };
    // Map the memory buffer so that we may access the data from the host side.
    hr = readBackBuffer->lpVtbl->Map(readBackBuffer, 0, &range, &pData);
    if (FAILED(hr))
        return;

    int* resultBuffer = malloc(TEST_DATA_COUNT * sizeof(*resultBuffer));
    memcpy(resultBuffer, pData, TEST_DATA_COUNT * sizeof(*resultBuffer));

    // After copying the data, just release the read-back buffer object.
    readBackBuffer->lpVtbl->Unmap(readBackBuffer, 0, NULL);
    readBackBuffer->lpVtbl->Release(readBackBuffer);

    // Verify the result
    bool equal = true;
    for (int i = 0; i < TEST_DATA_COUNT; i++)
    {
        if (resultBuffer[i] - 10 != s_DataBuffer0[i])
        {
            printf("%d index elements are not equal!\n", i);
            equal = false;
            break;
        }
    }
    if (equal)
        puts("Verification OK!");

    free(resultBuffer);
}

// Release all the resources
void ReleaseResources(void)
{
    if (s_heap != NULL)
        s_heap->lpVtbl->Release(s_heap);

    if (s_srcDataBuffer != NULL)
        s_srcDataBuffer->lpVtbl->Release(s_srcDataBuffer);

    if (s_dstDataBuffer != NULL)
        s_dstDataBuffer->lpVtbl->Release(s_dstDataBuffer);

    if (s_uploadBuffer != NULL)
        s_uploadBuffer->lpVtbl->Release(s_uploadBuffer);

    if (s_computeAllocator != NULL)
        s_computeAllocator->lpVtbl->Release(s_computeAllocator);

    if (s_computeCommandList != NULL)
        s_computeCommandList->lpVtbl->Release(s_computeCommandList);

    if (s_computeCommandQueue != NULL)
        s_computeCommandQueue->lpVtbl->Release(s_computeCommandQueue);

    if (s_computeState != NULL)
        s_computeState->lpVtbl->Release(s_computeState);

    if (s_computeRootSignature != NULL)
        s_computeRootSignature->lpVtbl->Release(s_computeRootSignature);

    if (s_device != NULL)
        s_device->lpVtbl->Release(s_device);
}

int main(void)
{
    do
    {
        if (!InitAssets())
        {
            puts("InitAssets failed!");
            break;
        }
        if (!InitComputeCommands())
        {
            puts("InitComputeCommands failed!");
            break;
        }
        if (!CreateBuffers())
        {
            puts("CreateBuuffers failed!");
            break;
        }

        if (s_computeCommandList->lpVtbl->Close(s_computeCommandList) < 0)
        {
            puts("Execute init commands failed!");
            break;
        }

        s_computeCommandQueue->lpVtbl->ExecuteCommandLists(s_computeCommandQueue, 1, (ID3D12CommandList* const []) { (ID3D12CommandList*)s_computeCommandList });

        SyncCommandQueue(s_computeCommandQueue, s_device, 1);

        // After finishing the whole buffer copy operation,
        // the intermediate buffer s_uploadBuffer can be released now.
        if (s_uploadBuffer != NULL)
        {
            s_uploadBuffer->lpVtbl->Release(s_uploadBuffer);
            s_uploadBuffer = NULL;
        }

        DoCompute();

    } while (false);

    ReleaseResources();
}

