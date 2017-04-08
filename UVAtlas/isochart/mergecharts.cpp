//-------------------------------------------------------------------------------------
// UVAtlas - mergecharts.cpp
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
#include "isochartmesh.h"
#include "maxheap.hpp"

using namespace Isochart;
using namespace DirectX;

namespace
{
    const size_t MAX_FACE_NUMBER = 0xfffffffe;
};


//-------------------------------------------------------------------------------------
// Try to merge small charts
HRESULT CIsochartMesh::MergeSmallCharts(
    ISOCHARTMESH_ARRAY &chartList,
    size_t dwExpectChartCount,
    const CBaseMeshInfo& baseInfo,
    CCallbackSchemer& callbackSchemer)
{
    DPF(1, "#<Chart Number Before Merge> : %Iu", chartList.size());
    if (chartList.size() < 4)
    {
        return S_OK;
    }
    HRESULT hr = S_OK;

    size_t dwFaceNumber = baseInfo.dwFaceCount;

    // 1. Sort the sub-charts by face number in ascending order
    ISOCHARTMESH_ARRAY children;
    try
    {
        children.insert(children.end(), chartList.cbegin(), chartList.cend());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    SortChartsByFaceNumber(children);

    // 2. For each sub-chart, search the sub-charts adjacent to it.
    FAILURE_RETURN(CalAdjacentChartsForEachChart(
        children, baseInfo.pdwOriginalFaceAdjacentArray, dwFaceNumber));

    FAILURE_RETURN(callbackSchemer.UpdateCallbackAdapt(1));

    // 3. Merge charts that can be merged together.
    chartList.clear();
    hr = PerformMerging(
        children,
        dwExpectChartCount,
        dwFaceNumber,
        callbackSchemer);
    if (FAILED(hr))
    {
        ReleaseAllNewCharts(children);
        return hr;
    }

    size_t dwNewChartCount = 0;
    for (size_t i=0; i < children.size(); i++)
    {
        if (children[i])
        {
            dwNewChartCount++;
        }
    }

    try
    {
        chartList.resize(dwNewChartCount);
    }
    catch (...)
    {
        ReleaseAllNewCharts(children);
        return E_OUTOFMEMORY;
    }

    size_t j = 0;
    for (size_t i=0; i<children.size(); i++)
    {
        if(children[i])
        {
            chartList[j] = children[i];
            j++;
        }
    }

    DPF(1,"#<Chart Number after Merge> : %Iu", chartList.size());
    return hr;
}


//-------------------------------------------------------------------------------------
// Delete all temporary charts, this function is called only 
// when some fatal errors happen
void CIsochartMesh::ReleaseAllNewCharts(
    ISOCHARTMESH_ARRAY& children)
{
    for (size_t i=0; i<children.size(); i++)
    {
        auto* pChart = children[i];
        if ( pChart && !pChart->IsInitChart() )
        {
            delete pChart;
        }
    }
    children.clear();
}


//-------------------------------------------------------------------------------------
// Sort charts by face number
void CIsochartMesh::SortChartsByFaceNumber(
    ISOCHARTMESH_ARRAY& children)
{
    for (size_t i=0; i<children.size()-1; i++)
    {
        for (size_t j = i + 1; j<children.size(); j++)
        {
            if (children[i]->GetFaceNumber() >
                children[j]->GetFaceNumber())
            {
                CIsochartMesh* pChart = children[i];
                children[i] = children[j];
                children[j] = pChart;
            }
        }
    }
}


//-------------------------------------------------------------------------------------
// For each chart, caculated adjacent charts
HRESULT
CIsochartMesh::CalAdjacentChartsForEachChart(
    ISOCHARTMESH_ARRAY& children,
    const uint32_t* pdwFaceAdjacentArray,
    size_t dwFaceNumber)
{
    std::unique_ptr<uint32_t []> pdwFaceChartId(new (std::nothrow) uint32_t[dwFaceNumber]);
    if (!pdwFaceChartId)
    {
        return E_OUTOFMEMORY;
    }

    for (uint32_t i=0; i<children.size(); i++)
    {
        ISOCHARTFACE* pFace = children[i]->GetFaceBuffer();
        for ( size_t j=0; j<children[i]->GetFaceNumber(); j++)
        {
            pdwFaceChartId[pFace->dwIDInRootMesh] = i;
            pFace++;
        }
    }

    for (uint32_t i = 0; i<children.size(); i++)
    {
        HRESULT hr = children[i]->CalculateAdjacentChart(
            i,
            pdwFaceChartId.get(),
            pdwFaceAdjacentArray);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::PerformMerging(
    ISOCHARTMESH_ARRAY& children,
    size_t dwExpectChartCount,
    size_t dwFaceNumber,
    CCallbackSchemer& callbackSchemer)
{
    HRESULT hr = S_OK;
    CMaxHeap<uint32_t, uint32_t> heap;

    size_t dwMaxMergeTimes = MAX_FACE_NUMBER;
    if (dwExpectChartCount != 0 && dwExpectChartCount < children.size())
    {
        dwMaxMergeTimes = children.size() - dwExpectChartCount;
    }
    
    size_t nchildren = children.size();

    if (!heap.resize(nchildren))
    {
        return E_OUTOFMEMORY;
    }

    std::unique_ptr<CMaxHeapItem<uint32_t, uint32_t> []> pHeapItems(new (std::nothrow) CMaxHeapItem<uint32_t, uint32_t>[nchildren]);
    std::unique_ptr<XMFLOAT3[]> pChartNormal( new (std::nothrow) XMFLOAT3[nchildren] );
    std::unique_ptr<bool[]> pbMergeFlag( new (std::nothrow) bool[nchildren] );
    if (!pHeapItems || !pChartNormal || !pbMergeFlag)
    {
        return E_OUTOFMEMORY;
    }

    CIsochartMesh *pChart = nullptr;
    // 1 Prepare all charts to be merged.
    for (uint32_t i = 0; i<nchildren; i++)
    {
        pChart = children[i];
        pChart->CalculateAveragNormal(pChartNormal.get()+i);
        if (pChart->GetAdjacentChartList().empty())
        {
            continue;
        }
        if (pChart->GetChart3DArea() == 0)
        {
            continue;
        }
        
        pHeapItems[i].m_weight = static_cast<uint32_t>( MAX_FACE_NUMBER - pChart->GetFaceNumber() );
        pHeapItems[i].m_data = i;
        heap.insert(pHeapItems.get() + i);
    }
    memset(pbMergeFlag.get(), 1, sizeof(bool)*children.size());

    FAILURE_RETURN(callbackSchemer.UpdateCallbackAdapt(1));

    size_t dwReservedCharts = heap.size();
    size_t dwLastReservedCharts = dwReservedCharts;

    // 2. Begin from the charts with less faces try to merage them to the
    // adjacent charts
    while(!heap.empty())
    {
        size_t dwDoneWork = dwLastReservedCharts - heap.size();
        if (0 == dwDoneWork)
        {
            if(FAILED(hr =  callbackSchemer.CheckPointAdapt()))
            {
                return hr;
            }
        }
        else
        {
            dwLastReservedCharts = heap.size();
            if (FAILED(hr = callbackSchemer.UpdateCallbackAdapt(dwDoneWork)))
            {
                return hr;
            }
        }

        CMaxHeapItem<uint32_t, uint32_t>*pTop = heap.cutTop();
        assert (pTop != 0);

        uint32_t index = pTop->m_data;
        assert(pTop == pHeapItems.get()+ index);

        if (!children[index])
        {
            continue;
        }

        bool bMerged = false;
        if (FAILED(hr = 
            MergeAdjacentChart(
                children, 
                index,
                dwFaceNumber,
                pbMergeFlag.get(),
                pChartNormal.get(),
                bMerged)))
        {
            return hr;
        }

        if (bMerged)
        {
            pTop->m_weight = static_cast<uint32_t>( MAX_FACE_NUMBER - children[index]->GetFaceNumber() );
            heap.insert(pTop);
            dwMaxMergeTimes--;
            if (dwMaxMergeTimes == 0)
            {
                break;
            }
        }
    }

    return hr;
}


//-------------------------------------------------------------------------------------
// For a special chart, try to merge it to the adjacent charts.
HRESULT CIsochartMesh::MergeAdjacentChart(
    ISOCHARTMESH_ARRAY& children,
    uint32_t dwMainChartID,
    size_t dwTotalFaceNumber,
    bool* pbMergeFlag,
    XMFLOAT3* pChartNormal,
    bool& bMerged)
{
    HRESULT hr = S_OK;
    bMerged = false;

    CIsochartMesh* pMainChart = children[dwMainChartID];

    auto& adjacentChartList = pMainChart->m_adjacentChart;
    size_t dwAdjacentChartNumber = adjacentChartList.size();
    if (dwAdjacentChartNumber == 0)
    {
        return hr;
    }

    // 1. Sort adjacent sub-charts according to the average normal 
    // alwasy try to merge charts having approximate normals firstly
    for (size_t i=0; i<dwAdjacentChartNumber-1; i++)
    {
        if (!children[adjacentChartList[i]])
        {
            continue;
        }
        for (size_t j=i+1; j<dwAdjacentChartNumber; j++)
        {
            if (!children[adjacentChartList[j]])
            {
                continue;
            }

            float fTemp1, fTemp2;
            fTemp1 = XMVectorGetX(XMVector3Dot(
                XMLoadFloat3(pChartNormal + dwMainChartID), XMLoadFloat3(pChartNormal + adjacentChartList[i])));

            fTemp2 = XMVectorGetX(XMVector3Dot(
                XMLoadFloat3(pChartNormal + dwMainChartID), XMLoadFloat3(pChartNormal + adjacentChartList[j])));

            if (fTemp1 < fTemp2)
            {
                uint32_t dwTemp = adjacentChartList[i];
                adjacentChartList[i] = adjacentChartList[j];
                adjacentChartList[j] = dwTemp;
            }
        }
    }

    // 2 . Try to merge current chart to its adjacent charts.
    uint32_t dwAdditionalChartID = INVALID_INDEX;

    CIsochartMesh* pMergedChart = nullptr;
    CIsochartMesh* pAddjacentChart = nullptr;
    size_t dwMaxFaceNumAfterMerging
        = std::max<size_t>(size_t(dwTotalFaceNumber * MAX_MERGE_RATIO),
        size_t(MAX_MERGE_FACE_NUMBER));

    for (size_t i=0; i<dwAdjacentChartNumber; i++)
    {
        uint32_t dwAdjacentChartID = adjacentChartList[i];

        // 2.1. Don't try merage this chart, if its has failed to merage other charts
        if (!pbMergeFlag[dwAdjacentChartID])
        {
            continue;
        }

        pAddjacentChart = children[dwAdjacentChartID];
        if (!pAddjacentChart)
        {
            continue;
        }
        if (0 == pAddjacentChart->GetChart3DArea())
        {
            continue;
        }

        // 2.2. Don't try to get a very large chart
        size_t dwMergedFaceNumber = pMainChart->GetFaceNumber() + pAddjacentChart->GetFaceNumber();
        if (dwMergedFaceNumber > dwMaxFaceNumAfterMerging)
        {
            continue;
        }

        // 2.3.  try to merge.
        FAILURE_RETURN(
            TryMergeChart(children, pMainChart, pAddjacentChart, &pMergedChart));
        if (!pMergedChart)
        {
            continue;
        }

        // 2.4 try to get right initial parameterization
        bool bParameterSucceed = false;
        if (FAILED(hr = pMergedChart->TryParameterize(bParameterSucceed)))
        {
            delete pMergedChart;
            pMergedChart = nullptr;
            return hr;
        }
        if (!bParameterSucceed)
        {
            delete pMergedChart;
            pMergedChart = nullptr;
            continue;
        }

        // 2.5 Check if the meraged chart also satisfied the stretch
        bool bCanMerge = true;
        if (FAILED(hr = CheckMerageResult(
            children,
            pMainChart,
            pAddjacentChart,
            pMergedChart,
            bCanMerge)))
        {
            delete pMergedChart;
            pMergedChart = nullptr;
            continue;
        }
        if (bCanMerge)
        {
            dwAdditionalChartID = dwAdjacentChartID;
            break;
        }
        else
        {
            delete pMergedChart;
            pMergedChart = nullptr;
        }
    }

    if (!pMergedChart)
    {
        pbMergeFlag[dwMainChartID] = false;
        bMerged = false;
        return S_OK;
    }

    // 3. Adjust the adjacence of merged charts and other charts
    for (size_t i=0; i<pMergedChart->m_adjacentChart.size(); i++)
    {
        pAddjacentChart = children[pMergedChart->m_adjacentChart[i]];
        if (!pAddjacentChart)
        {
            continue;
        }
        removeItem(pAddjacentChart->m_adjacentChart, dwAdditionalChartID);
        if (!addNoduplicateItem(pAddjacentChart->m_adjacentChart,
            dwMainChartID))
        {
            delete pMergedChart;
            return E_OUTOFMEMORY;
        }
    }

    // Delete the two sub-charts that joined the merging.
    if ( children[dwAdditionalChartID]->m_bIsInitChart )
        children[dwAdditionalChartID] = nullptr;
    else
    {
        delete children[dwAdditionalChartID];
        children[dwAdditionalChartID] = nullptr;
    }

    if ( !pMainChart->m_bIsInitChart )
        delete pMainChart;

    // Assign merged chart to main chart, caculate the normal of the new chart.
    children[dwMainChartID] = pMergedChart;
    pMergedChart->CalculateAveragNormal(pChartNormal + dwMainChartID);
    bMerged = true;

    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::CheckMerageResult(
    ISOCHARTMESH_ARRAY &chartList,
    CIsochartMesh* pOldChart1,
    CIsochartMesh* pOldChart2,
    CIsochartMesh* pNewChart,
    bool& bCanMerge)
{
    assert(chartList.size() > 1);
    HRESULT hr = S_OK;

    if (FAILED(hr = pNewChart->OptimizeChartL2Stretch(false)))
    {
        delete pNewChart;
        return hr;
    }

    ISOCHARTMESH_ARRAY tempChartList;
    try
    {
        for (size_t ii = 0; ii < chartList.size(); ii++)
        {
            if (chartList[ii] != pOldChart1
                && chartList[ii] != pOldChart2
                && chartList[ii])
            {
                tempChartList.push_back(chartList[ii]);
            }
        }

        tempChartList.push_back(pNewChart);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    float fMergedAvgStretch = CalOptimalAvgL2SquaredStretch(tempChartList);
    bCanMerge = 
        IsReachExpectedTotalAvgL2SqrStretch(
            fMergedAvgStretch,
            tempChartList[0]->m_baseInfo.fExpectAvgL2SquaredStretch);

    return S_OK;
}


//-------------------------------------------------------------------------------------
// For the chart generated by merge 2 charts, recaculate the parameterization 
HRESULT CIsochartMesh::CalculateIsoParameterization()
{
    if (m_bIsParameterized)
    {
        return S_OK;
    }

    HRESULT hr = S_OK;
    size_t dwLandmarkNumber = 0;
    size_t dwCalculatedDimension = 0;

    // 1. Calculate the landmark vertices
    FAILURE_RETURN(
        CalculateLandmarkVertices(MIN_LANDMARK_NUMBER, dwLandmarkNumber));

    // 2. Calculate the distance matrix of landmark vertices

    std::unique_ptr<float[]> vertGeodesicDistance( new (std::nothrow) float[dwLandmarkNumber * m_dwVertNumber] );
    std::unique_ptr<float[]> geodesicMatrix( new (std::nothrow) float[dwLandmarkNumber * dwLandmarkNumber] );

    if (!vertGeodesicDistance || !geodesicMatrix)
    {
        return E_OUTOFMEMORY;
    }

    float* pfVertGeodesicDistance = vertGeodesicDistance.get();
    float* pfGeodesicMatrix = geodesicMatrix.get();

    #if USING_COMBINED_DISTANCE_TO_PARAMETERIZE

    if (IsIMTSpecified())
    {
        hr = CalculateGeodesicDistance(
            m_landmarkVerts, pfVertGeodesicDistance, nullptr);
    }
    else
    {
        hr = CalculateGeodesicDistance(
            m_landmarkVerts, nullptr, pfVertGeodesicDistance);	
    }
    
    #else

    hr = CalculateGeodesicDistance(
        m_landmarkVerts, nullptr, pfVertGeodesicDistance);

    #endif

    CalculateGeodesicMatrix(
        m_landmarkVerts, pfVertGeodesicDistance, pfGeodesicMatrix);
    
    // 3. Perform Isomap to decrease dimension
    if (FAILED( hr = m_isoMap.Init(dwLandmarkNumber, pfGeodesicMatrix)))
    {
        goto LEnd;
    }

    if (FAILED(hr = m_isoMap.ComputeLargestEigen(2, dwCalculatedDimension)))
    {
        goto LEnd;
    }
    assert(2 == dwCalculatedDimension);

    // 4. Parameterization...
    if (FAILED(hr=CalculateVertMappingCoord(
        pfVertGeodesicDistance, dwLandmarkNumber, 2, nullptr)))
    {
        goto LEnd;
    }
    m_bIsParameterized = true;

LEnd:
    m_landmarkVerts.clear();
    m_isoMap.Clear();
    
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::CollectSharedVerts(
    const CIsochartMesh* pChart1,
    const CIsochartMesh* pChart2,
    std::vector<uint32_t>& vertMap,
    std::vector<bool>& vertMark,
    VERTEX_ARRAY& sharedVertexList,
    VERTEX_ARRAY& anotherSharedVertexList,
    bool& bCanMerge)
{
    bCanMerge = false;

    // 1.Find all vertices in chart1 and chart2 that can be connected
    // (They are the same vertex in the root chart)
    try
    {
        size_t dwVertexCount = pChart2->m_dwVertNumber;
        for (size_t i=0; i < pChart1->m_dwVertNumber; i++)
        {
            ISOCHARTVERTEX* pVertex1 = pChart1->m_pVerts + i;
            assert(pVertex1->dwID == i);
            vertMark[pVertex1->dwID] = true;
            if (!pVertex1->bIsBoundary)
            {
                vertMap[i] = static_cast<uint32_t>(dwVertexCount++);
                continue;
            }

            size_t dwSameVertCount = 0;
            uint32_t dwSharedVerteIndex = INVALID_INDEX;
            for (uint32_t j = 0; j < pChart2->m_dwVertNumber; j++)
            {
                ISOCHARTVERTEX* pVertex2 = pChart2->m_pVerts + j;
                if (!pVertex2->bIsBoundary)
                {
                    continue;
                }
                if (pVertex1->dwIDInRootMesh == pVertex2->dwIDInRootMesh)
                {
                    // If more than 2 vertices are same in root chart, just
                    // give up to connect them.
                    if (dwSameVertCount > 0)
                    {
                        return S_OK;
                    }
                    dwSameVertCount++;
                    dwSharedVerteIndex = j;
                }
            }

            // pVertex1 and pVertex2 can connect together, add them to the shared vertex list.
            if (dwSameVertCount == 1)
            {
                if (std::find(anotherSharedVertexList.cbegin(), anotherSharedVertexList.cend(), pChart2->m_pVerts + dwSharedVerteIndex)
                    != anotherSharedVertexList.cend())
                {
                    return S_OK;
                }
                anotherSharedVertexList.push_back(
                    pChart2->m_pVerts + dwSharedVerteIndex);

                sharedVertexList.push_back(pVertex1);

                vertMap[i] = dwSharedVerteIndex;
                vertMark[i] = false;
            }
            else
            {
                vertMap[i] = static_cast<uint32_t>(dwVertexCount++);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    bCanMerge = true;
    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::CheckMergingToplogy(
    VERTEX_ARRAY& sharedVertexList,
    bool& bIsManifold)
{
    assert(!sharedVertexList.empty());
    bIsManifold = false;

    VERTEX_ARRAY checkedVertexList;
    ISOCHARTVERTEX* pVertex1 = sharedVertexList[0];

    try
    {
        checkedVertexList.push_back(pVertex1);
        sharedVertexList.erase(sharedVertexList.begin());

        while (!checkedVertexList.empty())
        {
            pVertex1 = checkedVertexList[0];
            checkedVertexList.erase(checkedVertexList.begin());
            for (size_t i=0; i < pVertex1->vertAdjacent.size(); i++)
            {
                for (size_t j = 0; j < sharedVertexList.size(); j++)
                {
                    if (sharedVertexList[j]->dwID == pVertex1->vertAdjacent[i])
                    {
                        checkedVertexList.push_back(sharedVertexList[j]);
                        sharedVertexList.erase(sharedVertexList.begin() + j);
                        break;
                    }
                }
            }
        }
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    if (!sharedVertexList.empty())
    {
        return S_OK;
    }

    bIsManifold = true;
    return S_OK;
}


//-------------------------------------------------------------------------------------
CIsochartMesh* CIsochartMesh::MergeTwoCharts(
    const CIsochartMesh* pChart1,
    const CIsochartMesh* pChart2,
    std::vector<uint32_t>& vertMap,
    std::vector<bool>& vertMark,
    size_t dwReduantVertNumber)
{
    ISOCHARTVERTEX* pNewVertex = nullptr;
    ISOCHARTFACE* pNewFace = nullptr;

    auto pNewChart = new (std::nothrow) CIsochartMesh(
        pChart1->m_baseInfo,
        pChart1->m_callbackSchemer,
        pChart1->m_IsochartEngine);
    if (!pNewChart)
    {
        return nullptr;
    }
    
    // 1. Create mesh
    pNewChart->m_bIsSubChart = true;
    pNewChart->m_bVertImportanceDone = true;
    pNewChart->m_fBoxDiagLen = pChart1->m_fBoxDiagLen;
    pNewChart->m_dwVertNumber
        = pChart1->m_dwVertNumber
        + pChart2->m_dwVertNumber-dwReduantVertNumber;
    
    pNewChart->m_dwFaceNumber
        = pChart1->m_dwFaceNumber + pChart2->m_dwFaceNumber;

    pNewChart->m_pVerts = new (std::nothrow) ISOCHARTVERTEX[pNewChart->m_dwVertNumber];
    pNewChart->m_pFaces = new (std::nothrow) ISOCHARTFACE[pNewChart->m_dwFaceNumber];
    if (!pNewChart->m_pVerts || !pNewChart->m_pFaces)
    {
        delete pNewChart;
        return nullptr;
    }

    // 2. Fill vertex buffer
    for (uint32_t i=0; i<pChart2->m_dwVertNumber; i++)
    {
        ISOCHARTVERTEX* pVertex2 = pChart2->m_pVerts + i;
        pNewVertex = pNewChart->m_pVerts+i;
        pNewVertex->dwID = i;
        pNewVertex->dwIDInRootMesh = pVertex2->dwIDInRootMesh;
        pNewVertex->nImportanceOrder = pVertex2->nImportanceOrder;
    }

    size_t dwVertexCount = pChart2->m_dwVertNumber;
    for (size_t i=0; i<pChart1->m_dwVertNumber; i++)
    {
        if (!vertMark[i])
        {
            continue;
        }

        ISOCHARTVERTEX* pVertex1 = pChart1->m_pVerts + i;
        pNewVertex = pNewChart->m_pVerts + dwVertexCount;
        pNewVertex->dwID = static_cast<uint32_t>(dwVertexCount);
        pNewVertex->dwIDInRootMesh = pVertex1->dwIDInRootMesh;
        pNewVertex->nImportanceOrder = pVertex1->nImportanceOrder;
        dwVertexCount++;
    }

    // 3. Fill face buffer
    for (uint32_t i = 0; i<pChart2->m_dwFaceNumber; i++)
    {
        pNewFace = pNewChart->m_pFaces + i;
        pNewFace->dwID = i;
        pNewFace->dwIDInRootMesh = pChart2->m_pFaces[i].dwIDInRootMesh;
        memcpy(pNewFace->dwVertexID, 
                pChart2->m_pFaces[i].dwVertexID,
                sizeof(uint32_t) * 3);
    }
    size_t dwFaceCount = pChart2->m_dwFaceNumber;
    for (uint32_t i = 0; i<pChart1->m_dwFaceNumber; i++)
    {
        pNewFace = pNewChart->m_pFaces + dwFaceCount;
        pNewFace->dwID = static_cast<uint32_t>(dwFaceCount);
        pNewFace->dwIDInRootMesh = pChart1->m_pFaces[i].dwIDInRootMesh;
        for (size_t j=0; j<3; j++)
        {
            pNewFace->dwVertexID[j] = vertMap[pChart1->m_pFaces[i].dwVertexID[j]];
        }
        dwFaceCount++;
    }
    assert(dwVertexCount == pNewChart->m_dwVertNumber);
    assert(dwFaceCount == pNewChart->m_dwFaceNumber);

    pNewChart->m_fChart3DArea = pNewChart->CalculateChart3DArea();
    pNewChart->m_fBaseL2Stretch= pNewChart->CalCharBaseL2SquaredStretch();
    return pNewChart;

}


//-------------------------------------------------------------------------------------
// Try to mege two sub-charts.
HRESULT CIsochartMesh::TryMergeChart(
    ISOCHARTMESH_ARRAY& children,
    const CIsochartMesh* pChart1, 
    const CIsochartMesh* pChart2,
    CIsochartMesh** ppFinialChart)
{
    assert(pChart1 != 0);
    assert(pChart2 != 0);
    assert(ppFinialChart != 0);
    *ppFinialChart = nullptr;

    std::vector<uint32_t> vertMap;
    std::vector<bool> vertMark;

    try
    {
        vertMap.resize(pChart1->m_dwVertNumber);
        vertMark.resize(pChart1->m_dwVertNumber);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = S_OK;

    // 1.Find all vertices in chart1 and chart2 that can be connected
    // (They are the same vertex in the root chart)
    VERTEX_ARRAY sharedVertexList;
    VERTEX_ARRAY anotherSharedVertexList;
    bool bCanMerge = false;
    FAILURE_RETURN(
        CollectSharedVerts(
            pChart1,
            pChart2,
            vertMap,
            vertMark,
            sharedVertexList,
            anotherSharedVertexList,
            bCanMerge));
    if (!bCanMerge)
    {
        return hr;
    }

    size_t dwReduantVertNumber = sharedVertexList.size();
    if (dwReduantVertNumber <= 1)
    {		
        return hr;
    }

    // 2. Check if merge two sub-charts can generate following non-manifold chart.
    bool bIsManifold = false;
    FAILURE_RETURN(
        CheckMergingToplogy(sharedVertexList, bIsManifold));
    if(!bIsManifold)
    {
        return hr;
    }
    FAILURE_RETURN(
        CheckMergingToplogy(anotherSharedVertexList, bIsManifold));
    if(!bIsManifold)
    {
        return hr;
    }

    //3. Create New chart by merging chart1 and chart2
    auto pMainChart = MergeTwoCharts(
        pChart1,
        pChart2,
        vertMap,
        vertMark,
        dwReduantVertNumber);
    if (!pMainChart)
    {
        return E_OUTOFMEMORY;
    }

    // 4. Build full connection to check if new sub-chart is manifold
    bool bManifold = false;
    hr = pMainChart->BuildFullConnection(bManifold);
    if (FAILED(hr) || !bManifold)
    {
        delete pMainChart;
        return hr;
    }

    // 5. if New charts has multiple boundaries, give up merging result.
    size_t dwBoundaryNumber = 0;
    bool bSimpleChart = 0;
    do{
        hr = pMainChart->PrepareSimpleChart(true, dwBoundaryNumber, bSimpleChart);
        if (FAILED(hr) 
            || dwBoundaryNumber == 0 
            || pMainChart->m_children.size() > 1)
        {
            delete pMainChart;
            return hr;
        }
        if (!bSimpleChart)
        {
            CIsochartMesh* pChild = pMainChart->GetChild(0);
            assert( pChild != 0 );
            _Analysis_assume_( pChild != 0 );
            pMainChart->UnlinkChild(0);
            delete pMainChart;
            pMainChart = pChild;		
        }
    }
    while(!bSimpleChart);

    // 6. Ccompute the adjacent sub-charts of new sub-chart.
    auto& adjacentChartList = pMainChart->m_adjacentChart;
    try
    {
        for (size_t i=0; i < pChart2->m_adjacentChart.size(); i++)
        {
            if (children[pChart2->m_adjacentChart[i]] != pChart1)
            {
                adjacentChartList.push_back(pChart2->m_adjacentChart[i]);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        delete pMainChart;
        return E_OUTOFMEMORY;
    }

    for (size_t i=0; i<pChart1->m_adjacentChart.size(); i++)
    {
        if (children[pChart1->m_adjacentChart[i]] != pChart2)
        {
            if (!addNoduplicateItem(adjacentChartList,
                pChart1->m_adjacentChart[i]))
            {
                delete pMainChart;
                return E_OUTOFMEMORY;
            }
        }
    }
    pMainChart->m_bIsSubChart = true;
    *ppFinialChart = pMainChart;
    return hr;
}


//-------------------------------------------------------------------------------------
// Find all boundary in a chart.
HRESULT CIsochartMesh::CalculateBoundaryNumber(
    size_t &dwBoundaryNumber) const
{
    dwBoundaryNumber = 0;

    std::unique_ptr<bool[]> vertMark( new (std::nothrow) bool[m_dwVertNumber] );
    if (!vertMark)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbVertMark = vertMark.get();

    memset(pbVertMark, 0, sizeof(bool)*m_dwVertNumber);

    try
    {
        VERTEX_ARRAY boundaryList;
        uint32_t dwVertIndex = 0;
        uint32_t dwBoundaryID = 0;

        while (dwVertIndex < m_dwVertNumber)
        {
            while (dwVertIndex < m_dwVertNumber &&
                (!m_pVerts[dwVertIndex].bIsBoundary
                || pbVertMark[dwVertIndex]))
            {
                dwVertIndex++;
            }
            if (dwVertIndex >= m_dwVertNumber)
            {
                break;
            }
            dwBoundaryID++;
            pbVertMark[dwVertIndex] = true;

            uint32_t dwHead, dwEnd;
            boundaryList.clear();
            boundaryList.push_back(m_pVerts + dwVertIndex);
            dwHead = 0;
            dwEnd = 1;
            while (dwHead < dwEnd)
            {
                ISOCHARTVERTEX* pCurrentVertex = boundaryList[dwHead];
                auto& adjacentVertList = pCurrentVertex->vertAdjacent;

                uint32_t dwIndex = adjacentVertList[0];
                assert(m_pVerts[dwIndex].dwID == dwIndex);

                if (m_pVerts[dwIndex].bIsBoundary && !pbVertMark[dwIndex])
                {
                    pbVertMark[dwIndex] = true;
                    boundaryList.push_back(m_pVerts + dwIndex);
                }

                dwIndex = adjacentVertList[adjacentVertList.size() - 1];
                assert(m_pVerts[dwIndex].dwID == dwIndex);
                if (m_pVerts[dwIndex].bIsBoundary && !pbVertMark[dwIndex])
                {
                    pbVertMark[dwIndex] = true;
                    boundaryList.push_back(m_pVerts + dwIndex);
                }
                dwHead++;
                dwEnd = static_cast<uint32_t>(boundaryList.size());
            }
        }
        dwBoundaryNumber = dwBoundaryID;
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}


//-------------------------------------------------------------------------------------
// compute average normal of a chart.
void CIsochartMesh::CalculateAveragNormal(
    XMFLOAT3* pNormal) const
{
    XMVECTOR normal = XMVectorZero();
    ISOCHARTFACE* pFace = m_pFaces;

    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        normal += XMLoadFloat3(m_baseInfo.pFaceNormalArray + pFace->dwIDInRootMesh)
            * m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
        pFace++;
    }

    XMStoreFloat3(pNormal, XMVector3Normalize(normal));
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::CalculateAdjacentChart(
    uint32_t dwCurrentChartID,
    uint32_t* pdwFaceChartRootID,
    const uint32_t* pRootFaceAdjacentArray)
{
    m_adjacentChart.clear();
    ISOCHARTFACE* pFace = m_pFaces;

    for (size_t j=0; j<m_dwFaceNumber; j++)
    {
        for (size_t k=0; k<3; k++)
        {
            if (pRootFaceAdjacentArray[3*pFace->dwIDInRootMesh+k] 
                == INVALID_FACE_ID)
            {
                continue;
            }

            uint32_t dwChartID = 
                pdwFaceChartRootID[pRootFaceAdjacentArray
                [3*pFace->dwIDInRootMesh+k]];

            if (dwChartID != dwCurrentChartID)
            {
                if (!addNoduplicateItem(m_adjacentChart, dwChartID))
                {
                    return E_OUTOFMEMORY;
                }
            }
        }
        pFace++;
    }
    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::TryParameterize(bool& bSucceed)
{
    HRESULT hr = S_OK;
    bSucceed = false;

    // 1. Try Isomap
    CalculateChartEdgeLength();
    if (FAILED(hr = CalculateIsoParameterization()))
    {
        return hr;
    }

    bool bIsOverlapping = false;
    #if CHECK_OVER_LAPPING_BEFORE_OPT_INFINIT
    if (FAILED(hr=IsParameterizationOverlapping(this,bIsOverlapping)))
    {
        return hr;
    }
    #endif
        
    bool bSucceedOptInfinte = false;
    if (!bIsOverlapping)
    {
        if (FAILED(hr = OptimizeGeoLnInfiniteStretch(bSucceedOptInfinte)))
        {
            return hr;
        }
    }
    if (bSucceedOptInfinte)
    {
        bSucceed = true;
        return hr;
    }

    bool bIsSolutionOverLap;
    float fSmallStretch;

    #if MERGE_TURN_ON_LSCM
    // 2. Try LSCM	
    bIsSolutionOverLap = true;
    DPF(1, "Try LSCM!");

    CIsochartMesh::ConvertToInternalCriterion(
        SMALL_STRETCH_TO_TURNON_LSCM, fSmallStretch, false);

    if (m_baseInfo.fExpectAvgL2SquaredStretch >= 
        fSmallStretch)
    {
        if (FAILED(hr=LSCMParameterization(bIsSolutionOverLap)))
        {
            return hr;
        }
        if (!bIsSolutionOverLap)
        {
            DPF(1, "LSCM Succeed!");
            bSucceed = true;
            return hr;
        }
    }
    #endif

    #if MERGE_TURN_ON_BARYCENTRIC
    // 3. Try Barcentric
    CIsochartMesh::ConvertToInternalCriterion(
        SMALL_STRETCH_TO_TURNON_BARY, fSmallStretch, false);
    
    if (m_baseInfo.fExpectAvgL2SquaredStretch >= 
        fSmallStretch)
    {	
        bIsSolutionOverLap = true;
        hr=BarycentricParameterization(bIsSolutionOverLap);
        bSucceed = !bIsSolutionOverLap;
    }
    #endif
    
    return hr;
}
