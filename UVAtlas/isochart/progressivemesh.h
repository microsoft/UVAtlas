//-------------------------------------------------------------------------------------
// UVAtlas - progressivemesh.h
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

#pragma once

#include "isochartmesh.h"

namespace Isochart
{
    //////////////////////////////////////////////////////////
    //Progressive Mesh structures ////////////////////////////
    ///////////////////////////////////////////////////////////

    typedef CMaxHeap<double, uint32_t> CCostHeap;
    typedef CMaxHeapItem<double, uint32_t> CCostHeapItem;

    // Face attribute use to compute the distance from point to a plane
    // fromed by 3 points of the face
    struct QUADRICERRORMETRIC
    {
        double fQA[3][3];
        double fQB[3];
        double fQC;
    };

    // PM : Progressive Mesh

    // Vertex in progressive mesh
    struct PMISOCHARTVERTEX
    {
        uint32_t dwID;                          // Index in the vertice array of current mesh
        uint32_t dwIDInRootMesh;                // the ID of this vertex in root chart
        int nImportanceOrder;                   // The order to be deleted. -1 means not delete
        bool bIsBoundary;                       // indicate if this vertex is a boundary vertex

        std::vector<uint32_t> vertAdjacent;     // ID of vertices having edge between this vertex
        std::vector<uint32_t> faceAdjacent;     // ID of faces using this vertex
        std::vector<uint32_t> edgeAdjacent;     // ID of edges using this vertex

        std::vector<uint32_t> quadricList;      // quadric errors of faces adjacent to the vertex
        QUADRICERRORMETRIC quadricError;        // quadirc error of this vertex
        bool bIsDeleted;                        // Indicate if this vertex has been deleted.
    };

    // Face in prgressive mesh
    struct PMISOCHARTFACE
    {
        uint32_t dwID; // Index in the faces array of current mesh
        uint32_t dwVertexID[3]; // The ID of 3 verices of this face
        uint32_t dwEdgeID[3]; // The ID of 3 edges of this face
    
        DirectX::XMFLOAT3 normal; // Face normal vector
        bool bIsDeleted; // Indicate if the face has been deleted.
    };

    // Edge in progressive mesh
    struct PMISOCHARTEDGE
    {
        uint32_t dwID; // Index in the edge array of current mesh
        uint32_t dwVertexID[2]; // The ID of 2 vertices of this edge
        uint32_t dwFaceID[2]; // The ID of 2 faces at the two sides of the edge 
                           // if the edge has only one face beside it, 
                           // dwFaceID[1] should be INVALID_FACE_ID
        uint32_t dwOppositVertID[2]; // Vertex opposite to the edge in the face
        bool bIsBoundary; // Indicate if the edge is a boundary.

        double fDeleteCost; // Delete cost

        uint32_t dwDeleteWhichVertex; // 0-Delete Vertex[0] , 1-Delete Vertex[1]
        bool bIsDeleted; // indicate if the edge has been deleted.
    };

    // Progressive Mesh
    class CProgressiveMesh
    {
    public:
        CProgressiveMesh(
            const CBaseMeshInfo &baseInfo,
            CCallbackSchemer& callbackSchemer);

        CProgressiveMesh(CProgressiveMesh const&) = delete;
        CProgressiveMesh& operator=(CProgressiveMesh const&) = delete;

        ~CProgressiveMesh();

        HRESULT Initialize(CIsochartMesh& mesh);

        void Clear();

        HRESULT Simplify();

        int GetVertexImportance(uint32_t dwIndex) const
        {
            assert(m_pVertArray != 0);
            assert(dwIndex < m_dwVertNumber);
            return m_pVertArray[dwIndex].nImportanceOrder; 
        }

    private:

        bool PrepareDeletingEdge(
            PMISOCHARTEDGE* pCurrentEdge,
            PMISOCHARTVERTEX** ppReserveVertex,
            PMISOCHARTVERTEX** ppDeleteVertex,
            bool& bIsGeodesicValid);

        bool IsProgressiveMeshToplogicValid(
            PMISOCHARTEDGE* pEdge,
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex) const;

        bool IsProgressiveMeshGeometricValid(
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex) const;

        bool IsEdgeOppositeToVertex(
            PMISOCHARTEDGE* pEdge, 
            PMISOCHARTVERTEX* pVertex) const;

        HRESULT DeleteCurrentEdge(
            CCostHeap& heap,
            CCostHeapItem* pHeapItems,
            PMISOCHARTEDGE* pCurrentEdge,
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);

        void DeleteFacesFromSufferedVertsList(
            PMISOCHARTEDGE* pCurrentEdge,
            PMISOCHARTVERTEX* pReserveVertex);

        void UpdateSufferedEdgesAttrib(
            CCostHeap& heap,
            CCostHeapItem* pHeapItems,
            PMISOCHARTEDGE* pCurrentEdge,
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);

        PMISOCHARTEDGE* GetSufferedEdges(
            PMISOCHARTEDGE* pCurrentEdge,
            PMISOCHARTEDGE* pEdgeToDeleteVert,
            PMISOCHARTVERTEX* pReserveVertex);

        void ProcessBoundaryEdge(
            CCostHeap& heap,
            CCostHeapItem* pHeapItems,
            PMISOCHARTEDGE* pEdgeToDeleteVert,
            PMISOCHARTEDGE* pEdgeToReserveVert,
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);

        void ProcessInternalEdge(
            PMISOCHARTEDGE* pEdgeToDeleteVert,
            PMISOCHARTEDGE* pEdgeToReserveVert,
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);


        HRESULT ReplaceDeleteVertWithReserveVert(
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);

        void UpdateReservedVertsAttrib(
            PMISOCHARTVERTEX* pReserveVertex,
            PMISOCHARTVERTEX* pDeleteVertex);

        void UpdateSufferedEdgesCost(
            CCostHeap& heap,
            CCostHeapItem* pHeapItems,
            PMISOCHARTVERTEX* pReserveVertex);

        HRESULT CreateProgressiveMesh(
            CIsochartMesh& mesh);

        HRESULT CalculateQuadricErrorMetric();

        HRESULT CalculateQuadricArray();

        void CalculateVertexQuadricError(
            PMISOCHARTVERTEX* pVertex);

        void CalculateEdgeQuadricError(
            PMISOCHARTEDGE* pEdge);

        double QuadricError(
            QUADRICERRORMETRIC& quadricErrorMetric,
            float* fVector) const;

    private:
        PMISOCHARTVERTEX* m_pVertArray;
        PMISOCHARTFACE* m_pFaceArray;
        PMISOCHARTEDGE* m_pEdgeArray;

        QUADRICERRORMETRIC* m_pQuadricArray;

        uint32_t m_dwVertNumber;
        uint32_t m_dwFaceNumber;
        uint32_t m_dwEdgeNumber;
        float m_fBoxDiagLen;

        const CBaseMeshInfo& m_baseInfo; 
        CCallbackSchemer& m_callbackSchemer;
    };
}
