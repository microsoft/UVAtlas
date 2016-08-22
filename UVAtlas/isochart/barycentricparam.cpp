//-------------------------------------------------------------------------------------
// UVAtlas - BarycentricParam.cpp
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
#include "sparsematrix.hpp"

using namespace Isochart;
using namespace DirectX;

namespace
{
    const size_t BC_MAX_ITERATION = 10000;

    class CBoundaryIter
    {
    private:
        uint32_t m_dwInit;
        uint32_t m_dwPrev;
        uint32_t m_dwCurr;
        ISOCHARTVERTEX* m_pVerts;
        const XMFLOAT3* m_pVert3dPos;
    public:
        CBoundaryIter(
            uint32_t dwInit,
            ISOCHARTVERTEX* pVerts,
            const XMFLOAT3* pVert3dPos)
            :m_dwInit(dwInit),
            m_dwPrev(dwInit),
            m_dwCurr(dwInit),
            m_pVerts(pVerts),
            m_pVert3dPos(pVert3dPos)
        {
        }

        uint32_t Next()
        {
            auto& vertAdjacent = m_pVerts[m_dwCurr].vertAdjacent;
            uint32_t dwAdjacent0 = vertAdjacent[0];
            uint32_t dwAdjacent1 = vertAdjacent[vertAdjacent.size() - 1];

            assert(m_pVerts[dwAdjacent0].bIsBoundary);
            assert(m_pVerts[dwAdjacent1].bIsBoundary);

            uint32_t dwNext = (dwAdjacent0 != m_dwPrev) ? dwAdjacent0 : dwAdjacent1;

            if (dwNext == m_dwInit)
            {
                m_dwPrev = m_dwCurr;
                m_dwCurr = m_dwInit;
                return INVALID_VERT_ID;
            }
            m_dwPrev = m_dwCurr;
            m_dwCurr = dwNext;
            return dwNext;
        }

        float GetCurrentEdgeLength()
        {
            const XMFLOAT3* p1 =
                m_pVert3dPos + m_pVerts[m_dwCurr].dwIDInRootMesh;

            const XMFLOAT3* p2 =
                m_pVert3dPos + m_pVerts[m_dwPrev].dwIDInRootMesh;

            return IsochartSqrtf((p1->x - p2->x)*(p1->x - p2->x) +
                (p1->y - p2->y)*(p1->y - p2->y) +
                (p1->z - p2->z)*(p1->z - p2->z));
        }
    };
}

HRESULT CIsochartMesh::GenerateVertexMap(
    std::vector<uint32_t>& vertMap,
    size_t& dwBoundaryCount,
    size_t& dwInternalCount)
{
    HRESULT hr = S_OK;

    try
    {
        vertMap.resize(m_dwVertNumber);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    dwBoundaryCount = 0;
    dwInternalCount = 0;
    for (size_t ii=0; ii<m_dwVertNumber; ii++)
    {
        if (m_pVerts[ii].bIsBoundary)
        {
            vertMap[ii] = static_cast<uint32_t>(dwBoundaryCount++);
        }
        else
        {
            vertMap[ii] = static_cast<uint32_t>(dwInternalCount++);
        }
    }
    return hr;
}

HRESULT CIsochartMesh::GenerateBoundaryCoord(
    std::vector<double>& boundTable,
    size_t dwBoundaryCount,
    const std::vector<uint32_t>& vertMap)
{
    HRESULT hr = S_OK;

    uint32_t dwInit = INVALID_VERT_ID;
    for (uint32_t ii=0; ii<m_dwVertNumber; ii++)
    {
        if (m_pVerts[ii].bIsBoundary)
        {
            dwInit = ii;
            break;
        }
    }
    if (INVALID_VERT_ID == dwInit)
    {
        return hr;
    }

    try
    {
        boundTable.resize(dwBoundaryCount * 2);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    boundTable[vertMap[dwInit]*2] = 0;
    CBoundaryIter it(
        dwInit,
        m_pVerts,
        m_baseInfo.pVertPosition);

    uint32_t dwCurr = 0;
    double totoLength = 0;
    while((dwCurr = it.Next()) != INVALID_VERT_ID)
    {
        totoLength += it.GetCurrentEdgeLength();
        boundTable[vertMap[dwCurr]*2] = totoLength;
    }
    totoLength += it.GetCurrentEdgeLength();

    for (size_t ii=0; ii<boundTable.size(); ii+=2)
    {
        double angle = 2 * boundTable[ii] * M_PI / totoLength;
        boundTable[ii] = static_cast<double>(cos(angle));
        boundTable[ii+1] = static_cast<double>(sin(angle));		
    }
    return hr;
}

HRESULT CIsochartMesh::InitializeBarycentricEquation(
    CSparseMatrix<double>& A,
    CVector<double>& BU,
    CVector<double>& BV,
    const std::vector<double>& boundTable,
    const std::vector<uint32_t>& vertMap)
{
    HRESULT hr = S_OK;

    CSparseMatrix<double> orgA;
    CVector<double> orgBU, orgBV;

    // 1. Allocate memory
    size_t dwOrgADim = m_dwVertNumber - boundTable.size()/2;

    try
    {
        orgA.resize(dwOrgADim, dwOrgADim);
        orgBU.resize(dwOrgADim);
        orgBV.resize(dwOrgADim);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 2. Fill the linear equation
    for (size_t ii=0; ii<m_dwVertNumber; ii++)
    {
        if (m_pVerts[ii].bIsBoundary)
        {
            continue;
        }
    
        auto& adjacent = m_pVerts[ii].vertAdjacent;
        double bu = 0, bv = 0;

        orgA.setItem(vertMap[ii], vertMap[ii], double(adjacent.size()));
        for (size_t jj=0; jj<adjacent.size(); jj++)
        {
            uint32_t dwAdj = adjacent[jj];
            
            if (m_pVerts[dwAdj].bIsBoundary)
            {
                bu += boundTable[vertMap[dwAdj]*2];
                bv += boundTable[vertMap[dwAdj]*2+1];
            }
            else
            {
                orgA.setItem(vertMap[ii], vertMap[dwAdj], double(-1));	
            }
        }
        orgBU[vertMap[ii]] = bu;
        orgBV[vertMap[ii]] = bv;
    }

    // 3. get Symmetric matrix
    // A' = A^T * A
    if (!CSparseMatrix<double>::Mat_Trans_MUL_Mat(A, orgA))
    {
        return E_OUTOFMEMORY;
    }

    // B' = A^T * b
    if (!CSparseMatrix<double>::Mat_Trans_Mul_Vec(BU, orgA, orgBU))
    {
        return E_OUTOFMEMORY;
    }

    if (!CSparseMatrix<double>::Mat_Trans_Mul_Vec(BV, orgA, orgBV))
    {
        return E_OUTOFMEMORY;
    }
    
    return hr;
}

HRESULT CIsochartMesh::AssignBarycentricResult(
    CVector<double>& U,
    CVector<double>& V,	
    const std::vector<double>& boundTable,
    const std::vector<uint32_t>& vertMap)
{
    HRESULT hr = S_OK;
    for (size_t ii=0; ii<m_dwVertNumber; ii++)
    {
        if (m_pVerts[ii].bIsBoundary)
        {
            m_pVerts[ii].uv.x = static_cast<float>(boundTable[vertMap[ii]*2]);
            m_pVerts[ii].uv.y = static_cast<float>(boundTable[vertMap[ii]*2+1]);
        }
        else
        {
            m_pVerts[ii].uv.x = static_cast<float>(U[vertMap[ii]]);
            m_pVerts[ii].uv.y = static_cast<float>(V[vertMap[ii]]);
        }
    }
    
    return hr;
}



HRESULT CIsochartMesh::BarycentricParameterization(
    bool& bIsOverLap)
{
    HRESULT hr = S_OK;

    bIsOverLap = true;

    // 1. Generated Vertex Map for each vertex, indicating its location in 	
    //COEFFICIENT or CONSTANT part
    std::vector<uint32_t> vertMap;
    size_t dwBoundaryCount = 0;
    size_t dwInternalCount = 0;
    std::vector<double> boundTable;
    CSparseMatrix<double> A;
    CVector<double> BU;
    CVector<double> BV;	
    CVector<double> U;
    size_t nIterCount = 0;
    CVector<double> V;	

    FAILURE_GOTO_END(
        GenerateVertexMap(
            vertMap,
            dwBoundaryCount,
            dwInternalCount));

    if( (dwBoundaryCount == 0) || (dwBoundaryCount >= 0x80000000) )
        goto LEnd;

    // 2. Generate the coordinates of boundary vertices
    FAILURE_GOTO_END(
        GenerateBoundaryCoord(
            boundTable, 
            dwBoundaryCount,
            vertMap));
    if (boundTable.empty())
    {
        goto LEnd;
    }

    // 3. Build up the linear equation set
    FAILURE_GOTO_END(
        InitializeBarycentricEquation(
            A,
            BU,
            BV,
            boundTable,
            vertMap));

    // 4. Solve the linear equation set
    FAILURE_GOTO_END(
        (false != CSparseMatrix<double>::ConjugateGradient(
            U,
            A,
            BU,
            BC_MAX_ITERATION,
            static_cast<double>(1e-8),
            nIterCount) ? S_OK : E_FAIL));
    if (nIterCount >= BC_MAX_ITERATION)
    {
        goto LEnd;
    }

    nIterCount = 0;
    FAILURE_GOTO_END(
        (false != CSparseMatrix<double>::ConjugateGradient(
            V,
            A,
            BV,
            BC_MAX_ITERATION,
            static_cast<double>(1e-8),
            nIterCount) ? S_OK : E_FAIL));
    if (nIterCount >= BC_MAX_ITERATION)
    {
        goto LEnd;
    }

    // 4. Assign UV coordinates
    FAILURE_GOTO_END(
        AssignBarycentricResult(
            U, 
            V,
            boundTable, 
            vertMap));

    // 5. Check Results
    FAILURE_GOTO_END(
        CheckLinearEquationParamResult(
            bIsOverLap));

    if (bIsOverLap)
    {
        DPF(0, "Barycentric faild");
    }

LEnd:
    return hr;
}
