//-------------------------------------------------------------------------------------
// UVAtlas - isochartutil.cpp
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
#include "isochartutil.h"

using namespace Isochart;
using namespace DirectX;

namespace
{
    static inline bool CalculateOverlappedSegmentsIntersection(
        float a0, float a1,
        float a3, float a4,
        float& result)
    {
        result = 0;

        if (a0 > a1)
        {
            std::swap(a0, a1);
        }

        if (a3 > a4)
        {
            std::swap(a3, a4);
        }

        if (a1 < a3 || a0 > a4)
        {
            return false;
        }

        if (a0 >= a3)
        {
            result = a0;
        }
        else
        {
            result = a3;
        }

        return true;

    }

    // Check if two lines : (x0, y0), (x1,y1) and (x3,y3), (x4, y4) intersect
    //
    // x0 + fT*(x1 - x0) = x3 + fS*(x4 - x3) = x
    // y0 + fT*(y1 - y0) = y3 + fS*(y4 - y3) = y
    // ask fT and fS
    // If two lines intersect, fT and fS must between 0.0 and 1.0
    static void CalculateSegmentsIntersection(
        const XMFLOAT2& p0, const XMFLOAT2& p1,	// Two ports of segment 1
        const XMFLOAT2& p3, const XMFLOAT2& p4,	// Two ports of segment 2
        XMFLOAT2& intersection,
        float& fT,
        float& fS)
    {
        float x0, x1, x3, x4;
        float y0, y1, y3, y4;

        x0 = p0.x;
        y0 = p0.y;

        x1 = p1.x;
        y1 = p1.y;

        x3 = p3.x;
        y3 = p3.y;

        x4 = p4.x;
        y4 = p4.y;

        fT = -2.0f;	// Set fT and fS to a value out of the range [0.0, 1.0]
        fS = -2.0f;

        // if line degenerates into point, return
        if (IsInZeroRange(x1 - x0) && IsInZeroRange(y1 - y0))
        {
            return;
        }

        if (IsInZeroRange(x4 - x3) && IsInZeroRange(y4 - y3))
        {
            return;
        }

        // if x3 == x4
        if (IsInZeroRange(x3 - x4))
        {
            // Logically,  y4 - y3 must not be zero here. or else the function will return before
            // run to here.  Using assert here only for check the potential error 
            // (such as Stack Overflow), and tell the readers why we need not  defend 
            // devide-zero exception, when using y4 - y3 or y3 - y4 as denominator
            // The other assert(!IsInZeroRange(...)) in the function also have the same
            // target as this. 
            assert(!IsInZeroRange(y4 - y3));

            if (IsInZeroRange(x0 - x1))
            {
                // x0 = x3 = x;
                // y0 + fT*(y1-y0) = y3 + fS*(y4-y3) = y;
                assert(!IsInZeroRange(y1 - y0));

                // line1 & line2 are parallels. No intersection, return directly.
                if (!IsInZeroRange(x3 - x0))
                {
                    return;
                }
                else // line1 & line2 overlap, Select one point on overlapped segment as intersection
                {
                    intersection.x = x0;
                    if (!CalculateOverlappedSegmentsIntersection(
                        y0, y1, y3, y4, intersection.y))
                    {
                        return;
                    }
                    //y0 + fT*(y1-y0) = intersection.y
                    //y3 + fS*(y4-y3) = intersection.y

                    fT = (intersection.y - y0) / (y1 - y0);
                    fS = (intersection.y - y3) / (y4 - y3);
                    return;
                }
            }
            else // x0 != x1
            {
                // x0 + fT*(x1-x0) = x3 = x;
                // y0 + fT*(y1-y0) = y3 + fS*(y4-y3) = y

                fT = (x3 - x0) / (x1 - x0);
                intersection.x = x3;
                intersection.y = y0 + fT * (y1 - y0);
                fS = (intersection.y - y3) / (y4 - y3);
                return;
            }
        }
        else if (IsInZeroRange(y3 - y4))
        {
            assert(!IsInZeroRange(x4 - x3));
            if (IsInZeroRange(y0 - y1))
            {
                // x0 + fT*(x1 - x0) = x3 + fS*(x4 - x3) = x
                // y0 = y3 = y			
                assert(!IsInZeroRange(x0 - x1));

                if (!IsInZeroRange(y3 - y0))
                {
                    return;
                }
                else
                {
                    intersection.y = y0;
                    if (!CalculateOverlappedSegmentsIntersection(
                        x0, x1, x3, x4, intersection.x))
                    {
                        return;
                    }

                    // x0 + fT*(x1 - x0) = intersection.x 
                    // x3 + fS*(x4 - x3) = intersection.x 
                    fT = (intersection.x - x0) / (x1 - x0);
                    fS = (intersection.x - x3) / (x4 - x3);

                    return;
                }
            }
            else // y0 != y1
            {
                // x0 + fT*(x1 - x0) = x3 + fS*(x4 - x3) = x
                // y0 + fT*(y1 - y0) = y3 = y	

                fT = (y3 - y0) / (y1 - y0);
                intersection.x = x0 + fT * (x1 - x0);
                intersection.y = y3;
                fS = (intersection.x - x3) / (x4 - x3);
                return;
            }
        }

        else if (IsInZeroRange(x0 - x1))
        {
            // x0 = x3 + fS*(x4 - x3) = x
            // y0 + fT*(y1 - y0) = y3 + fS*(y4 - y3) = y

            assert(!IsInZeroRange(y0 - y1));
            assert(!IsInZeroRange(x3 - x4));

            fS = (x0 - x3) / (x4 - x3);
            intersection.x = x0;
            intersection.y = y3 + fS * (y4 - y3);

            fT = (intersection.y - y0) / (y1 - y0);
            return;
        }
        else if (IsInZeroRange(y0 - y1))
        {
            // x0 + fT*(x1 - x0) = x3 + fS*(x4 - x3) = x
            // y0 = y3 + fS*(y4 - y3) = y

            assert(!IsInZeroRange(x0 - x1));
            assert(!IsInZeroRange(y3 - y4));

            fS = (y0 - y3) / (y4 - y3);
            intersection.x = x3 + fS * (x4 - x3);
            intersection.y = y0;

            fT = (intersection.x - x0) / (x1 - x0);
            return;

        }
        else
        {

            assert(!IsInZeroRange(x1 - x0));
            assert(!IsInZeroRange(x4 - x3));

            assert(!IsInZeroRange(y1 - y0));
            assert(!IsInZeroRange(y4 - y3));

            // x0 + fT*(x1 - x0) = x3 + fS*(x4 - x3) = x
            // y0 + fT*(y1 - y0) = y3 + fS*(y4 - y3) = y

            // 1. Check if two lines are parallel
            float v1[2], v2[2], fLength;

            v1[0] = x1 - x0;
            v1[1] = y1 - y0;
            fLength = IsochartSqrtf(v1[0] * v1[0] + v1[1] * v1[1]);
            if (IsInZeroRange(fLength))
            {
                return;
            }
            v1[0] /= fLength;
            v1[1] /= fLength;

            v2[0] = x4 - x3;
            v2[1] = y4 - y3;
            fLength = IsochartSqrtf(v2[0] * v2[0] + v2[1] * v2[1]);
            v2[0] /= fLength;
            v2[1] /= fLength;

            // 2. Two lines are parallel. 
            if (static_cast<float>(fabs(v1[0] * v2[1] - v1[1] * v2[0]))
                < ISOCHART_ZERO_EPS / 2.0)
            {
                v1[0] = (x3 - x0) / (x1 - x0);
                v1[1] = (y3 - y0) / (y1 - y0);

                if (!IsInZeroRange(v1[0] - v1[1]))//Two lines are not overlapped, return
                {
                    return;
                }
                else //Calculate intersection of overlapped lines
                {
                    if (!CalculateOverlappedSegmentsIntersection(
                        x0, x1, x3, x4, intersection.x))
                    {
                        return;
                    }

                    fT = (intersection.x - x0) / (x1 - x0);
                    fS = (intersection.x - x3) / (x4 - x3);
                    intersection.y = y0 + fT*(y1 - y0);

                    return;
                }
            }
            else // 3. two Lines are not parallel, resolve the original equation to get fT and fS
            {
                fT = ((x3 - x0)*(y4 - y3) - (y3 - y0)*(x4 - x3)) / ((x1 - x0)*(y4 - y3) - (y1 - y0)*(x4 - x3));
                intersection.x = x0 + fT*(x1 - x0);
                intersection.y = y0 + fT*(y1 - y0);

                if (fabsf(x4 - x3) > fabsf(y4 - y3))
                {
                    fS = (intersection.x - x3) / (x4 - x3);
                }
                else
                {
                    fS = (intersection.y - y3) / (y4 - y3);
                }

                return;
            }
        }
    }
}

bool Isochart::IsochartIsSegmentsIntersect(
    const XMFLOAT2& p0,
    const XMFLOAT2& p1,
    const XMFLOAT2& p3,
    const XMFLOAT2& p4,
    XMFLOAT2* pIntersection)
{
    XMFLOAT2 intersection;
    float fS, fT;

    CalculateSegmentsIntersection(
        p0, p1, p3, p4,  intersection, fS, fT);

    float epsion2 = ISOCHART_ZERO_EPS*ISOCHART_ZERO_EPS;
    if (fS>-epsion2 && fS<epsion2+1 && fT>-epsion2 && fT<epsion2+1)
    {
        if (pIntersection)
        {
            *pIntersection = intersection;
        }
        
        return true;
    }
    return false;
}

float Isochart::CalL2SquaredStretchLowBoundOnFace(
       const float* pMT,
    float fFace3DArea,
    float fMaxDistortionRate,
    float* fRotMatrix)
{
    assert(!IsInZeroRange2(fMaxDistortionRate));
    if (fRotMatrix)
    {
        fRotMatrix[0] = fRotMatrix[3] =1;
        fRotMatrix[1] = fRotMatrix[2] = 0;
    }
    if (!pMT)
    {
        return fFace3DArea;
    }

    float IMT[IMT_DIM];
    GetIMTOnCanonicalFace(
        pMT, 
        fFace3DArea, 
        IMT);

    // Solve Eigen value d1, d2, d1 >= d2
    float b = IMT[0]+IMT[2];
    float c = IMT[0]*IMT[2] - IMT[1]*IMT[1];

    float fTemp = IsochartSqrtf(b*b-4*c);
    float d1 = (b+fTemp)/2;
    float d2 = (b-fTemp)/2;

    if (IsInZeroRange(d1) && IsInZeroRange(d2))
    {
        return CombineSigAndGeoStretch(pMT, 0, fFace3DArea);
    }

    // Solve Eigen vector v1, v2
    assert(d1 >= d2);

    float a00 = IMT[0] - d1;
    float a01 = IMT[1];
    float a10 = IMT[1];
    float a11 = IMT[2] - d1;

    
    
    //assert(IsInZeroRange(a00/a01 - a10/a11));

    float b00 = IMT[0] - d2;
    float b01 = IMT[1];
    float b10 = IMT[1];
    float b11 = IMT[2] - d2;


    // Solve the optical tansform maxtrix
    float v1[2];
    float v2[2];

    float delta1 = IsochartSqrtf(a01*a01 + a00*a00);
    float delta2 = IsochartSqrtf(a11*a11 + a10*a10);

    if (IsInZeroRange2(delta1) && IsInZeroRange2(delta2))
    {
        return CombineSigAndGeoStretch(pMT, 0, fFace3DArea);
    }
    if (delta1 >= delta2)
    {
        v1[0] = a01 / delta1;
        v1[1] = -a00 / delta1;
    }
    else
    {
        v1[0] = a11 / delta2;
        v1[1] = -a10 / delta2;
    }

    delta1 = IsochartSqrtf(b01*b01 + b00*b00);
    delta2 = IsochartSqrtf(b11*b11 + b10*b10);
    if (IsInZeroRange2(delta1) && IsInZeroRange2(delta2))
    {
        return CombineSigAndGeoStretch(pMT, 0, fFace3DArea);
    }

    if (delta1 >= delta2)
    {
        v2[0] = b01 / delta1;
        v2[1] = -b00 / delta1;
    }
    else
    {
        v2[0] = b11 / delta2;
        v2[1] = -b10 / delta2;
    }


    //assert(IsInZeroRange(v1[0]*v2[0]+v1[1]*v2[1]));
    /*
    v1[0] /= IsochartSqrtf(a01*a01 + a00*a00);
    v1[1] /= IsochartSqrtf(a01*a01 + a00*a00);

    v2[0] /= IsochartSqrtf(b01*b01 + b00*b00);
    v2[1] /= IsochartSqrtf(b01*b01 + b00*b00);*/

    //assert(IsInZeroRange(v1[0]*v1[0] + v2[0]*v2[0]-1));
    //assert(IsInZeroRange(v1[1]*v1[1] + v2[1]*v2[1]-1));
    float m0 = v1[0]*v1[0]*d1+v2[0]*v2[0]*d2;
    float m1 = v1[0]*v1[1]*d1+v2[0]*v2[1]*d2;
    float m2 = v1[1]*v1[1]*d1+v2[1]*v2[1]*d2;		
        
    float d = IsochartSqrtf(IsochartSqrtf(d2/d1));
    if (d < 1/fMaxDistortionRate)
    {
        d = 1/fMaxDistortionRate;
    }
        
    float a0, a1, a2, a3;
    a0 = v1[0]*v1[0]*d + v2[0]*v2[0]/d;
    a3 = v1[1]*v1[1]*d + v2[1]*v2[1]/d;
    a1 = a2 = v1[0]*v1[1]*d + v2[0]*v2[1]/d;

    float delta = a3*a0 - a1*a2;
    if (IsInZeroRange2(delta))
    {
        return CombineSigAndGeoStretch(pMT, 0, fFace3DArea);
    }

    if (fRotMatrix)
    {
        fRotMatrix[0] = a3/delta;
        fRotMatrix[1] = -a1/delta;
        fRotMatrix[2] = -a2 / delta;
        fRotMatrix[3] = a0 / delta;
    }

    //float fTestStretch0 = (IMT[0]+IMT[2])/2;
    //float fTesttretch1 = IsochartSqrtf(d1*d2);
    //float fTestStretch2 = IMT[0]*IMT[2] - IMT[1]*IMT[1];

    float fSigStretch = ((a0*a0+a2*a2)*m0 + 2*(a0*a1+a2*a3)*m1+(a1*a1+a3*a3)*m2)/2;
    float fGeoStretch = (d*d + 1/(d*d))*fFace3DArea;

    return CombineSigAndGeoStretch(
        pMT, fSigStretch, fGeoStretch);
}

float Isochart::CombineSigAndGeoStretch(
    const float* pMT,
    float fSigStretch,
    float fGeoStretch)
{
    UNREFERENCED_PARAMETER(pMT);
    UNREFERENCED_PARAMETER(fGeoStretch);
    return fSigStretch;
}
