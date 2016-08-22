//-------------------------------------------------------------------------------------
// UVAtlas - Isochart.cpp
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//  
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#include "pch.h"
#include "isochart.h"
#include "isochartengine.h"

using namespace DirectX;
using namespace Isochart;

namespace
{
    static bool CheckIsochartInput(
        const void* pVertexArray,
        size_t VertexCount,
        size_t VertexStride,
        DXGI_FORMAT IndexFormat,
        const void* pFaceIndexArray,
        size_t FaceCount,
        const FLOAT3* pIMTArray,
        size_t MaxChartNumber,
        float Stretch,
        size_t Width,
        size_t Height,
        float Gutter,
        std::vector<UVAtlasVertex>* pvVertexArrayOut,
        std::vector<uint8_t>* pvFaceIndexArrayOut,
        std::vector<uint32_t>* pvVertexRemapArrayOut,
        size_t* pChartNumberOut,
        float* pMaxStretchOut,
        LPISOCHARTCALLBACK pCallback,
        float Frequency,
        DWORD dwOptions)
    {
        if (!CheckInitializeParameters(
            pVertexArray,
            VertexCount,
            VertexStride,
            IndexFormat,
            pFaceIndexArray,
            FaceCount,
            pIMTArray,
            dwOptions))
        {
            return false;
        }

        if (!CheckPartitionParameters(
            MaxChartNumber,
            FaceCount,
            Stretch,
            pChartNumberOut,
            pMaxStretchOut,
            nullptr))
        {
            return false;
        }

        if (!CheckPackParameters(
            Width,
            Height,
            Gutter,
            pvVertexArrayOut,
            pvFaceIndexArrayOut,
            pvVertexRemapArrayOut,
            nullptr))
        {
            return false;
        }

        if (!CheckSetCallbackParameters(
            pCallback,
            Frequency))
        {
            return false;
        }

        return true;
    }
}


/////////////////////////////////////////////////////////////////////
//isochart
//	Generate UV-atlas by given a mesh
//Parameters:
//-pVertexArray:
//		Input vertex buffer. Each vertex starts with a XMFLOAT3 structure.
//
//-VertexStride:
//		Vertex size in bytes.
//
//-IndexFormat:
//		Face index format, can be D3DFMT_16 or D3DFMT_32
//
//-pFaceIndexArray:
//		Input face buffer. Must be a triangle list.
//
//-pIMTArray:
//		An array of integrated metric tensor matrices, one per face, that 
//		describe how a signal varies over the surface of the face.Set this 
//		parameter to nullptr to ignore the infection of signal.
//
//-MaxChartNumber:
//		The max output chart number. Set it to 0 to let Stretch fully 
//		control the partition. MaxChartNumber must smaller than FaceCount
//
//-Stretch:
//		Between 0 and 1, control distortion. 0 means no distortion at all.
//		1 means as much distortion as possible.
//
//-Width, Height:
//		Size of UV map.
//
//-Gutter:
//		The least distance between two charts on Width*Height UV-atlas.
//
//-ppVertexArrayOut:
//		Output vertex buffer, each output vertex can be converted to UVAtlasVertex
//		format to get uv-coordinate.
//
//-ppFaceIndexArrayOut:
//		Output face index buffer. The index indicate the vertex order in output 
//		vertex buffer.

//-ppVertexRemapArrayOut:
//		Maps output vertices to input vertices.
//
//-ppAttributeID:
//		Attribute identifer of each output face.Faces with same ID are in same 
//		sub-chart.
//
//-pChartNumberOut:
//		Actual number of charts generated. Can be nullptr.
//
//-pMaxStretchOut:
//		Actual max stretch.
//
//-pCallback, Frequency
//		See detail in header file.
//
//Return value:
//-If succeed, return S_OK
//-If fail, return value can be one of the following values
//		ERROR_INVALID_DATA, Can not process the input non-manifold mesh
//		E_OUTOFMEMORY, Could not allocate sufficient memory to compute the call
//		E_INVALIDARG, Passed invalid argument
//		E_FAIL, Exceptional errors

    
HRESULT WINAPI Isochart::isochart(
    const void* pVertexArray,
    size_t VertexCount,
    size_t VertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    size_t FaceCount,
    const FLOAT3* pIMTArray,
    size_t MaxChartNumber,
    float Stretch,
    size_t Width,
    size_t Height,
    float Gutter,
    const uint32_t* pOriginalAjacency,// Reserved
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    size_t* pChartNumberOut,
    float* pMaxStretchOut,
    LPISOCHARTCALLBACK pCallback,
    float Frequency,  
    DWORD dwOptions)
{
    // 1. Check input parameter
    if (!CheckIsochartInput(
        pVertexArray,
        VertexCount,
        VertexStride,
        IndexFormat,
        pFaceIndexArray,
        FaceCount,
        pIMTArray,
        MaxChartNumber,
        Stretch,
        Width,
        Height,
        Gutter,
        pvVertexArrayOut,
        pvFaceIndexArrayOut,
        pvVertexRemapArrayOut,
        pChartNumberOut,
        pMaxStretchOut,
        pCallback,
        Frequency,
        dwOptions))
    {
        return E_INVALIDARG;
    }

    // 2. Create isochart engine
    auto pEngine = IIsochartEngine::CreateIsochartEngine();
    if (!pEngine)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = S_OK;
    size_t dwChartNumberOut = 0;
    float fMaxChartStretchOut = 0.0f;

    // 3. Set Callback function
    // Even initialization() takes long time, user can stop long 
    // initialization.
    if (pCallback)
    {
        if (FAILED(hr = pEngine->SetCallback(
            pCallback, 
            Frequency)))
        {
            goto LEnd;
        }
    }

    // 4. Initialize isochart engine
    if (FAILED(hr = pEngine->Initialize(
        pVertexArray, 
        VertexCount, 
        VertexStride, 
        IndexFormat, 
        pFaceIndexArray, 
        FaceCount, 
        pIMTArray, 
        pOriginalAjacency,
        nullptr,
        dwOptions)))
    {
        goto LEnd;
    }

    // 5. Partition
    dwChartNumberOut = 0;
    if (FAILED(hr = pEngine->Partition(
            MaxChartNumber, 
            Stretch, 
            dwChartNumberOut, 
            fMaxChartStretchOut, 
            nullptr)))
    {
        goto LEnd;
    }

    // 6. Pack charts to UV-atlas
    if (FAILED(hr = pEngine->Pack(
            Width,
            Height,
            Gutter,
            pFaceIndexArray,
            pvVertexArrayOut,
            pvFaceIndexArrayOut,
            pvVertexRemapArrayOut,
            nullptr)))
    {
        goto LEnd;
    }
    
LEnd:

    // 7. Free resources of isochart engine
    pEngine->Free();

    if (pChartNumberOut)
    {
        *pChartNumberOut = dwChartNumberOut;
    }

    if (pMaxStretchOut)
    {
        *pMaxStretchOut = fMaxChartStretchOut;
    }

    IIsochartEngine::ReleaseIsochartEngine(pEngine);
    return hr;
}

_Use_decl_annotations_
HRESULT WINAPI Isochart::isochartpartition(
    const void* pVertexArray,
    size_t VertexCount,
    size_t VertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    size_t FaceCount,
    const FLOAT3* pIMTArray,
    size_t MaxChartNumber,
    float Stretch,
    const uint32_t* pOriginalAjacency,
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut,
    size_t* pChartNumberOut,
    float* pMaxStretchOut,
    unsigned int Stage,
    LPISOCHARTCALLBACK pCallback,
    float Frequency,
    const uint32_t* pSplitHint,
    DWORD dwOptions)
{
    unsigned int dwTotalStage = STAGE_TOTAL(Stage);
    unsigned int dwDoneStage = STAGE_DONE(Stage);
        
    DPF(0, "Vertex Number:%Iu\n", VertexCount);
    DPF(0, "Face Number:%Iu\n", FaceCount);
        
    // 2. Create isochart engine
    auto pEngine = IIsochartEngine::CreateIsochartEngine();
    if (!pEngine)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = S_OK;
    float fMaxChartStretchOut = 0.0f;
    size_t dwChartNumberOut = 0;

    // 3. Set Callback function
    // Even initialization() takes long time, user can stop long 
    // initialization.
    if (pCallback)
    {
        if (FAILED(hr = pEngine->SetCallback(
            pCallback,
            Frequency)))
        {
            goto LEnd;
        }
    }
    pEngine->SetStage(dwTotalStage, dwDoneStage);

    // 4. Initialize isochart engine
    if (FAILED(hr = pEngine->Initialize(
            pVertexArray, 
            VertexCount, 
            VertexStride, 
            IndexFormat, 
            pFaceIndexArray, 
            FaceCount, 
            pIMTArray, 
            pOriginalAjacency,
            pSplitHint,
            dwOptions)))
    {
        goto LEnd;
    }				
    pEngine->SetStage(dwTotalStage, dwDoneStage+1);
        
    // 5. Partition
    if (FAILED(hr = pEngine->Partition(
            MaxChartNumber,
            Stretch,
            dwChartNumberOut,
            fMaxChartStretchOut,
            nullptr)))
    {
        goto LEnd;
    }

    // 6. Export Partition Result
    hr = pEngine->ExportPartitionResult(
        pvVertexArrayOut,
        pvFaceIndexArrayOut, 
        pvVertexRemapArrayOut, 
        pvAttributeIDOut,
        pvAdjacencyOut);

    pEngine->SetStage(dwTotalStage, dwDoneStage+1);
LEnd:

    // 7. Free resources of isochart engine
    pEngine->Free();

    if (pChartNumberOut)
    {
        *pChartNumberOut = dwChartNumberOut;
    }

    if (pMaxStretchOut)
    {
        *pMaxStretchOut = fMaxChartStretchOut;
    }

    IIsochartEngine::ReleaseIsochartEngine(pEngine);
    return hr;
}

