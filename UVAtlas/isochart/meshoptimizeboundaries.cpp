//-------------------------------------------------------------------------------------
// UVAtlas - meshoptimizestretch.cpp
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
    Notes:
        Chart  boundaries should satisfy two objectives:
        (1) They should cut through areas of high curvature without
        being too jaggy.
        (2) They should minimize the embedding distorations of the 
        charts they border

        See more detail in [Kun04]
    Reference:
        This file implements algorithms in following papers:

        [Kun04]:Kun Zhou, John Synder, Baining Guo, Heung-Yeung Shum:
        Iso-charts: Stretch-driven Mesh Parameterization using Spectral 
        Analysis, page 3 Eurographics Symposium on Geometry Processing (2004)
*/

#include "pch.h"
#include "isochartmesh.h"

using namespace Isochart;
using namespace DirectX;

namespace
{
    // Define the percent of faces in chart that are need to 
    // re-decide chart ID by graph cut.
    const float FUZYY_REGION_PERCENT = 0.30f;

    // Graph cut optimize consider two factors: stretch and angle
    // OPTIMAL_CUT_STRETCH_WEIGHT indicates stretch factor, then
    // angle factor will be 1-OPTIMAL_CUT_STRETCH_WEIGHT
    const float OPTIMAL_CUT_STRETCH_WEIGHT = 0.35f;
}

/////////////////////////////////////////////////////////////////////
////////////Optimizing Boundary Methods/////////////////////////////
////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////
////////////Optimizing Boundary By Angle/////////////////////
/////////////////////////////////////////////////////////////
// Optimize boundary only according to the first objective:
// See Note in file header.
HRESULT CIsochartMesh::OptimizeBoundaryByAngle(
    uint32_t* pdwFaceChartID,
    size_t dwMaxSubchartCount,
    bool& bIsOptimized)
{
    if (dwMaxSubchartCount < 2 || m_children.size() < 2)
    {
        return S_OK;
    }
    
    // 1. Calculate dihedral angle for each edge using formula in 
    // [Kun04], section 4
    std::unique_ptr<uint32_t []> pdwFaceChartIDBackup(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    std::unique_ptr<bool[]> pbIsFuzzyFatherFace( new (std::nothrow) bool[m_dwFaceNumber] );
    std::unique_ptr<float[]> pfEdgeAngleDistance( new (std::nothrow) float[m_dwEdgeNumber] );
    
    if (!pfEdgeAngleDistance || !pbIsFuzzyFatherFace || !pdwFaceChartIDBackup)
    {
        return E_OUTOFMEMORY;
    }

    memcpy( pdwFaceChartIDBackup.get(),
        pdwFaceChartID,
        sizeof(uint32_t) * m_dwFaceNumber);

    float fAverageAngleDistance = 0;
    if (!CalculateEdgeAngleDistance(
            pfEdgeAngleDistance.get(),
            fAverageAngleDistance))
    {
        return S_OK;
    }

    // 2. Decide fuzzy region used in graph cut.
    HRESULT hr = S_OK;
    memset(pbIsFuzzyFatherFace.get(), 0, sizeof(bool)*m_dwFaceNumber);
    for (uint32_t i = 0; i<m_children.size(); i++)
    {
        CIsochartMesh* pChart = m_children[i];
        hr = pChart->CalculateFuzzyRegion(pbIsFuzzyFatherFace.get());
        if (FAILED(hr))
        {
            return hr;
        }
        for (size_t j = 0; j<pChart->m_dwFaceNumber; j++)
        {
            ISOCHARTFACE* pFace = pChart->m_pFaces + j;
            pdwFaceChartID[pFace->dwIDInFatherMesh] = i;
        }
    }

    // 3. Apply graph cut to optimize boundary.
    hr = ApplyGraphCutByAngle(
            pdwFaceChartID,
            pbIsFuzzyFatherFace.get(),
            pfEdgeAngleDistance.get(),
            fAverageAngleDistance);
    if (FAILED(hr))
    {
        return hr;
    }

    // 4. Make optimize valid.
    hr = ApplyBoundaryOptResult(
        pdwFaceChartID,
        pdwFaceChartIDBackup.get(),
        dwMaxSubchartCount,
        bIsOptimized);
 
    return hr;
}

// Caculate first term in graph cut capacity equation
bool CIsochartMesh::CalculateEdgeAngleDistance(
        float* pfEdgeAngleDistance,
        float& fAverageAngleDistance) const
{
    size_t dwEdgeAngleCount = 0;
    fAverageAngleDistance = 0;
    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        const ISOCHARTEDGE& edge = m_edges[i];
        pfEdgeAngleDistance[i] = 0;
        if (!edge.bIsBoundary)
        {
            ISOCHARTFACE* pFace1 = m_pFaces + edge.dwFaceID[0];
            ISOCHARTFACE* pFace2 = m_pFaces + edge.dwFaceID[1];

            pfEdgeAngleDistance[i] = 
                XMVectorGetX(XMVector3Dot(
                XMLoadFloat3(m_baseInfo.pFaceNormalArray+pFace1->dwIDInRootMesh),
                XMLoadFloat3(m_baseInfo.pFaceNormalArray+pFace2->dwIDInRootMesh)));
            pfEdgeAngleDistance[i] = 1.0f - pfEdgeAngleDistance[i];

            fAverageAngleDistance += pfEdgeAngleDistance[i];
            dwEdgeAngleCount++;
        }
    }

    if (0 == dwEdgeAngleCount)
    {
        return false;
    }

    fAverageAngleDistance /= dwEdgeAngleCount;
    if (IsInZeroRange(fAverageAngleDistance))
    {
        return false;
    }
    return true;
}

HRESULT	CIsochartMesh::CalculateFuzzyRegion(
    bool* pbIsFuzzyFatherFace)
{
    assert(m_bVertImportanceDone);
    assert(m_pFather != 0);

    std::unique_ptr<bool[]> isFuzzyVert( new (std::nothrow) bool[m_dwVertNumber] );
    if (!isFuzzyVert)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbIsFuzzyVert  = isFuzzyVert.get();

    memset(pbIsFuzzyVert, 0, sizeof(bool) * m_dwVertNumber);

    // 1. Find new boundary vertices
    std::vector<uint32_t> canidateVertexList;
    HRESULT hr = FindNewBoundaryVert(canidateVertexList, pbIsFuzzyVert);
    if(FAILED(hr))
    {
        return hr;
    }

    // 2. Getting no vertex in step 1 means no "fuzzy region"
    if (canidateVertexList.empty())
    {
        for (size_t i=0; i<m_dwFaceNumber; i++)
        {
            pbIsFuzzyFatherFace[m_pFaces[i].dwIDInFatherMesh] = false;
        }
        return S_OK;
    }

    // 3. Start from vertices find in step1 to find other "fuzzy vertex".
    //  The order to find these verices decide their fuzzy level.
    std::vector<uint32_t> levelVertCountList;
    if(FAILED(hr = SpreadFuzzyVert(
        canidateVertexList,
        levelVertCountList,
        pbIsFuzzyVert)))
    {
        return hr;
    }

    size_t dwMaxLevel = levelVertCountList.size();
    assert(dwMaxLevel > 0);
    size_t dwMinLevel = 
        std::min<size_t>(
        size_t(dwMaxLevel*FUZYY_REGION_PERCENT + 0.5f),
        dwMaxLevel-1);

    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        pbIsFuzzyFatherFace[m_pFaces[i].dwIDInFatherMesh] = true;
    }

    for (size_t i = levelVertCountList[dwMinLevel];
        i<canidateVertexList.size();
        i++)
    {
        ISOCHARTVERTEX* pVertex = m_pVerts + canidateVertexList[i];
        for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
        {
            ISOCHARTFACE* pFace = m_pFaces + pVertex->faceAdjacent[j];
            pbIsFuzzyFatherFace[pFace->dwIDInFatherMesh] = false;
        }
    }

    return S_OK;
}

HRESULT CIsochartMesh::SpreadFuzzyVert(
    std::vector<uint32_t>& canidateVertexList,
    std::vector<uint32_t>& levelVertCountList,
    bool* pbIsFuzzyVert)
{
    try
    {
        uint32_t dwHead = 0;
        uint32_t dwEnd = static_cast<uint32_t>(canidateVertexList.size());
        ISOCHARTVERTEX* pVertex;
        do{
            levelVertCountList.push_back(dwEnd);

            for (size_t dwStart = dwHead; dwStart < dwEnd; dwStart++)
            {
                pVertex = m_pVerts + canidateVertexList[dwStart];
                for (size_t i=0; i < pVertex->vertAdjacent.size(); i++)
                {
                    uint32_t dwAdjacentVertID = pVertex->vertAdjacent[i];
                    if (pbIsFuzzyVert[dwAdjacentVertID])
                    {
                        continue;
                    }

                    canidateVertexList.push_back(dwAdjacentVertID);
                    pbIsFuzzyVert[dwAdjacentVertID] = true;
                }
            }
            dwHead = dwEnd;
            dwEnd = static_cast<uint32_t>(canidateVertexList.size());
        } while (dwHead != dwEnd);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

// Find such boundary vertex:
// In sub-chart it belongs to a boundary edge but in father chart 
// it doesn't belongs to a boundary edge.
// These vertices are "fuzzy vertices" needed to be optimized.
HRESULT CIsochartMesh::FindNewBoundaryVert(
    std::vector<uint32_t>& canidateVertexList,
    bool* pbIsFuzzyVert)
{
    try
    {
        ISOCHARTVERTEX* pVertex;
        ISOCHARTVERTEX* pFatherVertex;

        for (size_t i=0; i < m_dwVertNumber; i++)
        {
            pVertex = m_pVerts + i;
            if (!pVertex->bIsBoundary)
            {
                continue;
            }

            pFatherVertex =
                m_pFather->m_pVerts + pVertex->dwIDInFatherMesh;

            if (pFatherVertex->bIsBoundary
                && pVertex->vertAdjacent.size() > 1)
            {

                ISOCHARTVERTEX* pVertex1 =
                    m_pVerts + pVertex->vertAdjacent[0];

                ISOCHARTVERTEX*  pVertex2 =
                    m_pVerts + pVertex->vertAdjacent[pVertex->vertAdjacent.size() - 1];

                ISOCHARTVERTEX*  pFatherVertex1 =
                    m_pFather->m_pVerts + pVertex1->dwIDInFatherMesh;

                ISOCHARTVERTEX*  pFatherVertex2 =
                    m_pFather->m_pVerts + pVertex2->dwIDInFatherMesh;

                if (pFatherVertex1->bIsBoundary && pFatherVertex2->bIsBoundary)
                {
                    continue;
                }
            }

            pbIsFuzzyVert[i] = true;
            canidateVertexList.push_back(pVertex->dwID);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

// See formula in section 4.4 of [Kun04]
HRESULT CIsochartMesh::ApplyGraphCutByAngle(
    uint32_t* pdwFaceChartID,
    const bool* pbIsFuzzyFatherFace,
    float* pfEdgeAngleDistance,
    float fAverageAngleDistance)
{
    CGraphcut graphCut;
    std::unique_ptr<uint32_t []> pdwFaceGraphNodeID(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    if (!pdwFaceGraphNodeID)
    {
        return E_OUTOFMEMORY;
    }

    for (size_t dwIteration=0; dwIteration<2; dwIteration++)
    {
        HRESULT hr = DriveGraphCutByAngle(
            graphCut,
            pdwFaceGraphNodeID.get(),
            pdwFaceChartID,
            pbIsFuzzyFatherFace,
            pfEdgeAngleDistance,
            fAverageAngleDistance);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

HRESULT CIsochartMesh::DriveGraphCutByAngle(
    CGraphcut& graphCut,
    uint32_t* pdwFaceGraphNodeID,
    uint32_t* pdwFaceChartID,
    const bool* pbIsFuzzyFatherFace,
    float* pfEdgeAngleDistance,
    float fAverageAngleDistance)
{
    HRESULT hr = S_OK;
    // 1. For each sub-chart, get its adjacent sub-charts
    for (uint32_t i =0; i < m_children.size(); i++)
    {
        CIsochartMesh* pChart = m_children[i];
        pChart->CalculateSubChartAdjacentChart(i, pdwFaceChartID);
    }

    // 2. Optimize boundaries between each 2 sub-charts
    for (uint32_t dwChartIdx1=0; dwChartIdx1<m_children.size(); dwChartIdx1++)
    {
        CIsochartMesh* pChart1 = m_children[dwChartIdx1];
        for (size_t i=0; i<pChart1->m_adjacentChart.size(); i++)
        {
            uint32_t dwChartIdx2 = pChart1->m_adjacentChart[i];
            if (dwChartIdx1 >= dwChartIdx2 )
            {
                continue;
            }

            FAILURE_RETURN(
                OptimizeOneBoundaryByAngle(
                    dwChartIdx1,
                    dwChartIdx2,
                    graphCut,
                    pdwFaceGraphNodeID,
                    pdwFaceChartID,
                    pbIsFuzzyFatherFace,
                    pfEdgeAngleDistance,
                    fAverageAngleDistance));
        }
    }
    return hr;
}

HRESULT CIsochartMesh::OptimizeOneBoundaryByAngle(
    uint32_t dwChartIdx1,
    uint32_t dwChartIdx2,
    CGraphcut& graphCut,
    uint32_t* pdwFaceGraphNodeID,
    uint32_t* pdwFaceChartID,
    const bool* pbIsFuzzyFatherFace,
    float* pfEdgeAngleDistance,
    float fAverageAngleDistance)
{
    // 2.1 Find all fuzzy faces. 
    std::vector<uint32_t> candidateFuzzyFaceList;
    try
    {
        for (uint32_t j = 0; j<m_dwFaceNumber; j++)
        {
            pdwFaceGraphNodeID[j] = INVALID_INDEX;

            if (pbIsFuzzyFatherFace[j] 
                && (pdwFaceChartID[j] == dwChartIdx1
                || pdwFaceChartID[j] == dwChartIdx2))
            {
                pdwFaceGraphNodeID[j] = static_cast<uint32_t>(candidateFuzzyFaceList.size());

                candidateFuzzyFaceList.push_back(j);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    if (candidateFuzzyFaceList.empty())
    {
        return S_OK;
    }

    // 2.2 Perform graph cut 
    uint32_t dwNodeNumber = static_cast<uint32_t>(candidateFuzzyFaceList.size());

    graphCut.Clear();

    HRESULT hr = S_OK;
    FAILURE_RETURN(graphCut.InitGraph(dwNodeNumber));

    std::unique_ptr<CGraphcut::NODEHANDLE[]> hNodes( new (std::nothrow) CGraphcut::NODEHANDLE[dwNodeNumber] );
    if (!hNodes)
    {
        return E_OUTOFMEMORY;
    }

    auto phNodes = hNodes.get();

    for (size_t j=0; j<dwNodeNumber; j++)
    {
        phNodes[j] = graphCut.AddNode();
    }

    for (size_t j=0; j<dwNodeNumber; j++)
    {
        ISOCHARTFACE* pFatherFace;
        pFatherFace = m_pFaces + candidateFuzzyFaceList[j];
        for (size_t k=0; k<3; k++)
        {
            ISOCHARTEDGE& edge = m_edges[pFatherFace->dwEdgeID[k]];
            if (edge.bIsBoundary)
            {
                continue;
            }

            uint32_t dwAdjacentFaceID = INVALID_FACE_ID;
            if (edge.dwFaceID[0] == pFatherFace->dwID)
            {
                dwAdjacentFaceID = edge.dwFaceID[1];
            }
            else
            {
                dwAdjacentFaceID = edge.dwFaceID[0];
            }

            if (pbIsFuzzyFatherFace[dwAdjacentFaceID] && 
                pdwFaceGraphNodeID[dwAdjacentFaceID] != INVALID_INDEX)
            {
                float fWeight = 
                    1+pfEdgeAngleDistance[edge.dwID]/fAverageAngleDistance;
                fWeight = 1 / fWeight;

                _Analysis_assume_(pdwFaceGraphNodeID[pFatherFace->dwID] < dwNodeNumber);
                _Analysis_assume_(pdwFaceGraphNodeID[dwAdjacentFaceID] < dwNodeNumber);
                hr = graphCut.AddEges(
                    phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                    phNodes[pdwFaceGraphNodeID[dwAdjacentFaceID]],
                    fWeight,
                    fWeight);
            }
            else if (!pbIsFuzzyFatherFace[dwAdjacentFaceID])
            {
                _Analysis_assume_(pdwFaceGraphNodeID[pFatherFace->dwID] < dwNodeNumber);
                if (pdwFaceChartID[dwAdjacentFaceID] == dwChartIdx1)
                {
                    hr = graphCut.SetWeights(
                        phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                        FLT_MAX,
                        0);
                }
                else
                {
                    hr = graphCut.SetWeights(
                        phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                        0,
                        FLT_MAX);
                }
            }
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    float fMaxFlow = 0;
    if (FAILED(hr = graphCut.CutGraph(fMaxFlow)))
    {
        return hr;
    }

    for (size_t j=0; j<dwNodeNumber; j++)
    {
        uint32_t dwFaceID = candidateFuzzyFaceList[j];
        _Analysis_assume_(pdwFaceGraphNodeID[dwFaceID] < dwNodeNumber);
        if (graphCut.IsInSourceDomain(phNodes[pdwFaceGraphNodeID[dwFaceID]]))
        {
            pdwFaceChartID[dwFaceID] = dwChartIdx1;
        }
        else
        {
            pdwFaceChartID[dwFaceID] = dwChartIdx2;
        }
    }
    
    return S_OK;
}

///////////////////////////////////////////////////////////////
////////////Optimizing Boundary By Stretch/////////////////////
///////////////////////////////////////////////////////////////
// Optimize boundary according to the combination of first and second objective:
// See Note in file header.
HRESULT CIsochartMesh::OptimizeBoundaryByStretch(
    const float* pfOldGeodesicDistance,
    uint32_t* pdwFaceChartID,
    size_t dwMaxSubchartCount,
    bool& bIsOptimized)
{
    bIsOptimized = false;
    if (dwMaxSubchartCount < 2  || m_children.size() < 2)
    {
        return S_OK;
    }

    std::vector<uint32_t> allLandmark;
    std::vector<uint32_t> oldLandmark;
    std::vector<uint32_t> newLandmark;

    std::unique_ptr<float[]> pfEdgeAngleDistance( new (std::nothrow) float[m_dwEdgeNumber] );
    std::unique_ptr<uint32_t []> pdwChartFuzzyLevel(new (std::nothrow) uint32_t[m_children.size()]);
    std::unique_ptr<bool[]> pbIsFuzzyFatherFace( new (std::nothrow) bool[m_dwFaceNumber] );
    std::unique_ptr<uint32_t []> pdwFaceChartIDBackup(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    std::unique_ptr<float[]> pfNewGeodesicDistance;

    if (!pfEdgeAngleDistance || !pdwChartFuzzyLevel || !pbIsFuzzyFatherFace || !pdwFaceChartIDBackup)
    {
        return E_OUTOFMEMORY;
    }

    memcpy(pdwFaceChartIDBackup.get(),
        pdwFaceChartID,
        sizeof(uint32_t) * m_dwFaceNumber);

    // 1. Calculate dihedral angle for each edge using formula 
    // in [Kun04], section 4
    // If failed to caculate dihedral, then just give up optimize
    float fAverageAngleDistance = 0;
    if (!CalculateEdgeAngleDistance(
            pfEdgeAngleDistance.get(),
            fAverageAngleDistance))
    {
        return S_OK;
    }

    // 2. Compute Fuzzy region and collect local landmarks for each sub-chart
    memset(pbIsFuzzyFatherFace.get(), 0, sizeof(bool)*m_dwFaceNumber);
    HRESULT hr = CalSubchartsFuzzyRegion(
        allLandmark,
        pdwFaceChartID,
        pbIsFuzzyFatherFace.get(),
        pdwChartFuzzyLevel.get());
    if (FAILED(hr))
    {
        return hr;
    }

    // 3. Compute geodesic distance from each landmark to all other vertex
    // Note if the local landmark is also a global one, we can directly use the
    // geodesic distance computed by anterior step.
    pfNewGeodesicDistance.reset( new (std::nothrow) float[allLandmark.size() * m_dwVertNumber] );
    if (!pfNewGeodesicDistance)
    {
        return E_OUTOFMEMORY;
    }

    // 4 Compute distance from vertices to each landmark in allLandmark
    hr = CalParamDistanceToAllLandmarks(
        pfOldGeodesicDistance,
        pfNewGeodesicDistance.get(),
        allLandmark);
    if (FAILED(hr))
    {
        return hr;
    }

    // 5. For each sub-chart, compute it's landmark UV.
    bool bIsDone = false;
    hr = CalSubchartsLandmarkUV(
        pfNewGeodesicDistance.get(),
        allLandmark,
        bIsDone);
    if (FAILED(hr) || !bIsDone)
    {
        return hr;
    }

    // 3.5 Apply graph cut.
    size_t dwSelectPrimaryDimension = 2;
    hr = ApplyGraphCutByStretch(
            allLandmark.size(),
            pdwFaceChartID,
            pbIsFuzzyFatherFace.get(),
            pdwChartFuzzyLevel.get(),
            dwSelectPrimaryDimension,
            pfNewGeodesicDistance.get(),
            pfEdgeAngleDistance.get(),
            fAverageAngleDistance);
    if (FAILED(hr))
    {
        return hr;
    }

    // 3.6 make optimization valid
    hr = ApplyBoundaryOptResult(
        pdwFaceChartID,
        pdwFaceChartIDBackup.get(),
        dwMaxSubchartCount,
        bIsOptimized);

    return hr;
}

HRESULT CIsochartMesh::CalSubchartsFuzzyRegion(
    std::vector<uint32_t>& allLandmark,
    uint32_t* pdwFaceChartID,
    bool* pbIsFuzzyFatherFace,
    uint32_t* pdwChartFuzzyLevel)
{
    std::unique_ptr<bool[]> isVertProcessed( new (std::nothrow) bool[m_dwVertNumber] );
    if (!isVertProcessed)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbIsVertProcessed = isVertProcessed.get();

    memset(pbIsVertProcessed, 0, sizeof(bool)*m_dwVertNumber);

    try
    {
        for (uint32_t i=0; i<m_children.size(); i++)
        {
            CIsochartMesh* pChart = m_children[i];
            for (size_t j = 0; j<pChart->GetFaceNumber(); j++)
            {
                ISOCHARTFACE* pFace = pChart->m_pFaces + j;
                pdwFaceChartID[pFace->dwIDInFatherMesh] = i;
            }

            HRESULT hr = pChart->CalculateLandmarkAndFuzzyRegion(
                pbIsFuzzyFatherFace,
                pdwChartFuzzyLevel[i]);
            if (FAILED(hr))
            {
                return hr;
            }

            for (size_t j = 0; j < pChart->m_landmarkVerts.size(); j++)
            {
                ISOCHARTVERTEX* pVertex =
                    pChart->m_pVerts + pChart->m_landmarkVerts[j];
                if (!pbIsVertProcessed[pVertex->dwIDInFatherMesh])
                {
                    allLandmark.push_back(pVertex->dwIDInFatherMesh);
                    pbIsVertProcessed[pVertex->dwIDInFatherMesh] = true;
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

HRESULT CIsochartMesh::CalculateLandmarkAndFuzzyRegion(
    bool* pbIsFuzzyFatherFace,
    uint32_t& dwFuzzyLevel)
{
    assert(m_bVertImportanceDone);
    assert(m_pFather != 0);

    std::vector<uint32_t> canidateVertexList;

    // 1. Find fuzzy region...
    std::unique_ptr<bool[]> isFuzzyVert( new (std::nothrow) bool[m_dwVertNumber] );
    if (!isFuzzyVert)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbIsFuzzyVert = isFuzzyVert.get();

    memset(pbIsFuzzyVert, 0, sizeof(bool) * m_dwVertNumber);
    
    HRESULT hr = FindNewBoundaryVert(canidateVertexList,pbIsFuzzyVert);
    if (FAILED(hr))
    {
        return hr;
    }

    try
    {
        if (canidateVertexList.empty())
        {
            for (size_t i=0; i<m_dwFaceNumber; i++)
            {
                pbIsFuzzyFatherFace[m_pFaces[i].dwIDInFatherMesh] = false;
            }

            m_landmarkVerts.resize(m_dwVertNumber);
            for (uint32_t i = 0; i<m_dwVertNumber; i++)
            {
                m_landmarkVerts[i] = i;
            }
            dwFuzzyLevel = 0;
            return S_OK;
        }

        std::vector<uint32_t> levelVertCountList;
        if(FAILED(hr = SpreadFuzzyVert(
            canidateVertexList,
            levelVertCountList,
            pbIsFuzzyVert)))
        {
            return hr;
        }

        size_t dwMaxLevel = levelVertCountList.size();
        assert(dwMaxLevel > 0);
        size_t dwMinLevel =
            std::min<size_t>(
                size_t(dwMaxLevel*FUZYY_REGION_PERCENT+0.5f),
                dwMaxLevel-1);

        bool bSucceed = false;
        size_t dwLevel = dwMinLevel+1;
        do{
            dwLevel--;
            if (m_dwVertNumber-levelVertCountList[dwLevel]
                >= MIN_LANDMARK_NUMBER)
            {
                bSucceed = true;
            }
        }while(dwLevel > 0 && !bSucceed);

        dwMinLevel = dwLevel;
        if (bSucceed)
        {
            for(size_t i=levelVertCountList[dwMinLevel];
                i<canidateVertexList.size();
                i++)
            {
                pbIsFuzzyVert[canidateVertexList[i]] = false;
            }
        }
        else
        {
            for (size_t i=0; i<m_dwVertNumber; i++)
            {
                pbIsFuzzyVert[i] = false;
            }
        }

    // 2. Calculate local landmarks.
        m_landmarkVerts.clear();
        for (size_t i=0; i < m_dwVertNumber; i++)
        {
            if (!pbIsFuzzyVert[i])
            {
                m_landmarkVerts.push_back(static_cast<uint32_t>(i));
            }
        }

        if (FAILED(hr = DecreaseLocalLandmark()))
        {
            return hr;
        }

        if (dwMinLevel >= 1)
        {
            dwFuzzyLevel = static_cast<uint32_t>(dwMinLevel - 1);
            for (size_t i=0; i<m_dwFaceNumber; i++)
            {
                pbIsFuzzyFatherFace[m_pFaces[i].dwIDInFatherMesh] = true;
            }

            for (size_t i=levelVertCountList[dwMinLevel];
                i<canidateVertexList.size();
                i++)
            {
                ISOCHARTVERTEX* pVertex = m_pVerts + canidateVertexList[i];
                for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
                {
                    ISOCHARTFACE* pFace =
                        m_pFaces + pVertex->faceAdjacent[j];
                    pbIsFuzzyFatherFace[pFace->dwIDInFatherMesh] = false;
                }
            }
        }
        else
        {
            dwFuzzyLevel = 0;
            for (size_t i=0; i<m_dwFaceNumber; i++)
            {
                pbIsFuzzyFatherFace[m_pFaces[i].dwIDInFatherMesh] = false;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT CIsochartMesh::DecreaseLocalLandmark()
{
    size_t dwLandmarkNumber = m_landmarkVerts.size();

    if (dwLandmarkNumber <= MIN_LANDMARK_NUMBER)
    {
        return S_OK;
    }

    size_t dwLandmarkCount =0;

    for (size_t i=0; i<dwLandmarkNumber-1; i++)
    {
        ISOCHARTVERTEX* pVertex1 = m_pVerts + m_landmarkVerts[i];
        if (pVertex1->nImportanceOrder != MUST_RESERVE)
        {
            int nCurrentMax = pVertex1->nImportanceOrder;
            for (size_t j=i+1; j<dwLandmarkNumber; j++)
            {
                ISOCHARTVERTEX* pVertex2 = m_pVerts +  m_landmarkVerts[j];

                if (pVertex2->nImportanceOrder == MUST_RESERVE
                ||nCurrentMax < pVertex2->nImportanceOrder)
                {
                    nCurrentMax = pVertex2->nImportanceOrder;
                    std::swap(m_landmarkVerts[i],m_landmarkVerts[j]);
                }

                if (pVertex2->nImportanceOrder == MUST_RESERVE)
                {
                    break;
                }
            }
        }

        dwLandmarkCount++;
        if (m_pVerts[m_landmarkVerts[dwLandmarkNumber-1]].nImportanceOrder > 0
        && dwLandmarkCount >= MIN_LANDMARK_NUMBER
        && dwLandmarkCount > 2
        && m_pVerts[m_landmarkVerts[dwLandmarkCount-1]].nImportanceOrder
        != m_pVerts[m_landmarkVerts[dwLandmarkCount-2]].nImportanceOrder)
        {
            break;
        }
    }
    if (dwLandmarkCount != m_landmarkVerts.size())
    {
        assert(dwLandmarkCount < m_landmarkVerts.size());
        try
        {
            m_landmarkVerts.resize(dwLandmarkCount);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}

HRESULT CIsochartMesh::CalParamDistanceToAllLandmarks(
    const float* pfOldGeodesicDistance,
    float* pfNewGeodesicDistance,
    std::vector<uint32_t>& allLandmark)
{
    HRESULT hr = S_OK;

    std::vector<uint32_t> oldLandmark;
    std::vector<uint32_t> newLandmark;

    try
    {
        for (size_t i=0; i < allLandmark.size(); i++)
        {
            ISOCHARTVERTEX* pVertex = m_pVerts + allLandmark[i];
            if (pVertex->bIsLandmark)
            {
                for (size_t j = 0; j < m_landmarkVerts.size(); j++)
                {
                    if (m_landmarkVerts[j] != pVertex->dwID)
                    {
                        continue;
                    }

                    memcpy(
                        pfNewGeodesicDistance + oldLandmark.size()*m_dwVertNumber,
                        pfOldGeodesicDistance + j*m_dwVertNumber,
                        m_dwVertNumber * sizeof(float));

                    oldLandmark.push_back(pVertex->dwID);
                    break;
                }
            }
            else
            {
                newLandmark.push_back(pVertex->dwID);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 3.2 compute geodesic distance from each local landmark to other vertices.
    if (!newLandmark.empty())
    {
        FAILURE_RETURN(
        CalculateGeodesicDistance(
            newLandmark,
            nullptr,
            pfNewGeodesicDistance+oldLandmark.size()*m_dwVertNumber));
    }

    assert(allLandmark.size() == 
        oldLandmark.size()+newLandmark.size());

    size_t dwOldLandmarkNumber = oldLandmark.size();
    for (size_t i=0; i<dwOldLandmarkNumber; i++)
    {
        allLandmark[i] = oldLandmark[i];
    }

    for (size_t i=0; i<newLandmark.size(); i++)
    {
        allLandmark[dwOldLandmarkNumber + i] = newLandmark[i];
    }

    return hr;
}

HRESULT CIsochartMesh::CalSubchartsLandmarkUV(
    float* pfNewGeodesicDistance,
    std::vector<uint32_t>& allLandmark,
    bool& bIsDone)
{
    HRESULT hr = S_OK;
    bIsDone = false;
    for (size_t i=0; i < allLandmark.size(); i++)
    {
        m_pVerts[allLandmark[i]].dwIndexInLandmarkList = static_cast<uint32_t>(i);
    }

    for (size_t i=0; i<m_children.size(); i++)
    {
        CIsochartMesh* pChart = m_children[i];
        for (size_t j=0; j<pChart->m_landmarkVerts.size(); j++)
        {
            ISOCHARTVERTEX* pVertex 
                = pChart->m_pVerts + pChart->m_landmarkVerts[j];

            pVertex->dwIndexInLandmarkList
                = m_pVerts[pVertex->dwIDInFatherMesh].dwIndexInLandmarkList;
        }
    }

    size_t dwSelectPrimaryDimension = 2;
    size_t dwCalculatedPrimaryDimension = 0;
    // 3.3 for each chart comput the vertices's embedding coordinates.
    for (size_t i=0; i<m_children.size(); i++)
    {
        CIsochartMesh* pChart = m_children[i];
        FAILURE_RETURN(
            pChart->CalculateLandmarkUV(
            pfNewGeodesicDistance, 
            dwSelectPrimaryDimension,
            dwCalculatedPrimaryDimension));

        if(dwSelectPrimaryDimension
            != dwCalculatedPrimaryDimension)
        {
            return hr;
        }
    }
    bIsDone = true;
    return hr;
}

HRESULT CIsochartMesh::CalculateLandmarkUV(
    float* pfVertGeodesicDistance,
    const size_t dwSelectPrimaryDimension,
    size_t& dwCalculatedPrimaryDimension)
{
    HRESULT hr = S_OK;

    assert(pfVertGeodesicDistance != 0);
    assert(m_pFather != 0);

    size_t dwSubLandmarkNumber = m_landmarkVerts.size();

    std::unique_ptr<float[]> subDistanceMatrix( new (std::nothrow) float[dwSubLandmarkNumber * dwSubLandmarkNumber] );
    if (!subDistanceMatrix)
    {
        return E_OUTOFMEMORY;
    }

    float* pfSubDistanceMatrix = subDistanceMatrix.get();

    size_t dwFatherVertNumber = m_pFather->GetVertexNumber();
    for (size_t j=0; j<dwSubLandmarkNumber; j++)
    {
        ISOCHARTVERTEX* pVertex1 = m_pVerts + m_landmarkVerts[j];
        pfSubDistanceMatrix[j*dwSubLandmarkNumber + j] = 0;
        for (size_t k = j + 1; k<dwSubLandmarkNumber; k++)
        {
            ISOCHARTVERTEX* pVertex2 = m_pVerts+m_landmarkVerts[k];

            uint32_t dwIndex1 = static_cast<uint32_t>(pVertex1->dwIndexInLandmarkList*dwFatherVertNumber +
                pVertex2->dwIDInFatherMesh);

            uint32_t dwIndex2 = static_cast<uint32_t>(pVertex2->dwIndexInLandmarkList*dwFatherVertNumber + 
                pVertex1->dwIDInFatherMesh);

            pfSubDistanceMatrix[j*dwSubLandmarkNumber + k] 
            = pfSubDistanceMatrix[k*dwSubLandmarkNumber + j]
            = std::min(
                pfVertGeodesicDistance[dwIndex1],
                pfVertGeodesicDistance[dwIndex2]);
        }
    }

    hr = m_isoMap.Init(dwSubLandmarkNumber, pfSubDistanceMatrix);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = m_isoMap.ComputeLargestEigen(
        dwSelectPrimaryDimension,
        dwCalculatedPrimaryDimension);
    if (FAILED(hr))
    {
        return hr;
    }
    assert(dwSelectPrimaryDimension == dwCalculatedPrimaryDimension);

    std::unique_ptr<float[]> vertMappingCoord( new (std::nothrow) float[dwSubLandmarkNumber*dwSelectPrimaryDimension] );
    if (!vertMappingCoord)
    {
        return E_OUTOFMEMORY;
    }
    float *pfVertMappingCoord = vertMappingCoord.get();


    m_isoMap.GetDestineVectors(
        dwSelectPrimaryDimension,
        pfVertMappingCoord);

    const float* pfTempVertMappingCoord = pfVertMappingCoord;
    for (size_t j=0; j<dwSubLandmarkNumber; j++)
    {
        ISOCHARTVERTEX* pVertex = m_pVerts + m_landmarkVerts[j];
        pVertex->uv.x = pfTempVertMappingCoord[0];
        pVertex->uv.y = pfTempVertMappingCoord[1];
        pfTempVertMappingCoord += dwSelectPrimaryDimension;
    }

    return hr;
}


HRESULT CIsochartMesh::CalculateSubChartAdjacentChart(
    uint32_t dwSelfChartID,
    uint32_t* pdwFaceChartID)
{
    m_adjacentChart.clear();
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        ISOCHARTVERTEX* pFatherVertex;
        pFatherVertex = 
            m_pFather->m_pVerts + m_pVerts[i].dwIDInFatherMesh;

        for (size_t j=0; j<pFatherVertex->faceAdjacent.size(); j++)
        {
            uint32_t dwAdjacentFaceID = pFatherVertex->faceAdjacent[j];
            if (pdwFaceChartID[dwAdjacentFaceID] != dwSelfChartID)
            {
                if (!addNoduplicateItem(m_adjacentChart,
                pdwFaceChartID[dwAdjacentFaceID]))
                {
                    return E_OUTOFMEMORY;
                }
            }
        }
    }
    return S_OK;
}

HRESULT CIsochartMesh::ApplyGraphCutByStretch(
    size_t dwLandmarkNumber,
    uint32_t* pdwFaceChartID,
    const bool* pbIsFuzzyFatherFace,
    const uint32_t* pdwChartFuzzyLevel,
    size_t dwDimension,
    float* pfVertGeodesicDistance,
    float* pfEdgeAngleDistance,
    float fAverageAngleDistance)
{
    CGraphcut graphCut;

    // It is possible for the children to have more landmark vertices than their parents.
    // This is due to vertices being cloned in CIsochartMesh::CleanNonmanifoldMesh function.
    // For this allocation simply use the max number of landmark vertices for the size.
    size_t workspaceSize = dwLandmarkNumber;
    for(size_t i = 0; i < m_children.size(); ++i)
    {
        size_t numChildLandmark = m_children[i]->m_landmarkVerts.size();
        if (workspaceSize < numChildLandmark)
        {
            workspaceSize = numChildLandmark;
        }
    }

    std::unique_ptr<float[]> pfWorkSpace( new (std::nothrow) float[workspaceSize] );
    std::unique_ptr<float[]> pfFacesStretchDiff( new (std::nothrow) float[m_dwFaceNumber] );
    std::unique_ptr<uint32_t []> pdwFaceGraphNodeID(new (std::nothrow) uint32_t[m_dwFaceNumber]);

    if ( !pfWorkSpace || !pfFacesStretchDiff || !pdwFaceGraphNodeID)
    {
        return E_OUTOFMEMORY;
    }

    // 3.4 For each sub-chart , getting adjacent sub-charts.
    HRESULT hr = S_OK;
    for (uint32_t i=0; i<m_children.size(); i++)
    {
        CIsochartMesh* pChart = m_children[i];
        hr = pChart->CalculateSubChartAdjacentChart(i, pdwFaceChartID);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    for (uint32_t dwChartIdx1 = 0; dwChartIdx1 < m_children.size(); dwChartIdx1++)
    {
        CIsochartMesh* pChart1 = m_children[dwChartIdx1];

        for (size_t i=0; i<pChart1->m_adjacentChart.size(); i++)
        {
            
            uint32_t dwChartIdx2 = pChart1->m_adjacentChart[i];
            if (dwChartIdx1 >= dwChartIdx2 
            || (pdwChartFuzzyLevel[dwChartIdx1] < 1
                && pdwChartFuzzyLevel[dwChartIdx2] < 1))
            {
                continue;
            }

            hr = 
                OptimizeOneBoundaryByAngle(
                    dwChartIdx1,
                    dwChartIdx2,
                    graphCut,
                    pdwFaceGraphNodeID.get(),
                    pdwFaceChartID,
                    pbIsFuzzyFatherFace,
                    dwDimension,
                    pfVertGeodesicDistance,
                    pfEdgeAngleDistance,
                    fAverageAngleDistance,
                    pfWorkSpace.get(),
                    pfFacesStretchDiff.get());

            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    return S_OK;
}

HRESULT CIsochartMesh::
OptimizeOneBoundaryByAngle(
    uint32_t dwChartIdx1,
    uint32_t dwChartIdx2,
    CGraphcut& graphCut,
    uint32_t* pdwFaceGraphNodeID,
    uint32_t* pdwFaceChartID,
    const bool* pbIsFuzzyFatherFace,
    size_t dwDimension,
    float* pfVertGeodesicDistance,
    float* pfEdgeAngleDistance,
    float fAverageAngleDistance,
    float* pfWorkSpace,
    float* pfFacesStretchDiff)
{
    CIsochartMesh* pChart1 = m_children[dwChartIdx1];
    CIsochartMesh* pChart2 = m_children[dwChartIdx2];

    // 1. Get all fuzzy faces as the nodes of graph
    std::vector<uint32_t> candidateFuzzyFaceList;
    try
    {
        for (uint32_t j = 0; j < m_dwFaceNumber; j++)
        {
            pdwFaceGraphNodeID[j] = INVALID_INDEX;
            if (pbIsFuzzyFatherFace[j]
                && (pdwFaceChartID[j] == dwChartIdx1
                || pdwFaceChartID[j] == dwChartIdx2))
            {
                pdwFaceGraphNodeID[j] = static_cast<uint32_t>(candidateFuzzyFaceList.size());
                candidateFuzzyFaceList.push_back(j);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    if (candidateFuzzyFaceList.empty())
    {
        return S_OK;
    }

    size_t dwNodeNumber = candidateFuzzyFaceList.size();
    float fAverageStetchDiff = 0;
    for (size_t j=0; j<dwNodeNumber; j++)
    {
        ISOCHARTFACE* pFatherFace
            = m_pFaces + candidateFuzzyFaceList[j];

        float fStretch1 =
            CalculateFaceGeodesicDistortion(
                pFatherFace,
                pChart1,
                pfWorkSpace,
                dwDimension,
                pfVertGeodesicDistance);
                
        float fStretch2 =
            CalculateFaceGeodesicDistortion(
                pFatherFace,
                pChart2,
                pfWorkSpace,
                dwDimension,
                pfVertGeodesicDistance);

        pfFacesStretchDiff[pFatherFace->dwID] = 
            fabsf(fStretch1 - fStretch2);
        fAverageStetchDiff += pfFacesStretchDiff[pFatherFace->dwID];
    }
    fAverageStetchDiff = 2*fAverageStetchDiff / dwNodeNumber;

    // Initialize graph
    std::unique_ptr<CGraphcut::NODEHANDLE[]> hNodes( new (std::nothrow) CGraphcut::NODEHANDLE[dwNodeNumber] );
    if (!hNodes)
    {
        return E_OUTOFMEMORY;
    }

    auto phNodes = hNodes.get();

    graphCut.Clear();
    HRESULT hr = graphCut.InitGraph(dwNodeNumber);
    if ( FAILED(hr))
    {
        return hr;
    }

    // Add Nodes to graph
    for (size_t j=0; j<dwNodeNumber; j++)
    {
        phNodes[j] = graphCut.AddNode();
    }

    // Set Nodes and Edge capacity of graph
    for (size_t j=0; j<dwNodeNumber; j++)
    {
        ISOCHARTFACE* pFatherFace;
        pFatherFace = m_pFaces + candidateFuzzyFaceList[j];
        for (size_t k=0; k<3; k++)
        {
            ISOCHARTEDGE& edge = m_edges[pFatherFace->dwEdgeID[k]];

            if (edge.bIsBoundary)
            {
                continue;
            }

            uint32_t dwAdjacentFaceID = INVALID_FACE_ID;
            if (edge.dwFaceID[0] == pFatherFace->dwID)
            {
                dwAdjacentFaceID = edge.dwFaceID[1];
            }
            else
            {
                dwAdjacentFaceID = edge.dwFaceID[0];
            }

            if (pbIsFuzzyFatherFace[dwAdjacentFaceID]
                && pdwFaceGraphNodeID[dwAdjacentFaceID]!=INVALID_INDEX)
            {
                float fWeight = 
                    (1 - OPTIMAL_CUT_STRETCH_WEIGHT)/
                    (1 + pfEdgeAngleDistance[edge.dwID] / fAverageAngleDistance);

                fWeight += 
                    (pfFacesStretchDiff[pFatherFace->dwID] 
                    + pfFacesStretchDiff[dwAdjacentFaceID]) /
                    fAverageStetchDiff *OPTIMAL_CUT_STRETCH_WEIGHT;

                _Analysis_assume_(pdwFaceGraphNodeID[dwAdjacentFaceID] < dwNodeNumber);
                _Analysis_assume_(pdwFaceGraphNodeID[pFatherFace->dwID] < dwNodeNumber);
                hr = graphCut.AddEges(
                    phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                    phNodes[pdwFaceGraphNodeID[dwAdjacentFaceID]],
                    fWeight,
                    fWeight);
            }
            else if(!pbIsFuzzyFatherFace[dwAdjacentFaceID])
            {
                _Analysis_assume_(pdwFaceGraphNodeID[pFatherFace->dwID] < dwNodeNumber);
                if (pdwFaceChartID[dwAdjacentFaceID] == dwChartIdx1)
                {
                    hr = graphCut.SetWeights(
                        phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                        FLT_MAX,
                        0);
                }
                else
                {
                    hr = graphCut.SetWeights(
                        phNodes[pdwFaceGraphNodeID[pFatherFace->dwID]],
                        0,
                        FLT_MAX);
                }
            }
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    // Perform graph cut
    float fMaxFlow = 0;
    if (FAILED(hr=graphCut.CutGraph(fMaxFlow)))
    {
        return hr;
    }

    // Use graph cut result to partition
    for (size_t j=0; j<dwNodeNumber; j++)
    {
        uint32_t dwFaceID = candidateFuzzyFaceList[j];
        _Analysis_assume_(pdwFaceGraphNodeID[dwFaceID] < dwNodeNumber);
        if (graphCut.IsInSourceDomain(phNodes[pdwFaceGraphNodeID[dwFaceID]]))
        {
            pdwFaceChartID[dwFaceID] = dwChartIdx1;
        }
        else
        {
            pdwFaceChartID[dwFaceID] = dwChartIdx2;
        }
    }

    return S_OK;
}

// See more detail in section 4.4.1 in [Kun04]
void CIsochartMesh::CalculateVertGeodesicCoord(
    float* pfCoord,
    ISOCHARTVERTEX* pFatherVertex,
    CIsochartMesh* pChart,
    float* pfWorkSpace,
    size_t dwDimension,
    float* pfVertGeodesicDistance) const
{
    size_t dwLandmarkNumber = pChart->m_landmarkVerts.size();
    const float* pfAverageColumn = pChart->m_isoMap.GetAverageColumn();
    
    for (size_t i=0; i<dwLandmarkNumber; i++)
    {
        ISOCHARTVERTEX* pVertex = 
            pChart->m_pVerts + pChart->m_landmarkVerts[i];

        uint32_t dwIndex = static_cast<uint32_t>(pVertex->dwIndexInLandmarkList*m_dwVertNumber
            +pFatherVertex->dwID);
        
        float fDistance = pfVertGeodesicDistance[dwIndex];
        pfWorkSpace[i] = fDistance*fDistance;
        pfWorkSpace[i] = pfAverageColumn[i] - pfWorkSpace[i] ;
    }

    const float* pfEigenValue = pChart->m_isoMap.GetEigenValue();
    const float* pfEigenVector = pChart->m_isoMap.GetEigenVector();
    
    for (size_t k=0; k<dwDimension; k++)
    {
        pfCoord[k] = 0;
        for(size_t i=0; i<dwLandmarkNumber; i++)
        {
            pfCoord[k] += pfWorkSpace[i]*pfEigenVector[k*dwLandmarkNumber+i];
        }

        pfCoord[k] /= static_cast<float>(IsochartSqrt(pfEigenValue[k]) * 2);
    }
}

// Compute face parameterization geodesic distorition using the formula in section 4.1
// of [Kun04]
float CIsochartMesh::CalculateFaceGeodesicDistortion(
    ISOCHARTFACE* pFatherFace,
    CIsochartMesh* pChart,
    float* pfWorkSpace,
    size_t dwDimension,
    float* pfVertGeodesicDistance) const
{
    assert (dwDimension <= ORIGINAL_CHART_EIGEN_DIMENSION);
    _Analysis_assume_(dwDimension <= ORIGINAL_CHART_EIGEN_DIMENSION);

    float pfCoord[ORIGINAL_CHART_EIGEN_DIMENSION];
    float pfMapCoord[ORIGINAL_CHART_EIGEN_DIMENSION];

    memset(pfMapCoord, 0, dwDimension*sizeof(float));
    for (size_t i=0; i<3; i++)
    {
        CalculateVertGeodesicCoord(
            pfCoord,
            m_pVerts+pFatherFace->dwVertexID[i],
            pChart,
            pfWorkSpace,
            dwDimension,
            pfVertGeodesicDistance);
        
        for (size_t j=0; j<dwDimension; j++)
        {
            pfMapCoord[j] += pfCoord[j];
        }
    }
    for (size_t i=0; i<dwDimension; i++)
    {
        pfMapCoord[i] /= 3;
    }

    float fGeodesicDistance;
    float fEulerDistance;
    float fError;
    size_t dwLandmarkNumber = pChart->m_landmarkVerts.size();
    fError = 0;

    for (size_t i=0; i<dwLandmarkNumber; i++)
    {
        float temp;
        ISOCHARTVERTEX* pSubVertex = pChart->m_pVerts + pChart->m_landmarkVerts[i];
        fEulerDistance = 0;

        temp = (pfMapCoord[0]-pSubVertex->uv.x);
        fEulerDistance +=  temp*temp; 

        temp = (pfMapCoord[1]-pSubVertex->uv.y); 
        fEulerDistance += temp*temp; 
        fEulerDistance = static_cast<float>(IsochartSqrt(fEulerDistance));

        fGeodesicDistance = 0;
        for (size_t j=0; j<3; j++)
        {
            uint32_t dwVertexID = pFatherFace->dwVertexID[j];
            assert(m_pVerts[dwVertexID].dwID == dwVertexID);
            
            fGeodesicDistance
                += pfVertGeodesicDistance[
                pSubVertex->dwIndexInLandmarkList*m_dwVertNumber+dwVertexID];
        }

        fGeodesicDistance /= 3;

        temp = (fEulerDistance-fGeodesicDistance);
        fError += temp * temp;
    }

    fError /= dwLandmarkNumber;
    
    return fError;
}

//
HRESULT CIsochartMesh::ApplyBoundaryOptResult(
    uint32_t* pdwFaceChartID,
    uint32_t* pdwFaceChartIDBackup,
    size_t dwMaxSubchartCount,
    bool& bIsOptimized)
{
    HRESULT hr = S_OK;

    assert(pdwFaceChartID != 0);
    assert(pdwFaceChartIDBackup != 0);
    
    bIsOptimized = true;

    // 1. If all faces have the same chart ID to abandon the boundary optimization.
    bool bHasDifferentID = false;
    for (size_t i=1; i<m_dwFaceNumber; i++)
    {
        if (pdwFaceChartID[0] != pdwFaceChartID[i])
        {
            bHasDifferentID = true;
            break;
        }
    }
    if (!bHasDifferentID)
    {
        memcpy(pdwFaceChartID, pdwFaceChartIDBackup, m_dwFaceNumber * sizeof(uint32_t));
    }

    
    // 2. Using MakePartitionValid to try to make each sub-chart valid.
    if (FAILED(hr = MakePartitionValid(
        dwMaxSubchartCount,
        pdwFaceChartID,
        bIsOptimized)))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA))
        {
            bIsOptimized = false;
            return hr = S_OK;
        }
        return hr;
    }

    return hr;
}
