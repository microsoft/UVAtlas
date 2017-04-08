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
    Terms:
        L2 Stretch: 
            Corresponds to the root-mean-square stretch over all directions
            in the domain.

        Ln Stretch:
            The worst-case norm Ln is the greatest stretch.

        Vertex Stretch:
            The sum of adjacent faces' stretch

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
*/

#include "pch.h"
#include "isochartmesh.h"
#include "UVAtlas.h"
#include "maxheap.hpp"

using namespace Isochart;
using namespace DirectX;

namespace Isochart
{
    struct CHARTOPTIMIZEINFO
    {
        // Parameters to customize the type of optimization
        bool bOptLn;
        bool bOptSignal;
        bool bUseBoundingBox;
        bool bOptBoundaryVert;
        bool bOptInternalVert;

        // Global stretch scale factor, this only used when optimizing by Ln stretch
        float fStretchScale;

        // Parameters to customize the process of optimization
        size_t dwOptTimes;
        size_t dwRandOptOneVertTimes;
        float fBarToStopOptAll;
        float fAverageEdgeLength;
        float fTolerance;

        // Storage of working space
        CMaxHeap<float, uint32_t> heap;
        CMaxHeapItem<float, uint32_t>* pHeapItems;
        float* pfVertStretch;
        float* pfFaceStretch;

        // Bounding Box
        XMFLOAT2 minBound;
        XMFLOAT2 maxBound;

        // Only for Ln stretch
        float fPreveMaxFaceStretch;
        float fInfiniteStretch;
        size_t dwInfinitStretchVertexCount;
        float fInfinitFacesArea;

        CHARTOPTIMIZEINFO() :
            bOptLn(false),
            bOptSignal(false),
            bUseBoundingBox(false),
            bOptBoundaryVert(false),
            bOptInternalVert(false),
            fStretchScale(0),
            dwOptTimes(0),
            dwRandOptOneVertTimes(0),
            fBarToStopOptAll(0),
            fAverageEdgeLength(0),
            fTolerance(0),
            pfVertStretch(nullptr),
            pfFaceStretch(nullptr),
            pHeapItems(nullptr),
            fPreveMaxFaceStretch(0),
            fInfiniteStretch(0),
            dwInfinitStretchVertexCount(0),
            fInfinitFacesArea(0)
        {
        }

        ~CHARTOPTIMIZEINFO()
        {
            SAFE_DELETE_ARRAY(pfVertStretch);
            SAFE_DELETE_ARRAY(pfFaceStretch);
            SAFE_DELETE_ARRAY(pHeapItems);
        }
    };

    struct VERTOPTIMIZEINFO
    {
        ISOCHARTVERTEX* pOptimizeVertex;
        XMFLOAT2 center;
        XMFLOAT2 start;
        XMFLOAT2 end;
        float fStartStretch;
        float fEndStretch;
        float* pfStartFaceStretch;
        float* pfEndFaceStretch;
        float* pfWorkStretch;
        float fRadius;
    };
}

namespace
{
    ///////////////////////////////////////////////////////////
    //////////// Configuration of stretch optimization/////////
    ///////////////////////////////////////////////////////////

    // when 10%+ vertices have infinite stretch, it's hard to get good results 
    // anyway.so don't apply optimize any more 0.1 is based on Kun's examination
    const float MAX_INFINITE_STRETCH_VERT_PERCENT = 0.1f;

    // When vertex being optimized, if the distance between new and old 
    // positions less than OPTIMIZE_TOLERANCE, stop optimize
    const float OPTIMIZE_TOLERANCE = 1e-4f;

    // How many times to optimize the stretch of all vertices in the same chart.
    const size_t ALL_VERTICES_OPTIMIZE_COUNT = 6;

    // Stop optimization when max stretch is smaller than STRETCH_TO_STOP_OPTIMIZE,
    const float STRETCH_TO_STOP_OPTIMIZE = 1.5f;
    const float STRETCH_TO_STOP_OPTIMIZE_IMT = 1e4f;

    // If the max stretch changes less than MINIMAL_OPTIMIZE_CHANGE, stop optimize.
    // Because more optimization won't improve result.
    const float MINIMAL_OPTIMIZE_CHANGE = 0.01f;

    // When moving vertex within its 1-ring neighbor range. A max moving distance
    // should be computed to avoid overlapping. After computing the max distance,
    // scale it with CONSERVATIVE_OPTIMIZE_FACTOR to make sure the vertex is not too
    // close to other edges.
    const float CONSERVATIVE_OPTIMIZE_FACTOR = 0.80f;

    // Direction: left, right, top, bottom
    const size_t BOUND_DIRECTION_NUMBER = 4;
}


///////////// Public Static Functions//////////////////////////
bool CIsochartMesh::IsReachExpectedTotalAvgL2SqrStretch(
    float fCurrAvgL2SqrStretch,
    float fExpectRatio)
{
    return 
        (fCurrAvgL2SqrStretch - 
        ISOCHART_ZERO_EPS*10<= 
        fExpectRatio);
}

uint32_t CIsochartMesh::GetChartWidthLargestGeoAvgStretch(
    ISOCHARTMESH_ARRAY &chartList,
    float& fMaxAvgL2Stretch)
{
    fMaxAvgL2Stretch = 0;
    uint32_t dwIdx = 0;
    for (uint32_t ii = 0; ii<chartList.size(); ii++)
    {
        if (IsInZeroRange(chartList[ii]->m_fChart2DArea)
        || IsInZeroRange(chartList[ii]->m_fChart3DArea))
        {
            continue;
        }

        float fScale = 
            chartList[ii]->m_fChart3DArea / 
            chartList[ii]->m_fChart2DArea;
                
        chartList[ii]->ScaleChart(IsochartSqrtf(fScale));
        
        if (fMaxAvgL2Stretch < 
            chartList[ii]->m_fParamStretchL2 / chartList[ii]->m_fChart3DArea)
        {
            fMaxAvgL2Stretch = 
                chartList[ii]->m_fParamStretchL2 / chartList[ii]->m_fChart3DArea;
            dwIdx = ii;
        }
    }

    return dwIdx;
}

uint32_t CIsochartMesh::GetBestPartitionCanidate(
    ISOCHARTMESH_ARRAY &chartList)
{
    uint32_t dwMaxIdx = INVALID_INDEX;
    float fMaxL2SquaredStretch = -1;

    for (uint32_t ii = 0; ii<chartList.size(); ii++)
    {
        // The average chart stretch has reached the minimal point, No use to parition
        // it again.
        if (chartList[ii]->GetL2SquaredStretch() == 
            chartList[ii]->GetBaseL2SquaredStretch())
        {
            continue;
        }

        // The chart has only one face, can not be partitioned again.
        if (chartList[ii]->GetFaceNumber()==1)
        {
            continue;
        }
        
        if (fMaxL2SquaredStretch < chartList[ii]->GetL2SquaredStretch())
        {
            fMaxL2SquaredStretch = chartList[ii]->GetL2SquaredStretch();
            dwMaxIdx = ii;
        }
    }

    if (INVALID_INDEX == dwMaxIdx)
    {
        for (uint32_t ii = 0; ii<chartList.size(); ii++)
        {
            if (chartList[ii]->GetFaceNumber() > 1)
            {
                dwMaxIdx = ii;
                break;
            }
        }
    }

    assert(INVALID_INDEX != dwMaxIdx);

    return dwMaxIdx;
}

HRESULT CIsochartMesh::OptimizeAllL2SquaredStretch(
        ISOCHARTMESH_ARRAY &chartList,
        bool bOptimizeSignal)
{
    HRESULT hr = S_OK;
    for (size_t ii=0; ii<chartList.size(); ii++)
    {
        FAILURE_RETURN(chartList[ii]->OptimizeChartL2Stretch(bOptimizeSignal));
    }
    return S_OK;
}

float CIsochartMesh::ComputeGeoAvgL2Stretch(
    ISOCHARTMESH_ARRAY& chartList,
    bool bReCompute)
{
    float fTotalGeoL2Stretch = 0;
    float fTotal2DArea = 0;
    float fTotal3DArea = chartList[0]->m_baseInfo.fMeshArea;
    for (size_t ii=0; ii<chartList.size(); ii++)
    {
        if (bReCompute)
            chartList[ii]->m_fGeoL2Stretch = 
                chartList[ii]->CalChartL2GeoSquaredStretch();

        fTotalGeoL2Stretch += chartList[ii]->m_fGeoL2Stretch;
        fTotal2DArea += chartList[ii]->m_fChart2DArea;
    }	

    return fTotal2DArea*fTotalGeoL2Stretch / (fTotal3DArea*fTotal3DArea);
}

HRESULT CIsochartMesh::OptimalScaleChart(
    ISOCHARTMESH_ARRAY& chartList,
    float fOpticalAvgL2SquaredStretch,
    bool bOptimizeSignal)
{
    if (chartList.empty())
    {
        return S_OK;
    }

    const CBaseMeshInfo& baseInfo = chartList[0]->m_baseInfo;

    float fSumSqrtEiiaii = 
        IsochartSqrtf(fOpticalAvgL2SquaredStretch) * baseInfo.fMeshArea;
    if (IsInZeroRange2(fSumSqrtEiiaii))
    {
        return S_OK;
    }

    // 1. Decide the largest chart area after scale
    float fTotalDomainArea = 0;
    for (size_t ii=0; ii<chartList.size(); ii++)
    {
        fTotalDomainArea += chartList[ii]->m_fChart2DArea;
    }
    fTotalDomainArea /= STANDARD_SPACE_RATE;
    
    float fSmallest2DChartArea = 
        fTotalDomainArea * SMALLEST_CHART_PIXEL_AREA;
    float fSmallest3DChartArea = 
        baseInfo.fMeshArea * SMALLEST_CHART_PIXEL_AREA;

    float fTotalOpticalDomainArea = 0;
    for (size_t ii=0; ii<chartList.size(); ii++)
    {
        float fEii = chartList[ii]->m_fParamStretchL2;
        float faii = chartList[ii]->m_fChart2DArea;

        if (IsInZeroRange2(faii))
        {
            continue;
        }

        float fAlpha = 
            (IsochartSqrtf(fEii/faii)) * fTotalDomainArea/
            (fSumSqrtEiiaii);

        if ( chartList[ii]->m_dwFaceNumber == 1 && baseInfo.pfIMTArray)
        {
            const FLOAT3* p = 
                baseInfo.pfIMTArray+chartList[ii]->m_pFaces->dwIDInRootMesh;
            if (((*p)[0] > (*p)[2] && (*p)[2] / (*p)[0] < 1e-8) ||
                ((*p)[0] < (*p)[2] && (*p)[0] / (*p)[2] < 1e-8) )
            {
                continue;
            }
        }
        
        if (baseInfo.pfIMTArray &&
             chartList[ii]->m_fChart2DArea*fAlpha < fSmallest2DChartArea && 
             chartList[ii]->m_fChart2DArea > fSmallest2DChartArea &&
             chartList[ii]->m_fChart3DArea > fSmallest3DChartArea)
        {
            fAlpha = fSmallest2DChartArea / chartList[ii]->m_fChart2DArea;
        }

        if (bOptimizeSignal)
        {
            if (chartList[ii]->m_fGeoL2Stretch > 
            baseInfo.fExpectAvgL2SquaredStretch*chartList[ii]->m_fChart3DArea
            *fAlpha)
            {
                fAlpha = 
                    baseInfo.fExpectAvgL2SquaredStretch*
                    chartList[ii]->m_fChart3DArea /
                    chartList[ii]->m_fGeoL2Stretch;					
            }

            if (chartList[ii]->m_fGeoL2Stretch < 
            baseInfo.fExpectMinAvgL2SquaredStretch*chartList[ii]->m_fChart3DArea
            *fAlpha)
            {
                fAlpha = 
                    baseInfo.fExpectMinAvgL2SquaredStretch*
                    chartList[ii]->m_fChart3DArea /
                    chartList[ii]->m_fGeoL2Stretch;	
            }
        }
        /*
        if (fAlpha > OPTIMAL_SCALE_FACTOR)
        {
            fAlpha = OPTIMAL_SCALE_FACTOR;			
        }
        if (fAlpha < 1.0f / OPTIMAL_SCALE_FACTOR)
        {
            fAlpha = 1.0f / OPTIMAL_SCALE_FACTOR;
        }
        */
        chartList[ii]->ScaleChart(IsochartSqrtf(fAlpha));
        fTotalOpticalDomainArea += chartList[ii]->m_fChart2DArea;

        if (bOptimizeSignal && !IsInZeroRange2(fAlpha))
        {
            chartList[ii]->m_fGeoL2Stretch /= fAlpha;
        }
    }

    return S_OK;
}

float CIsochartMesh::CalOptimalAvgL2SquaredStretch(
    ISOCHARTMESH_ARRAY& chartList) // Scale each chart.
{
    if (chartList.empty())
    {
        return 0;
    }

    bool bAllChartSatisfiedStretch = true;
    const CBaseMeshInfo& baseInfo = chartList[0]->m_baseInfo;
    float fSumSqrtEiiaii = 0;
    for (size_t ii=0; ii<chartList.size(); ii++)
    {
        float fEii = chartList[ii]->m_fParamStretchL2;
        float faii = chartList[ii]->m_fChart2DArea;
        bAllChartSatisfiedStretch = 
            (bAllChartSatisfiedStretch && (fEii == faii));

        fSumSqrtEiiaii += IsochartSqrtf(fEii * faii);
    }

    if (bAllChartSatisfiedStretch)
    {
        return 1;
    }
    
    return (fSumSqrtEiiaii/baseInfo.fMeshArea)*(fSumSqrtEiiaii/baseInfo.fMeshArea);
}
    
//////////////Main Functions////////////////////////////////

HRESULT CIsochartMesh::OptimizeWholeChart(
    float fMaxAvgGeoL2Stretch)
{
    HRESULT hr = S_OK;

    float fNewGeoL2Stretch = 0.f;

    // 1. Check if parameterized
    assert(m_bIsParameterized);

    // 2. Calculate sum of IMT of all triangles.	
    float f2D = 0;

    double dm[3] = {0, 0, 0};	
    double dGeoM[3] = {0, 0, 0};	

    float m[3];
    float geoM[3];	

    float matrix[4];
    
    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t ii=0; ii<m_dwFaceNumber; ii++)
    {
        float fStretch = CalFaceSigL2SquraedStretch(
            pFace,
            m_pVerts[pFace->dwVertexID[0]].uv,
            m_pVerts[pFace->dwVertexID[1]].uv,
            m_pVerts[pFace->dwVertexID[2]].uv,
            f2D,
            m,
            geoM);
        if (fStretch == INFINITE_STRETCH)
        {
            DPF(0, "Can not opimize scale all chart, some face has infinite stretch");
            goto LEnd;
        }
        dm[0] += m[0];dm[1] += m[1];dm[2] += m[2];

        float fFace3DArea = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
        dGeoM[0] += geoM[0] * fFace3DArea;
        dGeoM[1] += geoM[1] * fFace3DArea;
        dGeoM[2] += geoM[2] * fFace3DArea;
        pFace++;
    }
    
    m[0] = static_cast<float>(dm[0] / m_dwFaceNumber);
    m[1] = static_cast<float>(dm[1] / m_dwFaceNumber);
    m[2] = static_cast<float>(dm[2] / m_dwFaceNumber);

    // 3. Get Transform matrix.
    
    CalL2SquaredStretchLowBoundOnFace(
        m,
        1,
        CHART_MAX_SCALE_FACTOR,
        matrix);

    fNewGeoL2Stretch 
        = static_cast<float>(
        (dGeoM[0] * (matrix[0]*matrix[0] + matrix[2]*matrix[2])
        + dGeoM[2] * (matrix[1]*matrix[1] + matrix[3]*matrix[3])
        + 2*dGeoM[1]*(matrix[1]*matrix[0] + matrix[2]*matrix[3]))/2);
    if (fNewGeoL2Stretch > fMaxAvgGeoL2Stretch*m_fChart3DArea)
    {
        goto LEnd;
    }
    // 4. transform each vertex.
    for (size_t ii=0; ii<m_dwVertNumber; ii++)
    {
        TransformUV(
            m_pVerts[ii].uv, 
            m_pVerts[ii].uv, 
            matrix);
    }
    
LEnd:
    return hr;
}

HRESULT CIsochartMesh::InitOptimizeInfo(
    bool bOptLn,
    bool bOptSignal,
    bool bUseBoundingBox,
    bool bOptBoundaryVert,
    bool bOptInternalVert,
    float fBarToStopOpt,
    size_t dwOptTimes,
    size_t dwRandOptOneVertTimes,
    bool bCalStretch,
    CHARTOPTIMIZEINFO& optimizeInfo,
    bool& bCanOptimize)
{
    bCanOptimize = false;

    if (bUseBoundingBox)
    {
        CalculateChartMinimalBoundingBox(
             BOUND_DIRECTION_NUMBER,
             optimizeInfo.minBound,
             optimizeInfo.maxBound);
    }

    // If Never allocated working memory, allocate it.
    if (!optimizeInfo.pfFaceStretch)
    {
        optimizeInfo.pfFaceStretch = new (std::nothrow) float[m_dwFaceNumber];
        optimizeInfo.pfVertStretch = new (std::nothrow) float[m_dwVertNumber];
        optimizeInfo.pHeapItems = new (std::nothrow) CMaxHeapItem<float, uint32_t>[m_dwVertNumber];
    }

    if (!optimizeInfo.pfFaceStretch || !optimizeInfo.pfVertStretch || !optimizeInfo.pHeapItems)
    {
        ReleaseOptimizeInfo(optimizeInfo);
        return E_OUTOFMEMORY;
    }

    if (bOptLn)
    {
        float fChartArea2D = 0;
        float fChartArea3D = 0;
        if (!CalculateChart2DTo3DScale(
                optimizeInfo.fStretchScale,
                fChartArea3D,
                fChartArea2D))
        {
            return S_OK;
        }
    }
    else
    {
        optimizeInfo.fStretchScale = 1;
    }
    
    if (0 == optimizeInfo.fAverageEdgeLength)
    {
        optimizeInfo.fAverageEdgeLength = CalculateAverageEdgeLength();
    }
    
    optimizeInfo.fTolerance = OPTIMIZE_TOLERANCE;
    optimizeInfo.bOptLn = bOptLn;
    optimizeInfo.bOptSignal = bOptSignal;
    optimizeInfo.bUseBoundingBox = bUseBoundingBox;
    optimizeInfo.bOptBoundaryVert = bOptBoundaryVert;
    optimizeInfo.bOptInternalVert = bOptInternalVert;
    optimizeInfo.fBarToStopOptAll = fBarToStopOpt;
    optimizeInfo.dwOptTimes = dwOptTimes;
    optimizeInfo.dwRandOptOneVertTimes = dwRandOptOneVertTimes;
    optimizeInfo.fInfiniteStretch = INFINITE_STRETCH/2;

    if (bCalStretch)
    {
        float f2D = 0;	
        ISOCHARTFACE* pFace = m_pFaces;

        for (size_t i=0; i<m_dwFaceNumber; i++)
        {
            optimizeInfo.pfFaceStretch[i] =
                CalFaceSquraedStretch(
                    optimizeInfo.bOptLn,
                    optimizeInfo.bOptSignal,
                    pFace,
                    m_pVerts[pFace->dwVertexID[0]].uv,
                    m_pVerts[pFace->dwVertexID[1]].uv,
                    m_pVerts[pFace->dwVertexID[2]].uv,
                    optimizeInfo.fStretchScale,
                    f2D);

            if (bOptLn && 
                optimizeInfo.pfFaceStretch[i] > optimizeInfo.fPreveMaxFaceStretch)
            {
                optimizeInfo.fPreveMaxFaceStretch = optimizeInfo.pfFaceStretch[i];
            }

            pFace++;
        }

        // 2. Compute Stretch for each vertex.
        ISOCHARTVERTEX* pVertex = m_pVerts;
        for (size_t i=0; i<m_dwVertNumber; i++)
        {		
            optimizeInfo.pfVertStretch[i] = 
                CalculateVertexStretch(	
                    optimizeInfo.bOptLn,
                    pVertex, 
                    optimizeInfo.pfFaceStretch);

            if (bOptLn && 
                optimizeInfo.pfVertStretch[i] >= optimizeInfo.fInfiniteStretch)
            {
                optimizeInfo.dwInfinitStretchVertexCount++;
            }	
            pVertex++;
        }
    }
    
    bCanOptimize = true;
    
    return S_OK;
}

void CIsochartMesh::ReleaseOptimizeInfo(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    SAFE_DELETE_ARRAY(optimizeInfo.pfFaceStretch);
    SAFE_DELETE_ARRAY(optimizeInfo.pfVertStretch);
    SAFE_DELETE_ARRAY(optimizeInfo.pHeapItems);
}

HRESULT CIsochartMesh::OptimizeChartL2Stretch(bool bOptimizeSignal)
{
    #if OPT_CHART_L2_STRETCH_ONCE
    if (m_bOptimizedL2Stretch && !bOptimizeSignal)
    {
        return S_OK;
    }
    #endif

    if (IsInZeroRange(fabsf(m_fParamStretchL2 - m_fBaseL2Stretch)) && !bOptimizeSignal)
    {
        m_fChart2DArea = m_fChart3DArea;
        m_bOptimizedL2Stretch = true;
        return S_OK;
    }
    if (m_dwFaceNumber == 1)
    {
        ParameterizeOneFace(
            bOptimizeSignal, 
            m_pFaces);		
        m_fChart2DArea = m_fChart3DArea;
        m_bOptimizedL2Stretch = true;
        return S_OK;
    }

    CHARTOPTIMIZEINFO optimizeInfo;
    HRESULT hr = S_OK;

    bool bCanOptimize = false;
    if (bOptimizeSignal)
    {	
        if (FAILED (hr =
            InitOptimizeInfo(
                false,
                true,
                false,
                false,
                true,
                0,
                L2_PREV_OPTIMIZESIG_COUNT,
                RAND_OPTIMIZE_L2_COUNT,
                true, 
                optimizeInfo,
                bCanOptimize)) || !bCanOptimize)
        {
            return hr;
        }
        FAILURE_RETURN(OptimizeStretch(optimizeInfo));

        OptimizeWholeChart(m_baseInfo.fExpectAvgL2SquaredStretch);

        if (FAILED (hr =
            InitOptimizeInfo(
                false,
                true,
                true,
                true,
                true,
                0,
                L2_POST_OPTIMIZESIG_COUNT,
                RAND_OPTIMIZE_L2_COUNT,
                true,
                optimizeInfo,
                bCanOptimize)) || !bCanOptimize)
        {
            return hr;
        }
        FAILURE_RETURN(OptimizeStretch(optimizeInfo));
    }
    else
    {
        if (FAILED (hr =
            InitOptimizeInfo(
                true,
                false,
                false,
                true,
                true,
                STRETCH_TO_STOP_LN_OPTIMIZE,
                LN_OPTIMIZE_COUNT,
                RAND_OPTIMIZE_LN_COUNT,
                true,
                optimizeInfo,
                bCanOptimize)) || !bCanOptimize)
        {
            return hr;
        }
        FAILURE_RETURN(OptimizeStretch(optimizeInfo));

        if (FAILED (hr =
            InitOptimizeInfo(
                false,
                false,
                false,
                false,
                true,
                0,
                L2_OPTIMIZE_COUNT,
                RAND_OPTIMIZE_L2_COUNT,
                true,
                optimizeInfo,
                bCanOptimize)) || !bCanOptimize)
        {
            return hr;
        }
        FAILURE_RETURN(OptimizeStretch(optimizeInfo));
    }

    m_fParamStretchL2 = 0;
    for (size_t ii=0; ii<m_dwFaceNumber; ii++)
    {
        m_fParamStretchL2 += optimizeInfo.pfFaceStretch[ii];		
    }
    m_fChart2DArea = CalculateChart2DArea();

    m_bOptimizedL2Stretch = true;
    return hr;
}

HRESULT CIsochartMesh::OptimizeGeoLnInfiniteStretch(
    bool& bSucceed)
{
    CHARTOPTIMIZEINFO optimizeInfo;

    bSucceed = false;

    HRESULT hr = S_OK;

    bool bCanOptimize = false;
    if (FAILED (hr =
        InitOptimizeInfo(
            true,
            false,
            false,
            true,
            true,
            0,
            INFINITE_VERTICES_OPTIMIZE_COUNT,
            RAND_OPTIMIZE_INFINIT_COUNT,
            true,
            optimizeInfo,
            bCanOptimize)))
    {
        return hr;
    }

    size_t dwBoundaryInfFaces = 0;
    if (bCanOptimize)
    {
        if (optimizeInfo.dwInfinitStretchVertexCount == 0)
        {
            bSucceed = true;
            return hr;
        }

        auto pHeapItems = optimizeInfo.pHeapItems;
        for (uint32_t i = 0; i<m_dwVertNumber; i++)
        {
            pHeapItems[i].m_weight = optimizeInfo.pfVertStretch[i];
            pHeapItems[i].m_data = i;
        }

        FAILURE_RETURN(
            OptimizeVertexWithInfiniteStretch(
                optimizeInfo));

        optimizeInfo.fInfinitFacesArea = 0;
        optimizeInfo.dwInfinitStretchVertexCount = 0;
        for (size_t i=0; i<m_dwFaceNumber; i++)
        {
            if (optimizeInfo.pfFaceStretch[i] >= optimizeInfo.fInfiniteStretch)
            {
                optimizeInfo.dwInfinitStretchVertexCount++;
                optimizeInfo.fInfinitFacesArea += 
                    m_baseInfo.pfFaceAreaArray[m_pFaces[i].dwIDInRootMesh];

                bool bBondary = 
                    m_pVerts[m_pFaces[i].dwVertexID[0]].bIsBoundary
                    ||m_pVerts[m_pFaces[i].dwVertexID[1]].bIsBoundary
                    ||m_pVerts[m_pFaces[i].dwVertexID[2]].bIsBoundary;
                dwBoundaryInfFaces += (bBondary?1:0);				
            }
        }

        bSucceed = 
            ((optimizeInfo.fInfinitFacesArea / m_fChart3DArea) <= 
             m_baseInfo.fOverturnTolerance);
    }

    if (!bSucceed)
    {
        DPF(1, "Infinite Optimize faild, %Iu Internal infinite vertices,%Iu boundary vert",
        optimizeInfo.dwInfinitStretchVertexCount-dwBoundaryInfFaces,
        dwBoundaryInfFaces);
    }
    return hr;

}

HRESULT CIsochartMesh::OptimizeStretch(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    HRESULT hr = S_OK;

    if (optimizeInfo.fPreveMaxFaceStretch == 0)
    {
        optimizeInfo.fPreveMaxFaceStretch = INFINITE_STRETCH;
    }

    auto pHeapItems = optimizeInfo.pHeapItems;
    for (uint32_t i = 0; i<m_dwVertNumber; i++)
    {
        pHeapItems[i].m_weight = optimizeInfo.pfVertStretch[i];
        pHeapItems[i].m_data = i;
    }

    FAILURE_RETURN(
        OptimizeAllVertex(optimizeInfo));

    return hr;
}

float CIsochartMesh::CalChartL2GeoSquaredStretch()
{
    ISOCHARTFACE* pFace = m_pFaces;
    float f2D = 0;
    float fTotalParamStretchL2 = 0;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        float fFaceStretchL2 = CalFaceGeoL2SquraedStretch(
            pFace,
            m_pVerts[pFace->dwVertexID[0]].uv,
            m_pVerts[pFace->dwVertexID[1]].uv,
            m_pVerts[pFace->dwVertexID[2]].uv,
            f2D);

        if (fFaceStretchL2 >= INFINITE_STRETCH)
        {
            fTotalParamStretchL2 = INFINITE_STRETCH;
            return INFINITE_STRETCH;
        }

        fTotalParamStretchL2 += fFaceStretchL2;
        pFace++;
    }
    return fTotalParamStretchL2;
}

float CIsochartMesh::CalCharLnSquaredStretch()
{
    // 1. Is fTotalArea3D is zero, this function will return false. Because the
    // stretches of Zero-area face are meaningless, just set them to the minimal
    // value
    float fTotalArea2D = 0;
    float fTotalArea3D = 0;
    float fStretchScale = 0;

    m_fParamStretchLn = 1.0;
    if (!CalculateChart2DTo3DScale(fStretchScale, fTotalArea3D, fTotalArea2D))
    {
        return 1.0f;
    }
    // 2. Caculate stretch	
    
    ISOCHARTFACE* pFace = m_pFaces;
    float f2D;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        float fFaceStretchN = INFINITE_STRETCH;

        //For each face, caculate Ln and L2 stretch.
        fFaceStretchN = CalFaceGeoLNSquraedStretch(
            pFace,
            m_pVerts[pFace->dwVertexID[0]].uv,
            m_pVerts[pFace->dwVertexID[1]].uv,
            m_pVerts[pFace->dwVertexID[2]].uv,
            fStretchScale,
            f2D);

        if (fFaceStretchN >= INFINITE_STRETCH)
        {
            m_fParamStretchLn = INFINITE_STRETCH;
            return INFINITE_STRETCH;
        }

        if (m_fParamStretchLn < fFaceStretchN)
        {
            m_fParamStretchLn = fFaceStretchN;
        }

        pFace++;
    }
    return m_fParamStretchLn;
}

float CIsochartMesh::CalCharBaseL2SquaredStretch()
{	
    m_fBaseL2Stretch =  m_fChart3DArea;
    return m_fBaseL2Stretch;
}

/////////////Assistant Functions/////////////////////////////

float CIsochartMesh::CalculateVertexStretch(
    bool bOptLn,
    const ISOCHARTVERTEX* pVertex,
    const float* pfFaceStretch) const
{
    float fVertStretch = 0;
    if (bOptLn)
    {
        for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
        {
            if (fVertStretch <
                pfFaceStretch[pVertex->faceAdjacent[j]])
            {
                fVertStretch =
                    pfFaceStretch[pVertex->faceAdjacent[j]];
            }
        }
    }
    else
    {
        for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
        {
            ISOCHARTFACE* pFace = m_pFaces + pVertex->faceAdjacent[j];
            if (fVertStretch == INFINITE_STRETCH)
            {
                return INFINITE_STRETCH;
            }
            fVertStretch += pfFaceStretch[pFace->dwID];
        }
    }

    return fVertStretch;
}

float CIsochartMesh::CalFaceSquraedStretch(
    bool bOptLn,
    bool bOptSignal,
    const ISOCHARTFACE* pFace,
    const XMFLOAT2& v0,
    const XMFLOAT2& v1,
    const XMFLOAT2& v2,
    const float fScale,
    float& f2D,
    float* pfGeoM) const
{
    if (bOptSignal)
    {
        return 
            CalFaceSigL2SquraedStretch(
                pFace,
                v0,
                v1,
                v2,
                f2D,
                nullptr,
                pfGeoM);
    }
    else if (bOptLn)
    {
        return 
            CalFaceGeoLNSquraedStretch(
                pFace,
                v0,
                v1,
                v2,
                fScale,
                f2D);
    }
    else
    {
        return 
            CalFaceGeoL2SquraedStretch(
                pFace,
                v0,
                v1,
                v2,
                f2D);
    }
        
}

static inline void SetAffineParameter(
    float* pGeoM,
    float fGeoMValue,	
    const float* pGeoMBuffer,	
    float* pM,
    float fMValue,
    const float* pMBuffer)
{
    if (pGeoM)
    {
        if (pGeoMBuffer)
        {
            memcpy(pGeoM, pGeoMBuffer, 3*sizeof(float));	
        }
        else
        {
            pGeoM[0] = pGeoM[1] = pGeoM[2] = fGeoMValue;
        }
    }

    if (pM)
    {
        if (pMBuffer)
        {
            memcpy(pM, pMBuffer, IMT_DIM*sizeof(float));
        }
        else
        {
            SetAllIMTValue(pM, fMValue);
        }
    }
}

float CIsochartMesh::CalFaceSigL2SquraedStretch(
    const ISOCHARTFACE* pFace,
    const XMFLOAT2& v0,
    const XMFLOAT2& v1,
    const XMFLOAT2& v2,
    float& f2D,
    float* pM,
    float* pGeoM) const
{
    float f3D = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
    f2D = Cal2DTriangleArea(
        v0, v1, v2);

    const FLOAT3* pMT = 
        m_baseInfo.pfIMTArray+ pFace->dwIDInRootMesh;

    FLOAT3 IMT;
    GetIMTOnCanonicalFace((const float*)(*pMT), f3D, IMT);

    if (f3D == 0)
    {
        SetAffineParameter(pGeoM, 1, nullptr, pM, 0, nullptr);
        return 0;
    }
    else if (f2D < 0)
    {
        SetAffineParameter(
            pGeoM, 
            FLT_MAX, 
            nullptr,
            pM,
            FLT_MAX, 
            nullptr);
        
        return INFINITE_STRETCH;
    }
    
    else if (f2D < ISOCHART_ZERO_EPS2)
    {
        if (IsInZeroRange2(f3D))
        {
            SetAffineParameter(pGeoM, 1, nullptr, pM, 0, nullptr);
            return 0;
        }
        else
        {
            SetAffineParameter(
                pGeoM, 
                FLT_MAX, 
                nullptr,
                pM,
                FLT_MAX, 
                nullptr);
            return INFINITE_STRETCH;
        }
    }
    else
    {
        XMFLOAT2* pCanonicalUV = 
            m_baseInfo.pFaceCanonicalUVCoordinate + pFace->dwIDInRootMesh * 3;

        FLOAT3 newIMT;
        float geo[3] = {0.0};
        AffineIMTOn2D(
            f2D,
            &v0,
            &v1,
            &v2,
            newIMT,
            pCanonicalUV,
            pCanonicalUV+1,
            pCanonicalUV+2,	
            IMT,
            geo);
    
        float fGeoStretch = (geo[0]+geo[1])/2 * f3D;		

        #if PIECEWISE_CONSTANT_IMT
        float fSigStretch = (newIMT[0]+newIMT[2])/2;
        #else
        #endif

        SetAffineParameter(
            pGeoM,
            0,
            geo,
            pM,
            0, 
            newIMT);
        
        return CombineSigAndGeoStretch(
            *pMT, fSigStretch, fGeoStretch);
    }
}

float CIsochartMesh::CalFaceGeoL2SquraedStretch(
    const ISOCHARTFACE* pFace,
    const XMFLOAT2& v0,
    const XMFLOAT2& v1,
    const XMFLOAT2& v2,
    float& f2D) const
{
    float f3D = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
    f2D = Cal2DTriangleArea(
        v0, v1, v2);

    // if original triangle's area is 0, No geodesic stretch.
    if (f3D == 0)
    {
        return 0;
    }	
    else if (f2D < 0 || 
        (f2D < ISOCHART_ZERO_EPS2 && f2D < f3D /2))
    {
        return INFINITE_STRETCH;
    }
    else if (IsInZeroRange2(f2D) && 
        IsInZeroRange2(f3D))
    {
        return 0;
    }
    else
    {
        XMFLOAT3 Ss, St;
        Compute2DtoNDPartialDerivatives(
            f2D,
            &v0,
            &v1,
            &v2,
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[0]].dwIDInRootMesh]),
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[1]].dwIDInRootMesh]),
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[2]].dwIDInRootMesh]),
            3,
            (float*)&Ss,
            (float*)&St);

        XMVECTOR vSs = XMLoadFloat3(&Ss);
        XMVECTOR vSt = XMLoadFloat3(&St);
        float a = XMVectorGetX(XMVector3Dot(vSs, vSs));
        float c = XMVectorGetX(XMVector3Dot(vSt, vSt));

        return (a+c)*f3D / 2;
    }
}

float CIsochartMesh::CalFaceGeoLNSquraedStretch(
    const ISOCHARTFACE* pFace,
    const XMFLOAT2& v0,
    const XMFLOAT2& v1,
    const XMFLOAT2& v2,
    const float fScale,
    float& f2D) const
{
    float f3D = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
    f2D = Cal2DTriangleArea(
        v0, v1, v2);

    // if original triangle's area is 0, No geodesic stretch.
    if (f3D == 0)
    {
        return 1;
    }	
    else if (f2D < 0 || 
        (f2D < ISOCHART_ZERO_EPS2 && f2D < f3D /2))
    {
        return INFINITE_STRETCH;
    }
    else if (IsInZeroRange2(f2D) && IsInZeroRange2(f3D))
    {
        return 1;
    }
    else
    {
        XMFLOAT3 Ss, St;
        Compute2DtoNDPartialDerivatives(
            f2D,
            &v0,
            &v1,
            &v2,
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[0]].dwIDInRootMesh]),
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[1]].dwIDInRootMesh]),
            (const float*)(&m_baseInfo.pVertPosition[
                m_pVerts[pFace->dwVertexID[2]].dwIDInRootMesh]),
            3,
            (float*)&Ss,
            (float*)&St);

        XMVECTOR vSs = XMLoadFloat3(&Ss);
        XMVECTOR vSt = XMLoadFloat3(&St);
        float a = XMVectorGetX(XMVector3Dot(vSs, vSs));
        float c = XMVectorGetX(XMVector3Dot(vSt, vSt));
        float b = XMVectorGetX(XMVector3Dot(vSs, vSt));

        float fTemp = (a-c)*(a-c)+4*b*b;
        assert(fTemp >= 0);

        float fTemp1 = (a+c+IsochartSqrtf(fTemp))/2;
        assert(fTemp1 >= 0);

        float fFaceStretchN = 
            fScale * IsochartSqrtf(fTemp1);


        float fMinSingleValue;
        fTemp1 = (a+c-IsochartSqrtf(fTemp))/2;
        if (fTemp1 >=0 )
        {
            assert(fTemp1 >=0 );
            fTemp = fScale * IsochartSqrtf(fTemp1);
            if (!IsInZeroRange(fTemp) )
            {
                fMinSingleValue = static_cast<float>(1/fTemp);
                if (fFaceStretchN < fMinSingleValue)
                {
                    fFaceStretchN = fMinSingleValue;
                }
            }
            else
            {
                fFaceStretchN = INFINITE_STRETCH;
            }
        }
        else
        {
            if (fFaceStretchN  < 1.0f)
            {
                fFaceStretchN = 1/fFaceStretchN;
            }
        }
        return fFaceStretchN;
    }
}



// Caculate average edge length
float CIsochartMesh::CalculateAverageEdgeLength()
{
    float fAverageEdgeLength = 0;
    for (size_t i=0; i<m_edges.size(); i++)
    {
        ISOCHARTVERTEX* pVertex1 = m_pVerts + m_edges[i].dwVertexID[0];
        ISOCHARTVERTEX* pVertex2 = m_pVerts + m_edges[i].dwVertexID[1];

        float x = pVertex1->uv.x - pVertex2->uv.x;
        float y = pVertex1->uv.y - pVertex2->uv.y;

        fAverageEdgeLength += (x*x + y*y);
    }
    fAverageEdgeLength = 
        IsochartSqrtf(fAverageEdgeLength/m_edges.size());

    return fAverageEdgeLength;
}

// Caculate chart 3D surface area and 2D area.
bool CIsochartMesh::CalculateChart2DTo3DScale(
    float& fScale, 
    float& fChart3DArea, 
    float& fChart2DArea)

{
    fChart2DArea = CalculateChart2DArea();
    fChart3DArea = m_fChart3DArea;

    if (IsInZeroRange(fChart3DArea))
    {
        return false;
    }

    fScale = IsochartSqrtf(fChart2DArea/fChart3DArea);
    return true;
}

// Only optimize vertices with inifinite stretch
HRESULT CIsochartMesh::OptimizeVertexWithInfiniteStretch(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    HRESULT hr = S_OK;
    
    for (size_t dwIteration=0;
        dwIteration<optimizeInfo.dwOptTimes;
        dwIteration++)
    {
        optimizeInfo.dwInfinitStretchVertexCount = 
            CollectInfiniteVerticesInHeap(
                optimizeInfo);

        if (optimizeInfo.dwInfinitStretchVertexCount == 0)
        {
            return hr;
        }
        if (FAILED(hr = OptimizeVerticesInHeap(
                optimizeInfo)) )
        {
            return hr;
        }
    }
    return hr;
}


// Optimize all vertices
HRESULT CIsochartMesh::OptimizeAllVertex(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    HRESULT hr = S_OK;
    auto& heap = optimizeInfo.heap;
    auto pHeapItems = optimizeInfo.pHeapItems;

    float fCurrentMaxFaceStretch;
    size_t dwIteration = 0;	
    do{
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            assert(!pHeapItems[i].isItemInHeap());
            heap.insert(pHeapItems+i);
        }

        if (FAILED(hr = OptimizeVerticesInHeap(
                optimizeInfo)))
        {
            return hr;
        }

        if (!optimizeInfo.bOptLn)
        {
            fCurrentMaxFaceStretch = 0;
            for (size_t i=0; i<m_dwFaceNumber; i++)
            {
                if (optimizeInfo.pfFaceStretch[i] > fCurrentMaxFaceStretch)
                {
                    fCurrentMaxFaceStretch = optimizeInfo.pfFaceStretch[i];
                }
            }
            
            // The iteration is convergent.
            if (optimizeInfo.fPreveMaxFaceStretch-fCurrentMaxFaceStretch
                <MINIMAL_OPTIMIZE_CHANGE)
            {
                break;
            }
            
            optimizeInfo.fPreveMaxFaceStretch = fCurrentMaxFaceStretch;
        }
        dwIteration++;
    }while(dwIteration < optimizeInfo.dwOptTimes);
    return hr;
}

// Collect all vertices with infinite stretch and its
// adjacent vertices in max heap. return the count of
// vertices with infinite stretch
size_t CIsochartMesh::CollectInfiniteVerticesInHeap(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    auto& heap = optimizeInfo.heap;
    auto pHeapItems = optimizeInfo.pHeapItems;

    size_t dwBadVertexCount = 0;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        // Add vertices with infinite stretch and its
        // adjacent vertices into heap
        if (pHeapItems[i].m_weight >= optimizeInfo.fInfiniteStretch)
        {
            if (!pHeapItems[i].isItemInHeap())
            {
                heap.insert(pHeapItems+i);
            }

            ISOCHARTVERTEX* pVertex1 = m_pVerts + i;
            for (size_t j=0; j<pVertex1->vertAdjacent.size(); j++)
            {
                uint32_t dwAdjacentVertID = pVertex1->vertAdjacent[j];
                if (!pHeapItems[dwAdjacentVertID].isItemInHeap())
                {
                    heap.insert(pHeapItems+dwAdjacentVertID);
                }
            }
            dwBadVertexCount++;
        }
    }
    return dwBadVertexCount;
}

// Optimize all vertices in current max heap.
HRESULT CIsochartMesh::OptimizeVerticesInHeap(
    CHARTOPTIMIZEINFO& optimizeInfo)
{
    HRESULT hr = S_OK;

    auto& heap = optimizeInfo.heap;
    auto pHeapItems = optimizeInfo.pHeapItems;
    
    while(!heap.empty())
    {
        auto pTop = heap.cutTop();
        assert(pTop != 0);

        // If stretch is small enough, don't perform optimization.
        if (pTop->m_weight < optimizeInfo.fBarToStopOptAll)
        {
            continue;
        }
        ISOCHARTVERTEX* pVertex = m_pVerts + pTop->m_data;
        if (!optimizeInfo.bOptBoundaryVert && pVertex->bIsBoundary)
        {
            continue;
        }
        if (!optimizeInfo.bOptInternalVert && !pVertex->bIsBoundary)
        {
            continue;
        }
        
        bool bIsUpdated = false;
        FAILURE_RETURN(
            OptimizeVertexParamStretch(
                pVertex,
                optimizeInfo,
                bIsUpdated));

        if (bIsUpdated)
        {
            assert(!pHeapItems[pVertex->dwID].isItemInHeap());
            pHeapItems[pVertex->dwID].m_weight = 
                optimizeInfo.pfVertStretch[pVertex->dwID];
            
            for (size_t j=0; j<pVertex->vertAdjacent.size(); j++)
            {
                uint32_t dwAdjacentVertID = pVertex->vertAdjacent[j];
                if(pHeapItems[dwAdjacentVertID].isItemInHeap())
                {			

                    heap.update(
                        pHeapItems+dwAdjacentVertID,
                        optimizeInfo.pfVertStretch[dwAdjacentVertID]);
                }
                else
                {
                    pHeapItems[dwAdjacentVertID].m_weight =
                        optimizeInfo.pfVertStretch[dwAdjacentVertID];
                }
            }
        }
    }
    return hr;
}

HRESULT CIsochartMesh::OptimizeVertexParamStretch(
    ISOCHARTVERTEX* pOptimizeVertex,
    CHARTOPTIMIZEINFO& optimizeInfo,
    bool& bIsUpdated)
{
    bIsUpdated = false;

    size_t dwAdjacentFaceCount = pOptimizeVertex->faceAdjacent.size();

    // 1. Prepare information structure to optimize current vertex's stretch.
    VERTOPTIMIZEINFO vertInfo;
    vertInfo.pfStartFaceStretch = nullptr;
    vertInfo.pfEndFaceStretch = nullptr;
    vertInfo.pfWorkStretch = nullptr;

    // Need to allocate 3 buffers with same size, so allocate them once time.
    vertInfo.pfStartFaceStretch = new (std::nothrow) float[3 * dwAdjacentFaceCount];
    if (!vertInfo.pfStartFaceStretch)
    {
        return E_OUTOFMEMORY;
    }

    vertInfo.pfEndFaceStretch =
        vertInfo.pfStartFaceStretch + dwAdjacentFaceCount;

    vertInfo.pfWorkStretch =
        vertInfo.pfEndFaceStretch + dwAdjacentFaceCount;

    // The Start Point to optimize
    vertInfo.start = pOptimizeVertex->uv;
    vertInfo.fStartStretch =
        optimizeInfo.pfVertStretch[pOptimizeVertex->dwID];
    
    for (size_t i=0; i<dwAdjacentFaceCount; i++)
    {
        vertInfo.pfStartFaceStretch[i] =
            optimizeInfo.pfFaceStretch[pOptimizeVertex->faceAdjacent[i]];
    }

    vertInfo.pOptimizeVertex = pOptimizeVertex;


    // Prepare optimization:
    // (1) Decide the center of optimization.
    // (2) Decide the radius vertex moves around.
    // (3) Precompute stretch of some predefined position. Make the position
    // with smallest stretch as a candidate of finial position.
    if (pOptimizeVertex->bIsBoundary)
    {
        PrepareBoundaryVertOpt(
            optimizeInfo,
            vertInfo);
    }
    else
    {
        PrepareInternalVertOpt(
            optimizeInfo,
            vertInfo);
    }

    // IsInZeroRange(vertInfo.fRadius) means current vertex can not
    // leave its current position. So no need to continue optimizing.
    if (IsInZeroRange(vertInfo.fRadius))
    {
        delete []vertInfo.pfStartFaceStretch;
        bIsUpdated = false;
        return S_OK;
    }

    // 3. Move vertex around the center to find a position with minimal
    // stretch.
    bIsUpdated = 
        OptimizeVertexStretchAroundCenter(
            optimizeInfo,
            vertInfo);

    delete []vertInfo.pfStartFaceStretch;

    return S_OK;
}

// If the vertex to be optimized is a boundary vertex, following rule should meet
//  (1) should not move out of the 1-ring neigborhood
//  (2) should not move across other boundary edges.
//  (3).should not cause adjacent edge move across other boundary vertices
void CIsochartMesh::PrepareBoundaryVertOpt(
    CHARTOPTIMIZEINFO& optimizeInfo,
    VERTOPTIMIZEINFO& vertInfo)
{
    ISOCHARTVERTEX* pOptimizeVertex = vertInfo.pOptimizeVertex;
    size_t dwAdjacentVertexCount = pOptimizeVertex->vertAdjacent.size();

    ISOCHARTVERTEX* pVertex0 = nullptr;
    ISOCHARTVERTEX* pVertex1 = nullptr;

    // To a boundary vertex, use its original position as center of optimization
    vertInfo.center = pOptimizeVertex->uv;
    vertInfo.end = pOptimizeVertex->uv;
    vertInfo.fEndStretch = 
        optimizeInfo.pfVertStretch[vertInfo.pOptimizeVertex->dwID];

    vertInfo.fRadius =  FLT_MAX;

    // 1. Don't move the vertex outside the 1-ring neighborhood.
    for (size_t i=0; i<dwAdjacentVertexCount; i++)
    {
        pVertex1 = m_pVerts + pOptimizeVertex->vertAdjacent[i];
        float fLength = 
            CaculateUVDistanceSquare(pOptimizeVertex->uv, pVertex1->uv);
        if (vertInfo.fRadius > fLength)
        {
            vertInfo.fRadius = fLength;
        }
    }

    // 2. Don't move the vertex across other boundary edges.
    for (size_t i=0; i<m_edges.size(); i++)
    {
        float fLength;
        ISOCHARTEDGE& edge = m_edges[i];
        if (!edge.bIsBoundary)
        {
            continue;
        }
        if (edge.dwVertexID[0] == pOptimizeVertex->dwID
        ||edge.dwVertexID[1] == pOptimizeVertex->dwID)
        {
            continue;
        }

        pVertex0 = m_pVerts + edge.dwVertexID[0];
        pVertex1 = m_pVerts + edge.dwVertexID[1];

        fLength = IsochartVertexToEdgeDistance2D(
            pOptimizeVertex->uv,
            pVertex0->uv,
            pVertex1->uv);
        
        if (vertInfo.fRadius > fLength)
        {
            vertInfo.fRadius = fLength;
        }
    }

    // 3. Don's move the vertex and make the adjacent boundary edges move across
    // other boundary vertices.
    for (size_t i=0; i<pOptimizeVertex->edgeAdjacent.size(); i++)
    {
        float fLength;
        ISOCHARTEDGE& edge = m_edges[pOptimizeVertex->edgeAdjacent[i]];
        if (!edge.bIsBoundary)
        {
            continue;
        }

        pVertex0 = m_pVerts + edge.dwVertexID[0];
        pVertex1 = m_pVerts + edge.dwVertexID[1];

        for (size_t j=0; j<m_dwVertNumber; j++)
        {
            ISOCHARTVERTEX* pVertex2 = m_pVerts + j;
            if (!pVertex2->bIsBoundary)
            {
                continue;
            }
            if ( j==edge.dwVertexID[0] || j==edge.dwVertexID[1])
            {
                continue;
            }

            fLength = IsochartVertexToEdgeDistance2D(
                pVertex2->uv,
                pVertex0->uv,
                pVertex1->uv);

            if (vertInfo.fRadius > fLength)
            {
                vertInfo.fRadius = fLength;
            }
        }
    }

    // Multiply a float value less than 1.0 to avoid the case that vertex
    // moves too close to boundary
    vertInfo.fRadius =
        IsochartSqrtf(vertInfo.fRadius) * CONSERVATIVE_OPTIMIZE_FACTOR;

    return;
}

// If the vertex to be optimized is a internal vertex, following rule should meet
// (1)should not move out of the 1-ring neigborhood 
void CIsochartMesh::PrepareInternalVertOpt(
    CHARTOPTIMIZEINFO& optimizeInfo,
    VERTOPTIMIZEINFO& vertInfo)
{
    ISOCHARTVERTEX* pOptimizeVertex = vertInfo.pOptimizeVertex;

    size_t dwAdjacentFaceCount = pOptimizeVertex->faceAdjacent.size();
    size_t dwAdjacentVertexCount = pOptimizeVertex->vertAdjacent.size();

    assert(dwAdjacentVertexCount > 0);

    // 1. Calculate the center position
    ISOCHARTVERTEX* pVertex1 = nullptr;
    vertInfo.end.x = vertInfo.end.y = 0;
    for (size_t i=0; i<dwAdjacentVertexCount; i++)
    {
        pVertex1 = m_pVerts + pOptimizeVertex->vertAdjacent[i];
        vertInfo.end.x += pVertex1->uv.x;
        vertInfo.end.y += pVertex1->uv.y;
    }
    vertInfo.center.x = vertInfo.end.x / dwAdjacentVertexCount;
    vertInfo.center.y = vertInfo.end.y / dwAdjacentVertexCount;
    vertInfo.end = vertInfo.center;

    TryAdjustVertexParamStretch(
        pOptimizeVertex,
        optimizeInfo.bOptLn,
        optimizeInfo.bOptSignal,
        optimizeInfo.fStretchScale,
        vertInfo.end,
        vertInfo.fEndStretch,
        vertInfo.pfEndFaceStretch);

    // 2. Precomputing some stretch of position around center.
    // assign the position with smallest stretch to vertInfo.end,
    // this position is a candidate of finial position
    XMFLOAT2 middle;
    for (size_t i=0; i<dwAdjacentVertexCount; i++)
    {
        pVertex1 = m_pVerts + pOptimizeVertex->vertAdjacent[i];
        XMStoreFloat2(&middle,
            XMLoadFloat2(&pVertex1->uv) * CONSERVATIVE_OPTIMIZE_FACTOR
            + XMLoadFloat2(&vertInfo.center) * (1 - CONSERVATIVE_OPTIMIZE_FACTOR));

        float fTempStretch = 0;
        TryAdjustVertexParamStretch(
            pOptimizeVertex,
            optimizeInfo.bOptLn,
            optimizeInfo.bOptSignal,
            optimizeInfo.fStretchScale,
            middle,
            fTempStretch,
            vertInfo.pfWorkStretch);

        if (fTempStretch < vertInfo.fEndStretch)
        {
            vertInfo.fEndStretch = fTempStretch;
            memcpy(
                vertInfo.pfEndFaceStretch,
                vertInfo.pfWorkStretch,
                sizeof(float)*dwAdjacentFaceCount);
            vertInfo.end = middle;
        }
    }

    // 3. Decide the radius of a circle, vertex must moves in the
    // circle.
    // Don't move current vertex out of 1-ring neighborhood.
    vertInfo.fRadius = FLT_MAX;
    for (size_t i=0; i<dwAdjacentVertexCount; i++)
    {
        pVertex1 = m_pVerts + pOptimizeVertex->vertAdjacent[i];
        float fTemp =
            CaculateUVDistanceSquare(pVertex1->uv, vertInfo.center);
        if (fTemp < vertInfo.fRadius)
        {
            vertInfo.fRadius = fTemp;
        }
    }

    vertInfo.fRadius = 
        IsochartSqrtf(vertInfo.fRadius) * CONSERVATIVE_OPTIMIZE_FACTOR;
}

// Move vertex randomly in a precomputed circle to find a position 
// with smallest vertex stretch.
bool CIsochartMesh::OptimizeVertexStretchAroundCenter(
    CHARTOPTIMIZEINFO& optimizeInfo,
    VERTOPTIMIZEINFO& vertInfo)
{
    ISOCHARTVERTEX* pOptimizeVertex = vertInfo.pOptimizeVertex;

    XMFLOAT2 originalStart = vertInfo.start;
    float fOriginalStartStretch = vertInfo.fStartStretch;

    XMFLOAT2 originalEnd = vertInfo.end;
    float fOriginalEndStretch = vertInfo.fEndStretch;
    
    float fToleranceLength 
        = optimizeInfo.fAverageEdgeLength*optimizeInfo.fAverageEdgeLength
        *optimizeInfo.fTolerance*optimizeInfo.fTolerance;

    float fTempStretch = 0;
    XMFLOAT2 middle;
    // As the decription in [SSGH01], randomly moving vertex will have more
    // chance to find the optimal position. To make consistent results, srand
    // with a specified value 2
    srand(2); 
    size_t iteration = 0;
    while (iteration < optimizeInfo.dwRandOptOneVertTimes)
    {
        // 1. Get a new random position in the optimizing circle range
        float fAngle = rand() * 2.f * XM_PI / RAND_MAX;
        vertInfo.end.x = 
            vertInfo.center.x + vertInfo.fRadius * cosf(fAngle);
        vertInfo.end.y = 
            vertInfo.center.y + vertInfo.fRadius * sinf(fAngle);

        // 2. When optimizing an boundary vertex during sigal-specified 
        // parameterizing, must gurantee the vertex didn't move outside
        // of chart bounding box.
        if (pOptimizeVertex->bIsBoundary && optimizeInfo.bUseBoundingBox)
        {
            LimitVertexToBoundingBox(
                vertInfo.end,
                optimizeInfo.minBound,
                optimizeInfo.maxBound,
                vertInfo.end);
        }

        // 3. Move vertex to the new position, and caculate new vertex stretch
        TryAdjustVertexParamStretch(
            pOptimizeVertex,
            optimizeInfo.bOptLn,
            optimizeInfo.bOptSignal,
            optimizeInfo.fStretchScale,			
            vertInfo.end,
            vertInfo.fEndStretch,
            vertInfo.pfEndFaceStretch);

        float fDiffernece =
                CaculateUVDistanceSquare(vertInfo.start, vertInfo.end);

        // 4. Bisearch the position along the segment between center and end.
        // get the position with smallest vertex stretch
        float fPrevDiff = fDiffernece;
        while(fDiffernece > fToleranceLength)
        {
            middle.x = (vertInfo.start.x + vertInfo.end.x) / 2;
            middle.y = (vertInfo.start.y + vertInfo.end.y) / 2;

            TryAdjustVertexParamStretch(
                pOptimizeVertex,				
                optimizeInfo.bOptLn,
                optimizeInfo.bOptSignal,
                optimizeInfo.fStretchScale,
                middle,
                fTempStretch,
                vertInfo.pfWorkStretch);

            // When Optimize bounday vertex signal stretch, if the L2 squared Stretch is 0,
            // this mean's no signal change on faces around the vertex, we can decrease their
            // 2D area.
            if (vertInfo.fStartStretch == vertInfo.fEndStretch && 								
                pOptimizeVertex->bIsBoundary &&
                optimizeInfo.bOptSignal &&
                IsInZeroRange(vertInfo.fEndStretch))
            {
                float fStatArea = GetFaceAreaAroundVertex(
                    pOptimizeVertex, vertInfo.start);
                float fEndArea = GetFaceAreaAroundVertex(
                    pOptimizeVertex, vertInfo.end);
                if (fStatArea < fEndArea)
                {
                    vertInfo.fEndStretch = fTempStretch;
                    vertInfo.end = middle;
                }
                else
                {
                    vertInfo.fStartStretch = fTempStretch;
                    vertInfo.start = middle;
                }
            }
            else if (vertInfo.fStartStretch < vertInfo.fEndStretch)
            {
                vertInfo.fEndStretch = fTempStretch;
                vertInfo.end = middle;
            }
            else
            {
                vertInfo.fStartStretch = fTempStretch;
                vertInfo.start = middle;
            }
                        
            fDiffernece =
                CaculateUVDistanceSquare(vertInfo.start, vertInfo.end);
            if (IsInZeroRange2(fPrevDiff - fDiffernece) || fPrevDiff < fDiffernece)
            {
                break;
            }
            fPrevDiff = fDiffernece;
        }

        if (vertInfo.fStartStretch == vertInfo.fEndStretch &&
            pOptimizeVertex->bIsBoundary &&
            optimizeInfo.bOptSignal &&
            IsInZeroRange(vertInfo.fEndStretch))
        {
            float fStatArea = GetFaceAreaAroundVertex(
                pOptimizeVertex, vertInfo.start);
            float fEndArea = GetFaceAreaAroundVertex(
                pOptimizeVertex, vertInfo.end);

            if (fStatArea > fEndArea)
            {
                vertInfo.start = vertInfo.end;
                vertInfo.fStartStretch = vertInfo.fEndStretch;
            }

        }
        else if (vertInfo.fStartStretch > vertInfo.fEndStretch)
        {
            vertInfo.start = vertInfo.end;
            vertInfo.fStartStretch = vertInfo.fEndStretch;
        }
        else {}

        iteration++;
    }

    if (vertInfo.fStartStretch == vertInfo.fEndStretch &&
        pOptimizeVertex->bIsBoundary &&
        optimizeInfo.bOptSignal &&
        IsInZeroRange(vertInfo.fEndStretch))
    {
        vertInfo.fEndStretch = vertInfo.fStartStretch;
        vertInfo.end = vertInfo.start;

        float fOldArea = GetFaceAreaAroundVertex(
            pOptimizeVertex, pOptimizeVertex->uv);
        float fNewArea = GetFaceAreaAroundVertex(
            pOptimizeVertex, vertInfo.end);

        if (fOldArea >fNewArea)
        {
            TryAdjustVertexParamStretch(
                pOptimizeVertex,
                optimizeInfo.bOptLn,
                optimizeInfo.bOptSignal,
                optimizeInfo.fStretchScale,
                vertInfo.end,
                vertInfo.fEndStretch,
                vertInfo.pfEndFaceStretch);

            UpdateOptimizeResult(
                optimizeInfo,
                pOptimizeVertex,
                vertInfo.end,
                vertInfo.fEndStretch,
                vertInfo.pfEndFaceStretch);
            return true;

        }
        else
        {
            return false;
        }

    }

    // If Precomputed candidate position is better, use precomputed one.
    if (vertInfo.fStartStretch >= fOriginalEndStretch)
    {
        vertInfo.fEndStretch = fOriginalEndStretch;
        vertInfo.end = originalEnd;
    }
    else
    {
        vertInfo.fEndStretch = vertInfo.fStartStretch;
        vertInfo.end = vertInfo.start;
    }
    // Update vertex adjacent faces' stretch
    if (vertInfo.fEndStretch < INFINITE_STRETCH && 
        vertInfo.fEndStretch < fOriginalStartStretch)
    {
        TryAdjustVertexParamStretch(
            pOptimizeVertex,
            optimizeInfo.bOptLn,
            optimizeInfo.bOptSignal,
            optimizeInfo.fStretchScale,			
            vertInfo.end,
            vertInfo.fEndStretch,
            vertInfo.pfEndFaceStretch);

        UpdateOptimizeResult(
            optimizeInfo,
            pOptimizeVertex,
            vertInfo.end,
            vertInfo.fEndStretch,
            vertInfo.pfEndFaceStretch);
        return true;
    }
    else
    {
        return false;
    }
}

// but faces area deceased, it's also a better parameterization.
float CIsochartMesh::GetFaceAreaAroundVertex(
    const ISOCHARTVERTEX* pOptimizeVertex,
    XMFLOAT2& newUV) const
{
    float fTotalFaceArea = 0;

    for (size_t j=0; j<pOptimizeVertex->faceAdjacent.size(); j++)	
    {
        ISOCHARTFACE* pFace = m_pFaces + pOptimizeVertex->faceAdjacent[j];
    
        // Calculate face Ln stretch using new UV-coordinates.
        if (pFace->dwVertexID[0] == pOptimizeVertex->dwID)
        {
            fTotalFaceArea+= Cal2DTriangleArea(
                &newUV,
                &(m_pVerts[pFace->dwVertexID[1]].uv),
                &(m_pVerts[pFace->dwVertexID[2]].uv));
        
        }
        else if (pFace->dwVertexID[1] == pOptimizeVertex->dwID)
        {
            fTotalFaceArea += Cal2DTriangleArea(
                &(m_pVerts[pFace->dwVertexID[0]].uv),
                &(newUV),
                &(m_pVerts[pFace->dwVertexID[2]].uv));

        }
        else
        {
            fTotalFaceArea += Cal2DTriangleArea(
                &(m_pVerts[pFace->dwVertexID[0]].uv),
                &(m_pVerts[pFace->dwVertexID[1]].uv),
                &(newUV));
        }
    }

    return fTotalFaceArea;
}

float CIsochartMesh::CalcuateAdjustedVertexStretch(
    bool bOptLn,
    const ISOCHARTVERTEX* pVertex,
    const float* pfAdjFaceStretch) const
{
    float fVertStretch = 0;
    if (!bOptLn)
    {
        for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
        {
            if (pfAdjFaceStretch[j] == INFINITE_STRETCH)
            {
                fVertStretch = INFINITE_STRETCH;
                break;
            }
            
            fVertStretch += pfAdjFaceStretch[j];
        }
    }
    else
    {
        for (size_t j=0; j<pVertex->faceAdjacent.size(); j++)
        {
            if (fVertStretch < pfAdjFaceStretch[j])
            {
                fVertStretch = pfAdjFaceStretch[j];
            }
        }
    }
    return fVertStretch;
}


// confine vertex in the chart bounding box
void CIsochartMesh::LimitVertexToBoundingBox(
    const XMFLOAT2& end,
    const XMFLOAT2& minBound,
    const XMFLOAT2& maxBound,
    XMFLOAT2& result)
{
    result.x = std::min(maxBound.x, end.x);
    result.x = std::max(minBound.x, end.x);

    result.y = std::min(maxBound.y, end.y);
    result.y = std::max(minBound.y, end.y);
}

// Update the stretch of optimized vertex and
// its adjacent faces.
void CIsochartMesh::UpdateOptimizeResult(
    CHARTOPTIMIZEINFO& optimizeInfo,
    ISOCHARTVERTEX* pOptimizeVertex,
    XMFLOAT2& vertexNewCoordinate,
    float fNewVertexStretch,
    float* fAdjacentFaceNewStretch)
{
    size_t dwAdjacentFaceCount = pOptimizeVertex->faceAdjacent.size();
    size_t dwAdjacentVertexCount = pOptimizeVertex->vertAdjacent.size();

    // 1. Update the optimized vertex.
    optimizeInfo.pfVertStretch[pOptimizeVertex->dwID] = fNewVertexStretch;
    pOptimizeVertex->uv = vertexNewCoordinate;

    // 2. Update the adjacent faces' stretch
    for (size_t i=0; i<dwAdjacentFaceCount; i++)
    {
        uint32_t dwAdjacentFaceID = pOptimizeVertex->faceAdjacent[i];
        optimizeInfo.pfFaceStretch[dwAdjacentFaceID]
            = fAdjacentFaceNewStretch[i];
    }

    // 3. Update adjacent vertices' stretch.
    ISOCHARTVERTEX* pVertex1;
    for (size_t i=0; i<dwAdjacentVertexCount; i++)
    {
        pVertex1 = m_pVerts + pOptimizeVertex->vertAdjacent[i];
        optimizeInfo.pfVertStretch[pVertex1->dwID] = 
            CalculateVertexStretch(
                optimizeInfo.bOptLn,
                pVertex1, 
                optimizeInfo.pfFaceStretch);
    }
    
}

// Using the expression given by [SSGH01]
void CIsochartMesh::TryAdjustVertexParamStretch(
    ISOCHARTVERTEX* pOptimizeVertex,
    bool bOptLn,	
    bool bOptSignal,	
    float fStretchScale,
    XMFLOAT2& newUV,
    float& fStretch,
    float* pfFaceStretch) const
{
    fStretch = 0;

    float f2D;
    float fGeoM[3]; // fGeoM[0] = Ss*Ss, fGeoM[1] = Ss*St, fGeoM[2] = St*St
    for (size_t i=0; i<pOptimizeVertex->faceAdjacent.size(); i++)
    {
        ISOCHARTFACE* pFace = m_pFaces + pOptimizeVertex->faceAdjacent[i];

        // Calculate face Ln stretch using new UV-coordinates.
        if (pFace->dwVertexID[0] == pOptimizeVertex->dwID)
        {
            pfFaceStretch[i] = CalFaceSquraedStretch(
                bOptLn,
                bOptSignal,		
                pFace,
                newUV,
                m_pVerts[pFace->dwVertexID[1]].uv,
                m_pVerts[pFace->dwVertexID[2]].uv,
                fStretchScale,
                f2D,
                fGeoM);
        }
        else if (pFace->dwVertexID[1] == pOptimizeVertex->dwID)
        {
            pfFaceStretch[i] = CalFaceSquraedStretch(
                bOptLn,
                bOptSignal,		
                pFace,
                m_pVerts[pFace->dwVertexID[0]].uv,
                newUV,
                m_pVerts[pFace->dwVertexID[2]].uv,
                fStretchScale,
                f2D,
                fGeoM);
        }
        else
        {
            pfFaceStretch[i] = CalFaceSquraedStretch(
                bOptLn,
                bOptSignal,		
                pFace,
                m_pVerts[pFace->dwVertexID[0]].uv,
                m_pVerts[pFace->dwVertexID[1]].uv,
                newUV,
                fStretchScale,
                f2D,
                fGeoM);
        }

        float f3DArea = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
        if (!bOptLn && 
            bOptSignal && 
            !IsInZeroRange2(f3DArea))
        {
            if (f2D < 0 
                || fGeoM[0] == INFINITE_STRETCH
                || fGeoM[2] == INFINITE_STRETCH)
            {
                fStretch = INFINITE_STRETCH;
            }
            
            if (fGeoM[0] + fGeoM[2] > 
                m_baseInfo.fExpectAvgL2SquaredStretch * 2)
            {
                fStretch = INFINITE_STRETCH;
            }

            if (fGeoM[0] + fGeoM[2] < 
                m_baseInfo.fExpectMinAvgL2SquaredStretch * 2)
            {
                fStretch = INFINITE_STRETCH;	
            }			
        }
    }

    if (fStretch == INFINITE_STRETCH)
    {
        return;
    }

    fStretch = CalcuateAdjustedVertexStretch(
        bOptLn, 
        pOptimizeVertex, 
        pfFaceStretch);
}

void CIsochartMesh::ParameterizeOneFace(
    bool bForSignal,
    ISOCHARTFACE* pFace)
{
    if (bForSignal)
    {
        float fMatrix[4];
        m_fParamStretchL2 = CalL2SquaredStretchLowBoundOnFace(
            m_baseInfo.pfIMTArray[pFace->dwIDInRootMesh], 
            m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh],
            FACE_MAX_SCALE_FACTOR,
            fMatrix);

        
        XMFLOAT2* p = 
            m_baseInfo.pFaceCanonicalUVCoordinate+m_pFaces->dwIDInRootMesh*3;

        TransformUV(m_pVerts[m_pFaces->dwVertexID[0]].uv, p[0], fMatrix);
        TransformUV(m_pVerts[m_pFaces->dwVertexID[1]].uv, p[1], fMatrix);
        TransformUV(m_pVerts[m_pFaces->dwVertexID[2]].uv, p[2], fMatrix);
        

        float fNew2DArea = Cal2DTriangleArea(
            m_pVerts[m_pFaces->dwVertexID[0]].uv,
            m_pVerts[m_pFaces->dwVertexID[1]].uv,
            m_pVerts[m_pFaces->dwVertexID[2]].uv);

        // float fOld2DArea = Cal2DTriangleArea( p[0], p[1],  p[2]);

#ifdef _DEBUG
        float fNewStretch =
#endif
        CalFaceSigL2SquraedStretch(
            pFace, 
            m_pVerts[m_pFaces->dwVertexID[0]].uv, 
            m_pVerts[m_pFaces->dwVertexID[1]].uv, 
            m_pVerts[m_pFaces->dwVertexID[2]].uv,
            fNew2DArea);

        DPF(1, "New Area %f", fNew2DArea);
        DPF(3, "Theory Stretch %f, New Stretch %f", m_fParamStretchL2, fNewStretch);		
    }
    else
    {
        XMFLOAT3 axis[2];
        IsochartCaculateCanonicalCoordinates(
            m_baseInfo.pVertPosition + m_pVerts[m_pFaces->dwVertexID[0]].dwIDInRootMesh,
            m_baseInfo.pVertPosition + m_pVerts[m_pFaces->dwVertexID[1]].dwIDInRootMesh,
            m_baseInfo.pVertPosition + m_pVerts[m_pFaces->dwVertexID[2]].dwIDInRootMesh,
            &(m_pVerts[m_pFaces->dwVertexID[0]].uv),
            &(m_pVerts[m_pFaces->dwVertexID[1]].uv),
            &(m_pVerts[m_pFaces->dwVertexID[2]].uv),
            axis);
        m_fParamStretchL2 = m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
    }
    m_fChart2DArea = m_fChart3DArea;
    m_bIsParameterized  = true;
}
