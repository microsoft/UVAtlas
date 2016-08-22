//-------------------------------------------------------------------------------------
// UVAtlas - meshcommon.inl
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

// [SGSH02] SANDER P., GORTLER S., SNYDER J., HOPPE H.:
// Signal-specialized parameterization
//  In Proceedings of Eurographics Workshop on Rendering 2002(2002)

namespace Isochart
{

/////////////////////////////////////////////////////////////
///////////////////////////Tool-Methods//////////////////////
/////////////////////////////////////////////////////////////

inline float CIsochartMesh::CaculateUVDistanceSquare(
    DirectX::XMFLOAT2& v0,
    DirectX::XMFLOAT2& v1) const
{
    return (v0.x-v1.x)*(v0.x-v1.x) + (v0.y-v1.y)*(v0.y-v1.y);
}

// Calculate a face's area in the U-V space.
inline float CIsochartMesh::CalculateUVFaceArea(
        ISOCHARTFACE& face) const
{
    return CalculateUVFaceArea(
        m_pVerts[face.dwVertexID[0]].uv,
        m_pVerts[face.dwVertexID[1]].uv,
        m_pVerts[face.dwVertexID[2]].uv);
}

// Caculate face area using cross multiplication
inline float CIsochartMesh::CalculateUVFaceArea(
        DirectX::XMFLOAT2& v0, 
        DirectX::XMFLOAT2& v1,
        DirectX::XMFLOAT2& v2) const
{
    float fA = ((v1.x-v0.x)*(v2.y-v0.y)-(v2.x-v0.x)*(v1.y-v0.y))/2;

    if (fA < 0)
    {
        fA = -fA;
    }

    return fA;
}

// Caculate parameterized chart's area
inline float CIsochartMesh::CalculateChart2DArea() const
{
    float fChart2DArea = 0;
    
    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {		
        fChart2DArea += CalculateUVFaceArea(*pFace);
        pFace++;
    }

    return fChart2DArea;
}

// Caculate chart's 3D area
inline float CIsochartMesh::CalculateChart3DArea() const
{
    float fChart3DArea = 0;
    
    ISOCHARTFACE* pFace = m_pFaces;
    for (size_t i=0; i<m_dwFaceNumber; i++)
    {		
        fChart3DArea += m_baseInfo.pfFaceAreaArray[pFace->dwIDInRootMesh];
        pFace++;
    }
    return fChart3DArea;
}

// Check if parameterization cause some edge overlapping.
inline HRESULT CIsochartMesh::IsParameterizationOverlapping(
    CIsochartMesh* pMesh,
    bool& bIsOverlapping)
{

    ISOCHARTEDGE* pEdge1 = nullptr;
    ISOCHARTEDGE* pEdge2 = nullptr;

    // Collect all boundary edges
    std::vector<ISOCHARTEDGE*> boundaryEdgeList;

    try
    {
        for (size_t i=0; i < pMesh->m_edges.size(); i++)
        {
            pEdge1 = &(pMesh->m_edges[i]);
            if (pEdge1->bIsBoundary)
            {
                boundaryEdgeList.push_back(pEdge1);
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    assert(!boundaryEdgeList.empty());

    for (size_t i=0; i < boundaryEdgeList.size()-1; i++)
    {
        pEdge1 = boundaryEdgeList[i];
        for (size_t j=i+1; j<boundaryEdgeList.size(); j++)
        {
            pEdge2 = boundaryEdgeList[j];

            // if two edges connect together, although they have
            // intersection, it's not counted as overlapping
            if (pEdge1->dwVertexID[0] == pEdge2->dwVertexID[0]
            ||pEdge1->dwVertexID[0] == pEdge2->dwVertexID[1]
            ||pEdge1->dwVertexID[1] == pEdge2->dwVertexID[0]
            ||pEdge1->dwVertexID[1] == pEdge2->dwVertexID[1])
            {
                continue;
            }
            // If two edges doesn't connect together, but have
            // intersection, overlapping occurs.
            if (IsochartIsSegmentsIntersect(
                pMesh->m_pVerts[pEdge1->dwVertexID[0]].uv,
                pMesh->m_pVerts[pEdge1->dwVertexID[1]].uv,
                pMesh->m_pVerts[pEdge2->dwVertexID[0]].uv,
                pMesh->m_pVerts[pEdge2->dwVertexID[1]].uv))
            {
                bIsOverlapping = true;
                return S_OK;
            }
            
        }
    }

    bIsOverlapping = false;
    return S_OK;
}


// Calculate the Euclid distance between to vertex on original mesh.
inline float CIsochartMesh::CalculateVextexDistance(
    ISOCHARTVERTEX& v0, ISOCHARTVERTEX& v1) const
{
    using namespace DirectX;

    XMVECTOR pv0 = XMLoadFloat3(m_baseInfo.pVertPosition + v0.dwIDInRootMesh);
    XMVECTOR pv1 = XMLoadFloat3(m_baseInfo.pVertPosition + v1.dwIDInRootMesh);
    XMVECTOR v2 = pv1 - pv0;
    return XMVectorGetX(XMVector3Length(v2));
}

// When performing canonical parameterization to each face, an origin,
// X-axis and Y-axis was specified. Using these information can compute
// the 2-D reflection of any point in 3-D face.
// ( In our algorithm, origin alwasy the first vertex of face)
inline void CIsochartMesh::Vertex3DTo2D(
    uint32_t dwFaceIDInRootMesh,
    const DirectX::XMFLOAT3* pOrg,
    const DirectX::XMFLOAT3* p3D,
    DirectX::XMFLOAT2* p2D)
{
    using namespace DirectX;

    XMFLOAT3* pAxisX
        = m_baseInfo.pFaceCanonicalParamAxis
        + 2 * dwFaceIDInRootMesh;

    XMFLOAT3* pAxisY = pAxisX + 1;

    XMVECTOR tempVector = XMLoadFloat3(p3D) - XMLoadFloat3(pOrg);
    p2D->x = XMVectorGetX(XMVector3Dot(tempVector, XMLoadFloat3(pAxisX)));
    p2D->y = XMVectorGetX(XMVector3Dot(tempVector, XMLoadFloat3(pAxisY)));
}

// Caculate the signal length of two vertices in one 3D face
// See more detail of this algorithm in [SGSH02]
inline float CIsochartMesh::CalculateSignalLengthOnOneFace(
    DirectX::XMFLOAT3* p3D0,
    DirectX::XMFLOAT3* p3D1,
    uint32_t dwFaceID)
{
    using namespace DirectX;

    if (INVALID_FACE_ID == dwFaceID)
    {
        return 0;
    }

    ISOCHARTFACE* pFace = m_pFaces + dwFaceID;

    // 1. Calculate the 2-D reflection of 2 3D vertex
    ISOCHARTVERTEX* pVertex = 
        m_pVerts + pFace->dwVertexID[0];

    XMFLOAT3* pOrg =
        m_baseInfo.pVertPosition + pVertex->dwIDInRootMesh;

    XMFLOAT2 v2d0;
    XMFLOAT2 v2d1;

    Vertex3DTo2D(
        pFace->dwIDInRootMesh, 
        pOrg, p3D0, &v2d0);

    Vertex3DTo2D(
        pFace->dwIDInRootMesh, 
        pOrg, p3D1, &v2d1);

    // 2. Using affine transformation to calculate signal length
    float fDeltaX = v2d1.x - v2d0.x;
    float fDeltaY = v2d1.y - v2d0.y;

    const FLOAT3* pIMT = m_baseInfo.pfIMTArray+pFace->dwIDInRootMesh;
    float fLength;
    fLength
        = (*pIMT)[0]*fDeltaX*fDeltaX
        + (*pIMT)[2]*fDeltaY*fDeltaY
        + 2*(*pIMT)[1]*fDeltaX*fDeltaY;

    fLength = IsochartSqrtf(fLength);
    return fLength;
}

// When computing signal length along internal edge, two adjacent faces
// are involved, individually caculate edge's signal length on the two
// faces and use average.
inline float CIsochartMesh::CalculateEdgeSignalLength(
    DirectX::XMFLOAT3* p3D0,
    DirectX::XMFLOAT3* p3D1,
    uint32_t dwAdjacentFaceID0,
    uint32_t dwAdjacentFaceID1)
{
    float fLength0;

    fLength0 = CalculateSignalLengthOnOneFace(
        p3D0, p3D1, dwAdjacentFaceID0);

    if (INVALID_FACE_ID == dwAdjacentFaceID1)
    {
        return fLength0;
    }
    else
    {
        float fLength1;
        fLength1 = CalculateSignalLengthOnOneFace(
            p3D0, p3D1, dwAdjacentFaceID1);

        return (fLength0 + fLength1) * 0.5f;
    }
}

inline float CIsochartMesh::CalculateEdgeSignalLength(ISOCHARTEDGE& edge)
{
    assert (INVALID_FACE_ID != edge.dwFaceID[0]);

    assert (
        (INVALID_FACE_ID == edge.dwFaceID[1] && edge.bIsBoundary)
        ||(INVALID_FACE_ID != edge.dwFaceID[1] || edge.bIsBoundary));

    float fTestLength = 0;
    DirectX::XMFLOAT3* pv0 =
        m_baseInfo.pVertPosition + m_pVerts[edge.dwVertexID[0]].dwIDInRootMesh;
    
    DirectX::XMFLOAT3* pv1 =
        m_baseInfo.pVertPosition + m_pVerts[edge.dwVertexID[1]].dwIDInRootMesh;

    fTestLength = CalculateEdgeSignalLength(
        pv0, pv1, edge.dwFaceID[0], edge.dwFaceID[1]);

    return fTestLength;
}

// Calculate edge's normal and signal length
inline void CIsochartMesh::CalculateChartEdgeLength()
{
    for (size_t i=0; i<m_dwEdgeNumber; i++)
    {
        ISOCHARTEDGE& edge = m_edges[i];
        edge.fLength = CalculateVextexDistance(
            m_pVerts[edge.dwVertexID[0]], m_pVerts[edge.dwVertexID[1]]);

        if (IsIMTSpecified())
        {
            edge.fSignalLength = CalculateEdgeSignalLength(edge);
        }
        else
        {
            edge.fSignalLength = 0;
        }
    }
}

inline void Rotate2DPoint(
    DirectX::XMFLOAT2& uvOut,
    const DirectX::XMFLOAT2& uvIn,
    const DirectX::XMFLOAT2& center,
    float fSin,
    float fCos)
{
    float x = 
        (uvIn.x-center.x)*fCos - (uvIn.y-center.y)*fSin;
    float y = 
        (uvIn.x-center.x)*fSin + (uvIn.y-center.y)*fCos;

    uvOut.x = x + center.x;
    uvOut.y = y + center.y;
}

// Rotate chart around origin
inline void CIsochartMesh::RotateChart(
    const DirectX::XMFLOAT2& center, float fAngle) const
{
    float fCos = cosf(fAngle);
    float fSin = sinf(fAngle);

    ISOCHARTVERTEX* pVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        Rotate2DPoint(
            pVertex->uv, pVertex->uv, center, fSin, fCos);

        pVertex++;
    }
}

// Rotate chart boundary, get bounding box
inline void CIsochartMesh::GetRotatedChartBoundingBox(
    const DirectX::XMFLOAT2& center,
    float fAngle,
    DirectX::XMFLOAT2& minBound,
    DirectX::XMFLOAT2& maxBound) const
{
    minBound.x = minBound.y = FLT_MAX;
    maxBound.x = maxBound.y = -FLT_MAX;

    DirectX::XMFLOAT2 tempCoordinate;
    ISOCHARTVERTEX* pVertex = m_pVerts;

    float fCos = cosf(fAngle);
    float fSin = sinf(fAngle);
    
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        if (!pVertex->bIsBoundary)
        {
            pVertex++;
            continue;
        }

        Rotate2DPoint(
            tempCoordinate,
            pVertex->uv,
            center,
            fSin,
            fCos);

        minBound.x = std::min(tempCoordinate.x, minBound.x);
        minBound.y = std::min(tempCoordinate.y, minBound.y);

        maxBound.x = std::max(tempCoordinate.x, maxBound.x);
        maxBound.y = std::max(tempCoordinate.y, maxBound.y);
        
        pVertex++;
    }
}

// Get the minial bounding box of current chart
inline void CIsochartMesh::CalculateChartMinimalBoundingBox(
    size_t dwRotationCount,
    DirectX::XMFLOAT2& minBound,
    DirectX::XMFLOAT2& maxBound) const
{
    using namespace DirectX;
    
    float fMinRectArea = 0;
    float fMinAngle = 0;

    XMFLOAT2 tempMinBound;
    XMFLOAT2 tempMaxBound;

    float tempArea;

    minBound.x = minBound.y = FLT_MAX;
    maxBound.x = maxBound.y = -FLT_MAX;

    for (size_t ii=0; ii<m_dwVertNumber; ii++)
    {
        const XMFLOAT2& uv = m_pVerts[ii].uv;
        minBound.x = std::min(uv.x, minBound.x);
        minBound.y = std::min(uv.y, minBound.y);

        maxBound.x = std::max(uv.x, maxBound.x);
        maxBound.y = std::max(uv.y, maxBound.y);
    }

    XMFLOAT2 center;
    XMStoreFloat2(&center, (XMLoadFloat2(&minBound) + XMLoadFloat2(&maxBound)) / 2);

    fMinRectArea = IsochartBoxArea(minBound, maxBound);
    fMinAngle = 0;
    
    //To calculate bounding box, only need to rotate with in PI/2 around the chart's center
    for (size_t dwRotID = 1; dwRotID <dwRotationCount; dwRotID++)
    {
        float fAngle = dwRotID* XM_PI /(dwRotationCount*2);

        GetRotatedChartBoundingBox(
            center, fAngle, tempMinBound, tempMaxBound);
        tempArea = IsochartBoxArea(tempMinBound, tempMaxBound);

        if (tempArea < fMinRectArea)
        {
            fMinRectArea = tempArea;
            fMinAngle = fAngle;
            minBound = tempMinBound;
            maxBound = tempMaxBound;
        }
    }
    
    if (fMinAngle > ISOCHART_ZERO_EPS)
    {
        RotateChart(center, fMinAngle);
    }
}

inline CIsochartMesh* CIsochartMesh::CreateNewChart(
    VERTEX_ARRAY& vertList,
    std::vector<uint32_t>& faceList,
    bool bIsSubChart) const
{
    auto pChart = new (std::nothrow) CIsochartMesh(m_baseInfo, m_callbackSchemer, m_IsochartEngine);
    if (!pChart)
    {
        return nullptr;
    }

    pChart->m_pFather = const_cast<CIsochartMesh* >(this);
    pChart->m_bVertImportanceDone = m_bVertImportanceDone;
    pChart->m_bIsSubChart = bIsSubChart;
    pChart->m_fBoxDiagLen = m_fBoxDiagLen;
    pChart->m_dwVertNumber = vertList.size();
    pChart->m_dwFaceNumber = faceList.size();

    pChart->m_pVerts = new (std::nothrow) ISOCHARTVERTEX[pChart->m_dwVertNumber];
    pChart->m_pFaces = new (std::nothrow) ISOCHARTFACE[pChart->m_dwFaceNumber];

    if (!pChart->m_pVerts || !pChart->m_pFaces)
    {
        delete pChart; // vertex and face buffer will be deleted
        return nullptr; // in destructor.
    }

    std::unique_ptr<uint32_t []> pdwVertMap(new (std::nothrow) uint32_t[m_dwVertNumber]);
    if (!pdwVertMap)
    {
        delete pChart;
        return nullptr;
    }

    ISOCHARTVERTEX* pOldVertex = nullptr;
    ISOCHARTVERTEX* pNewVertex = pChart->m_pVerts;
    for (uint32_t i = 0; i<pChart->m_dwVertNumber; i++)
    {
        pOldVertex = vertList[i];
        pNewVertex->dwID = i;
        pNewVertex->dwIDInRootMesh = pOldVertex->dwIDInRootMesh;
        pNewVertex->dwIDInFatherMesh = pOldVertex->dwID;
        pNewVertex->bIsBoundary = pOldVertex->bIsBoundary;
        pNewVertex->nImportanceOrder = pOldVertex->nImportanceOrder;
        pdwVertMap[pOldVertex->dwID] = i;
        pNewVertex++;
    }

    ISOCHARTFACE* pNewFace = pChart->m_pFaces;
    for (uint32_t i = 0; i<pChart->m_dwFaceNumber; i++)
    {
        ISOCHARTFACE* pOldFace = m_pFaces + faceList[i];
        pNewFace->dwID = i;
        pNewFace->dwIDInRootMesh = pOldFace->dwIDInRootMesh;
        pNewFace->dwIDInFatherMesh = pOldFace->dwID;
        for (size_t j=0; j<3; j++)
        {
            pNewFace->dwVertexID[j] = 
                pdwVertMap[pOldFace->dwVertexID[j]];
        }
        pNewFace++;
    }

    pChart->m_bNeedToClean = m_bNeedToClean;
    return pChart;
}

inline HRESULT CIsochartMesh::MoveTwoValueToHead(
    std::vector<uint32_t>& list,
    uint32_t dwIdx1,
    uint32_t dwIdx2)
{

    if (list.size() <3)
    {
        return S_OK;
    }

    // if the 2 landmark aready in head
    if ((0 == dwIdx1 || 0 == dwIdx2) &&
        (1 == dwIdx1 || 1 == dwIdx2))
    {
        return S_OK;
    }
    else if (0 == dwIdx1)
    {
        std::swap(list[1],list[dwIdx2]);
    }
    else if (0 == dwIdx2)
    {
        std::swap(list[1], list[dwIdx1]);
    }
    else if (1 == dwIdx1)
    {
        std::swap(list[0],list[dwIdx2]);
    }
    else if (1 == dwIdx2)
    {
        std::swap(list[0],list[dwIdx1]);
    }
    else
    {
        std::swap(list[0], list[dwIdx1]);
        std::swap(list[1], list[dwIdx2]);
    }
    return S_OK;
}

}
