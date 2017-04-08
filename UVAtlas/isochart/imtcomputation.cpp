//-------------------------------------------------------------------------------------
// UVAtlas - imtcomputation.cpp
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
#include "isochart.h"
#include "isochartmesh.h"
#include "isochartutil.h"

#define TEXEL_OFFSET 0

using namespace Isochart;
using namespace DirectX;

namespace
{
    struct SUBFACE
    {
        uint32_t dwVertIdx[3];
        uint32_t dwDepth;
    };

    static bool IsInZeroRangeDouble(double a)
    {
        return a < 1e-12 && a > -1e-12;
    }


    static bool CheckIMTFromPerVertexSignalInput(
        const XMFLOAT3* pV3d,
        const float* pfSignalArray,
        size_t dwSignalDimension,
        FLOAT3* pfIMTArray)
    {
        UNREFERENCED_PARAMETER(dwSignalDimension);

        if (!pV3d || !pfSignalArray || !pfIMTArray)
        {
            return false;
        }

        return true;
    }

    static bool CheckIMTFromTextureMapInput(
        const XMFLOAT3* pV3d,	// [In] surface coordinates of face's vertices
        const XMFLOAT2* pUV,	// [In] Texture coordinates of each vertices in the range of 0 to 1.0f
        LPIMTSIGNALCALLBACK pfnGetSignal,
        FLOAT3* pfIMTArray)
    {
        if (!pV3d || !pUV || !pfIMTArray || !pfnGetSignal)
        {
            return false;
        }

        return true;
    }

    static void ConvertToCanonicalIMT(
        float* pfCanonicalIMT,
        float* pfIMT,
        const XMFLOAT3* pV3d,
        const XMFLOAT2* pUV)
    {
        float fOldA = Cal2DTriangleArea(
            pUV, pUV + 1, pUV + 2);
        if (IsInZeroRange2(fOldA))
        {
            return;
        }

        // Standard face parameterizaion
        XMFLOAT2 v2d[3];
        XMFLOAT3 axis[2]; // X, Y axis
        IsochartCaculateCanonicalCoordinates(
            pV3d,
            pV3d + 1,
            pV3d + 2,
            v2d,
            v2d + 1,
            v2d + 2,
            axis);


        float fNewA = Cal2DTriangleArea(
            v2d, v2d + 1, v2d + 2);

        AffineIMTOn2D(
            fNewA,
            v2d,
            v2d + 1,
            v2d + 2,
            pfCanonicalIMT,
            pUV,
            pUV + 1,
            pUV + 2,
            pfIMT,
            nullptr);

    }

    static void CalTriangleIMTFromPerVertexSignal(
        const XMFLOAT2* pv2D0,
        const XMFLOAT2* pv2D1,
        const XMFLOAT2* pv2D2,
        float f2D,
        float* Ss,
        float* St,
        const float* pfSignalArray,
        size_t dwSignalDimension,
        FLOAT3* pfIMTArray)
    {
        // if the face's area is 0, the signal may change sharply (when different signal on 3 vertices),
        // for this condition, just specify the IMT to be INF
        if (IsInZeroRange2(f2D))
        {
            (*pfIMTArray)[0] = (*pfIMTArray)[1] = (*pfIMTArray)[2] = 0;
        }
        else
        {
            float q[3];
            for (size_t ii = 0; ii < dwSignalDimension; ii++)
            {
                for (size_t jj = 0; jj < 3; jj++)
                {
                    q[jj] = pfSignalArray[jj*dwSignalDimension + ii];
                }

                Ss[ii] = (q[0] * (pv2D1->y - pv2D2->y) +
                    q[1] * (pv2D2->y - pv2D0->y) +
                    q[2] * (pv2D0->y - pv2D1->y)) / f2D;

                St[ii] = (q[0] * (pv2D2->x - pv2D1->x) +
                    q[1] * (pv2D0->x - pv2D2->x) +
                    q[2] * (pv2D1->x - pv2D0->x)) / f2D;
            }

            (*pfIMTArray)[0] = IsochartVectorDot(Ss, Ss, dwSignalDimension);
            (*pfIMTArray)[2] = IsochartVectorDot(St, St, dwSignalDimension);
            (*pfIMTArray)[1] = IsochartVectorDot(Ss, St, dwSignalDimension);
        }
    }

    // Is current triangle is needed to be splitted again.
    static bool IsContinueSplit(
        double d2dArea,
        double d3dArea,
        size_t dwMaxSplitLevel,
        float fMinVertexUvIDistance,
        SUBFACE* pFace,
        std::vector<XMFLOAT2>& vertList)
    {
        // 1. If enough depth has been arrived. Stop split.
        if (dwMaxSplitLevel != 0
            && pFace->dwDepth >= dwMaxSplitLevel)
        {
            return false;
        }

        if (IsInZeroRangeDouble(d3dArea / (uint64_t(1) << (uint64_t(pFace->dwDepth + 1) << 1))))
        {
            return false;
        }

        if (IsInZeroRangeDouble(d2dArea / (uint64_t(1) << (uint64_t(pFace->dwDepth + 1) << 1))))
        {
            return false;
        }

        // 2. If the distance between vertices is too small, stop split.
        XMFLOAT2 texCoord[3];
        for (size_t ii = 0; ii < 3; ii++)
        {
            texCoord[ii] = vertList[pFace->dwVertIdx[ii]];
        }

        for (size_t ii = 0; ii < 3; ii++)
        {
            XMVECTOR v = XMLoadFloat2(&texCoord[ii]) - XMLoadFloat2(&texCoord[(ii + 1) % 3]);
            float fDisc =
                XMVectorGetX(XMVector2Length(v));
            if (fDisc > fMinVertexUvIDistance)
            {
                return true; // if distance between any 2 vertices is large enough, continue splitting
            }
        }

        // 3. Stop splitting
        return false;
    }

    // Split current face
    HRESULT SplitFace(
        SUBFACE* pFace,
        std::queue<SUBFACE*>& subFaceList,
        std::vector<XMFLOAT2>& vertList)
    {
        HRESULT hr = S_OK;
        XMFLOAT2 vNew;

        uint32_t dwNewIdx = static_cast<uint32_t>(vertList.size());

        SUBFACE* pNewFace[4] = { nullptr, nullptr, nullptr, nullptr };

        // 1. Compute new vertices that split the original triangle into 4-sub triangles.
        try
        {
            for (size_t ii = 0; ii < 3; ii++)
            {
                XMStoreFloat2(&vNew,
                    (XMLoadFloat2(&vertList[pFace->dwVertIdx[ii]]) +
                    XMLoadFloat2(&vertList[pFace->dwVertIdx[(ii + 1) % 3]])) / 2);
                vertList.push_back(vNew);
            }
        }
        catch (std::bad_alloc&)
        {
            hr = E_OUTOFMEMORY;
            goto LEnd;
        }

        // 2. Push the new 4 sub-triangles into queue.
        for (size_t ii = 0; ii < 4; ii++)
        {
            pNewFace[ii] = new (std::nothrow) SUBFACE;
            if (!pNewFace[ii])
            {
                hr = E_OUTOFMEMORY;
                goto LEnd;

            }

            try
            {
                subFaceList.push(pNewFace[ii]);
            }
            catch (std::bad_alloc&)
            {
                delete pNewFace[ii];
                hr = E_OUTOFMEMORY;
                goto LEnd;
            }

            (pNewFace[ii])->dwDepth = pFace->dwDepth + 1;
        }

        assert( pNewFace[0] != 0 );
        assert( pNewFace[1] != 0 );
        assert( pNewFace[2] != 0 );
        assert( pNewFace[3] != 0 );

        _Analysis_assume_( pNewFace[0] != 0 );
        _Analysis_assume_( pNewFace[1] != 0 );
        _Analysis_assume_( pNewFace[2] != 0 );
        _Analysis_assume_( pNewFace[3] != 0 );

        // 3. Specify vertex indices of each sub-triangle
        (pNewFace[0])->dwVertIdx[0] = pFace->dwVertIdx[0];
        (pNewFace[0])->dwVertIdx[1] = dwNewIdx;
        (pNewFace[0])->dwVertIdx[2] = dwNewIdx + 2;

        (pNewFace[1])->dwVertIdx[0] = dwNewIdx;
        (pNewFace[1])->dwVertIdx[1] = pFace->dwVertIdx[1];
        (pNewFace[1])->dwVertIdx[2] = dwNewIdx + 1;

        (pNewFace[2])->dwVertIdx[0] = dwNewIdx + 2;
        (pNewFace[2])->dwVertIdx[1] = dwNewIdx + 1;
        (pNewFace[2])->dwVertIdx[2] = pFace->dwVertIdx[2];

        (pNewFace[3])->dwVertIdx[0] = dwNewIdx;
        (pNewFace[3])->dwVertIdx[1] = dwNewIdx + 1;
        (pNewFace[3])->dwVertIdx[2] = dwNewIdx + 2;

    LEnd:

        if (FAILED(hr))
        {
            while (!subFaceList.empty())
            {
                pFace = subFaceList.front();
                subFaceList.pop();
                delete pFace;
            }
        }

        return hr;
    }
}


//-------------------------------------------------------------------------------------
HRESULT WINAPI Isochart::IMTFromPerVertexSignal(
    const XMFLOAT3* pV3d,
    const float* pfSignalArray,
    size_t dwSignalDimension,
    FLOAT3* pfIMTArray)
{
    HRESULT hr = S_OK;

    if (!CheckIMTFromPerVertexSignalInput(
        pV3d, 
        pfSignalArray, 
        dwSignalDimension,
        pfIMTArray))
    {
        return E_INVALIDARG;;
    }

    std::unique_ptr<float[]> Ss(new (std::nothrow) float[dwSignalDimension]);
    std::unique_ptr<float[]> St(new (std::nothrow) float[dwSignalDimension]);

    if (!Ss || !St)
    {
        return E_OUTOFMEMORY;
    }

    // Standard face parameterizaion
    XMFLOAT2 v2d[3];
    XMFLOAT3 axis[2]; // X, Y axis
    IsochartCaculateCanonicalCoordinates(
        pV3d,
        pV3d+1,
        pV3d+2,
        v2d,
        v2d+1,
        v2d+2,
        axis);

    float f2D = Cal2DTriangleArea(
        v2d, v2d+1, v2d+2);

    CalTriangleIMTFromPerVertexSignal(
        v2d,
        v2d+1,
        v2d+2,
        f2D,
        Ss.get(),
        St.get(),
        pfSignalArray,
        dwSignalDimension,
        pfIMTArray);

    for (size_t ii = 0; ii<3; ii++)
    {
        if (IsInZeroRange2((*pfIMTArray)[ii]))
        {
            (*pfIMTArray)[ii] = 0;
        }
    }
    
    return hr;
}


//-------------------------------------------------------------------------------------
HRESULT WINAPI
Isochart::IMTFromTextureMap(
    const XMFLOAT3* pV3d,	// [In] surface coordinates of face's vertices
    const XMFLOAT2* pUV,	// [In] Texture coordinates of each vertices in the range of 0 to 1.0f
    size_t dwMaxSplitLevel,		// [In] Split into how many levels.
    float fMinVertexUvIDistance,	// [In] smallest vertices distance, in pixel.
    size_t uPrimitiveId,
    size_t dwSignalDimension,
    LPIMTSIGNALCALLBACK pfnGetSignal,
    void* lpTextureData,
    FLOAT3* pfIMTArray)	// [Out] Result IMT
{
    HRESULT hr = S_OK;

    if (!CheckIMTFromTextureMapInput(
        pV3d,
        pUV,
        pfnGetSignal,
        pfIMTArray))
    {
        return E_INVALIDARG;		
    }

    (*pfIMTArray)[0] = (*pfIMTArray)[1] = (*pfIMTArray)[2] = 0;

    // 1. Allocate needed resource
    std::unique_ptr<float[]> Ss( new (std::nothrow) float[dwSignalDimension] );
    std::unique_ptr<float[]> St( new (std::nothrow) float[dwSignalDimension] );
    std::unique_ptr<float[]> pfTriangleSignal( new (std::nothrow) float[dwSignalDimension * 3] );
    std::unique_ptr<float[]> pfSignalBase;

    if (!Ss || !St || !pfTriangleSignal)
    {
        return E_OUTOFMEMORY;
    }

    // 2. Build a Queue to store all sub-triangle
    // Actually, using recursing function here seems easier to be understand, however,
    // to avoid potential stack overflow, just using a queue to simulate the recursing process.
    double dTotalIMT[3];
    FLOAT3 tempIMT;
    std::queue<SUBFACE*> subFaceIdxList;
    std::vector<SUBFACE*> finalSubFaceIdxList;
    std::vector<XMFLOAT2> vertList;

    double d3dArea = fabs(Cal3DTriangleArea(
        pV3d, pV3d+1, pV3d+2));
    double d2dArea = fabs(Cal2DTriangleArea(
        pUV, pUV+1, pUV+2));

    if (IsInZeroRangeDouble(d3dArea) ||
        IsInZeroRangeDouble(d2dArea))
    {
        DPF(0, "IMTFromTextureMap failed due to zero area");
        return E_FAIL;
    }
    
    // 2.1 Initialize splitting face queue
    auto pFace = new (std::nothrow) SUBFACE;
    if (!pFace)
    {
        return E_OUTOFMEMORY;
    }

    try
    {
        subFaceIdxList.push(pFace);
    }
    catch (std::bad_alloc&)
    {
        delete pFace;
        return E_OUTOFMEMORY;
    }

    float* pfSignal = nullptr;

    pFace->dwDepth = 0;
    try
    {
        for (uint32_t ii = 0; ii < 3; ii++)
        {
            vertList.push_back(pUV[ii]);
            pFace->dwVertIdx[ii] = ii;
        }
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }

    // 3. Split triangle to get sub-triangles for integration IMT.
    do
    {
        assert(!subFaceIdxList.empty());

        SUBFACE* pCurrFace = subFaceIdxList.front();
        subFaceIdxList.pop();

        // Need to split current face again?
        if (IsContinueSplit(
            d2dArea,
            d3dArea,
            dwMaxSplitLevel, 
            fMinVertexUvIDistance, 
            pCurrFace,
            vertList))
        {
            // Split current face
            if (FAILED(hr = SplitFace(
                pCurrFace, 
                subFaceIdxList, 
                vertList)))
            {
                delete pCurrFace;
                goto LEnd;
            }
            delete pCurrFace;
        }
        else
        {
            try
            {
                finalSubFaceIdxList.push_back(pCurrFace);
            }
            catch (std::bad_alloc&)
            {
                delete pCurrFace;
                hr = E_OUTOFMEMORY;
                goto LEnd;
            }
        }				
    }while(!subFaceIdxList.empty());

    // 4. Get signal on all vertex.
    pfSignalBase.reset( new (std::nothrow) float[vertList.size() * dwSignalDimension] );
    if (!pfSignalBase)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }
    
    pfSignal = pfSignalBase.get();
    for (size_t ii = 0; ii<vertList.size(); ii++)
    {		
        XMFLOAT2 coord = vertList[ii];
                
        hr = pfnGetSignal(
            &coord,
            uPrimitiveId,
            dwSignalDimension,
            lpTextureData,
            pfSignal);

        if (FAILED(hr))
        {
            goto LEnd;
        }
        
        pfSignal += dwSignalDimension;
    }

    
    dTotalIMT[0] = dTotalIMT[1] = dTotalIMT[2] = 0;
    for (size_t ii = 0; ii<finalSubFaceIdxList.size(); ii++)
    {
        SUBFACE* pCurrFace = finalSubFaceIdxList[ii];
        // Compute IMT of current face
        pfSignal = pfTriangleSignal.get();
        for (size_t jj = 0; jj<3; jj++)
        {
            memcpy(
                pfSignal,
                pfSignalBase.get()+pCurrFace->dwVertIdx[jj],
                sizeof(float)*dwSignalDimension);

            pfSignal += dwSignalDimension;
        }

        float fA = static_cast<float>(d2dArea / (uint64_t(1) << (uint64_t(pCurrFace->dwDepth) << 1) ));
        // Compute IMT using standard parameterization coordinates.
        CalTriangleIMTFromPerVertexSignal(
            &(vertList[pCurrFace->dwVertIdx[0]]),
            &(vertList[pCurrFace->dwVertIdx[1]]),
            &(vertList[pCurrFace->dwVertIdx[2]]),
            fA,
            Ss.get(),
            St.get(),
            pfTriangleSignal.get(),
            dwSignalDimension,
            &tempIMT);

        double dIntegratedArea = d3dArea / (uint64_t(1) << (uint64_t(pCurrFace->dwDepth) << 1) );			
        dTotalIMT[0] += tempIMT[0]*dIntegratedArea;
        dTotalIMT[1] += tempIMT[1]*dIntegratedArea;
        dTotalIMT[2] += tempIMT[2]*dIntegratedArea;
        delete pCurrFace;
    }

    
    for (size_t ii = 0; ii<IMT_DIM; ii++)
    {
        (*pfIMTArray)[ii] = static_cast<float>((*pfIMTArray)[ii] / d3dArea);
    }

    // 4. Convert to canonical IMT 
    ConvertToCanonicalIMT(
        (*pfIMTArray), (*pfIMTArray), pV3d, pUV);

    finalSubFaceIdxList.clear();

LEnd:
    while (!subFaceIdxList.empty())
    {
        pFace = subFaceIdxList.front();
        subFaceIdxList.pop();
        delete pFace;
    }

    for (size_t ii = 0; ii<finalSubFaceIdxList.size(); ii++)
    {
        delete finalSubFaceIdxList[ii];
    }

    for (size_t ii = 0; ii<3; ii++)
    {
        if (IsInZeroRange2((*pfIMTArray)[ii]))
        {
            (*pfIMTArray)[ii] = 0;
        }
    }
    
    return hr;
}



//////////////////////////////////////////////////////////////////////////////////////////
namespace
{
    struct DOUBLEVECTOR2
    {
        double x;
        double y;
    };

    static inline uint32_t intround(double v)
    {
        v=floor(v + 0.5);
        return static_cast<uint32_t>(v);
    }
    static inline double minPos(
        double pos,
        double gutter)
    {
        double result;
        if (pos < 0)
        {
            result = pos - (gutter + fmod(pos, gutter));
        }
        else
        {
            result = pos - fmod(pos, gutter);
        }

        return IsInZeroRangeDouble(pos - result - gutter) ?
        pos : result;
    }

    static inline double maxPos(
        double pos,
        double gutter)
    {
        double result;
        if (pos < 0)
        {
            result = pos - fmod(pos, gutter);
        }
        else
        {
            result = pos + (gutter - fmod(pos, gutter));
        }

        return IsInZeroRangeDouble(result - pos - gutter) ?
        pos : result;
    }

    static inline void GetBoundOnLine(
        double* pLine,
        double& minBound,
        double& maxBound)
    {
        minBound = DBL_MAX;
        maxBound = -DBL_MAX;

        for (size_t ii = 0; ii<3; ii++)
        {
            if (pLine[ii] != DBL_MAX)
            {
                if (minBound > pLine[ii])
                {
                    minBound = pLine[ii];
                }
                if (maxBound < pLine[ii])
                {
                    maxBound = pLine[ii];
                }
            }
        }
    }
    static void GetCoveredPixelsCount(
        DOUBLEVECTOR2* pUV,
        double fTexelLengthW,
        double fTexelLengthH,
        DOUBLEVECTOR2& leftBottom,
        uint32_t& dwRowLineCount,
        uint32_t& dwColLineCount)
    {
        DOUBLEVECTOR2 minV = { DBL_MAX, DBL_MAX };
        DOUBLEVECTOR2 maxV = { -DBL_MAX, -DBL_MAX };

        for (size_t ii = 0; ii<3; ii++)
        {
            if (minV.x > pUV[ii].x)
            {
                minV.x = pUV[ii].x;
            }
            if (minV.y > pUV[ii].y)
            {
                minV.y = pUV[ii].y;
            }
            if (maxV.x < pUV[ii].x)
            {
                maxV.x = pUV[ii].x;
            }
            if (maxV.y < pUV[ii].y)
            {
                maxV.y = pUV[ii].y;
            }
        }

        // 1. Decide the left boundary
        leftBottom.x = minPos(minV.x, fTexelLengthW);

        // 2. Decide the bottom boundary
        leftBottom.y = minPos(minV.y, fTexelLengthH);

        // 3. Decide the right boundary
        DOUBLEVECTOR2 rightTop;
        rightTop.x = maxPos(maxV.x, fTexelLengthW);

        // 4. Decide the top boundary
        rightTop.y = maxPos(maxV.y, fTexelLengthH);

        dwColLineCount = intround((rightTop.x - leftBottom.x) / fTexelLengthW) + 1;
        dwRowLineCount = intround((rightTop.y - leftBottom.y) / fTexelLengthH) + 1;
    }

    static HRESULT ComputeAllIntersection(
        DOUBLEVECTOR2* pUV,
        double fTexelLengthW,
        double fTexelLengthH,
        DOUBLEVECTOR2& leftBottom,
        size_t dwRowCount,
        size_t dwColCount,
        double* rgvVerticalIntersection,
        double* rgvHorizonIntersection)
    {
        assert(rgvHorizonIntersection != 0);
        assert(rgvVerticalIntersection != 0);

        HRESULT hr = S_OK;


        for (size_t ii = 0; ii < dwRowCount * 3; ii++)
        {
            rgvHorizonIntersection[ii] = DBL_MAX;
        }

        for (size_t ii = 0; ii < dwColCount * 3; ii++)
        {
            rgvVerticalIntersection[ii] = DBL_MAX;
        }

        // Compute the intersection between vertical lines with the 3 triangle edges
        for (size_t ii = 0; ii < 3; ii++)
        {
            DOUBLEVECTOR2 v0, v1;

            double fx, fy;
            if (pUV[ii].x < pUV[(ii + 1) % 3].x)
            {
                v0.x = pUV[ii].x, v0.y = pUV[ii].y;
                v1.x = pUV[(ii + 1) % 3].x, v1.y = pUV[(ii + 1) % 3].y;
            }
            else
            {
                v1.x = pUV[ii].x, v1.y = pUV[ii].y;
                v0.x = pUV[(ii + 1) % 3].x, v0.y = pUV[(ii + 1) % 3].y;
            }

            if (IsInZeroRangeDouble(v1.x - v0.x))
            {
                continue;
            }

            fx = maxPos(v0.x, fTexelLengthW);
            if (fx > v1.x)
            {
                continue;
            }

            fy = (fx - v0.x)*(v1.y - v0.y) / (v1.x - v0.x) + v0.y;
            double fYDelta = (fTexelLengthW) *(v1.y - v0.y) / (v1.x - v0.x);


            uint32_t dwStart = intround((fx - leftBottom.x) / fTexelLengthW);

            double* p =
                rgvVerticalIntersection + dwStart * 3 + ii;
            do
            {
                *p = fy;
                fy += fYDelta;
                fx += fTexelLengthW;

                p += 3;
                dwStart++;
            } while (fx <= v1.x);
            assert(dwStart <= dwColCount);
        }

        // Compute the intersection between horizonal lines with the 3 triangle edges
        for (size_t ii = 0; ii < 3; ii++)
        {
            DOUBLEVECTOR2 v0, v1;
            double fx, fy;
            if (pUV[ii].y < pUV[(ii + 1) % 3].y)
            {
                v0.x = pUV[ii].x, v0.y = pUV[ii].y;
                v1.x = pUV[(ii + 1) % 3].x, v1.y = pUV[(ii + 1) % 3].y;
            }
            else
            {
                v1.x = pUV[ii].x, v1.y = pUV[ii].y;
                v0.x = pUV[(ii + 1) % 3].x, v0.y = pUV[(ii + 1) % 3].y;
            }
            if (IsInZeroRangeDouble(v1.y - v0.y))
            {
                continue;
            }

            fy = maxPos(v0.y, fTexelLengthH);
            if (fy > v1.y)
            {
                continue;
            }

            fx = (fy - v0.y)*(v1.x - v0.x) / (v1.y - v0.y) + v0.x;
            double fXDelta = (fTexelLengthH) *(v1.x - v0.x) / (v1.y - v0.y);

            uint32_t dwStart = intround((fy - leftBottom.y) / fTexelLengthH);

            double* p =
                rgvHorizonIntersection + dwStart * 3 + ii;
            do
            {
                *p = fx;
                fx += fXDelta;
                fy += fTexelLengthH;
                p += 3;
                dwStart++;
            } while (fy <= v1.y);
            assert(dwStart <= dwRowCount);
        }

        return hr;
    }

    static bool IsPointInSquare(
        DOUBLEVECTOR2& leftBottom,
        double fEdgeLengthW,
        double fEdgeLengthH,
        DOUBLEVECTOR2& point)
    {
        return
            point.x >= leftBottom.x &&
            point.y >= leftBottom.y &&
            point.x <= leftBottom.x + fEdgeLengthW &&
            point.y <= leftBottom.y + fEdgeLengthH;
    }

    static HRESULT GenerateAccumulationLines(
        std::vector<DOUBLEVECTOR2>& keyPointList, // all key points on the lines, composite a convex polygon
        std::vector<DOUBLEVECTOR2>& above, // above line when applying 2 times accumulation
        std::vector<DOUBLEVECTOR2>& below)// below line when applying 2 times accumulation
    {
        if (keyPointList.empty())
        {
            return S_OK;
        }

        for (size_t ii = 0; ii < keyPointList.size() - 1; ii++)
        {
            for (size_t jj = ii + 1; jj < keyPointList.size();)
            {
                if (IsInZeroRangeDouble(keyPointList[jj].x - keyPointList[ii].x) &&
                    IsInZeroRangeDouble(keyPointList[jj].y - keyPointList[ii].y))
                {
                    keyPointList.erase(keyPointList.begin() + jj);
                }
                else
                {
                    jj++;
                }
            }
        }
        if (keyPointList.size() < 3)
        {
            return S_OK;
        }

        // 1. Find the left most point
        DOUBLEVECTOR2 leftMost = { DBL_MAX, DBL_MAX };
        DOUBLEVECTOR2 rightMost = { -DBL_MAX, -DBL_MAX };
        DOUBLEVECTOR2 tempV;
        double fTemp;

        std::unique_ptr<double[]> tanList(new (std::nothrow) double[keyPointList.size()]);
        if (!tanList)
        {
            return E_OUTOFMEMORY;
        }

        double* pTanList = tanList.get();

        uint32_t dwLeftMost = 0;
        for (uint32_t ii = 0; ii<keyPointList.size(); ii++)
        {
            if (leftMost.x > keyPointList[ii].x ||
                (leftMost.x == keyPointList[ii].x && leftMost.y > keyPointList[ii].y))
            {
                leftMost = keyPointList[ii];
                dwLeftMost = ii;
            }
            if (rightMost.x < keyPointList[ii].x ||
                (rightMost.x == keyPointList[ii].x && rightMost.y > keyPointList[ii].y))
            {
                rightMost = keyPointList[ii];
            }
        }
        keyPointList[dwLeftMost] = keyPointList[0];
        keyPointList[0] = leftMost;

        // 2. Sort points along the counter clock-wise direction.
        for (size_t ii = 1; ii < keyPointList.size(); ii++)
        {
            double fy = keyPointList[ii].y - leftMost.y;
            double fx = keyPointList[ii].x - leftMost.x;

            if (IsInZeroRangeDouble(fx))
            {
                if (IsInZeroRangeDouble(fy))
                {
                    pTanList[ii] = -DBL_MAX;
                }
                else if (fy > 0)
                {
                    pTanList[ii] = DBL_MAX;
                }
                else
                {
                    pTanList[ii] = -DBL_MAX;
                }
            }
            else
            {
                pTanList[ii] = fy / fx;
            }
        }

        for (size_t ii = 1; ii < keyPointList.size() - 1; ii++)
        {
            for (size_t jj = ii + 1; jj<keyPointList.size(); jj++)
            {
                if (pTanList[ii] > pTanList[jj])
                {
                    tempV = keyPointList[ii];
                    keyPointList[ii] = keyPointList[jj];
                    keyPointList[jj] = tempV;

                    fTemp = pTanList[ii];
                    pTanList[ii] = pTanList[jj];
                    pTanList[jj] = fTemp;
                }
            }
        }

        // 3. Get above & below lines.
        try
        {
            uint32_t dwCur = 0;
            do
            {
                below.push_back(keyPointList[dwCur]);
                dwCur++;
            } while (dwCur < keyPointList.size()
                && keyPointList[dwCur - 1].x < keyPointList[dwCur].x);

            assert(keyPointList[dwCur - 1].x == rightMost.x);
            assert(keyPointList[dwCur - 1].y == rightMost.y);

            if (keyPointList[keyPointList.size() - 1].x > leftMost.x)
            {
                above.push_back(leftMost);
            }

            for (size_t jj = keyPointList.size() - 1; jj > 0; jj--)
            {
                above.push_back(keyPointList[jj]);

                if (keyPointList[jj].x >= rightMost.x)
                {
                    break;
                }
            }
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        for (size_t ii = 0; ii < above.size() - 1; ii++)
        {
            assert(above[ii].x <= above[ii + 1].x);
        }
        for (size_t ii = 0; ii < below.size() - 1; ii++)
        {
            assert(below[ii].x <= below[ii + 1].x);
        }

        return S_OK;
    }

    static bool CalculateLineParameters(
        DOUBLEVECTOR2& v1,
        DOUBLEVECTOR2& v2,
        double& a,
        double& b)
    {

        if (IsInZeroRangeDouble(v2.x - v1.x))
        {
            return false;
        }

        a = (v2.y - v1.y) / (v2.x - v1.x);
        b = v1.y - v1.x*(v2.y - v1.y) / (v2.x - v1.x);

        return true;
    }

    static uint32_t NextItegralPoint(
        std::vector<DOUBLEVECTOR2>& line,
        uint32_t dwCur)
    {
        uint32_t dwNext = dwCur + 1;
        while (dwNext < line.size())
        {
            if (!IsInZeroRangeDouble(line[dwNext].x - line[dwCur].x))
            {
                return dwNext;
            }
            dwNext++;
        }
        return INVALID_INDEX;
    }

    static HRESULT Accumulation(
        DOUBLEVECTOR2* corner,
        float* pfSignal,
        size_t dwSignalDimension,
        std::vector<DOUBLEVECTOR2>& above, // above line when applying 2 times accumulation
        std::vector<DOUBLEVECTOR2>& below,
        double IMTResult [],
        double& dPieceArea)// below line when applying 2 times accumulation)
    {
        HRESULT hr = S_OK;

        dPieceArea = 0;
        if (above.size() < 2 || below.size() < 2)
        {
            return hr;
        }
        assert(above[0].x == below[0].x);

        std::unique_ptr<double[]> m(new (std::nothrow) double[4 * dwSignalDimension]);
        if (!m)
        {
            return E_OUTOFMEMORY;
        }

        double* m1 = m.get();
        double* m2 = m1 + dwSignalDimension;
        double* m3 = m2 + dwSignalDimension;
        double* m4 = m3 + dwSignalDimension;

        /*
        // Following is a texel,

        // (c) _ _ _ _ (d)
        //  |               |
        //  |               |
        //  |               |
        //  |               |
        // (a)-------(b)
        */
        for (size_t ii = 0; ii < dwSignalDimension; ii++)
        {
            double a = pfSignal[0 * dwSignalDimension + ii];
            double b = pfSignal[1 * dwSignalDimension + ii];
            double c = pfSignal[2 * dwSignalDimension + ii];
            double d = pfSignal[3 * dwSignalDimension + ii];

            m1[ii] = a + d - c - b;
            m2[ii] = (b - a)*corner[1].y + (c - d)*corner[0].y;
            m3[ii] = a + d - c - b;
            m4[ii] = (c - a)*corner[1].x + (b - d)*corner[0].x;
        }

        // C
        uint32_t dwCurrA = 0, dwCurrB = 0;
        uint32_t dwNextA = NextItegralPoint(above, dwCurrA);
        uint32_t dwNextB = NextItegralPoint(below, dwCurrB);

        double a2 = 0, b2 = 0, a1 = 0, b1 = 0;
        bool bNewSegmentA = true;
        bool bNewSegmentB = true;

        double fStartX = above[dwCurrA].x;
        double fEndX = fStartX;

        while (dwNextA != INVALID_INDEX && dwNextB != INVALID_INDEX)
        {
            fStartX = fEndX;
            if (bNewSegmentA)
            {
                CalculateLineParameters(
                    above[dwCurrA],
                    above[dwNextA],
                    a2,
                    b2);
                bNewSegmentA = false;
            }
            if (bNewSegmentB)
            {
                CalculateLineParameters(
                    below[dwCurrB],
                    below[dwNextB],
                    a1,
                    b1);
                bNewSegmentB = false;
            }

            if (IsInZeroRangeDouble(above[dwNextA].x - below[dwNextB].x))
            {
                fEndX = above[dwNextA].x;
                dwCurrA = dwNextA;
                dwCurrB = dwNextB;
                dwNextA = NextItegralPoint(above, dwCurrA);
                dwNextB = NextItegralPoint(below, dwCurrB);

                bNewSegmentA = true;
                bNewSegmentB = true;
            }
            else if (above[dwNextA].x < below[dwNextB].x)
            {
                fEndX = above[dwNextA].x;
                dwCurrA = dwNextA;
                dwNextA = NextItegralPoint(above, dwCurrA);
                bNewSegmentA = true;
            }
            else
            {
                fEndX = below[dwNextB].x;
                dwCurrB = dwNextB;
                dwNextB = NextItegralPoint(below, dwCurrB);
                bNewSegmentB = true;
            }

            double aa1 = a1*a1;
            double aaa1 = aa1*a1;
            double aa2 = a2*a2;
            double aaa2 = aa2*a2;

            double bb1 = b1*b1;
            double bbb1 = bb1*b1;
            double bb2 = b2*b2;
            double bbb2 = bb2*b2;

            double u1 = fStartX;
            double uu1 = u1*u1;
            double uuu1 = uu1*u1;
            double uuuu1 = uu1*uu1;

            double u2 = fEndX;
            double uu2 = u2*u2;
            double uuu2 = uu2*u2;
            double uuuu2 = uu2*uu2;

            dPieceArea += (a2 - a1)*(uu2 - uu1) / 2 + (b2 - b1)*(u2 - u1);

            for (size_t ii = 0; ii < dwSignalDimension; ii++)
            {
                double n3 = m1[ii] * m1[ii] * (aaa2 - aaa1) / 3;
                double n2 = m1[ii] * m1[ii] * (aa2*b2 - aa1*b1) + m1[ii] * m2[ii] * (aa2 - aa1);
                double n1 = m1[ii] * m1[ii] * (a2*bb2 - a1*bb1) + 2 * m1[ii] * m2[ii] * (a2*b2 - a1*b1) + m2[ii] * m2[ii] * (a2 - a1);
                double n0 = m1[ii] * m1[ii] * (bbb2 - bbb1) / 3 + m1[ii] * m2[ii] * (bb2 - bb1) + m2[ii] * m2[ii] * (b2 - b1);
                double fTemp =
                    n3*(uuuu2 - uuuu1) / 4 +
                    n2*(uuu2 - uuu1) / 3 +
                    n1*(uu2 - uu1) / 2 +
                    n0*(u2 - u1);

                if (fTemp < 0) fTemp = 0;//Theoritically, the result must larger than 0
                IMTResult[0] += fTemp;

                n3 = m3[ii] * m3[ii] * (a2 - a1);
                n2 = 2 * m3[ii] * m4[ii] * (a2 - a1) + m3[ii] * m3[ii] * (b2 - b1);
                n1 = 2 * m3[ii] * m4[ii] * (b2 - b1) + m4[ii] * m4[ii] * (a2 - a1);
                n0 = m4[ii] * m4[ii] * (b2 - b1);

                fTemp =
                    n3*(uuuu2 - uuuu1) / 4 +
                    n2*(uuu2 - uuu1) / 3 +
                    n1*(uu2 - uu1) / 2 +
                    n0*(u2 - u1);
                if (fTemp < 0) fTemp = 0; //Theoritically, the result must larger than 0

                IMTResult[2] += fTemp;

                n3 = m1[ii] * m3[ii] * (aa2 - aa1) / 2;
                n2 = m1[ii] * m4[ii] * (aa2 - aa1) / 2 + m1[ii] * m3[ii] * (a2*b2 - a1*b1) + m2[ii] * m3[ii] * (a2 - a1);
                n1 = m1[ii] * m3[ii] * (bb2 - bb1) / 2 + m1[ii] * m4[ii] * (a2*b2 - a1*b1) + m2[ii] * m4[ii] * (a2 - a1) + m2[ii] * m3[ii] * (b2 - b1);
                n0 = m1[ii] * m4[ii] * (bb2 - bb1) / 2 + m2[ii] * m4[ii] * (b2 - b1);
                IMTResult[1] +=
                    n3*(uuuu2 - uuuu1) / 4 +
                    n2*(uuu2 - uuu1) / 3 +
                    n1*(uu2 - uu1) / 2 +
                    n0*(u2 - u1);
            }
        }

        double fPixelSize =
            (corner[1].x - corner[0].x)*(corner[1].y - corner[0].y);

        IMTResult[0] /= (fPixelSize*fPixelSize);
        IMTResult[1] /= (fPixelSize*fPixelSize);
        IMTResult[2] /= (fPixelSize*fPixelSize);

        return hr;
    }

    static HRESULT ComputeIMTOnPixel(
        double tempIMT [],
        DOUBLEVECTOR2* pUV,
        double fTexelLengthW,
        double fTexelLengthH,
        size_t dwRow,
        double* rgvHorizonIntersection,
        size_t dwCol,
        double* rgvVerticalIntersection,
        DOUBLEVECTOR2& leftBottom,
        size_t uPrimitiveId,
        size_t dwSignalDimension,
        LPIMTSIGNALCALLBACK pfnGetSignal,
        void* lpTextureData,
        double& dPieceArea)
    {
        HRESULT hr = S_OK;

        dPieceArea = 0;
        assert(tempIMT != 0);
        memset(tempIMT, 0, sizeof(double)*IMT_DIM);

        DOUBLEVECTOR2 corner[2];
        corner[0].x = leftBottom.x + dwCol*fTexelLengthW;
        corner[0].y = leftBottom.y + dwRow*fTexelLengthH;

        corner[1].x = corner[0].x + fTexelLengthW;
        corner[1].y = corner[0].y + fTexelLengthH;

        std::vector<DOUBLEVECTOR2> keyPointList;
        DOUBLEVECTOR2 p, p1;

        // Find the points belong to square and inside the triangle
        try
        {
            for (size_t ii = 0; ii < 2; ii++)
            {
                double minX = 0, minY = 0, maxX = 0, maxY = 0;

                GetBoundOnLine(
                    rgvHorizonIntersection + (dwRow + ii) * 3,
                    minX,
                    maxX);

                p.y = corner[ii].y;
                for (size_t jj = 0; jj < 2; jj++)
                {
                    p.x = corner[jj].x;
                    GetBoundOnLine(
                        rgvVerticalIntersection + (dwCol + jj) * 3,
                        minY,
                        maxY);

                    if (p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY)
                    {
                        keyPointList.push_back(p);
                    }
                }
            }
            // Find all intersection on the pixel boundary
            for (size_t ii = 0; ii<2; ii++)
            {
                double minX = 0, minY = 0, maxX = 0, maxY = 0;
                GetBoundOnLine(
                    rgvHorizonIntersection + (dwRow + ii) * 3,
                    minX,
                    maxX);
                GetBoundOnLine(
                    rgvVerticalIntersection + (dwCol + ii) * 3,
                    minY,
                    maxY);

                if (minX > corner[0].x && minX < corner[1].x)
                {
                    p1.x = minX;
                    p1.y = corner[ii].y;
                    keyPointList.push_back(p1);
                }

                if (maxX > corner[0].x && maxX < corner[1].x)
                {
                    p1.x = maxX;
                    p1.y = corner[ii].y;
                    keyPointList.push_back(p1);
                }

                if (minY > corner[0].y && minY < corner[1].y)
                {
                    p1.x = corner[ii].x;
                    p1.y = minY;
                    keyPointList.push_back(p1);
                }

                if (maxY > corner[0].y && maxY < corner[1].y)
                {
                    p1.x = corner[ii].x;
                    p1.y = maxY;
                    keyPointList.push_back(p1);
                }
            }


            // Find the points belong to the triangle and inside the square
            for (size_t jj = 0; jj < 3; jj++)
            {
                if (IsPointInSquare(
                    corner[0], fTexelLengthW, fTexelLengthH, pUV[jj]))
                {
                    keyPointList.push_back(pUV[jj]);
                }
            }
            if (keyPointList.size() < 3)
            {
                return hr;
            }
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        std::vector< DOUBLEVECTOR2 > above;
        std::vector< DOUBLEVECTOR2 > below;

        FAILURE_RETURN(
            GenerateAccumulationLines(
            keyPointList,
            above,
            below));

        if (above.size() < 2 || below.size() < 2)
        {
            return hr;
        }

        XMFLOAT2 c;
        std::unique_ptr<float[]> signalBase(new (std::nothrow) float[dwSignalDimension*sizeof(float) * 4]);
        if (!signalBase)
        {
            return E_OUTOFMEMORY;
        }

        float* pfSignal = signalBase.get();
        for (size_t ii = 0; ii < 2; ii++)
        {
            c.y = (float) (corner[ii].y);
            for (size_t jj = 0; jj < 2; jj++)
            {
                c.x = (float) (corner[jj].x);
                hr = pfnGetSignal(
                    &c,
                    uPrimitiveId,
                    dwSignalDimension,
                    lpTextureData,
                    pfSignal);
                if (FAILED(hr))
                {
                    return hr;
                }
                pfSignal += dwSignalDimension;
            }
        }

        hr = Accumulation(
            corner,
            signalBase.get(),
            dwSignalDimension,
            above,
            below,
            tempIMT,
            dPieceArea);

        return hr;
    }

    struct IMTFloatArrayDescIn
    {
        float *pTexture;
        size_t uHeight, uWidth, uStride;
    };
}


//-------------------------------------------------------------------------------------
HRESULT WINAPI
Isochart::IMTFromTextureMapEx(
    const XMFLOAT3* pV3d,	// [In] surface coordinates of face's vertices
    const XMFLOAT2* pUV,	// [In] Texture coordinates of each vertices in the range of 0 to 1.0f
    size_t uPrimitiveId,
    size_t dwSignalDimension,
    LPIMTSIGNALCALLBACK pfnGetSignal,
    void* lpTextureData,
    FLOAT3* pfIMTArray)	// [Out] Result IMT
{
    HRESULT hr = S_OK;

    uint32_t dwRowLineCount = 0;
    uint32_t dwColLineCount = 0;

    SetAllIMTValue(
        *pfIMTArray, 0);

    float f3dArea = fabsf(Cal3DTriangleArea(
        pV3d, pV3d+1, pV3d+2));

    float f2dArea = fabsf(Cal2DTriangleArea(
        pUV, pUV+1, pUV+2));

    if (IsInZeroRange2(f3dArea) || IsInZeroRange2(f2dArea))
    {
        return S_OK;
    }

    auto pTexDesc = reinterpret_cast<IMTFloatArrayDescIn*>( lpTextureData );
    DOUBLEVECTOR2 leftBottom = {0.0, 0.0};

    double fTexelLengthW = (1.0 / pTexDesc->uWidth);
    double fTexelLengthH = (1.0 / pTexDesc->uHeight);

    DOUBLEVECTOR2 uv[3] = {0};
    for (size_t ii = 0; ii<3; ii++)
    {
        uv[ii].x = pUV[ii].x;
        uv[ii].y = pUV[ii].y;		
    }

    GetCoveredPixelsCount(
        uv,
        fTexelLengthW, 
        fTexelLengthH, 
        leftBottom, 
        dwRowLineCount, 
        dwColLineCount);

    std::unique_ptr<double[]> rgvHorizonIntersection(new (std::nothrow) double[3 * dwRowLineCount]);
    std::unique_ptr<double[]> rgvVerticalIntersection(new (std::nothrow) double[3 * dwColLineCount]);

    if (!rgvHorizonIntersection || !rgvVerticalIntersection)
    {
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = ComputeAllIntersection(
        uv, 
        fTexelLengthW, 
        fTexelLengthH, 
        leftBottom, 
        dwRowLineCount,
        dwColLineCount, 
        rgvVerticalIntersection.get(),
        rgvHorizonIntersection.get())))
    {
        return hr;
    }

    double tempIMT[IMT_DIM];
    double tempSumIMT[IMT_DIM];
    
    memset(tempSumIMT, 0, sizeof(double)*IMT_DIM);

    double dTotal2DArea = 0;
    double dPieceArea = 0;
    for (size_t ii = 0; ii<dwRowLineCount - 1; ii++)
    {
        for (size_t jj = 0; jj<dwColLineCount - 1; jj++)
        {
            if (FAILED(hr = ComputeIMTOnPixel(
                tempIMT, 
                uv,
                fTexelLengthW,
                fTexelLengthH,
                ii, 
                rgvHorizonIntersection.get(),	
                jj, 
                rgvVerticalIntersection.get(),
                leftBottom, 
                uPrimitiveId,
                dwSignalDimension,
                pfnGetSignal,
                lpTextureData,
                dPieceArea)))
            {
                return hr;
            }

            for (size_t kk = 0; kk<IMT_DIM; kk++)
            {
                tempSumIMT[kk] += tempIMT[kk];
            }

            dTotal2DArea += dPieceArea;
        }
    }

    DPF(3, "2d area by formal %f", f2dArea);
    DPF(3, "integrated 2d area %f", float(dTotal2DArea));

    // 2. Standard face parameterizaion
    for (size_t ii = 0; ii<IMT_DIM; ii++)
    {
        (*pfIMTArray)[ii] = static_cast<float>(tempSumIMT[ii]);
    }

    ConvertToCanonicalIMT(
        (*pfIMTArray),
        (*pfIMTArray),
        pV3d,
        pUV);

    for (size_t ii = 0; ii<IMT_DIM; ii++)
    {
        (*pfIMTArray)[ii] /= f3dArea;
    }

    return hr;
}
