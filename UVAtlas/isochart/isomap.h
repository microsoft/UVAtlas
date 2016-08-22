//-------------------------------------------------------------------------------------
// UVAtlas - isomap.h
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
    class CIsoMap
    {
    public:
        CIsoMap();
        ~CIsoMap();

        HRESULT Init(
            size_t dwDimension,
            float *pfGeodesicMatrix);

        void Clear();
        
        HRESULT ComputeLargestEigen(
            size_t dwSelectedDimension,	 // How many largest eigen values & vectors want to compute
            size_t &dwCalculatedDimension);	 // How man largest eigen values & vectos have been computed.

        HRESULT GetPrimaryEnergyDimension(
            float fEnergyPercent,	
            size_t& dwPrimaryEnergyDimension);


        bool GetDestineVectors(
            size_t dwPrimaryEigenDimension,
            float* pfDestCoord);

        const float* GetEigenValue() const { return m_pfEigenValue; }
        const float* GetEigenVector() const{ return m_pfEigenVector; }
        const float* GetAverageColumn() const{ return m_pfAvgSquaredDstColumn;}
        size_t GetCalculatedDimension() const{ return m_dwCalculatedDimension; }
    private:
        size_t m_dwMatrixDimension;
        size_t  m_dwCalculatedDimension;
        size_t m_dwPrimaryDimension;
        float* m_pfMatrixB;
        float* m_pfEigenValue;
        float* m_pfEigenVector;
        float* m_pfAvgSquaredDstColumn;
        float m_fSumOfEigenValue;
    };
}
