//-------------------------------------------------------------------------------------
// UVAtlas
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
#include "UVAtlas.h"
#include "isochart.h"
#include "UVAtlasRepacker.h"

using namespace Isochart;
using namespace DirectX;

namespace
{
    template<typename IndexType>
    HRESULT UVAtlasGetRealVertexRemap(
        _In_                            size_t nFaces,
        _In_                            size_t nVerts,
        _In_reads_(nFaces*3)            const IndexType *pInIndexData,
        _Inout_updates_all_ (nFaces*3)  IndexType *pOutIndexData,
        _Out_                           size_t *nNewVerts,
        _Inout_                         std::vector<uint32_t>& vOutVertexRemapBuffer,
        _Inout_                         std::unique_ptr<uint32_t[]>& forwardRemapArray)
    {
        if (!pInIndexData || !pOutIndexData || !nNewVerts)
            return E_POINTER;

        *nNewVerts = nVerts;

        forwardRemapArray.reset( new (std::nothrow) uint32_t[3 * nFaces + nVerts] );
        std::unique_ptr<uint32_t[]> reverseRemapArray( new (std::nothrow) uint32_t[3 * nFaces + nVerts] );
        std::unique_ptr<uint32_t[]> possibleRemapArray( new (std::nothrow) uint32_t[3 * nFaces + nVerts] );
        std::unique_ptr<IndexType[]> newIndexData( new (std::nothrow) IndexType[3 * nFaces] );
        if (!forwardRemapArray || !reverseRemapArray || !possibleRemapArray || !newIndexData)
            return E_OUTOFMEMORY;

        // new vertex id -> new texcoord id
        uint32_t* pForwardRemapArray = forwardRemapArray.get();

        // new vertex id -> old vertex id (should be identity until new vertices)
        uint32_t* pReverseRemapArray = reverseRemapArray.get();

        // new vertex id -> next possible remap (a circularly linked list)
        // this lets us have a list of possible vertices to remap to if
        // a vertex is taken
        uint32_t* pPossibleRemapArray = possibleRemapArray.get();

        // updated index buffer
        IndexType* pNewIndexData = newIndexData.get();

        for (uint32_t i = 0; i < 3 * nFaces + nVerts; i++)
        {
            pForwardRemapArray[i] = uint32_t(-1);
            pReverseRemapArray[i] = uint32_t(-1);
            pPossibleRemapArray[i] = i;
        }

        for (size_t i = 0; i < 3 * nFaces; i++)
        {
            uint32_t uInVert = pInIndexData[i];
            uint32_t uOutVert = pOutIndexData[i];

            if ( pReverseRemapArray[uInVert] == uint32_t(-1) )
            {
                // take this vertex
                pReverseRemapArray[uInVert] = uInVert;
                pForwardRemapArray[uInVert] = uOutVert;
                pNewIndexData[i] = static_cast<IndexType>(uInVert);
            }
            else if (pForwardRemapArray[uInVert] == uOutVert)
            {
                pNewIndexData[i] = static_cast<IndexType>(uInVert);
            }
            else
            {
                // first see if any possible remaps have the same forward mapping
                uint32_t uVert = pPossibleRemapArray[uInVert];
                bool bFound = false;

                while (uVert != uInVert)
                {
                    if (pForwardRemapArray[uVert] == uOutVert)
                    {
                        // this one works
                        bFound = true;
                        pNewIndexData[i] = static_cast<IndexType>(uVert);
                        break;
                    }
                    else
                    {
                        uVert = pPossibleRemapArray[uVert];
                    }
                }

                // need to create a new vert
                if (!bFound)
                {
                    size_t j = *nNewVerts;
                    pReverseRemapArray[j] = uInVert;
                    pForwardRemapArray[j] = uOutVert;
                    pPossibleRemapArray[j] = pPossibleRemapArray[uInVert];
                    pPossibleRemapArray[uInVert] = static_cast<uint32_t>( j );
                    pNewIndexData[i] = static_cast<IndexType>(j);
                    (*nNewVerts)++;
                }
            }
        }

#pragma warning( suppress : 4127 )
        if ((sizeof(IndexType) == sizeof(uint16_t)) && (*nNewVerts > 0x0fffe))
        {
            DPF(0, "Resulting mesh is too large to fit in 16-bit mesh.");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        try
        {
            vOutVertexRemapBuffer.resize(*nNewVerts);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        // ensure that unused vertices are remapped back onto themselves
        for (uint32_t i = 0; i < nVerts; i++)
        {
            if (pReverseRemapArray[i] == uint32_t(-1))
                pReverseRemapArray[i] = i;
        }

        memcpy(vOutVertexRemapBuffer.data(), pReverseRemapArray, (*nNewVerts) * sizeof(uint32_t));
        memcpy(pOutIndexData, pNewIndexData, 3 * nFaces * sizeof(IndexType));

        return S_OK;
    }

    inline uint32_t FindEquivParent(uint32_t *pEquivs, uint32_t v)
    {
        if (pEquivs[v] == v)
            return v;
        return pEquivs[v] = FindEquivParent(pEquivs, pEquivs[v]);
    }

    // returns INVALIDDATA if the false edges are not all connected
    template<typename IndexType>
    HRESULT FalseEdgesConnected(
        _In_reads_(nFaces*3)        const IndexType *pIndexData,
        _In_reads_(nFaces*3)        const uint32_t *adjacency,
        _In_reads_opt_(nFaces*3)    const uint32_t *falseEdges,
        _In_                        size_t nFaces)
    {
        if (!pIndexData || !adjacency || !falseEdges)
            return E_POINTER;

        if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

        // put all vertices in their own equivalency class
        std::unique_ptr<uint32_t[]> equivs(new (std::nothrow) uint32_t[nFaces * 3]);
        if (!equivs)
            return E_OUTOFMEMORY;

        uint32_t* pEquivs = equivs.get();

        for (uint32_t i = 0; i < nFaces * 3; i++)
            pEquivs[i] = i;

        // join vertices that are equivalent through adjacency
        for (size_t i = 0; i < nFaces; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                uint32_t neighbor = adjacency[i * 3 + j];
                if (neighbor >= nFaces)
                    continue;

                size_t k;
                for (k = 0; k < 3; k++)
                {
                    if (adjacency[neighbor * 3 + k] == i)
                        break;
                }

                if (k >= 3)
                {
                    DPF(0, "Adjacency data is invalid, %u is a neighbor of %Iu, but not vice versa.", neighbor, i);
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }

                uint32_t v1 = static_cast<uint32_t>( i * 3 + j );
                uint32_t v2 = neighbor * 3 + ((k + 1) % 3);

                pEquivs[FindEquivParent(pEquivs, v1)] = FindEquivParent(pEquivs, v2);
            }
        }

        // join vertices that are connected by a non-false-edge
        for (size_t i = 0; i < nFaces; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                if (falseEdges[i * 3 + j] != uint32_t(-1))
                    continue;

                uint32_t v1 = static_cast<uint32_t>( i * 3 + j );
                uint32_t v2 = static_cast<uint32_t>(i * 3 + ((j + 1) % 3) );

                pEquivs[FindEquivParent(pEquivs, v1)] = FindEquivParent(pEquivs, v2);
            }
        }

        // ensure that all false edges have both vertices connected
        for (size_t i = 0; i < nFaces; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                if (falseEdges[i * 3 + j] == uint32_t(-1))
                    continue;

                uint32_t v1 = static_cast<uint32_t>( i * 3 + j );
                uint32_t v2 = static_cast<uint32_t>( i * 3 + ((j + 1) % 3) );

                if (FindEquivParent(pEquivs, v1) != FindEquivParent(pEquivs, v2))
                {
                    DPF(0, "False edge data is invalid, %d and %d are only connected by false edges.", pIndexData[v1], pIndexData[v2]);
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
        }

        return S_OK;
    }


    //---------------------------------------------------------------------------------
    HRESULT UVAtlasPartitionInt(
        _In_reads_(nVerts)          const XMFLOAT3* positions,
        _In_                        size_t nVerts,
        _When_(indexFormat == DXGI_FORMAT_R16_UINT, _In_reads_bytes_(nFaces*sizeof(uint16_t)))
        _When_(indexFormat != DXGI_FORMAT_R16_UINT, _In_reads_bytes_(nFaces*sizeof(uint32_t))) const void* indices,
        _In_                        DXGI_FORMAT indexFormat,
        _In_                        size_t nFaces,
        _In_                        size_t maxChartNumber,
        _In_                        float maxStretch,
        _In_reads_(nFaces * 3)      const uint32_t *adjacency,
        _In_reads_opt_(nFaces * 3)  const uint32_t *falseEdgeAdjacency,
        _In_reads_opt_(nFaces * 3)  const float *pIMTArray,
        _In_opt_                    LPISOCHARTCALLBACK statusCallBack,
        _In_                        float callbackFrequency,
        _In_                        DWORD options,
        _Inout_                     std::vector<UVAtlasVertex>& vMeshOutVertexBuffer,
        _Inout_                     std::vector<uint8_t>& vMeshOutIndexBuffer,
        _Inout_opt_                 std::vector<uint32_t> *pvFacePartitioning,
        _Inout_opt_                 std::vector<uint32_t> *pvVertexRemapArray,
        _Inout_                     std::vector<uint32_t>& vPartitionResultAdjacency,
        _Out_opt_                   float *maxStretchOut,
        _Out_opt_                   size_t *numChartsOut,
        _In_                        unsigned int uStageInfo)
    {
        if (!positions || !nVerts || !indices || !nFaces)
            return E_INVALIDARG;

        if (!adjacency)
        {
            DPF(0, "Input adjacency pointer cannot be nullptr. Use DirectXMesh to compute it");
            return E_INVALIDARG;
        }

        switch (indexFormat)
        {
        case DXGI_FORMAT_R16_UINT:
            if (nVerts >= UINT16_MAX)
                return E_INVALIDARG;
            break;

        case DXGI_FORMAT_R32_UINT:
            if (nVerts >= UINT32_MAX)
                return E_INVALIDARG;
            break;

        default:
            return E_INVALIDARG;
        }

        if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

        if (maxChartNumber > nFaces)
            maxChartNumber = nFaces;

        HRESULT hr;

        if (falseEdgeAdjacency)
        {
            for (size_t i = 0; i < 3 * nFaces; i++)
            {
                if ((adjacency[i] == uint32_t(-1)) &&
                    (falseEdgeAdjacency[i] != uint32_t(-1)))
                {
                    DPF(0, "False edge found on triangle with no adjacent triangle.");
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }

            // verify that for every false edge, the two vertices are connected by a path through
            // non false-edges.
            if (DXGI_FORMAT_R16_UINT == indexFormat)
            {
                hr = FalseEdgesConnected<uint16_t>(reinterpret_cast<const uint16_t*>(indices), adjacency, falseEdgeAdjacency, nFaces);
            }
            else
            {
                hr = FalseEdgesConnected<uint32_t>(reinterpret_cast<const uint32_t*>(indices), adjacency, falseEdgeAdjacency, nFaces);
            }
            if (FAILED(hr))
                return hr;
        }

        std::vector<UVAtlasVertex> vOutVertexBuffer;
        std::vector<uint8_t> vOutIndexBuffer;
        std::vector<uint32_t> vOutVertexRemapArray;
        std::vector<uint32_t> vOutFacePartitioning;
        std::vector<uint32_t> vOutAdjacency;

        size_t numCharts = 0;
        float maxChartingStretch = 0.f;

        hr = isochartpartition(positions,
            nVerts,
            sizeof(XMFLOAT3),
            indexFormat,
            indices,
            nFaces,
            reinterpret_cast<const FLOAT3*>(pIMTArray),
            maxChartNumber,
            maxStretch,
            adjacency,
            &vOutVertexBuffer,
            &vOutIndexBuffer,
            &vOutVertexRemapArray,
            &vOutFacePartitioning,
            &vOutAdjacency,
            &numCharts,
            &maxChartingStretch,
            uStageInfo,
            statusCallBack,
            callbackFrequency,
            falseEdgeAdjacency,
            options);
        if (FAILED(hr))
            return hr;

        if (DXGI_FORMAT_R16_UINT == indexFormat)
            assert(nFaces * 3 * sizeof(uint16_t) == vOutIndexBuffer.size());
        else
            assert(nFaces * 3 * sizeof(uint32_t) == vOutIndexBuffer.size());

        // the output remap array gives a remap that merges vertices that have the same
        // position. So this really only gives data on where to get the output texture
        // coordinates from for each vertex. The important thing is we need to detect
        // if a vertex has been split, when this happens the output index buffer will
        // have two triangles that have what was originally the same vertex, but now
        // have different vertices
        vOutVertexRemapArray.clear();

        std::unique_ptr<uint32_t[]> forwardRemapArray;
        size_t outMeshNumVertices = 0;
        if (DXGI_FORMAT_R16_UINT == indexFormat)
        {
            hr = UVAtlasGetRealVertexRemap<uint16_t>(nFaces, nVerts, reinterpret_cast<const uint16_t*>(indices), reinterpret_cast<uint16_t*>(vOutIndexBuffer.data()),
                                                     &outMeshNumVertices, vOutVertexRemapArray, forwardRemapArray);
        }
        else
        {
            hr = UVAtlasGetRealVertexRemap<uint32_t>(nFaces, nVerts, reinterpret_cast<const uint32_t*>(indices), reinterpret_cast<uint32_t*>(vOutIndexBuffer.data()),
                                                     &outMeshNumVertices, vOutVertexRemapArray, forwardRemapArray);
        }
        if (FAILED(hr))
            return hr;

        // make sure we didn't lose vertices, or change the number of faces or switch the format
        assert(outMeshNumVertices >= nVerts);

        // clone old mesh, copy in new vertex buffer and index buffer
        try
        {
            vMeshOutVertexBuffer.resize(outMeshNumVertices);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        // copy old vertex data using remap array
        {
            auto bBaseIn = reinterpret_cast<const uint8_t*>(positions);
            auto bBaseOut = vMeshOutVertexBuffer.data();

            uint32_t* pdwRemap = vOutVertexRemapArray.data();
            auto pOutVerts = vOutVertexBuffer.data();

            uint32_t* pForwardRemapArray = forwardRemapArray.get();

            for (size_t i = 0; i < outMeshNumVertices; i++)
            {
                memcpy(&bBaseOut[i].pos.x, bBaseIn + pdwRemap[i] * sizeof(XMFLOAT3), sizeof(XMFLOAT3));
                if (pForwardRemapArray[i] == uint32_t(-1))
                {
                    bBaseOut[i].uv.x = bBaseOut[i].uv.y = 0.f;
                }
                else
                {
                    bBaseOut[i].uv.x = pOutVerts[pForwardRemapArray[i]].uv.x;
                    bBaseOut[i].uv.y = pOutVerts[pForwardRemapArray[i]].uv.y;
                }
            }
        }

        // copy index data from OutIndexBuffer to the index buffer
        std::swap(vMeshOutIndexBuffer, vOutIndexBuffer);

        if (maxStretchOut)
        {
            *maxStretchOut = maxChartingStretch;
        }

        if (numChartsOut)
        {
            *numChartsOut = numCharts;
        }

        if (pvFacePartitioning)
        {
            std::swap(*pvFacePartitioning, vOutFacePartitioning);
        }

        if (pvVertexRemapArray)
        {
            std::swap(*pvVertexRemapArray, vOutVertexRemapArray);
        }

        std::swap(vPartitionResultAdjacency, vOutAdjacency);

        return S_OK;
    }


    //---------------------------------------------------------------------------------
    HRESULT UVAtlasPackInt(
        _Inout_                 std::vector<UVAtlasVertex>& vMeshVertexBuffer,
        _Inout_                 std::vector<uint8_t>& vMeshIndexBuffer,
        _In_                    DXGI_FORMAT indexFormat,
        _In_                    size_t width,
        _In_                    size_t height,
        _In_                    float gutter,
        _In_                    const std::vector<uint32_t>& vPartitionResultAdjacency,
        _In_opt_                LPISOCHARTCALLBACK statusCallback,
        float                   callbackFrequency,
        _In_                    unsigned int uStageInfo)
    {
        if (!width || !height)
            return E_INVALIDARG;

        if ((width > UINT32_MAX) || (height > UINT32_MAX))
            return E_INVALIDARG;

        if (vMeshVertexBuffer.empty() || vMeshIndexBuffer.empty())
            return E_INVALIDARG;

        size_t nVerts = vMeshVertexBuffer.size();

        size_t nFaces = 0;

        switch (indexFormat)
        {
        case DXGI_FORMAT_R16_UINT:
            if (nVerts >= UINT16_MAX)
                return E_INVALIDARG;

            nFaces = (vMeshIndexBuffer.size() / (sizeof(uint16_t) * 3));
            break;

        case DXGI_FORMAT_R32_UINT:
            if (nVerts >= UINT32_MAX)
                return E_INVALIDARG;

            nFaces = (vMeshIndexBuffer.size() / (sizeof(uint32_t) * 3));
            break;

        default:
            return E_INVALIDARG;
        }

        assert(nFaces > 0);

        if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

        if (vPartitionResultAdjacency.size() != (nFaces*3) )
        {
            DPF(0, "Partition result adjacency info invalid");
            return E_INVALIDARG;
        }

        HRESULT hr = S_OK;

        std::vector<uint8_t> vTempIndexBuffer;
        std::vector<UVAtlasVertex> vTempVertexBuffer;
        try
        {
            vTempIndexBuffer.resize(vMeshIndexBuffer.size());
            vTempVertexBuffer.resize(nVerts);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        auto pIsoVerts = vTempVertexBuffer.data();

        // first transfer mesh position data into a vertex buffer that matches what
        // isochartpack is expecting (x,y,z,u,v)
        for (size_t i = 0; i < nVerts; i++)
        {
            pIsoVerts[i] = vMeshVertexBuffer[i];
        }

        // copy index buffer for isochartpack
        memcpy(vTempIndexBuffer.data(), vMeshIndexBuffer.data(), vTempIndexBuffer.size());

        hr = IsochartRepacker::isochartpack2(&vTempVertexBuffer,
            nVerts,
            &vTempIndexBuffer,
            nFaces,
            vPartitionResultAdjacency.data(),
            width,
            height,
            gutter,
            uStageInfo,
            statusCallback,
            callbackFrequency);
        if (FAILED(hr))
            return hr;

        // encode new texture coordinates in mesh
        {
            auto pOutVerts = vTempVertexBuffer.data();

            for (size_t i = 0; i < nVerts; i++)
            {
                vMeshVertexBuffer[i].uv.x = pOutVerts[i].uv.x;
                vMeshVertexBuffer[i].uv.y = pOutVerts[i].uv.y;
            }
        }

        return S_OK;
    }
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasPartition(
    const XMFLOAT3* positions,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    size_t maxChartNumber,
    float maxStretch,
    const uint32_t *adjacency,
    const uint32_t *falseEdgeAdjacency,
    const float *pIMTArray,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float callbackFrequency,
    DWORD options,
    std::vector<UVAtlasVertex>& vMeshOutVertexBuffer,
    std::vector<uint8_t>& vMeshOutIndexBuffer,
    std::vector<uint32_t>* pvFacePartitioning,
    std::vector<uint32_t>* pvVertexRemapArray,
    std::vector<uint32_t>& vPartitionResultAdjacency,
    float *maxStretchOut,
    size_t *numChartsOut)
{
    return UVAtlasPartitionInt(positions,
                                   nVerts,
                                   indices,
                                   indexFormat,
                                   nFaces,
                                   maxChartNumber,
                                   maxStretch,
                                   adjacency,
                                   falseEdgeAdjacency,
                                   pIMTArray,
                                   statusCallBack,
                                   callbackFrequency,
                                   options,
                                   vMeshOutVertexBuffer,
                                   vMeshOutIndexBuffer,
                                   pvFacePartitioning,
                                   pvVertexRemapArray,
                                   vPartitionResultAdjacency,
                                   maxStretchOut,
                                   numChartsOut,
                                   (maxChartNumber == 0) ?
                                        MAKE_STAGE(2U, 0U, 2U) :
                                        MAKE_STAGE(3U, 0U, 3U));

}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasPack(
    std::vector<UVAtlasVertex>& vMeshVertexBuffer,
    std::vector<uint8_t>& vMeshIndexBuffer,
    DXGI_FORMAT indexFormat,
    size_t width,
    size_t height,
    float gutter,
    const std::vector<uint32_t>& vPartitionResultAdjacency,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float callbackFrequency)
{    
    return UVAtlasPackInt(vMeshVertexBuffer,
                          vMeshIndexBuffer,
                          indexFormat,
                          width,
                          height,
                          gutter,
                          vPartitionResultAdjacency,
                          statusCallBack,
                          callbackFrequency,
                          MAKE_STAGE(1, 0, 1)
                          );
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasCreate(
    const XMFLOAT3* positions,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    size_t maxChartNumber,
    float maxStretch,
    size_t width,
    size_t height,
    float gutter,
    const uint32_t *adjacency,
    const uint32_t *falseEdgeAdjacency,
    const float *pIMTArray,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float callbackFrequency,
    DWORD options,
    std::vector<UVAtlasVertex>& vMeshOutVertexBuffer,
    std::vector<uint8_t>& vMeshOutIndexBuffer,
    std::vector<uint32_t>* pvFacePartitioning,
    std::vector<uint32_t>* pvVertexRemapArray,
    float *maxStretchOut,
    size_t *numChartsOut)
{
    std::vector<uint32_t> vFacePartitioning;
    std::vector<uint32_t> vAdjacencyOut;

    HRESULT hr = UVAtlasPartitionInt(positions,
        nVerts,
        indices,
        indexFormat,
        nFaces,
        maxChartNumber,
        maxStretch,
        adjacency,
        falseEdgeAdjacency,
        pIMTArray,
        statusCallBack,
        callbackFrequency,
        options & UVATLAS_PARTITIONVALIDBITS,
        vMeshOutVertexBuffer,
        vMeshOutIndexBuffer,
        &vFacePartitioning,
        pvVertexRemapArray,
        vAdjacencyOut,
        maxStretchOut,
        numChartsOut,
        (maxChartNumber == 0) ?
        MAKE_STAGE(3U, 0U, 2U) :
        MAKE_STAGE(4U, 0U, 3U));
    if (FAILED(hr))
        return hr;

    hr = UVAtlasPackInt(vMeshOutVertexBuffer,
        vMeshOutIndexBuffer,
        indexFormat,
        width,
        height,
        gutter,
        vAdjacencyOut,
        statusCallBack,
        callbackFrequency,
        (maxChartNumber == 0) ?
        MAKE_STAGE(3U, 2U, 1U) :
        MAKE_STAGE(4U, 3U, 1U));
    if (FAILED(hr))
        return hr;

    if( pvFacePartitioning )
    {
        std::swap(*pvFacePartitioning, vFacePartitioning);
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasComputeIMTFromPerVertexSignal(
    const XMFLOAT3* positions,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    const float *pVertexSignal,
    size_t signalDimension,
    size_t signalStride,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float* pIMTArray)
{
    if (!positions || !nVerts || !indices || !nFaces || !pVertexSignal || !pIMTArray)
        return E_INVALIDARG;

    if ( !signalStride || ( signalStride % sizeof(float) ) )
    {
        DPF(0, "UVAtlasComputeIMT: signalStride (%Iu) must be a multiple of %Iu.", signalStride, sizeof(float));
        return E_INVALIDARG;
    }

    if ( (signalStride / sizeof(float)) < signalDimension )
    {
        DPF(0, "UVAtlasComputeIMT: signalStride (%Iu) must accommodate signal dimension float values (%Iu)\n", signalStride, signalDimension);
        return E_INVALIDARG;
    }

    switch (indexFormat)
    {
    case DXGI_FORMAT_R16_UINT:
        if (nVerts >= UINT16_MAX)
            return E_INVALIDARG;
        break;

    case DXGI_FORMAT_R32_UINT:
        if (nVerts >= UINT32_MAX)
            return E_INVALIDARG;
        break;

    default:
        return E_INVALIDARG;
    }

    if ((uint64_t(signalDimension) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    std::unique_ptr<float[]> signalData( new (std::nothrow) float[3 * signalDimension] );
    if (!signalData)
        return E_OUTOFMEMORY;

    float* pfSignalData = signalData.get();

    auto pdwIndexData = reinterpret_cast<const uint32_t*>(indices);
    auto pwIndexData = reinterpret_cast<const uint16_t*>(indices);

    float* pfIMTData = pIMTArray;

    HRESULT hr;

    for (size_t i = 0; i < nFaces; i++)
    {
        if (statusCallBack && ((i % 64) == 0))
        {
            float fPct = i / (float) nFaces;
            hr = statusCallBack(fPct);
            if (FAILED(hr))
                return E_ABORT;
        }

        XMFLOAT3 pos[3];
        for (size_t j = 0; j < 3; j++)
        {
            uint32_t dwId;
            if (indexFormat == DXGI_FORMAT_R16_UINT)
            {
                dwId = pwIndexData[3*i + j];
            }
            else
            {
                dwId = pdwIndexData[3*i + j];
            }

            if (dwId >= nVerts)
            {
                DPF(0, "UVAtlasComputeIMT: Vertex ID out of range.");
                return E_FAIL;
            }

            pos[j] = positions[dwId];

            for( size_t k = 0; k < signalDimension; k++ )
            {
                pfSignalData[ j *signalDimension + k] = pVertexSignal[dwId * (signalStride / sizeof(float)) + k];
            }
        }

        hr = IMTFromPerVertexSignal(pos,
                                    pfSignalData,
                                    signalDimension,
                                    reinterpret_cast<FLOAT3*>(pfIMTData + 3 * i) );
        if( FAILED(hr) )
        {
            DPF(0, "UVAtlasComputeIMT: IMT data calculation failed.");
            return hr;
        }
    }
    
    if (statusCallBack)
    {
        hr = statusCallBack(1.0f);
        if (FAILED(hr))
            return E_ABORT;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasComputeIMTFromSignal(
    const XMFLOAT3* positions,
    const XMFLOAT2* texcoords,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    size_t signalDimension,
    float maxUVDistance,
    std::function<HRESULT __cdecl(const DirectX::XMFLOAT2 *uv, size_t primitiveID, size_t signalDimension, void* userData, float* signalOut)> signalCallback,
    void *userData,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float* pIMTArray)
{
    if (!positions || !texcoords || !nVerts || !indices || !nFaces || !pIMTArray)
        return E_INVALIDARG;

    if ( !signalCallback )
    {
        DPF(0, "ComputeIMTFromSignal: requires signal computation callback." );
        return E_INVALIDARG;
    }

    if (signalDimension > UINT32_MAX)
        return E_INVALIDARG;

    switch (indexFormat)
    {
    case DXGI_FORMAT_R16_UINT:
        if (nVerts >= UINT16_MAX)
            return E_INVALIDARG;
        break;

    case DXGI_FORMAT_R32_UINT:
        if (nVerts >= UINT32_MAX)
            return E_INVALIDARG;
        break;

    default:
        return E_INVALIDARG;
    }

    if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    auto pdwIndexData = reinterpret_cast<const uint32_t*>(indices);
    auto pwIndexData = reinterpret_cast<const uint16_t*>(indices);

    float* pfIMTData = pIMTArray;

    HRESULT hr;

    for (size_t i = 0; i < nFaces; i++)
    {
        if (statusCallBack && ((i % 64) == 0))
        {
            float fPct = i / (float) nFaces;
            hr = statusCallBack(fPct);
            if (FAILED(hr))
                return E_ABORT;
        }

        XMFLOAT3 pos[3];
        XMFLOAT2 uv[3];
        for (size_t j = 0; j < 3; j++)
        {
            uint32_t dwId;
            if (indexFormat == DXGI_FORMAT_R16_UINT)
            {
                dwId = pwIndexData[3*i + j];
            }
            else
            {
                dwId = pdwIndexData[3*i + j];
            }

            if( dwId >= nVerts )
            {
                DPF(0, "UVAtlasComputeIMT: Vertex ID out of range.");
                return E_FAIL;
            }

            pos[j] = positions[dwId];
            uv[j] = texcoords[dwId];
        }

        hr = IMTFromTextureMap(pos, uv, 
                               8, // max 64k subtesselations
                               maxUVDistance,
                               i,
                               signalDimension,
                               signalCallback,
                               userData,
                               (FLOAT3*)(pfIMTData + 3*i));
        if ( FAILED(hr) )
        {
            DPF(0, "UVAtlasComputeIMT: IMT data calculation failed.");
            return hr;
        }
    }
    
    if (statusCallBack)
    {
        hr = statusCallBack(1.0f);
        if( FAILED(hr) )
            return E_ABORT;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
namespace
{
    struct IMTTextureDesc
    {
        const XMFLOAT4 *pTexture;
        size_t uHeight, uWidth;
    };

    HRESULT __cdecl IMTTextureCbWrapNone(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);
        UNREFERENCED_PARAMETER(uSignalDimension);

        auto pTexDesc = reinterpret_cast<IMTTextureDesc*>( pUserData );

        float u = uv->x;
        float v = uv->y;

        if (u < 0.f)
            u = 0.f;
        if (u > 1.f)
            u = 1.f;

        if (v < 0.f)
            v = 0.f;
        if (v > 1.f)
            v = 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = std::max(0, std::min<int>(i, int(pTexDesc->uWidth) - 1));
        i2 = std::max(0, std::min<int>(i2, int(pTexDesc->uWidth) - 1));
        j = std::max(0, std::min<int>(j, int(pTexDesc->uHeight) - 1));
        j2 = std::max(0, std::min<int>(j2, int(pTexDesc->uHeight) - 1));

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        XMVECTOR C1, C2, C3, C4, res;
        C1 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i]);
        C2 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i2]);
        C3 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i]);
        C4 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i2]);

        res = (C1 * (1.f - du) + C2 * du) * (1.f - dv) +
            (C3 * (1.f - du) + C4 * du) * dv;

        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(pfSignalOut), res);

        return S_OK;
    }

    HRESULT __cdecl IMTTextureCbWrapU(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);
        UNREFERENCED_PARAMETER(uSignalDimension);

        auto pTexDesc = reinterpret_cast<IMTTextureDesc*>(pUserData);

        float u = fmodf(uv->x, 1.f);
        float v = uv->y;

        if (u < 0.f)
            u += 1.f;

        if (v < 0.f)
            v = 0.f;
        if (v > 1.f)
            v = 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = i % pTexDesc->uWidth;
        i2 = i2 % pTexDesc->uWidth;

        if (i < 0)
            i += int(pTexDesc->uWidth);
        if (i2 < 0)
            i2 += int(pTexDesc->uWidth);

        j = std::max(0, std::min<int>(j, int(pTexDesc->uHeight) - 1));
        j2 = std::max(0, std::min<int>(j2, int(pTexDesc->uHeight) - 1));

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        XMVECTOR C1, C2, C3, C4, res;
        C1 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i]);
        C2 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i2]);
        C3 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i]);
        C4 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i2]);

        res = (C1 * (1.f - du) + C2 * du) * (1 - dv) +
            (C3 * (1.f - du) + C4 * du) * dv;

        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(pfSignalOut), res);

        return S_OK;
    }

    HRESULT __cdecl IMTTextureCbWrapV(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);
        UNREFERENCED_PARAMETER(uSignalDimension);

        auto pTexDesc = reinterpret_cast<IMTTextureDesc*>(pUserData);

        float u = uv->x;
        float v = fmodf(uv->y, 1.f);

        if (u < 0.f)
            u = 0.f;
        if (u > 1.f)
            u = 1.f;

        if (v < 0.f)
            v += 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = std::max(0, std::min<int>(i, int(pTexDesc->uWidth) - 1));
        i2 = std::max(0, std::min<int>(i2, int(pTexDesc->uWidth) - 1));

        j = j % pTexDesc->uHeight;
        j2 = j2 % pTexDesc->uHeight;

        if (j < 0)
            j += int(pTexDesc->uHeight);
        if (j2 < 0)
            j2 += int(pTexDesc->uHeight);

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        XMVECTOR C1, C2, C3, C4, res;
        C1 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i]);
        C2 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i2]);
        C3 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i]);
        C4 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i2]);

        res = (C1 * (1.f - du) + C2 * du) * (1.f - dv) +
            (C3 * (1.f - du) + C4 * du) * dv;

        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(pfSignalOut), res);

        return S_OK;
    }

    HRESULT __cdecl IMTTextureCbWrapUV(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);
        UNREFERENCED_PARAMETER(uSignalDimension);

        auto pTexDesc = reinterpret_cast<IMTTextureDesc*>(pUserData);

        float u = fmodf(uv->x, 1.f);
        float v = fmodf(uv->y, 1.f);

        if (u < 0.f)
            u += 1.f;

        if (v < 0.f)
            v += 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = i % pTexDesc->uWidth;
        i2 = i2 % pTexDesc->uWidth;

        if (i < 0)
            i += int(pTexDesc->uWidth);
        if (i2 < 0)
            i2 += int(pTexDesc->uWidth);

        j = j % pTexDesc->uHeight;
        j2 = j2 % pTexDesc->uHeight;

        if (j < 0)
            j += int(pTexDesc->uHeight);
        if (j2 < 0)
            j2 += int(pTexDesc->uHeight);

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        XMVECTOR C1, C2, C3, C4, res;
        C1 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i]);
        C2 = XMLoadFloat4(&pTexDesc->pTexture[j * pTexDesc->uWidth + i2]);
        C3 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i]);
        C4 = XMLoadFloat4(&pTexDesc->pTexture[j2 * pTexDesc->uWidth + i2]);

        res = (C1 * (1.f - du) + C2 * du) * (1.f - dv) +
            (C3 * (1.f - du) + C4 * du) * dv;

        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(pfSignalOut), res);

        return S_OK;
    }
}

_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasComputeIMTFromTexture(
    const XMFLOAT3* positions,
    const XMFLOAT2* texcoords,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    const float* pTexture,
    size_t width,
    size_t height,
    DWORD options,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float* pIMTArray)
{
    if (!positions || !texcoords || !nVerts || !indices || !nFaces || !pTexture || !pIMTArray)
        return E_INVALIDARG;

    if (!width || !height)
        return E_INVALIDARG;

    if ((width > UINT32_MAX) || (height > UINT32_MAX))
        return E_INVALIDARG;

    switch (indexFormat)
    {
    case DXGI_FORMAT_R16_UINT:
        if (nVerts >= UINT16_MAX)
            return E_INVALIDARG;
        break;

    case DXGI_FORMAT_R32_UINT:
        if (nVerts >= UINT32_MAX)
            return E_INVALIDARG;
        break;

    default:
        return E_INVALIDARG;
    }

    if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    LPIMTSIGNALCALLBACK pSignalCallback = nullptr;
    if ((options & UVATLAS_IMT_WRAP_UV) == UVATLAS_IMT_WRAP_UV)
    {
        pSignalCallback = IMTTextureCbWrapUV;
    }
    else if ( options & UVATLAS_IMT_WRAP_U )
    {
        pSignalCallback = IMTTextureCbWrapU;
    }
    else if( options & UVATLAS_IMT_WRAP_V )
    {
        pSignalCallback = IMTTextureCbWrapV;
    }
    else
    {
        pSignalCallback = IMTTextureCbWrapNone;
    }

    auto pdwIndexData = reinterpret_cast<const uint32_t*>( indices );
    auto pwIndexData = reinterpret_cast<const uint16_t*>( indices );

    IMTTextureDesc TextureDesc;
    TextureDesc.pTexture = reinterpret_cast<const XMFLOAT4*>( pTexture );
    TextureDesc.uWidth   = width;
    TextureDesc.uHeight  = height;

    float* pfIMTData = pIMTArray;

    HRESULT hr;

    for ( size_t i = 0; i < nFaces; i++)
    {
        if (statusCallBack && ((i % 64) == 0))
        {
            float fPct = i / (float) nFaces;
            hr = statusCallBack(fPct);
            if (FAILED(hr))
                return E_ABORT;
        }

        XMFLOAT3 pos[3];
        XMFLOAT2 uv[3];
        for (size_t j = 0; j < 3; j++)
        {
            uint32_t dwId;
            if (indexFormat == DXGI_FORMAT_R16_UINT)
            {
                dwId = pwIndexData[3*i + j];
            }
            else
            {
                dwId = pdwIndexData[3*i + j];
            }

            if( dwId >= nVerts )
            {
                DPF(0, "UVAtlasComputeIMT: Vertex ID out of range.");
                return E_FAIL;
            }

            pos[j] = positions[dwId];
            uv[j] = texcoords[dwId];
        }

        hr = IMTFromTextureMapEx(pos,
                                 uv,
                                 i,
                                 4, // dimension 4, rgba, can be zeroes if less than 4
                                 pSignalCallback,
                                 &TextureDesc,
                                 (FLOAT3*)(pfIMTData + 3*i));
        if (FAILED(hr))
        {
            DPF(0, "UVAtlasComputeIMT: IMT data calculation failed.");
            return hr;
        }
    }
    
    if (statusCallBack)
    {
        hr = statusCallBack(1.0f);
        if (FAILED(hr))
            return E_ABORT;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
namespace
{
    struct IMTFloatArrayDesc
    {
        const float *pTexture;
        size_t uHeight, uWidth, uStride;
    };

    HRESULT __cdecl IMTFloatArrayCbWrapNone(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);

        auto pTexDesc = reinterpret_cast<IMTFloatArrayDesc*>( pUserData );

        float u = uv->x;
        float v = uv->y;

        if (u < 0.f)
            u = 0.f;
        if (u > 1.f)
            u = 1.f;

        if (v < 0.f)
            v = 0.f;
        if (v > 1.f)
            v = 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = std::max(0, std::min<int>(i, int(pTexDesc->uWidth) - 1));
        i2 = std::max(0, std::min<int>(i2, int(pTexDesc->uWidth) - 1));
        j = std::max(0, std::min<int>(j, int(pTexDesc->uHeight) - 1));
        j2 = std::max(0, std::min<int>(j2, int(pTexDesc->uHeight) - 1));

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        const float *C1 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C2 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i2) * pTexDesc->uStride];
        const float *C3 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C4 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i2) * pTexDesc->uStride];

        for (size_t k = 0; k < uSignalDimension; k++)
        {
            pfSignalOut[k] = (C1[k] * (1.f - du) + C2[k] * du) * (1.f - dv) +
                (C3[k] * (1.f - du) + C4[k] * du) * dv;
        }

        return S_OK;
    }

    HRESULT __cdecl IMTFloatArrayCbWrapU(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);

        auto pTexDesc = reinterpret_cast<IMTFloatArrayDesc*>(pUserData);

        float u = fmodf(uv->x, 1.f);
        float v = uv->y;

        if (u < 0.f)
            u += 1.f;

        if (v < 0.f)
            v = 0.f;
        if (v > 1.f)
            v = 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = i % pTexDesc->uWidth;
        i2 = i2 % pTexDesc->uWidth;

        if (i < 0)
            i += int(pTexDesc->uWidth);
        if (i2 < 0)
            i2 += int(pTexDesc->uWidth);

        j = std::max(0, std::min<int>(j, int(pTexDesc->uHeight) - 1));
        j2 = std::max(0, std::min<int>(j2, int(pTexDesc->uHeight) - 1));

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        const float *C1 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C2 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i2) * pTexDesc->uStride];
        const float *C3 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C4 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i2) * pTexDesc->uStride];

        for (size_t k = 0; k < uSignalDimension; k++)
        {
            pfSignalOut[k] = (C1[k] * (1.f - du) + C2[k] * du) * (1.f - dv) +
                (C3[k] * (1.f - du) + C4[k] * du) * dv;
        }

        return S_OK;
    }

    HRESULT __cdecl IMTFloatArrayCbWrapV(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);

        auto pTexDesc = reinterpret_cast<IMTFloatArrayDesc*>(pUserData);

        float u = uv->x;
        float v = fmodf(uv->y, 1.f);

        if (u < 0.f)
            u = 0.f;
        if (u > 1.f)
            u = 1.f;

        if (v < 0.f)
            v += 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = std::max(0, std::min<int>(i, int(pTexDesc->uWidth) - 1));
        i2 = std::max(0, std::min<int>(i2, int(pTexDesc->uWidth) - 1));

        j = j % pTexDesc->uHeight;
        j2 = j2 % pTexDesc->uHeight;

        if (j < 0)
            j += int(pTexDesc->uHeight);
        if (j2 < 0)
            j2 += int(pTexDesc->uHeight);

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        const float *C1 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C2 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i2) * pTexDesc->uStride];
        const float *C3 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C4 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i2) * pTexDesc->uStride];

        for (size_t k = 0; k < uSignalDimension; k++)
        {
            pfSignalOut[k] = (C1[k] * (1.f - du) + C2[k] * du) * (1.f - dv) +
                (C3[k] * (1.f - du) + C4[k] * du) * dv;
        }

        return S_OK;
    }

    HRESULT __cdecl IMTFloatArrayCbWrapUV(const XMFLOAT2 *uv,
        size_t uPrimitiveId,
        size_t uSignalDimension,
        void *pUserData,
        float *pfSignalOut)
    {
        UNREFERENCED_PARAMETER(uPrimitiveId);

        auto pTexDesc = reinterpret_cast<IMTFloatArrayDesc*>( pUserData );

        float u = fmodf(uv->x, 1.f);
        float v = fmodf(uv->y, 1.f);

        if (u < 0.f)
            u += 1.f;

        if (v < 0.f)
            v += 1.f;

        u = u * pTexDesc->uWidth;
        v = v * pTexDesc->uHeight;

        int i = (int) u;
        int j = (int) v;
        int i2 = i + 1;
        int j2 = j + 1;

        float du = u - i;
        float dv = v - j;

        i = i % pTexDesc->uWidth;
        i2 = i2 % pTexDesc->uWidth;

        if (i < 0)
            i += int(pTexDesc->uWidth);
        if (i2 < 0)
            i2 += int(pTexDesc->uWidth);

        j = j % pTexDesc->uHeight;
        j2 = j2 % pTexDesc->uHeight;

        if (j < 0)
            j += int(pTexDesc->uHeight);
        if (j2 < 0)
            j2 += int(pTexDesc->uHeight);

        // 
        // C1 ---- C2  ^          dv
        //  | .    |   |           |
        //  |      |   |           |
        //  |      |   |           |
        // C3 ---- C4  v, u --->   v
        //

        const float *C1 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C2 = &pTexDesc->pTexture[(j * pTexDesc->uWidth + i2) * pTexDesc->uStride];
        const float *C3 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i) * pTexDesc->uStride];
        const float *C4 = &pTexDesc->pTexture[(j2 * pTexDesc->uWidth + i2) * pTexDesc->uStride];

        for (size_t k = 0; k < uSignalDimension; k++)
        {
            pfSignalOut[k] = (C1[k] * (1.f - du) + C2[k] * du) * (1.f - dv) +
                (C3[k] * (1.f - du) + C4[k] * du) * dv;
        }

        return S_OK;
    }
}

_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasComputeIMTFromPerTexelSignal(
    const XMFLOAT3* positions,
    const XMFLOAT2* texcoords,
    size_t nVerts,
    const void* indices,
    DXGI_FORMAT indexFormat,
    size_t nFaces,
    const float *pTexelSignal,
    size_t width,
    size_t height,
    size_t signalDimension,
    size_t nComponents,
    DWORD options,
    std::function<HRESULT __cdecl(float percentComplete)> statusCallBack,
    float* pIMTArray)
{
    if (!positions || !texcoords || !nVerts || !indices || !nFaces || !pTexelSignal || !pIMTArray)
        return E_INVALIDARG;

    if (!width || !height)
        return E_INVALIDARG;

    if ((width > UINT32_MAX) || (height > UINT32_MAX) || (signalDimension > UINT32_MAX) || (nComponents > UINT32_MAX))
        return E_INVALIDARG;

    switch (indexFormat)
    {
    case DXGI_FORMAT_R16_UINT:
        if (nVerts >= UINT16_MAX)
            return E_INVALIDARG;
        break;

    case DXGI_FORMAT_R32_UINT:
        if (nVerts >= UINT32_MAX)
            return E_INVALIDARG;
        break;

    default:
        return E_INVALIDARG;
    }

    if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    if (nComponents < signalDimension)
    {
        DPF(0, "UVAtlasComputeIMT: number of components must be >= signal dimension");
        return E_INVALIDARG;
    }

    const float *pTextureData = pTexelSignal;

    LPIMTSIGNALCALLBACK pSignalCallback = nullptr;
    if ((options & UVATLAS_IMT_WRAP_UV) == UVATLAS_IMT_WRAP_UV)
    {
        pSignalCallback = IMTFloatArrayCbWrapUV;
    }
    else if( options & UVATLAS_IMT_WRAP_U )
    {
        pSignalCallback = IMTFloatArrayCbWrapU;
    }
    else if( options & UVATLAS_IMT_WRAP_V )
    {
        pSignalCallback = IMTFloatArrayCbWrapV;
    }
    else
    {
        pSignalCallback = IMTFloatArrayCbWrapNone;
    }

    auto pdwIndexData = reinterpret_cast<const uint32_t*>( indices );
    auto pwIndexData = reinterpret_cast<const uint16_t*>( indices );

    IMTFloatArrayDesc FloatArrayDesc;
    FloatArrayDesc.pTexture = pTextureData;
    FloatArrayDesc.uWidth   = width;
    FloatArrayDesc.uHeight  = height;
    FloatArrayDesc.uStride  = nComponents;

    float* pfIMTData = pIMTArray;

    HRESULT hr;

    for (size_t i = 0; i < nFaces; i++)
    {
        if (statusCallBack && ((i % 64) == 0))
        {
            float fPct = i / (float) nFaces;
            hr = statusCallBack(fPct);
            if (FAILED(hr))
                return E_ABORT;
        }

        XMFLOAT3 pos[3];
        XMFLOAT2 uv[3];
        for (size_t j = 0; j < 3; j++)
        {
            uint32_t dwId;
            if (indexFormat == DXGI_FORMAT_R16_UINT)
            {
                dwId = pwIndexData[3*i + j];
            }
            else
            {
                dwId = pdwIndexData[3*i + j];
            }

            if( dwId >= nVerts )
            {
                DPF(0, "UVAtlasComputeIMT: Vertex ID out of range.");
                return E_FAIL;
            }

            pos[j] = positions[dwId];
            uv[j] = texcoords[dwId];
        }

        hr = IMTFromTextureMapEx(pos,
                                 uv,
                                 i,
                                 signalDimension,
                                 pSignalCallback,
                                 &FloatArrayDesc,
                                 (FLOAT3*)(pfIMTData + 3*i));
        if (FAILED(hr))
        {
            DPF(0, "UVAtlasComputeIMT: IMT data calculation failed.");
            return hr;
        }
    }
    
    if (statusCallBack)
    {
        hr = statusCallBack(1.0f);
        if (FAILED(hr))
            return E_ABORT;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT __cdecl DirectX::UVAtlasApplyRemap(
    const void* vbin,
    size_t stride,
    size_t nVerts,
    size_t nNewVerts,
    const uint32_t* vertexRemap,
    void* vbout )
{
    if ( !vbin || !stride || !nVerts || !nNewVerts || !vertexRemap || !vbout )
        return E_INVALIDARG;

    if ( nNewVerts >= UINT32_MAX )
        return E_INVALIDARG;

    if ( nVerts > nNewVerts )
        return E_INVALIDARG;

    if ( stride > 2048 /*D3D11_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES*/ )
        return E_INVALIDARG;

    if ( vbin == vbout )
        return HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED );

    auto sptr = reinterpret_cast<const uint8_t*>( vbin );
    auto dptr = reinterpret_cast<uint8_t*>( vbout );

    for( size_t j = 0; j < nNewVerts; ++j )
    {
        uint32_t src = vertexRemap[ j ];

        if ( src == uint32_t(-1) )
        {
            // entry is unused
            memset( dptr, 0, stride );
        }
        else if ( src < nVerts )
        {
            memcpy( dptr, sptr + src * stride, stride ); 
        }
        else
            return E_FAIL;

        dptr += stride;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
#ifdef _DEBUG
_Use_decl_annotations_
void __cdecl UVAtlasDebugPrintf(unsigned int lvl, LPCSTR szFormat, ...)
{
    if (lvl > 0)
    {
        // Change this to see more verbose messages...
        return;
    }

    char strA[4096];
    char strB[4096];

    va_list ap;
    va_start(ap, szFormat);
    vsprintf_s(strA, sizeof(strA), szFormat, ap);
    strA[4095] = '\0';
    va_end(ap);

    sprintf_s(strB, sizeof(strB), "UVAtlas: %s\r\n", strA);

    strB[4095] = '\0';

    OutputDebugStringA(strB);
}
#endif // _DEBUG