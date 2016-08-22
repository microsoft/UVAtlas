//-------------------------------------------------------------------------------------
// UVAtlas - isochartutil.h
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

#include "isochartconfig.h"

#define FAILURE_RETURN(x) if (FAILED( hr =(x))) return hr
#define FAILURE_GOTO_END(x) if (FAILED( hr =(x))) goto LEnd

namespace Isochart
{
// values between ISOCHART_ZERO_EPS and -ISOCHART_ZERO_EPS
// will be regarded as zero
const float ISOCHART_ZERO_EPS = 1e-6f;
const float ISOCHART_ZERO_EPS2 = 1e-12f;

// Check if to segments intersect together.
bool IsochartIsSegmentsIntersect(
    const DirectX::XMFLOAT2& p0,
    const DirectX::XMFLOAT2& p1,
    const DirectX::XMFLOAT2& p3,
    const DirectX::XMFLOAT2& p4,
    DirectX::XMFLOAT2* pIntersection = nullptr);

// Check if a float value is near zero.
inline bool IsInZeroRange(
    float a)
{
    return ((a >= -ISOCHART_ZERO_EPS) && (a <= ISOCHART_ZERO_EPS));
}

inline bool IsInZeroRange2(
    float a)
{
    return ((a >= -ISOCHART_ZERO_EPS2) && (a <= ISOCHART_ZERO_EPS2));
}


inline float IsochartSqrtf(float a)
{
    if (a < 0)
    {
        return 0;
    }
    return sqrtf(a);
}

inline double IsochartSqrt(double a)
{
    if (a < 0)
    {
        return 0;
    }
    return sqrt(a);
}


inline float Cal3DTriangleArea(
    const DirectX::XMFLOAT3* pv0,
    const DirectX::XMFLOAT3* pv1,
    const DirectX::XMFLOAT3* pv2)
{
    using namespace DirectX;

    XMVECTOR v0 = XMLoadFloat3(pv1) - XMLoadFloat3(pv0);
    XMVECTOR v1 = XMLoadFloat3(pv2) - XMLoadFloat3(pv0);
    XMVECTOR n = XMVector3Cross(v0, v1);
    float area = XMVectorGetX(XMVector3Dot(n, n));
    return IsochartSqrtf(area) * 0.5f;
}

inline float Cal2DTriangleArea(
    const DirectX::XMFLOAT2& v0,
    const DirectX::XMFLOAT2& v1,
    const DirectX::XMFLOAT2& v2)
{
    return  ((v1.x - v0.x)*(v2.y - v0.y) - (v2.x - v0.x)*(v1.y - v0.y)) / 2;
}


inline float Cal2DTriangleArea(
    const DirectX::XMFLOAT2* pv0,
    const DirectX::XMFLOAT2* pv1,
    const DirectX::XMFLOAT2* pv2)
{
    return  ((pv1->x - pv0->x)*(pv2->y - pv0->y) - (pv2->x - pv0->x)*(pv1->y - pv0->y)) / 2;
}

inline float IsochartVertexToEdgeDistance2D(
    DirectX::XMFLOAT2& rVertex,
    DirectX::XMFLOAT2& rEdgeVertex0,
    DirectX::XMFLOAT2& rEdgeVertex1)
{
    using namespace DirectX;

    XMVECTOR vertex = XMLoadFloat2(&rVertex);
    XMVECTOR edgeVertex0 = XMLoadFloat2(&rEdgeVertex0);
    XMVECTOR edgeVertex1 = XMLoadFloat2(&rEdgeVertex1);

    XMVECTOR normal;
    XMVECTOR vector[2];

    float fDot, fLength, fT;

    vector[0] = vertex - edgeVertex0;
    normal = edgeVertex1 - edgeVertex0;

    fDot = XMVectorGetX(XMVector2Dot(normal, normal));

    if (IsInZeroRange(fDot))
    {
        fLength = XMVectorGetX(XMVector2Dot(vector[0], vector[0]));
    }
    else
    {
        fT = XMVectorGetX(XMVector2Dot(vector[0], normal)) / fDot;
        if (fT<0)
        {
            fT = 0;
        }
        if (fT>1)
        {
            fT = 1;
        }
        normal *= fT;
        vector[1] = edgeVertex0 + normal;
        vector[0] = vector[1] - vertex;
        fLength = XMVectorGetX(XMVector2Dot(vector[0], vector[0]));
    }

    return fLength;
}

inline float IsochartBoxArea(
    DirectX::XMFLOAT2& minBound,
    DirectX::XMFLOAT2& maxBound)
{
    float fDeltaX = maxBound.x - minBound.x;
    float fDeltaY = maxBound.y - minBound.y;

    return fDeltaX*fDeltaX + fDeltaY*fDeltaY;
}

inline float CalculateZOfVec3Cross(
    const DirectX::XMFLOAT3 *pV1,
    const DirectX::XMFLOAT3 *pV2)
{
    return pV1->x * pV2->y - pV1->y * pV2->x;
}

inline float CalculateZOfVec2Cross(
    const DirectX::XMFLOAT2 *pV1,
    const DirectX::XMFLOAT2 *pV2)
{
    return pV1->x * pV2->y - pV1->y * pV2->x;
}

inline float IsochartVectorDot(
    float* v1, 
    float* v2, 
    size_t dwDimension)
{
    float fResult = 0;
    for (size_t i = 0; i<dwDimension; i++)
    {
        fResult += v1[i]*v2[i];
    }

    return fResult;
}

inline void IsochartCaculateCanonicalCoordinates(
    const DirectX::XMFLOAT3* pv3D0,
    const DirectX::XMFLOAT3* pv3D1,
    const DirectX::XMFLOAT3* pv3D2,
    DirectX::XMFLOAT2* pv2D0,
    DirectX::XMFLOAT2* pv2D1,
    DirectX::XMFLOAT2* pv2D2,
    DirectX::XMFLOAT3* pAxis)
{
    using namespace DirectX;

    XMVECTOR axisX;
    XMVECTOR axisY;
    XMVECTOR axisZ;

    XMVECTOR v3D0 = XMLoadFloat3(pv3D0);
    XMVECTOR v3D1 = XMLoadFloat3(pv3D1);
    XMVECTOR v3D2 = XMLoadFloat3(pv3D2);
    XMVECTOR v2D0 = XMLoadFloat2(pv2D0);
    XMVECTOR v2D1 = XMLoadFloat2(pv2D1);
    XMVECTOR v2D2 = XMLoadFloat2(pv2D2);

    axisX = v3D1 - v3D0;
    axisY = v3D2 - v3D0;
    axisZ = XMVector3Cross(axisX, axisY);

    axisZ = XMVector3Normalize(axisZ);
    axisX = XMVector3Normalize(axisX);
    axisY = XMVector3Cross(axisZ, axisX);
    axisY = XMVector3Normalize(axisY);

    XMStoreFloat3(pAxis + 0, axisX);
    XMStoreFloat3(pAxis + 1, axisY);

    v2D0 = XMVectorSet(0, 0, 0, 0);

    XMVECTOR tempVector = v3D1 - v3D0;
    v2D1 = XMVectorSet(XMVectorGetX(XMVector3Dot(tempVector, axisX)), 0, 0, 0);

    tempVector = v3D2 - v3D0;
    v2D2 = XMVectorSet(XMVectorGetX(XMVector3Dot(tempVector, axisX)),
        XMVectorGetX(XMVector3Dot(tempVector, axisY)), 0, 0);

    if (XMVector3Equal(v3D1, v3D2))
    {
        v2D2 = XMVectorSetY(v2D2, 0);
    }

    XMStoreFloat2(pv2D0, v2D0);
    XMStoreFloat2(pv2D1, v2D1);
    XMStoreFloat2(pv2D2, v2D2);
}


inline void GetIMTOnCanonicalFace(
    const float* pMT,
    const float f3D,
    float* pIMT)
{
    assert(pIMT != 0 && pMT != 0);

    for (size_t ii=0; ii<IMT_DIM; ii++)
    {
        pIMT[ii] = pMT[ii] * f3D;
    }
}

inline void Compute2DtoNDPartialDerivatives(
    float fNew2DArea,
    const DirectX::XMFLOAT2* pv2D0,
    const DirectX::XMFLOAT2* pv2D1,
    const DirectX::XMFLOAT2* pv2D2,
    __in_ecount(dwDimensonN) const float* pfND0,
    __in_ecount(dwDimensonN) const float* pfND1,
    __in_ecount(dwDimensonN) const float* pfND2,
    size_t dwDimensonN,
    __out_ecount(dwDimensonN) float* Ss,
    __out_ecount(dwDimensonN) float* St)
{
    assert(!IsInZeroRange2(fNew2DArea));

    float q[3];
    for (size_t ii=0; ii<dwDimensonN; ii++)
    {
        q[0] = pfND0[ii];
        q[1] = pfND1[ii];
        q[2] = pfND2[ii];

        if (!IsInZeroRange2(fNew2DArea))
        {
            Ss[ii] = (q[0]*(pv2D1->y-pv2D2->y) + 
                q[1]*(pv2D2->y-pv2D0->y) + 
                q[2]*(pv2D0->y-pv2D1->y))/(fNew2DArea*2);
                
            St[ii] = (q[0]*(pv2D2->x-pv2D1->x) + 
                q[1]*(pv2D0->x-pv2D2->x) + 
                q[2]*(pv2D1->x-pv2D0->x))/(fNew2DArea*2);
            
        }
        else
        {
            if (q[0] == q[1] && q[0] == q[2])
            {
                Ss[ii] = St[ii] = 0;
            }
            else
            {
                Ss[ii] = St[ii] = FLT_MAX;
            }
        }
    }

    return;
}



inline void AffineIMTOn2D(
    float fNew2DArea,
    const DirectX::XMFLOAT2* pNewUv0,
    const DirectX::XMFLOAT2* pNewUv1,
    const DirectX::XMFLOAT2* pNewUv2,
    __out_ecount(IMT_DIM) float* pNewIMT,
    const DirectX::XMFLOAT2* pOldUv0,
    const DirectX::XMFLOAT2* pOldUv1,
    const DirectX::XMFLOAT2* pOldUv2,
    __in_ecount(IMT_DIM) const float* pOldIMT,
    float* pGeo)
{
    using namespace DirectX;

    if (IsInZeroRange2(fNew2DArea))
    {
        for (size_t ii=0; ii<IMT_DIM; ii++)
        {
            pNewIMT[ii] = FLT_MAX;
        }
        return;
    }

    XMFLOAT2 Ss, St;
    Compute2DtoNDPartialDerivatives(
        fNew2DArea,
        pNewUv0,
        pNewUv1,
        pNewUv2,
        (const float*)(pOldUv0),
        (const float*)(pOldUv1),
        (const float*)(pOldUv2),
        2,
        (float*)&Ss,
        (float*)&St);

    if (pGeo)
    {
        XMVECTOR vSs = XMLoadFloat2(&Ss);
        XMVECTOR vSt = XMLoadFloat2(&St);
        pGeo[0] = XMVectorGetX(XMVector2Dot(vSs, vSs));
        pGeo[1] = XMVectorGetX(XMVector2Dot(vSs, vSt));
        pGeo[2] = XMVectorGetX(XMVector2Dot(vSt, vSt));
    }

    float oldIMT[IMT_DIM];
    memcpy(oldIMT, pOldIMT, IMT_DIM*sizeof(float));

    #if PIECEWISE_CONSTANT_IMT
    pNewIMT[0] = 
        Ss.x*Ss.x*oldIMT[0] + Ss.y*Ss.y*oldIMT[2] + 2*Ss.x*Ss.y*oldIMT[1];
    pNewIMT[2] = 
        St.x*St.x*oldIMT[0] + St.y*St.y*oldIMT[2] + 2*St.x*St.y*oldIMT[1];
    pNewIMT[1] = 
        Ss.x*St.x*oldIMT[0] + Ss.y*St.y*oldIMT[2] + (Ss.x*St.y + Ss.y*St.x)*oldIMT[1];
    #else
    #endif

    return;
}

inline void TransformUV(
    DirectX::XMFLOAT2& newUv,
    const DirectX::XMFLOAT2& oldUv,
    const float* pMatrix)
{
    float u = pMatrix[0]*oldUv.x + pMatrix[1]*oldUv.y;
    float v = pMatrix[2]*oldUv.x + pMatrix[3]*oldUv.y;

    newUv.x = u, newUv.y = v;
    return;
}

inline void SetAllIMTValue(
    __out_ecount(IMT_DIM) float* pIMT,
    float fValue)
{
    for (size_t ii=0; ii<IMT_DIM; ii++)
    {
        pIMT[ii] = fValue;
    }

}

float CalL2SquaredStretchLowBoundOnFace(
    const float* pMT,
    float fFace3DArea,
    float fMaxDistortionRate,
    float* fRotMatrix);

float CombineSigAndGeoStretch(
    const float* pMT,
    float fSigStretch,
    float fGeoStretch);
}
