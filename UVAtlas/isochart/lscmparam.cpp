//-------------------------------------------------------------------------------------
// UVAtlas - Lscmparam.cpp
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

// "Least Squares Conformal Maps for Automatic Texture Atlas Generation", Bruno Levy, etc.

#include "pch.h"
#include "isochartmesh.h"
#include "isochart.h"
#include "sparsematrix.hpp"

using namespace Isochart;
using namespace DirectX;

namespace
{
    const size_t LSCM_MAX_ITERATION = 10000;
    enum EQUATION_POSITION
    {
        IN_COEFFICIENT,
        IN_CONSTANT
    };

    static EQUATION_POSITION GetPosInMatrix(
        uint32_t dwVertID,
        size_t dwTotolVertNum,
        uint32_t dwBaseVertId1,
        uint32_t dwBaseVertId2,
        size_t& dwCol1,
        size_t& dwCol2)
    {
        if (dwBaseVertId1 == dwVertID || dwBaseVertId2 == dwVertID)
        {
            dwCol1 = (dwVertID == dwBaseVertId1) ? 0 : 1;
            dwCol2 = dwCol1 + 2;
            return IN_CONSTANT;
        }
        else
        {
            if (dwVertID < dwBaseVertId1)
            {
                dwCol1 = dwVertID;
            }
            else if (dwVertID < dwBaseVertId2)
            {
                dwCol1 = dwVertID - 1;
            }
            else
            {
                dwCol1 = dwVertID - 2;
            }
            dwCol2 = dwCol1 + dwTotolVertNum - 2;
            return IN_COEFFICIENT;
        }
    }
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::FindTwoFarestBoundaryVertices(
    uint32_t& dwVertId1,
    uint32_t& dwVertId2)
{
    HRESULT hr = S_OK;

    dwVertId1 = INVALID_VERT_ID;
    dwVertId2 = INVALID_VERT_ID;	
    for (uint32_t ii=0; ii<m_dwVertNumber; ii++)
    {
        if (m_pVerts[ii].bIsBoundary)
        {
            dwVertId1 = ii;
            break;
        }
    }
    if (INVALID_VERT_ID == dwVertId1)
    {
        return hr;
    }

    //FAILURE_RETURN(CalculateGeodesicDistanceToVertex(dwVertId1, false, &dwVertId2));
    FAILURE_RETURN(CalculateDijkstraPathToVertex(dwVertId1, &dwVertId2));

    if (dwVertId1 > dwVertId2)
    {
        std::swap(dwVertId1,dwVertId2);
    }

    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::AddFaceWeight(
    uint32_t dwFaceID,
    CSparseMatrix<double>& A,
    CSparseMatrix<double>& M,
    uint32_t dwBaseVertId1,
    uint32_t dwBaseVertId2)
{
    HRESULT hr = S_OK;

    assert(dwBaseVertId1 < dwBaseVertId2);
    ISOCHARTFACE& face = m_pFaces[dwFaceID];

    XMFLOAT2 v2d[3];
    XMFLOAT3 axis[2];

    IsochartCaculateCanonicalCoordinates(
        m_baseInfo.pVertPosition + m_pVerts[face.dwVertexID[0]].dwIDInRootMesh,
        m_baseInfo.pVertPosition + m_pVerts[face.dwVertexID[1]].dwIDInRootMesh,
        m_baseInfo.pVertPosition + m_pVerts[face.dwVertexID[2]].dwIDInRootMesh,
        v2d,
        v2d+1,
        v2d+2,
        axis);

    double t = 
        (v2d[0].x*v2d[1].y - v2d[0].y*v2d[1].x) +
        (v2d[1].x*v2d[2].y - v2d[1].y*v2d[2].x) +
        (v2d[2].x*v2d[0].y - v2d[2].y*v2d[0].x);

    t = static_cast<double>(IsochartSqrt(t));
    if (IsInZeroRange2(static_cast<float>(t)))
    {
        return hr;
    }

    CSparseMatrix<double>* pA = nullptr;
    for (size_t ii=0; ii<3; ii++)
    {
        ISOCHARTVERTEX& vert = m_pVerts[face.dwVertexID[ii]];

        double w_r = v2d[(ii+2)%3].x - v2d[(ii+1)%3].x;
        double w_i = v2d[(ii+2)%3].y - v2d[(ii+1)%3].y;

        size_t dwCol1;
        size_t dwCol2;

        if ( IN_CONSTANT == GetPosInMatrix(
            vert.dwID, 
            m_dwVertNumber, 
            dwBaseVertId1,
            dwBaseVertId2,
            dwCol1,
            dwCol2))
        {
            pA = &M;
        }
        else
        {
            pA = &A;
        }

        if (!pA->setItem(dwFaceID, dwCol1, w_r/t))
        {
            return E_OUTOFMEMORY;
        }
        if (!pA->setItem(dwFaceID, dwCol2, -w_i/t))
        {
            return E_OUTOFMEMORY;
        }

        if (!pA->setItem(dwFaceID+m_dwFaceNumber, dwCol1, w_i/t))
        {
            return E_OUTOFMEMORY;
        }		
        if (!pA->setItem(dwFaceID+m_dwFaceNumber, dwCol2, w_r/t))
        {
            return E_OUTOFMEMORY;
        }		
    }
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::EstimateSolution(
    CVector<double>& V)
{
    try
    {
        V.resize(2 * 2);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    V[0] = static_cast<double>(1.0);
    V[1] = static_cast<double>(0.0);
    V[2] = static_cast<double>(-1.0);
    V[3] = static_cast<double>(0.0);
    return S_OK;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::AssignLSCMResult(
    CVector<double>& U,
    CVector<double>& X,
    uint32_t dwBaseVertId1,
    uint32_t dwBaseVertId2)
{
    HRESULT hr = S_OK;

    CVector<double> *pV = nullptr;
    for (uint32_t ii=0; ii<m_dwVertNumber; ii++)
    {	
        size_t dwCol1, dwCol2;
        if (IN_CONSTANT == GetPosInMatrix(
            ii,
            m_dwVertNumber,
            dwBaseVertId1,
            dwBaseVertId2,
            dwCol1,
            dwCol2))
        {
            pV = &U;
        }
        else
        {
            pV = &X;
        }
        m_pVerts[ii].uv.x = static_cast<float>((*pV)[dwCol1]);
        m_pVerts[ii].uv.y = static_cast<float>((*pV)[dwCol2]);
    }
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::InitializeLSCMEquation(
    CSparseMatrix<double>& A,
    CVector<double>& B,
    CVector<double>& U,
    uint32_t dwBaseVertId1,
    uint32_t dwBaseVertId2)
{
    HRESULT hr = S_OK;
    CSparseMatrix<double> orgA;
    CSparseMatrix<double> M;
    CVector<double> orgB;

    if (!orgA.resize(2*m_dwFaceNumber, (m_dwVertNumber-2)*2))
    {
        return E_OUTOFMEMORY;
    }
    if (!M.resize(2*m_dwFaceNumber, 2*2))
    {
        return E_OUTOFMEMORY;
    }
    
    for (uint32_t ii=0; ii<m_dwFaceNumber; ii++)
    {
        FAILURE_RETURN(
            AddFaceWeight(ii, orgA, M, dwBaseVertId1, dwBaseVertId2));
    }

    // b = -M*u
    if (!CSparseMatrix<double>::Mat_Mul_Vec(orgB, M, U))
    {
        return E_OUTOFMEMORY;
    }
    assert(orgB.size() == 2*m_dwFaceNumber);
    CVector<double>::scale(orgB, orgB, static_cast<double>(-1));

    // A' = A^T * A
    if (!CSparseMatrix<double>::Mat_Trans_MUL_Mat(A, orgA))
    {
        return E_OUTOFMEMORY;
    }

    // B' = A^T * b
    if (!CSparseMatrix<double>::Mat_Trans_Mul_Vec(B, orgA, orgB))
    {
        return E_OUTOFMEMORY;
    }
    
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::CheckLinearEquationParamResult(
    bool& bIsOverLap)
{
    HRESULT hr = S_OK;

    double fTotal2D = 0;
    for (size_t ii=0; ii<m_dwFaceNumber; ii++)
    {
        ISOCHARTFACE& face = m_pFaces[ii];
        double fA = Cal2DTriangleArea(
            m_pVerts[face.dwVertexID[0]].uv,
            m_pVerts[face.dwVertexID[1]].uv,
            m_pVerts[face.dwVertexID[2]].uv);
        if (fA < 0)
        {
            DPF(1, "Negative face %f", fA);
            bIsOverLap = true;
            return hr;
        }
        fTotal2D += fA;
    }

    bIsOverLap = false;
    ScaleChart(static_cast<float>(IsochartSqrt(m_fChart3DArea/fTotal2D)));
    m_fChart2DArea = m_fChart3DArea;

    m_bIsParameterized = true;
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT CIsochartMesh::LSCMParameterization(
    bool& bIsOverLap)
{
    HRESULT hr = S_OK;

    bIsOverLap = true;

    // 1. Find 2 farest boundary vertices as the reference vertices
    uint32_t dwBaseVertId1, dwBaseVertId2;
    CVector<double> U, X;
    CSparseMatrix<double> A;
    CVector<double> B;
    size_t nIterCount = 0;

    FAILURE_GOTO_END(
        FindTwoFarestBoundaryVertices(
            dwBaseVertId1, dwBaseVertId2));
    if (dwBaseVertId1 == INVALID_VERT_ID || dwBaseVertId2 == INVALID_VERT_ID)
    {
        goto LEnd;
    }

    // 2. Setup the linear equation set	
    FAILURE_GOTO_END(
        EstimateSolution(U));
    
    FAILURE_GOTO_END(
        InitializeLSCMEquation(
            A, 
            B, 
            U, 
            dwBaseVertId1, 
            dwBaseVertId2));

    // 3. Solve the linear equation set
    FAILURE_GOTO_END(
        (false != CSparseMatrix<double>::ConjugateGradient(
            X,
            A,
            B,
            LSCM_MAX_ITERATION,
            static_cast<double>(1e-8),
            nIterCount) ? S_OK : E_FAIL));
    if (nIterCount >= LSCM_MAX_ITERATION)
    {
        goto LEnd;
    }

    // 4. Assign UV coordinates
    FAILURE_GOTO_END(
        AssignLSCMResult(
            U, 
            X, 
            dwBaseVertId1, 
            dwBaseVertId2));

    // 5. Check Results
    FAILURE_GOTO_END(
        CheckLinearEquationParamResult(
            bIsOverLap));

    if (bIsOverLap)
    {
        DPF(0, "LSCM faild");
    }
LEnd:
    return hr;
}
