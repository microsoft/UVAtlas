//-------------------------------------------------------------------------------------
// UVAtlas - Isochartmesh.cpp
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

/*
    Terms:
        L2 Stretch: 
            Corresponds to the root-mean-square stretch over all directions
            in the domain.

        Ln Stretch:
            The worst-case norm Ln is the greatest stretch.

        Note that both L2 and Ln increase to infinity as the parametrization of 
        face becomes degenerate, since its parametric area A drops to zero.

        See more detail in [SSGH01]
    Reference:
        This file implements algorithms in following papers:

        [Kun04]:Kun Zhou, John Synder, Baining Guo, Heung-Yeung Shum:
        Iso-charts: Stretch-driven Mesh Parameterization using Spectral 
        Analysis, page 3 Eurographics Symposium on Geometry Processing (2004)

        [SSGH01]: SANDER P., SNYDER J., GORTLER S., HOPPE H. :
        Textutre mapping processive meshes. In Proceeding of SIGGRAPH 2001,

        [SGSH02] SANDER P., GORTLER S., SNYDER J., HOPPE H.:
        Signal-specialized parameterization
        In Proceedings of Eurographics Workshop on Rendering 2002(2002)

        [GH97] GARLAND M., HECKBERT P.:
        Surface simplification using quadric error metrics
        In Proceedings of SIGGRAPH 1997
        (1997), pp. 209-216 

        [KS98] KIMMEL R., SETHIAN J.:
        Computing geodesics on manifolds.
        In Proceedings of Nat'l Academy Sciences(1998),
        pp. 8431-8435
*/

#include "pch.h"
#include "maxheap.hpp"
#include "isochartmesh.h"
#include "progressivemesh.h"
#include "vertiter.h"

using namespace Isochart;
using namespace DirectX;

#define FACE_ARRAY_ITME(addr, idx, item) addr[idx*3+item]

namespace
{
    ///////////////////////////////////////////////////////////
    /////////// Configuration of internal stretch /////////////
    ///////////////////////////////////////////////////////////

    const float ISOCHART_MAX_STRETCH_RATIO = FLT_MAX; // 1e8f;

    ///////////////////////////////////////////////////////////
    /////////// Configuration of applying isomap///////////////
    ///////////////////////////////////////////////////////////

    // The eigen values and vectors need to compute, when
    // processing the sub-charts which have been partitioned
    // before.
    const size_t SUB_CHART_EIGEN_DIMENSION = 4;

    // Using to ignore the eigen vectors whose eigen value are too small.
    // Only cosider the larger eigen values which consume 90%
    // percent of all energy presenting by sum of all eigen values.
    const float PRIMARY_EIGEN_ENERGY_PERCENT = 0.90f;

    // To check if current chart is a special-shape, each vertex must has
    // a 3 dimension vector.
    const size_t DIMENSION_TO_CHECK_SPECIAL_SHAPE = 3;
}

/////////////////////////////////////////////////////////////
/////////////////Constructor and Decstructor/////////////////
/////////////////////////////////////////////////////////////

CIsochartMesh::CIsochartMesh(
    const CBaseMeshInfo& baseInfo,
    CCallbackSchemer& callbackSchemer,    
    const CIsochartEngine &IsochartEngine)
    :m_baseInfo(baseInfo),
    m_callbackSchemer(callbackSchemer),
    m_pVerts(nullptr),
    m_pFaces(nullptr),
    m_pPackingInfo(nullptr),
    m_dwFaceNumber(0),
    m_dwVertNumber(0),
    m_bVertImportanceDone(false),
    m_bIsSubChart(false),
    m_bIsInitChart(false),
    m_bIsParameterized(false),
    m_IsochartEngine(IsochartEngine)
{
    m_fParamStretchL2 = 0;
    m_fParamStretchLn = 0;
    m_fBaseL2Stretch = 0;
    m_fGeoL2Stretch = 0;
    m_fChart2DArea = 0;
    m_fChart3DArea = 0;
    m_bOptimizedL2Stretch = false;
    m_bOrderedLandmark = false;
    m_bNeedToClean = false;
}

CIsochartMesh::~CIsochartMesh()
{
    Free();
}

void CIsochartMesh::DeleteChildren()
{
    for (size_t i=0; i<m_children.size(); i++)
    {
        delete m_children[i];
    }
    m_children.clear();
}

void CIsochartMesh::Free()
{
    SAFE_DELETE_ARRAY(m_pVerts);
    SAFE_DELETE_ARRAY(m_pFaces);

    DestroyPakingInfoBuffer();
    DeleteChildren();
}

/////////////////////////////////////////////////////////////
//////////////////////Class Public Methods //////////////////
/////////////////////////////////////////////////////////////

// detect whether or not the mesh has boundary vertices
bool CIsochartMesh::HasBoundaryVertex() const
{
    for (size_t i = 0; i < m_dwVertNumber; ++i)
    {
        if (m_pVerts[i].bIsBoundary)
        {
            return true;
        }
    }
    return false;
}

// Convert external stretch to the internal stretches
// See more details in [SSGH01] page 2-3:
void CIsochartMesh::	ConvertToInternalCriterion(
    float fStretch,
    float& fCriterion,
    bool bIsSignalSpecialized)
{
    assert(fStretch >=0.0f && fStretch <= 1.0f);

    DPF(3,"Convert Stretch...");

    // Stretch L2 correspond to external Stretch.
    // fStretch == 0 --> fStretchL2 == 1;
    // fStretch == 1 --> fStretchL2 == ISOCHART_STRETCH_LN

    float fTemp;

    if (bIsSignalSpecialized)
    {
        fTemp = 1 - pow(fStretch, POW_OF_IMT_GEO_L2_STRETCH);	
    }
    else
    {
        fTemp = 1 - fStretch;
    }
    
    if (IsInZeroRange(fTemp))
    {
        fCriterion = ISOCHART_MAX_STRETCH_RATIO;
    }
    else
    {
        fCriterion = 1.0f / fTemp;
        if (fCriterion > ISOCHART_MAX_STRETCH_RATIO)
        {
            fCriterion = ISOCHART_MAX_STRETCH_RATIO;
        }
    }
}

// Convert from internal Stretch L2 to external Stretch
float CIsochartMesh::ConvertToExternalStretch(
    float fTotalAvgL2SquaredStretch,
    bool bIsSignalSpecialized)
{
    if (IsInZeroRange2(fTotalAvgL2SquaredStretch))
    {
        return 0;
    }

    float fStretch = 1.0f - 1.0f / fTotalAvgL2SquaredStretch;

    if (bIsSignalSpecialized)
    {
        fStretch = pow(fStretch, 1/POW_OF_IMT_GEO_L2_STRETCH);	
    }
    if (fStretch < 0)
    {
        return 0;
    } 
        
     return fStretch;
}

namespace
{
    // template function used to fill root chart face buffer
    // according to input face index type
    template <class INDEXTYPE>
    static void FillRootChartFaceBuffer(
        const void* pFaceIndexArray,
        ISOCHARTFACE* pFaceBuffer,
        size_t dwFaceCount)
    {
        const INDEXTYPE *pFacesInBase =
            static_cast<INDEXTYPE*>(const_cast<void*>(pFaceIndexArray));

        ISOCHARTFACE* pFace = pFaceBuffer;
        const INDEXTYPE *pFacesIn = pFacesInBase;

        for (uint32_t i=0; i < dwFaceCount; i++)
        {
            pFace->dwID = pFace->dwIDInRootMesh = i;
            pFace->dwVertexID[0] = pFacesIn[0];
            pFace->dwVertexID[1] = pFacesIn[1];
            pFace->dwVertexID[2] = pFacesIn[2];
            pFace++;
            pFacesIn += 3;
        }
    }
}

// Root chart is built directly from the input mesh.
HRESULT CIsochartMesh::BuildRootChart(
    CBaseMeshInfo& baseInfo, 
    const void* pFaceIndexArray, 
    DXGI_FORMAT IndexFormat, 
    CIsochartMesh* pChart,
    bool bIsForPartition)
{
    assert(pFaceIndexArray != 0);
    assert(pChart != 0);

    HRESULT hr = S_OK;

    size_t dwVertexCount = baseInfo.dwVertexCount;
    size_t dwFaceCount = baseInfo.dwFaceCount;

    assert(dwVertexCount > 0);
    assert(dwFaceCount > 0);

    // 1. allocate resource of root Mesh
    pChart->m_pFaces = new (std::nothrow) ISOCHARTFACE[dwFaceCount];
    if (!pChart->m_pFaces)
    {
        return E_OUTOFMEMORY;
    }

    pChart->m_pVerts = new (std::nothrow) ISOCHARTVERTEX[dwVertexCount];
    if (!pChart->m_pVerts)
    {
        SAFE_DELETE_ARRAY(pChart->m_pFaces);
        return E_OUTOFMEMORY;
    }

    // 2. fill in the basic information of the mesh.
    pChart->m_dwFaceNumber = dwFaceCount;
    pChart->m_dwVertNumber = dwVertexCount;

    for (uint32_t i = 0; i<dwVertexCount; i++)
    {
        pChart->m_pVerts[i].dwID = i;
        pChart->m_pVerts[i].dwIDInRootMesh = i;
    }

    if (DXGI_FORMAT_R32_UINT == IndexFormat)
    {
        FillRootChartFaceBuffer<uint32_t>(
            pFaceIndexArray,
            pChart->m_pFaces,
            dwFaceCount);
    }
    else if(DXGI_FORMAT_R16_UINT == IndexFormat)
    {
        FillRootChartFaceBuffer<uint16_t>(
            pFaceIndexArray,
            pChart->m_pFaces,
            dwFaceCount);
    }
    else
    {
        pChart->Free();
        return E_FAIL;
    }

    if (baseInfo.bIsFaceAdjacenctArrayReady)
    {
         FAILURE_RETURN(pChart->ReBuildRootChartByAdjacence());
    }

    // 3. Build full connection for root chart.
    bool bManifold = false;
    if (FAILED(hr = pChart->BuildFullConnection(bManifold)))
    {
        pChart->Free();
        return hr;
    }
    if (!bManifold)
    {
        pChart->Free();
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // 4. Get face adjacency for MergeSmallCharts().
    if (bIsForPartition)
    {
        pChart->m_fBoxDiagLen = baseInfo.fBoxDiagLen;
        if (!baseInfo.bIsFaceAdjacenctArrayReady)
        {
            pChart->GetFaceAdjacentArray(baseInfo.pdwFaceAdjacentArray);
        }
    }

    pChart->m_fChart3DArea = baseInfo.fMeshArea;
    pChart->m_fBaseL2Stretch = pChart->CalCharBaseL2SquaredStretch();
    return hr;
}

namespace
{
    static bool IsFacesShareEdge(
        uint32_t* rgdwAdjacency,
        uint32_t dwFace1,
        uint32_t dwFace2)
    {
        uint32_t *p1 = rgdwAdjacency + 3 * dwFace1;
#ifndef NDEBUG
        uint32_t *p2 = rgdwAdjacency + 3 * dwFace2;
#endif

        bool bResult =
            ((p1[0] == dwFace2) || (p1[1] == dwFace2) || (p1[2] == dwFace2));

        if (bResult)
        {
            assert(
            (p2[0] == dwFace1) || (p2[1] == dwFace1) || (p2[2] == dwFace1));
        }
        else
        {
            assert(
            !((p2[0] == dwFace1) || (p2[1] == dwFace1) || (p2[2] == dwFace1)));
        }

        return bResult;
    }

    struct EdgeInfoItem
    {
        uint32_t dwPeerVertID;
        uint32_t dwFaceID[2];
        bool bSplit;
    };

    static bool IsNeedToSplit(
        std::vector<EdgeInfoItem>& edgeList,
        uint32_t dwPeerVertID,
        uint32_t dwCurrentFaceID,
        uint32_t* rgdwAdjacency,
        EdgeInfoItem** ppEdge)
    {
        for (size_t i=0; i < edgeList.size(); i++)
        {
            EdgeInfoItem& et = edgeList[i];
            if (dwPeerVertID == et.dwPeerVertID)
            {
                assert(et.dwFaceID[0] != INVALID_FACE_ID);
                *ppEdge = &et;
                if (et.bSplit)
                {
                    return true;
                }
                if (et.dwFaceID[1] != INVALID_FACE_ID)
                {
                    return true;
                }
                if (!IsFacesShareEdge(
                    rgdwAdjacency, et.dwFaceID[0], dwCurrentFaceID))
                {
                    return true;
                }
                et.dwFaceID[1] = dwCurrentFaceID;
                return false;
            }
        }

        EdgeInfoItem edgeInfo;
        edgeInfo.dwPeerVertID = dwPeerVertID;
        edgeInfo.dwFaceID[0] = dwCurrentFaceID;
        edgeInfo.dwFaceID[1] = INVALID_FACE_ID;
        edgeInfo.bSplit = false;
        edgeList.push_back(edgeInfo);
        return false;
    }

    static HRESULT AddConnectedFalseEdges(std::vector<uint32_t> *pList, const uint32_t *pdwAdj, const uint32_t *pdwFalseEdges, uint32_t uFace)
    {
        if (isInArray(*pList, uFace))
            return S_OK;

        try
        {
            pList->push_back(uFace);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        if (!pdwFalseEdges)
            return S_OK;

        HRESULT hr = S_OK;

        for (size_t i = 0; i < 3; i++)
        {
            size_t uNeighbor = uFace * 3 + i;
            if (pdwFalseEdges[uNeighbor] != -1)
            {
                if (FAILED(hr = AddConnectedFalseEdges(pList, pdwAdj, pdwFalseEdges, pdwAdj[uNeighbor])))
                    return hr;
            }
        }

        return S_OK;
    }

    static HRESULT SplitSharedEdges(
        const uint32_t* rgdwFalseEdges,
        uint32_t* rgdwAdjacency,
        uint32_t* rgdwFaceIdx,
        size_t dwFaceCount,
        size_t& dwNewVertCount,
        bool& bChangedVertex)
    {
        bChangedVertex = false;

        std::vector<uint32_t> splitFaceList;
        std::unique_ptr<std::vector<EdgeInfoItem>[]> pVertEdgeList( new (std::nothrow) std::vector<EdgeInfoItem>[dwNewVertCount] );
        if (!pVertEdgeList)
        {
            return E_OUTOFMEMORY;
        }

        std::vector<uint32_t> splitEdgePos;

        uint32_t *pIdx = rgdwFaceIdx;
        for (uint32_t iFace = 0; iFace < dwFaceCount; iFace++)
        {
            for (size_t iVert = 0; iVert < 3; iVert++)
            {
                uint32_t v1 = pIdx[iVert];
                uint32_t v2 = pIdx[(iVert + 1) % 3];

                if (v1 > v2)
                {
                    std::swap(v1, v2);
                }

                EdgeInfoItem* pEdge = nullptr;
                if (IsNeedToSplit(
                    pVertEdgeList[v1],
                    v2,
                    iFace,
                    rgdwAdjacency,
                    &pEdge))
                {
                    HRESULT hr = AddConnectedFalseEdges(&splitFaceList, rgdwAdjacency, rgdwFalseEdges, iFace);
                    if (FAILED(hr))
                    {
                        return hr;
                    }
                    if (!pEdge->bSplit)
                    {
                        if (!addNoduplicateItem(splitFaceList, pEdge->dwFaceID[0]))
                        {
                            return E_OUTOFMEMORY;
                        }
                        if (pEdge->dwFaceID[1] != INVALID_FACE_ID)
                        {
                            if (!addNoduplicateItem(splitFaceList, pEdge->dwFaceID[1]))
                            {
                                return E_OUTOFMEMORY;
                            }
                        }
                        pEdge->bSplit = true;
                    }
                }
            }
            pIdx += 3;
        }

        bChangedVertex = !(splitFaceList.empty());
        for (size_t i=0; i < splitFaceList.size(); i++)
        {
            uint32_t dwFaceID = splitFaceList[i];
            pIdx = rgdwFaceIdx + 3 * dwFaceID;
            uint32_t *pAdjacency = rgdwAdjacency + 3 * dwFaceID;
            const uint32_t dummy[3] = { uint32_t(-1), uint32_t(-1), uint32_t(-1) };
            const uint32_t *pFalseEdge = rgdwFalseEdges ? (rgdwFalseEdges + 3 * dwFaceID) : dummy;
            for (size_t iVert = 0; iVert < 3; iVert++)
            {
                pIdx[iVert] = static_cast<uint32_t>(dwNewVertCount++);

                if (pAdjacency[iVert] == INVALID_FACE_ID || (pFalseEdge[iVert] != -1))
                {
                    continue;
                }
                uint32_t *pPeerAdjacency = rgdwAdjacency + 3 * pAdjacency[iVert];
                pAdjacency[iVert] = INVALID_FACE_ID;
                for (size_t j = 0; j < 3; j++)
                {
                    if (pPeerAdjacency[j] == dwFaceID)
                    {
                        pPeerAdjacency[j] = INVALID_FACE_ID;
                    }
                }
            }
        }

        return S_OK;
    }

    static HRESULT ReorderVertices(
        const  uint32_t* rgdwAdjacency,
        uint32_t* rgdwNewFaceIdx,
        size_t dwFaceCount,
        size_t& dwNewVertCount)
    {
        CVertIter vertIter(rgdwAdjacency);
        memset(rgdwNewFaceIdx, 0xff, dwFaceCount * 3 * sizeof(uint32_t));

        dwNewVertCount = 0;
        for (uint32_t iFace = 0; iFace < dwFaceCount; iFace++)
        {
            for (uint32_t iVert = 0; iVert < 3; iVert++)
            {
                if (rgdwNewFaceIdx[iFace * 3 + iVert] != INVALID_VERT_ID)
                {
                    continue;
                }

                if (!vertIter.Init(iFace, iVert, dwFaceCount))
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }

                uint32_t dwCenterVertID = static_cast<uint32_t>(dwNewVertCount++);

                do
                {
                    uint32_t dwCurFaceID = vertIter.GetCurrentFace();
                    uint32_t dwCurVertIdx = vertIter.GetCurrentVertIdx();

                    FACE_ARRAY_ITME(
                        rgdwNewFaceIdx, dwCurFaceID, dwCurVertIdx) = dwCenterVertID;
                } while (vertIter.HasNextFace()
                    && vertIter.NextFace());
            }
        }

        return S_OK;
    }
}

HRESULT CIsochartMesh::ReBuildRootChartByAdjacence()
{
    assert(m_baseInfo.bIsFaceAdjacenctArrayReady);

    std::unique_ptr<uint32_t[]> rgdwNewFaceIdx(new (std::nothrow) uint32_t[m_dwFaceNumber * 3]);
    if (!rgdwNewFaceIdx)
    {
        return E_OUTOFMEMORY;
    }

    size_t dwNewVertCount;
    bool bChangedVertex;
    do
    {
        dwNewVertCount = 0;
        HRESULT hr=ReorderVertices(
            m_baseInfo.pdwFaceAdjacentArray,
            rgdwNewFaceIdx.get(),
            m_dwFaceNumber,
            dwNewVertCount);
        if (FAILED(hr))
        {
            return hr;
        }

        bChangedVertex = false;
        if (FAILED(hr = SplitSharedEdges(
            m_baseInfo.pdwSplitHint,
            m_baseInfo.pdwFaceAdjacentArray,
            rgdwNewFaceIdx.get(),
            m_dwFaceNumber,
            dwNewVertCount,
            bChangedVertex)))
        {
            return hr;
        }
    }while(bChangedVertex);

    if (dwNewVertCount != m_dwVertNumber)
    {
        SAFE_DELETE_ARRAY(m_pVerts);
        m_dwVertNumber = dwNewVertCount;
        m_pVerts = new (std::nothrow) ISOCHARTVERTEX[m_dwVertNumber];
        if (!m_pVerts)
        {
            return E_OUTOFMEMORY;
        }
    }
    
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        m_pVerts[i].dwID = static_cast<uint32_t>(i);
        m_pVerts[i].dwIDInRootMesh = INVALID_VERT_ID;
    }

    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        ISOCHARTFACE& face = m_pFaces[i];
        for (size_t j=0; j<3; j++)
        {
            uint32_t dwNewID = FACE_ARRAY_ITME(rgdwNewFaceIdx.get(), i, j);
            assert(dwNewID != INVALID_VERT_ID);
            
            if (m_pVerts[dwNewID].dwIDInRootMesh == INVALID_VERT_ID)
            {
                m_pVerts[dwNewID].dwIDInRootMesh = face.dwVertexID[j];								
            }

            face.dwVertexID[j] = dwNewID;
        }
    }

    return S_OK;
}

/////////////////////////////////////////////////////////////
////////////////////Algorithm Public Methods/////////////////
/////////////////////////////////////////////////////////////

// Simple chart:
// If a chart satisfied following 2 conditions, it is a "simple chart".
//		a. only 1 object
//		b. only 1 boundary

// Two conditions must be met to be a partition-able chart:
//		a. Must be a simple chart
//		b. Vertex importance order computation has been done.
HRESULT CIsochartMesh::PrepareProcessing(
    bool bIsForPartition)
{
    HRESULT hr = S_OK;
    size_t dwBoundaryNumber = 0;
    bool bIsSimpleChart = false;

    // 1. Check if current chart is a simple chart. 
    // Otherwise, try to make it simpler (Export individual charts and
    // merge multiple boundaries).
    hr = PrepareSimpleChart(
        bIsForPartition,
        dwBoundaryNumber, 
        bIsSimpleChart);
    
    if (FAILED(hr) || !bIsSimpleChart)
    {
        return hr;
    }

    if (bIsForPartition)
    {
        // 2. Calculate vertex importance in the simple chart
        // using mesh simplify algorithm
        if (SUCCEEDED(hr = CalculateVertImportanceOrder()))
        {
            m_bVertImportanceDone = true;
            m_bIsInitChart = true;
        }
    }

    return hr;
}

// Partition by stretch only. 
// See more detail about algorithm of isochart in :  [Kun04]
HRESULT CIsochartMesh::Partition()
{
    assert(m_bVertImportanceDone);

    HRESULT hr = S_OK;

    // With/without IMT, pfVertGeodesicDistance contains geodesic distance.
    // With IMT, pfVertParameterDistance combines geodesic & signal distance,
    // Without IMT, pfVertParameterDistance just equals to pfVertGeodesicDistance

    float* pfVertGeodesicDistance = nullptr;
    float* pfVertCombineDistance = nullptr;
    float* pfVertMappingCoord = nullptr;

    size_t dwBoundaryNumber = 0;
    bool bIsSimpleChart = false;
    bool bSpecialShape = false;
    bool bTrivialShape = false;

    // 1. Prepare simple chart
    if (FAILED(hr = PrepareSimpleChart(
        true,
        dwBoundaryNumber, 
        bIsSimpleChart)) || !bIsSimpleChart)
    {
        // bIsSimpleChart == false is not an error and need no error code
        // under this condition, current chart has already been changed to
        // simpler charts contained in current charts children list.	
        return hr;
    }

    if (m_dwFaceNumber == 1)
    {
        ParameterizeOneFace(
            false, m_pFaces);
        return S_OK;
    }

    // 2. Process Plane.
    bool bPlaneShape = false;
    if (FAILED(hr = ProcessPlaneShape(bPlaneShape)) || bPlaneShape)
    {
        return hr;
    }	

    // 2. Apply Isomap to parameterize current chart
    size_t dwPrimaryEigenDimension;
    size_t dwMaxEigenDimension;
    bool bIsLikePlane = false;
    if (FAILED(hr = IsomapParameterlization(
        bIsLikePlane,
        dwPrimaryEigenDimension,
        dwMaxEigenDimension,
        &pfVertGeodesicDistance,
        &pfVertCombineDistance,
        &pfVertMappingCoord)) || bIsLikePlane)
    {
        goto LEnd;
    }

    // 3. Detect and process trivial shape.
    // Trivial shape includes: 
    //  a. chart with only one face
    //  b. chart been degenerated to a point
    if (FAILED(hr = ProcessTrivialShape(
        dwPrimaryEigenDimension, 
        bTrivialShape)) || bTrivialShape)
    {
        goto LEnd;
    }

    // 4. Detect and process special chart.
    // Special chart includes:
    //  a. Cylinder
    //  b. Longhorn

    hr = ProcessSpecialShape(
            dwBoundaryNumber,
            pfVertGeodesicDistance,
            pfVertCombineDistance,
            pfVertMappingCoord,
            dwPrimaryEigenDimension,
            dwMaxEigenDimension,
            bSpecialShape);
    if ( FAILED(hr) || (bSpecialShape && !m_children.empty()))
    {
        goto LEnd;
    }
    
    // 5. Current chart is not a simple chart or a special chart, then,
    // process general shape.
    hr = ProcessGeneralShape(
        dwPrimaryEigenDimension,
        dwBoundaryNumber,
        pfVertGeodesicDistance,
        pfVertCombineDistance,
        pfVertMappingCoord);
LEnd:
    m_isoMap.Clear();

    if (!IsIMTSpecified())
    {
        assert(pfVertGeodesicDistance == pfVertCombineDistance);
        SAFE_DELETE_ARRAY(pfVertGeodesicDistance);
    }
    else
    {
        SAFE_DELETE_ARRAY(pfVertGeodesicDistance);
        SAFE_DELETE_ARRAY(pfVertCombineDistance);
    }
    SAFE_DELETE_ARRAY(pfVertMappingCoord);
    return hr;
}

HRESULT CIsochartMesh::ComputeBiParitionLandmark()
{
    HRESULT hr = S_OK;
    if (m_bOrderedLandmark)
    {
        return S_OK;
    }
    
    float fMaxDistance = -FLT_MAX;

    if (m_landmarkVerts.size() < 2)
    {
        uint32_t dwIdx1 = INVALID_INDEX;
        uint32_t dwIdx2 = INVALID_INDEX;

        for (uint32_t ii = 0; ii<m_dwVertNumber - 1; ii++)
        {
            for (uint32_t jj = ii + 1; jj < m_dwVertNumber; jj++)
            {
                float fDeltaX = (m_pVerts[ii].uv.x-m_pVerts[jj].uv.x);
                float fDeltaY = (m_pVerts[ii].uv.y-m_pVerts[jj].uv.y);
                //fDeltaX and fDeltaY has been scaled in 0~500, no overflow here
                float fTempDistance = fDeltaX*fDeltaX + fDeltaY*fDeltaY;
                if (fMaxDistance < fTempDistance)
                {
                    fMaxDistance = fTempDistance;
                    dwIdx1= ii;
                    dwIdx2= jj;
                }
            }
        }

        try
        {
            m_landmarkVerts.push_back(dwIdx1);
            m_landmarkVerts.push_back(dwIdx2);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

    }
    else
    {
        uint32_t dwIdx1 = INVALID_INDEX;
        uint32_t dwIdx2 = INVALID_INDEX;

        for (uint32_t ii = 0; ii<m_landmarkVerts.size() - 1; ii++)
        {
            uint32_t id1 = m_landmarkVerts[ii];
            for (uint32_t jj = ii + 1; jj < m_landmarkVerts.size(); jj++)
            {
                uint32_t id2 = m_landmarkVerts[jj];
                float fDeltaX = (m_pVerts[id1].uv.x-m_pVerts[id2].uv.x);
                float fDeltaY = (m_pVerts[id1].uv.y-m_pVerts[id2].uv.y);
                //fDeltaX and fDeltaY has been scaled in 0~500, no overflow here
                float fTempDistance = fDeltaX*fDeltaX + fDeltaY*fDeltaY;
                if (fMaxDistance < fTempDistance)
                {
                    fMaxDistance = fTempDistance;
                    dwIdx1= ii;
                    dwIdx2= jj;
                }
            }
        }

        FAILURE_RETURN(
        MoveTwoValueToHead(m_landmarkVerts, dwIdx1, dwIdx2));
    }

    return S_OK;

}

HRESULT CIsochartMesh::Bipartition3D()
{
    assert(m_bIsParameterized);

    if (m_dwFaceNumber == 1)
    {
        ParameterizeOneFace(
            IsIMTSpecified(), m_pFaces);
        return S_OK;
    }

    HRESULT hr = S_OK;

    FAILURE_RETURN(ComputeBiParitionLandmark());

#if OPT_3D_BIPARTITION_BOUNDARY_BY_ANGLE
    size_t dwLandCount = 2;
    bool bOptByAngle = true;
#else
    size_t dwLandCount = m_landmarkVerts.size();
    bool bOptByAngle = false;	
#endif

    std::vector<uint32_t> representativeVertsIdx;	
    float* pfVertCombineDistance = nullptr;
    bool bIsPartitionSucceed = false;

    // 1. Calculate Distance (Geodesic & Siganl)  between vertices and landmarks.
    float* pfVertGeoDistance = new (std::nothrow) float[dwLandCount*m_dwVertNumber];
    if (!pfVertGeoDistance)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }

    if (IsIMTSpecified())
    {
        pfVertCombineDistance = new (std::nothrow) float[dwLandCount*m_dwVertNumber];
        if (!pfVertCombineDistance)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }		
    }
    else
    {
        pfVertCombineDistance = pfVertGeoDistance;
    }

    try
    {
        representativeVertsIdx.resize(dwLandCount);
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }

    for (size_t ii=0; ii<representativeVertsIdx.size(); ii++)
    {
        representativeVertsIdx[ii] = m_landmarkVerts[ii];
    }

    if (FAILED(hr = CalculateGeodesicDistance(
            representativeVertsIdx, 
            pfVertCombineDistance, 
            pfVertGeoDistance)))
    {
        goto LEnd;
    }

    representativeVertsIdx[0]=0;
    representativeVertsIdx[1]=1;
    // 2. Partition 
    FAILURE_RETURN(
        PartitionGeneralShape(
            pfVertGeoDistance,
            pfVertCombineDistance,
            representativeVertsIdx,
            bOptByAngle,
            bIsPartitionSucceed));
    if (bIsPartitionSucceed && m_children.size() > 1)
    {
        goto LEnd;
    }
    else
    {
        m_children.clear();
    }

    // 3. if Failed to partition on 3D surface, just partitioning On Domain Surface
    if (FAILED(hr = Bipartition2D()))
    {
        goto LEnd;
    }
    if (m_children.size() != 2)
    {
        m_children.clear();
        hr = PartitionEachFace();
    }
    
    //assert(m_children.size() == 2);
LEnd:
    if (!IsIMTSpecified())
    {
        assert(pfVertCombineDistance == pfVertGeoDistance);
        SAFE_DELETE_ARRAY(pfVertGeoDistance);	
    }
    else
    {
        SAFE_DELETE_ARRAY(pfVertCombineDistance);
        SAFE_DELETE_ARRAY(pfVertGeoDistance);
    }

    return hr;
}

// Partition a parameterized chartinto 2 sub-charts.
// This function is used when partitioning by number
HRESULT CIsochartMesh::Bipartition2D( )
{
    // Only the parameterized chart can be bipartitioned
    assert(m_bIsParameterized);

    HRESULT hr = S_OK;

    // 2. Find the two vertices have largest distance in UV-atlas
    float fMaxDistance = -FLT_MAX;

    std::vector<uint32_t> keyVerts;
    float* pfVertCombineDistance = nullptr;

    try
    {
        keyVerts.resize(2);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    for (uint32_t ii = 0; ii<m_dwVertNumber - 1; ii++)
    {
        for (uint32_t jj = ii + 1; jj < m_dwVertNumber; jj++)
        {
            float fDeltaX = (m_pVerts[ii].uv.x-m_pVerts[jj].uv.x);
            float fDeltaY = (m_pVerts[ii].uv.y-m_pVerts[jj].uv.y);
            //fDeltaX and fDeltaY has been scaled in 0~500, no overflow here
            float fTempDistance = fDeltaX*fDeltaX + fDeltaY*fDeltaY;
            if (fMaxDistance < fTempDistance)
            {
                fMaxDistance = fTempDistance;
                keyVerts[0] = ii;
                keyVerts[1] = jj;
            }
        }
    }

    // 3. Calculate the goedesic distance from other vertices to these 2
    // vertices
    float* pfVertGeoDistance = new (std::nothrow) float[2 * m_dwVertNumber];
    if (!pfVertGeoDistance)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }

    if (IsIMTSpecified())
    {
        pfVertCombineDistance = new (std::nothrow) float[2 * m_dwVertNumber];
        if (!pfVertCombineDistance)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }		
    }
    else
    {
        pfVertCombineDistance = pfVertGeoDistance;
    }

    hr = CalculateGeodesicDistance(
            keyVerts, 
            pfVertCombineDistance, 
            pfVertGeoDistance);

    if (FAILED(hr))
    {
        goto LEnd;
    }

    // 4. Partition current chart according to pfVertGeodesicDistance
    keyVerts[0] = 0; // indicate the offset of representative vertices
    keyVerts[1] = 1; // in pfVertGeodesicDistance
    hr = BiPartitionParameterlizeShape(
            pfVertCombineDistance,
            keyVerts);
LEnd:
    if (!IsIMTSpecified())
    {
        assert(pfVertCombineDistance == pfVertGeoDistance);
        SAFE_DELETE_ARRAY(pfVertGeoDistance);	
    }
    else
    {
        SAFE_DELETE_ARRAY(pfVertCombineDistance);
        SAFE_DELETE_ARRAY(pfVertGeoDistance);
    }
    return hr;
}

// Simple chart
// A chart with 1 boundary, and all vertices in the chart are connected.
// Because isomap can only process simple charts, each chart must be 
// simplifed to simple chart before applying isomap on it
HRESULT CIsochartMesh::PrepareSimpleChart(
    bool bIsForPartition,
    size_t& dwBoundaryNumber,
    bool& bIsSimpleChart)
{
    HRESULT hr = S_OK;

    dwBoundaryNumber = 0;
    bIsSimpleChart = false;

    // 1. Check if current chart has multiple objects,
    // if true, split the chart and return for next iteration
    bool bHasMultiObjects = false;
    if (FAILED(hr = CheckAndDivideMultipleObjects(bHasMultiObjects)))
    {
        return hr;
    }

    // It breaks original isochart logic
    if (bHasMultiObjects)
    {
        hr = m_callbackSchemer.CheckPointAdapt();
        assert(m_children.size() > 0);
        return hr;
    }

    if (!bIsForPartition)
    {
        bIsSimpleChart = true;
        return hr;
    }

    // 2. Now the chart has only one object
    // Check if it has multiple boundaries, if true, merge 2 boundaries 
    // and return. 

    // Note, if the original chart has N boundaries, it decreases only 1 
    // boundary each time and builds a new chart with N-1 boundaries. 
    // The new chart will be processed in future.
    // Note that because cut boundaies caused complex change of mesh,
    // topology and sometime generated multiple objects, simple iteration
    // at here may not work.
    if (FAILED(hr = CheckAndCutMultipleBoundaries(dwBoundaryNumber)))
    {
        return hr;
    }
    if (!m_children.empty())
    {
        hr = m_callbackSchemer.CheckPointAdapt();
        assert(m_children.size() > 0);
        return hr;
    }

    // 3. No mutiple object and only one boundary
    bIsSimpleChart = true;
    return hr;
}

// Parameterize simple chart by isomap :[Kun04]
HRESULT CIsochartMesh::IsomapParameterlization(
    bool& bIsLikePlane,
    size_t& dwPrimaryEigenDimension,
    size_t& dwMaxEigenDimension,
    float** ppfVertGeodesicDistance,
    float** ppfVertCombineDistance,
    float** ppfVertMappingCoord)
{
    assert(ppfVertGeodesicDistance != 0);
    assert(ppfVertCombineDistance != 0);
    assert(ppfVertMappingCoord != 0);

    HRESULT hr = S_OK;	
    bIsLikePlane = false;

    bool bIsSignalSpecialized = IsIMTSpecified();
    float* pfVertGeodesicDistance = nullptr;
    float* pfVertCombinedDistance = nullptr;
    float* pfGeodesicMatrix = nullptr;
    float* pfVertMappingCoord = nullptr;
    size_t dwLandmarkNumber = 0;
    size_t dwCalculatedDimension = 0;

    // 1. Calculate the landmark vertices
    if (FAILED(hr = CalculateLandmarkVertices(
                        MIN_LANDMARK_NUMBER,
                        dwLandmarkNumber)))
    {
        goto LEnd;
    }

    // 2. Calculate the geodesic distance matrix of landmark vertices
    pfVertGeodesicDistance = new (std::nothrow) float[dwLandmarkNumber * m_dwVertNumber];

    if (bIsSignalSpecialized)
    {
        pfVertCombinedDistance = new (std::nothrow) float[dwLandmarkNumber * m_dwVertNumber];
    }
    else
    {
        pfVertCombinedDistance = pfVertGeodesicDistance;
    }
    pfGeodesicMatrix = new (std::nothrow) float[dwLandmarkNumber * dwLandmarkNumber];
    if (!pfVertGeodesicDistance || !pfGeodesicMatrix || !pfVertCombinedDistance)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }
    
    if (FAILED(hr = CalculateGeodesicDistance(
                        m_landmarkVerts,
                        pfVertCombinedDistance,
                        pfVertGeodesicDistance)))
    {
        goto LEnd;
    }

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
    CalculateGeodesicMatrix(
        m_landmarkVerts,
        pfVertCombinedDistance,
        pfGeodesicMatrix);

#else
    CalculateGeodesicMatrix(
        m_landmarkVerts,
        pfVertGeodesicDistance,
        pfGeodesicMatrix);
#endif

    // 4. Perform Isomap to do surface spectral analysis
    if (m_bIsSubChart)
    {
        dwMaxEigenDimension = std::min(SUB_CHART_EIGEN_DIMENSION, dwLandmarkNumber);
    }
    else
    {
        dwMaxEigenDimension = std::min(ORIGINAL_CHART_EIGEN_DIMENSION, dwLandmarkNumber);
    }
    if (FAILED( hr = m_isoMap.Init(
                        dwLandmarkNumber,
                        pfGeodesicMatrix)))
    {
        goto LEnd;
    }

    if (FAILED(hr = m_isoMap.ComputeLargestEigen(
                        dwMaxEigenDimension,
                        dwCalculatedDimension)))
    {
        goto LEnd;
    }
    SAFE_DELETE_ARRAY(pfGeodesicMatrix);

    assert(dwMaxEigenDimension >= dwCalculatedDimension);
    
    dwMaxEigenDimension = dwCalculatedDimension;
    dwPrimaryEigenDimension = 0;
    if (FAILED( hr = m_isoMap.GetPrimaryEnergyDimension(
                        PRIMARY_EIGEN_ENERGY_PERCENT,
                        dwPrimaryEigenDimension)))
    {
        goto LEnd;
    }

    // If current chart degenerated to a point, dwPrimaryEigenDimension
    // will equal to 0
    if (0 == dwPrimaryEigenDimension)
    {
        goto LEnd;
    }

    if (FAILED(hr = ProcessPlaneLikeShape(
        dwCalculatedDimension,
        dwPrimaryEigenDimension,
        bIsLikePlane)) || bIsLikePlane)
    {
        goto LEnd;
    }

    // if CIsomap::GetPrimaryEnergyDimension discard too many vector 
    // demensions which are needed by special-shape detecting, just 
    // set it back to DIMENSION_TO_CHECK_SPECIAL_SHAPE
    if ( dwPrimaryEigenDimension < DIMENSION_TO_CHECK_SPECIAL_SHAPE
        && dwCalculatedDimension >= DIMENSION_TO_CHECK_SPECIAL_SHAPE)
    {
        dwPrimaryEigenDimension = DIMENSION_TO_CHECK_SPECIAL_SHAPE;
    }

    //5. Compute n-dimensional embedding coordinates of each vertex
    //   here, n = dwPrimaryEigenDimension
    pfVertMappingCoord = new (std::nothrow) float[m_dwVertNumber * dwPrimaryEigenDimension];
    if (!pfVertMappingCoord)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
    if (FAILED(hr = CalculateVertMappingCoord(
                        pfVertCombinedDistance,
                        dwLandmarkNumber,
                        dwPrimaryEigenDimension,
                        pfVertMappingCoord)))
    {
        goto LEnd;
    }
#else
    if (FAILED(hr = CalculateVertMappingCoord(
                        pfVertGeodesicDistance,
                        dwLandmarkNumber,
                        dwPrimaryEigenDimension,
                        pfVertMappingCoord)))
    {
        goto LEnd;
    }
    
#endif

    m_bIsParameterized = true;
LEnd:
    SAFE_DELETE_ARRAY(pfGeodesicMatrix);
    if (FAILED(hr))
    {
        SAFE_DELETE_ARRAY(pfVertGeodesicDistance);
        if (bIsSignalSpecialized)
        {
            SAFE_DELETE_ARRAY(pfVertCombinedDistance);
        }
        SAFE_DELETE_ARRAY(pfVertMappingCoord);
    }
    else
    {
        *ppfVertCombineDistance = pfVertCombinedDistance;
        *ppfVertGeodesicDistance = pfVertGeodesicDistance;
        *ppfVertMappingCoord = pfVertMappingCoord;
    }

    return hr;
}

/////////////////////////////////////////////////////////////
/////////////////////Build Full Connection Methods///////////
/////////////////////////////////////////////////////////////

//Build Full connection means:
// 1. Scan all edges, get adjacent face for each edge.
// 2. Scan all vertices, get adjacent vertices and faces for each vertex.
HRESULT CIsochartMesh::BuildFullConnection(bool& bIsManifold)
{
    HRESULT hr = S_OK;
    
    assert (m_pVerts != 0);
    assert (m_pFaces != 0);

    // 1. Clear old adjacence of each vertex.
    ClearVerticesAdjacence();

    // 2. Find all edges, add vertex's adjacent faces and edges
    // if more than 2 faces share one edge, it's not a valid toplogy
    hr = FindAllEdges(bIsManifold);
    if (FAILED(hr) || !bIsManifold)
    {
        return hr;
    }

    // 3. If we need to clean the mesh. try to clean bowtie by add new vertices.
    if (m_bNeedToClean)
    {
        // 3.1 Clean the mesh by add new vertices
        m_bNeedToClean = false;

        // 3.2 CleanMesh
        bool bCleaned = false;
        if (FAILED(hr = CleanNonmanifoldMesh(bCleaned)))
        {
            return hr;
        }

        // 3.3 Re-find all edges on the rebuilt mesh.
        if (bCleaned)
        {
            ClearVerticesAdjacence();
            hr = FindAllEdges(bIsManifold);
            if (FAILED(hr) || !bIsManifold)
            {
                return hr;
            }
        }
    }
    
    // 4. Check face index order
    bIsManifold = IsAllFaceVertexOrderValid();
    if (!bIsManifold)
    {
        return hr;
    }

    // 5. Build Adjacent vertices array of each vertex. 
    // sort them in the same order.
    hr = SortAdjacentVertices(bIsManifold);
    if (FAILED(hr) || !bIsManifold)
    {
        return hr;
    }	

    // 6.
    // Decide if the edges can be splitted
    hr = SetEdgeSplitAttribute();

    return hr;
}

void CIsochartMesh::ClearVerticesAdjacence()
{
    ISOCHARTVERTEX* pVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pVertex->vertAdjacent.clear();
        pVertex->edgeAdjacent.clear();
        pVertex->faceAdjacent.clear();
        pVertex++;
    }
    return;
}

namespace
{
    // Internal structure Used in scanning all edges in a mesh.
    struct EdgeTableItem
    {
        uint32_t dwPeerVert;
        uint32_t dwEdgeIndex;
    };
}

//1. Find All Edges, specify the 3 edges of each face
//.Algorithm:
//(1) scan each face, check the 3 edges of each face
//(2) if the edge is not in the edge table, create new edges and put it into edge table.
//(3) to avoid put one edge twice, only store the edge whoes first vertex id is smaller than second

// Note if  More than 2 faces share the same edge, it's a non-manifold mesh
HRESULT CIsochartMesh::FindAllEdges(
    bool& bIsManifold)
{	
    ISOCHARTEDGE*  pEdge;
    ISOCHARTEDGE tempEdge;
    EdgeTableItem tempEdgeTableItem;

    bIsManifold = false;

    m_dwEdgeNumber = 0;
    m_edges.clear();

    std::unique_ptr<std::vector<EdgeTableItem>[]> pVertEdges( new (std::nothrow) std::vector<EdgeTableItem>[m_dwVertNumber] );
    if ( !pVertEdges )
    {
        return E_OUTOFMEMORY;
    }

    try
    {
        ISOCHARTFACE* pTriangle = m_pFaces;
        for (uint32_t i = 0; i<m_dwFaceNumber; i++)
        {
            uint32_t v1, v2;
            uint32_t edgeId;
            for (size_t j=0; j<3; j++)
            {
                pEdge = nullptr;
                v1 = pTriangle->dwVertexID[j];
                v2 = pTriangle->dwVertexID[(j+1)%3];

                m_pVerts[v1].faceAdjacent.push_back(i);
                if (v1 > v2)
                {
                    std::swap(v1, v2);
                }

                auto& et = pVertEdges[v1];
                size_t k;
                for (k=0; k<et.size(); k++)
                {
                    if (et[k].dwPeerVert == v2)
                    {
                        pEdge = &(m_edges[et[k].dwEdgeIndex]);
                        break;
                    }
                }
                if (!pEdge) // find new edge
                {
                    tempEdge.dwID = static_cast<uint32_t>(m_dwEdgeNumber);
                    tempEdge.dwVertexID[0] = pTriangle->dwVertexID[j];
                    tempEdge.dwVertexID[1] = pTriangle->dwVertexID[(j+1)%3];
                    tempEdge.dwOppositVertID[0] = pTriangle->dwVertexID[(j+2)%3];
                    tempEdge.dwOppositVertID[1] = INVALID_VERT_ID;
                    tempEdge.dwFaceID[0] = i;
                    tempEdge.dwFaceID[1] = INVALID_FACE_ID;
                    tempEdge.bIsBoundary = true;
                    tempEdge.bCanBeSplit = true;

                    m_edges.push_back(tempEdge);
                
                    tempEdgeTableItem.dwPeerVert = v2;
                    tempEdgeTableItem.dwEdgeIndex = static_cast<uint32_t>(m_dwEdgeNumber);
                    et.push_back(tempEdgeTableItem);

                    m_dwEdgeNumber++;
                    assert(m_dwEdgeNumber == m_edges.size());
                    edgeId = tempEdge.dwID;
                
                }
                else
                {	// at least 3 faces have the same edge, non-manifold
                    if (pEdge->dwFaceID[1] != INVALID_FACE_ID)
                    {
                        DPF(3,"Non-manifold: More than 2 faces have the same edge...\n");
                        return S_OK;
                    }

                    assert(pEdge->dwOppositVertID[1] == INVALID_VERT_ID);
                
                    pEdge->dwFaceID[1] = i;
                    pEdge->dwOppositVertID[1] = pTriangle->dwVertexID[(j+2)%3];
                    pEdge->bIsBoundary = false;

                    edgeId = pEdge->dwID;
                }
                pTriangle->dwEdgeID[j] = edgeId;
            
            }
            pTriangle++;
        }

        for (uint32_t i = 0; i < m_dwEdgeNumber; i++)
        {
            ISOCHARTEDGE &edge = m_edges[i];
            m_pVerts[edge.dwVertexID[0]].edgeAdjacent.push_back(i);
            m_pVerts[edge.dwVertexID[1]].edgeAdjacent.push_back(i);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    bIsManifold = true;
    return S_OK;
}

class VertFaceIter
{
private:
    uint32_t m_dwMainVertID;
    uint32_t m_dwBeginFace;
    uint32_t m_dwCurEdge;
    uint32_t m_dwCurFace;

    ISOCHARTFACE* m_pFaces;
    std::vector<ISOCHARTEDGE>& m_edges;
    
public:
    VertFaceIter(
        uint32_t mainVertID, uint32_t currEdge, uint32_t currFace,
        ISOCHARTFACE* pFaces, 
        std::vector<ISOCHARTEDGE>& edges):
        m_dwMainVertID(mainVertID),
        m_dwCurEdge(currEdge),
        m_dwCurFace(currFace),
        m_pFaces(pFaces),
        m_edges(edges)
    {
        m_dwBeginFace = currFace;
    }

    VertFaceIter(VertFaceIter const&) = delete;
    VertFaceIter& operator=(VertFaceIter const&) = delete;

    bool Next()
    {
        if (m_dwCurFace == INVALID_FACE_ID)
        {
            return false;
        }
        
        ISOCHARTFACE& face = m_pFaces[m_dwCurFace];
        for (size_t ii=0; ii<3; ii++)
        {
            if (face.dwEdgeID[ii] != m_dwCurEdge)
            {
                ISOCHARTEDGE& edge = m_edges[face.dwEdgeID[ii]];			
                if (edge.dwVertexID[0] == m_dwMainVertID
                ||edge.dwVertexID[1] == m_dwMainVertID)
                {
                    m_dwCurEdge = face.dwEdgeID[ii];
                    m_dwCurFace = 
                        (edge.dwFaceID[0] == m_dwCurFace)?
                        edge.dwFaceID[1]:
                        edge.dwFaceID[0];

                    break;
                }
            }
        }
        return (m_dwCurFace != INVALID_FACE_ID && m_dwCurFace != m_dwBeginFace);
    }
    uint32_t GetCurrEdge() { return m_dwCurEdge; }
    uint32_t GetCurrFace() { return m_dwCurFace; }
    bool IsBackToBegin() {return m_dwCurFace == m_dwBeginFace; }
};

HRESULT CIsochartMesh::CleanNonmanifoldMesh(bool& bCleaned)
{	
    HRESULT hr = S_OK;
    std::vector<uint32_t> vertexFaceList;
    std::vector<uint32_t> newVertMap;
    uint32_t dwNewVertID;

    DPF(0, "Try to clean the non-manifold mesh, generated by partition");

    try
    {
        bCleaned = true;
        dwNewVertID = static_cast<uint32_t>(m_dwVertNumber);

        std::unique_ptr<bool []> bProcessedEdge(new (std::nothrow) bool[m_dwEdgeNumber]);
        if (!bProcessedEdge)
            return E_OUTOFMEMORY;

        // 1. Find all vertices need to be splitted
        for (uint32_t ii = 0; ii < m_dwVertNumber; ii++)
        {
            ISOCHARTVERTEX& vert = m_pVerts[ii];

            uint32_t dwAdjEdgeCount = static_cast<uint32_t>(vert.edgeAdjacent.size());

            if (dwAdjEdgeCount <= 2) continue;

            memset(bProcessedEdge.get(), 0, sizeof(bool)*m_dwEdgeNumber);

            bool bIsRing = false;
            uint32_t dwClusterCount = 0;
            for (size_t jj = 0; jj < dwAdjEdgeCount; jj++)
            {
                uint32_t dwMainEdge = vert.edgeAdjacent[jj];
                if (bProcessedEdge[dwMainEdge]) continue;

                ISOCHARTEDGE& edge = m_edges[dwMainEdge];

                // Find One face culster			
                vertexFaceList.clear();
                for (size_t kk = 0; kk < 2; kk++)
                {
                    if (edge.dwFaceID[kk] == INVALID_FACE_ID)
                    {
                        continue;
                    }
                    VertFaceIter iter(ii, dwMainEdge, edge.dwFaceID[kk], m_pFaces, m_edges);

                    uint32_t dwCurrEdge = INVALID_INDEX;
                    do
                    {
                        dwCurrEdge = iter.GetCurrEdge();
                        bProcessedEdge[dwCurrEdge] = true;

                        if (dwClusterCount > 0)
                        {
                            vertexFaceList.push_back(iter.GetCurrFace());
                        }
                    } while (iter.Next());
                    bProcessedEdge[iter.GetCurrEdge()] = true;

                    if (iter.IsBackToBegin()) //Scaned all faces.
                    {
                        bIsRing = true;
                        break;
                    }
                }

                if (bIsRing) break;

                // If we have ever seen a cluster, then bowtie found, break vertex.
                for (size_t kk = 0; kk < vertexFaceList.size(); kk++)
                {
                    ISOCHARTFACE& face = m_pFaces[vertexFaceList[kk]];

                    if (face.dwVertexID[0] == ii)
                        face.dwVertexID[0] = dwNewVertID;
                    else if (face.dwVertexID[1] == ii)
                        face.dwVertexID[1] = dwNewVertID;
                    else
                        face.dwVertexID[2] = dwNewVertID;
                }
                if (dwClusterCount > 0)
                {
                    newVertMap.push_back(ii);
                }

                if (dwClusterCount > 0)
                    dwNewVertID++;

                dwClusterCount++;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 2. Split vertices to fix bowtie.	
    assert(m_dwVertNumber+newVertMap.size() == dwNewVertID);

    if (dwNewVertID == m_dwVertNumber)
    {
        bCleaned = false;
        return hr;
    }
    
    auto pNewVertList = new (std::nothrow) ISOCHARTVERTEX[dwNewVertID];
    if (!pNewVertList)
    {
        return E_OUTOFMEMORY;
    }

    ISOCHARTVERTEX* pNewVertex = pNewVertList;
    ISOCHARTVERTEX* pOldVertex = m_pVerts;
    
    for (uint32_t ii = 0; ii<m_dwVertNumber; ii++)
    {
        pNewVertex->dwID = ii;
        pNewVertex->dwIDInRootMesh = pOldVertex->dwIDInRootMesh;
        pNewVertex->dwIDInFatherMesh = pOldVertex->dwID;
        pNewVertex->bIsBoundary = pOldVertex->bIsBoundary;
        pNewVertex->nImportanceOrder = pOldVertex->nImportanceOrder;
        pNewVertex++;
        pOldVertex++;
    }

    for (size_t ii = m_dwVertNumber; ii<dwNewVertID; ii++)
    {
        pOldVertex = m_pVerts + newVertMap[ii-m_dwVertNumber];
        pNewVertex->dwID = static_cast<uint32_t>(ii);
        pNewVertex->dwIDInRootMesh = pOldVertex->dwIDInRootMesh;
        pNewVertex->dwIDInFatherMesh = pOldVertex->dwID;
        pNewVertex->bIsBoundary = pOldVertex->bIsBoundary;
        pNewVertex->nImportanceOrder = pOldVertex->nImportanceOrder;
        pNewVertex++;		
    }
        
    delete []m_pVerts;

    m_pVerts = pNewVertList;
    m_dwVertNumber = dwNewVertID;
    return hr;
}

HRESULT CIsochartMesh::SetEdgeSplitAttribute()
{
    // The the bCanBeSplit item of each edge. If user don't specified the parameter, all edges can 
    // be splitted
    HRESULT hr = S_OK;
    if (!m_baseInfo.pdwSplitHint)
        return hr;

    for (size_t ii=0; ii<m_dwFaceNumber; ii++)
    {
        ISOCHARTFACE &face = m_pFaces[ii];
        const uint32_t* pSplitInfo = m_baseInfo.pdwSplitHint + face.dwIDInRootMesh * 3;

        for (size_t jj=0; jj<3; jj++)
        {
            if (pSplitInfo[jj] != INVALID_FACE_ID)
            {
                if(m_edges[face.dwEdgeID[jj]].bIsBoundary)
                {
                    DPF(0, "UVAtlas Internal error: Non-splittable edge was chosen as a boundary edge");
                    return E_FAIL;
                }
                m_edges[face.dwEdgeID[jj]].bCanBeSplit = false;
            }
        }
    }

    return hr;
}


// Check the index order of each face. In a manifold mesh, each internal edge
// has 2 faces on different sides. If 2 faces on same side of a edge, the chart
// is nonmainfold
bool CIsochartMesh::IsAllFaceVertexOrderValid()
{
    //.Algorithm
    // For each edges, if it isn't a boundary edge, make sure that  the two faces 
    // are sit in different side of it.
    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        ISOCHARTEDGE &edge = m_edges[i];
        if (edge.bIsBoundary)
        {
            continue;
        }

        ISOCHARTFACE* pTri1 = m_pFaces + edge.dwFaceID[0];
        ISOCHARTFACE* pTri2 = m_pFaces + edge.dwFaceID[1];

        // Get the "third vertex" of the face
        size_t j, k;
        for (j=0; j<3; j++)
        {
            if (pTri1->dwVertexID[j] != edge.dwVertexID[0] &&
                pTri1->dwVertexID[j] != edge.dwVertexID[1] )
            {
                break;
            }
        }

        for (k=0; k<3; k++)
        {
            if (pTri2->dwVertexID[k] != edge.dwVertexID[0] &&
                pTri2->dwVertexID[k] != edge.dwVertexID[1] )
            {
                break;
            }
        }

        // Check if the two faces is exactly sit in different side of the edge
        if (pTri1->dwVertexID[(j+1)%3] != pTri2->dwVertexID[(k+2)%3] ||
            pTri1->dwVertexID[(j+2)%3] != pTri2->dwVertexID[(k+1)%3])
        {
            DPF(3,
            "Non-manifold: The mesh is folded. 2 face on the same side of a edge...\n");
            return false;
        }
    }
    return true;
}

//Build Adjacent vertices array of each vertex. sort them in the same order.
//.Algorithm ( for internal vertex A )
//.(1) Fetch the first edge B adjacent A
//.(2) To edge B, fetch the first face C having edge B
//.(3) In face C, the vertex D not belonging to edge B is the next vertex adjacent A
HRESULT CIsochartMesh::SortAdjacentVertices(
    bool& bIsManifold)
{
    bIsManifold = false;

    try
    {
        ISOCHARTVERTEX* pVertex = m_pVerts;
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            uint32_t dwEdgeNum = static_cast<uint32_t>(pVertex->edgeAdjacent.size());
            uint32_t dwFaceNum = static_cast<uint32_t>(pVertex->faceAdjacent.size());

            if (0 == dwEdgeNum) //Isolated vertex
            {
                pVertex++;
                continue;
            }

            pVertex->vertAdjacent.reserve(dwEdgeNum);

            if (dwEdgeNum == dwFaceNum)// internal vertex
            {
                bIsManifold = 
                    SortAdjacentVerticesOfInternalVertex(pVertex);
                if (!bIsManifold)
                {
                    return S_OK;
                }
            }
            else // boundary vertex
            {
                bIsManifold = 
                    SortAdjacentVerticesOfBoundaryVertex(pVertex);
                if (!bIsManifold)
                {
                    return S_OK;
                }
            }

            // Sort Adjacent edge according in the same order of adjacent vertex
            for (size_t j=0; j<pVertex->vertAdjacent.size(); j++)
            {
                uint32_t dwAdjacentVertID = pVertex->vertAdjacent[j];
                for (size_t k=j; k<pVertex->edgeAdjacent.size(); k++)
                {
                    ISOCHARTEDGE &edge = m_edges[pVertex->edgeAdjacent[k]];
                    if (edge.dwVertexID[0] == dwAdjacentVertID || 
                        edge.dwVertexID[1] == dwAdjacentVertID)
                    {
                        std::swap(pVertex->edgeAdjacent[j],pVertex->edgeAdjacent[k]);
                        break;
                    }
                }
            }

            pVertex++;
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    bIsManifold = true;
    return S_OK;
}

// Sort Adjacent vertices of a boundary vertex
// Algorithm:
// Begin from a adjacent boundary edge scan all adjacent edges
// by the same direction to reach another adjacent boundary edge.
bool
CIsochartMesh::SortAdjacentVerticesOfBoundaryVertex(
    ISOCHARTVERTEX* pVertex)
{
    uint32_t dwEdgeNum = static_cast<uint32_t>(pVertex->edgeAdjacent.size());

    pVertex->bIsBoundary = true;

    uint32_t dwNextV;
    ISOCHARTEDGE* pPreEdge = nullptr;
    ISOCHARTEDGE* pCurrentEdge = nullptr;

    uint32_t dwLastIndex = 0;
    for (uint32_t j = 0; j<dwEdgeNum; j++)
    {
        pCurrentEdge = &(m_edges[pVertex->edgeAdjacent[j]]);
        if (pCurrentEdge->bIsBoundary)
        {
            dwLastIndex = j;
            break;
        }
        pCurrentEdge = nullptr;
    }

    if (!pCurrentEdge)
    {
        DPF(3,"Non-manifold: \
            A boundary vertex doesn't has adjacent boundary edge.\n");
        return false;
    }

    if (pCurrentEdge->dwVertexID[0] == pVertex->dwID)
    {
        dwNextV = pCurrentEdge->dwVertexID[1];
    }
    else
    {
        dwNextV = pCurrentEdge->dwVertexID[0];
    }

    ISOCHARTFACE* pTriangle = m_pFaces + pCurrentEdge->dwFaceID[0];

    size_t k;
    for (k=0; k<3; k++)
    {
        if (pTriangle->dwVertexID[k] == pVertex->dwID)
        { 
            break;
        }
    }

    // We need to order the adjacent vertexes in the same order of face vertex.
    // For D3D, clockwise, for OpenGL anticlockwise
    if (pTriangle->dwVertexID[(k+1)%3] != dwNextV)
    {
        pCurrentEdge = nullptr;
        for (size_t j= dwLastIndex+1; j<dwEdgeNum; j++)
        {
            pCurrentEdge = &(m_edges[pVertex->edgeAdjacent[j]]);
            if (pCurrentEdge->bIsBoundary)
            {
                break;
            }
            else
            {
                pCurrentEdge = nullptr;
            }
        }
        if (!pCurrentEdge)
        {
            DPF(3,"Non-manifold: A boundary vertex only has one adjacent boundary edge.\n");
            return false;
        }
        if (pCurrentEdge->dwVertexID[0] == pVertex->dwID)
        {
            dwNextV = pCurrentEdge->dwVertexID[1];
        }
        else
        {
            dwNextV = pCurrentEdge->dwVertexID[0];
        }

        pTriangle = m_pFaces + pCurrentEdge->dwFaceID[0];
        size_t m;
        for(m=0; m<3; m++)
        {
            if (pTriangle->dwVertexID[m] == pVertex->dwID)
            {
                break;
            }
        }

        if (m>=3 || pTriangle->dwVertexID[(m+1)%3] != dwNextV)
        {
            DPF(3,"Non-manifold: logic error, Need to be investigated...\n");
            return false;
        }
    }

    try
    {
        for (size_t j = 0; j < dwEdgeNum; j++)
        {
            if (pCurrentEdge == pPreEdge)
            {
                DPF(3, "Non-manifold: Vertex has more than 2 adjacent boundary edges. \n");
                return false;
            }

            pVertex->vertAdjacent.push_back(dwNextV);

            if (pPreEdge)
            {
                if (pCurrentEdge->bIsBoundary)
                {
                    pPreEdge = pCurrentEdge;
                    continue;
                }

                if (pCurrentEdge->dwOppositVertID[0] == pPreEdge->dwVertexID[0]
                    || pCurrentEdge->dwOppositVertID[0] == pPreEdge->dwVertexID[1])
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[1];
                }
                else
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[0];
                }
            }
            else
            {
                dwNextV = pCurrentEdge->dwOppositVertID[0];
            }

            pPreEdge = pCurrentEdge;
            pCurrentEdge = nullptr;

            for (size_t m = 0; m < dwEdgeNum; m++)
            {
                pCurrentEdge = &(m_edges[pVertex->edgeAdjacent[m]]);
                if (pCurrentEdge->dwVertexID[0] == dwNextV
                    || pCurrentEdge->dwVertexID[1] == dwNextV)
                {
                    break;
                }
                pCurrentEdge = nullptr;
            }

            if (!pCurrentEdge && j + 1 < dwEdgeNum)
            {
                DPF(3, "Non-manifold: logic error, Need to be investigated...\n");
                return false;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return false;
    }
    assert( pVertex->vertAdjacent.size() == dwEdgeNum);
    return true;
}

// Sort Adjacent vertices of a internal vertex
// Algorithm:
//  From one of adjacent edge, scan all edges in the same direction
bool
CIsochartMesh::SortAdjacentVerticesOfInternalVertex(
    ISOCHARTVERTEX* pVertex)
{
    uint32_t dwEdgeNum = static_cast<uint32_t>(pVertex->edgeAdjacent.size());

    pVertex->bIsBoundary = false;

    uint32_t dwNextV;

    ISOCHARTEDGE* pPreEdge = nullptr;
    ISOCHARTEDGE* pCurrentEdge = &(m_edges[pVertex->edgeAdjacent[0]]);

    // 1. Find the first adjacent vertex
    if (pCurrentEdge->dwVertexID[0] == pVertex->dwID)
    {
        dwNextV = pCurrentEdge->dwVertexID[1];
    }
    else
    {
        dwNextV = pCurrentEdge->dwVertexID[0];
    }

    try
    {
        pVertex->vertAdjacent.push_back(dwNextV);

        for (size_t j = 1; j < dwEdgeNum; j++)
        {
            if (pPreEdge)
            {
                if (pCurrentEdge->dwOppositVertID[0] == pPreEdge->dwVertexID[0]
                    || pCurrentEdge->dwOppositVertID[0] == pPreEdge->dwVertexID[1])
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[1];
                }
                else
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[0];
                }
            }
            else
            {
                ISOCHARTFACE* pTriangle = m_pFaces + pCurrentEdge->dwFaceID[0];
                size_t k;
                for (k = 0; k < 3; k++)
                {
                    if (pTriangle->dwVertexID[k] == pVertex->dwID)
                    {
                        break;
                    }
                }

                // This step assure that to all vertexes, their adjacent vertexes
                // ordered in the same round direction!
                if (pTriangle->dwVertexID[(k + 1) % 3] == dwNextV)
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[0];
                }
                else
                {
                    dwNextV = pCurrentEdge->dwOppositVertID[1];
                }
            }

            size_t k;
            for (k = 0; k < j; k++)
            {
                if (pVertex->vertAdjacent[k] == dwNextV)
                {
                    break;
                }
            }
            if (k < j)
            {
                DPF(3, "Non-manifold: Vertex has two same adjacent vertices.\n");
                return false;
            }

            pPreEdge = pCurrentEdge;
            pCurrentEdge = nullptr;

            for (k = 0; k < dwEdgeNum; k++)
            {
                ISOCHARTEDGE* pEdge = &(m_edges[pVertex->edgeAdjacent[k]]);
                if (pEdge->dwVertexID[0] == dwNextV
                    || pEdge->dwVertexID[1] == dwNextV)
                {
                    pCurrentEdge = pEdge;
                    break;
                }
            }

            if (!pCurrentEdge)
            {
                DPF(3, "Non-manifold: logic error, can not find a right edge.\n");
                return false;
            }

            pVertex->vertAdjacent.push_back(dwNextV);
        }
    }
    catch (std::bad_alloc&)
    {
        return false;
    }

    assert( pVertex->vertAdjacent.size() == dwEdgeNum);
    return true;
}

// Compute adjacent faces for each face. This algorithm can only process manifold meshes.
// Algorithm:
// For each face in mesh, get adjacent faces using the edge adjacent faces information 
// gotten by BuildFullConnection.
void CIsochartMesh::GetFaceAdjacentArray(
    uint32_t* pdwFaceAdjacentArray) const
{
    assert(pdwFaceAdjacentArray != 0);

    uint32_t *pFaceAjacence = pdwFaceAdjacentArray;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {		
        for (size_t j=0; j<3; j++)
        {
            const ISOCHARTEDGE &edge = m_edges[m_pFaces[i].dwEdgeID[j]];
            if (edge.bIsBoundary)
            {
                 pFaceAjacence[j] = INVALID_FACE_ID;
            }
            else
            {
                if (edge.dwFaceID[0] == i)
                {
                    pFaceAjacence[j] = edge.dwFaceID[1];
                }
                else
                {
                    pFaceAjacence[j] = edge.dwFaceID[0];
                }
            }
        }
        pFaceAjacence += 3;
    }
}

/////////////////////////////////////////////////////////////
///////////////////Simplify Chart Methods////////////////////
/////////////////////////////////////////////////////////////

//Check if the original mesh have independant sub-meshes.
// if true creat new meshes
// Algorithm:
// Keep a vertex queue Q and vertex array.A
// (1) Push an unprocessed vertex into Q.Clear A.
// (2) Try to pop out a vertex V from Q.If the Q is empty, export
//     all vertices in A as an new chart and goto (1)
// (3) Push all adjacent vertices of V into Q. goto (2)

HRESULT CIsochartMesh::CheckAndDivideMultipleObjects(
    bool& bHasMultiObjects)
{

    assert(m_dwVertNumber != 0 || m_dwFaceNumber != 0);
    
    bHasMultiObjects = false;
    
    std::unique_ptr<bool[]> vertMark( new (std::nothrow) bool[m_dwVertNumber] );
    if (!vertMark)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbVertMark = vertMark.get();

    memset(pbVertMark, 0, m_dwVertNumber*sizeof(bool));

    try
    {
        for (size_t i=0; i < m_dwVertNumber; i++)
        {
            if (pbVertMark[i])
            {
                continue;
            }
            pbVertMark[i] = true;

            ISOCHARTVERTEX* pVertex = m_pVerts + i;
            if (pVertex->vertAdjacent.size() == 0)
            {
                continue;
            }

            VERTEX_ARRAY vertList;
            uint32_t dwHead, dwEnd;

            vertList.push_back(pVertex);

            dwHead = 0;
            dwEnd = 1;
            while (dwHead < dwEnd)
            {
                HRESULT hr = m_callbackSchemer.CheckPointAdapt();
                if ( FAILED(hr) )
                    return hr;

                pVertex = vertList[dwHead];
                for (size_t j = 0; j < pVertex->vertAdjacent.size(); j++)
                {
                    uint32_t dwTempIndex = pVertex->vertAdjacent[j];
                    if (!pbVertMark[dwTempIndex])
                    {
                        pbVertMark[dwTempIndex] = true;
                        vertList.push_back(m_pVerts + dwTempIndex);
                    }
                }
                dwHead++;
                dwEnd = static_cast<uint32_t>(vertList.size());
            }
            // If all vertices can be connected together, only one object in cuurent chart
            if (vertList.size() == m_dwVertNumber)
            {
                bHasMultiObjects = false;
                return S_OK;
            }
            // Must have mulitple object, export the new object as an chart
            else if (vertList.size() > 0)
            {
                bHasMultiObjects = true;
                CIsochartMesh* pChart = nullptr;
                //Creat new chart
                HRESULT hr = ExtractIndependentObject(vertList, &pChart);
                if (FAILED(hr))
                {
                    return hr;
                }

                m_children.push_back(pChart);

                DPF(3,
                    "Generate new mesh: %Iu vert, %Iu face, %Iu edge\n",
                    pChart->m_dwVertNumber, pChart->m_dwFaceNumber, pChart->m_dwEdgeNumber);
            }
        }
        DPF(3, "....Divide into %Iu sub-meshes...\n", m_children.size());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

// Use vertex list to creat new chart and build full connection for new chart
HRESULT CIsochartMesh::ExtractIndependentObject(
    VERTEX_ARRAY& vertList,
    CIsochartMesh** ppChart) const
{
    assert(ppChart != 0);
    if (vertList.empty())
    {
        return S_OK;
    }

    std::vector<uint32_t> faceList;

    std::unique_ptr<bool[]> pbFaceMark( new (std::nothrow) bool[m_dwFaceNumber] );
    if (!pbFaceMark)
    {
        return E_OUTOFMEMORY;
    }

    memset(pbFaceMark.get(), 0, sizeof(bool)*m_dwFaceNumber);

    // 1. Find all faces in new chart
    try
    {
        ISOCHARTVERTEX* pOldVertex = nullptr;
        for (size_t i=0; i < vertList.size(); i++)
        {
            pOldVertex = vertList[i];
            for (size_t j = 0; j < pOldVertex->faceAdjacent.size(); j++)
            {
                uint32_t dwFaceIndex = pOldVertex->faceAdjacent[j];
                if (!pbFaceMark[dwFaceIndex])
                {
                    pbFaceMark[dwFaceIndex] = true;
                    faceList.push_back(dwFaceIndex);
                }
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 2. Create new chart by using the vertex and face list
    auto pChart = CreateNewChart(vertList, faceList, m_bIsSubChart);
    if (!pChart)
    {
        return E_OUTOFMEMORY;
    }

    // 3. Build full connection for new chart
    bool bManifold = false;
    HRESULT hr = pChart->BuildFullConnection(bManifold);
    if (FAILED(hr))
    {
        delete pChart;
        return hr;
    }
    if (!bManifold)
    {
        delete pChart;
        return HRESULT_FROM_WIN32( ERROR_INVALID_DATA );
    }

    if (m_baseInfo.pfFaceAreaArray)
    {
        pChart->m_fChart3DArea = pChart->CalculateChart3DArea();
        pChart->m_fBaseL2Stretch= pChart->CalCharBaseL2SquaredStretch();
    }

    *ppChart = pChart;

    return S_OK;
}


// If the chart  has 2 or more boundaries, cut the chart along edge paths
// to connect these boundaies. Each call for this function can decrease one 
// boundary.
HRESULT CIsochartMesh::CheckAndCutMultipleBoundaries(
    size_t &dwBoundaryNumber)
{
    assert(m_dwVertNumber != 0);
    DPF(3,"Check and cut multi boundary...\n");

    dwBoundaryNumber = 0;

    // 1. Calculate chart edge length
    CalculateChartEdgeLength();

    // 2. Find all bondary edges

    // allBoundaryList will store all boundary vertex, the vertices belong to
    // same boundary store beside each other.

    // boundaryRecord will store the start position and end position of each
    // boundary group in allBoundaryList.

    // For example:
    // if vertice 1, 3, 5, 7, 9, 11, 13, 15  are boundary vertex.
    // 1,5,13 belong to boundary group A
    // 3,7,15 belong to boundary group B
    // 9,11 belong to boundary group C
    // Then allBoundaryList will be 1,5,13,3,7,15,9,11
    // and boundaryRecord will be 0, 3, 6, 8

    VERTEX_ARRAY allBoundaryList;
    std::vector<uint32_t> boundaryRecord;

    std::unique_ptr<uint32_t []> pdwVertBoundaryID(new (std::nothrow) uint32_t[m_dwVertNumber]);
    if (!pdwVertBoundaryID)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = FindAllBoundaries(
        dwBoundaryNumber,
        allBoundaryList,
        boundaryRecord,
        pdwVertBoundaryID.get());
    if (FAILED(hr))
    {
        return hr;
    }
    
    // 2. if current chart has multiple boundaries, cut current chart.
    if ( dwBoundaryNumber >= 2) 
    {
        hr = DecreaseBoundary(
            dwBoundaryNumber,
            allBoundaryList,
            boundaryRecord,
            pdwVertBoundaryID.get());
    }
    
    return hr;
}


// Clustering the boundaies edges in chart
// Algorithm:
// Scan each boundary vertex, if one has not been put into an existent boundary set,
// create a new boundary set and put this vertex and all the bounday vertices which 
// have connection path to this vertex into the new boundary set.
HRESULT CIsochartMesh::FindAllBoundaries(
    size_t &dwBoundaryNumber,
    VERTEX_ARRAY& allBoundaryList,
    std::vector<uint32_t>& boundaryRecord,
    uint32_t *pdwVertBoundaryID)
{
    assert(pdwVertBoundaryID != 0);

    HRESULT hr = S_OK;

    // each vertex has a boundary ID, 0 means it has not been assigned a boundary ID
    memset(pdwVertBoundaryID, 0, sizeof(uint32_t) * m_dwVertNumber);
    uint32_t dwBoundaryID = 0;
    try
    {
        boundaryRecord.push_back(0);

        uint32_t dwVertIndex = 0;
        while (dwVertIndex < m_dwVertNumber)
        {
            // 1.find boundary vertex which has not be assigned a boundary ID
            while (dwVertIndex < m_dwVertNumber)
            {
                if (m_pVerts[dwVertIndex].bIsBoundary
                    && 0 == pdwVertBoundaryID[dwVertIndex])
                {
                    break;
                }
                dwVertIndex++;
            }
            // 2 if all vertices have been scaned, all boundaries have been found.
            if (dwVertIndex >= m_dwVertNumber)
            {
                break;
            }
            // 3 assign a new boundary ID to current vertex, from current vertex,
            //   scan all other boundary vertices which belong to the same boundary.
            dwBoundaryID++;
            pdwVertBoundaryID[dwVertIndex] = dwBoundaryID;

            allBoundaryList.push_back(m_pVerts + dwVertIndex);

            uint32_t dwEnd = static_cast<uint32_t>(allBoundaryList.size());
            assert(dwEnd > 0);
            uint32_t dwHead = dwEnd - 1;

            while (dwHead < dwEnd)
            {
                hr = m_callbackSchemer.CheckPointAdapt();
                if ( FAILED(hr) )
                    return hr;

                ISOCHARTVERTEX* pCurrentVertex = allBoundaryList[dwHead];
                auto& adjacentVertList = pCurrentVertex->vertAdjacent;

                // When building full connection, adjacent boundary vertex always
                // been put at head or end of adjacence list. So, need only check
                // these 2 positions.

                uint32_t dwIndex = adjacentVertList[0];
                assert(m_pVerts[dwIndex].dwID == dwIndex);
                if (m_pVerts[dwIndex].bIsBoundary && 0 == pdwVertBoundaryID[dwIndex])
                {
                    pdwVertBoundaryID[dwIndex] = dwBoundaryID;
                    allBoundaryList.push_back(m_pVerts + dwIndex);
                }

                dwIndex = adjacentVertList[adjacentVertList.size() - 1];
                assert(m_pVerts[dwIndex].dwID == dwIndex);
                if (m_pVerts[dwIndex].bIsBoundary && 0 == pdwVertBoundaryID[dwIndex])
                {
                    pdwVertBoundaryID[dwIndex] = dwBoundaryID;
                    allBoundaryList.push_back(m_pVerts + dwIndex);
                }
                dwHead++;
                dwEnd = static_cast<uint32_t>(allBoundaryList.size());
            }

            // 4. record the end position of each boundary in allBoundaryList.
            boundaryRecord.push_back(dwEnd);
        }
    }
    catch (std::bad_alloc&)
    {
        return  E_OUTOFMEMORY;
    }

    dwBoundaryNumber = dwBoundaryID;
    assert(boundaryRecord.size() == dwBoundaryNumber+1);

    return hr;
}

HRESULT CIsochartMesh::CalMinPathToOtherBoundary(
    VERTEX_ARRAY& allBoundaryList,
    uint32_t dwStartIdx,
    uint32_t dwEndIdx,
    uint32_t* pdwVertBoundaryID,
    uint32_t& dwPeerVertID,
    float& fDistance)
{
    std::unique_ptr<bool[]> vertProcessed( new (std::nothrow) bool[m_dwVertNumber] );
    std::unique_ptr<CMaxHeapItem<float, uint32_t> []> heapItem(new (std::nothrow) CMaxHeapItem<float, uint32_t>[m_dwVertNumber]);
    if (!vertProcessed || !heapItem)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbVertProcessed = vertProcessed.get();
    memset(pbVertProcessed, 0, sizeof(bool) * m_dwVertNumber);
    
    auto pHeapItem = heapItem.get();

    CMaxHeap<float, uint32_t> heap;
    if (!heap.resize(m_dwVertNumber))
    {
        return E_OUTOFMEMORY;
    }

    // 1. Init the distance to souce of each vertice
    ISOCHARTVERTEX* pCurrentVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pCurrentVertex->fGeodesicDistance = FLT_MAX;
        pCurrentVertex->dwNextVertIDOnPath = INVALID_VERT_ID;
        pCurrentVertex++;
    }

    // 2. Init the source vertice	
    for (size_t i=dwStartIdx; i<dwEndIdx; i++)
    {
        pCurrentVertex = allBoundaryList[i];
        pbVertProcessed[pCurrentVertex->dwID] = true;
        pCurrentVertex->fGeodesicDistance = 0;
        
        pHeapItem[pCurrentVertex->dwID].m_weight =
            -pCurrentVertex->fGeodesicDistance;
        pHeapItem[pCurrentVertex->dwID].m_data = 
            pCurrentVertex->dwID;
        
        if (!heap.insert(pHeapItem+pCurrentVertex->dwID))
        {
            return E_OUTOFMEMORY;
        }		
    }
    
    // 3.  iteration of computing distance, from the one ring neighorhood to the outside 
    uint32_t dwCurrentBoundaryID =
        pdwVertBoundaryID[allBoundaryList[dwStartIdx]->dwID];

     for (size_t i=0; i<m_dwVertNumber; i++)
     {
         CMaxHeapItem<float, uint32_t>* pTop = heap.cutTop();
        if (!pTop)
        {
            break;
        }

        // 3.1 Get vertices having min-distance to source
        pCurrentVertex = m_pVerts+pTop->m_data;
        assert(pCurrentVertex->dwID == pTop->m_data);
        pbVertProcessed[pCurrentVertex->dwID] = true;

        if (pCurrentVertex->bIsBoundary &&
            pdwVertBoundaryID[pCurrentVertex->dwID] !=
            dwCurrentBoundaryID)
        {
            dwPeerVertID = pCurrentVertex->dwID;
            fDistance = pCurrentVertex->fGeodesicDistance;
            assert( pCurrentVertex->dwNextVertIDOnPath != INVALID_VERT_ID);
            return S_OK;
        }


        // 3.2 Computing the distance of the vertices adjacent to current vertices
        for (size_t j=0; j<pCurrentVertex->edgeAdjacent.size(); j++)
        {
            uint32_t dwAdjacentVertID;

            const ISOCHARTEDGE& edge = m_edges[pCurrentVertex->edgeAdjacent[j]];

            if (m_baseInfo.pdwSplitHint && !edge.bCanBeSplit)
            {
                continue;	
            }
            
            if (edge.dwVertexID[0] == pCurrentVertex->dwID)
            {
                dwAdjacentVertID = edge.dwVertexID[1];
            }
            else
            {
                dwAdjacentVertID = edge.dwVertexID[0];
            }

            if (pbVertProcessed[dwAdjacentVertID])
            {
                continue;
            }

            ISOCHARTVERTEX* pAdjacentVertex = m_pVerts + dwAdjacentVertID;
            if (pAdjacentVertex->fGeodesicDistance 
                > pCurrentVertex->fGeodesicDistance+edge.fLength)
            {
                pAdjacentVertex->fGeodesicDistance 
                    = pCurrentVertex->fGeodesicDistance+edge.fLength;
                pAdjacentVertex->dwNextVertIDOnPath = pCurrentVertex->dwID;
            }
        }

        // 3.3 prepare for next iteration
        for (size_t j=0; j<pCurrentVertex->vertAdjacent.size(); j++)
        {
            uint32_t dwAdjacentVertID = pCurrentVertex->vertAdjacent[j];
            if (pbVertProcessed[dwAdjacentVertID])
            {
                continue;
            }

            // Make sure adjacent edge & vertex sort in the same order.
            if (m_baseInfo.pdwSplitHint &&
                !m_edges[pCurrentVertex->edgeAdjacent[j]].bCanBeSplit)
            {
                continue;
            }

            ISOCHARTVERTEX* pAdjacentVertex = m_pVerts + dwAdjacentVertID;
            if (pHeapItem[dwAdjacentVertID].isItemInHeap())
            {
                heap.update(pHeapItem+dwAdjacentVertID, 
                    -pAdjacentVertex->fGeodesicDistance);
            }
            else
            {
                pHeapItem[dwAdjacentVertID].m_data = dwAdjacentVertID;
                pHeapItem[dwAdjacentVertID].m_weight 
                    = -pAdjacentVertex->fGeodesicDistance;
                if (!heap.insert(pHeapItem+dwAdjacentVertID))
                {
                    return E_OUTOFMEMORY;
                }
            }
        }
     }
        
    return S_OK;

}

#pragma warning(push)
#pragma warning( disable : 4706 )

HRESULT CIsochartMesh::RetreiveVertDijkstraPathToSource(
    uint32_t dwVertexID,
    std::vector<uint32_t>& dijkstraPath)
{
    HRESULT hr = S_OK;

    assert( dwVertexID < m_dwVertNumber );
    dijkstraPath.clear();
    ISOCHARTVERTEX* p = m_pVerts + dwVertexID;

    try
    {
        do
        {
            dijkstraPath.push_back(p->dwID);
        } while ((p->dwNextVertIDOnPath != INVALID_VERT_ID) && (p = m_pVerts + p->dwNextVertIDOnPath));
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    size_t ii = 0, jj = dijkstraPath.size() - 1;
    while(ii < jj)
    {
        std::swap(dijkstraPath[ii],dijkstraPath[jj]);
        ii++;
        jj--;
    }
    return hr;
}

#pragma warning(pop)

HRESULT CIsochartMesh::CalMinPathBetweenBoundaries(
    VERTEX_ARRAY& allBoundaryList,
    std::vector<uint32_t>& boundaryRecord,
    uint32_t* pdwVertBoundaryID,
    std::vector<uint32_t>& minDijkstraPath)
{
    HRESULT hr = S_OK;
    float fMinDistance = FLT_MAX;

    for (size_t i=0; i<boundaryRecord.size()-1; i++)
    {
        float fDistance = 0;
        uint32_t dwVertIdx = INVALID_VERT_ID;
        FAILURE_RETURN(
            CalMinPathToOtherBoundary(
                allBoundaryList,
                boundaryRecord[i],
                boundaryRecord[i+1],
                pdwVertBoundaryID,
                dwVertIdx,
                fDistance));
        if (fDistance < fMinDistance)
        {
            fMinDistance = fDistance;
            FAILURE_RETURN(
                RetreiveVertDijkstraPathToSource(dwVertIdx, minDijkstraPath));
        }
    }
    return hr;
}

//Cut along an edge path to merge 2 boundary of chart
HRESULT CIsochartMesh::DecreaseBoundary(
    size_t& dwBoundaryNumber,
    VERTEX_ARRAY& allBoundaryList,
    std::vector<uint32_t>& boundaryRecord,
    uint32_t *pdwVertBoundaryID)
{
    assert(pdwVertBoundaryID != 0);
    assert(!allBoundaryList.empty());
    assert(!boundaryRecord.empty());

    if (dwBoundaryNumber <= 1)
    {
        return S_OK;
    }

    HRESULT hr = S_OK;
    DPF(3,"....Has %Iu boundies...\n", dwBoundaryNumber);

    std::vector<uint32_t> minDijkstraPath;
    FAILURE_RETURN(
    CalMinPathBetweenBoundaries(
        allBoundaryList,
        boundaryRecord,
        pdwVertBoundaryID,
        minDijkstraPath));

    // 4. Cut current chart along the dijkstra path gotten by 3
    FAILURE_RETURN(
    CutChartAlongPath(minDijkstraPath));

    dwBoundaryNumber -= 1;
    return hr;
}

HRESULT 
CIsochartMesh::CalVertWithMinDijkstraDistanceToSrc(
    uint32_t dwSourceVertID,
    uint32_t& dwPeerVertID,
    uint32_t *pdwVertBoundaryID)
{
    assert(pdwVertBoundaryID != 0);

    HRESULT hr = S_OK;
    if (FAILED(hr = CalculateDijkstraPathToVertex(dwSourceVertID)))
    {
        return hr;
    }

    float fMinDistance = FLT_MAX;
    dwPeerVertID = INVALID_VERT_ID;

    for (uint32_t i=0; i<m_dwVertNumber; i++)
    {
        if (m_pVerts[i].bIsBoundary &&
            pdwVertBoundaryID[i] != pdwVertBoundaryID[dwSourceVertID])
        {
            if (m_pVerts[i].fGeodesicDistance < fMinDistance)
            {
                fMinDistance = m_pVerts[i].fGeodesicDistance;
                dwPeerVertID = i;
            }
        }
    }
    assert(dwPeerVertID != INVALID_VERT_ID);

    return hr;
}

// Cut current chart along a path presented by vertex list.
HRESULT CIsochartMesh::CutChartAlongPath(std::vector<uint32_t>& dijkstraPath)
{
    HRESULT hr = S_OK;
    std::vector<uint32_t> splitPath;

    // 1. Find the vertices need to be splited on the dijkstraPath
    if (FAILED(hr = FindSplitPath(dijkstraPath, splitPath)))
    {
        return hr;
    }

    
    assert(splitPath.size() >= 2);

    // 2. Find the faces affected by vertex split and the correspond
    // vertex to be splited
    std::vector<uint32_t> changeFaceList;
    std::vector<uint32_t> corresVertList;
    if (FAILED(hr = FindFacesAffectedBySplit(
                splitPath,
                changeFaceList,
                corresVertList)))
    {
        return hr;
    }

    // 3.Split each vertex on splitpath to get a new chart with less
    // boundaries
    auto pChart = SplitVertices( splitPath, changeFaceList, corresVertList);
    if (!pChart)
    {
        return E_OUTOFMEMORY;
    }

    // 4. Build full connectiong
    bool bManifold = false;
    if (SUCCEEDED(hr = pChart->BuildFullConnection(bManifold)))
    {
        if (!bManifold)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            goto LEnd;
        }
        
        try
        {
            m_children.push_back(pChart);
        }
        catch (std::bad_alloc&)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }
    }

LEnd:
    if (FAILED(hr))
    {
        delete pChart;
    }
    return hr;
}


// Scan the dijkstraPath to find the vertices that need to 
// split. (Remove all boundary edges from dijkstraPath, only
// reserve internal edges.
HRESULT CIsochartMesh::FindSplitPath(
    const std::vector<uint32_t>& dijkstraPath,
    std::vector<uint32_t>& splitPath)
{
    assert(!dijkstraPath.empty());

    HRESULT hr = S_OK;
    uint32_t dwStartCutID = 0;

    // Find the first vertex need to be splited
    while(dwStartCutID < dijkstraPath.size()-1
        && m_pVerts[dijkstraPath[dwStartCutID]].bIsBoundary)
    {
        dwStartCutID++;
    }

    assert(dwStartCutID > 0);
    dwStartCutID--;

    // Scan to find all other vertices need to be splited
    try
    {
        for (size_t i = dwStartCutID; i < dijkstraPath.size(); i++)
        {
            assert(dijkstraPath[i] != INVALID_VERT_ID);

            splitPath.push_back(dijkstraPath[i]);

            ISOCHARTVERTEX* pVertex = m_pVerts + dijkstraPath[i];
            if (i != dwStartCutID && pVertex->bIsBoundary)
            {
                break;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return hr;
}

// if one of a face's vertex need be splited, the face's vertex index
// may change. This function finds all suffered faces, and correspond
// vertex.
HRESULT CIsochartMesh::FindFacesAffectedBySplit(
    const std::vector<uint32_t>& splitPath,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    HRESULT hr = S_OK;

    hr = CalSplitInfoOfFirstSplitVert(
        splitPath,
        changeFaceList,
        corresVertList);

    hr = CalSplitInfoOfMiddleSplitVerts(
        splitPath,
        changeFaceList,
        corresVertList);

    hr = CalSplitInfoOfLastSplitVert(
        splitPath,
        changeFaceList,
        corresVertList);

    return hr;
}

HRESULT CIsochartMesh::
CalSplitInfoOfFirstSplitVert(
    const std::vector<uint32_t>& splitPath,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    assert(m_pVerts[splitPath[0]].bIsBoundary);
    assert(splitPath.size()>1);

    std::vector<uint32_t> vertListOnOneSide;

    ISOCHARTVERTEX* pCurrVertex = m_pVerts + splitPath[0];
    ISOCHARTVERTEX* pNextVertex = m_pVerts + splitPath[1];

    size_t ringSize = pCurrVertex->vertAdjacent.size();
        
    try
    {
        for (size_t i = 0; i < ringSize; i++)
        {
            if (pCurrVertex->vertAdjacent[i] == pNextVertex->dwID)
            {
                break;
            }
            vertListOnOneSide.push_back(pCurrVertex->vertAdjacent[i]);
        }
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = AddToChangedFaceList(
        pCurrVertex,
        vertListOnOneSide,
        changeFaceList,
        corresVertList);

    return hr;
}

HRESULT CIsochartMesh::
CalSplitInfoOfMiddleSplitVerts(
    const std::vector<uint32_t>& splitPath,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    assert(splitPath.size()>1);

    HRESULT hr = S_OK;
    std::vector<uint32_t> vertListOnOneSide;
    uint32_t dwPathLength = static_cast<uint32_t>(splitPath.size());

    try
    {
        for (size_t i = 1; i < dwPathLength - 1; i++)
        {
            ISOCHARTVERTEX* pCurrVertex = m_pVerts + splitPath[i];
            ISOCHARTVERTEX* pPrevVertex = m_pVerts + splitPath[i - 1];
            ISOCHARTVERTEX* pNextVertex = m_pVerts + splitPath[i + 1];

            uint32_t dwPrevIndex = INVALID_INDEX;
            uint32_t dwNextIndex = INVALID_INDEX;

            size_t dwRingSize = pCurrVertex->vertAdjacent.size();

            vertListOnOneSide.clear();
            for (uint32_t j = 0; j < dwRingSize; j++)
            {
                if (pCurrVertex->vertAdjacent[j] == pPrevVertex->dwID)
                {
                    dwPrevIndex = j;
                    break;
                }
            }
            for (uint32_t k = 1; k < dwRingSize; k++)
            {
                dwNextIndex = (dwPrevIndex + k) % dwRingSize;

                if (pCurrVertex->vertAdjacent[dwNextIndex] == pNextVertex->dwID)
                {
                    break;
                }
                vertListOnOneSide.push_back(pCurrVertex->vertAdjacent[dwNextIndex]);
            }

            //                 /|prev
            //                / |
            //        current \ |
            //                 \|next
            if (vertListOnOneSide.empty()) //k=1
            {
                for (size_t j = 0; j < pCurrVertex->faceAdjacent.size(); j++)
                {
                    ISOCHARTFACE* pFace = m_pFaces + pCurrVertex->faceAdjacent[j];
                    if ((pFace->dwVertexID[0] == pPrevVertex->dwID ||
                        pFace->dwVertexID[1] == pPrevVertex->dwID ||
                        pFace->dwVertexID[2] == pPrevVertex->dwID) &&
                        (pFace->dwVertexID[0] == pNextVertex->dwID ||
                        pFace->dwVertexID[1] == pNextVertex->dwID ||
                        pFace->dwVertexID[2] == pNextVertex->dwID))
                    {
                        assert(pFace->dwID == pCurrVertex->faceAdjacent[j]);
                        changeFaceList.push_back(pFace->dwID);
                        corresVertList.push_back(pCurrVertex->dwID);
                        break;
                    }
                }
            }

            FAILURE_RETURN(
                AddToChangedFaceList(
                pCurrVertex,
                vertListOnOneSide,
                changeFaceList,
                corresVertList));
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT 
CIsochartMesh::CalSplitInfoOfLastSplitVert(
    const std::vector<uint32_t>& splitPath,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    assert(splitPath.size()>1);
    assert(m_pVerts[splitPath[splitPath.size()-1]].bIsBoundary);

    std::vector<uint32_t> vertListOnOneSide;
    size_t dwPathLength = splitPath.size();

    ISOCHARTVERTEX* pCurrVertex = m_pVerts + splitPath[dwPathLength-1];
    ISOCHARTVERTEX* pPrevVertex = m_pVerts + splitPath[dwPathLength-2];

    size_t dwRingSize = pCurrVertex->vertAdjacent.size();
    uint32_t dwPrevIndex = INVALID_INDEX;
    for (uint32_t i = 0; i<dwRingSize; i++)
    {
        if (pCurrVertex->vertAdjacent[i] == pPrevVertex->dwID)
        {
            dwPrevIndex = i;
            break;
        }
    }

    assert(dwPrevIndex != INVALID_INDEX);
    try
    {
        for (size_t i = dwPrevIndex + 1; i < dwRingSize; i++)
        {
            vertListOnOneSide.push_back(pCurrVertex->vertAdjacent[i]);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = AddToChangedFaceList(
        pCurrVertex,
        vertListOnOneSide,
        changeFaceList,
        corresVertList);

    return hr;
}

HRESULT CIsochartMesh::AddToChangedFaceList(
    ISOCHARTVERTEX* pCurrVertex,
    std::vector<uint32_t>& vertListOnOneSide,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    try
    {
        for (size_t j = 0; j < vertListOnOneSide.size(); j++)
        {
            ISOCHARTVERTEX* pAdjacentVertex =
                m_pVerts + vertListOnOneSide[j];

            for (size_t k = 0; k < pAdjacentVertex->faceAdjacent.size(); k++)
            {
                ISOCHARTFACE* pFace =
                    m_pFaces + pAdjacentVertex->faceAdjacent[k];

                if (pFace->dwVertexID[0] == pCurrVertex->dwID
                    || pFace->dwVertexID[1] == pCurrVertex->dwID
                    || pFace->dwVertexID[2] == pCurrVertex->dwID)
                {
                    assert(pFace->dwID == pAdjacentVertex->faceAdjacent[k]);
                    changeFaceList.push_back(pFace->dwID);
                    corresVertList.push_back(pCurrVertex->dwID);
                }
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

// Split vertices on splitPath, create new chart
CIsochartMesh* CIsochartMesh::SplitVertices(
    const std::vector<uint32_t>& splitPath,
    std::vector<uint32_t>& changeFaceList,
    std::vector<uint32_t>& corresVertList)
{
    auto pChart = new (std::nothrow) CIsochartMesh(m_baseInfo, m_callbackSchemer, m_IsochartEngine);
    if (!pChart)
    {
        return nullptr;
    }
    
    pChart->m_pFather = this;
    pChart->m_bVertImportanceDone = m_bVertImportanceDone;
    pChart->m_bIsSubChart = m_bIsSubChart;
    pChart->m_fBoxDiagLen = m_fBoxDiagLen;
    pChart->m_dwFaceNumber = m_dwFaceNumber;
    pChart->m_pFaces = m_pFaces; // Face number not change
    m_pFaces = nullptr;

    size_t dwNewVertNumber = m_dwVertNumber;

    for (size_t i = 0; i<splitPath.size(); i++)
    {
        for (size_t j=0; j<changeFaceList.size(); j++)
        {
            if (changeFaceList[j] == INVALID_FACE_ID)
            {
                continue;
            }

            ISOCHARTFACE* pFace = pChart->m_pFaces+changeFaceList[j];
            for (size_t k=0; k<3; k++)
            {
                if (pFace->dwVertexID[k] == splitPath[i]
                    && corresVertList[j] == splitPath[i])
                {
                    pFace->dwVertexID[k] = static_cast<uint32_t>(dwNewVertNumber);
                    changeFaceList[j] = INVALID_FACE_ID;
                }
            }
        }
        dwNewVertNumber++;
    }

    changeFaceList.clear();
    size_t nDupVerts = splitPath.size();
    assert(dwNewVertNumber == m_dwVertNumber + nDupVerts);
    _Analysis_assume_(dwNewVertNumber == m_dwVertNumber + nDupVerts);
    DPF(3, "new vert number is :%Iu\n", dwNewVertNumber);

    // Creat all vertices for new chart.
    pChart->m_dwVertNumber = dwNewVertNumber;
    pChart->m_pVerts = new (std::nothrow) ISOCHARTVERTEX[dwNewVertNumber];
    if (!pChart->m_pVerts)
    {
        delete pChart;
        return nullptr;
    }

    uint32_t i=0;

    for (; i<m_dwVertNumber; i++)
    {
        pChart->m_pVerts[i].dwID = m_pVerts[i].dwID;
        pChart->m_pVerts[i].dwIDInFatherMesh = m_pVerts[i].dwID;
        pChart->m_pVerts[i].dwIDInRootMesh = m_pVerts[i].dwIDInRootMesh;
        pChart->m_pVerts[i].nImportanceOrder = m_pVerts[i].nImportanceOrder;
    }

    for (size_t j = 0; j<nDupVerts && i<dwNewVertNumber; i++, j++)
    {
        auto pCurrVertex = m_pVerts + splitPath[j];
        pChart->m_pVerts[i].dwID = i;

        pChart->m_pVerts[i].dwIDInFatherMesh = pCurrVertex->dwID;

        pChart->m_pVerts[i].dwIDInRootMesh = pCurrVertex->dwIDInRootMesh;

        pChart->m_pVerts[i].nImportanceOrder = pCurrVertex->nImportanceOrder;	
    }

    pChart->m_fChart3DArea = pChart->CalculateChart3DArea();
    pChart->m_fBaseL2Stretch= pChart->CalCharBaseL2SquaredStretch();
    return pChart;
}

// Calculate the geodesic distance from all other vertices to the source vertice,
// using dijkstra algorithm. using heap algorithm to get the min-distance at each
//step.
HRESULT CIsochartMesh::CalculateDijkstraPathToVertex(
    uint32_t dwSourceVertID,
    uint32_t* pdwFarestPeerVertID) const
{
    uint32_t dwFarestPeerVertID = INVALID_VERT_ID;

    std::unique_ptr<bool[]> vertProcessed( new (std::nothrow) bool[m_dwVertNumber] );
    std::unique_ptr<CMaxHeapItem<float, uint32_t> []> heapItem(new (std::nothrow) CMaxHeapItem<float, uint32_t>[m_dwVertNumber]);
    if (!vertProcessed || !heapItem)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbVertProcessed = vertProcessed.get();
    memset(pbVertProcessed, 0, sizeof(bool) * m_dwVertNumber);

    auto pHeapItem = heapItem.get();
    
    CMaxHeap<float, uint32_t> heap;
    if (!heap.resize(m_dwVertNumber))
    {
        return E_OUTOFMEMORY;
    }

    // 1. Init the distance to souce of each vertice
    ISOCHARTVERTEX* pCurrentVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pCurrentVertex->fGeodesicDistance = FLT_MAX;
        pCurrentVertex->dwNextVertIDOnPath = INVALID_VERT_ID;
        pCurrentVertex++;
    }

    // 2. Init the source vertice
    pCurrentVertex = m_pVerts + dwSourceVertID;
    pbVertProcessed[dwSourceVertID] = true;
    pCurrentVertex->fGeodesicDistance = 0;

    pHeapItem[dwSourceVertID].m_weight = -pCurrentVertex->fGeodesicDistance;
    pHeapItem[dwSourceVertID].m_data = dwSourceVertID;
    if (!heap.insert(pHeapItem+dwSourceVertID))
    {
        return E_OUTOFMEMORY;
    }		

    // 3.  iteration of computing distance, from the one ring neighorhood to the outside 

     for (size_t i=0; i<m_dwVertNumber; i++)
     {
         CMaxHeapItem<float, uint32_t>* pTop = heap.cutTop();
        if (!pTop)
        {
            break;
        }

        // 3.1 Get vertices having min-distance to source
        pCurrentVertex = m_pVerts+pTop->m_data;
        assert(pCurrentVertex->dwID == pTop->m_data);
        pbVertProcessed[pCurrentVertex->dwID] = true;
        dwFarestPeerVertID = pCurrentVertex->dwID;

        // 3.2 Computing the distance of the vertices adjacent to current vertices
        for (size_t j=0; j<pCurrentVertex->edgeAdjacent.size(); j++)
        {
            uint32_t dwAdjacentVertID;

            const ISOCHARTEDGE& edge = m_edges[pCurrentVertex->edgeAdjacent[j]];
            if (edge.dwVertexID[0] == pCurrentVertex->dwID)
            {
                dwAdjacentVertID = edge.dwVertexID[1];
            }
            else
            {
                dwAdjacentVertID = edge.dwVertexID[0];
            }

            if (pbVertProcessed[dwAdjacentVertID])
            {
                continue;
            }

            ISOCHARTVERTEX* pAdjacentVertex = m_pVerts + dwAdjacentVertID;
            if (pAdjacentVertex->fGeodesicDistance
                > pCurrentVertex->fGeodesicDistance+edge.fLength)
            {
                pAdjacentVertex->fGeodesicDistance 
                    = pCurrentVertex->fGeodesicDistance+edge.fLength;

                pAdjacentVertex->dwNextVertIDOnPath = pCurrentVertex->dwID;
            }
        }

        // 3.3 prepare for next iteration
        for (size_t j=0; j<pCurrentVertex->vertAdjacent.size(); j++)
        {
            uint32_t dwAdjacentVertID = pCurrentVertex->vertAdjacent[j];
            if (pbVertProcessed[dwAdjacentVertID])
            {
                continue;
            }

            ISOCHARTVERTEX* pAdjacentVertex = m_pVerts + dwAdjacentVertID;
            
            if (pHeapItem[dwAdjacentVertID].isItemInHeap())
            {
                heap.update(pHeapItem+dwAdjacentVertID, 
                    -pAdjacentVertex->fGeodesicDistance);
            }
            else
            {
                pHeapItem[dwAdjacentVertID].m_data = dwAdjacentVertID;
                pHeapItem[dwAdjacentVertID].m_weight 
                    = -pAdjacentVertex->fGeodesicDistance;
                if (!heap.insert(pHeapItem+dwAdjacentVertID))
                {
                    return E_OUTOFMEMORY;
                }
            }
        }
    }

    if (pdwFarestPeerVertID)
    {
        *pdwFarestPeerVertID = dwFarestPeerVertID;
    }

    return S_OK;
}


/////////////////////////////////////////////////////////////
////////////////Calculate Vertex Importance methods//////////
/////////////////////////////////////////////////////////////


// Using Progressive Mesh Algorithm to simplify current chart to get weight of importance
// of each vertex.
// See more detai In  [GH97],  [SSGH01]

HRESULT CIsochartMesh::CalculateVertImportanceOrder()
{	
    
    DPF(3,"Calculate Importance order for each vertex...\n");
    HRESULT hr = S_OK;
    
    if (m_dwFaceNumber == 0)
    {
        return S_OK;
    }

    m_bVertImportanceDone = true;

    if (m_dwVertNumber < MIN_LANDMARK_NUMBER)
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            m_pVerts[i].nImportanceOrder = MUST_RESERVE;
        }

        return S_OK;
    }

    CProgressiveMesh progressiveMesh(m_baseInfo, m_callbackSchemer);

    if (FAILED(hr = progressiveMesh.Initialize(*this)))
    {
        return hr;
    }

    if (FAILED(hr = progressiveMesh.Simplify()))
    {
        return hr;
    }
    for (uint32_t i = 0; i<m_dwVertNumber; i++)
    {
        m_pVerts[i].nImportanceOrder = progressiveMesh.GetVertexImportance(i);
    }
    
    return hr;
}
