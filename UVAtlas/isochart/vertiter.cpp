//-------------------------------------------------------------------------------------
// UVAtlas - vertiter.cpp
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
#include "vertiter.h"
#include "isochartmesh.h"

using namespace Isochart;

CVertIter::CVertIter(const uint32_t* rgdwAdjacency)
    :m_rgdwAdjacency(rgdwAdjacency),
    m_dwCurrentFaceID(0),
    m_dwCurrentVertIdx(0),
    m_dwPrevFaceID(0),
    m_dwPrevVertIdx(0),
    m_dwBeginFaceID(0),
    m_dwBeginVertID(0),
    m_bclockwise(false)
{
}

bool CVertIter::Init(
    uint32_t dwFaceID, uint32_t dwVertIdx, size_t dwTotoalFaceCount)
{
    m_dwCurrentFaceID = dwFaceID;
    m_dwCurrentVertIdx = dwVertIdx;
    m_dwBeginFaceID = m_dwCurrentFaceID;
    m_dwBeginVertID = m_dwCurrentVertIdx;

    m_bclockwise = false;

    size_t dwItCount = 0;
    while(HasNextFace() && dwItCount <= dwTotoalFaceCount)
    {
        if (!NextFace())
        {
            return false;
        }
        dwItCount++;
    }

    if (dwItCount > dwTotoalFaceCount)
    {
        return false;
    }

    m_dwBeginFaceID = m_dwCurrentFaceID;
    m_dwBeginVertID = m_dwCurrentVertIdx;
    m_dwPrevFaceID = INVALID_FACE_ID;
    m_dwPrevVertIdx = INVALID_INDEX;

    m_bclockwise = true;
    return true;
}

uint32_t CVertIter::GetNextFace()
{
    if (m_bclockwise)
    {
        return
        m_rgdwAdjacency[m_dwCurrentFaceID*3 + (m_dwCurrentVertIdx+2)%3];
    }
    else
    {
        return
        m_rgdwAdjacency[m_dwCurrentFaceID*3 + m_dwCurrentVertIdx];
    }
}

bool CVertIter::HasNextFace()
{
    uint32_t dwNextFaceID = GetNextFace();
    if (dwNextFaceID == INVALID_FACE_ID
        ||dwNextFaceID == m_dwBeginFaceID)
    {
        return false;
    }
    return true;
}

bool CVertIter::NextFace()
{
    uint32_t dwNextFaceID = GetNextFace();

    uint32_t dwNextVertIdx = INVALID_INDEX;
    const uint32_t *pAdj = m_rgdwAdjacency + dwNextFaceID * 3;

    for (uint32_t i=0; i<3; i++)
    {
        if (pAdj[i] == m_dwCurrentFaceID)
        {
            if (m_bclockwise)
            {
                dwNextVertIdx = i;
                break;
            }
            else
            {
                dwNextVertIdx = (i+1)%3;
                break;
            }
        }
    }
    assert(dwNextVertIdx != INVALID_INDEX);
    
    if(dwNextFaceID != INVALID_FACE_ID
        && dwNextFaceID == m_dwPrevFaceID
        && dwNextVertIdx == m_dwPrevVertIdx)
    {
        return false;
    }

    m_dwPrevFaceID = m_dwCurrentFaceID;
    m_dwPrevVertIdx = m_dwCurrentVertIdx;
    m_dwCurrentFaceID = dwNextFaceID;
    m_dwCurrentVertIdx = dwNextVertIdx;

    return true;
}
