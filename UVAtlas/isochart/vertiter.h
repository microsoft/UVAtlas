//-------------------------------------------------------------------------------------
// UVAtlas - vertiter.h
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

namespace Isochart
{
    class CVertIter
    {
    public:
        CVertIter(const uint32_t* rgdwAdjacency);
        bool Init(uint32_t dwFaceID, uint32_t dwVertIdx, size_t dwTotoalFaceCount);
        uint32_t GetNextFace();
        bool HasNextFace();
        bool NextFace();
        uint32_t GetBeginFace(){ return m_dwBeginFaceID; }
        uint32_t GetBeginVertIdx(){ return m_dwBeginVertID; }
        uint32_t GetCurrentFace(){ return m_dwCurrentFaceID; }
        uint32_t GetCurrentVertIdx(){ return m_dwCurrentVertIdx; }
        uint32_t GetPrevFace() { return m_dwPrevFaceID; }
        uint32_t GetPrevVertIdx() { return m_dwPrevVertIdx; }

    private:
        const uint32_t* m_rgdwAdjacency;
        uint32_t m_dwCurrentFaceID;
        uint32_t m_dwCurrentVertIdx;
        uint32_t m_dwPrevFaceID;
        uint32_t m_dwPrevVertIdx;
        uint32_t m_dwBeginFaceID;
        uint32_t m_dwBeginVertID;
        bool m_bclockwise;
    };
}
