//-------------------------------------------------------------------------------------
// UVAtlas - isomap.cpp
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#include "pch.h"
#include "isomap.h"
#include "isochartutil.h"

using namespace Isochart;

#include "SymmetricMatrix.hpp"

#include <Eigen/Eigen>

CIsoMap::CIsoMap()
    :m_dwMatrixDimension(0),
    m_dwCalculatedDimension(0),
    m_dwPrimaryDimension(0),
    m_pfMatrixB(nullptr),
    m_pfEigenValue(nullptr),
    m_pfEigenVector(nullptr),
    m_pfAvgSquaredDstColumn(nullptr),
    m_fSumOfEigenValue(0)
{
}

CIsoMap::~CIsoMap()
{
    Clear();
}

HRESULT CIsoMap::Init(size_t dwDimension, float* pGeodesicMatrix)
{
    Clear();
    assert(pGeodesicMatrix != nullptr);
    assert(m_dwCalculatedDimension == 0);
    assert(m_fSumOfEigenValue == 0);
    assert(m_dwPrimaryDimension == 0);
    assert(!m_pfMatrixB);

    m_pfMatrixB = pGeodesicMatrix;
    m_dwMatrixDimension = dwDimension;

    float* pRow = m_pfMatrixB;
    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pRow[j] *= pRow[j];
        }

        pRow += m_dwMatrixDimension;
    }

    std::unique_ptr<float[]> average(new (std::nothrow) float[m_dwMatrixDimension]);
    if (!average)
    {
        return E_OUTOFMEMORY;
    }

    float* pfAverage = average.get();

    m_pfAvgSquaredDstColumn = new (std::nothrow) float[m_dwMatrixDimension];
    if (!m_pfAvgSquaredDstColumn)
    {
        return E_OUTOFMEMORY;
    }

    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        pfAverage[i] = 0;
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pfAverage[i] += m_pfMatrixB[j * m_dwMatrixDimension + i];
        }
        pfAverage[i] /= float(m_dwMatrixDimension);
    }
    memcpy(m_pfAvgSquaredDstColumn, pfAverage, m_dwMatrixDimension * sizeof(float));

    pRow = m_pfMatrixB;
    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pRow[j] -= pfAverage[j];
        }
        pRow += m_dwMatrixDimension;
    }

    pRow = m_pfMatrixB;
    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        pfAverage[i] = 0;
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pfAverage[i] += pRow[j];
        }

        pfAverage[i] /= float(m_dwMatrixDimension);
        pRow += m_dwMatrixDimension;
    }

    pRow = m_pfMatrixB;
    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pRow[j] -= pfAverage[i];
        }
        pRow += m_dwMatrixDimension;
    }

    pRow = m_pfMatrixB;
    for (size_t i = 0; i < m_dwMatrixDimension; i++)
    {
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pRow[j] *= -0.5f;
        }
        pRow += m_dwMatrixDimension;
    }
    return S_OK;
}

HRESULT CIsoMap::ComputeLargestEigen(
    size_t dwSelectedDimension,
    size_t& dwCalculatedDimension)
{
    assert(m_pfMatrixB != nullptr);
    _Analysis_assume_(m_pfMatrixB != nullptr);
    assert(m_pfAvgSquaredDstColumn != nullptr);
    _Analysis_assume_(m_pfAvgSquaredDstColumn != nullptr);
    assert(dwSelectedDimension <= m_dwMatrixDimension);
    _Analysis_assume_(dwSelectedDimension <= m_dwMatrixDimension);

    std::unique_ptr<float[]> pfEigenValue(new (std::nothrow) float[m_dwMatrixDimension]);
    std::unique_ptr<float[]> pfEigenVector(new (std::nothrow) float[m_dwMatrixDimension * m_dwMatrixDimension]);
    if (!pfEigenValue || !pfEigenVector)
    {
        return E_OUTOFMEMORY;
    }

    m_pfEigenValue = new (std::nothrow) float[dwSelectedDimension];
    m_pfEigenVector = new (std::nothrow) float[m_dwMatrixDimension * dwSelectedDimension];

    if (!m_pfEigenValue || !m_pfEigenVector)
    {
        return E_OUTOFMEMORY;
    }

    Eigen::MatrixXf M(m_dwMatrixDimension, m_dwMatrixDimension);
    for (int i = 0; i < m_dwMatrixDimension; i++) {
        for (int j = 0; j < m_dwMatrixDimension; j++) {
            M(i, j) = m_pfMatrixB[i * m_dwMatrixDimension + j];
        }
    }
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> eigenAlg(M);

    for (int i = 0; i < dwSelectedDimension; i++) {
        int eigenIndex = (int)m_dwMatrixDimension - i - 1;
        pfEigenValue[i] = eigenAlg.eigenvalues()(eigenIndex);
        for (int j = 0; j < m_dwMatrixDimension; j++) {
            pfEigenVector[i * m_dwMatrixDimension + j] = eigenAlg.eigenvectors()(j, eigenIndex);
        }
    }

    //if (!CSymmetricMatrix<float>::GetEigen(
    //    m_dwMatrixDimension, m_pfMatrixB,
    //    pfEigenValue.get(), pfEigenVector.get(),
    //    dwSelectedDimension))
    //{
    //    return E_OUTOFMEMORY;
    //}

    memcpy(m_pfEigenValue, pfEigenValue.get(), dwSelectedDimension * sizeof(float));
    memcpy(
        m_pfEigenVector,
        pfEigenVector.get(),
        m_dwMatrixDimension * dwSelectedDimension * sizeof(float));

    m_fSumOfEigenValue = 0;
    dwCalculatedDimension = 0;
    for (size_t i = 0; i < dwSelectedDimension; i++)
    {
        if (m_pfEigenValue[i] < ISOCHART_ZERO_EPS
            || (i > 0 && m_pfEigenValue[i]
                < m_pfEigenValue[i - 1] * ISOCHART_ZERO_EPS && m_pfEigenValue[i])) // BUGBUG -Wfloat-conversion!?!
        {
            break;
        }
        m_fSumOfEigenValue += m_pfEigenValue[i];
        dwCalculatedDimension++;
    }

    if (dwSelectedDimension == 2)
    {
        dwCalculatedDimension = 2;
    }

    m_dwCalculatedDimension = dwCalculatedDimension;

    return S_OK;
}

HRESULT CIsoMap::GetPrimaryEnergyDimension(
    float fEnergyPercent,
    size_t& dwPrimaryEnergyDimension)
{
    if (IsInZeroRange(m_fSumOfEigenValue))
    {
        dwPrimaryEnergyDimension = 0;
        return S_OK;
    }

    if (fEnergyPercent >= 1.0f)
    {
        dwPrimaryEnergyDimension = m_dwCalculatedDimension;
    }

    size_t dwDestineDimension = 2;

    float fPrecision = m_pfEigenValue[0] + m_pfEigenValue[1];


    while (fPrecision < 0.99f * m_fSumOfEigenValue
        && dwDestineDimension < m_dwCalculatedDimension)
    {
        fPrecision += m_pfEigenValue[dwDestineDimension];
        dwDestineDimension++;
    }

    m_dwPrimaryDimension = dwDestineDimension;

    std::unique_ptr<float[]> eigenValueRatio(new (std::nothrow) float[dwDestineDimension - 1]);
    if (!eigenValueRatio)
    {
        return E_OUTOFMEMORY;
    }

    float* pfEigenValueRatio = eigenValueRatio.get();

    for (size_t i = 0; i < dwDestineDimension - 1; i++)
    {
        pfEigenValueRatio[i]
            = IsochartSqrtf(m_pfEigenValue[i]) - IsochartSqrtf(m_pfEigenValue[i + 1]);
    }

    size_t dwAccumulateDimension = 2;
    fPrecision = m_pfEigenValue[0] + m_pfEigenValue[1];

    while (fPrecision < m_fSumOfEigenValue * fEnergyPercent
        && dwAccumulateDimension < dwDestineDimension)
    {
        fPrecision += m_pfEigenValue[dwAccumulateDimension];
        dwAccumulateDimension++;
    }

    if (fEnergyPercent >= 0.91f)
    {
        dwPrimaryEnergyDimension = dwAccumulateDimension;
        if (dwPrimaryEnergyDimension < 2)
        {
            dwPrimaryEnergyDimension = 2;
        }
        return S_OK;
    }

    size_t dwRequiredDimension = 0;
    float fMaxRatio = 0;

    for (size_t i = dwAccumulateDimension - 1; i < dwDestineDimension - 1; i++)
    {
        if (i == dwAccumulateDimension - 1 || fMaxRatio < pfEigenValueRatio[i])
        {
            dwRequiredDimension = i;
            fMaxRatio = pfEigenValueRatio[i];
        }
    }

    if (dwRequiredDimension == 0)
    {
        dwAccumulateDimension = 2;
    }
    else
    {
        dwAccumulateDimension = dwRequiredDimension + 1;
    }

    dwPrimaryEnergyDimension = dwAccumulateDimension;

    return S_OK;
}


bool CIsoMap::GetDestineVectors(size_t dwPrimaryEigenDimension, float* pfDestCoord)
{
    if (dwPrimaryEigenDimension > m_dwMatrixDimension)
    {
        return false;
    }

    float* fpEigenVector = m_pfEigenVector;

    for (size_t i = 0; i < dwPrimaryEigenDimension; i++)
    {
        //assert(m_pfEigenValue[i] >= 0);
        if (m_pfEigenValue[i] < 0)
            m_pfEigenValue[i] = 0;

        float fTemp = static_cast<float>(IsochartSqrt(double(m_pfEigenValue[i])));
        for (size_t j = 0; j < m_dwMatrixDimension; j++)
        {
            pfDestCoord[j * dwPrimaryEigenDimension + i] = (fTemp * fpEigenVector[j]);
        }

        fpEigenVector += m_dwMatrixDimension;
    }
    return true;
}


void CIsoMap::Clear()
{
    SAFE_DELETE_ARRAY(m_pfEigenValue)
        SAFE_DELETE_ARRAY(m_pfEigenVector)
        SAFE_DELETE_ARRAY(m_pfAvgSquaredDstColumn)

        m_dwMatrixDimension = 0;
    m_dwCalculatedDimension = 0;
    m_dwPrimaryDimension = 0;
    m_pfMatrixB = nullptr;
    m_fSumOfEigenValue = 0;

    return;
}
