//-------------------------------------------------------------------------------------
// UVAtlas - meshapplyisomap.cpp
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

#include "geodesics\ExactOneToAll.h"
#include "geodesics\ApproximateOneToAll.h"
#include "geodesics\mathutils.h"

using namespace Isochart;
using namespace GeodesicDist ;
using namespace DirectX;

// define the macro to 1 to use the exact algorithm, otherwise the fast approximate algorithm is employed    
#if _USE_EXACT_ALGORITHM
#define ONE_TO_ALL_ENGINE m_ExactOneToAllEngine
#else
#define ONE_TO_ALL_ENGINE m_ApproximateOneToAllEngine
#endif

namespace
{
    // face number limit, below this limit, new geodesic algorithm is used, otherwise the old KS98 is used
    const size_t LIMIT_FACENUM_USENEWGEODIST = _LIMIT_FACENUM_USENEWGEODIST;

    // Used to combine geodesic and signal distance. See [Kun04], 6 section.
    const float SIGNAL_DISTANCE_WEIGHT = 0.30f;
}

/////////////////////////////////////////////////////////////
///////////////Isomap Processing Methods/////////////////////
/////////////////////////////////////////////////////////////

// Sort vertices by importance order
// Vertices with higher importance are selected as landmark
// See more detail in section 5 of [Kun04]
HRESULT CIsochartMesh::CalculateLandmarkVertices(
    size_t dwMinLandmarkNumber, 
    size_t& dwLandmarkNumber)
{
    assert(m_pVerts != 0);
    assert(m_bVertImportanceDone);

    std::unique_ptr<uint32_t []> landmark(new (std::nothrow) uint32_t[m_dwVertNumber]);
    if (!landmark)
    {
        return E_OUTOFMEMORY;
    }

    uint32_t *pdwLandmark = landmark.get();

    for (uint32_t i=0; i<m_dwVertNumber; i++)
    {
        pdwLandmark[i] = i;
    }

    // 1. Sort vertices by importance order
    if (m_dwVertNumber > dwMinLandmarkNumber)
    {
        dwLandmarkNumber =0;

        for (size_t i=0; i<m_dwVertNumber-1; i++)
        {
            ISOCHARTVERTEX* pVertex1 = m_pVerts + pdwLandmark[i];

            if (pVertex1->nImportanceOrder != MUST_RESERVE)
            {
                int nCurrentMax = pVertex1->nImportanceOrder;
                for (size_t j=i+1; j<m_dwVertNumber; j++)
                {
                    ISOCHARTVERTEX* pVertex2 = m_pVerts + pdwLandmark[j];

                    if (pVertex2->nImportanceOrder == MUST_RESERVE
                    ||nCurrentMax < pVertex2->nImportanceOrder)
                    {
                        nCurrentMax = pVertex2->nImportanceOrder;
                        std::swap(pdwLandmark[i],pdwLandmark[j]);
                    }

                    if (pVertex2->nImportanceOrder == MUST_RESERVE)
                    {
                        break;
                    }
                }
            }

            dwLandmarkNumber++;
            
            // if we have found enough landmark, stop iteration.
            if (m_pVerts[pdwLandmark[dwLandmarkNumber-1]].nImportanceOrder > 0
            && dwLandmarkNumber >= dwMinLandmarkNumber
            && dwLandmarkNumber > 2
            && m_pVerts[pdwLandmark[dwLandmarkNumber-1]].nImportanceOrder
            != m_pVerts[pdwLandmark[dwLandmarkNumber-2]].nImportanceOrder)
            {
                break;
            }
        }
    }
    else
    {
        dwLandmarkNumber = m_dwVertNumber;
    }

    DPF(1, "total landmark count is %Iu", dwLandmarkNumber);
    
    // 2. Get landmark
    m_landmarkVerts.clear();
    try
    {
        m_landmarkVerts.resize(dwLandmarkNumber);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        m_pVerts[i].bIsLandmark = false;
    }

    for (size_t i=0; i<dwLandmarkNumber; i++)
    {
        m_pVerts[pdwLandmark[i]].bIsLandmark = true;
        m_landmarkVerts[i] = pdwLandmark[i];
    }

    return S_OK;
}

// init structures used in CExactOneToAll or CApproximateOneToAll
HRESULT CIsochartMesh::InitOneToAllEngine()
{
    ONE_TO_ALL_ENGINE.m_VertexList.clear() ;
    ONE_TO_ALL_ENGINE.m_EdgeList.clear() ;
    ONE_TO_ALL_ENGINE.m_FaceList.clear() ;

    try
    {
        ONE_TO_ALL_ENGINE.m_VertexList.resize(m_dwVertNumber);
        ONE_TO_ALL_ENGINE.m_EdgeList.resize(m_dwEdgeNumber);
        ONE_TO_ALL_ENGINE.m_FaceList.resize(m_dwFaceNumber);

        // init vertex list in ONE_TO_ALL_ENGINE
        for (size_t i = 0; i < m_dwVertNumber; ++i)
        {
            Vertex &thisVertex = ONE_TO_ALL_ENGINE.m_VertexList[i];

            thisVertex.x = m_baseInfo.pVertPosition[m_pVerts[i].dwIDInRootMesh].x;
            thisVertex.y = m_baseInfo.pVertPosition[m_pVerts[i].dwIDInRootMesh].y;
            thisVertex.z = m_baseInfo.pVertPosition[m_pVerts[i].dwIDInRootMesh].z;
            thisVertex.bBoundary = m_pVerts[i].bIsBoundary;
        }

        // init edge list in ONE_TO_ALL_ENGINE
        for (size_t i = 0; i < m_dwEdgeNumber; ++i)
        {
            Edge &thisEdge = ONE_TO_ALL_ENGINE.m_EdgeList[i];

            thisEdge.dwVertexIdx0 = m_edges[i].dwVertexID[0];
            thisEdge.pVertex0 = &ONE_TO_ALL_ENGINE.m_VertexList[thisEdge.dwVertexIdx0];
            thisEdge.dwVertexIdx1 = m_edges[i].dwVertexID[1];
            thisEdge.pVertex1 = &ONE_TO_ALL_ENGINE.m_VertexList[thisEdge.dwVertexIdx1];

            thisEdge.dwAdjFaceIdx0 = m_edges[i].dwFaceID[0];
            thisEdge.pAdjFace0 = &ONE_TO_ALL_ENGINE.m_FaceList[thisEdge.dwAdjFaceIdx0];
            thisEdge.dwAdjFaceIdx1 = m_edges[i].dwFaceID[1] == INVALID_FACE_ID ? FLAG_INVALIDDWORD : m_edges[i].dwFaceID[1];
            thisEdge.pAdjFace1 = m_edges[i].dwFaceID[1] == INVALID_FACE_ID ? nullptr : &ONE_TO_ALL_ENGINE.m_FaceList[thisEdge.dwAdjFaceIdx1];

            thisEdge.dEdgeLength = sqrt(SquredD3Dist(*thisEdge.pVertex0, *thisEdge.pVertex1));

            thisEdge.pVertex0->edgesAdj.push_back(&thisEdge);
            thisEdge.pVertex1->edgesAdj.push_back(&thisEdge);
        }

        // init face list in ONE_TO_ALL_ENGINE
        for (size_t i = 0; i < m_dwFaceNumber; ++i)
        {
            Face &thisFace = ONE_TO_ALL_ENGINE.m_FaceList[i];

            thisFace.dwEdgeIdx0 = m_pFaces[i].dwEdgeID[0];
            thisFace.pEdge0 = &ONE_TO_ALL_ENGINE.m_EdgeList[thisFace.dwEdgeIdx0];
            thisFace.dwEdgeIdx1 = m_pFaces[i].dwEdgeID[1];
            thisFace.pEdge1 = &ONE_TO_ALL_ENGINE.m_EdgeList[thisFace.dwEdgeIdx1];
            thisFace.dwEdgeIdx2 = m_pFaces[i].dwEdgeID[2];
            thisFace.pEdge2 = &ONE_TO_ALL_ENGINE.m_EdgeList[thisFace.dwEdgeIdx2];

            thisFace.dwVertexIdx0 = m_pFaces[i].dwVertexID[0];
            thisFace.pVertex0 = &ONE_TO_ALL_ENGINE.m_VertexList[thisFace.dwVertexIdx0];
            thisFace.dwVertexIdx1 = m_pFaces[i].dwVertexID[1];
            thisFace.pVertex1 = &ONE_TO_ALL_ENGINE.m_VertexList[thisFace.dwVertexIdx1];
            thisFace.dwVertexIdx2 = m_pFaces[i].dwVertexID[2];
            thisFace.pVertex2 = &ONE_TO_ALL_ENGINE.m_VertexList[thisFace.dwVertexIdx2];

            thisFace.pVertex2->dAngle += ComputeAngleBetween2Lines(*thisFace.pVertex2, *thisFace.pVertex0, *thisFace.pVertex1);
            thisFace.pVertex1->dAngle += ComputeAngleBetween2Lines(*thisFace.pVertex1, *thisFace.pVertex0, *thisFace.pVertex2);
            thisFace.pVertex0->dAngle += ComputeAngleBetween2Lines(*thisFace.pVertex0, *thisFace.pVertex1, *thisFace.pVertex2);

            thisFace.pVertex0->bUsed = true;
            thisFace.pVertex1->bUsed = true;
            thisFace.pVertex2->bUsed = true;

            thisFace.pVertex0->facesAdj.push_back(&thisFace);
            thisFace.pVertex1->facesAdj.push_back(&thisFace);
            thisFace.pVertex2->facesAdj.push_back(&thisFace);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK ;
}

// For each vertex in landmark list, compute geodesic distance from
// this vertex to all other vertices in the same chart.
HRESULT CIsochartMesh::CalculateGeodesicDistance(
    std::vector<uint32_t>& vertList,
    float* pfVertCombineDistance,
    float* pfVertGeodesicDistance) const
{
    if (vertList.empty())
    {
        return S_OK;
    }
    assert( !(!pfVertGeodesicDistance && !pfVertCombineDistance));

    HRESULT hr = S_OK;
    size_t dwVertLandNumber = static_cast<size_t>(vertList.size());
    bool bIsSignalDistance = IsIMTSpecified();

    if ( 
          (
             // if the geodesic algorithm selection field of the isochart option is DEFAULT, check whether suitable to apply the new algorithm
             (  
                 (  
                     (m_IsochartEngine.m_dwOptions & _OPTIONMASK_ISOCHART_GEODESIC) 
                     == 
                     (_OPTION_ISOCHART_DEFAULT & _OPTIONMASK_ISOCHART_GEODESIC)
                 )
                 &&
                 (m_baseInfo.dwFaceCount < LIMIT_FACENUM_USENEWGEODIST)
             ) 
             ||

             // or the user forces to use the new algorithm 
             (
                 m_IsochartEngine.m_dwOptions & _OPTION_ISOCHART_GEODESIC_QUALITY
             )
          ) 
          &&
          
          // anyway, if IMT is specified, use the old geodesic distance algorithm, because currently the new geodesic distance algorithm does not support IMT
          (          
              !bIsSignalDistance && 
              m_dwVertNumber > 0 && 
              m_dwFaceNumber > 0
          )
       )
    {
        const_cast<CIsochartMesh*>(this)->InitOneToAllEngine() ;
    }

    float* pfTempGeodesicDistance = nullptr;
    if (!pfVertGeodesicDistance)
    {
        pfTempGeodesicDistance = new (std::nothrow) float[dwVertLandNumber * m_dwVertNumber];
        if (!pfTempGeodesicDistance)
        {				
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        pfTempGeodesicDistance = pfVertGeodesicDistance;
    }	

    float *pCombineDistanceToOneLandmark = pfVertCombineDistance;
    float *pGeodesicDstanceToOneLandmark = pfTempGeodesicDistance;
    for (size_t i=0; i<dwVertLandNumber; i++)
    {
        if (FAILED(hr = CalculateGeodesicDistanceToVertex(
                            vertList[i],
                            bIsSignalDistance)))
        {
            if (pfVertGeodesicDistance != pfTempGeodesicDistance)
            {
                delete []pfTempGeodesicDistance;
            }		
            return hr;
        }

        if (pfVertCombineDistance && bIsSignalDistance)
        {
            for (size_t j=0; j<m_dwVertNumber; j++)
            {				
                pCombineDistanceToOneLandmark[j] = m_pVerts[j].fSignalDistance;
                pGeodesicDstanceToOneLandmark[j] = m_pVerts[j].fGeodesicDistance;
            }
            pCombineDistanceToOneLandmark += m_dwVertNumber;
            pGeodesicDstanceToOneLandmark += m_dwVertNumber;
        }
        else
        {
            for (size_t j=0; j<m_dwVertNumber; j++)
            {
                pGeodesicDstanceToOneLandmark[j] = m_pVerts[j].fGeodesicDistance;
            }
            pGeodesicDstanceToOneLandmark += m_dwVertNumber;
        }
    }

    if (pfVertCombineDistance && bIsSignalDistance)
    {
        CombineGeodesicAndSignalDistance(
            pfVertCombineDistance,
            pfTempGeodesicDistance,
            dwVertLandNumber);
    }

    for (size_t i=0; i<dwVertLandNumber; i++)
    {
        for (size_t j=i; j<dwVertLandNumber; j++)
        {
            uint32_t dwIndex1 = static_cast<uint32_t>( i*m_dwVertNumber + vertList[j]);
            uint32_t dwIndex2 = static_cast<uint32_t>( j*m_dwVertNumber + vertList[i]);

            if (pfVertCombineDistance && bIsSignalDistance)
            {
                pfVertCombineDistance[dwIndex1]
                    = pfVertCombineDistance[dwIndex2]
                    = std::min<float>(
                        pfVertCombineDistance[dwIndex1],
                        pfVertCombineDistance[dwIndex2]);
            }

            if (pfVertGeodesicDistance)
            {
                pfVertGeodesicDistance[dwIndex1]
                    = pfVertGeodesicDistance[dwIndex2]
                    = std::min<float>(
                        pfVertGeodesicDistance[dwIndex1],
                        pfVertGeodesicDistance[dwIndex2]);
            }
        }
    }

    if (pfVertGeodesicDistance != pfTempGeodesicDistance)
    {	
        delete []pfTempGeodesicDistance;
    }
    
    return S_OK;

}

void CIsochartMesh::CombineGeodesicAndSignalDistance(
    float* pfSignalDistance,
    const float* pfGeodesicDistance,
    size_t dwVertLandNumber) const
{
    assert(pfSignalDistance != 0);
    assert(pfGeodesicDistance != 0);

    float fAverageSignalDifference = 0;
    float fAverageGeodesicDifference = 0;

    size_t dwDistanceCount = dwVertLandNumber * m_dwVertNumber;

    for (size_t ii=0; ii<dwDistanceCount; ii++)
    {
        fAverageSignalDifference += pfSignalDistance[ii];
        fAverageGeodesicDifference+= pfGeodesicDistance[ii];
    }

    float fSignalWeight = SIGNAL_DISTANCE_WEIGHT;
    
    fAverageSignalDifference /= dwDistanceCount;
    fAverageGeodesicDifference /= dwDistanceCount;

    if ( fAverageSignalDifference > ISOCHART_ZERO_EPS)
    {
        float fRatio
            = fAverageGeodesicDifference/fAverageSignalDifference;

        for (size_t ii=0; ii<dwDistanceCount; ii++)
        {
            pfSignalDistance[ii] =
                pfGeodesicDistance[ii] * (1-fSignalWeight)
                + fRatio*pfSignalDistance[ii] * fSignalWeight;
        }
    }
    else
    {
        memcpy(pfSignalDistance, pfGeodesicDistance, sizeof(float)*dwDistanceCount);
    }
}

void CIsochartMesh::UpdateAdjacentVertexGeodistance(
    ISOCHARTVERTEX* pCurrentVertex,
    ISOCHARTVERTEX* pAdjacentVertex,
    const ISOCHARTEDGE& edgeBetweenVertex,
    bool* pbVertProcessed,
    bool bIsSignalDistance) const
{
    assert(pCurrentVertex != 0);
    assert(pAdjacentVertex != 0);
    assert(pbVertProcessed != 0);

    if (pAdjacentVertex->fGeodesicDistance
        > (pCurrentVertex->fGeodesicDistance 
            + edgeBetweenVertex.fLength))
    {
        pAdjacentVertex->fGeodesicDistance = 
                (pCurrentVertex->fGeodesicDistance 
                + edgeBetweenVertex.fLength);

        if (bIsSignalDistance)
        {
            pAdjacentVertex->fSignalDistance = 
                pCurrentVertex->fSignalDistance
                + edgeBetweenVertex.fSignalLength;
        }

    }

    for (size_t k=0; k<2; k++)
    {
        if (edgeBetweenVertex.dwOppositVertID[k] == INVALID_VERT_ID)
        {
            assert(k == 1);
            break;
        }

        assert(!(k == 1 && edgeBetweenVertex.bIsBoundary));

        ISOCHARTVERTEX* pOppositeVertex = m_pVerts + edgeBetweenVertex.dwOppositVertID[k];

        if (pbVertProcessed[pOppositeVertex->dwID])
        {
            if (pOppositeVertex->fGeodesicDistance >
                pCurrentVertex->fGeodesicDistance)
            {
                CalculateGeodesicDistanceABC(
                    pCurrentVertex,
                    pOppositeVertex,
                    pAdjacentVertex);
            }
            else
            {
                CalculateGeodesicDistanceABC(
                    pOppositeVertex,
                    pCurrentVertex,	
                    pAdjacentVertex);
            }
        }
    }
}

HRESULT CIsochartMesh::CalculateGeodesicDistanceToVertex(
    uint32_t dwSourceVertID,
    bool bIsSignalDistance,
    uint32_t* pdwFarestPeerVertID) const
{
    HRESULT hr = 
        CalculateGeodesicDistanceToVertexKS98( dwSourceVertID, bIsSignalDistance, pdwFarestPeerVertID ) ;
    if ( FAILED(hr) )
        return hr ;

    if ( 
          (
             // if the geodesic algorithm selection field of the isochart option is DEFAULT, check whether suitable to apply the new algorithm
             (  
                 (  
                     (m_IsochartEngine.m_dwOptions & _OPTIONMASK_ISOCHART_GEODESIC) 
                     == 
                     (_OPTION_ISOCHART_DEFAULT & _OPTIONMASK_ISOCHART_GEODESIC)
                 )
                 &&
                 (m_baseInfo.dwFaceCount < LIMIT_FACENUM_USENEWGEODIST)
             ) 
             ||

             // or the user forces to use the new algorithm 
             (
                 m_IsochartEngine.m_dwOptions & _OPTION_ISOCHART_GEODESIC_QUALITY
             )
          ) 
          &&
          
          // anyway, if IMT is specified, use the old geodesic distance algorithm, because currently the new geodesic distance algorithm does not support IMT
          (          
              !bIsSignalDistance && 
              m_dwVertNumber > 0 && 
              m_dwFaceNumber > 0
          )
       )
    {
        hr = const_cast<CIsochartMesh*>(this)->CalculateGeodesicDistanceToVertexNewGeoDist( dwSourceVertID, pdwFarestPeerVertID ) ;
    }

    return hr ;
}

HRESULT CIsochartMesh::CalculateGeodesicDistanceToVertexNewGeoDist(
    uint32_t dwSourceVertID,
    uint32_t* pdwFarestPeerVertID)
{
    try
    {
        ONE_TO_ALL_ENGINE.SetSrcVertexIdx( dwSourceVertID ) ;    
        ONE_TO_ALL_ENGINE.Run() ;
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    uint32_t dwFarestVertID = 0;
    double dGeoFarest = 0.0 ;
    for (uint32_t i = 0; i < m_dwVertNumber; ++i)
    {
        m_pVerts[i].fGeodesicDistance = m_pVerts[i].fSignalDistance = 
            std::min(m_pVerts[i].fGeodesicDistance,
                 (float)ONE_TO_ALL_ENGINE.m_VertexList[i].dGeoDistanceToSrc ) ;

        if ( m_pVerts[i].fGeodesicDistance > dGeoFarest )
        {
            dGeoFarest = m_pVerts[i].fGeodesicDistance ;
            dwFarestVertID = i ;
        }
    }

    if ( pdwFarestPeerVertID )
    {
        *pdwFarestPeerVertID = dwFarestVertID ;
    }

    return S_OK;
}

// See more detail in [KS98]
HRESULT CIsochartMesh::CalculateGeodesicDistanceToVertexKS98(
    uint32_t dwSourceVertID,
    bool bIsSignalDistance,
    uint32_t* pdwFarestPeerVertID) const
{
    uint32_t dwFarestVertID = 0;

    std::unique_ptr<bool[]> pbVertProcessed( new (std::nothrow) bool[m_dwVertNumber] );
    std::unique_ptr<CMaxHeapItem<float, uint32_t> []> heapItem(new (std::nothrow) CMaxHeapItem<float, uint32_t>[m_dwVertNumber]);
    if (!pbVertProcessed || !heapItem)
    {
        return E_OUTOFMEMORY;
    }
    memset(pbVertProcessed.get(), 0, sizeof(bool) * m_dwVertNumber);

    CMaxHeap<float, uint32_t> heap;
    if (!heap.resize(m_dwVertNumber))
    {
        return E_OUTOFMEMORY;
    }

    auto pHeapItem = heapItem.get();

    // 1. Init the distance to source of each vertex
    ISOCHARTVERTEX* pCurrentVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pCurrentVertex->fGeodesicDistance = FLT_MAX;
        pCurrentVertex->fSignalDistance = FLT_MAX;
        pCurrentVertex++;
    }

    // 2. Init the source vertices
    pCurrentVertex = m_pVerts + dwSourceVertID;
    pbVertProcessed[dwSourceVertID] = true;
    pCurrentVertex->fGeodesicDistance = 0;
    pCurrentVertex->fSignalDistance = 0;

    // 3. Init heap to prepare process of iteration.
    pHeapItem[dwSourceVertID].m_data = dwSourceVertID;
    pHeapItem[dwSourceVertID].m_weight = 0;

    if (!heap.insert(pHeapItem + dwSourceVertID))
    {
        return E_OUTOFMEMORY;
    }

    dwFarestVertID = dwSourceVertID;

    // 4. Dijkstra algorithm to compute geodesic distance from source
    // to other vertices.
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        CMaxHeapItem<float, uint32_t>* pTop;
        pTop = heap.cutTop();
        if (!pTop)
        {
            break;
        }

        pCurrentVertex = m_pVerts + pTop->m_data;
        pbVertProcessed[pCurrentVertex->dwID] = true;
        dwFarestVertID = pCurrentVertex->dwID;

        // 4.1 For each vertex adjacent to current vertex, Compute geodesic
        //     distance to source vertex.
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

            UpdateAdjacentVertexGeodistance(
                pCurrentVertex, pAdjacentVertex,
                edge, pbVertProcessed.get(), bIsSignalDistance);

        }

        // 4.2 Update heap according to 4.1 step.
        for (size_t j=0; j<pCurrentVertex->vertAdjacent.size(); j++)
        {
            uint32_t dwAdjacentID = pCurrentVertex->vertAdjacent[j];
            if (pbVertProcessed[dwAdjacentID])
            {
                continue;
            }

            ISOCHARTVERTEX* pAdjacentVertex = m_pVerts + dwAdjacentID;
            if (pHeapItem[dwAdjacentID].isItemInHeap())
            {
                heap.update(pHeapItem+dwAdjacentID,
                    -pAdjacentVertex->fGeodesicDistance);
            }
            else
            {
                pHeapItem[dwAdjacentID].m_data = dwAdjacentID;
                pHeapItem[dwAdjacentID].m_weight =
                    -pAdjacentVertex->fGeodesicDistance;
                if (!heap.insert(pHeapItem+dwAdjacentID))
                {
                    return E_OUTOFMEMORY;
                }
            }
        }
    }
    
    if (pdwFarestPeerVertID)
    {
        *pdwFarestPeerVertID = dwFarestVertID;
    }

    return S_OK;
}

void CIsochartMesh::CalculateGeodesicDistanceABC(
    ISOCHARTVERTEX* pVertexA,
    ISOCHARTVERTEX* pVertexB,
    ISOCHARTVERTEX* pVertexC) const
{
    XMVECTOR v[3];
    float u = pVertexB->fGeodesicDistance - pVertexA->fGeodesicDistance;
    v[0] = XMLoadFloat3(m_baseInfo.pVertPosition + pVertexB->dwIDInRootMesh)
        - XMLoadFloat3(m_baseInfo.pVertPosition + pVertexC->dwIDInRootMesh);

    v[1] = XMLoadFloat3(m_baseInfo.pVertPosition + pVertexA->dwIDInRootMesh)
        - XMLoadFloat3(m_baseInfo.pVertPosition + pVertexC->dwIDInRootMesh);

    float a = XMVectorGetX(XMVector3Length(v[0]));
    float b = XMVectorGetX(XMVector3Length(v[1]));
    float c = a * b;

    if (IsInZeroRange(c))
    {
        return;
    }

    float fCosTheta = XMVectorGetX(XMVector3Dot(v[0], v[1])) / c;

    v[2] = XMVector3Cross(v[0], v[1]);
    float fSinTheta = XMVectorGetX(XMVector3Length(v[2])) / c;

    float fA = a*a + b*b - 2 * a*b*fCosTheta;
    float fB = 2 * b*u*(a*fCosTheta - b);
    float fC = b*b*(u*u - a*a*fSinTheta*fSinTheta);

    float t = fB*fB - 4 * fA*fC;

    if (t < 0 || IsInZeroRange(fA))
    {
        return;
    }

    t = (IsochartSqrtf(t) - fB) / (2 * fA);
    if (t<u || IsInZeroRange(t))
    {
        return;
    }

    float fT = b*(t - u) / t;

    if (fCosTheta > ISOCHART_ZERO_EPS && fT > a / fCosTheta)
    {
        return;
    }

    if (fT < a*fCosTheta)
    {
        return;
    }

    if (pVertexC->fGeodesicDistance > pVertexA->fGeodesicDistance + t)
    {
        pVertexC->fGeodesicDistance = pVertexA->fGeodesicDistance + t;
    }


}



// Calculate Geodesic Matrix for landmarks
void CIsochartMesh::CalculateGeodesicMatrix(
    std::vector<uint32_t>& vertList,
    const float* pfVertGeodesicDistance,
    float* pfGeodesicMatrix) const
{
    assert(pfVertGeodesicDistance != 0);
    assert(pfGeodesicMatrix != 0);

    size_t dwVertLandNumber = vertList.size();

    const float *pDistanceToOneLandmark = pfVertGeodesicDistance;
    float* pfGeodesicColumn = pfGeodesicMatrix;
    for (size_t i=0; i<dwVertLandNumber; i++)
    {
        for (size_t j=0; j<dwVertLandNumber; j++)
        {
            pfGeodesicColumn[j] = pDistanceToOneLandmark[m_landmarkVerts[j]];
        }
        pDistanceToOneLandmark += m_dwVertNumber;
        pfGeodesicColumn += dwVertLandNumber;
    }

#ifdef _DEBUG
    for (size_t i=0; i<dwVertLandNumber; i++)
    {
        for (size_t j=i; j<dwVertLandNumber; j++)
        {
            assert(pfGeodesicMatrix[i*dwVertLandNumber+j]
                == pfGeodesicMatrix[j*dwVertLandNumber+i]);
        }
    }
#endif

    return;
}

// Compute n-dimension embeddings of all vertices which are not landmark, using
// algorithm in section 4 of [Kun04]
HRESULT CIsochartMesh::CalculateVertMappingCoord(
    const float* pfVertGeodesicDistance,
    size_t dwLandmarkNumber,
    size_t dwPrimaryEigenDimension,
    float* pfVertMappingCoord)	// If not nullptr, store dwPrimaryEigenDimension
                                // coordinates of each vertex in it.Not Only
                                // store UV coordinate in vertex
{
    assert(pfVertGeodesicDistance != 0);
    assert(dwPrimaryEigenDimension >= 2);
    _Analysis_assume_(dwPrimaryEigenDimension >= 2);

    std::unique_ptr<float[]> landmarkCoords;
    if (dwLandmarkNumber * dwPrimaryEigenDimension >
        dwLandmarkNumber + dwPrimaryEigenDimension)
    {
        landmarkCoords.reset( new (std::nothrow) float[dwLandmarkNumber * dwPrimaryEigenDimension] );
    }
    else
    {
        landmarkCoords.reset( new (std::nothrow) float[dwLandmarkNumber + dwPrimaryEigenDimension] );
    }
    if (!landmarkCoords)
    {
        return E_OUTOFMEMORY;
    }

    float* pfLandmarkCoords = landmarkCoords.get();

    if (!m_isoMap.GetDestineVectors(dwPrimaryEigenDimension, pfLandmarkCoords))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    float* pfCoord = pfLandmarkCoords;
    ISOCHARTVERTEX* pVertex = nullptr;
    for (size_t i=0; i<dwLandmarkNumber; i++)
    {
        pVertex = m_pVerts + m_landmarkVerts[i];
        if (pfVertMappingCoord)
        {
            memcpy(
                pfVertMappingCoord
                    + (m_landmarkVerts[i])*dwPrimaryEigenDimension,
                pfCoord,
                dwPrimaryEigenDimension * sizeof(float));
        }

        pVertex->uv.x = pfCoord[0];
        pVertex->uv.y = pfCoord[1];

        pfCoord += dwPrimaryEigenDimension;
    }

    const float* pfAverage = m_isoMap.GetAverageColumn();

    pVertex= m_pVerts;

    //Beacause pfLandmarkCoords is no longer used. Here reuse the buffer for
    //other work. Just reduce additional memory allocation.
    float *fVectorWeight = pfLandmarkCoords;

    pfCoord = pfLandmarkCoords + dwLandmarkNumber;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        if (pVertex->bIsLandmark)
        {
            pVertex++;
            continue;
        }

        const float* pfDistance = pfVertGeodesicDistance;

        for (size_t j=0; j<dwLandmarkNumber; j++)
        {
            fVectorWeight[j] = pfAverage[j] - pfDistance[i]*pfDistance[i];
            pfDistance += m_dwVertNumber;
        }

        if (pfVertMappingCoord)
        {
            pfCoord = pfVertMappingCoord + i * dwPrimaryEigenDimension;
        }

        for (size_t k=0; k<dwPrimaryEigenDimension; k++)
        {
            pfCoord[k] = 0;
            const float* fEigenVector = m_isoMap.GetEigenVector()+ k*dwLandmarkNumber;

            for (size_t j=0; j<dwLandmarkNumber; j++)
            {
                pfCoord[k] += fVectorWeight[j] * fEigenVector[j];

            }
            pfCoord[k] /= IsochartSqrtf(m_isoMap.GetEigenValue()[k])*2;
        }

        pVertex->uv.x = pfCoord[0];
        pVertex->uv.y = pfCoord[1];

        pVertex++;
    }

    // Make the parameterization on the right plane
    uint32_t dwPositiveFaceNumber = 0;

    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t i=0; i <m_dwFaceNumber; i++)
    {
        XMFLOAT3 vec1(m_pVerts[pFace->dwVertexID[1]].uv.x - m_pVerts[pFace->dwVertexID[0]].uv.x, 
                           m_pVerts[pFace->dwVertexID[1]].uv.y - m_pVerts[pFace->dwVertexID[0]].uv.y,
                           0);

        XMFLOAT3 vec2(m_pVerts[pFace->dwVertexID[2]].uv.x - m_pVerts[pFace->dwVertexID[0]].uv.x,
                           m_pVerts[pFace->dwVertexID[2]].uv.y - m_pVerts[pFace->dwVertexID[0]].uv.y,
                           0);

        if (CalculateZOfVec3Cross(&vec1, &vec2) >= 0)
        {
            dwPositiveFaceNumber++;
        }
        pFace++;
    }

    if (dwPositiveFaceNumber < m_dwFaceNumber- dwPositiveFaceNumber)
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            m_pVerts[i].uv.y = -m_pVerts[i].uv.y;
        }
    }

    return S_OK;
}
