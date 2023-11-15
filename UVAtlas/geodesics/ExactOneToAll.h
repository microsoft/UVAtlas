//-------------------------------------------------------------------------------------
// UVAtlas - ExactOneToAll.h
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#include "datatypes.h"

namespace GeodesicDist
{

    class CExactOneToAll
    {
    protected:
        void* m_pVertices;
        uint32_t* m_pIndices;
        uint32_t* m_pAdj;
        size_t m_dwNumBytesPerVertex;
        size_t m_dwNumFaces;
        size_t  m_dwNumVertices;
        uint32_t m_dwSrcVertexIdx;

        EdgeWindow m_AnotherNewWindow;
        EdgeWindow m_NewExistingWindow;

        TypeEdgeWindowsHeap m_EdgeWindowsHeap;

        virtual void CutHeapTopData(EdgeWindow& EdgeWindowOut);
        void ProcessNewWindow(_In_ EdgeWindow* pNewEdgeWindow);
        void IntersectWindow(_In_ EdgeWindow* pExistingWindow,
            _In_ EdgeWindow* pNewWindow,
            bool* pExistingWindowChanged,
            bool* pNewWindowChanged,
            bool* pExistingWindowNotAvailable,
            bool* pNewWindowNotAvailable);
        void GenerateWindowsAroundSaddleOrBoundaryVertex(const EdgeWindow& WindowToBePropagated,
            const uint32_t dwSaddleOrBoundaryVertexId,
            std::vector<EdgeWindow>& WindowsOut);
        void InternalRun();
        void AddWindowToHeapAndEdge(const EdgeWindow& WindowToAdd);

    public:
        TypeEdgeList m_EdgeList;
        TypeFaceList m_FaceList;
        TypeVertexList m_VertexList;

        EdgeWindow tmpWindow0; // window that is to be processed (to be inserted on edge and into heap)
        EdgeWindow WindowToBePropagated; // window that is just popped off the heap

        CExactOneToAll();
        virtual ~CExactOneToAll() = default;

        // set the source vertex index before run
        void SetSrcVertexIdx(const uint32_t dwSrcVertexIdx);

        // run the algorithm
        void Run();
    };

}
