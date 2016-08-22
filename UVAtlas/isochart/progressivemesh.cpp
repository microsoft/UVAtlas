//-------------------------------------------------------------------------------------
// UVAtlas - progressivemesh.cpp
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

// GARLAND M., HECKBERT P.:
// Surface simplification using quadric error metrics 
// In Proceedings of SIGGRAPH 1997 (1997), pp. 209-216

#include "pch.h"
#include "progressivemesh.h"

#define DOUBLE_OP(x, y, op) (double)(x) op (double)(y)

using namespace Isochart;
using namespace DirectX;

///////////////////////////////////////////////////////////
/////////// Configuration of simplifying mesh//////////////
///////////////////////////////////////////////////////////

namespace
{
    // if deleting an edge will produce a quadric error larger than 
    // MAX_PM_ERROR, just stop simplify mesh. Otherwise, some important
    // geodesic information may be lost. 0.9 is based on examination of
    // Kun
    const float MAX_PM_ERROR = 0.90f;

    void IsochartVec3Substract(
        XMFLOAT3 *pOut,
        const XMFLOAT3 *pV1,
        const XMFLOAT3 *pV2)
    {
        pOut->x = (float) (DOUBLE_OP(pV1->x, pV2->x, -));
        pOut->y = (float) (DOUBLE_OP(pV1->y, pV2->y, -));
        pOut->z = (float) (DOUBLE_OP(pV1->z, pV2->z, -));
    }

    void IsochartVec3Cross(
        XMFLOAT3 *pOut,
        const XMFLOAT3 *pV1,
        const XMFLOAT3 *pV2)
    {
        XMFLOAT3 v;

        v.x = (float) (DOUBLE_OP(pV1->y, pV2->z, *) - DOUBLE_OP(pV1->z, pV2->y, *));
        v.y = (float) (DOUBLE_OP(pV1->z, pV2->x, *) - DOUBLE_OP(pV1->x, pV2->z, *));
        v.z = (float) (DOUBLE_OP(pV1->x, pV2->y, *) - DOUBLE_OP(pV1->y, pV2->x, *));

        *pOut = v;
    }

    float IsochartVec3Dot(
        const XMFLOAT3 *pV1,
        const XMFLOAT3 *pV2)
    {
        return (float) (
            DOUBLE_OP(pV1->x, pV2->x, *) +
            DOUBLE_OP(pV1->y, pV2->y, *) +
            DOUBLE_OP(pV1->z, pV2->z, *));
    }
}

// Constructor
CProgressiveMesh::CProgressiveMesh(
    const CBaseMeshInfo &baseInfo,
    CCallbackSchemer& callbackSchemer)
    :m_baseInfo(baseInfo),
    m_callbackSchemer(callbackSchemer),
    m_pVertArray(nullptr),
    m_pFaceArray(nullptr),
    m_pEdgeArray(nullptr),
    m_pQuadricArray(nullptr),
    m_dwVertNumber(0),
    m_dwFaceNumber(0),
    m_dwEdgeNumber(0)
{
    
}

CProgressiveMesh::~CProgressiveMesh()
{
    Clear();
}

// Release all memory resource
void CProgressiveMesh::Clear()
{
    SAFE_DELETE_ARRAY(m_pVertArray);
    SAFE_DELETE_ARRAY(m_pFaceArray);
    SAFE_DELETE_ARRAY(m_pEdgeArray);
    SAFE_DELETE_ARRAY(m_pQuadricArray);

    m_dwVertNumber = 0;
    m_dwFaceNumber = 0;
    m_dwEdgeNumber = 0;
}

HRESULT CProgressiveMesh::Initialize(CIsochartMesh& mesh)
{
    HRESULT hr = S_OK;
    Clear();

    m_dwVertNumber = static_cast<uint32_t>(mesh.GetVertexNumber());
    m_dwFaceNumber = static_cast<uint32_t>(mesh.GetFaceNumber());
    m_dwEdgeNumber = static_cast<uint32_t>(mesh.GetEdgeNumber());
    m_fBoxDiagLen = mesh.GetBoxDiagLen();

    // 1. Allocate memory resource
    m_pVertArray = new (std::nothrow) PMISOCHARTVERTEX[m_dwVertNumber];
    m_pFaceArray = new (std::nothrow) PMISOCHARTFACE[m_dwFaceNumber];
    m_pEdgeArray = new (std::nothrow) PMISOCHARTEDGE[m_dwEdgeNumber];

    if (!m_pVertArray || !m_pFaceArray || !m_pEdgeArray)
    {
        Clear();
        return E_OUTOFMEMORY;
    }

    // 2. Create progressive mesh or original mesh
    if (FAILED( hr = CreateProgressiveMesh(mesh)))
    {
        Clear();
        return hr;
    }

    // 3. Caculated quadric error for each edge and vertex
    if (FAILED( hr = CalculateQuadricErrorMetric()))
    {
        Clear();
        return hr;
    }

    return hr;
}



// Simplify Progressive Mesh

// Algorithm:
// Iteratively delete vertex from progressive mesh, Each iteration delete the vertex whose 
// vanishment makes least distortion of whole mesh. 
// The order to delete the vertices decide the vertices's importance order.
// See more detail in : [GH97]
HRESULT CProgressiveMesh::Simplify()
{
    uint32_t dwMinVertNumber = MIN_PM_VERT_NUMBER;
    float fMaxError = MAX_PM_ERROR;

    CCostHeap heap;
    CCostHeapItem* pNeedCutEdgeItem = nullptr;

    if (!heap.resize(m_dwEdgeNumber))
    {
        return  E_OUTOFMEMORY;
    }

    std::unique_ptr<CCostHeapItem[]> heapItems( new (std::nothrow) CCostHeapItem[m_dwEdgeNumber] );
    if (!heapItems)
    {
        return E_OUTOFMEMORY;
    }

    auto pHeapItems = heapItems.get();

    // 1. Initialize a heap
    for (uint32_t i=0; i < m_dwEdgeNumber; i++)
    {
        pHeapItems[i].m_weight = -m_pEdgeArray[i].fDeleteCost;
        
        if (pHeapItems[i].m_weight > -ISOCHART_ZERO_EPS)
        {
            pHeapItems[i].m_weight = -ISOCHART_ZERO_EPS;
        }
        pHeapItems[i].m_data = i;
        if (!heap.insert(pHeapItems+i))
        {
            return E_OUTOFMEMORY;
        }
        assert(pHeapItems[i].getPos() != NOT_IN_HEAP);
    }

    DPF(3,"----Begin Simplify----");
    HRESULT hr = m_callbackSchemer.CheckPointAdapt();
    if ( FAILED(hr) )
        return hr;

    // 2. Iteration of deleting edges.
    size_t dwEdgeCount = 0;
    int nImportanceOrder = 1;
    uint32_t dwRemainVertNumber = m_dwVertNumber;
    size_t dwRepeat = 0;
    while (dwEdgeCount < m_dwEdgeNumber 
        && dwRemainVertNumber > dwMinVertNumber)
    {
        // 2.1 Fetch a candidate edge to be deleted.
        pNeedCutEdgeItem = heap.cutTop();
        if (!pNeedCutEdgeItem)
        {
            break;
        }

        PMISOCHARTEDGE* pCurrentEdge = 
            m_pEdgeArray + pNeedCutEdgeItem->m_data;
        assert(!pCurrentEdge->bIsDeleted);

        // 2.2 If deleting current edge makes distortion more than some limit, then stop
        // deleting edges.
        
        if (static_cast<float>(fabs(pNeedCutEdgeItem->m_weight))
            > fMaxError*m_fBoxDiagLen)
        {	
            break;
        }

        // 2.3 Decide if current edge can be deleted, which vertex of current edge
        // will be deleted, which will be reserved.
        PMISOCHARTVERTEX* pReserveVertex;
        PMISOCHARTVERTEX* pDeleteVertex;
        bool bIsGeodesicValid;

        if (!PrepareDeletingEdge(
            pCurrentEdge, &pReserveVertex, &pDeleteVertex, bIsGeodesicValid))
        {
            dwRepeat = 0;
            dwEdgeCount++;
            continue;
        }
        if (!bIsGeodesicValid)
        {
            // Amplify deleteing cost of current edge, so current edge can not be deleted
            // this time, but may be deleted in future.
            pNeedCutEdgeItem->m_weight *= 100;
            assert(pNeedCutEdgeItem->getPos() == NOT_IN_HEAP);

            heap.insert(pNeedCutEdgeItem);
            dwRepeat++;
            if (dwRepeat >= m_dwEdgeNumber)
            {
                break;
            }
            continue;
        }

        // 2.3 Now We can safely delete current edge. Following steps will perform
        // deletion. Not only the edge should be deleted, but also the connection
        // relationship Near the edge should be updated.
        pDeleteVertex->nImportanceOrder = nImportanceOrder++;
        dwRemainVertNumber--;

        hr = DeleteCurrentEdge(
            heap,
            pHeapItems,
            pCurrentEdge,
            pReserveVertex,
            pDeleteVertex);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    // Force to reserve dwMinVertNumber points, Don't care geodesic error.
    while (dwRemainVertNumber > dwMinVertNumber)
    {
        // 2.1 Fetch a candidate edge to be deleted.
        pNeedCutEdgeItem = heap.cutTop();
        if (!pNeedCutEdgeItem)
        {
            break;
        }
        
        PMISOCHARTEDGE* pCurrentEdge = 
            m_pEdgeArray + pNeedCutEdgeItem->m_data;

        // 2.3 Decide if current edge can be deleted, which vertex of current edge
        // will be deleted, which will be reserved.
        PMISOCHARTVERTEX* pReserveVertex;
        PMISOCHARTVERTEX* pDeleteVertex;
        bool bIsGeodesicValid;

        if (!PrepareDeletingEdge(
            pCurrentEdge, &pReserveVertex, &pDeleteVertex, bIsGeodesicValid))
        {
            continue;
        }

        // 2.3 Now We can safely delete current edge. Following steps will perform
        // deletion. Not only the edge should be deleted, but also the connection
        // relationship Near the edge should be updated.
        pDeleteVertex->nImportanceOrder = nImportanceOrder++;
        dwRemainVertNumber--;

        hr = DeleteCurrentEdge(
            heap,
            pHeapItems,
            pCurrentEdge,
            pReserveVertex,
            pDeleteVertex);

        if (FAILED(hr))
        {
            return hr;
        }
    }	

    DPF(3,"#Remained vert: %d\n", dwRemainVertNumber);
    DPF(3, "Exported simplified mesh");

    return S_OK;
}

//Decide if current edge can be deleted, which vertex of current edge
//will be deleted, which will be reserved.
bool CProgressiveMesh::PrepareDeletingEdge(
    PMISOCHARTEDGE* pCurrentEdge,
    PMISOCHARTVERTEX** ppReserveVertex,
    PMISOCHARTVERTEX** ppDeleteVertex,
    bool& bIsGeodesicValid)
{
    assert(ppReserveVertex != 0);
    assert(ppDeleteVertex != 0);
    bIsGeodesicValid = true;

    // 1. If current has been deleted, return
    if (pCurrentEdge->bIsDeleted 
        || pCurrentEdge->dwVertexID[0]==pCurrentEdge->dwVertexID[1])
    {
        return false;
    }

    // 2.Decide which vertex will be reserved, which one will
    // be deleted
    *ppReserveVertex =
        m_pVertArray +
        pCurrentEdge->dwVertexID[1-pCurrentEdge->dwDeleteWhichVertex];

    *ppDeleteVertex =
        m_pVertArray +
        pCurrentEdge->dwVertexID[pCurrentEdge->dwDeleteWhichVertex];

    // 3. If one of vertice has been deleted, return
    if ((*ppReserveVertex)->bIsDeleted || (*ppDeleteVertex)->bIsDeleted)
    {
        return false;
    }

    // 4. Boundary vertex is very important for retainning mesh shape.
    // Don't delete it.
    if ((*ppReserveVertex)->bIsBoundary && (*ppDeleteVertex)->bIsBoundary)
    {
        if (!pCurrentEdge->bIsBoundary)
        {
            return false;
        }
    }

    // 5. Check if toplogic is valid after deleting current edge.
    // Logic invalid is not acceptable. See detail in the function
    if( !IsProgressiveMeshToplogicValid(
        pCurrentEdge, *ppReserveVertex, *ppDeleteVertex))
    {
        return false;
    }

    // 6. Check if geometric is valid after deleting current edge.
    // Geometric invalid is acceptable, but current edge cost should
    // be amplified and be re-put into heap.
    // See detail in the function.
    if (!IsProgressiveMeshGeometricValid(*ppReserveVertex, *ppDeleteVertex))
    {
        // amplify weight to current edge, which can cause geometric error,
        // This edge may be deleted in future.
        bIsGeodesicValid = false;
    }
    return true;
}

// This Function check if deleting a edge can cause followingtoplogic error.
// Error1 : make faces degenerate to segments.
bool CProgressiveMesh::IsProgressiveMeshToplogicValid(
    PMISOCHARTEDGE* pEdge,
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex) const
{
    for (size_t k=0; k<2; k++)
    {
        if (pEdge->dwOppositVertID[k] == INVALID_VERT_ID)
        {
            break;
        }

        PMISOCHARTVERTEX* pThirdVertex =
            m_pVertArray + pEdge->dwOppositVertID[k];
        for (size_t j=0; j<pThirdVertex->edgeAdjacent.size(); j++)
        {
            PMISOCHARTEDGE* pEdge1 = m_pEdgeArray + pThirdVertex->edgeAdjacent[j];
            if (IsEdgeOppositeToVertex(pEdge1, pReserveVertex)
            &&(IsEdgeOppositeToVertex(pEdge1, pDeleteVertex)))
            {
                return false;
            }
        }
    }

    PMISOCHARTFACE* pFace1 =
        m_pFaceArray + pEdge->dwFaceID[0];

    PMISOCHARTFACE* pFace2 = nullptr;
    if (!pEdge->bIsBoundary)
    {
        pFace2 = m_pFaceArray + pEdge->dwFaceID[1];
    }

    for (size_t j=0; j<pReserveVertex->edgeAdjacent.size(); j++)
    {
        PMISOCHARTEDGE* pEdge1 = m_pEdgeArray + pReserveVertex->edgeAdjacent[j];
        if (pEdge1->dwID == pFace1->dwEdgeID[0]
        || pEdge1->dwID == pFace1->dwEdgeID[1]
        || pEdge1->dwID == pFace1->dwEdgeID[2])
        {
            continue;
        }

        if (pFace2)
        {
            if (pEdge1->dwID == pFace2->dwEdgeID[0]
            || pEdge1->dwID == pFace2->dwEdgeID[1]
            || pEdge1->dwID == pFace2->dwEdgeID[2])
            {
                continue;
            }
        }

        for (size_t k=0; k<pDeleteVertex->edgeAdjacent.size(); k++)
        {
            PMISOCHARTEDGE* pEdge2 = m_pEdgeArray + pDeleteVertex->edgeAdjacent[k];
            if (pEdge2->dwID == pFace1->dwEdgeID[0]
            || pEdge2->dwID == pFace1->dwEdgeID[1]
            || pEdge2->dwID == pFace1->dwEdgeID[2])
            {
                continue;
            }

            if (pFace2)
            {
                if (pEdge2->dwID == pFace2->dwEdgeID[0]
                || pEdge2->dwID == pFace2->dwEdgeID[1]
                || pEdge2->dwID == pFace2->dwEdgeID[2])
                {
                    continue;
                }
            }

            // If true, the face (pReserveVertex, pDeleteVertex, pEdge1->dwVertexID[0])
            // or face (pReserveVertex, pDeleteVertex, pEdge1->dwVertexID[1) will 
            // degenerate to a segment.
            if (pEdge2->dwVertexID[0] == pEdge1->dwVertexID[0]
            || pEdge2->dwVertexID[0] == pEdge1->dwVertexID[1]
            || pEdge2->dwVertexID[1] == pEdge1->dwVertexID[0]
            || pEdge2->dwVertexID[1] == pEdge1->dwVertexID[1])
            {
                return false;
            }
        }
    }
    return true;
}

// This function check if deleting an edge makes some faces overturn.
bool CProgressiveMesh::IsProgressiveMeshGeometricValid(
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex) const
{
    XMFLOAT3* pv[3];
    XMFLOAT3 v1, v2, normal;

    for (size_t j=0; j<pDeleteVertex->faceAdjacent.size(); j++)
    {
        if (isInArray(pReserveVertex->faceAdjacent,pDeleteVertex->faceAdjacent[j]))
        {
            continue;
        }

        PMISOCHARTFACE* pFace = 
            m_pFaceArray + pDeleteVertex->faceAdjacent[j];
        for (size_t k=0; k<3; k++)
        {
            if (pFace->dwVertexID[k] == pDeleteVertex->dwID)
            {
                pv[k] = m_baseInfo.pVertPosition
                    + pReserveVertex->dwIDInRootMesh;
            }
            else
            {
                pv[k] = m_baseInfo.pVertPosition
                    + m_pVertArray[pFace->dwVertexID[k]].dwIDInRootMesh;
            }
        }

        IsochartVec3Substract(&v1, pv[1], pv[0]);
        IsochartVec3Substract(&v2, pv[2], pv[0]);

        XMStoreFloat3(&v1, XMVector3Normalize(XMLoadFloat3(&v1)));
        XMStoreFloat3(&v2, XMVector3Normalize(XMLoadFloat3(&v2)));
        IsochartVec3Cross( &normal, &v1, &v2);

        float fDotResult  = IsochartVec3Dot(&normal, &(pFace->normal));
        // if true deleting edge will make face pFace overturn!
        
        if (fDotResult < ISOCHART_ZERO_EPS)
        {
            return false;
        }
    }

    return true;
}


bool CProgressiveMesh::IsEdgeOppositeToVertex
    (PMISOCHARTEDGE* pEdge,
    PMISOCHARTVERTEX* pVertex) const
{
    return (pEdge->dwOppositVertID[0] == pVertex->dwID
    || pEdge->dwOppositVertID[1] == pVertex->dwID);
}

// Delete current edge and correspond topology.
HRESULT CProgressiveMesh::DeleteCurrentEdge(
    CCostHeap& heap,
    CCostHeapItem* pHeapItems,
    PMISOCHARTEDGE* pCurrentEdge,
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    HRESULT hr = S_OK;

    pDeleteVertex->bIsDeleted = true;
    pCurrentEdge->bIsDeleted = true;

    // 1  Delete the faces which have current edge.
    DeleteFacesFromSufferedVertsList(
        pCurrentEdge,
        pReserveVertex);

    // 2. Adjust the attributes of edges suffered by deleting
    // current edge
    UpdateSufferedEdgesAttrib(
        heap,
        pHeapItems,
        pCurrentEdge,
        pReserveVertex,
        pDeleteVertex);

    // 3. Changed all connecting relationship with pDeleteVertex
    // to pReserveVertex.
    FAILURE_RETURN(
    ReplaceDeleteVertWithReserveVert(
        pReserveVertex,
        pDeleteVertex));

    // 4. Adjust the atrributes of the reserved vertex.
    UpdateReservedVertsAttrib(pReserveVertex, pDeleteVertex);

    // 5. Recompute the cost of edges connecting to the reserved vertex.
    UpdateSufferedEdgesCost(heap, pHeapItems, pReserveVertex);

    hr = m_callbackSchemer.UpdateCallbackAdapt(1);

    return hr;
}

// Deleted faces sharing current edge. Remove them from suffered
// vertices' adjacence list.
void CProgressiveMesh::DeleteFacesFromSufferedVertsList(
    PMISOCHARTEDGE* pCurrentEdge,
    PMISOCHARTVERTEX* pReserveVertex)
{
    for (size_t k=0; k<2; k++)
    {
        if (pCurrentEdge->bIsBoundary && k == 1)
        {
            break;
        }

        //Delete the faces which share current edge
        PMISOCHARTFACE* pFace = 
            m_pFaceArray + pCurrentEdge->dwFaceID[k];
        pFace->bIsDeleted = true;

        // Remove deleted face from adjacence list of reserved
        // vertex
        removeItem(pReserveVertex->faceAdjacent, pFace->dwID);

        // Remove deleted face from adjacence list of 
        // vertex opposite to current edge.
        removeItem(
        m_pVertArray[
            pCurrentEdge->dwOppositVertID[k]].faceAdjacent,
                    pFace->dwID);
    }
    return;
}

void CProgressiveMesh::UpdateSufferedEdgesAttrib(
    CCostHeap& heap,
    CCostHeapItem* pHeapItems,
    PMISOCHARTEDGE* pCurrentEdge,
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    for (size_t j=0; j<pDeleteVertex->edgeAdjacent.size(); j++)
    {
        if (pDeleteVertex->edgeAdjacent[j] == pCurrentEdge->dwID)
        {
            continue;
        }

        PMISOCHARTEDGE* pEdgeToDeleteVert =
            m_pEdgeArray + pDeleteVertex->edgeAdjacent[j];
        assert(pEdgeToDeleteVert != 0);
        if (!IsEdgeOppositeToVertex(pEdgeToDeleteVert, pReserveVertex))
        {
            continue;
        }

        pEdgeToDeleteVert->bIsDeleted = true;
        heap.remove(pHeapItems+pEdgeToDeleteVert->dwID);

        PMISOCHARTEDGE* pEdgeToReserveVert = 
            GetSufferedEdges(
                pCurrentEdge,
                pEdgeToDeleteVert,
                pReserveVertex);
        assert(pEdgeToReserveVert != 0);

        if (pEdgeToDeleteVert->bIsBoundary)
        {
            ProcessBoundaryEdge(
                heap,
                pHeapItems,
                pEdgeToDeleteVert,
                pEdgeToReserveVert,
                pReserveVertex,
                pDeleteVertex);
        }
        else
        {
            ProcessInternalEdge(
                pEdgeToDeleteVert,
                pEdgeToReserveVert,
                pReserveVertex,
                pDeleteVertex);
        }
    }
    return;
}

PMISOCHARTEDGE* CProgressiveMesh::GetSufferedEdges(
    PMISOCHARTEDGE* pCurrentEdge,
    PMISOCHARTEDGE* pEdgeToDeleteVert,
    PMISOCHARTVERTEX* pReserveVertex)
{
    PMISOCHARTFACE* pFace;
    if (pEdgeToDeleteVert->dwOppositVertID[0]
        == pReserveVertex->dwID)
    {
        pFace = m_pFaceArray + pEdgeToDeleteVert->dwFaceID[0];
    }
    else
    {
        assert(
            (pEdgeToDeleteVert->dwOppositVertID[1]
            == pReserveVertex->dwID));
        pFace = m_pFaceArray + pEdgeToDeleteVert->dwFaceID[1];
    }

    for (size_t k=0; k<3; k++)
    {
        if (pFace->dwEdgeID[k] != pCurrentEdge->dwID
        && pFace->dwEdgeID[k] != pEdgeToDeleteVert->dwID)
        {
            return m_pEdgeArray + pFace->dwEdgeID[k];
        }
    }
    return nullptr;
}

void CProgressiveMesh::ProcessBoundaryEdge(
    CCostHeap& heap,
    CCostHeapItem* pHeapItems,
    PMISOCHARTEDGE* pEdgeToDeleteVert,
    PMISOCHARTEDGE* pEdgeToReserveVert,
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    PMISOCHARTVERTEX* pThirdVertex;
    if (pEdgeToReserveVert->bIsBoundary)
    {
        //Now pEdgeToReserveVert is a independent boundary edge with no face
        // aside. it must be deleted
        pEdgeToReserveVert->bIsDeleted = true;
        heap.remove(pHeapItems+ pEdgeToReserveVert->dwID);
        if (pEdgeToReserveVert->dwVertexID[0] != pReserveVertex->dwID)
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[0];
        }
        else
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[1];
        }

        removeItem(pReserveVertex->vertAdjacent, pThirdVertex->dwID);
        removeItem(pReserveVertex->edgeAdjacent, pEdgeToReserveVert->dwID);

        removeItem(pThirdVertex->vertAdjacent, pReserveVertex->dwID);
        removeItem(pThirdVertex->edgeAdjacent, pEdgeToDeleteVert->dwID);
        removeItem(pThirdVertex->edgeAdjacent, pEdgeToReserveVert->dwID);
    }
    else
    {
        // Delete a boundary edge, so the internal edge beside the 
        // boundary edge will become a boundary one.
        pEdgeToReserveVert->bIsBoundary = true;
        if (pEdgeToReserveVert->dwOppositVertID[0] == pDeleteVertex->dwID)
        {
            pEdgeToReserveVert->dwOppositVertID[0] = 
                pEdgeToReserveVert->dwOppositVertID[1];
            
            pEdgeToReserveVert->dwFaceID[0] = 
                pEdgeToReserveVert->dwFaceID[1];
        }
        if (pEdgeToReserveVert->dwVertexID[0] == pReserveVertex->dwID)
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[1];
        }
        else
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[0];
        }

        removeItem(pThirdVertex->edgeAdjacent, pEdgeToDeleteVert->dwID);
        pEdgeToReserveVert->dwOppositVertID[1] = INVALID_VERT_ID;
        pEdgeToReserveVert->dwFaceID[1] = INVALID_FACE_ID;
    }
}

void CProgressiveMesh::ProcessInternalEdge(
    PMISOCHARTEDGE* pEdgeToDeleteVert,
    PMISOCHARTEDGE* pEdgeToReserveVert,
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    PMISOCHARTVERTEX* pThirdVertex;
    PMISOCHARTFACE* pFace = nullptr;
    if (pEdgeToReserveVert->bIsBoundary)
    {
        if (pEdgeToDeleteVert->dwOppositVertID[0] == pReserveVertex->dwID)
        {
            pEdgeToReserveVert->dwOppositVertID[0] = 
                pEdgeToDeleteVert->dwOppositVertID[1];
            
            pEdgeToReserveVert->dwFaceID[0] = 
                pEdgeToDeleteVert->dwFaceID[1];
        }
        else
        {
            pEdgeToReserveVert->dwOppositVertID[0] = 
                pEdgeToDeleteVert->dwOppositVertID[0];

            pEdgeToReserveVert->dwFaceID[0] = 
                pEdgeToDeleteVert->dwFaceID[0];
        }
        
        if (pEdgeToReserveVert->dwVertexID[0] == pReserveVertex->dwID)
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[1];
        }
        else
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[0];
        }
        removeItem(pThirdVertex->edgeAdjacent, pEdgeToDeleteVert->dwID);
        pFace = m_pFaceArray + pEdgeToReserveVert->dwFaceID[0];
        for (size_t k=0; k<3; k++)
        {
            if (pFace->dwEdgeID[k] == pEdgeToDeleteVert->dwID)
            {
                pFace->dwEdgeID[k] = pEdgeToReserveVert->dwID;
                break;
            }
        }
    }
    else
    {
        PMISOCHARTFACE* pFace1;
        if (pEdgeToDeleteVert->dwOppositVertID[0] == pReserveVertex->dwID)
        {
            pThirdVertex = m_pVertArray + pEdgeToDeleteVert->dwOppositVertID[1];
            pFace1 = m_pFaceArray + pEdgeToDeleteVert->dwFaceID[1];
        }
        else
        {
            
            pThirdVertex = m_pVertArray + pEdgeToDeleteVert->dwOppositVertID[0];
            pFace1 = m_pFaceArray + pEdgeToDeleteVert->dwFaceID[0];
        }
        if (pEdgeToReserveVert->dwOppositVertID[0] == pDeleteVertex->dwID)
        {
            pEdgeToReserveVert->dwOppositVertID[0] = pThirdVertex->dwID;
            pEdgeToReserveVert->dwFaceID[0] = pFace1->dwID;
        }
        else
        {
            pEdgeToReserveVert->dwOppositVertID[1] = pThirdVertex->dwID;
            pEdgeToReserveVert->dwFaceID[1] = pFace1->dwID;
        }
        for (size_t k=0; k<3; k++)
        {
            if (pFace1->dwEdgeID[k] == pEdgeToDeleteVert->dwID)
            {
                pFace1->dwEdgeID[k] = pEdgeToReserveVert->dwID;
                break;
            }
        }
        if (pEdgeToReserveVert->dwVertexID[0] == pReserveVertex->dwID)
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[1];
        }
        else
        {
            pThirdVertex = m_pVertArray + pEdgeToReserveVert->dwVertexID[0];
        }
        removeItem(pThirdVertex->edgeAdjacent, pEdgeToDeleteVert->dwID);
    }
}


// Adjust the the 1-Ring vertice adjacency relationship of suffered vertices
// Replace pDeleteVertex with pReserveVertex
HRESULT CProgressiveMesh::ReplaceDeleteVertWithReserveVert(
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    // 1. Add all vertices adjacent to pDeleteVertex to the list of
    //  pReserveVertex'sadjacence
    removeItem(pReserveVertex->vertAdjacent, pDeleteVertex->dwID);
    for (size_t j=0; j<pDeleteVertex->vertAdjacent.size(); j++)
    {
        if (pDeleteVertex->vertAdjacent[j] == pReserveVertex->dwID)
        {
            continue;
        }
        PMISOCHARTVERTEX* pScanVertex =
            m_pVertArray + pDeleteVertex->vertAdjacent[j];
        removeItem(pScanVertex->vertAdjacent, pDeleteVertex->dwID);

        if (!addNoduplicateItem(pScanVertex->vertAdjacent, pReserveVertex->dwID))
        {
            return E_OUTOFMEMORY;
        }

        if (!addNoduplicateItem(pReserveVertex->vertAdjacent, pScanVertex->dwID))
        {
            return E_OUTOFMEMORY;
        }
    }

    // 2. Connect all edges which connected to pDeleteVertex before 
    // to pReserveVertex
    for (size_t j=0; j<pDeleteVertex->edgeAdjacent.size(); j++)
    {
        PMISOCHARTEDGE* pEdge1 = 
            m_pEdgeArray + pDeleteVertex->edgeAdjacent[j];
        if (pEdge1->bIsDeleted)
        {
            continue;
        }
        if (pEdge1->dwVertexID[0] == pDeleteVertex->dwID)
        {
            pEdge1->dwVertexID[0] = pReserveVertex->dwID;
        }
        else if (pEdge1->dwVertexID[1] == pDeleteVertex->dwID)
        {
            assert(pEdge1->dwVertexID[1] == pDeleteVertex->dwID);
            pEdge1->dwVertexID[1] = pReserveVertex->dwID;
        }

        if (!addNoduplicateItem(pReserveVertex->edgeAdjacent, pEdge1->dwID))
        {
            return E_OUTOFMEMORY;
        }
    }

    for (size_t j=0; j<pReserveVertex->edgeAdjacent.size();)
    {
        PMISOCHARTEDGE* pEdge1 = 
            m_pEdgeArray + pReserveVertex->edgeAdjacent[j];
        if (pEdge1->bIsDeleted)
        {
            removeItem(pReserveVertex->edgeAdjacent, pEdge1->dwID);
        }
        else
        {
            j++;
        }
    }

    // 3. connect all faces which connected to pDeleteVertex 
    // before to pReserveVertex
    for (size_t j=0; j<pDeleteVertex->faceAdjacent.size(); j++)
    {
        PMISOCHARTFACE* pFace = 
            m_pFaceArray + pDeleteVertex->faceAdjacent[j];
        if (pFace->bIsDeleted)
        {
            continue;
        }

        for (size_t k=0; k<3; k++)
        {
            if (pFace->dwVertexID[k] == pDeleteVertex->dwID)
            {
                pFace->dwVertexID[k] = pReserveVertex->dwID;
                break;
            }
        }
        if (!addNoduplicateItem(pReserveVertex->faceAdjacent, pFace->dwID))
        {
            return E_OUTOFMEMORY;
        }
    }


    // 4. Replace pDeleteVertex with pReserveVertex for every egdges which
    // are opposite to pDeleteVertex before deleting current edge.
    for (size_t j=0; j<pDeleteVertex->faceAdjacent.size(); j++)
    {
        PMISOCHARTFACE* pFace = 
            m_pFaceArray + pDeleteVertex->faceAdjacent[j];

        if (pFace->bIsDeleted)
        {
            continue;
        }
        for (size_t k=0; k<3; k++)
        {
            PMISOCHARTEDGE* pEdge1 =
                m_pEdgeArray + pFace->dwEdgeID[k];

            if (pEdge1->dwVertexID[0] == pReserveVertex->dwID
            ||pEdge1->dwVertexID[1] == pReserveVertex->dwID)
            {
                continue;
            }
            if (pEdge1->dwOppositVertID[0] == pDeleteVertex->dwID)
            {
                pEdge1->dwOppositVertID[0] = pReserveVertex->dwID;
            }
            else if (pEdge1->dwOppositVertID[1] == pDeleteVertex->dwID)
            {
                pEdge1->dwOppositVertID[1] = pReserveVertex->dwID;
            }
        }
    }

    return S_OK;
}

void CProgressiveMesh::UpdateReservedVertsAttrib(
    PMISOCHARTVERTEX* pReserveVertex,
    PMISOCHARTVERTEX* pDeleteVertex)
{
    if (pDeleteVertex->bIsBoundary)
    {
        pReserveVertex->bIsBoundary = true;
    }

    for (size_t j=0; j<pDeleteVertex->quadricList.size(); j++)
    {
        addNoduplicateItem(pReserveVertex->quadricList,
            pDeleteVertex->quadricList[j]);
    }

    pDeleteVertex->quadricList.clear();
    CalculateVertexQuadricError(pReserveVertex);

    XMFLOAT3 v1, v2;
    for (size_t j=0; j<pReserveVertex->faceAdjacent.size(); j++)
    {
        PMISOCHARTFACE* pFace = 
            m_pFaceArray + pReserveVertex->faceAdjacent[j];

        IsochartVec3Substract(
            &v1, 
            m_baseInfo.pVertPosition
                +m_pVertArray[pFace->dwVertexID[1]].dwIDInRootMesh,
            m_baseInfo.pVertPosition
                +m_pVertArray[pFace->dwVertexID[0]].dwIDInRootMesh);

        IsochartVec3Substract(
            &v2, 
            m_baseInfo.pVertPosition
                +m_pVertArray[pFace->dwVertexID[2]].dwIDInRootMesh,
            m_baseInfo.pVertPosition
                +m_pVertArray[pFace->dwVertexID[0]].dwIDInRootMesh);

        XMStoreFloat3(&v1, XMVector3Normalize(XMLoadFloat3(&v1)));
        XMStoreFloat3(&v2, XMVector3Normalize(XMLoadFloat3(&v2)));
        IsochartVec3Cross(&(pFace->normal), &v1, &v2);
        XMStoreFloat3(&(pFace->normal), XMVector3Normalize(XMLoadFloat3(&(pFace->normal))));
    }
}

void CProgressiveMesh::UpdateSufferedEdgesCost(
    CCostHeap& heap,
    CCostHeapItem* pHeapItems,
    PMISOCHARTVERTEX* pReserveVertex)
{
    for (size_t j=0; j<pReserveVertex->edgeAdjacent.size(); j++)
    {
        uint32_t dwCurrentEdgeIndex = pReserveVertex->edgeAdjacent[j];

        PMISOCHARTEDGE* pEdge1 = 
            m_pEdgeArray + dwCurrentEdgeIndex;

        CalculateEdgeQuadricError(pEdge1);
        float fNewDeleteCost = -static_cast<float>(fabs(pEdge1->fDeleteCost));
        if (fNewDeleteCost > -ISOCHART_ZERO_EPS)
        {
            fNewDeleteCost = -ISOCHART_ZERO_EPS;
        }

        if (pHeapItems[dwCurrentEdgeIndex].isItemInHeap())
        {
            heap.update(pHeapItems+dwCurrentEdgeIndex, fNewDeleteCost);
        }
        else
        {
            pHeapItems[dwCurrentEdgeIndex].m_weight = fNewDeleteCost;
        }
    }
    return;
}

HRESULT CProgressiveMesh::CreateProgressiveMesh(CIsochartMesh& mesh)
{	
    ISOCHARTVERTEX* pOrgVerts = mesh.GetVertexBuffer();
    ISOCHARTFACE* pOrgFaces = mesh.GetFaceBuffer();
    auto& orgEdges = mesh.GetEdgesList();

    try
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            m_pVertArray[i].dwID = pOrgVerts[i].dwID;
            m_pVertArray[i].dwIDInRootMesh = pOrgVerts[i].dwIDInRootMesh;
            m_pVertArray[i].bIsBoundary = pOrgVerts[i].bIsBoundary;
            m_pVertArray[i].bIsDeleted = false;
            m_pVertArray[i].nImportanceOrder = MUST_RESERVE;

            m_pVertArray[i].vertAdjacent.insert(m_pVertArray[i].vertAdjacent.end(),
                                                pOrgVerts[i].vertAdjacent.cbegin(), pOrgVerts[i].vertAdjacent.cend());
            m_pVertArray[i].faceAdjacent.insert(m_pVertArray[i].faceAdjacent.end(),
                                                pOrgVerts[i].faceAdjacent.cbegin(), pOrgVerts[i].faceAdjacent.cend());
            m_pVertArray[i].edgeAdjacent.insert(m_pVertArray[i].edgeAdjacent.end(),
                                                pOrgVerts[i].edgeAdjacent.cbegin(), pOrgVerts[i].edgeAdjacent.cend());
        }
    }
    catch (std::bad_alloc&)
    {
        return  E_OUTOFMEMORY;
    }

    for (size_t i=0; i<m_dwFaceNumber; i++)
    {
        m_pFaceArray[i].dwID = pOrgFaces[i].dwID;
        m_pFaceArray[i].bIsDeleted = false;
        memcpy(
            m_pFaceArray[i].dwVertexID, 
            pOrgFaces[i].dwVertexID, 
            sizeof(pOrgFaces[i].dwVertexID));
        
        memcpy(
            m_pFaceArray[i].dwEdgeID, 
            pOrgFaces[i].dwEdgeID, 
            sizeof(pOrgFaces[i].dwEdgeID));
        
        m_pFaceArray[i].normal
            = m_baseInfo.pFaceNormalArray[pOrgFaces[i].dwIDInRootMesh];
    }

    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        const ISOCHARTEDGE& edge = orgEdges[i];
        m_pEdgeArray[i].dwID = edge.dwID;
        m_pEdgeArray[i].bIsBoundary = edge.bIsBoundary;
        m_pEdgeArray[i].bIsDeleted = false;
        
        memcpy(m_pEdgeArray[i].dwVertexID, edge.dwVertexID, sizeof(edge.dwVertexID));

        memcpy(
            m_pEdgeArray[i].dwOppositVertID, 
            edge.dwOppositVertID, 
            sizeof(edge.dwOppositVertID));
        
        memcpy(m_pEdgeArray[i].dwFaceID, edge.dwFaceID, sizeof(edge.dwFaceID));		
    }

    return S_OK;
}

// Calculate Quardic Error Metric, See more detail in  [GH97]
HRESULT CProgressiveMesh::CalculateQuadricErrorMetric()
{
    HRESULT hr = S_OK;

    XMFLOAT3 leftTop(
        FLT_MAX, 
        FLT_MAX, 
        FLT_MAX);

    XMFLOAT3 rightBottom(
        -FLT_MAX, 
        -FLT_MAX, 
        -FLT_MAX);

    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        const float* p = 
            (const float* )(&m_baseInfo.pVertPosition[m_pVertArray[i].dwIDInRootMesh]);
        float* p1 = (float* )(&leftTop);
        float* p2 = (float* )(&rightBottom);

        for (size_t j=0; j<3; j++)
        {
            if (p1[j] > p[j])
            {
                p1[j] = p[j];
            }
            if (p2[j] < p[j])
            {
                p2[j] = p[j];
            }
        }
    }

    m_fBoxDiagLen = XMVectorGetX(XMVector3Length(XMLoadFloat3(&rightBottom) - XMLoadFloat3(&leftTop)));

    // 3. Calculate quadirc matrix for each vertex.
    FAILURE_RETURN(CalculateQuadricArray());
    FAILURE_RETURN(m_callbackSchemer.UpdateCallbackAdapt(1));

    // 4. Calculate quadric error for each vertex
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        CalculateVertexQuadricError(m_pVertArray+i);
        FAILURE_RETURN(m_callbackSchemer.UpdateCallbackAdapt(1));
    }
    
    // 5. Calculate quadric error for each edge
    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        CalculateEdgeQuadricError(m_pEdgeArray+i);
        FAILURE_RETURN(m_callbackSchemer.UpdateCallbackAdapt(1));
    }
    return hr;
}

HRESULT CProgressiveMesh::CalculateQuadricArray()
{
    HRESULT hr = S_OK;

   
    uint32_t dwQuadricNumber = m_dwFaceNumber;
    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        if (m_pEdgeArray[i].bIsBoundary)
        {
            dwQuadricNumber++;
        }
    }

    m_pQuadricArray = new (std::nothrow) QUADRICERRORMETRIC[dwQuadricNumber];
    if (!m_pQuadricArray)
    {
        return E_OUTOFMEMORY;
    }

    uint32_t dwQuadricCount = 0;
    XMFLOAT3 tempVector;
    XMFLOAT3 normal;

    double fTemp;

    try
    {
        QUADRICERRORMETRIC* pQuadric = m_pQuadricArray;
        for (size_t i=0; i<m_dwFaceNumber; i++)
        {
            PMISOCHARTFACE* pFace = m_pFaceArray + i;
            PMISOCHARTVERTEX* pVert = m_pVertArray + pFace->dwVertexID[0];
            normal = pFace->normal;
            fTemp = IsochartVec3Dot(
                &normal, 
                m_baseInfo.pVertPosition+pVert->dwIDInRootMesh);

            fTemp = -fTemp;
            /*
            pQuadric->fQA[0][0] = normal.x * normal.x;
            pQuadric->fQA[1][1] = normal.y * normal.y;
            pQuadric->fQA[2][2] = normal.z * normal.z;

            pQuadric->fQA[0][1] = normal.x * normal.y;
            pQuadric->fQA[0][2] = normal.x * normal.z;
            pQuadric->fQA[1][2] = normal.y * normal.z;

            pQuadric->fQA[1][0] = pQuadric->fQA[0][1];
            pQuadric->fQA[2][0] = pQuadric->fQA[0][2];
            pQuadric->fQA[2][1] = pQuadric->fQA[1][2];

            pQuadric->fQB[0] = normal.x * fTemp;
            pQuadric->fQB[1] = normal.y * fTemp;
            pQuadric->fQB[2] = normal.z * fTemp;
            */

            pQuadric->fQA[0][0] = DOUBLE_OP(normal.x,  normal.x, *);
            pQuadric->fQA[1][1] = DOUBLE_OP(normal.y, normal.y, *);
            pQuadric->fQA[2][2] = DOUBLE_OP(normal.z, normal.z, *);

            pQuadric->fQA[0][1] = DOUBLE_OP(normal.x, normal.y, *);
            pQuadric->fQA[0][2] = DOUBLE_OP(normal.x, normal.z, *);
            pQuadric->fQA[1][2] = DOUBLE_OP(normal.y, normal.z, *);

            pQuadric->fQA[1][0] = pQuadric->fQA[0][1];
            pQuadric->fQA[2][0] = pQuadric->fQA[0][2];
            pQuadric->fQA[2][1] = pQuadric->fQA[1][2];

            pQuadric->fQB[0] = normal.x * fTemp;
            pQuadric->fQB[1] = normal.y * fTemp;
            pQuadric->fQB[2] = normal.z * fTemp;
        

            pQuadric->fQC = fTemp * fTemp;

            for (uint32_t j = 0; j<3; j++)
            {
                pVert = m_pVertArray + pFace->dwVertexID[j];
                pVert->quadricList.push_back(dwQuadricCount);
            }

            dwQuadricCount++;
            pQuadric++;
        }

        for (size_t i=0; i<m_dwEdgeNumber; i++)
        {
            PMISOCHARTEDGE* pEdge = m_pEdgeArray + i;

            if (pEdge->bIsBoundary)
            {
                PMISOCHARTFACE* pFace = m_pFaceArray + pEdge->dwFaceID[0];

                IsochartVec3Substract(
                    &tempVector, 
                    m_baseInfo.pVertPosition
                        +m_pVertArray[pEdge->dwVertexID[1]].dwIDInRootMesh,
                    m_baseInfo.pVertPosition
                        +m_pVertArray[pEdge->dwVertexID[0]].dwIDInRootMesh);

                IsochartVec3Cross(&normal, &tempVector, &(pFace->normal));
                XMStoreFloat3(&normal, XMVector3Normalize(XMLoadFloat3(&normal)));

                fTemp = IsochartVec3Dot(
                    &normal, 
                    m_baseInfo.pVertPosition
                        +m_pVertArray[pEdge->dwVertexID[0]].dwIDInRootMesh);

                fTemp = -fTemp;

                pQuadric->fQA[0][0] = normal.x * normal.x;
                pQuadric->fQA[1][1] = normal.y * normal.y;
                pQuadric->fQA[2][2] = normal.z * normal.z;

                pQuadric->fQA[0][1] = normal.x * normal.y;
                pQuadric->fQA[0][2] = normal.x * normal.z;
                pQuadric->fQA[1][2] = normal.y * normal.z;

                pQuadric->fQA[1][0] = pQuadric->fQA[0][1];
                pQuadric->fQA[2][0] = pQuadric->fQA[0][2];
                pQuadric->fQA[2][1] = pQuadric->fQA[1][2];

                pQuadric->fQB[0] = normal.x * fTemp;
                pQuadric->fQB[1] = normal.y * fTemp;
                pQuadric->fQB[2] = normal.z * fTemp;

                pQuadric->fQC = fTemp * fTemp;

                PMISOCHARTVERTEX* pVert;
                for (size_t j=0; j<2; j++)
                {
                    pVert = m_pVertArray + pEdge->dwVertexID[j];

                    pVert->quadricList.push_back(dwQuadricCount);
                }
                dwQuadricCount++;
                pQuadric++;
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    assert(dwQuadricCount == dwQuadricNumber);
    return hr;
}

void CProgressiveMesh::CalculateVertexQuadricError(
    PMISOCHARTVERTEX* pVertex)
{
    QUADRICERRORMETRIC* pQuadric = nullptr;

    memset( &(pVertex->quadricError), 0, sizeof(QUADRICERRORMETRIC));

    for (size_t i=0; i<pVertex->quadricList.size(); i++)
    {
        pQuadric = m_pQuadricArray + pVertex->quadricList[i];
        for (size_t j=0; j<3; j++)
        {
            pVertex->quadricError.fQA[j][0] += pQuadric->fQA[j][0];
            pVertex->quadricError.fQA[j][1] += pQuadric->fQA[j][1];
            pVertex->quadricError.fQA[j][2] += pQuadric->fQA[j][2];
            pVertex->quadricError.fQB[j] += pQuadric->fQB[j];
        }
        pVertex->quadricError.fQC += pQuadric->fQC;
    }
}

void CProgressiveMesh::CalculateEdgeQuadricError(
    PMISOCHARTEDGE* pEdge)
{
    QUADRICERRORMETRIC* pQuadric = nullptr;
    
    QUADRICERRORMETRIC tempQE;
    
    float tempVector[3];
    
    PMISOCHARTVERTEX* pVertex1;
    PMISOCHARTVERTEX* pVertex2;

    pVertex1 = m_pVertArray + pEdge->dwVertexID[0];
    pVertex2 = m_pVertArray + pEdge->dwVertexID[1];

    for (size_t i= 0; i<3; i++)
    {
        tempQE.fQA[i][0]
            = pVertex1->quadricError.fQA[i][0] + pVertex2->quadricError.fQA[i][0];
        tempQE.fQA[i][1]
            = pVertex1->quadricError.fQA[i][1]  + pVertex2->quadricError.fQA[i][1];
        tempQE.fQA[i][2]
            = pVertex1->quadricError.fQA[i][2]  + pVertex2->quadricError.fQA[i][2];
        tempQE.fQB[i]
            = pVertex1->quadricError.fQB[i] + pVertex2->quadricError.fQB[i];
    }
    tempQE.fQC = pVertex1->quadricError.fQC + pVertex2->quadricError.fQC;

    for (size_t i=0; i<pVertex1->quadricList.size(); i++)
    {
        size_t j;
        for (j=0; j<pVertex2->quadricList.size(); j++)
        {
            if (pVertex1->quadricList[i] == pVertex2->quadricList[j])
            {
                break;
            }
        }
        if (j<pVertex2->quadricList.size())
        {
            pQuadric = m_pQuadricArray + pVertex1->quadricList[i];
            for (size_t k=0; k<3; k++)
            {
                tempQE.fQA[k][0] -= pQuadric->fQA[k][0];
                tempQE.fQA[k][1] -= pQuadric->fQA[k][1];
                tempQE.fQA[k][2] -= pQuadric->fQA[k][2];
                tempQE.fQB[k] -= pQuadric->fQB[k];
            }
            tempQE.fQC -= pQuadric->fQC;
        }
    }

    if (pVertex1->bIsBoundary && !pVertex2->bIsBoundary)
    {
        XMFLOAT3* pv = 
            m_baseInfo.pVertPosition+pVertex1->dwIDInRootMesh;
        tempVector[0] =  pv->x;
        tempVector[1] =  pv->y;
        tempVector[2] =  pv->z;
        pEdge->fDeleteCost = QuadricError(tempQE, tempVector);
                
        pEdge->dwDeleteWhichVertex = 1;
    }

    else if (pVertex2->bIsBoundary && !pVertex1->bIsBoundary)
    {
        XMFLOAT3* pv =
            m_baseInfo.pVertPosition+pVertex2->dwIDInRootMesh;
        tempVector[0] =  pv->x;
        tempVector[1] =  pv->y;
        tempVector[2] =  pv->z;
        
        pEdge->fDeleteCost = QuadricError(tempQE, tempVector);

        pEdge->dwDeleteWhichVertex = 0;
    }
    else
    {
        XMFLOAT3* pv =
            m_baseInfo.pVertPosition+pVertex1->dwIDInRootMesh;
        tempVector[0] =  pv->x;
        tempVector[1] =  pv->y;
        tempVector[2] =  pv->z;

        pEdge->fDeleteCost = QuadricError(tempQE, tempVector);
        pEdge->dwDeleteWhichVertex = 1;

        pv = m_baseInfo.pVertPosition+pVertex2->dwIDInRootMesh;
        tempVector[0] =  pv->x;
        tempVector[1] =  pv->y;
        tempVector[2] =  pv->z;

        double tempCost = QuadricError(tempQE, tempVector);

        if (pEdge->fDeleteCost > tempCost)
        {
            pEdge->fDeleteCost = tempCost;
            pEdge->dwDeleteWhichVertex = 0;
        }
    }

    if (pEdge->fDeleteCost < 0)
    {
        pEdge->fDeleteCost = 0;
    }
    else
    {
        pEdge->fDeleteCost = IsochartSqrt(pEdge->fDeleteCost);
    }
}

double CProgressiveMesh::QuadricError(
    QUADRICERRORMETRIC& quadricErrorMetric, float* fVector) const
{
    double tempV[3];
    double quadricError = 0.0;

    for (size_t i=0; i<3; i++)
    {
        tempV[i] = 0.0;
        for (size_t j=0; j<3; j++)
        {
            tempV[i] = tempV[i] + fVector[j]*quadricErrorMetric.fQA[i][j];
        }
    }

    for (size_t i=0; i<3; i++)
    {
        quadricError = quadricError + tempV[i] * fVector[i];
    }

    for (size_t i=0; i<3; i++)
    {
        quadricError = quadricError + 2 * quadricErrorMetric.fQB[i] * fVector[i];
    }

    quadricError = quadricError + quadricErrorMetric.fQC;

    return quadricError;

}
