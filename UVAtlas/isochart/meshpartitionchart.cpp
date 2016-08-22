//-------------------------------------------------------------------------------------
// UVAtlas - meshpartitionchart.cpp
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

using namespace Isochart;
using namespace DirectX;

/////////////////////////////////////////////////////////////
////////////////////Common Patition Methods//////////////////
/////////////////////////////////////////////////////////////
HRESULT CIsochartMesh::GenerateAllSubCharts(
    const uint32_t* pdwFaceChartID,
    size_t dwMaxSubchartCount,
    bool& bAllManifold)
{
    bAllManifold = true;

    if (dwMaxSubchartCount < 2)
    {
        return S_OK;
    }
    DeleteChildren();

    std::unique_ptr<std::vector<uint32_t>[]> chartFaceList( new (std::nothrow) std::vector<uint32_t>[dwMaxSubchartCount] );
    if (!chartFaceList)
    {
        return E_OUTOFMEMORY;
    }

    auto pChartFaceList = chartFaceList.get();

    // 1. Search all faces for each sub-chart
    try
    {
        for (uint32_t i = 0; i<m_dwFaceNumber; i++)
        {
            assert(pdwFaceChartID[i] < dwMaxSubchartCount);
            pChartFaceList[pdwFaceChartID[i]].push_back(i);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    for (size_t i=0; i<dwMaxSubchartCount; i++)
    {
        // If all faces in the same sub-chart, needn't create new
        // sub chart.
        if (pChartFaceList[i].size() == m_dwFaceNumber)
        {
            return S_OK;
        }
    }

#ifdef _DEBUG
    size_t dwTotalFace = 0;
    for (size_t i = 0; i<dwMaxSubchartCount; i++)
    {
        dwTotalFace += pChartFaceList[i].size();
    }
    assert(dwTotalFace == m_dwFaceNumber);
#endif

    // 2. Generate sub-charts.
    for (size_t i=0; i<dwMaxSubchartCount; i++)
    {
        if (pChartFaceList[i].empty())
        {
            continue;
        }
        HRESULT hr = BuildSubChart(pChartFaceList[i], bAllManifold);
        if (FAILED(hr) || !bAllManifold)
        {
            DeleteChildren();
            return hr;
        }
    }

    assert(m_children.size() > 1);

    return S_OK;
}

// Build sub chart of current chart using some faces of current
// chart. Then, build full connection for the new chart
HRESULT CIsochartMesh::BuildSubChart(
    std::vector<uint32_t>& faceList,
    bool& bManifold)
{
    assert(!faceList.empty());
    HRESULT hr = S_OK;
    
    VERTEX_ARRAY subChartVertList;

    // 1. Get all vertices belong to the new chart
    FAILURE_RETURN(GetAllVerticesInSubChart(faceList, subChartVertList));

    // 2. Create new chart by using the vertex and face list
    auto pSubChart = CreateNewChart(subChartVertList, faceList, true);
    if (!pSubChart)
    {
        return E_OUTOFMEMORY;
    }
    // 3. Build full connection. 
    bManifold = false;
    hr = pSubChart->BuildFullConnection(bManifold);

    if ((FAILED (hr) || !bManifold))
    {
        delete pSubChart;
        return hr;
    }
    else
    {
        assert(pSubChart != 0);
        try
        {
            m_children.push_back(pSubChart);
        }
        catch (std::bad_alloc&)
        {
            delete pSubChart;
            return E_OUTOFMEMORY;
        }
    }

    pSubChart->m_fChart3DArea = pSubChart->CalculateChart3DArea();
    pSubChart->m_fBaseL2Stretch = pSubChart->CalCharBaseL2SquaredStretch();
    return hr;
}


// Get all vertices belong to the new chart
HRESULT CIsochartMesh::GetAllVerticesInSubChart(
    const std::vector<uint32_t>& faceList,
    VERTEX_ARRAY& subChartVertList)
{
    std::unique_ptr<bool []> isVertInNewChart(new (std::nothrow) bool[m_dwVertNumber]);
    if (!isVertInNewChart)
    {
        return E_OUTOFMEMORY;
    }

    bool* pbIsVertInNewChart = isVertInNewChart.get();

    memset(pbIsVertInNewChart, 0, m_dwVertNumber * sizeof(bool));

    size_t dwVertCountInNewChart = 0;
    for (size_t i=0; i < faceList.size(); i++)
    {
        ISOCHARTFACE* pFace = m_pFaces + faceList[i];
        for (size_t j = 0; j < 3; j++)
        {
            if (!pbIsVertInNewChart[pFace->dwVertexID[j]])
            {
                pbIsVertInNewChart[pFace->dwVertexID[j]] = true;
                dwVertCountInNewChart++;
            }
        }
    }

    try
    {
        subChartVertList.reserve(dwVertCountInNewChart);

        for (size_t i=0; i < m_dwVertNumber; i++)
        {
            if (pbIsVertInNewChart[i])
            {
                subChartVertList.push_back(m_pVerts + i);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    assert(subChartVertList.size() == dwVertCountInNewChart);

    return S_OK;
}

// Optimize Partition
// Before perform this function, all faces in current chart have been
// assign a group ID, each group ID present a new chart. This function
// try to adjust some faces ID to get a better partition result.

// 1. Creat a heap to get the chart with least face each time.
// 2. For each face on each chart, check the chart ID of adjacent faces.
// Try to amend the hackle on new chart boundary.

// 3. Step 2 may generate non-manifold mesh, so need to check and avoid
// non-manifold mesh.

HRESULT CIsochartMesh::SmoothPartitionResult(
    size_t dwMaxSubchartCount,
    uint32_t* pdwFaceChartID,
    bool& bIsOptimized)
{
    assert(dwMaxSubchartCount > 0);

#ifdef _DEBUG
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        assert(pdwFaceChartID[i] != INVALID_INDEX);
    }
#endif

    //1. Creat a heap to get the chart with least face each time.
    CMaxHeap<int, uint32_t> heap;
    if (!heap.resize(dwMaxSubchartCount))
    {
        return E_OUTOFMEMORY;
    }

    std::unique_ptr<CMaxHeapItem<int, uint32_t> []> heapItems(new (std::nothrow) CMaxHeapItem<int, uint32_t>[dwMaxSubchartCount]);
    if (!heapItems)
    {
        return E_OUTOFMEMORY;
    }

    auto pHeapItems = heapItems.get();

    for (uint32_t i=0; i<dwMaxSubchartCount; i++)
    {
        pHeapItems[i].m_weight = 0;
        pHeapItems[i].m_data = i;
    }
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        assert(pdwFaceChartID[i] <dwMaxSubchartCount);
        _Analysis_assume_(pdwFaceChartID[i] <dwMaxSubchartCount);
        // Count the face number of each new chart
        pHeapItems[pdwFaceChartID[i]].m_weight -= 1;
    }

    for (size_t i=0; i<dwMaxSubchartCount; i++)
    {
        // The memory has been allocated in heap.resize(dwMaxSubchartCount)
        // No need to check if insert is successful.
        heap.insert(pHeapItems+i);
    }

    // 2. Groupp faces by their chart ID
    std::unique_ptr<std::vector<uint32_t>[]> pFaceGroup( new (std::nothrow) std::vector<uint32_t>[dwMaxSubchartCount] );
    if (!pFaceGroup)
    {
        return E_OUTOFMEMORY;
    }

    try
    {
        for (size_t i=0; i < dwMaxSubchartCount; i++)
        {
            pFaceGroup[i].reserve(static_cast<uint32_t>(-pHeapItems[i].m_weight));
        }
        for (uint32_t i = 0; i < m_dwFaceNumber; i++)
        {
            pFaceGroup[pdwFaceChartID[i]].push_back(i);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 3. Optimize partition
    while(!heap.empty())
    {
        auto pTop = heap.cutTop();
        assert(pTop != 0 && (pTop->m_weight <= 0));

        for (size_t j=0; j < pFaceGroup[pTop->m_data].size(); j++)
        {
            uint32_t dwFaceID = pFaceGroup[pTop->m_data][j];
            ISOCHARTFACE* pFace = m_pFaces + dwFaceID;
            
            assert(dwFaceID == pFace->dwID);
            assert(pTop->m_data == pdwFaceChartID[pFace->dwID]);

            SmoothOneFace(pFace, pdwFaceChartID);
        }
    }

    // 3. Make optimzation valid.
    return MakePartitionValid(dwMaxSubchartCount, pdwFaceChartID, bIsOptimized);
}

void CIsochartMesh::SmoothOneFace(
    ISOCHARTFACE* pFace,
    uint32_t* pdwFaceChartID)
{
    uint32_t dwAdjacentChart[3];
    uint32_t dwCurrentFaceChartID = pdwFaceChartID[pFace->dwID];

    // 1. Get chart ID of ajacent faces, store in dwAdjacentChart
    size_t dwOtherChartFaceCount = 0;
    for (size_t k=0; k<3; k++)
    {
        ISOCHARTEDGE& edge = m_edges[pFace->dwEdgeID[k]];
        if (edge.bIsBoundary)
        {
            dwAdjacentChart[k] = dwCurrentFaceChartID;
        }
        else
        {
            if (edge.dwFaceID[0] == pFace->dwID)
            {
                dwAdjacentChart[k] = pdwFaceChartID[edge.dwFaceID[1]];
            }
            else
            {
                dwAdjacentChart[k] = pdwFaceChartID[edge.dwFaceID[0]];
            }
            if (dwAdjacentChart[k] != dwCurrentFaceChartID)
            {
                dwOtherChartFaceCount++;
            }
        }
    }

    // 2. If 2 of 3 adjacent faces are not in the same chart with
    // current face.
    if (2 == dwOtherChartFaceCount)
    {
        uint32_t k = 0;
        for (k=0; k<3; k++)
        {
            // kth adjacent face and another adjacent face
            // have same chart ID , then change current face's
            // chart ID to kth adjacent face Chart ID,
            // As following, the middel face's chart ID "2" has
            // been changed to "1"
            // ---------         -----------
            // \1 / \1 /          \1 / \1 /
            //  \/ 2 \/            \/ 1 \/
            //   ----      --->     ---- 
            //   \ 2/               \ 2/
            //    \/                 \/
            if (dwAdjacentChart[k] != dwCurrentFaceChartID
            && (dwAdjacentChart[k] == dwAdjacentChart[(k+1)%3]
            ||dwAdjacentChart[k] == dwAdjacentChart[(k+2)%3]))
            {
                pdwFaceChartID[pFace->dwID] = dwAdjacentChart[k];
                break;
            }
        }

        if ( k >=3 )
        {
            // ---------
            // \1 / \3 /
            //  \/ 2 \/
            //   ---- 
            //   \ 2/
            //    \/
            // Change current face chart ID according the adjacent face
            // sharing max length edge with current face. (Here can not
            // chane adjacent face chart ID)

            uint32_t dwMaxLengthEdgeIndex = 0;
            for (k=1; k<3; k++)
            {
                if (m_edges[pFace->dwEdgeID[dwMaxLengthEdgeIndex]].fLength
                    < m_edges[pFace->dwEdgeID[k]].fLength)
                {
                    dwMaxLengthEdgeIndex = k;
                }
            }
            pdwFaceChartID[pFace->dwID] = dwAdjacentChart[dwMaxLengthEdgeIndex];
        }
    }

    // 3. If all adjacent faces are not in the same chart with
    // current face.
    else if (3 == dwOtherChartFaceCount)
    {
        uint32_t k = 0;
        for (k=0; k<3; k++)
        {
            // ---------         -----------
            // \1 / \1 /          \1 / \1 /
            //  \/ 2 \/            \/ 1 \/
            //   ----      --->     ---- 
            //   \ 3/               \ 3/
            //    \/                 \/
            if (dwAdjacentChart[k] == dwAdjacentChart[(k+1)%3]
            ||dwAdjacentChart[k] == dwAdjacentChart[(k+2)%3] )
            {
                pdwFaceChartID[pFace->dwID] = dwAdjacentChart[k];
                break;
            }
        }

        if ( k >=3 )
        {
            // ---------
            // \1 / \3 /
            //  \/ 2 \/
            //   ---- 
            //   \ 4/
            //    \/
            // Change current face chart ID according the adjacent face
            uint32_t dwMaxLengthEdgeIndex = 0;
            for (k=1; k<3; k++)
            {
                if (m_edges[pFace->dwEdgeID[dwMaxLengthEdgeIndex]].fLength
                    < m_edges[pFace->dwEdgeID[k]].fLength)
                {
                    dwMaxLengthEdgeIndex = k;
                }
            }
            pdwFaceChartID[pFace->dwID] = dwAdjacentChart[dwMaxLengthEdgeIndex];
        }
    }
}

HRESULT CIsochartMesh::AdjustToSameChartID(
    uint32_t* pdwFaceChartID,
    size_t	dwCongFaceCount,	
    uint32_t* pdwCongFaceID,
    bool &bModified)
{
    HRESULT hr = S_OK;

    std::vector<uint32_t> allDiffSubChartIDList;
    std::vector<uint32_t> subChartIDCountList;	
    bModified = false;

    // 1. Find all different sub chart id		
    for (size_t ii=0; ii<dwCongFaceCount; ii++)
    {
        if (!addNoduplicateItem(allDiffSubChartIDList,
            pdwFaceChartID[pdwCongFaceID[ii]]))
        {
            return E_OUTOFMEMORY;
        }
    }
    if (allDiffSubChartIDList.size() <= 1)
    {
        return S_OK;
    }

    // 2. Caculate the count of all possible subchart id
    try
    {
        subChartIDCountList.resize(allDiffSubChartIDList.size());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    memset(subChartIDCountList.data(), 0, sizeof(uint32_t)*subChartIDCountList.size());
    for (size_t ii= 0; ii<dwCongFaceCount; ii++)
    {
        for (size_t jj=0; jj<allDiffSubChartIDList.size(); jj++)
        {		
            if (pdwFaceChartID[pdwCongFaceID[ii]] == allDiffSubChartIDList[jj])
            {
                subChartIDCountList[jj]	 = subChartIDCountList[jj] + 1;
                break;
            }
        }
    }	

    // Sort subchart id by subchart count
    for (size_t ii=0; ii<subChartIDCountList.size()-1; ii++)
    {
        for (size_t jj=ii+1; jj<subChartIDCountList.size(); jj++)
        {
            if (subChartIDCountList[ii] < subChartIDCountList[jj])
            {
                std::swap(subChartIDCountList[ii],subChartIDCountList[jj]);
                std::swap(allDiffSubChartIDList[ii],allDiffSubChartIDList[jj]);
            }
        }
    }

    uint32_t dwTargetSubChartID = allDiffSubChartIDList[0];

    // 3. Set new sub chart id
    for (size_t ii=0; ii<dwCongFaceCount; ii++)
    {
        pdwFaceChartID[pdwCongFaceID[ii]] = dwTargetSubChartID;
    }

    bModified = true;
    return hr;
}


HRESULT CIsochartMesh::FindCongenerFaces(
    std::vector<uint32_t>& congenerFaceCategories,
    std::vector<uint32_t>& congenerFaceCategoryLen,
    bool& bHasFalseEdge)
{
    bHasFalseEdge = false;

    // 1. Find all false faces. (face having false edge.)
    std::unique_ptr<bool[]> bFalseFace(new (std::nothrow) bool[m_dwFaceNumber]);
    if ( !bFalseFace )
    {
        return E_OUTOFMEMORY;
    }

    memset(bFalseFace.get(), 0, sizeof(bool)*m_dwFaceNumber);
    
    for (size_t ii=0; ii<m_dwEdgeNumber; ii++)
    {
        ISOCHARTEDGE& edge = m_edges[ii];
        
        if (!edge.bCanBeSplit)
        {
            if( (edge.dwFaceID[0] == INVALID_FACE_ID) ||
                (edge.dwFaceID[1] == INVALID_FACE_ID) )
            {
                DPF(0, "UVAtlas Internal error: false edge exists on a boundary edge");
                return E_FAIL;
            }

            bFalseFace[edge.dwFaceID[0]] = true;
            bFalseFace[edge.dwFaceID[1]] = true;

            bHasFalseEdge = true;
        }
    }

    if (!bHasFalseEdge)
    {
        return S_OK;
    }
        
    // 2. Find congener faces (faces can be connected by false edge. must have same chart id)
    std::unique_ptr<bool []> bProcessedFace(new (std::nothrow) bool[m_dwFaceNumber]);
    if (!bProcessedFace)
    {
        return E_OUTOFMEMORY;
    }

    memset(bProcessedFace.get(), 0, sizeof(bool)*m_dwFaceNumber);

    try
    {
        for (uint32_t ii = 0; ii < m_dwFaceNumber; ii++)
        {
            if (!bFalseFace[ii] || bProcessedFace[ii])
            {
                continue;
            }

            uint32_t dwBegin = static_cast<uint32_t>(congenerFaceCategories.size());

            congenerFaceCategories.push_back(ii);
            bProcessedFace[ii] = true;

            uint32_t dwCur = dwBegin;
            do
            {
                uint32_t dwCurrentFace = congenerFaceCategories[dwCur];
                ISOCHARTFACE& face = m_pFaces[dwCurrentFace];

                for (size_t jj = 0; jj < 3; jj++)
                {
                    ISOCHARTEDGE& edge = m_edges[face.dwEdgeID[jj]];
                    if (edge.bCanBeSplit) continue;

                    uint32_t dwAdjFace =
                        (edge.dwFaceID[0] == dwCurrentFace) ?
                        edge.dwFaceID[1] : edge.dwFaceID[0];

                    assert(bFalseFace[dwAdjFace]);
                    if (dwAdjFace == INVALID_FACE_ID ||
                        bProcessedFace[dwAdjFace])
                    {
                        continue;
                    }

                    congenerFaceCategories.push_back(dwAdjFace);
                    bProcessedFace[dwAdjFace] = true;
                }
                dwCur++;
            } while (dwCur < congenerFaceCategories.size());

            uint32_t dwCongEdgeCount = dwCur - dwBegin;
            congenerFaceCategoryLen.push_back(dwCongEdgeCount);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;	
}


HRESULT CIsochartMesh::SatifyUserSpecifiedRule(
    uint32_t* pdwFaceChartID,
    bool& bHasFalseEdge,
    bool& bIsModifiedPartition,
    bool& bIsSatifiedUserRule)
{
    HRESULT hr = S_OK;
    bIsModifiedPartition = false;
    bIsSatifiedUserRule = true;
    bHasFalseEdge = false;

    if (!m_baseInfo.pdwSplitHint)
    {
        return hr;
    }

    std::vector<uint32_t> congenerFaceCategories;
    std::vector<uint32_t> congenerFaceCategoryLen;

    // 1. Find congener edge categories.
    //If non-splittable edge A has same ajacent faces with non-splittable edge B,
    //then they belong to same category.
    FAILURE_RETURN(
        FindCongenerFaces(
            congenerFaceCategories, 
            congenerFaceCategoryLen,
            bHasFalseEdge));

    // No false edge, the user specified rule is satisifed
    if (!bHasFalseEdge)
    {
        return hr;
    }

    // 2. Adjust the sub-chart id
    uint32_t dwBegin = 0;
    for (size_t ii=0; ii<congenerFaceCategoryLen.size(); ii++)
    {
        uint32_t *pCongFaceID = congenerFaceCategories.data() + dwBegin;
        uint32_t dwCongFaceCount = congenerFaceCategoryLen[ii];

        bool bModifiedCurPass = false;

        FAILURE_RETURN(
            AdjustToSameChartID(
                pdwFaceChartID, 
                dwCongFaceCount,					
                pCongFaceID, 
                bModifiedCurPass));			

        bIsModifiedPartition |= bModifiedCurPass;
        dwBegin += congenerFaceCategoryLen[ii];
    }

    // 3. If all faces in current mesh has same sub chart id, then we cannot split current chart
    uint32_t dwSubChartID = pdwFaceChartID[0];

    bIsSatifiedUserRule = false;
    for (size_t ii=1; ii<m_dwFaceNumber; ii++)
    {
        if (dwSubChartID != pdwFaceChartID[ii])
        {
            bIsSatifiedUserRule = true;
            break;
        }
    }

    if (!bIsSatifiedUserRule)
    {		
        if (congenerFaceCategoryLen.empty())
        {
            assert(!congenerFaceCategoryLen.empty());	
            DPF(0, "All faces have same chart id, it's not possible!");
        }
        else if (congenerFaceCategoryLen[0] == m_dwFaceNumber)
        {
            DPF(0, "Can not split chart without cutting false edge!");
        }
        else
        {
            uint32_t targetID = 0;
            if (dwSubChartID == 0)
                targetID = 1;
            else
                targetID = 0;

            for (size_t ii=0; ii<congenerFaceCategoryLen[0]; ii++)
            {
                pdwFaceChartID[congenerFaceCategories[ii]] = targetID;			
            }
            bIsSatifiedUserRule = true;	
        }
    }
        
    return hr;
}

HRESULT CIsochartMesh::SatifyManifoldRule(
    size_t dwMaxSubchartCount,
    uint32_t* pdwFaceChartID,
    bool& bIsModifiedPartition,
    bool& bIsManifold)
{
    HRESULT hr = S_OK;

    bIsModifiedPartition = false;
    size_t dwIteration = 0;
    
    // 1. Check if current partiton will generated non-manifold mesh and
    // try to adjust the chart ID of some faces to avoid non-manifold
    bool bIsModifiedCurPass;
    do
    {	
        bIsModifiedCurPass = false;
        ISOCHARTVERTEX* pVertex = m_pVerts;

        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            assert(pVertex->dwID == i);

            bool bIsModifiedCurOperation = false;
            FAILURE_RETURN(
                MakeValidationAroundVertex(
                    pVertex, pdwFaceChartID, true, bIsModifiedCurOperation));
            
            bIsModifiedCurPass = (bIsModifiedCurPass | bIsModifiedCurOperation);
            pVertex++;
        }
        dwIteration++;

        bIsModifiedPartition = (bIsModifiedPartition | bIsModifiedCurPass);
    // if there are still some non-manifold topology, check and fix again
    }while(bIsModifiedCurPass && dwIteration <= dwMaxSubchartCount);

    //if found and fixed some non-manifold topology, check all vetices
    //again. This algorithm can not gurantee convergence. If current
    //partition method still causes non-manifold topology after dwMaxSubchartCount
    //times iteration, it's not a valid partition.
    if (dwIteration > dwMaxSubchartCount)
    {
        bIsManifold = false;
    }
    else
    {
        bIsManifold = true;
    }
    
    return hr;
}


// Partition optimization may generate non-manifold sub-charts, in this 
// condition, some  adjustment should apply to gurantee the sub-charts are manifold.
HRESULT CIsochartMesh::MakePartitionValid(
        size_t dwMaxSubchartCount, 
        uint32_t* pdwFaceChartID,
        bool& bIsPartitionValid)
{
    HRESULT hr = S_OK;

    bool bIsSatifiedUserRule = false;
    bool bModifiedForManifold = false;
    bool bHasFalseEdge = false;

    
    bool bIsManifold = false;
    bool bModifiedForUserRule = false;

    bIsPartitionValid = false;

    size_t dwIterationCount = 0;
    do
    {
        bModifiedForManifold = false;
        bModifiedForUserRule = false;

        if (FAILED(hr=SatifyUserSpecifiedRule(
            pdwFaceChartID, 
            bHasFalseEdge,
            bModifiedForUserRule, 
            bIsSatifiedUserRule)))
        {
            return hr;
        }
        if (!bIsSatifiedUserRule)
        {
            DPF(0, "Cannot partition the mesh without breaking false edges.");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        if (dwIterationCount+1 >= dwMaxSubchartCount)
        {	// If we can not satisfy the Non-manifold and False edge in the same time. 
            // just keep non-manifold here and clean it during build-full relationship.
            bIsPartitionValid = true;
            return hr;
        }
        
        if (bModifiedForUserRule || !bIsManifold)
        {
            if (FAILED(hr=SatifyManifoldRule(
                dwMaxSubchartCount, 
                pdwFaceChartID, 
                bModifiedForManifold,
                bIsManifold)))
            {
                return hr;
            }
        }
        
        bIsPartitionValid = bIsManifold;
        if (bIsManifold && (!bModifiedForManifold || !bHasFalseEdge))
        {
            m_bNeedToClean = false;
            return hr;
        }
        
        m_bNeedToClean = true;
        dwIterationCount++;
    }while(bModifiedForManifold && 
    dwIterationCount < (dwMaxSubchartCount)); // avoid dead lock

    // Now, always set it to true. clean mesh later..
    bIsPartitionValid = true;

    return hr;		

}

// For each vertex check its adjacent faces to find and adjust
// non-manifold toplogic. Following is an example of non-manifold
// topology.
//  __________
//  |\     / |
//  |  \2 /  |
//  |1  \/ 1 |
//  |  /  \  |
//  | / 2  \ |
//  ----------
// Algorithm:
//.(1) Scan the adjacent faces around current vertex
//  check if all faces having same chart ID share one
//  edge with each other. ( This means faces belonging
//  to same chart can connected by edge.
// (2) if faces belonging to the same chart can not be
// connected together by edge, change chart ID of some faces
// to avoid non-manifold
HRESULT
CIsochartMesh::MakeValidationAroundVertex(
    ISOCHARTVERTEX* pVertex,
    uint32_t* pdwFaceChartID,
    bool bDoneFix,	// false: just indicate non-manifold but not modify the pdwFaceChartID
    bool& bIsFixedSomeNonmanifold)
{
    HRESULT hr = S_OK;
    bIsFixedSomeNonmanifold = false;
    if (pVertex->faceAdjacent.empty())
    {
        return S_OK;
    }

    uint32_t dwCandidateChartID1;
    uint32_t dwCandidateChartID2;

    // 1. if all faces around current vertex belong to
    // same chart, it must be a vaild topology. otherwise
    // store two different chart ID in dwCandidateChartID1
    // and dwCandidateChartID2
    if (IsAdjacentFacesInOneChart(
            pVertex,
            pdwFaceChartID,
            dwCandidateChartID1,
            dwCandidateChartID2))
    {
        return S_OK;
    }

    std::vector<uint32_t> checkedChartIDList;
    FACE_ARRAY unconnectedFaceList;
    FACE_ARRAY connectedFaceList;

    // 2. Detect and fix invalid toplogy 
    try
    {
        for (size_t i=0; i < pVertex->faceAdjacent.size(); i++)
        {
            ISOCHARTFACE* pCurrentFace =
                m_pFaces + pVertex->faceAdjacent[i];

            uint32_t dwCurrentFaceChartID =
                pdwFaceChartID[pCurrentFace->dwID];

            // 2.1 Skip the faces having the checked chart ID
            if (isInArray(checkedChartIDList, dwCurrentFaceChartID))
            {
                continue;
            }

            checkedChartIDList.push_back(dwCurrentFaceChartID);

            // 2.2 Add current face into connectedFaceList, 
            // Add All faces having same chart ID as current face
            // into unconnectedFaceList
            connectedFaceList.clear();
            connectedFaceList.push_back(pCurrentFace);

            unconnectedFaceList.clear();
            for (size_t j = i + 1; j < pVertex->faceAdjacent.size(); j++)
            {
                ISOCHARTFACE* pSubsequentFace =
                    m_pFaces + pVertex->faceAdjacent[j];

                if (pdwFaceChartID[pSubsequentFace->dwID]
                    == dwCurrentFaceChartID)
                {
                    unconnectedFaceList.push_back(pSubsequentFace);
                }
            }

            if (!unconnectedFaceList.empty())
            {
                // 2.3 if face in unconnectedFaceList sharing a edge with a face in 
                // connectedFaceList, move it to connectedFaceList. 
                FAILURE_RETURN(
                    TryConnectAllFacesInSameChart(
                    unconnectedFaceList, connectedFaceList));

                // 2.4 if some faces in unconnectedFaceList can not be moved into 
                // connectedFaceList, non-manifold topology occurs. Amend some face's
                // chart ID to fix the invalid topology.
                if (!unconnectedFaceList.empty())
                {
                    if (bDoneFix) // Some times, we only want to know if there exist non-manifold mesh.
                    {
                        AdjustChartIDToAvoidNonmanifold(
                            pdwFaceChartID,
                            unconnectedFaceList,
                            connectedFaceList,
                            dwCurrentFaceChartID,
                            dwCandidateChartID1,
                            dwCandidateChartID2);
                    }

                    bIsFixedSomeNonmanifold = true;
                }
            }
            // If some invalid topology has been fixed, don't continue to check current
            // vertex
            if (bIsFixedSomeNonmanifold)
            {
                break;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

// check if all faces around current vertex belong to
// same chart
bool CIsochartMesh::IsAdjacentFacesInOneChart(
    ISOCHARTVERTEX* pVertex,
    uint32_t* pdwFaceChartID,
    uint32_t& dwChartID1,
    uint32_t& dwChartID2)
{
    dwChartID1 = dwChartID2 =
        pdwFaceChartID[pVertex->faceAdjacent[0]];

    for (size_t i=1; i<pVertex->faceAdjacent.size(); i++)
    {
        dwChartID2 = 
            pdwFaceChartID[pVertex->faceAdjacent[i]] ;

        if(dwChartID1 != dwChartID2)
        {
            return false;
        }
    }
    return true;
}

//If face in unconnectedFaceList sharing a edge with a face in 
//connectedFaceList, move it to unconnectedFaceList. 
HRESULT CIsochartMesh::TryConnectAllFacesInSameChart(
    FACE_ARRAY& unconnectedFaceList,
    FACE_ARRAY& connectedFaceList)
{
    try
    {
        for (size_t ii = 0; ii < connectedFaceList.size(); ii++)
        {
            if (unconnectedFaceList.empty())
            {
                break;
            }
            ISOCHARTFACE* pConnectedFace = connectedFaceList[ii];

            for (size_t jj = 0; jj < 3; jj++)
            {
                ISOCHARTEDGE& edge = m_edges[pConnectedFace->dwEdgeID[jj]];

                ISOCHARTFACE* pNextFace;
                if (!edge.bIsBoundary)
                {
                    if (edge.dwFaceID[0] == pConnectedFace->dwID)
                    {
                        pNextFace = m_pFaces + edge.dwFaceID[1];
                    }
                    else
                    {
                        pNextFace = m_pFaces + edge.dwFaceID[0];
                    }

                    auto it = std::find(unconnectedFaceList.cbegin(), unconnectedFaceList.cend(), pNextFace);
                    if (it != unconnectedFaceList.cend())
                    {
                        connectedFaceList.push_back(pNextFace);
                        unconnectedFaceList.erase(it);
                    }
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

// Change face's chart ID in unconnectedFaceList or in
// connectedFaceList.
void CIsochartMesh::
AdjustChartIDToAvoidNonmanifold(
    uint32_t* pdwFaceChartID,
    FACE_ARRAY& unconnectedFaceList,
    FACE_ARRAY& connectedFaceList,
    uint32_t dwOriginalChartID,
    uint32_t dwCandidateChartID1,
    uint32_t dwCandidateChartID2)
{
    uint32_t dwFaceNewChartID =
        (dwCandidateChartID1 == dwOriginalChartID)?
        dwCandidateChartID2: dwCandidateChartID1;

    if (unconnectedFaceList.size() > connectedFaceList.size())
    {
        for (size_t k=0; k<connectedFaceList.size(); k++)
        {
            pdwFaceChartID[connectedFaceList[k]->dwID] = dwFaceNewChartID;
        }
    }
    else
    {
        for (size_t k=0; k<unconnectedFaceList.size(); k++)
        {
            pdwFaceChartID[unconnectedFaceList[k]->dwID] = dwFaceNewChartID;
        }
    }
}

/////////////////////////////////////////////////////////////
///////////////Partition Simple Shape Methods////////////////////
/////////////////////////////////////////////////////////////
HRESULT CIsochartMesh::ProcessPlaneShape(
    bool& bPlaneShape)
{
    HRESULT hr = S_OK;
    
    uint32_t dwStandardFaceID = INVALID_FACE_ID;
    bPlaneShape = false;

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
    if (IsIMTSpecified())
    {
        return hr;
    }
#endif

    XMVECTOR axisX, axisY, axisZ;
    XMVECTOR normalDelta;
    for (uint32_t i = 0; i<m_dwFaceNumber; i++)
    {	
        if (m_baseInfo.pfFaceAreaArray[m_pFaces[i].dwIDInRootMesh]
            > ISOCHART_ZERO_EPS 
            && INVALID_FACE_ID == dwStandardFaceID)
        {
            dwStandardFaceID = i;
            axisZ = XMLoadFloat3(&m_baseInfo.pFaceNormalArray[m_pFaces[i].dwIDInRootMesh]);
        }
        
        if (INVALID_FACE_ID == dwStandardFaceID)
        {
            continue;
        }

        normalDelta = 
            XMLoadFloat3(&m_baseInfo.pFaceNormalArray[m_pFaces[i].dwIDInRootMesh]) - 
            XMLoadFloat3(&m_baseInfo.pFaceNormalArray[m_pFaces[dwStandardFaceID].dwIDInRootMesh]);
        
        if (!IsInZeroRange(XMVectorGetX(XMVector3Dot(
            normalDelta,
            normalDelta))))
        {
            if (IsInZeroRange(XMVectorGetX(XMVector3Length(
                XMLoadFloat3(m_baseInfo.pFaceNormalArray+
                m_pFaces[i].dwIDInRootMesh)))))
            {
                continue;
            }			
            return S_OK;
        }		
    }

    if (INVALID_FACE_ID == dwStandardFaceID)
    {		
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            m_pVerts[i].uv.x = 0;
            m_pVerts[i].uv.y = 0;
        }
        bPlaneShape = true;

        m_fParamStretchL2 = 0;		
        m_fParamStretchLn = 1;
        m_bIsParameterized = true;
        return S_OK;
    }

    XMVECTOR vV[3];
    ISOCHARTFACE& face = m_pFaces[dwStandardFaceID];
    for (size_t i=0; i<3; i++)
    {
        ISOCHARTVERTEX& v = m_pVerts[face.dwVertexID[i]];
        vV[i] = XMLoadFloat3(m_baseInfo.pVertPosition+v.dwIDInRootMesh);
    }

    float fMinDot = FLT_MAX;

    uint32_t dwOrgIndex = INVALID_INDEX;
    for (uint32_t i=0; i<3; i++)
    {
        axisX = vV[(i+1)%3] - vV[i];
        axisY = vV[(i+2)%3] - vV[i];

        float fDot = fabsf(XMVectorGetX(XMVector3Dot(axisX, axisY)));
        if (fMinDot > fDot)
        {
            fMinDot = fDot;
            dwOrgIndex = i;
        }
    }

    _Analysis_assume_(dwOrgIndex < 3);

    axisX = vV[(dwOrgIndex+1)%3] - vV[dwOrgIndex];
    axisY = vV[(dwOrgIndex+2)%3] - vV[dwOrgIndex];
    axisZ = XMVector3Cross(axisX, axisY);
    axisY = XMVector3Cross(axisZ, axisX);

    axisX = XMVector3Normalize(axisX);
    axisY = XMVector3Normalize(axisY);
    axisZ = XMVector3Normalize(axisZ);
    
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        normalDelta = 
            XMLoadFloat3(&m_baseInfo.pVertPosition[m_pVerts[i].dwIDInRootMesh])
            - vV[dwOrgIndex];
        m_pVerts[i].uv.x = XMVectorGetX(XMVector3Dot(normalDelta, axisX));
        m_pVerts[i].uv.y = XMVectorGetX(XMVector3Dot(normalDelta, axisY));
        
    }

    hr = OptimizeGeoLnInfiniteStretch(
        bPlaneShape);

    m_bIsParameterized = bPlaneShape;
    m_fParamStretchL2 = m_fBaseL2Stretch;
    return hr;
}

// This function is used to check the result of ProcessPlaneLikeShape, if self overlapping
// happened, just abandon the result generated by ProcessPlaneLikeShape
static bool IsSelfOverlapping(
       CIsochartMesh* pChart)
{
    auto& edgeList1 = pChart->GetEdgesList();
    ISOCHARTVERTEX* pVertList1 = pChart->GetVertexBuffer();

    if (edgeList1.size() < 1)
    {
        return false;
    }

    for (size_t jj=0; jj<edgeList1.size()-1; jj++)
    {
        ISOCHARTEDGE& edge1 = edgeList1[jj];
        const XMFLOAT2& v1 = pVertList1[edge1.dwVertexID[0]].uv;
        const XMFLOAT2& v2 = pVertList1[edge1.dwVertexID[1]].uv;
        
        for (size_t kk=jj+1; kk<edgeList1.size(); kk++)
        {
            ISOCHARTEDGE& edge2 = edgeList1[kk];

            // If the 2 edges are adjacent, skip checking
            if (edge1.dwVertexID[0] == edge2.dwVertexID[0]
            ||edge1.dwVertexID[0] == edge2.dwVertexID[1]
            ||edge1.dwVertexID[1] == edge2.dwVertexID[0]
            ||edge1.dwVertexID[1] == edge2.dwVertexID[1])
            {
                continue;
            }
            const XMFLOAT2& v3 = pVertList1[edge2.dwVertexID[0]].uv;
            const XMFLOAT2& v4 = pVertList1[edge2.dwVertexID[1]].uv;
            bool bIsIntersect = IsochartIsSegmentsIntersect(v1, v2, v3, v4);

            if (bIsIntersect) 
            {		
                ISOCHARTFACE* pFaceList1 = pChart->GetFaceBuffer();
                const CBaseMeshInfo& baseInfo = pChart->GetBaseMeshInfo();
                
                        uint32_t dwFaceRootID = 
                            pFaceList1[edge1.dwFaceID[0]].dwIDInRootMesh;
                        if (IsInZeroRange2(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                        {
                            continue;
                        }

                        if (edge1.dwFaceID[1] != INVALID_FACE_ID)
                        {
                            dwFaceRootID = 
                            pFaceList1[edge1.dwFaceID[1]].dwIDInRootMesh;
                            if (IsInZeroRange2(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                            {
                                continue;
                            }
                        }
                        dwFaceRootID = 
                            pFaceList1[edge2.dwFaceID[0]].dwIDInRootMesh;
                        if (IsInZeroRange2(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                        {
                            continue;
                        }

                        if (edge2.dwFaceID[1] != INVALID_FACE_ID)
                        {
                            dwFaceRootID = 
                            pFaceList1[edge2.dwFaceID[1]].dwIDInRootMesh;
                            if (IsInZeroRange2(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                            {
                                continue;
                            }
                        }
                
                XMVECTOR vv1 = XMLoadFloat2(&v1);
                XMVECTOR vv2 = XMLoadFloat2(&v2);
                XMVECTOR vv3 = XMLoadFloat2(&v3);
                XMVECTOR vv4 = XMLoadFloat2(&v4);

                XMVECTOR vv5 = vv1 - vv3;
                if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                vv5 = vv1 - vv4;
                if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                vv5 = vv2 - vv3;
                if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                vv5 = vv2 - vv4;
                if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                DPF(1, "(%f, %f) (%f, %f) --> (%f, %f) (%f, %f)",						
                        v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v4.x, v4.y);

                return true;
            }
        }
    }

    return false;
}


HRESULT CIsochartMesh::ProcessPlaneLikeShape(
    size_t dwCalculatedDimension,
    size_t dwPrimaryEigenDimension,
    bool& bPlaneLikeShape)
{
    HRESULT hr = S_OK;

    bPlaneLikeShape = false;

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
    if (IsIMTSpecified())
    {
        return hr;
    }
#endif

    // When processing sub-chart, only process the chart which is exactly isomorphic
    // to a plane. Otherwise, self overlapping can be easily generated.
    if (m_bIsSubChart && dwCalculatedDimension > 2)
    {
        return hr;
    }

    // Only used to expand charts whose energy centralizes into one plane.
    if (dwPrimaryEigenDimension > 2 )
    {
        return hr;
    }

    // Find one face as the standard face to expand all other faces
    uint32_t dwStandardFaceID = INVALID_FACE_ID;
    for (uint32_t i = 0; i<m_dwFaceNumber; i++)
    {
        if (m_baseInfo.pfFaceAreaArray[m_pFaces[i].dwIDInRootMesh]
            > ISOCHART_ZERO_EPS 
            && INVALID_FACE_ID == dwStandardFaceID)
        {
            dwStandardFaceID = i;
            break;
        }
    }
    if (INVALID_FACE_ID == dwStandardFaceID)
    {
        return hr;
    }

    XMVECTOR axisX, axisY, axisZ;
    XMVECTOR normalDelta;
    XMFLOAT3 v1, v2;
    XMVECTOR temp[3];

    std::queue<uint32_t> faceQueue;

    std::unique_ptr<bool[]> rgbVertProcessed( new (std::nothrow) bool[m_dwVertNumber] );
    std::unique_ptr<bool[]> rgbFaceAdded( new (std::nothrow) bool[m_dwFaceNumber] );
    if ( !rgbVertProcessed || !rgbFaceAdded)
    {
        return E_OUTOFMEMORY;
    }
    
    memset(rgbFaceAdded.get(), 0, sizeof(bool)*m_dwFaceNumber);
    memset(rgbVertProcessed.get(), 0, sizeof(bool)*m_dwVertNumber);

    // Parameterize the standard face to UV plane
    XMVECTOR vV[3];
    ISOCHARTFACE& face = m_pFaces[dwStandardFaceID];
    for (size_t i=0; i<3; i++)
    {
        ISOCHARTVERTEX& v = m_pVerts[face.dwVertexID[i]];
        vV[i] = XMLoadFloat3(m_baseInfo.pVertPosition+v.dwIDInRootMesh);
    }

    float fMinDot = FLT_MAX;
    uint32_t dwOrgIndex = INVALID_INDEX;
    // Find a vertex whose adjacent 2 edges has angle closest to PI/4
    for (uint32_t i = 0; i<3; i++)
    {
        axisX = vV[(i+1)%3] - vV[i];
        axisY = vV[(i+2)%3] - vV[i];

        axisX = XMVector3Normalize(axisX);
        axisY = XMVector3Normalize(axisY);

        float fDot = fabsf(XMVectorGetX(XMVector3Dot(axisX, axisY)));
        if (fMinDot > fDot)
        {
            fMinDot = fDot;
            dwOrgIndex = i;
        }
    }

    _Analysis_assume_(dwOrgIndex < 3);

    axisX = vV[(dwOrgIndex+1)%3] - vV[dwOrgIndex];
    axisY = vV[(dwOrgIndex+2)%3] - vV[dwOrgIndex];
    
    
    axisZ = XMVector3Cross(axisX, axisY);
    axisY = XMVector3Cross(axisZ, axisX);

    axisX = XMVector3Normalize(axisX);
    axisY = XMVector3Normalize(axisY);
    axisZ = XMVector3Normalize(axisZ);
    
    for (size_t i=0; i<3; i++)
    {
        normalDelta = 
            vV[(dwOrgIndex+i)%3]
            - vV[dwOrgIndex];	
        
        m_pVerts[face.dwVertexID[(dwOrgIndex+i)%3]].uv.x = 
            XMVectorGetX(XMVector3Dot(normalDelta, axisX));
        
        m_pVerts[face.dwVertexID[(dwOrgIndex+i)%3]].uv.y = 
            XMVectorGetX(XMVector3Dot(normalDelta, axisY));

        temp[(dwOrgIndex+i)%3] = XMVectorSet(
            m_pVerts[face.dwVertexID[(dwOrgIndex+i)%3]].uv.x,
            m_pVerts[face.dwVertexID[(dwOrgIndex+i)%3]].uv.y,
            0, 0);

        rgbVertProcessed[face.dwVertexID[(dwOrgIndex + i) % 3]] = true;
    }

    XMStoreFloat3(&v1, temp[1] - temp[0]);
    XMStoreFloat3(&v2, temp[2] - temp[0]);
    bool bPositive = (CalculateZOfVec3Cross(&v1, &v2) >= 0);

    // From iteratively lay faces adjacent to the parameterized faces onto UV plane.
    try
    {
        faceQueue.push(dwStandardFaceID);

        rgbFaceAdded[dwStandardFaceID] = true;
        while(!faceQueue.empty())
        {
            uint32_t dwFaceID = faceQueue.front();
            faceQueue.pop();
            ISOCHARTFACE& curFace = m_pFaces[dwFaceID];
            for (size_t i=0; i<3; i++)
            {
                if (!rgbVertProcessed[curFace.dwVertexID[i]])
                {
                    uint32_t vId0 = curFace.dwVertexID[(i + 1) % 3];
                    uint32_t vId1 = curFace.dwVertexID[(i + 2) % 3];
                    assert(rgbVertProcessed[vId0]);
                    assert(rgbVertProcessed[vId1]);
                
                    uint32_t vId2 = curFace.dwVertexID[i];

                    vV[0] = XMLoadFloat3(m_baseInfo.pVertPosition + m_pVerts[vId0].dwIDInRootMesh);
                    vV[1] = XMLoadFloat3(m_baseInfo.pVertPosition + m_pVerts[vId1].dwIDInRootMesh);
                    vV[2] = XMLoadFloat3(m_baseInfo.pVertPosition + m_pVerts[vId2].dwIDInRootMesh);

                    XMVECTOR vv1 = vV[1] - vV[0];
                    XMVECTOR vv2 = vV[2] - vV[0];

                    float fLen1 = XMVectorGetX(XMVector3Length(vv1));
                    float fLen2 = XMVectorGetX(XMVector3Length(vv2));
                
                    if (IsInZeroRange(fLen1))
                    {
                        return S_OK;
                    }
                    if (IsInZeroRange(fLen2))
                    {
                        m_pVerts[vId2].uv = m_pVerts[vId0].uv;	
                        rgbVertProcessed[vId2] = true;
                        break;
                    }

                    float cosB = XMVectorGetX(XMVector3Dot(vv1, vv2)) / (fLen1*fLen2);
                    if (cosB < -1.0f)
                    {
                        cosB = -1.0f;
                    }
                    if (cosB > 1.0)
                    {
                        cosB = 1.0;
                    }

                    float sinB = IsochartSqrtf(1.0f - cosB*cosB);

                    XMFLOAT2 v2D;
                    XMStoreFloat2(&v2D, XMVector2Normalize(
                        XMLoadFloat2(&m_pVerts[vId1].uv) - XMLoadFloat2(&m_pVerts[vId0].uv)));

                    float x = v2D.x*cosB - v2D.y*sinB;
                    float y = v2D.y*cosB + v2D.x*sinB;

                    temp[i] = XMVectorSet(x, y, 0, 0);
                    temp[(i+1)%3] = XMVectorSet(0, 0, 0, 0);
                    temp[(i+2)%3] = XMVectorSet(v2D.x, v2D.y, 0, 0);
                

                    XMStoreFloat3(&v1, temp[1] - temp[0]);
                    XMStoreFloat3(&v2, temp[2] - temp[0]);
                
                    bool bPositive1 = (CalculateZOfVec3Cross(&v1, &v2) >= 0);

                    if ( (bPositive && !bPositive1) 
                    ||(!bPositive && bPositive1) )
                    {
                        sinB = -sinB;
                        x = v2D.x*cosB - v2D.y*sinB;
                        y = v2D.y*cosB + v2D.x*sinB;					
                    }

                    m_pVerts[vId2].uv.x = fLen2*x + m_pVerts[vId0].uv.x;
                    m_pVerts[vId2].uv.y = fLen2*y + m_pVerts[vId0].uv.y;

                    assert(_finite(m_pVerts[vId2].uv.x) != 0 &&
                        _finite(m_pVerts[vId2].uv.y) != 0);

                    if (_finite(m_pVerts[vId2].uv.x) == 0 ||
                        _finite(m_pVerts[vId2].uv.y) == 0)
                    {
                        DPF(0, "ProcessPlaneLikeShape failed due to INFs");
                        return E_FAIL;
                    }

                    rgbVertProcessed[vId2] = true;
                    break;
                }
            }

            for (size_t i=0; i<3; i++)
            {
                ISOCHARTEDGE& edge = m_edges[curFace.dwEdgeID[i]];
                uint32_t dwAdjacent = edge.dwFaceID[0];
                if (dwAdjacent == curFace.dwID)
                {
                    dwAdjacent = edge.dwFaceID[1];
                }
                if (dwAdjacent != INVALID_FACE_ID && 
                !rgbFaceAdded[dwAdjacent])
                {
                    faceQueue.push(dwAdjacent);
                    rgbFaceAdded[dwAdjacent] = true;
                }
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

#if CHECK_OVER_LAPPING_BEFORE_OPT_INFINIT
    bool bIsOverlapping = false;
    
    bIsOverlapping = IsSelfOverlapping(this);
    if (bIsOverlapping)
    {
        DPF(1, "Generate self overlapping chart when processing plane-like chart");
        return S_OK;
    }
#endif

    hr = OptimizeGeoLnInfiniteStretch(bPlaneLikeShape);
    m_bIsParameterized = bPlaneLikeShape;
    m_fParamStretchL2 = m_fBaseL2Stretch;

    return hr;
}



HRESULT CIsochartMesh::ProcessTrivialShape(
    size_t dwPrimaryEigenDimension,
    bool& bTrivialShape)
{
    HRESULT hr = S_OK;
    bTrivialShape = true;

    // Case 2: All eigen values of isomap resulst of current chart
    // are zero, this means current chart is degenerated to a point
    if (0 == dwPrimaryEigenDimension)
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            m_pVerts[i].uv.x = m_pVerts[i].uv.y = 0;
        }
        DeleteChildren();

        m_fParamStretchL2 = 1;
        m_fChart2DArea= 0;
        return hr;
    }

    // Case 1: Only one face in current chart
    if (m_dwFaceNumber <= 1)
    {
        assert(m_dwFaceNumber ==1);
        ParameterizeOneFace(IsIMTSpecified(), m_pFaces);

        DeleteChildren();
        return hr;
    }

    bTrivialShape = false;
    return hr;
}

/////////////////////////////////////////////////////////////
///////////////Partition Special Shape Methods///////////////
/////////////////////////////////////////////////////////////
HRESULT CIsochartMesh::ProcessSpecialShape(
    size_t dwBoundaryNumber, 
    const float* pfVertGeodesicDistance,
    const float* pfVertCombineDistance,
    const float* pfVertMappingCoord, 
    size_t dwPrimaryEigenDimension, 
    size_t dwMaxEigenDimension,
    bool& bSpecialShape)
{
    UNREFERENCED_PARAMETER(pfVertCombineDistance);

    HRESULT hr = S_OK;
    bool bIsCylinder = false;
    bool bIsLonghorn = false;

#if PARAM_TURN_ON_BARYCENTRIC
    float fSmallStretch;
    CIsochartMesh::ConvertToInternalCriterion(
        SMALL_STRETCH_TO_TURNON_BARY, fSmallStretch, false);

    if (m_baseInfo.fExpectAvgL2SquaredStretch >= 
        fSmallStretch && dwBoundaryNumber == 1)
    {
        return 0;
    }
#endif

    assert(
        (IsIMTSpecified() && pfVertGeodesicDistance != pfVertCombineDistance) 
        ||(!IsIMTSpecified() && pfVertGeodesicDistance == pfVertCombineDistance));


    // 1. Detect special shape
    uint32_t dwLonghornExtremeVexID = 0;
    if (dwMaxEigenDimension < 3)
    {
        bSpecialShape = false;
        return hr;
    }

    FAILURE_RETURN(
        CheckCylinderLonghornShape(
            dwBoundaryNumber,
            bIsCylinder,
            bIsLonghorn,
            dwLonghornExtremeVexID));

    bSpecialShape = (bIsCylinder || bIsLonghorn);

    bool bIsPartitionSucceed = false;
    // 2. Partition special shape
    if (bIsCylinder)
    {
        DPF(1,"....This is a Cylinder!...\n");
        hr = PartitionCylindricalShape(
                pfVertGeodesicDistance,
                pfVertMappingCoord,
                dwPrimaryEigenDimension,
                bIsPartitionSucceed);
    }
    else if (bIsLonghorn)
    {
        DPF(1,"....This is a Longhorn!...\n");
        hr =PartitionLonghornShape(
                pfVertGeodesicDistance,
                dwLonghornExtremeVexID,
                bIsPartitionSucceed);
    }
    if (!bIsPartitionSucceed)
    {
        DeleteChildren();
    }

    return hr;
}


// Check longhorn and cylinder shape
// See more detail in section 4.5 of [Kun04]
// All constants in this function are based by Kun's
// examination
HRESULT CIsochartMesh::CheckCylinderLonghornShape(
    size_t dwBoundaryNumber, 
    bool& bIsCylinder, 
    bool& bIsLonghorn, 
    uint32_t& dwLonghornExtremeVexID) const
{
    HRESULT hr = S_OK;

    bIsCylinder = false;
    bIsLonghorn = false;

    const float* fEigenValue = m_isoMap.GetEigenValue();

    // If energy of 2th and 3th dimension is very small,
    // the chart can not be a cylinder of longhorn.
    if (IsInZeroRange(fEigenValue[1])
    ||IsInZeroRange(fEigenValue[2]))
    {
        return hr;
    }

    float fEigenRatio01 = fEigenValue[0] / fEigenValue[1];
    float fEigenRatio02 = fEigenValue[0] / fEigenValue[2];
    float fEigenRatio12 = fEigenValue[1] / fEigenValue[2];

    // Based on kun's examination
    if (m_isoMap.GetCalculatedDimension() > 3 && !IsInZeroRange(fEigenValue[3]))
    {
        float fEigenRatio03 = fEigenValue[0] / fEigenValue[3];
        float fEigenRatio23 = fEigenValue[2] / fEigenValue[3];

        if (fEigenRatio02 < 20 && fEigenRatio03 > 18 
            && fEigenRatio12< 5 && fEigenRatio23 > 2)
        {
            bIsCylinder = true;
        }
    }
    if (dwBoundaryNumber != 1)
    {
        return hr;
    }

    if (fEigenRatio01 > 10)
    {
        bIsLonghorn = true;
    }

    // Try to find the extreme vertex of cylinder or longhron.
    uint32_t dwVertexID = CaculateExtremeVertex();
    if (INVALID_VERT_ID == dwVertexID)
    {
        bIsLonghorn = bIsCylinder = false;
        return hr;
    }

    // Caculate distance from each vertex to extreme vertex
    float fMinDistance;
    float fMaxDistance;
    float fAverageDistance;
    FAILURE_RETURN(
    hr = CaculateDistanceToExtremeVertex(
        dwVertexID,
        fAverageDistance,
        fMinDistance,
        fMaxDistance));

    if (fMinDistance > fAverageDistance /2 
    && fMaxDistance < fAverageDistance *2)
    {
        dwLonghornExtremeVexID = dwVertexID;
    }
    else
    {
        bIsCylinder = bIsLonghorn = false;
    }

    if (bIsCylinder)
    {
        bIsLonghorn = false;
    }

    return hr;
}


uint32_t CIsochartMesh::
CaculateExtremeVertex() const
{
    float fMinDistance;
    uint32_t dwVertexID = INVALID_VERT_ID;
    float fMaxDistance = -FLT_MAX;

    ISOCHARTVERTEX* pVertex = m_pVerts;
    for (uint32_t i = 0; i<m_dwVertNumber; i++)
    {
        if (pVertex->bIsBoundary)
        {
            float fU = fabsf(pVertex->uv.x);
            if (fU > fMaxDistance)
            {
                dwVertexID = i;
                fMaxDistance = fU;
            }
        }
        pVertex++;
    }
    if (INVALID_VERT_ID == dwVertexID)
    {
        return INVALID_VERT_ID;
    }

    bool bIsBoundaryPositive = (m_pVerts[dwVertexID].uv.x > 0);
    dwVertexID = INVALID_VERT_ID;

    if ( bIsBoundaryPositive)
    {
        fMinDistance = FLT_MAX;
        pVertex = m_pVerts;
        for (uint32_t i = 0; i<m_dwVertNumber; i++)
        {
            if (!pVertex->bIsBoundary)
            {
                if (pVertex->uv.x < fMinDistance)
                {
                    dwVertexID = i;
                    fMinDistance = pVertex->uv.x;
                }
            }
            pVertex++;
        }
    }
    else
    {
        fMaxDistance = -FLT_MAX;
        pVertex = m_pVerts;
        for (uint32_t i = 0; i<m_dwVertNumber; i++)
        {
            if (!pVertex->bIsBoundary)
            {
                if (pVertex->uv.x > fMaxDistance)
                {
                    dwVertexID = i;
                    fMaxDistance = pVertex->uv.x;
                }
            }
            pVertex++;
        }
    }
    return dwVertexID;
}

HRESULT CIsochartMesh::
CaculateDistanceToExtremeVertex(
    uint32_t dwVertexID,
    float& fAverageDistance,
    float& fMinDistance,
    float& fMaxDistance) const
{
    HRESULT hr = S_OK;
    if (FAILED( hr = CalculateDijkstraPathToVertex(dwVertexID)))
    {
        return hr;
    }

    fMinDistance = FLT_MAX;
    fMaxDistance = -FLT_MAX;
    fAverageDistance = 0;

    size_t dwBoundaryVertexCount = 0;
    ISOCHARTVERTEX* pVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        if (pVertex->bIsBoundary)
        {
            fAverageDistance += pVertex->fGeodesicDistance;
            dwBoundaryVertexCount++;

            if (pVertex->fGeodesicDistance < fMinDistance)
            {
                fMinDistance = pVertex->fGeodesicDistance;
            }

            if (pVertex->fGeodesicDistance > fMaxDistance)
            {
                fMaxDistance = pVertex->fGeodesicDistance;
            }
        }
        pVertex++;
    }

    fAverageDistance /= dwBoundaryVertexCount;

    return hr;
}

// Parition Cylinder shape by cutting it profile into 2 parts 
HRESULT CIsochartMesh::PartitionCylindricalShape(
    const float* pfVertGeodesicDistance,
    const float* pfVertMapCoord, 
    size_t dwMapDim,
    bool& bIsPartitionSucceed)
{
    bIsPartitionSucceed = false;
    std::unique_ptr<uint32_t []> pdwFaceChartID(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    if (!pdwFaceChartID)
    {
        return E_OUTOFMEMORY;
    }

    // 1. Firstly, partition the shape according to the third principla dimension,
    // which corresponds to the shorter cyclic axis of underside
    size_t dwNegativeFaceCount = 0;
    size_t dwPossitiveFaceCount = 0;
    GroupByFaceSign(
        pfVertMapCoord,
        dwMapDim,
        2,
        dwPossitiveFaceCount,
        dwNegativeFaceCount,
        pdwFaceChartID.get());

    // 2. if partiton is not balanced, partitionning according to the second 
    // principal dimension which corresponds to the longer cyclic axis.
    if (dwPossitiveFaceCount == 0 || dwNegativeFaceCount == 0
    || dwPossitiveFaceCount/dwNegativeFaceCount > 2
    || dwNegativeFaceCount/dwPossitiveFaceCount > 2)
    {
        GroupByFaceSign(
            pfVertMapCoord,
            dwMapDim,
            1,
            dwPossitiveFaceCount,
            dwNegativeFaceCount,
            pdwFaceChartID.get());
    }

    // 3. Optimize the partition result and generate new sub-charts.
    HRESULT hr = S_OK;
    if (dwPossitiveFaceCount >0 && dwNegativeFaceCount>0 )
    {
        size_t dwMaxSubchartCount = 2;
        
        // 3.1 Smooth partition result
        if (FAILED(hr = SmoothPartitionResult(
            dwMaxSubchartCount, 
            pdwFaceChartID.get(), 
            bIsPartitionSucceed)) || !bIsPartitionSucceed)
        {
            return hr;
        }

        // 3.2 Create all sub-charts according to result of partition
        if (FAILED(hr = GenerateAllSubCharts(
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bIsPartitionSucceed)) || !bIsPartitionSucceed)
        {
            return hr;
        }

        // 3.3 Using graph cut to optimize boundary
        bool bOptimized = false;

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
        if (FAILED(hr = OptimizeBoundaryByStretch(
            pfVertCombineDistance,
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bOptimized)) || !bOptimized)
        {
            return hr;
        }

#else
        if (FAILED(hr = OptimizeBoundaryByStretch(
            pfVertGeodesicDistance,
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bOptimized)) || !bOptimized)
        {
            return hr;
        }
#endif
        
        // 3.4 Using the result of boundary optimization to Genearte sub-charts again
        hr = GenerateAllSubCharts(
            pdwFaceChartID.get(), 
            dwMaxSubchartCount,
            bIsPartitionSucceed);
    }

    return hr;
}

// Compute the sum of face vertices' dwComputeDimension 
// coordinates. classify the faces by sign of the sum.
void CIsochartMesh::
GroupByFaceSign(
    const float* pfVertMapCoord,
    size_t dwMapDimension,
    size_t dwComputeDimension,
    size_t& dwPossitiveFaceCount,
    size_t& dwNegativeFaceCount,
    uint32_t* pdwFaceChartID)
{
    dwPossitiveFaceCount = 0;
    dwNegativeFaceCount = 0;

    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        float fSumOfZ = 0;
        for (size_t j=0; j<3; j++)
        {
            fSumOfZ += 
                pfVertMapCoord[pFace->dwVertexID[j]*dwMapDimension
                + dwComputeDimension];
        }
        if (fSumOfZ < 0)
        {
            pdwFaceChartID[i] = 0;
            dwNegativeFaceCount++;
        }
        else
        {
            pdwFaceChartID[i] = 1;
            dwPossitiveFaceCount++;
        }
        pFace++;
    }
}

// Partition longhorn shape, the 1-ring neighborhood faces
// of extreme vertex belong to one chart, other faces belong
// to another chart
HRESULT CIsochartMesh::PartitionLonghornShape(
    const float* pfVertGeodesicDistance,
    uint32_t dwLonghornExtremeVexID,
    bool& bIsPartitionSucceed)
{
    bIsPartitionSucceed = false;

    std::unique_ptr<uint32_t []> pdwFaceChartID(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    if (!pdwFaceChartID)
    {
        return E_OUTOFMEMORY;
    }

    // 1. faces adjacent to extrem vertex will be partitioned as one 
    // sub-chart.other faces will be partitioned as another sub-chart.
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        pdwFaceChartID[i] = 1;
    }

    ISOCHARTVERTEX* pExtremeVertex = m_pVerts + dwLonghornExtremeVexID;
    for (size_t i=0; i<pExtremeVertex->faceAdjacent.size(); i++)
    {
        pdwFaceChartID[pExtremeVertex->faceAdjacent[i]] = 0;
    }

    // 2. Smooth partition result
    size_t dwMaxSubchartCount = 2;
    HRESULT hr = MakePartitionValid(
        dwMaxSubchartCount, 
        pdwFaceChartID.get(), 
        bIsPartitionSucceed);
    if ( FAILED(hr) || !bIsPartitionSucceed)
    {
        return hr;
    }

    // 3. Create all sub-charts according to result of partition
    if (FAILED(hr = GenerateAllSubCharts(
        pdwFaceChartID.get(),
        dwMaxSubchartCount,
        bIsPartitionSucceed)) || !bIsPartitionSucceed)
    {
        return hr;
    }
    
    // 4. Using graph cut to optimze cut boundary.
    bool bOptimized = false;

#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
    if (FAILED(hr = OptimizeBoundaryByStretch(
        pfVertCombineDistance,
        pdwFaceChartID.get(),
        dwMaxSubchartCount,
        bOptimized)) || !bOptimized)
    {
        return hr;
    }
#else
    if (FAILED(hr = OptimizeBoundaryByStretch(
        pfVertGeodesicDistance,
        pdwFaceChartID.get(),
        dwMaxSubchartCount,
        bOptimized)) || !bOptimized)
    {
        return hr;
    }
#endif
    // 5. Using the result of boundary optimization to Genearte sub-charts again
    return GenerateAllSubCharts(
        pdwFaceChartID.get(), 
        dwMaxSubchartCount,
        bIsPartitionSucceed);
}

//////////////////////////////////////////////////////////////
/////////////////Partition General Shape Methods//////////////
//////////////////////////////////////////////////////////////

// See more detail in section 4.3 of [Kun04]
HRESULT CIsochartMesh::ProcessGeneralShape(
    size_t dwPrimaryEigenDimension,
    size_t dwBoundaryNumber,
    const float* pfVertGeodesicDistance,
    const float* pfVertCombineDistance,
    const float* pfVertMappingCoord)
{
    HRESULT hr = S_OK;

    assert(m_children.empty());

    assert(
        (IsIMTSpecified() && pfVertGeodesicDistance != pfVertCombineDistance) 
        ||(!IsIMTSpecified() && pfVertGeodesicDistance == pfVertCombineDistance));

    // 1. If dwPrimaryEigenDimension is small enough, The algorithm of
    // stretch optimization can work well. So, optimize the Initial
    // parameterization, if the chart satisfied the stretch restriction,
    // stop partitioning it.
    if (dwBoundaryNumber == 1 && dwPrimaryEigenDimension < 4)
    {
        bool bIsOverlapping = false;

#if CHECK_OVER_LAPPING_BEFORE_OPT_INFINIT
        FAILURE_RETURN(
            IsParameterizationOverlapping(this, bIsOverlapping));
#endif

        if (!bIsOverlapping)
        {
            bool bSucceed = false;
            FAILURE_RETURN(OptimizeGeoLnInfiniteStretch(bSucceed));
            if (bSucceed)
            {
                FAILURE_RETURN(ReserveFarestTwoLandmarks(pfVertGeodesicDistance));
                return hr;
            }
        }
    }

    float fSmallStretch;
#if PARAM_TURN_ON_LSCM
    CIsochartMesh::ConvertToInternalCriterion(
        SMALL_STRETCH_TO_TURNON_LSCM, fSmallStretch, false);

    if (dwBoundaryNumber == 1 &&
        m_baseInfo.fExpectAvgL2SquaredStretch >= fSmallStretch
        && dwPrimaryEigenDimension < 4)
    {
        bool bIsOverLap = true;	
        bIsOverLap = true;
        FAILURE_RETURN(LSCMParameterization(bIsOverLap));
        if (!bIsOverLap) return hr;			
    }
#endif

#if PARAM_TURN_ON_BARYCENTRIC
    CIsochartMesh::ConvertToInternalCriterion(
        SMALL_STRETCH_TO_TURNON_BARY, fSmallStretch, false);
    if (dwBoundaryNumber == 1 &&
        m_baseInfo.fExpectAvgL2SquaredStretch >= fSmallStretch)
    {
        bool bIsOverLap = true;

        bIsOverLap = true;
        FAILURE_RETURN(BarycentricParameterization(bIsOverLap));
        if (!bIsOverLap) return hr;
    }
#endif

    

    // 2. General spectral clustering, Compute representative vertices
    std::vector<uint32_t> representativeVertsIdx;
    FAILURE_RETURN(CalculateRepresentiveVertices(
        representativeVertsIdx,
        dwPrimaryEigenDimension,
        pfVertMappingCoord));
    
    if (m_bIsSubChart)
    {
        try
        {
            representativeVertsIdx.resize(2);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        FAILURE_RETURN(
            RemoveCloseRepresentiveVertices(
                representativeVertsIdx,
                dwPrimaryEigenDimension,
                pfVertGeodesicDistance));
    }

    // 4. Patition General shape....
    bool bIsPartitionSucceed = false;

    FAILURE_RETURN(
        PartitionGeneralShape(
            pfVertGeodesicDistance,
            pfVertCombineDistance,
            representativeVertsIdx,
            false,
            bIsPartitionSucceed));

    if (bIsPartitionSucceed && m_children.size() > 1)
    {
        return S_OK;
    }

    // 5. if Failed to partition, just partitioning each face.
    //assert(!bIsPartitionSucceed);
    hr = PartitionEachFace();
    return hr;
}

// Calculate representative vertices in landmark. These
// vertices will beused to cluster other vertices.
HRESULT CIsochartMesh::CalculateRepresentiveVertices(
    std::vector<uint32_t>& representativeVertsIdx,
    size_t dwPrimaryEigenDimension,
    const float* pfVertMappingCoord)
{
    representativeVertsIdx.clear();
#if BIPARTITION
    for (size_t dwDimIndex=0;
        dwDimIndex<dwPrimaryEigenDimension;
        dwDimIndex++)
    {
        float fMaxDist, fMinDist;
        fMaxDist = -FLT_MAX;
        fMinDist = FLT_MAX;
        uint32_t vi = INVALID_INDEX;
        uint32_t vj = INVALID_INDEX;

        for (size_t i=0; i<m_landmarkVerts.size(); i++)
        {
            float fCoord = pfVertMappingCoord[
                dwPrimaryEigenDimension*m_landmarkVerts[i] + dwDimIndex];

            if (fCoord> fMaxDist)
            {
                vi = i;
                fMaxDist = fCoord;
            }
            if (fCoord < fMinDist)
            {
                vj = i;
                fMinDist = fCoord;
            }
        }

        if (vi == INVALID_VERT_ID ||vj == INVALID_VERT_ID)
        {
            continue;
        }

        else
        {
            if (!representativeVertsIdx.addNoduplicateItem(vi))
            {
                return E_OUTOFMEMORY;
            }

            if (!representativeVertsIdx.addNoduplicateItem(vj))
            {
                return E_OUTOFMEMORY;
            }
            break;
        }
    }

#else
    for (size_t dwDimIndex=0;
        dwDimIndex<dwPrimaryEigenDimension;
        dwDimIndex++)
    {
        float fMaxDist, fMinDist;
        fMaxDist = -FLT_MAX;
        fMinDist = FLT_MAX;
        uint32_t vi = INVALID_INDEX;
        uint32_t vj = INVALID_INDEX;

        for (uint32_t i=0; i<m_landmarkVerts.size(); i++)
        {
            float fCoord = pfVertMappingCoord[
                dwPrimaryEigenDimension*m_landmarkVerts[i] + dwDimIndex];

            if (fCoord> fMaxDist)
            {
                vi = i;
                fMaxDist = fCoord;
            }
            if (fCoord < fMinDist)
            {
                vj = i;
                fMinDist = fCoord;
            }
        }

        if (vi == INVALID_VERT_ID ||vj == INVALID_VERT_ID)
        {
            continue;
        }

        if (!addNoduplicateItem(representativeVertsIdx,vi))
        {
            return E_OUTOFMEMORY;
        }

        if (!addNoduplicateItem(representativeVertsIdx,vj))
        {
            return E_OUTOFMEMORY;
        }
    }
    
#endif

    return S_OK;
}

// The representative vertices gotten by CalculateRepresentiveVertices
// may be too closei. This function remove the vertices that are too
// close.
HRESULT CIsochartMesh::RemoveCloseRepresentiveVertices(
    std::vector<uint32_t>& representativeVertsIdx,
    size_t dwPrimaryEigenDimension,
    const float* pfVertGeodesicDistance)
{
    float fAvgChartRadius;
    size_t i;

    fAvgChartRadius
        = IsochartSqrtf(m_fChart3DArea/ (dwPrimaryEigenDimension+1));

    float fMaxDist;
    uint32_t dwMaxIndex;

    // Algorithm of computing the distance of 2 vertices set.
    for (i=2; i< representativeVertsIdx.size(); i++)
    {
        fMaxDist = 0;
        dwMaxIndex = INVALID_INDEX;

        for (size_t j=i; j<representativeVertsIdx.size(); j++)
        {
            float fMinDist = FLT_MAX;
            for (size_t k=0; k<i; k++)
            {
                uint32_t index = static_cast<uint32_t>(
                    representativeVertsIdx[k] * m_dwVertNumber 
                    + m_landmarkVerts[representativeVertsIdx[j]]);

                if (pfVertGeodesicDistance[index] < fMinDist)
                {
                    fMinDist = pfVertGeodesicDistance[index];
                }
            }

            if (fMinDist > fMaxDist)
            {
                fMaxDist = fMinDist;
                dwMaxIndex = static_cast<uint32_t>(j);
            }
        }

        if (fMaxDist < fAvgChartRadius)
        {
            break;
        }

        // Move the redundant vertices to the end of representativeVertsIdx.
        std::swap(representativeVertsIdx[i],representativeVertsIdx[dwMaxIndex]);
    }

    // Cut off the redundant vertices.
    try
    {
        representativeVertsIdx.resize(i);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;

}

HRESULT CIsochartMesh::GetMainRepresentive(
    std::vector<uint32_t>& representativeVertsIdx,
    size_t dwNumber,
    const float* pfVertGeodesicDistance)
{
    assert(pfVertGeodesicDistance != 0);
    assert(dwNumber >= 2);
    assert(representativeVertsIdx.size() >= 2);

    if (representativeVertsIdx.size() <= dwNumber)
    {
        return S_OK;
    }
    
    for (size_t i=2; i<dwNumber; i++)
    {
        float fMaxTotalDistance = -FLT_MAX;
        uint32_t dwSeletedVert = INVALID_VERT_ID;
        for (size_t j=i; j<representativeVertsIdx.size(); j++)
        {
            float fTotalDistance = 0;
            for (size_t k=0; k<i; k++)
            {
                uint32_t dwIdx = static_cast<uint32_t>(
                    representativeVertsIdx[k]*m_dwVertNumber +
                    m_landmarkVerts[representativeVertsIdx[j]]);
                
                fTotalDistance  += 
                    pfVertGeodesicDistance[dwIdx];
            }
            if (fTotalDistance > fMaxTotalDistance)
            {
                fMaxTotalDistance = fTotalDistance;
                dwSeletedVert = static_cast<uint32_t>(j);
            }
        }

        std::swap(representativeVertsIdx[i],representativeVertsIdx[dwSeletedVert]);
    }

    try
    {
        representativeVertsIdx.resize(dwNumber);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    
    return S_OK;
}

HRESULT CIsochartMesh::PartitionGeneralShape(
    const float* pfVertGeodesicDistance,	
    const float* pfVertCombineDistance, 
    std::vector<uint32_t>& representativeVertsIdx,
    const bool bOptSubBoundaryByAngle,
    bool& bIsPartitionSucceed)
{
    DPF(3, "Partition General shape...\n");
    bIsPartitionSucceed = false;

    // Only one representative vertex, no need to cluster vertices.
    if (representativeVertsIdx.size() < 2)
    {
        return S_OK;
    }

    std::unique_ptr<uint32_t []> pdwFaceChartID(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    if (!pdwFaceChartID)
    {
        return E_OUTOFMEMORY;
    }

    // 1. Partition the chart into representativeVertsIdx.size() 
    // parts by growing charts simultaneously around the representatives
    ClusterFacesByParameterDistance(
        pdwFaceChartID.get(),
        pfVertCombineDistance,
        representativeVertsIdx);

    // 2.Smooth parititon result
    size_t dwMaxSubchartCount = representativeVertsIdx.size();

    HRESULT hr = SmoothPartitionResult(
        dwMaxSubchartCount,
        pdwFaceChartID.get(),
        bIsPartitionSucceed);
    if (FAILED(hr) || !bIsPartitionSucceed)
    {
        return hr;
    }

    // 3. boundary optimization
    if (FAILED(hr = GenerateAllSubCharts(
        pdwFaceChartID.get(),
        dwMaxSubchartCount,
        bIsPartitionSucceed)) || !bIsPartitionSucceed)
    {
        return hr;
    }

    bool bIsOptimized = false;

    if (!m_bIsSubChart || bOptSubBoundaryByAngle)
    {
        hr = OptimizeBoundaryByAngle(
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bIsOptimized);
    }	
    else
    {
#if USING_COMBINED_DISTANCE_TO_PARAMETERIZE
        hr = OptimizeBoundaryByStretch(
            pfVertCombineDistance, 
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bIsOptimized);
#else
        hr = OptimizeBoundaryByStretch(
            pfVertGeodesicDistance, 
            pdwFaceChartID.get(),
            dwMaxSubchartCount,
            bIsOptimized);
#endif
    }

    if (FAILED(hr) || !bIsOptimized)
    {
        return hr;
    }

    return GenerateAllSubCharts(
        pdwFaceChartID.get(),
        dwMaxSubchartCount,
        bIsPartitionSucceed);
}

void CIsochartMesh::ClusterFacesByParameterDistance(
    uint32_t* pdwFaceChartID,
    const float* pfVertParitionDistance,
    std::vector<uint32_t>& representativeVertsIdx)
{
    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        float fMinDistance = FLT_MAX;
        pdwFaceChartID[i] = INVALID_INDEX;
        
        for (uint32_t j = 0; j<representativeVertsIdx.size(); j++)
        {
            const float *pfParameterDistance
                = pfVertParitionDistance
                + m_dwVertNumber*representativeVertsIdx[j];
            
            float fDistance = pfParameterDistance[pFace->dwVertexID[0]]
                    + pfParameterDistance[pFace->dwVertexID[1]]
                    + pfParameterDistance[pFace->dwVertexID[2]];
            if (fDistance < fMinDistance)
            {
                pdwFaceChartID[i] = j;
                fMinDistance = fDistance;
            }
        }
        assert(pdwFaceChartID[i] != INVALID_INDEX);
        pFace++;
    }
}

// For each face, creat a sub-chart.
HRESULT CIsochartMesh::PartitionEachFace()
{
    DPF(3, "Partition each face...\n");

    HRESULT hr = S_OK;
    bool bMainfold = true;

    if (m_dwFaceNumber < 1)
    {
        return hr;
    }

    DeleteChildren();
    std::vector<uint32_t> chartFaceList;

    try
    {
        chartFaceList.resize(1);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    for (uint32_t i = 0; i<m_dwFaceNumber; i++)
    {
        chartFaceList[0] = i;
        hr = BuildSubChart(chartFaceList, bMainfold);
        assert(bMainfold);
        if (FAILED(hr))
        {
            return hr;
        }
    }
    return hr;
}
/////////////////////////////////////////////////////////////
/////////////////Bipartition chart functions/////////////////
/////////////////////////////////////////////////////////////

// This function is used when partition by number.
HRESULT CIsochartMesh::BiPartitionParameterlizeShape(
    const float* pfVertCombineDistance,
    std::vector<uint32_t>& representativeVertsIdx)
{
    std::unique_ptr<uint32_t []> pdwFaceChartID(new (std::nothrow) uint32_t[m_dwFaceNumber]);
    if (!pdwFaceChartID)
    {
        return E_OUTOFMEMORY;
    }

    // 1. Cluster faces to initialize partition
    ClusterFacesByParameterDistance(
        pdwFaceChartID.get(),
        pfVertCombineDistance,
        representativeVertsIdx);

    // 2. Optimize partition
    bool bIsOptimized;
    size_t dwMaxSubchartCount = 2;

    HRESULT hr = SmoothPartitionResult(
            dwMaxSubchartCount,
            pdwFaceChartID.get(),
            bIsOptimized);
    if (FAILED(hr)|| !bIsOptimized)
    {
        return hr;
    }

    if(FAILED(hr = GenerateAllSubCharts(
        pdwFaceChartID.get(), 
        dwMaxSubchartCount, 
        bIsOptimized)) || !bIsOptimized || m_children.size() < 2)
    {
        return hr;
    }

    if (FAILED(hr = OptimizeBoundaryByAngle(
        pdwFaceChartID.get(), 
        dwMaxSubchartCount,
        bIsOptimized)))
    {
        return hr;
    }

    // Restore pdwFaceChartID to the content before boundary opitimization
    if (!bIsOptimized)
    {
        for (uint32_t i=0; i<m_children.size(); i++)
        {
            ISOCHARTFACE* pFace = m_children[i]->m_pFaces;
            for (size_t j=0; j<m_children[i]->m_dwFaceNumber; j++)
            {
                pdwFaceChartID[pFace->dwIDInFatherMesh] = i;
                pFace++;
            }
        }
    }
    
    // 3. Above method sometimes may cause non-manifold,
    // or may generate non-simple sub-charts, which contain
    // multiple objects, InsureBiPartition sloves these
    // problems by finding a cut path only has 2 boundary vertices.
    if (FAILED(hr = InsureBiPartition(pdwFaceChartID.get())))
    {
        return hr;
    }

    // 4. Generate all sub charts
    if(FAILED(hr = GenerateAllSubCharts(
        pdwFaceChartID.get(), 
        dwMaxSubchartCount, 
        bIsOptimized)))
    {
        return hr;
    }
    assert(bIsOptimized);

    // 5. Using old parameterlization value
    for (size_t ii=0; ii < m_children.size(); ii++)
    {
        CIsochartMesh* pSubChart = m_children[ii];
        assert(pSubChart != 0);

        ISOCHARTVERTEX* pNewVertex = pSubChart->m_pVerts;
        ISOCHARTVERTEX* pOldVertex;
        for (size_t jj=0; jj<pSubChart->m_dwVertNumber; jj++)
        {
            pOldVertex = m_pVerts + pNewVertex->dwIDInFatherMesh;
            pNewVertex->uv = pOldVertex->uv;
            pNewVertex++;
        }

        pSubChart->m_bIsParameterized = true;
    }

    return S_OK;
}

HRESULT CIsochartMesh::InsureBiPartition(
    uint32_t* pdwFaceChartID)
{
    HRESULT hr = S_OK;
    EDGE_ARRAY internalEdgeList;
    EDGE_ARRAY marginalEdgeList;
    
    // 1. Find all edges whose side faces belong to
    // different sub-chart
    FAILURE_RETURN(
        FindWatershed(
            pdwFaceChartID,
            internalEdgeList,
            marginalEdgeList));

    // If NO cut path exists, for example each face belongs 
    // to the same sub-chart then, don't partition this chart
    if (marginalEdgeList.empty())
    {
        return hr;
    }

    // 2. Get a cut path which insure the sub-charts will be
    // simple charts
    EDGE_ARRAY cutPath;
    FAILURE_RETURN(
        GetMaxLengthCutPathsInWatershed(
            internalEdgeList,
            marginalEdgeList,
            cutPath));

    // 3. Decide the faces' chart ID according to cut path.
    hr = GrowPartitionFromCutPath(
        cutPath, pdwFaceChartID);

    return hr;
}

HRESULT CIsochartMesh::FindWatershed(
    const uint32_t* pdwFaceChartID,
    EDGE_ARRAY& internalEdgeList,
    EDGE_ARRAY& marginalEdgeList)
{
    try
    {
        for (size_t ii = 0; ii < m_dwEdgeNumber; ii++)
        {
            ISOCHARTEDGE& edge = m_edges[ii];
            if (edge.bIsBoundary)
            {
                continue;
            }

            assert(edge.dwFaceID[1] != INVALID_FACE_ID);
            if (pdwFaceChartID[edge.dwFaceID[0]]
                != pdwFaceChartID[edge.dwFaceID[1]])
            {
                if (m_pVerts[edge.dwVertexID[0]].bIsBoundary
                    || m_pVerts[edge.dwVertexID[1]].bIsBoundary)
                {
                    marginalEdgeList.push_back(&edge);
                }
                else
                {
                    internalEdgeList.push_back(&edge);
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

HRESULT CIsochartMesh::GetMaxLengthCutPathsInWatershed(
    EDGE_ARRAY& internalEdgeList,
    EDGE_ARRAY& marginalEdgeList,
    EDGE_ARRAY& cutPath)
{
    HRESULT hr = S_OK;
    std::vector< EDGE_ARRAY* > pathList;

    float fMaxPathLength = -FLT_MAX;
    uint32_t dwMaxLengthPathID = INVALID_INDEX;

    while(!marginalEdgeList.empty())
    {
        ISOCHARTEDGE* pStartEdge = marginalEdgeList[0];

        marginalEdgeList.erase(marginalEdgeList.begin());
        auto path = new (std::nothrow) EDGE_ARRAY;
        if (!path)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }

        try
        {
            pathList.push_back(path);
        }
        catch (std::bad_alloc&)
        {
            delete path;
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }

        uint32_t dwStartVertexID = INVALID_VERT_ID;
        uint32_t dwNextVertexID = INVALID_VERT_ID;
        uint32_t dwEndVertexID = INVALID_VERT_ID;

        if (m_pVerts[pStartEdge->dwVertexID[0]].bIsBoundary)
        {
            dwStartVertexID =  pStartEdge->dwVertexID[0];
            dwNextVertexID =  pStartEdge->dwVertexID[1];
        }
        else
        {
            dwStartVertexID =  pStartEdge->dwVertexID[1];
            dwNextVertexID =  pStartEdge->dwVertexID[0];
        }

        try
        {
            path->push_back(pStartEdge);

            float fCurrentPathLength = pStartEdge->fLength;

            if (m_pVerts[pStartEdge->dwVertexID[0]].bIsBoundary
                &&m_pVerts[pStartEdge->dwVertexID[1]].bIsBoundary)
            {
                dwEndVertexID = dwNextVertexID;
            }

            while (dwEndVertexID == INVALID_VERT_ID
                && !(marginalEdgeList.empty() && internalEdgeList.empty()))
            {
                ISOCHARTEDGE* pEndEdge = nullptr;

                for (size_t ii = 0; ii < marginalEdgeList.size(); ii++)
                {
                    pEndEdge = marginalEdgeList[ii];

                    if (pEndEdge->dwVertexID[0] == dwNextVertexID)
                    {
                        dwEndVertexID = pEndEdge->dwVertexID[1];
                        marginalEdgeList.erase(marginalEdgeList.begin() + ii);
                        break;
                    }

                    if (pEndEdge->dwVertexID[1] == dwNextVertexID)
                    {
                        dwEndVertexID = pEndEdge->dwVertexID[0];
                        marginalEdgeList.erase(marginalEdgeList.begin() + ii);
                        break;
                    }
                }

                if (dwEndVertexID != INVALID_VERT_ID)
                {
                    path->push_back(pEndEdge);
                    break;
                }

                for (size_t ii = 0; ii < internalEdgeList.size(); ii++)
                {
                    ISOCHARTEDGE* pMiddleEdge = internalEdgeList[ii];
                    if (pMiddleEdge->dwVertexID[0] == dwNextVertexID)
                    {
                        dwNextVertexID = pMiddleEdge->dwVertexID[1];
                        internalEdgeList.erase(internalEdgeList.begin() + ii);
                        fCurrentPathLength += pMiddleEdge->fLength;
                        path->push_back(pMiddleEdge);
                        break;
                    }

                    if (pMiddleEdge->dwVertexID[1] == dwNextVertexID)
                    {
                        dwNextVertexID = pMiddleEdge->dwVertexID[0];
                        internalEdgeList.erase(internalEdgeList.begin() + ii);
                        fCurrentPathLength += pMiddleEdge->fLength;
                        path->push_back(pMiddleEdge);
                        break;
                    }
                }
                assert(dwNextVertexID != INVALID_VERT_ID);
            }

            assert(dwEndVertexID != INVALID_VERT_ID);

            if (fCurrentPathLength > fMaxPathLength)
            {
                dwMaxLengthPathID = static_cast<uint32_t>(pathList.size() - 1);
                fMaxPathLength = fCurrentPathLength;
            }
        }
        catch (std::bad_alloc&)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }
    }

    assert(dwMaxLengthPathID != INVALID_INDEX);

    try
    {
        cutPath.insert(cutPath.end(), pathList[dwMaxLengthPathID]->cbegin(), pathList[dwMaxLengthPathID]->cend());
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

LEnd:
    for (size_t ii = 0; ii < pathList.size(); ii++)
    {
        auto pPath = pathList[ii];
        delete pPath;
    }
    return hr;
}

// Know cut path and faces' chart ID along the path,
// using a growing queue to decide all other faces' chart ID.
HRESULT CIsochartMesh::GrowPartitionFromCutPath(
    EDGE_ARRAY& cutPath,
    uint32_t* pdwFaceChartID)
{
    std::unique_ptr<bool[]> bMask( new (std::nothrow) bool[m_dwFaceNumber] );
    if (!bMask)
    {
        return E_OUTOFMEMORY;
    }
    memset(bMask.get(), 0, sizeof(bool) * m_dwFaceNumber);

    try
    {
        std::queue<uint32_t> faceQueue;
        for (size_t ii=0; ii<cutPath.size(); ii++)
        {
            ISOCHARTEDGE* pEdge = cutPath[ii];
            bMask[pEdge->dwFaceID[0]] = true;
            bMask[pEdge->dwFaceID[1]] = true;
            faceQueue.push(pEdge->dwFaceID[0]);
            faceQueue.push(pEdge->dwFaceID[1]);
        }

        while(!faceQueue.empty())
        {
            uint32_t dwFaceID = faceQueue.front();
            faceQueue.pop();
            ISOCHARTFACE& face = m_pFaces[dwFaceID];
            for (size_t ii =0; ii < 3; ii++)
            {
                ISOCHARTEDGE& edge = m_edges[face.dwEdgeID[ii]];
                if (edge.bIsBoundary)
                {
                    continue;
                }
            
                uint32_t dwAdjacentFaceID = 0;
                if (edge.dwFaceID[0] == dwFaceID)
                {
                    dwAdjacentFaceID = edge.dwFaceID[1];
                }
                else
                {
                    dwAdjacentFaceID = edge.dwFaceID[0];
                }

                if (!bMask[dwAdjacentFaceID])
                {
                    pdwFaceChartID[dwAdjacentFaceID]
                        = pdwFaceChartID[dwFaceID];
                    bMask[dwAdjacentFaceID] = true;

                    faceQueue.push(dwAdjacentFaceID);
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

HRESULT CIsochartMesh::ReserveFarestTwoLandmarks(
    const float* pfVertGeodesicDistance)
{
    assert(pfVertGeodesicDistance != 0);
    HRESULT hr = S_OK;
    m_bOrderedLandmark = true;
    if (m_landmarkVerts.size() <3)
    {
        return hr;
    }

    float fMaxDistance = -FLT_MAX;
    uint32_t dwIdx[2] = { 0 };
    for (uint32_t ii = 0; ii<m_landmarkVerts.size() - 1; ii++)
    {
        for (uint32_t jj=ii+1; jj<m_landmarkVerts.size(); jj++)
        {
            assert(
                pfVertGeodesicDistance[ii*m_dwVertNumber+m_landmarkVerts[jj]] == 
                pfVertGeodesicDistance[jj*m_dwVertNumber+m_landmarkVerts[ii]]);
        
            if (pfVertGeodesicDistance[ii*m_dwVertNumber+m_landmarkVerts[jj]] 
                > fMaxDistance)
            {
                fMaxDistance = 
                    pfVertGeodesicDistance[ii*m_dwVertNumber+m_landmarkVerts[jj]];
                dwIdx[0] = ii;
                dwIdx[1] = jj;
            }
        }
    }

    FAILURE_RETURN(
        MoveTwoValueToHead(m_landmarkVerts, dwIdx[0], dwIdx[1]));

    return hr;
}
