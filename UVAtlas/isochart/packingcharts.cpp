//-------------------------------------------------------------------------------------
// UVAtlas - packingcharts.cpp
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

/*
    [Levy 2002]Levy, B. Pettjean, S., Ray, N.,And Mallet, J.-L. 2002
    Least squares conformal maps for automatic texture atlas generation.
    In Processings of SIGGRAPH 2002. 362-371.

    [SGSH02] SANDER P., GORTLER S., SNYDER J., HOPPE H.:
    Signal-specialized parameterization
    In Proceedings of Eurographics Workshop on Rendering 2002(2002)

    Algorithm Introduction:

    The packing algorithm is an extension of the "Tetris" algroithm in
    [Levy 2002]. In "Tetris", charts can only be introducted from the top;
    isochart introduces them from the top, bottom, left or right sides.   

    This packing algroithm keeps track of the current "borders" of atlas
    for each of the four directions, given all the charts introduced so far.

    Specified the desired atlas width and height, packing performs the
    following steps:
        (1) Align all charts to their longest axis.
        (2) Rescale charts
        (3) For each chart:
            - Choose the direction pair in which to add the chart
              The rule is try to keep atlas width/height ratio.
            - Insert the chart into the atlas
              From the direction pair of the previous step, choose the
              single direction that wastes the least space.
            - Merge the new chart and old atlas border for next iteration.

    Terms:
    Radial direction:
        Direction that the chart is packed into atlas. E.g. when packing
        a chart from left or right side, radial direction is along X axis,
        otherwise, it's along Y axis.

    Tangent direction:
        Tangent direction is the counterpart of radial direction.
        When packing a chart along radial direction. the chart can move
        along a corresponding tangent direction to find the best place.
        If radial direction is along X axis then tangent direction is
        along Y coordinate.

                                Tangent
                              --------->
                                   |
                                   | Radial
                                   |
                                   V
                        |-------------------|
                        |                   |   Radial    |
                        |        Atlas      | <-----------| Tangent
                        |                   |             |
                        --------------------|             V
*/

#include "pch.h"
#include "isochartmesh.h"
#include "maxheap.hpp"

// VECTOR field selector
// v can be a XMFLOAT2 or XMFLOAT3 variable
// axis = XAxis, return v.x
// axis = YAxis, return v.y
// axis = ZAxis, return v.z
#define VECTOR_ITEM(v, axis) (((float*)(v))[axis])
#define VECTOR_CHANGE_ITEM(v1, v2, axis, op, value) \
    {(((float*)(v1))[axis]) = (((float*)(v2))[axis]) op value;}
#define VECTOR_ITEM_OP(v1, v2, axis, op) \
    (((float*)(v1))[axis] op ((float*)(v2))[axis])

#define CONTROL_SEARCH_BY_STEP_COUNT 1

using namespace Isochart;
using namespace DirectX;

namespace
{
    // To pack using small atlas area as possible, charts will try to rotate
    // CHART_ROTATION_NUMBER angles for a better pose.
    // E.g. CHART_ROTATION_NUMBER = 6 means the chart will choose the best pose
    // from rotation of 0, 60, 120, 180, 240, 300 degrees.

    const size_t CHART_ROTATION_NUMBER = 4;

    // The algorithm moves charts along the tangent direction of atlas borders searching the
    // best position to add new chart. The searching step can be controled by 2 ways:
    //.SEARCH_STEP_LENGTH = 2 means moving 2 pixels each step.
    //.SEARCH_STEP_COUNT = 120 means at most searching 120 steps. 
    // Using CONTROL_SEARCH_BY_STEP_COUNT to swich between these 2 ways.
    const size_t SEARCH_STEP_LENGTH = 2;
    const size_t SEARCH_STEP_COUNT = 120;

    //Based on experiment, when gutter = 2, Width = 512, Height = 512, the space rate of 
    // finial UV-atlas.
    const float STANDARD_UV_SIZE = 512;
    const float STANDARD_GUTTER = 2;

    // Tables for precomputed triangle values. 
    float g_PackingCosTable[CHART_ROTATION_NUMBER];
    float g_PackingSinTable[CHART_ROTATION_NUMBER];
}

///////////////////////////////////////////////////////////////////////////
/////////////// Packing Algorithm Structures //////////////////////////////
///////////////////////////////////////////////////////////////////////////

namespace Isochart
{
    // Each chart has one _PackingInfo instance
    struct PACKINGINFO
    {
        // pfVertUV and pStandardUV store temporary coordinates
        XMFLOAT2* pVertUV;

        // The uv coordinates of chart when its left-bottom corner be moved to origin
        XMFLOAT2* pStandardUV;

        XMFLOAT2* pStandardVirtualCorner;

        // chart's widths and heights after rotations
        float fUVWidth[CHART_ROTATION_NUMBER];
        float fUVHeight[CHART_ROTATION_NUMBER];

        VERTEX_ARRAY topBorder[CHART_ROTATION_NUMBER]; // Top border vertices
        VERTEX_ARRAY bottomBorder[CHART_ROTATION_NUMBER]; // Bottom border vertices
        VERTEX_ARRAY leftBorder[CHART_ROTATION_NUMBER]; // Left border vertices
        VERTEX_ARRAY rightBorder[CHART_ROTATION_NUMBER];// Right border vertices

        PACKINGINFO() :
            pVertUV(nullptr),
            pStandardUV(nullptr),
            pStandardVirtualCorner(nullptr)
        {
        }
        ~PACKINGINFO()
        {
            SAFE_DELETE_ARRAY(pVertUV);
            SAFE_DELETE_ARRAY(pStandardUV);
            SAFE_DELETE_ARRAY(pStandardVirtualCorner);
        }
    };

    // Store the information of current atlas.
    struct ATLASINFO
    {
        float fBoxTop; // Current top coordinate of atlas
        float fBoxBottom;// Current bottom coordinate of atlas
        float fBoxLeft;// Current left coordinate of atlas
        float fBoxRight;// Current right coordinate of atlas

        float fPixelLength; // Length of one pixel
        float fGutter;  // Minimal distance between two charts
        // It has same unit as fPixelLen.
        float fPackedChartArea; // Current Packed chart Area
        float fExpectedAtlasWidth; // The expected width of atlas, the same unit as fPixelLen
        float fWidthHeightRatio; // Ratio of width and height of finial atlas.

        // Atlas top, bottom, left and right borders.
        // When inserting a chart into the atlas, it shouldn't enter the
        // region besieged by the 4 borders.
        VERTEX_ARRAY currentTopBorder;
        VERTEX_ARRAY currentBottomBorder;
        VERTEX_ARRAY currentLeftBorder;
        VERTEX_ARRAY currentRightBorder;

        VERTEX_ARRAY virtualCornerVertices;
    };
}

namespace
{
    // Indicate the location of a vertex against a Border.
    enum VertexLocation
    {
        RightToBorder, // On the right side of a border
        LeftToBorder, // On the left side of a border
        AboveBorder, // On the upside of a border
        BelowBorder, // Under a border
        NotDefined
    };

    // Packing direction means from which direction to add a new chart into atlas.
    const size_t PACKING_DIRECTION_NUMBER = 4;
    enum PackingDirection
    {
        FromRight = 0, // From right side of current atlas, adding a new chart
        FromLeft = 1,  // Form left side of current atlas, adding a new chart
        FromTop = 2,   // From top side of current atlas, adding a new chart
        FromBottom = 3,// Packing chart under current atlas, adding a new chart
    };

    enum Axis
    {
        XAxis = 0,
        YAxis = 1,
    };
}

///////////////////////////////////////////////////////////////////////////
/////////////////////////// Helper functions //////////////////////////////
///////////////////////////////////////////////////////////////////////////
namespace
{
    static HRESULT MergeBorders(
        PackingDirection direction,
        VERTEX_ARRAY& atlasBorder,
        VERTEX_ARRAY& chartBorder);

    static void FreeAditionalVertices(
        ATLASINFO& atlasInfo)
    {
        for (size_t ii=0; ii<atlasInfo.virtualCornerVertices.size(); ii++)
        {
            delete [](atlasInfo.virtualCornerVertices[ii]);
        }
    
        atlasInfo.virtualCornerVertices.clear();
    }

    static HRESULT AddNewCornerVertices(
        ATLASINFO& atlasInfo,
        uint32_t& dwBeginPos,
        ISOCHARTVERTEX* pLeft,
        ISOCHARTVERTEX* pRight,
        ISOCHARTVERTEX* pTop,
        ISOCHARTVERTEX* pBottom)
    {

        auto pAdditional = new (std::nothrow) ISOCHARTVERTEX[4];
        if (!pAdditional)
        {
            return E_OUTOFMEMORY;
        }

        try
        {
            atlasInfo.virtualCornerVertices.push_back(pAdditional);
        }
        catch (std::bad_alloc&)
        {
            delete []pAdditional;
            return E_OUTOFMEMORY;
        }

        // 1. add Left-Top vertices
        pAdditional[0].uv.x = 0;
        pAdditional[0].uv.y = pTop->uv.y - pBottom->uv.y;
        pAdditional[0].dwID = 0;
        pAdditional[0].dwIDInRootMesh = INVALID_VERT_ID;

        // 2. add Right-Top vertices
        pAdditional[1].uv.x = pRight->uv.x - pLeft->uv.x;
        pAdditional[1].uv.y = pTop->uv.y - pBottom->uv.y;
        pAdditional[1].dwID = 1;
        pAdditional[1].dwIDInRootMesh = INVALID_VERT_ID;

        // 3. add Left-Bottom vertices
        pAdditional[2].uv.x = 0;
        pAdditional[2].uv.y = 0;
        pAdditional[2].dwID = 2;
        pAdditional[2].dwIDInRootMesh = INVALID_VERT_ID;

        // 4. add Right-Bottom vertices
        pAdditional[3].uv.x = pRight->uv.x - pLeft->uv.x;
        pAdditional[3].uv.y = 0;
        pAdditional[3].dwID = 3;
        pAdditional[3].dwIDInRootMesh = INVALID_VERT_ID;

        dwBeginPos = static_cast<uint32_t>( atlasInfo.virtualCornerVertices.size() - 1 );
        return S_OK;
    }

    static HRESULT AddBoundingBoxBorder(
        ATLASINFO& atlasInfo,
        PACKINGINFO& packingInfo,
        size_t dwRotationID,
        ISOCHARTVERTEX* pLeft,
        ISOCHARTVERTEX* pRight,
        ISOCHARTVERTEX* pTop,
        ISOCHARTVERTEX* pBottom
        )
    {
        HRESULT hr = S_OK;

        uint32_t dwIdx = INVALID_INDEX;
        FAILURE_RETURN(
            AddNewCornerVertices(
            atlasInfo,
            dwIdx,
            pLeft,
            pRight,
            pTop,
            pBottom));

        if (!packingInfo.pStandardVirtualCorner)
        {
            packingInfo.pStandardVirtualCorner = new (std::nothrow) XMFLOAT2[4];
            if (!packingInfo.pStandardVirtualCorner)
            {
                return E_OUTOFMEMORY;
            }
        }

        ISOCHARTVERTEX* pAdd = atlasInfo.virtualCornerVertices[dwIdx];

        packingInfo.topBorder[dwRotationID].clear();
        packingInfo.bottomBorder[dwRotationID].clear();
        packingInfo.leftBorder[dwRotationID].clear();
        packingInfo.rightBorder[dwRotationID].clear();

        try
        {
            packingInfo.topBorder[dwRotationID].push_back(pAdd + 2);
            packingInfo.topBorder[dwRotationID].push_back(pAdd + 0);
            packingInfo.topBorder[dwRotationID].push_back(pAdd + 1);

            packingInfo.bottomBorder[dwRotationID].push_back(pAdd + 2);
            packingInfo.bottomBorder[dwRotationID].push_back(pAdd + 3);
            packingInfo.bottomBorder[dwRotationID].push_back(pAdd + 1);

            packingInfo.leftBorder[dwRotationID].push_back(pAdd + 2);
            packingInfo.leftBorder[dwRotationID].push_back(pAdd + 0);
            packingInfo.leftBorder[dwRotationID].push_back(pAdd + 1);

            packingInfo.rightBorder[dwRotationID].push_back(pAdd + 2);
            packingInfo.rightBorder[dwRotationID].push_back(pAdd + 3);
            packingInfo.rightBorder[dwRotationID].push_back(pAdd + 1);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    // Decide the step length in searching new positions of a specified chart.
    // The length is in pixel. SEARCH_STEP_COUNT is also considered.
    inline size_t GetSearchStepLength(size_t dwPixelCount)
    {
    #if CONTROL_SEARCH_BY_STEP_COUNT
        return std::max(SEARCH_STEP_LENGTH, dwPixelCount / SEARCH_STEP_COUNT);
    #else
        return SEARCH_STEP_LENGTH;
    #endif
    }

    // Create two extreme vertices
    // One has maximal x, y coordinate
    // One has minimum x, y coordinate
    inline void UpdateMinMaxVertex(
        XMFLOAT2& currentVertex,
        XMFLOAT2& minVec,
        XMFLOAT2& maxVec)
    {
        if (currentVertex.x > maxVec.x)
        {
            maxVec.x = currentVertex.x;
        }
        if (currentVertex.y > maxVec.y)
        {
            maxVec.y = currentVertex.y;
        }
        if (currentVertex.x < minVec.x)
        {
            minVec.x = currentVertex.x;
        }
        if (currentVertex.y < minVec.y)
        {
            minVec.y = currentVertex.y;
        }
    }

    void AdjustCornerBorder(
        ISOCHARTVERTEX* pCorderBorder,
        ISOCHARTVERTEX* pChartVert,
        size_t dwVertNumber)
    {
        XMFLOAT2 minVec(FLT_MAX, FLT_MAX);
        XMFLOAT2 maxVec(-FLT_MAX, -FLT_MAX);
    
        for (size_t ii=0; ii<dwVertNumber; ii++)
        {
            UpdateMinMaxVertex(pChartVert[ii].uv, minVec, maxVec);
        }

         pCorderBorder[0].uv.x= minVec.x;
         pCorderBorder[0].uv.y= maxVec.y;

         pCorderBorder[1].uv.x= maxVec.x;
         pCorderBorder[1].uv.y= maxVec.y;

         pCorderBorder[2].uv.x= minVec.x;
         pCorderBorder[2].uv.y= minVec.y;

         pCorderBorder[3].uv.x= maxVec.x;
         pCorderBorder[3].uv.y= minVec.y;
    }


    // The following 3 functions are grouped.
    // (1) FindCoreespondSegmentsOfBorders
    // (2) FindVertexRangeStartOnBorder
    // (3) FindVertexRangeEndOnBorder

    // Given 2 borders, find the corresponding segments along tangent direction
    // on the 2 borders, which needs to compute the relative radial location.
    // For example:
    //   Border1 and Border2 are aligned horizontally. so tangent direction is along X axis
    //   Border1 includes 3 vertices, the x coordinates are 0, 2, 4
    //   Border2 includes 3 vertices, the x coordinates are 3, 5,7
    //   So, the correspond segment on Border1 include vertices with X coordinates 2,4. the
    //   correspond segment on Border2 include vertices with X coordinates 3, 5

    // Binary searching to find a vertex on the border. The vertex's coordinate in TangentAxis
    // is largest in all vertices whose coordinates in TangentAxis are smaller than target.
    inline size_t static FindVertexRangeStartOnBorder(
        VERTEX_ARRAY& aBorder,
        float target,
        Axis TangentAxis)
    {
        size_t dwBorderSize = aBorder.size();
        size_t dwBorderStart = 0;

        size_t low = 0;
        size_t hi = dwBorderSize - 1;

        do{
            dwBorderStart = (low + hi) >> 1;
            if ( VECTOR_ITEM(&aBorder[dwBorderStart]->uv, TangentAxis)
                == target )
            {
                while ( dwBorderStart > 0 &&
                    VECTOR_ITEM(&aBorder[dwBorderStart-1]->uv, TangentAxis)
                    == target)
                {
                    dwBorderStart--;
                }
                break;
            }

            if (VECTOR_ITEM(&aBorder[dwBorderStart]->uv, TangentAxis)
                < target)
            {
                low = dwBorderStart+1;
            }
            else
            {
                if (dwBorderStart == 0)
                {
                    break;
                }
                hi = dwBorderStart-1;
            }
        }while (low <= hi);

        if (low > hi)
        {
            assert(
                VECTOR_ITEM(&aBorder[low]->uv, TangentAxis)
                >=
                VECTOR_ITEM(&aBorder[hi]->uv, TangentAxis));

            dwBorderStart = hi;
        }
        return dwBorderStart;
    }

    // Searching along the border, finding a vertex whose coordinate in TangentAxis is the 
    // smallest in all vertices whose coordinate in TangentAxis is larger than target.
    inline size_t static FindVertexRangeEndOnBorder(
        VERTEX_ARRAY& aBorder,
        size_t dwBorderStart,
        float target,
        Axis TangentAxis)
    {
        size_t dwBorderEnd = dwBorderStart;
        size_t dwBorderSize = aBorder.size();

        while (dwBorderEnd < dwBorderSize &&
            VECTOR_ITEM(&aBorder[dwBorderEnd]->uv, TangentAxis)
            <=  target )
        {
            dwBorderEnd++;
        }

        if (dwBorderEnd == dwBorderSize)
        {
            dwBorderEnd -= 1;
        }

        return dwBorderEnd;
    }

    // 
    _forceinline static bool FindCorrespondSegmentsOfBorders(
        VERTEX_ARRAY& aBorder1,
        VERTEX_ARRAY& aBorder2,
        size_t& dwBorder1Start,
        size_t& dwBorder1End,
        size_t& dwBorder2Start,
        size_t& dwBorder2End,
        Axis TangentAxis)
    {
        dwBorder1Start = dwBorder1End = 0;
        dwBorder2Start = dwBorder2End = 0;

        size_t BorderSize1 = aBorder1.size();
        size_t BorderSize2 = aBorder2.size();

        assert(BorderSize1 > 0);
        assert(BorderSize2 > 0);
    
        // 1.  If 2 Borders have no correspond segments, return directly.
        if (VECTOR_ITEM(&aBorder1[0]->uv, TangentAxis) >
            VECTOR_ITEM(&aBorder2[BorderSize2-1]->uv, TangentAxis)
        || VECTOR_ITEM(&aBorder1[BorderSize1-1]->uv, TangentAxis)
        < VECTOR_ITEM(&aBorder2[0]->uv, TangentAxis))
        {
            return false;
        }

        // 2. Calculate the correspond begin vertices of 2 borders.
        if (VECTOR_ITEM(&aBorder1[0]->uv, TangentAxis) >=
            VECTOR_ITEM(&aBorder2[0]->uv, TangentAxis) )
        {
            dwBorder1Start = 0;
            dwBorder2Start =
                FindVertexRangeStartOnBorder(
                    aBorder2,
                    VECTOR_ITEM(&aBorder1[0]->uv, TangentAxis),
                    TangentAxis);
        }
        else
        {
            dwBorder2Start = 0;
            dwBorder1Start =
                FindVertexRangeStartOnBorder(
                    aBorder1,
                    VECTOR_ITEM(&aBorder2[0]->uv, TangentAxis),
                    TangentAxis);
        }

        // 3. Calculate the correspond end vertices of 2 borders.
        if (VECTOR_ITEM(&aBorder1[BorderSize1-1]->uv, TangentAxis)
            <= VECTOR_ITEM(&aBorder2[BorderSize2-1]->uv, TangentAxis))
        {
            dwBorder1End = BorderSize1-1;
            dwBorder2End =
                FindVertexRangeEndOnBorder(
                    aBorder2,
                    dwBorder2Start,
                    VECTOR_ITEM(&aBorder1[BorderSize1-1]->uv, TangentAxis),
                    TangentAxis);
        }
        else
        {
            dwBorder2End = BorderSize2-1;
            dwBorder1End =
                FindVertexRangeEndOnBorder(
                    aBorder1,
                    dwBorder1Start,
                    VECTOR_ITEM(&aBorder2[BorderSize2-1]->uv, TangentAxis),
                    TangentAxis);
        }
        return true;
    }


    // 1. Judge that the vertex is on which side of the border.
    // 2. Calculate the distance from the vertex to the border.
    _forceinline static VertexLocation CalculateVertexLocationToBorder(
        VERTEX_ARRAY& aBorder, // a vertical Border.
        size_t dwBorderStart,
        size_t dwBorderEnd,
        XMFLOAT2& point,// a vertex
        float fGutter, // the min distance between two sub-chart
        float& fDistance, // the distance from the vertex to the Border.
        Axis TangentAxis)
    {
        VertexLocation higherPosition;
        VertexLocation lowerPosition;

        Axis RadialAxis;
        if (XAxis == TangentAxis) 
        {
            higherPosition = AboveBorder;
            lowerPosition = BelowBorder;
            RadialAxis = YAxis;
        }
        else
        {
            higherPosition = RightToBorder;
            lowerPosition = LeftToBorder;
            RadialAxis = XAxis;
        }

        fDistance = FLT_MAX;

        // 1. Find correspond segment along scan direction
        size_t i;
        for (i=dwBorderStart; i<dwBorderEnd+1; i++)
        {
            if (VECTOR_ITEM(&point, TangentAxis) <
                VECTOR_ITEM(&aBorder[i]->uv, TangentAxis) )
            {
                break;
            }
        }

        // No correspond segment.
        if (dwBorderStart == i)
        {
            return NotDefined;
        }

        // fIntersection stores intersection between the border and beeline
        // crossing the vertex along axises TangentAxis and RadialAxis
        float fIntersection = 0;
        float fExtraDistance = 0;

        // 2. No corresponding segment or some vertices at the end of the border have
        // equal value with vertex in scan direction.
        if (i == dwBorderEnd+1)
        {
            float fMax = -FLT_MAX;
            float fMin = FLT_MAX;
            for (size_t j=0; j<=dwBorderEnd; j++)
            {
                if (IsInZeroRange(
                    VECTOR_ITEM(&point, TangentAxis) -
                    VECTOR_ITEM(&aBorder[j]->uv, TangentAxis) ))
                {
                    if (fMax < VECTOR_ITEM(&aBorder[j]->uv, RadialAxis))
                    {
                        fMax = VECTOR_ITEM(&aBorder[j]->uv, RadialAxis);
                    }
                    if (fMin > VECTOR_ITEM(&aBorder[j]->uv, RadialAxis))
                    {
                        fMin = VECTOR_ITEM(&aBorder[j]->uv, RadialAxis);
                    }
                }
            }

            if (fMax < fMin)
            {
                return NotDefined;
            }

            if (VECTOR_ITEM(&point, RadialAxis) > fMax)
            {
                fIntersection = fMax;
            }
            else if (VECTOR_ITEM(&point, RadialAxis) < fMin)
            {
                fIntersection = fMin;
            }
            else
            {
                return NotDefined;
            }

            fExtraDistance = fGutter;
        }
        // 3. Has corresponding segment
        else
        {
            XMVECTOR vBiasVector = XMLoadFloat2(&(aBorder[i]->uv)) - XMLoadFloat2(&(aBorder[i - 1]->uv));
            XMFLOAT2 biasVector;
            XMStoreFloat2(&biasVector, vBiasVector);

            // If the beeline crossing the vertex also crosses the aBorder[i]->uv, and
            // aBorder[i-1]->uv, then, easy to decide the intersection
            if (IsInZeroRange(VECTOR_ITEM(&biasVector, TangentAxis)))
            {
                float fMin, fMax;
                if (VECTOR_ITEM(&aBorder[i]->uv, RadialAxis)
                > VECTOR_ITEM(&aBorder[i-1]->uv, RadialAxis))
                {
                    fMax = VECTOR_ITEM(&aBorder[i]->uv, RadialAxis);
                    fMin = VECTOR_ITEM(&aBorder[i-1]->uv, RadialAxis);
                }
                else
                {
                    fMax = VECTOR_ITEM(&aBorder[i-1]->uv, RadialAxis);
                    fMin = VECTOR_ITEM(&aBorder[i]->uv, RadialAxis);
                }

                if (VECTOR_ITEM(&point, RadialAxis) > fMax)
                {
                    fIntersection = fMax;
                }
                else if (VECTOR_ITEM(&point, TangentAxis) < fMin)
                {
                    fIntersection = fMin;
                }
                else
                {
                    return NotDefined;
                }
                fExtraDistance = fGutter;
            }
            else
            {
                // (y-y[i-1]) / (y[i]-y[i-1]) = (x-x[i-1]) / (x[i]-x[i-1])
                fIntersection =
                    VECTOR_ITEM(&aBorder[i-1]->uv, RadialAxis)
                    + VECTOR_ITEM(&biasVector, RadialAxis)
                    * ( VECTOR_ITEM(&point, TangentAxis) -
                    VECTOR_ITEM(&aBorder[i-1]->uv, TangentAxis))
                    / VECTOR_ITEM(&biasVector, TangentAxis);

                fExtraDistance =
                    fGutter * fabsf(XMVectorGetX(XMVector2Length(vBiasVector)) /
                    VECTOR_ITEM(&biasVector, TangentAxis));
            }
        }

        fDistance = fIntersection - VECTOR_ITEM(&point, RadialAxis);
        if (fDistance < 0)
        {
            fDistance = -fDistance;
        }
        fDistance -= fExtraDistance;

        if (fIntersection < VECTOR_ITEM(&point, RadialAxis))
        {
            return higherPosition;
        }
        else if (fIntersection> VECTOR_ITEM(&point, RadialAxis))
        {
            return lowerPosition;
        }
        else
        {
            return NotDefined;
        }
    }

    ///////////////////////////////////////////////////////////////////////////
    ///////////////////////// Internal methods ////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////

    // Rotate vertex in clockwise direction, Because we only care the relative position of each
    // vertex, no need to add center back.
    inline static void RotateVertexAroundCenter(
        XMFLOAT2& vertexOut,
        const XMFLOAT2& vertexIn,
        float fcenterX,
        float fcenterY,
        float fSin,
        float fCos)
    {
        float fx = vertexIn.x - fcenterX;
        float fy = vertexIn.y - fcenterY;
        vertexOut.x = fx * fCos - fy * fSin;
        vertexOut.y = fx * fSin + fy * fCos;
    }

    // If borde 1 and border 2 connect together at one end, this function
    // check if border 2 is on the clockwise direction of border 1. After finding to borders,
    // this function is used to judge the role of these 2 borders.
    static bool IsB2OnClockwiseDirOfB1AtBegin(
        VERTEX_ARRAY& border1,
        VERTEX_ARRAY& border2,
        bool& bIsDecided,
        float& fDotValue)
    {
        assert(border1[0] == border2[0]);
        assert(border1.size() > 1);
        assert(border2.size() > 1);
        if (border1.size() <=1 || border2.size() <=1)
        {
            return false;	
        }

        bIsDecided = true;
        size_t i = 1;
        size_t j = 1;
        float fZ = 0;

        ISOCHARTVERTEX* pOrigin = border1[0];
        do
        {
            ISOCHARTVERTEX* pVertex1 = border1[i];
            ISOCHARTVERTEX* pVertex2 = border2[j];

            XMVECTOR vv1 = XMVectorSet(
                pVertex1->uv.x - pOrigin->uv.x,
                pVertex1->uv.y - pOrigin->uv.y,
                0, 0);

            XMVECTOR vv2 = XMVectorSet(
                pVertex2->uv.x - pOrigin->uv.x,
                pVertex2->uv.y - pOrigin->uv.y,
                0, 0); 

            float f1 = XMVectorGetX(XMVector3LengthSq(vv1));
            float f2 = XMVectorGetX(XMVector3LengthSq(vv2));

            if (IsInZeroRange(f1) || IsInZeroRange(f2))
            {
                fZ = 0;
                fDotValue = 1;
            }
            else
            {
                vv1 = XMVector3Normalize(vv1);
                vv2 = XMVector3Normalize(vv2);
                fDotValue = XMVectorGetX(XMVector3Dot(vv1, vv2));
                XMFLOAT3 v1, v2;
                XMStoreFloat3(&v1, vv1);
                XMStoreFloat3(&v2, vv2);
        
                if (IsInZeroRange(v1.x) && IsInZeroRange(v2.x) 
                    && fabs(v1.y) > 0.1f && fabs(v2.y) > 0.1f
                    && v1.y * v2.y < 0)
                {
                    if (v1.y > v2.y)
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                if (IsInZeroRange(v1.y) && IsInZeroRange(v2.y) 
                    && fabs(v1.x) > 0.1f && fabs(v2.x) > 0.1f
                    && v1.x * v2.x < 0)
                {
                    if (v1.x < v2.x)
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                fZ = CalculateZOfVec3Cross(&v1, &v2);
            }
             // pOrigin, pVertex1, pVertex2 in the same line,
             // need to move origin forward, and Calculate again
             if (fabs(fZ) < ISOCHART_ZERO_EPS)
             {
                if (f1 < f2)
                {
                    pOrigin = border1[i];
                    i++;
                }
                else if (f1 > f2)
                {
                    pOrigin = border2[j];
                     j++;
                }
                else
                {
                    i++;
                    j++;
                    if (i>=border1.size() || j>= border2.size())
                    {
                        fZ = 0;
                        break;
                    }
                    pOrigin = border1[i];
                }
             }
             else
             {
                break;
             }
        }while (i < border1.size() && j < border2.size());

        if (fZ > ISOCHART_ZERO_EPS)
        {
            return false;
        }
        else if (fZ < -ISOCHART_ZERO_EPS)
        {
            return true;
        }
        else
        {
            bIsDecided = false;
            return true;
        }
    }


    static bool IsB1OnClockwiseDirOfB2AtEnd(
        VERTEX_ARRAY& border1,
        VERTEX_ARRAY& border2,
        bool& bIsDecided,
        float& fDotValue)
    {
        assert(border1[border1.size()-1] ==
            border2[border2.size()-1]);

        assert(border1.size() > 1);
        assert(border2.size() > 1);

        if (border1.size() <=1 || border2.size() <=1)
        {
            return false;	
        }

        bIsDecided = true;
        size_t i = border1.size() - 2;
        size_t j = border2.size() - 2;
        float fZ = 0;
        ISOCHARTVERTEX* pOrigin = border1[border1.size()-1];
        for(;;)
        {
            ISOCHARTVERTEX* pVertex1 = border1[i];
            ISOCHARTVERTEX* pVertex2 = border2[j];

            XMVECTOR vv1 = XMVectorSet(
                pVertex1->uv.x - pOrigin->uv.x,
                pVertex1->uv.y - pOrigin->uv.y,
                0, 0);

            XMVECTOR vv2 = XMVectorSet(
                pVertex2->uv.x - pOrigin->uv.x,
                pVertex2->uv.y - pOrigin->uv.y,
                0, 0); 

            float f1 = XMVectorGetX(XMVector3LengthSq(vv1));
            float f2 = XMVectorGetX(XMVector3LengthSq(vv2));

            if (IsInZeroRange(f1) || IsInZeroRange(f2))
            {
                fZ = 0;
                fDotValue = 1;
            }
            else
            {
                vv1 = XMVector3Normalize(vv1);
                vv2 = XMVector3Normalize(vv2);
                fDotValue = XMVectorGetX(XMVector3Dot(vv1, vv2));
                XMFLOAT3 v1, v2;
                XMStoreFloat3(&v1, vv1);
                XMStoreFloat3(&v2, vv2);

                if (IsInZeroRange(v1.x) && IsInZeroRange(v2.x)
                    && fabs(v1.y) > 0.1f && fabs(v2.y) > 0.1f
                    && v1.y * v2.y < 0)
                {
                    if (v1.y > v2.y)
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                if (IsInZeroRange(v1.y) && IsInZeroRange(v2.y)
                    && fabs(v1.x) > 0.1f && fabs(v2.x) > 0.1f
                    && v1.x * v2.x < 0)
                {
                    if (v1.x < v2.x)
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                fZ = CalculateZOfVec3Cross(&v1, &v2);
            }
            // pOrigin, pVertex1, pVertex2 in the same line,
            // need to move origin forward, and Calculate again
            if (fabs(fZ) < ISOCHART_ZERO_EPS)
            {
                if (f1 < f2)
                {
                    pOrigin = border1[i];
                    if (i > 0) i--;
                    else break;
                }
                else if (f1 > f2)
                {
                    pOrigin = border2[j];
                    if (j > 0) j--;
                    else break;
                }
                else
                {
                    if (i > 0) i--;
                    else break;
          
                    if (j > 0) j--;
                    else break;
                    pOrigin = border1[i];
                }
            }
            else
            {
                break;
            }
        }

        if (fZ > ISOCHART_ZERO_EPS)
        {
            return false;
        }
        else if (fZ < -ISOCHART_ZERO_EPS)
        {
            return true;
        }
        else
        {
            bIsDecided = false;
            return true;
        }
    }

    // Inverse angle in border can produce redundant vertex, remove
    // these vertex. for example, if following border is a top bonder,
    // remove B, if it's a bottom border, remove A.
    //  ----------------- A
    //                  /
    //                 /-----------
    //                B
    static HRESULT RemoveRedundantVerticesInBorders(
        bool bHorizontal,
        bool bLowerBoder,
        VERTEX_ARRAY& border)
    {
        HRESULT hr = S_OK;

        Axis TangentAxis;
        Axis RadialAxis;

        PackingDirection direction;
        if (bHorizontal)
        {
            TangentAxis = XAxis;
            RadialAxis =  YAxis;

        direction = bLowerBoder?FromBottom:FromTop;
        }
        else
        {
            TangentAxis = YAxis;
            RadialAxis =  XAxis;

        direction = bLowerBoder?FromLeft:FromRight;
        }
    
        VERTEX_ARRAY increaseSegement;
        VERTEX_ARRAY backBorder;
    
        size_t ii = 0;
        try
        {
            backBorder.insert(backBorder.end(), border.cbegin(), border.cend());
            border.clear();

            while (ii < backBorder.size())
            {
                increaseSegement.clear();
                increaseSegement.push_back(backBorder[ii]);
                ii++;

                float t1 = VECTOR_ITEM(&backBorder[ii - 1]->uv, TangentAxis);
                float t2 = VECTOR_ITEM(&backBorder[ii]->uv, TangentAxis);
                while (t1 <= t2 && ii < backBorder.size())
                {
                    increaseSegement.push_back(backBorder[ii]);

                    ii++;
                    if (ii == backBorder.size())
                    {
                        break;
                    }
                    t1 = t2;
                    t2 = VECTOR_ITEM(&backBorder[ii]->uv, TangentAxis);
                }
                if (border.empty())
                {
                    border.insert(border.end(), increaseSegement.cbegin(), increaseSegement.cend());
                }
                else
                {
                    FAILURE_RETURN(
                        MergeBorders(
                        direction,
                        border,
                        increaseSegement));
                }
                if (ii == backBorder.size())
                {
                    break;
                }

                assert(ii + 1 < backBorder.size());
                t1 = t2;
                t2 = VECTOR_ITEM(&backBorder[ii + 1]->uv, TangentAxis);
                while (t2 <= t1)
                {
                    ii++;
                    assert(ii + 1 < backBorder.size());
                    t1 = t2;
                    t2 = VECTOR_ITEM(&backBorder[ii + 1]->uv, TangentAxis);
                }
            }
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        ii = 1;
        while (ii+1 < border.size())
        {
            float t1 = VECTOR_ITEM(&border[ii-1]->uv, TangentAxis);
            float t2 = VECTOR_ITEM(&border[ii]->uv, TangentAxis);
            float t3 = VECTOR_ITEM(&border[ii+1]->uv, TangentAxis);

            float r1 = VECTOR_ITEM(&border[ii-1]->uv, RadialAxis);
            float r2 = VECTOR_ITEM(&border[ii]->uv, RadialAxis);
            float r3 = VECTOR_ITEM(&border[ii+1]->uv, RadialAxis);

            if (fabs(t1-t2) < ISOCHART_ZERO_EPS
            && fabs(t3-t2) < ISOCHART_ZERO_EPS)
            {
                if((r1 >= r2 && r2 >= r3) || (r1 <= r2 && r2 <= r3))
                {
                    border.erase(border.begin() + ii);
                }
                else
                {
                    ii++;
                }
            }
            else
            {
                ii++;
            }
        }

        return S_OK;
    }

    // When adding a chart to atlas, moving it from origin to a candidate iposition.
    _forceinline static void MoveChartToNewPosition(
        VERTEX_ARRAY& newChartBorder,
        const XMFLOAT2* pOrigUV,
           Axis TangentAxis,
           Axis RadialAxis,
           float fTangentDelta,
           float fRadialDelta,
           float fGutter)
    {
        size_t dwNewChartBorderSize = newChartBorder.size();
        for (size_t k = 1; k<dwNewChartBorderSize - 1; k++)
        {
            VECTOR_CHANGE_ITEM(
                &newChartBorder[k]->uv,
                &pOrigUV[newChartBorder[k]->dwID],
                TangentAxis,
                +,
                fTangentDelta);

            VECTOR_CHANGE_ITEM(
                &newChartBorder[k]->uv,
                &pOrigUV[newChartBorder[k]->dwID],
                RadialAxis,
                +,
                fRadialDelta);
        }

       // Two additional vertices on each end of the border. These vertices used to guarantee
       // gutter between charts.
        newChartBorder[0]->uv = newChartBorder[1]->uv;
        newChartBorder[dwNewChartBorderSize-1]->uv
            = newChartBorder[dwNewChartBorderSize-2]->uv;

        VECTOR_CHANGE_ITEM(
            &newChartBorder[0]->uv,
            &newChartBorder[1]->uv,
            TangentAxis,
            -,
            fGutter);

        VECTOR_CHANGE_ITEM(
            &newChartBorder[dwNewChartBorderSize-1]->uv,
            &newChartBorder[dwNewChartBorderSize-2]->uv,
            TangentAxis,
            +,
            fGutter);
    }

    _forceinline static bool CalMinDistanceBetweenAtlasAndChart(
        VertexLocation invalidatlasLocationAgainstChart,
        VertexLocation invalidChartLocationAgainstAtlas,
        bool bPackingFromLowerPlace,
        VERTEX_ARRAY& newChartBorder,
        size_t newChartBorderStart,
        size_t newChartBorderEnd,
        VERTEX_ARRAY& atlasBorder,
        size_t atlasBorderStart,
        size_t atlasBorderEnd,
        Axis TangentAxis,
        Axis RadialAxis,
        float fGutter,
        float& fMinDistance,
        float& fBetweenArea)
    {
       float fDistance;
       VertexLocation location;
       size_t ii = atlasBorderStart;
       size_t jj = newChartBorderStart;
       while(ii<=atlasBorderEnd && jj<=newChartBorderEnd)
       {
            float tangent1 = VECTOR_ITEM(&atlasBorder[ii]->uv, TangentAxis);
            float tangent2 = VECTOR_ITEM(&newChartBorder[jj]->uv, TangentAxis);

            if (tangent1 < tangent2)
            {
                location = CalculateVertexLocationToBorder(
                        newChartBorder,
                        newChartBorderStart,
                        newChartBorderEnd,
                        atlasBorder[ii]->uv,
                        fGutter,
                        fDistance,
                        TangentAxis);

                if (location == invalidatlasLocationAgainstChart)
                {
                    return false;
                }

                if (location != NotDefined)
                {
                    fBetweenArea += fDistance;
                }

                atlasBorderStart = ii;
                ii++;
            }
            else if (tangent1 > tangent2)
            {
                location = CalculateVertexLocationToBorder(
                        atlasBorder,
                        atlasBorderStart,
                        atlasBorderEnd,
                        newChartBorder[jj]->uv,
                        fGutter,
                        fDistance,
                        TangentAxis);
                if (location == invalidChartLocationAgainstAtlas)
                {
                    return false;
                }
                newChartBorderStart = jj;
                jj++;
            }
            else
            {
                float fRadia1 = VECTOR_ITEM(&atlasBorder[ii]->uv, RadialAxis);
                float fRadia2 = VECTOR_ITEM(&newChartBorder[jj]->uv, RadialAxis);

                if (bPackingFromLowerPlace)
                {
                    fDistance = fRadia1 - fRadia2 - fGutter;
                }
                else
                {
                    fDistance = fRadia2 - fRadia1 - fGutter;
                }
                fBetweenArea += fDistance;
                atlasBorderStart = ii;
                newChartBorderStart = jj;
                ii++;
                jj++;
            }

            if (fDistance < 0)
            {
                return false;
            }

            if (fMinDistance > fDistance)
            {
                fMinDistance = fDistance;
            }
        }

        return true;
    }

    _forceinline static void UpdateOptimalPosition(
        bool bPackingFromLowerPlace,
        ATLASINFO& atlasInfo,
        VERTEX_ARRAY& atlasBorder,
        float fAtlasNearChartExtreme,
        float fAtlasAwayChartExtreme,
        float fAtlasTangentMaxExtreme,
        float fAtlasTangentMinExtreme,
        Axis TangentAxis,
        Axis RadialAxis,
        float fChartTangentSize,
        float fChartRadialSize,
        float fTangentDelta,
        float fRadialDelta,
        float fMinDistance,
        float fBetweenArea,
        XMFLOAT2& resultOrg,
        float& fMinAreaLost,
        float& fMiniBetweenArea)
    {
        float fRealRadialDelta = fRadialDelta;
        float fNewAtlasRadialExtreme;
        if (bPackingFromLowerPlace)
        {
            fRealRadialDelta += fMinDistance;
            fNewAtlasRadialExtreme = fRealRadialDelta;
            if (fNewAtlasRadialExtreme > fAtlasNearChartExtreme)
            {
                fNewAtlasRadialExtreme = fAtlasNearChartExtreme;
            }
        }
        else
        {
            fRealRadialDelta -= fMinDistance;
            fNewAtlasRadialExtreme = fRealRadialDelta + fChartRadialSize;
            if (fNewAtlasRadialExtreme < fAtlasNearChartExtreme)
            {
                fNewAtlasRadialExtreme = fAtlasNearChartExtreme;
            }
        }

        // Calculate atlas size and area lost rate after packing current chart
        float fRadialSize =
        fNewAtlasRadialExtreme - fAtlasAwayChartExtreme;
        if (fRadialSize < 0)
        {
            fRadialSize = -fRadialSize;
        }

        float fNewAtlasTangentExtreme = fTangentDelta + fChartTangentSize;
        if (fNewAtlasTangentExtreme < fAtlasTangentMaxExtreme)
        {
            fNewAtlasTangentExtreme = fAtlasTangentMaxExtreme;
        }

        float fTangentSize =
            fNewAtlasTangentExtreme - fAtlasTangentMinExtreme;

        fBetweenArea -= atlasBorder.size() * fMinDistance;
        float fAreaLost = 1 - atlasInfo.fPackedChartArea /(fRadialSize * fTangentSize);

        // Record the minimal area lost
        if (IsInZeroRange(fAreaLost - fMinAreaLost))
        {
            if (fBetweenArea < fMiniBetweenArea)
            {
                fMiniBetweenArea = fBetweenArea;
                fMinAreaLost = fAreaLost;
                VECTOR_ITEM(&resultOrg, TangentAxis) = fTangentDelta;
                VECTOR_ITEM(&resultOrg, RadialAxis) = fRealRadialDelta;
            }
        }
        else if (fAreaLost < fMinAreaLost)
        {
            fMiniBetweenArea = fBetweenArea;
            fMinAreaLost = fAreaLost;
            VECTOR_ITEM(&resultOrg, TangentAxis) = fTangentDelta;
            VECTOR_ITEM(&resultOrg, RadialAxis) = fRealRadialDelta;
        }
    }

    // Find chart packing position from a special direction.
    inline static HRESULT FindChartPosition(
        PackingDirection direction,
        ATLASINFO& atlasInfo,
        PACKINGINFO* pPackingInfo,
        size_t dwRotationID,
        XMFLOAT2& resultOrg,
        float &fBetweenArea,
        float &fAreaLost)
    {
        VERTEX_ARRAY* pAtlasBorder = nullptr;
        VERTEX_ARRAY* pChartBorder = nullptr;

        Axis TangentAxis = YAxis;
        Axis RadialAxis = XAxis;

        // The radial coordinate of the border vertex in atlas, which is nearest to the new chart
        float fAtlasNearChartExtreme = 0;
        // The radial coordinate of the border vertex in atlas, which is farest to the new chart
        float fAtlasAwayChartExtreme = 0;
        // The largest tangent coordinate of the border vertex in atlas.
        float fAtlasTangentMaxExtreme = 0;
         // The smallest tangent coordinate of the border vertex in atlas.  
        float fAtlasTangentMinExtreme = 0;

        float fChartTangentSize = 0;
        float fChartRadialSize = 0;
        bool bPackingFromLowerPlace = false;

        VertexLocation invalidChartLocationAgainstAtlas;
        VertexLocation invalidatlasLocationAgainstChart;

        switch (direction)
        {
        case FromRight:
            pAtlasBorder = &(atlasInfo.currentRightBorder);
            pChartBorder = &(pPackingInfo->leftBorder[dwRotationID]);
            fAtlasNearChartExtreme = atlasInfo.fBoxRight;
            fAtlasAwayChartExtreme = atlasInfo.fBoxLeft;
            fAtlasTangentMaxExtreme = atlasInfo.fBoxTop;
            fAtlasTangentMinExtreme = atlasInfo.fBoxBottom;
            fChartTangentSize = pPackingInfo->fUVHeight[dwRotationID];
            fChartRadialSize = pPackingInfo->fUVWidth[dwRotationID];
            invalidChartLocationAgainstAtlas = LeftToBorder;
            invalidatlasLocationAgainstChart = RightToBorder;
            break;

        case FromLeft:
            pAtlasBorder = &(atlasInfo.currentLeftBorder);
            pChartBorder = &(pPackingInfo->rightBorder[dwRotationID]);
            fAtlasNearChartExtreme = atlasInfo.fBoxLeft;
            fAtlasAwayChartExtreme = atlasInfo.fBoxRight;
            fAtlasTangentMaxExtreme = atlasInfo.fBoxTop;
            fAtlasTangentMinExtreme = atlasInfo.fBoxBottom;
            fChartTangentSize = pPackingInfo->fUVHeight[dwRotationID];
            fChartRadialSize = pPackingInfo->fUVWidth[dwRotationID];
            bPackingFromLowerPlace = true;
            invalidChartLocationAgainstAtlas = RightToBorder;
            invalidatlasLocationAgainstChart = LeftToBorder;
            break;

        case FromTop:
            pAtlasBorder = &(atlasInfo.currentTopBorder);
            pChartBorder = &(pPackingInfo->bottomBorder[dwRotationID]);
            TangentAxis = XAxis; // x field
            RadialAxis = YAxis;
            fAtlasNearChartExtreme = atlasInfo.fBoxTop;
            fAtlasAwayChartExtreme = atlasInfo.fBoxBottom;
            fAtlasTangentMaxExtreme = atlasInfo.fBoxRight;
            fAtlasTangentMinExtreme = atlasInfo.fBoxLeft;
            fChartTangentSize = pPackingInfo->fUVWidth[dwRotationID];
            fChartRadialSize = pPackingInfo->fUVHeight[dwRotationID];
            invalidChartLocationAgainstAtlas = BelowBorder;
            invalidatlasLocationAgainstChart = AboveBorder;
            break;

        case FromBottom:
            pAtlasBorder = &(atlasInfo.currentBottomBorder);
            pChartBorder = &(pPackingInfo->topBorder[dwRotationID]);
            TangentAxis = XAxis; // x field
            RadialAxis = YAxis;
            fAtlasNearChartExtreme = atlasInfo.fBoxBottom;
            fAtlasAwayChartExtreme = atlasInfo.fBoxTop;
            fAtlasTangentMaxExtreme = atlasInfo.fBoxRight;
            fAtlasTangentMinExtreme = atlasInfo.fBoxLeft;
            fChartTangentSize = pPackingInfo->fUVWidth[dwRotationID];
            fChartRadialSize = pPackingInfo->fUVHeight[dwRotationID];
            bPackingFromLowerPlace = true;
            invalidChartLocationAgainstAtlas = AboveBorder;
            invalidatlasLocationAgainstChart = BelowBorder;
            break;

        default:
            assert(false);
            return E_FAIL;
        }

        VERTEX_ARRAY& atlasBorder = *pAtlasBorder;
        VERTEX_ARRAY& chartBorder = *pChartBorder;

        XMFLOAT2* pOrigUV = pPackingInfo->pStandardUV;
        // Border is composite with virtual vertices
        if (chartBorder[0]->dwIDInRootMesh == INVALID_VERT_ID)
        {
            pOrigUV = pPackingInfo->pStandardVirtualCorner;
        }

        float fMinAreaLost = FLT_MAX;
        float fMiniBetweenArea = FLT_MAX;
        fAreaLost = FLT_MAX;

        float fMinTangentPosition = VECTOR_ITEM(&atlasBorder[0]->uv, TangentAxis);

        // Decide the searching step length
        float fTangentRange =
            VECTOR_ITEM(&atlasBorder[atlasBorder.size()-1]->uv, TangentAxis) -
            VECTOR_ITEM(&atlasBorder[0]->uv, TangentAxis) -fChartTangentSize;

        size_t dwTangentLenInPixel;
        if (fTangentRange < 0)
        {
            dwTangentLenInPixel = 1;
        }
        else
        {
            dwTangentLenInPixel =
                static_cast<size_t>(fTangentRange / atlasInfo.fPixelLength) + 1;
        }

        size_t dwStepLength = GetSearchStepLength(dwTangentLenInPixel);
        ISOCHARTVERTEX startExtraVertex, endExtraVertex;

        // To guarantee enough gutter between different charts, add 2 extra vertex
        // at each end of current chart border.
        VERTEX_ARRAY newChartBorder;

        try
        {
            newChartBorder.push_back(&startExtraVertex);
            newChartBorder.insert(newChartBorder.end(), chartBorder.cbegin(), chartBorder.cend());
            newChartBorder.push_back(&endExtraVertex);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        // Put current chart far away atlas, by fRadialDelta offset
        float fRadialDelta;
        if (bPackingFromLowerPlace)
        {
            fRadialDelta =
                fAtlasNearChartExtreme - fChartRadialSize - 100 * atlasInfo.fGutter;
        }
        else
        {
            fRadialDelta =
                fAtlasNearChartExtreme + fChartRadialSize + 100 * atlasInfo.fGutter;
        }

        // Search the optimal packing position
        for (size_t i=0; i<dwTangentLenInPixel; i+=dwStepLength)
        {
            fBetweenArea = 0;

            // Searching from center to both sides
            float fTangentDelta;
            if (dwTangentLenInPixel > 1)
            {
                fTangentDelta =
                    fMinTangentPosition + ((i+dwTangentLenInPixel/2)%dwTangentLenInPixel)
                    * fTangentRange / (dwTangentLenInPixel-1);
            }
            else
            {
                fTangentDelta= fMinTangentPosition;
            }

            // Move chart to new position
            MoveChartToNewPosition(
                newChartBorder,
                pOrigUV,
                TangentAxis,
                RadialAxis,
                fTangentDelta,
                fRadialDelta,
                atlasInfo.fGutter);

            // Find correspond segments on atlas border and chart border
            size_t atlasBorderStart, atlasBorderEnd;
            size_t newChartBorderStart, newChartBorderEnd;
            if (!FindCorrespondSegmentsOfBorders(
                atlasBorder,
                newChartBorder,
                atlasBorderStart,
                atlasBorderEnd,
                newChartBorderStart,
                newChartBorderEnd,
                TangentAxis))
            {
                continue;
            }

            // Calculate the minimal distance between chart and atlas
            float fMinDistance = FLT_MAX;
         if (!CalMinDistanceBetweenAtlasAndChart(
                invalidatlasLocationAgainstChart,
                invalidChartLocationAgainstAtlas,
                bPackingFromLowerPlace,
                newChartBorder,
                newChartBorderStart,
                newChartBorderEnd,
                atlasBorder,
                atlasBorderStart,
                atlasBorderEnd,
                TangentAxis,
                RadialAxis,
                atlasInfo.fGutter,
                fMinDistance,
                fBetweenArea))
         {
                continue;
         }

            // Move chart from far away position to the real packing position. Check if current
            // position is better than old ones
         UpdateOptimalPosition(
                bPackingFromLowerPlace, 
                atlasInfo,
                atlasBorder,
                fAtlasNearChartExtreme,
                fAtlasAwayChartExtreme,
                fAtlasTangentMaxExtreme,
                fAtlasTangentMinExtreme,
                TangentAxis,
                RadialAxis,
                fChartTangentSize,
                fChartRadialSize,
                fTangentDelta,
                fRadialDelta,
                fMinDistance,
                fBetweenArea,
                resultOrg,  
                fMinAreaLost,
                fMiniBetweenArea);
        }

        fBetweenArea = fMiniBetweenArea;
        fAreaLost = fMinAreaLost;
        return S_OK;
    }

    inline void UpdateAreaLostInfo(
        size_t dwPackingDirection,

        size_t dwDirMinRotationId [],
        size_t dwRotationId,

        XMFLOAT2 dirOrg[],
        XMFLOAT2& currentOrg,

        float fDirMinAreaLost[],
        float fAreaLost,

        float fMinBetweenArea[],
        float fBetweenArea)
    {
        if ((fabsf(fDirMinAreaLost[dwPackingDirection] - fAreaLost)
            < ISOCHART_ZERO_EPS
            && fBetweenArea < fMinBetweenArea[dwPackingDirection])
            || fDirMinAreaLost[dwPackingDirection] > fAreaLost)
        {
            fMinBetweenArea[dwPackingDirection] = fBetweenArea;
            fDirMinAreaLost[dwPackingDirection] = fAreaLost;
            dwDirMinRotationId[dwPackingDirection] = dwRotationId;
            dirOrg[dwPackingDirection] = currentOrg;
        }
    }

    // Initialize atlas
    // It should be called before adding the first chart into empty atlas
    static HRESULT Initializeatlas(
        ATLASINFO& atlasInfo,
        PACKINGINFO& packingInfo,
        size_t dwMinRotationId)
    {
        assert(dwMinRotationId < CHART_ROTATION_NUMBER);
        _Analysis_assume_(dwMinRotationId < CHART_ROTATION_NUMBER);

        try
        {
            atlasInfo.currentTopBorder.insert(atlasInfo.currentTopBorder.end(),
                packingInfo.topBorder[dwMinRotationId].cbegin(), packingInfo.topBorder[dwMinRotationId].cend());

            atlasInfo.currentBottomBorder.insert(atlasInfo.currentBottomBorder.end(),
                packingInfo.bottomBorder[dwMinRotationId].cbegin(), packingInfo.bottomBorder[dwMinRotationId].cend());

            atlasInfo.currentLeftBorder.insert(atlasInfo.currentLeftBorder.end(),
                packingInfo.leftBorder[dwMinRotationId].cbegin(), packingInfo.leftBorder[dwMinRotationId].cend());

            atlasInfo.currentRightBorder.insert(atlasInfo.currentRightBorder.end(),
                packingInfo.rightBorder[dwMinRotationId].cbegin(), packingInfo.rightBorder[dwMinRotationId].cend());
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        atlasInfo.fBoxLeft = 0;
        atlasInfo.fBoxBottom = 0;
        atlasInfo.fBoxTop = packingInfo.fUVHeight[dwMinRotationId];
        atlasInfo.fBoxRight = packingInfo.fUVWidth[dwMinRotationId];
        return S_OK;
    }

    // Merge chart borders to current atlas borders in one direction.
    inline static HRESULT MergeBorders(
        PackingDirection direction,
        VERTEX_ARRAY& atlasBorder,
        VERTEX_ARRAY& chartBorder)
    {
        Axis TangentAxis = XAxis;
        Axis RadialAxis = XAxis;

        // If a vertex on one border is on the discardLocation
        // side of another border, don't add it to the updated
        // border
        // For example, when updating left border, vertex A on
        // border 1 is on the right side of border 2, A will be
        // discarded
        VertexLocation discardLocation = NotDefined;
        bool bPackingFromLowerPlace = false;

        switch (direction)
        {
        case FromRight:
        {
            discardLocation = LeftToBorder;
            TangentAxis = YAxis; // y
            RadialAxis = XAxis; // x
            bPackingFromLowerPlace = false;
            break;
        }
        case FromLeft:
        {
            discardLocation = RightToBorder;
            TangentAxis = YAxis; // y
            RadialAxis = XAxis; // x
            bPackingFromLowerPlace = true;
            break;
        }
        case FromTop:
        {
            discardLocation = BelowBorder;
            TangentAxis = XAxis; // x
            RadialAxis = YAxis; // y
            bPackingFromLowerPlace = false;
            break;
        }
        case FromBottom:
        {
            discardLocation = AboveBorder;
            TangentAxis = XAxis; // x
            RadialAxis = YAxis; // y
            bPackingFromLowerPlace = true;
            break;
        }
        default:
            assert(false);
            break;
        }

        size_t dwAtlasBorderSize = atlasBorder.size();
        size_t dwChartBorderSize = chartBorder.size();

        VertexLocation location;
        float fDistance;

        VERTEX_ARRAY tempBorder;

        try
        {
            // 1. Before merge chart border and atlas border, find the
            // correspond segments on each border. This operation is just
            // used to decrease useless computation
            size_t dwAtlasBorderStart, dwAtlasBorderEnd;
            size_t dwChartBorderStart, dwChartBorderEnd;

            if (!FindCorrespondSegmentsOfBorders(
                atlasBorder,
                chartBorder,
                dwAtlasBorderStart,
                dwAtlasBorderEnd,
                dwChartBorderStart,
                dwChartBorderEnd,
                TangentAxis))
            {
                // if 2 borders have no correspond segments, just merge them according to
                // the increasing order of coordinate in tangent direction
                if (VECTOR_ITEM(&atlasBorder[dwAtlasBorderSize - 1]->uv, TangentAxis) <
                    VECTOR_ITEM(&chartBorder[0]->uv, TangentAxis))
                {
                    atlasBorder.insert(atlasBorder.end(), chartBorder.cbegin(), chartBorder.cend());
                }
                else if (VECTOR_ITEM(&atlasBorder[0]->uv, TangentAxis) >
                    VECTOR_ITEM(&chartBorder[dwChartBorderSize - 1]->uv, TangentAxis))
                {
                    tempBorder.insert(tempBorder.end(), atlasBorder.cbegin(), atlasBorder.cend());
                    atlasBorder.clear();

                    atlasBorder.insert(atlasBorder.end(), chartBorder.cbegin(), chartBorder.cend());

                    atlasBorder.insert(atlasBorder.end(), tempBorder.cbegin(), tempBorder.cend());
                }
                else
                {
                    assert(false);
                }
                return S_OK;
            }

            // 2. Add vertices before correspond segments into new border
            for (size_t i=0; i < dwAtlasBorderStart; i++)
            {
                tempBorder.push_back(atlasBorder[i]);
            }
            for (size_t i=0; i < dwChartBorderStart; i++)
            {
                tempBorder.push_back(chartBorder[i]);
            }

            // 3. Merge the correspond segments

            size_t ii = dwAtlasBorderStart;
            size_t jj = dwChartBorderStart;
            while (ii <= dwAtlasBorderEnd && jj <= dwChartBorderEnd)
            {
                float tangent1 = VECTOR_ITEM(&atlasBorder[ii]->uv, TangentAxis);
                float tangent2 = VECTOR_ITEM(&chartBorder[jj]->uv, TangentAxis);

                // 3.1 Check if current vertex on old atlas border can be a vertex
                // on new atlas border
                if (tangent1 < tangent2)
                {
                    location = CalculateVertexLocationToBorder(
                        chartBorder,
                        dwChartBorderStart,
                        dwChartBorderEnd,
                        atlasBorder[ii]->uv,
                        0,
                        fDistance,
                        TangentAxis);


                    if (location != discardLocation)
                    {
                        tempBorder.push_back(atlasBorder[ii]);
                    }
                    dwAtlasBorderStart = ii;
                    ii++;
                }
                // 3.2 Check if current vertex on chart border can be a vertex
                // on new atlas border
                else if (tangent1 > tangent2)
                {
                    location = CalculateVertexLocationToBorder(
                        atlasBorder,
                        dwAtlasBorderStart,
                        dwAtlasBorderEnd,
                        chartBorder[jj]->uv,
                        0,
                        fDistance,
                        TangentAxis);

                    if (location != discardLocation)
                    {
                        tempBorder.push_back(chartBorder[jj]);
                    }
                    dwChartBorderStart = jj;
                    jj++;
                }
                // 3.3 compare 2 vertices on different borders, and add
                // one into new atlas border.
                else
                {
                    float fRadia1 = VECTOR_ITEM(&atlasBorder[ii]->uv, RadialAxis);
                    float fRadia2 = VECTOR_ITEM(&chartBorder[jj]->uv, RadialAxis);

                    if (bPackingFromLowerPlace)
                    {
                        if (fRadia1 < fRadia2)
                        {
                            tempBorder.push_back(atlasBorder[ii]);
                        }
                        else if (fRadia1 > fRadia2)
                        {
                            tempBorder.push_back(chartBorder[jj]);
                        }
                        else
                        {
                            tempBorder.push_back(chartBorder[jj]);
                            //assert(fRadia1 != fRadia2);
                        }
                    }
                    else
                    {
                        if (fRadia1 > fRadia2)
                        {
                            tempBorder.push_back(atlasBorder[ii]);
                        }
                        else if (fRadia1 < fRadia2)
                        {
                            tempBorder.push_back(chartBorder[jj]);
                        }
                        else
                        {
                            tempBorder.push_back(chartBorder[jj]);
                            //assert(fRadia1 != fRadia2);
                        }
                    }
                    dwAtlasBorderStart = ii;
                    dwChartBorderStart = jj;
                    ii++;
                    jj++;
                }
            }
            // 4. Add vertices after correspond segments into new border
            for (size_t i = ii; i < dwAtlasBorderSize; i++)
            {
                tempBorder.push_back(atlasBorder[i]);
            }
            for (size_t i = jj; i < dwChartBorderSize; i++)
            {
                tempBorder.push_back(chartBorder[i]);
            }

            // 6. Update atlas border.
            atlasBorder.clear();

            atlasBorder.insert(atlasBorder.end(), tempBorder.cbegin(), tempBorder.cend());

        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    // Update atlas each time after adding a new chart
    static HRESULT UpdateAtlas(
        ATLASINFO& atlasInfo,
        PACKINGINFO& packingInfo,
        XMFLOAT2& newOrg,
        size_t dwMinRotationId)
    {
        HRESULT hr = S_OK;

        // 1. Update atlas bounding box
        if (newOrg.y < atlasInfo.fBoxBottom)
        {
            atlasInfo.fBoxBottom = newOrg.y;
        }

        if (newOrg.x < atlasInfo.fBoxLeft)
        {
            atlasInfo.fBoxLeft = newOrg.x;
        }

        if (newOrg.y + packingInfo.fUVHeight[dwMinRotationId]
            > atlasInfo.fBoxTop)
        {
            atlasInfo.fBoxTop =
                newOrg.y + packingInfo.fUVHeight[dwMinRotationId];
        }

        if (newOrg.x + packingInfo.fUVWidth[dwMinRotationId]
            > atlasInfo.fBoxRight)
        {
            atlasInfo.fBoxRight =
                newOrg.x + packingInfo.fUVWidth[dwMinRotationId];
        }

        // 2. Update atlas borders.
        FAILURE_RETURN(
            MergeBorders(
                FromTop,
                atlasInfo.currentTopBorder,
                packingInfo.topBorder[dwMinRotationId]));

        FAILURE_RETURN(
            MergeBorders(
                FromBottom,
                atlasInfo.currentBottomBorder,
                packingInfo.bottomBorder[dwMinRotationId]));

        FAILURE_RETURN(
            MergeBorders(
                FromLeft,
                atlasInfo.currentLeftBorder,
                packingInfo.leftBorder[dwMinRotationId]));

        FAILURE_RETURN(
            MergeBorders(
                FromRight,
                atlasInfo.currentRightBorder,
                packingInfo.rightBorder[dwMinRotationId]));

        return hr;
    }
}

///////////////////////////////////////////////////////////////////////////
////////////////////////// Public mehtods ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////

#ifdef _DEBUG
static void BruteForceFoldChecking(
        ISOCHARTMESH_ARRAY& chartList)
{
    const CBaseMeshInfo& baseInfo = chartList[0]->GetBaseMeshInfo();
    
    for (size_t ii=0; ii<chartList.size()-1; ii++)
    {
        auto& edgeList1 = chartList[ii]->GetEdgesList();
        ISOCHARTVERTEX* pVertList1 = chartList[ii]->GetVertexBuffer();
        ISOCHARTFACE* pFaceList1 = chartList[ii]->GetFaceBuffer(); 
        
        bool bFoundFold = false;
        if (edgeList1.size() < 1)
        {
            continue;
        }
        for (size_t jj=0; jj<edgeList1.size()-1; jj++)
        {
            ISOCHARTEDGE& edge1 = edgeList1[jj];
            const XMFLOAT2& v1 = pVertList1[edge1.dwVertexID[0]].uv;
            const XMFLOAT2& v2 = pVertList1[edge1.dwVertexID[1]].uv;
            
            for (size_t kk = jj + 1; kk<edgeList1.size(); kk++)
            {
                ISOCHARTEDGE& edge2 = edgeList1[kk];

                if (edge1.dwVertexID[0] == edge2.dwVertexID[0]
                ||edge1.dwVertexID[0] == edge2.dwVertexID[1]
                ||edge1.dwVertexID[1] == edge2.dwVertexID[0]
                ||edge1.dwVertexID[1] == edge2.dwVertexID[1])
                {
                    continue;
                }
                const XMFLOAT2& v3 = pVertList1[edge2.dwVertexID[0]].uv;
                const XMFLOAT2& v4 = pVertList1[edge2.dwVertexID[1]].uv;

                bool bIsIntersect = IsochartIsSegmentsIntersect(v1, v2, v3, v4);
                if (bIsIntersect)
                {
                    XMVECTOR vv1 = XMLoadFloat2(&v1);
                    XMVECTOR vv2 = XMLoadFloat2(&v2);
                    XMVECTOR vv3 = XMLoadFloat2(&v3);
                    XMVECTOR vv4 = XMLoadFloat2(&v4);

                    XMVECTOR vv5 = vv1 - vv3;
                    if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                    vv5 = vv1 - vv4;
                    if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                    vv5 = vv2 - vv3;
                    if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                    vv5 = vv2 - vv4;
                    if (IsInZeroRange(XMVectorGetX(XMVector2Length(vv5)))) continue;

                    size_t dwFaceRootID =
                        pFaceList1[edge1.dwFaceID[0]].dwIDInRootMesh;
                    if (IsInZeroRange(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                    {
                        continue;
                    }

                    if (edge1.dwFaceID[1] != INVALID_FACE_ID)
                    {
                        dwFaceRootID = 
                        pFaceList1[edge1.dwFaceID[1]].dwIDInRootMesh;
                        if (IsInZeroRange(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                        {
                            continue;
                        }
                    }
                    dwFaceRootID = 
                        pFaceList1[edge2.dwFaceID[0]].dwIDInRootMesh;
                    if (IsInZeroRange(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                    {
                        continue;
                    }

                    if (edge2.dwFaceID[1] != INVALID_FACE_ID)
                    {
                        dwFaceRootID = 
                        pFaceList1[edge2.dwFaceID[1]].dwIDInRootMesh;
                        if (IsInZeroRange(baseInfo.pfFaceAreaArray[dwFaceRootID]))
                        {
                            continue;
                        }
                    }
                    if (!bFoundFold)
                    {
                        bFoundFold = true;
                        DPF(0, "Found fold in chart %Iu...", ii);
                        DPF(0, "(%f, %f) (%f, %f) --> (%f, %f) (%f, %f)",
                                v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v4.x, v4.y);
                    }
                }
            }
        }
    }
}

static void BruteForceOverlappingChecking(
        ISOCHARTMESH_ARRAY& chartList)
{
    if (chartList.size() < 1) return;
    
    for (size_t ii=0; ii<chartList.size()-1; ii++)
    {
        auto& edgeList1 = chartList[ii]->GetEdgesList();
        ISOCHARTVERTEX* pVertList1 = chartList[ii]->GetVertexBuffer();

        for (size_t jj=ii+1; jj<chartList.size(); jj++)
        {           
            auto& edgeList2 = chartList[jj]->GetEdgesList();
            ISOCHARTVERTEX* pVertList2 = chartList[jj]->GetVertexBuffer();

            for (size_t m=0; m<edgeList1.size(); m++)
            {
                ISOCHARTEDGE& edge1 = edgeList1[m];
                if (!edge1.bIsBoundary)
                {
                    continue;
                }
                const XMFLOAT2& v1 = pVertList1[edge1.dwVertexID[0]].uv;
                const XMFLOAT2& v2 = pVertList1[edge1.dwVertexID[1]].uv;
                for (size_t n=0; n<edgeList2.size(); n++)
                {
                    ISOCHARTEDGE& edge2 = edgeList2[n];

                    const XMFLOAT2& v3 = pVertList2[edge2.dwVertexID[0]].uv;
                    const XMFLOAT2& v4 = pVertList2[edge2.dwVertexID[1]].uv;

                    bool bIsIntersect = IsochartIsSegmentsIntersect(v1, v2, v3, v4);
                    if (bIsIntersect)
                    {   
                        DPF(0, "Found intersection...");
                        DPF(0, "Edge 1 is %d-%d",
                                pVertList1[edge1.dwVertexID[0]].dwIDInRootMesh,
                                pVertList1[edge1.dwVertexID[1]].dwIDInRootMesh);

                        DPF(0, "Edge 2 is %d-%d",
                                pVertList2[edge2.dwVertexID[0]].dwIDInRootMesh,
                                pVertList2[edge2.dwVertexID[1]].dwIDInRootMesh);

                        DPF(0, "Chart1 %Iu, Chart2 %Iu\n", ii, jj);

                        DPF(0, "(%f, %f) (%f, %f) --> (%f, %f) (%f, %f)",
                            v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v4.x, v4.y);

                        assert(!bIsIntersect);
                    }
                }
            }
        }
    }
}
#endif

HRESULT CIsochartMesh::PackingCharts(
    ISOCHARTMESH_ARRAY& chartList,
    size_t dwWidth,
    size_t dwHeight,
    float gutter,
    CCallbackSchemer& callbackSchemer)
{
    HRESULT hr = S_OK;

#ifdef _DEBUG
    BruteForceFoldChecking(chartList);
#endif

    // 1. Prepare packing information.
    ATLASINFO atlasInfo;
    if (FAILED(hr = PreparePacking(
        chartList,
        dwWidth, dwHeight,
        gutter,
        atlasInfo)))
    {
        goto LEnd;
    }
    if (FAILED(hr =callbackSchemer.UpdateCallbackAdapt(1)))
    {
        goto LEnd;
    }

    // 2. Packing each chart.
    for (size_t iteration = 0; iteration< chartList.size(); iteration++)
    {
        CIsochartMesh* pChart = chartList[iteration];
        // Adding one chart into current atlas.
        hr = PackingOneChart( pChart, atlasInfo, iteration);
        if (FAILED(hr))
        {
            goto LEnd;
        }       

        // Destroy packing information structure of current chart.
        pChart->DestroyPakingInfoBuffer();
        if (FAILED(hr = callbackSchemer.UpdateCallbackAdapt(1)))
        {
            goto LEnd;
        }
    }

    DPF(3, "Area lost rate is : %f", 1 - atlasInfo.fPackedChartArea / 
           ((atlasInfo.fBoxRight-atlasInfo.fBoxLeft) * 
           (atlasInfo.fBoxTop-atlasInfo.fBoxBottom)));

    // 3. Normalize the atlas to [0.0, 1.0]
    NormalizeAtlas(chartList, atlasInfo);
#ifdef _DEBUG
    BruteForceOverlappingChecking(chartList);
#endif

LEnd:
    // If success, all packing information buffer has been destroyed.
    if (FAILED(hr))
    {
        DestroyChartsPackingBuffer(chartList);
    }

    FreeAditionalVertices(atlasInfo);
    return hr;
}

///////////////////////////////////////////////////////////////////////////
/////////////////////////// Packing Preparation////////////////////////////
///////////////////////////////////////////////////////////////////////////
// Estimate Pixel Length
static float EstimatePixelLength(
    ISOCHARTMESH_ARRAY& chartList,
    float fTotalArea,
    size_t dwWidth,
    size_t dwHeight,
    float gutter)
{
    float fGutter = gutter * STANDARD_UV_SIZE / std::min(dwWidth, dwHeight);
    float fBaseSpaceArea = 
        fTotalArea * STANDARD_SPACE_RATE/ (1-STANDARD_SPACE_RATE);

    float fBasePixelLength = 
        IsochartSqrtf((fTotalArea+fBaseSpaceArea) / (dwHeight * dwWidth));

    float fBaseGutter = gutter * fBasePixelLength;

    float fChartShortenLength = 
        (fBaseGutter * (fGutter/STANDARD_GUTTER) - fBaseGutter)/2;

    
    for (size_t i=0; i<chartList.size(); i++)
    {
        PACKINGINFO* pPackInfo = chartList[i]->GetPackingInfoBuffer();
        if (IsInZeroRange(pPackInfo->fUVHeight[0]))
        {
            continue;
        }
        float fScale = 
            (pPackInfo->fUVHeight[0] -fChartShortenLength)/pPackInfo->fUVHeight[0];
        
        fBaseSpaceArea += (1-fScale*fabsf(fScale))*chartList[i]->GetChart2DArea();
    }

    float fNewChartRate = fTotalArea / (fTotalArea + fBaseSpaceArea);

    return IsochartSqrtf(fTotalArea / (dwHeight * dwWidth * fNewChartRate));
}

float CIsochartMesh::GuranteeSmallestChartArea(
    ISOCHARTMESH_ARRAY& chartList)
{
    float fTotalArea = CalculateAllPackingChartsArea(chartList);

    if (IsInZeroRange2(fTotalArea))
    {
        return fTotalArea;
    }

    float fNewTotalArea = fTotalArea;

    return fNewTotalArea;
}

// Performed before packing chart.
// 1. Allocate packing information buffer for each chart
// 2. Initialize global sin and cos table
// 3. Align each chart along longest axis
// 4. Adjust chart UV-area
// 5. Initialize atlas information structure
HRESULT CIsochartMesh::PreparePacking(
    ISOCHARTMESH_ARRAY& chartList,
    size_t dwWidth,
    size_t dwHeight,
    float gutter,
    ATLASINFO& atlasInfo)
{
    assert(dwWidth > 0);
    assert(dwHeight > 0);
    HRESULT hr = S_OK;

    // 1. Create data structure for each chart needed by Packing Charts.
    FAILURE_RETURN(CreateChartsPackingBuffer(chartList));

    // 2. Initialize global sin and cos table needed in packing process.
    for (size_t ii=0; ii<CHART_ROTATION_NUMBER; ii++)
    {
        float fAngle = ii*2.f*XM_PI / CHART_ROTATION_NUMBER;
        g_PackingCosTable[ii] = cosf(fAngle);
        g_PackingSinTable[ii] = sinf(fAngle);
    }

    // 3. Gurantee All charts larger than a lower bound.
    float fTotalArea = GuranteeSmallestChartArea(chartList);

    // 4. Align all charts according to the axis connecting the farthest
    // two vertices in the chart.
    AlignChartsWithLongestAxis(chartList);

    // 5. Sort charts by some attribute (currently, by height).
    SortCharts(chartList);

    // 6. Initialize atlas information structure
    // Convert between metric specified by user ( in pixel ) and metric of original
    // mesh
    atlasInfo.fPixelLength = 
        EstimatePixelLength(
            chartList,
            fTotalArea,
            dwWidth,
            dwHeight,
            gutter);

    atlasInfo.fGutter = gutter * atlasInfo.fPixelLength;
    DPF(2, "Pixel Length is %f", atlasInfo.fPixelLength);
    atlasInfo.fExpectedAtlasWidth = dwWidth * atlasInfo.fPixelLength;
    atlasInfo.fWidthHeightRatio = (static_cast<float>(dwWidth)) / (dwHeight);
    atlasInfo.fBoxTop = 0;
    atlasInfo.fBoxBottom = 0;
    atlasInfo.fBoxLeft = 0;
    atlasInfo.fBoxRight = 0;
    atlasInfo.fPackedChartArea = 0;
    return hr;
}

HRESULT CIsochartMesh::CreateChartsPackingBuffer(
    ISOCHARTMESH_ARRAY& chartList)
{
    for (size_t i=0; i<chartList.size(); i++)
    {
        assert( chartList[i] != 0 );
        HRESULT hr = S_OK;
        if (FAILED(hr = chartList[i]->CreatePackingInfoBuffer()))
        {
            DestroyChartsPackingBuffer(chartList);
            return hr;
        }
    }
    return S_OK;
}

void CIsochartMesh::DestroyChartsPackingBuffer(
    ISOCHARTMESH_ARRAY& chartList)
{
    for (size_t i=0; i<chartList.size(); i++)
    {
        assert( chartList[i] != 0 );
        chartList[i]->DestroyPakingInfoBuffer();
    }
}

HRESULT CIsochartMesh::CreatePackingInfoBuffer()
{
    delete m_pPackingInfo;
    m_pPackingInfo = nullptr;

    m_pPackingInfo = new (std::nothrow) PACKINGINFO;
    if (!m_pPackingInfo)
    {
        return E_OUTOFMEMORY;
    }

    m_pPackingInfo->pVertUV = new (std::nothrow) XMFLOAT2[m_dwVertNumber];
    m_pPackingInfo->pStandardUV = new (std::nothrow) XMFLOAT2[m_dwVertNumber];

    if (!m_pPackingInfo->pVertUV || !m_pPackingInfo->pStandardUV)
    {
        delete m_pPackingInfo;
        m_pPackingInfo = nullptr;
        // pVertUV and pStandardUV will be deleted in destructor.
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

void CIsochartMesh::DestroyPakingInfoBuffer()
{
    delete m_pPackingInfo;
    m_pPackingInfo = nullptr;
}

PACKINGINFO* CIsochartMesh::GetPackingInfoBuffer() const
{
    return m_pPackingInfo;
}


float CIsochartMesh::CalculateAllPackingChartsArea(
    ISOCHARTMESH_ARRAY& chartList)
{
    float fTotalArea = 0;
    for (size_t i=0; i<chartList.size(); i++)
    {
        assert(chartList[i] != 0);
        chartList[i]->m_fChart2DArea = chartList[i]->CalculateChart2DArea();
        fTotalArea += chartList[i]->m_fChart2DArea;
    }
    return fTotalArea;
}

// Rotate charts to make their bounding box have longest height.
void CIsochartMesh::AlignChartsWithLongestAxis(
    ISOCHARTMESH_ARRAY& chartList)
{
    for (size_t i=0; i<chartList.size(); i++)
    {
        CIsochartMesh* pChart  = chartList[i];
        pChart->AlignUVWithLongestAxis();
    }
}

// Rotate a chart to make its bounding box has longest height
void CIsochartMesh::AlignUVWithLongestAxis() const
{
    ISOCHARTVERTEX* pVertex1 = nullptr;
    XMFLOAT2 minVec;
    XMFLOAT2 maxVec;
    CalculateChartMinimalBoundingBox(
        CHART_ROTATION_NUMBER,    
        minVec,
        maxVec);
    m_pPackingInfo->fUVWidth[0] = maxVec.x - minVec.x;
    m_pPackingInfo->fUVHeight[0] = maxVec.y - minVec.y;

    // 4. Set the left-bottom corner of bounding box to be origin.
    pVertex1 = m_pVerts;
    XMFLOAT2* pUV = m_pPackingInfo->pVertUV;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pVertex1->uv.x -= minVec.x;
        pVertex1->uv.y -= minVec.y;
        *pUV = pVertex1->uv;
        pVertex1++;
        pUV++;
    }
}

// Sort the charts in decreasing order by chart area.
namespace
{
    int CompareChart(const void* chart1, const void* chart2)
    {
        const CIsochartMesh* pChart1 = *(const CIsochartMesh**) chart1;
        const CIsochartMesh* pChart2 = *(const CIsochartMesh**) chart2;

        auto pPackingInfo1 = pChart1->GetPackingInfoBuffer();
        auto pPackingInfo2 = pChart2->GetPackingInfoBuffer();

        float a = pPackingInfo1->fUVHeight[0] - pPackingInfo2->fUVHeight[0];

        if (a > 0)
        {
            return -1;
        }
        if (a < 0)
        {
            return 1;
        }
        return 0;
    }
}

void CIsochartMesh::SortCharts(
    ISOCHARTMESH_ARRAY& chartList)
{
    std::sort(chartList.begin(), chartList.end(), CompareChart);
}

// Add One Chart into atlas
HRESULT CIsochartMesh::PackingOneChart(
    CIsochartMesh* pChart,
    ATLASINFO& atlasInfo,
    size_t dwIteration)
{
    HRESULT hr = S_OK;

    auto pPackingInfo = pChart->GetPackingInfoBuffer();

    // 1.  If current chart's area is zero, don't pack it, just put it at (0, 0).
    if (IsInZeroRange2(pChart->m_fChart2DArea))
    {
        PackingZeroAreaChart(pChart);
        return hr;
    }
    // 2. Rotate current chart and Calculate the borders of the chart in all directions
    // This function rotate current chart in CHART_ROTATION_NUMBER direction, Calculate
    // left, right, top and bottom borders of in each direction. Calculate bounding box
    // of current chart in each direction.
    FAILURE_RETURN(pChart->CalculateChartBordersOfAllDirection(atlasInfo));

    // 3. Packing one chart
    size_t dwMinRotationId = 0;
    size_t dwDirMinRotationId[PACKING_DIRECTION_NUMBER];

    float fAreaLost = 0;
    float fMinAreaLost = 0;
    float fDirMinAreaLost[PACKING_DIRECTION_NUMBER];

    float fBetweenArea;
    float fMinBetweenArea[PACKING_DIRECTION_NUMBER];

    XMFLOAT2 dirOrg[PACKING_DIRECTION_NUMBER];
    XMFLOAT2 newOrgin;

    /*
    //if ((dwIteration > 0 && dwIteration < 12) ||dwIteration > 12 )
    
    if (dwIteration > 1 )
    {
        for (size_t i=0; i<pChart->m_dwVertNumber; i++)
        {
            pChart->m_pVerts[i].uv.x = pChart->m_pVerts[i].uv.y = 0;
        }

        return hr;
    }
    */

    // 3.1 If current chart is the first chart to be packed,
    // initialize the UV-atlas by putting the first chart into it.
    if (0 == dwIteration || atlasInfo.fPackedChartArea == 0)
    {
        // Find one direction with smallest area lost rate.
        atlasInfo.fPackedChartArea = pChart->m_fChart2DArea;
        fMinAreaLost = FLT_MAX;
        for (size_t i=0; i<CHART_ROTATION_NUMBER; i++)
        {
            fAreaLost =
                1.0f - atlasInfo.fPackedChartArea /
                (pPackingInfo->fUVWidth[i]*pPackingInfo->fUVHeight[i]);

            if (fAreaLost < fMinAreaLost)
            {
                fMinAreaLost = fAreaLost;
                dwMinRotationId = i;
            }
        }
        // Rotate chart to the direction gotten by above step
        pChart->RotateChartAroundCenter(dwMinRotationId, false);

        // Initialize atlas after packing the first chart.
        FAILURE_RETURN(
            Initializeatlas(
            atlasInfo,
            *pPackingInfo,
            dwMinRotationId));
    }

    // 3.2 Add charts into current atlas
    else
    {
        ISOCHARTVERTEX* pVex;

        atlasInfo.fPackedChartArea += pChart->m_fChart2DArea;

        for (size_t i=0; i<PACKING_DIRECTION_NUMBER; i++)
        {
            fDirMinAreaLost[i] = FLT_MAX;
            fMinBetweenArea[i] = FLT_MAX;
            dwDirMinRotationId[i] = INVALID_INDEX;
        }

        atlasInfo.fExpectedAtlasWidth =
            (atlasInfo.fBoxTop - atlasInfo.fBoxBottom) * atlasInfo.fWidthHeightRatio;

        // 3.1.1 Need to add chart in horizon direction to increase width of atlas
        if (atlasInfo.fExpectedAtlasWidth
            > atlasInfo.fBoxRight - atlasInfo.fBoxLeft)
        {
            for (size_t i=0; i<CHART_ROTATION_NUMBER; i++)
            {     
                 ISOCHARTVERTEX* pOneBorderVertex = pPackingInfo->leftBorder[i][1];
                 if (INVALID_VERT_ID == pOneBorderVertex->dwIDInRootMesh)
                 {
                     for (size_t j=0; j<4; j++)
                     {
                         pPackingInfo->pStandardVirtualCorner[j] = pOneBorderVertex[j].uv;
                     }
                 }
                 else
                 {
                     pChart->RotateBordersAroundCenter(i);
                     pVex = pChart->GetVertexBuffer();
                     for (size_t j=0; j<pChart->GetVertexNumber(); j++)
                     {
                         pPackingInfo->pStandardUV[j] = pVex->uv;
                         pVex++;
                     }
                 }

                //Try packing from right
                FAILURE_RETURN(
                    FindChartPosition(
                        FromRight,
                        atlasInfo,
                        pPackingInfo,
                        i,
                        newOrgin,
                        fBetweenArea,
                        fAreaLost));

                UpdateAreaLostInfo(
                    FromRight,
                    dwDirMinRotationId,
                    i,
                    dirOrg,
                    newOrgin,
                    fDirMinAreaLost,
                    fAreaLost,
                    fMinBetweenArea,
                    fBetweenArea);

                //Try packing from left
                FAILURE_RETURN(
                    FindChartPosition(
                        FromLeft,
                        atlasInfo,
                        pPackingInfo,
                        i,
                        newOrgin,
                        fBetweenArea,
                        fAreaLost));

                UpdateAreaLostInfo(
                    FromLeft,
                    dwDirMinRotationId,
                    i,
                    dirOrg,
                    newOrgin,
                    fDirMinAreaLost,
                    fAreaLost,
                    fMinBetweenArea,
                    fBetweenArea);
            }
        }

        // 3.1.2 Need to add chart in vertical direction to increase height of atlas
        else
        {
            for (size_t i=0; i<CHART_ROTATION_NUMBER; i++)
            {
                ISOCHARTVERTEX* pOneBorderVertex = pPackingInfo->topBorder[i][1];
                if (INVALID_VERT_ID == pOneBorderVertex->dwIDInRootMesh)
                {
                    for (size_t j=0; j<4; j++)
                    {
                        pPackingInfo->pStandardVirtualCorner[j] = pOneBorderVertex[j].uv;
                    }
                }
                else
                {
                    pChart->RotateBordersAroundCenter(i);
                    pVex = pChart->GetVertexBuffer();
                    for (size_t j=0; j<pChart->GetVertexNumber(); j++)
                    {
                        pPackingInfo->pStandardUV[j] = pVex->uv;
                        pVex++;
                    }
                }

                //Try packing from top
                FAILURE_RETURN(
                    FindChartPosition(
                        FromTop,
                        atlasInfo,
                        pPackingInfo,
                        i,
                        newOrgin,
                        fBetweenArea,
                        fAreaLost));

                UpdateAreaLostInfo(
                    FromTop,
                    dwDirMinRotationId,
                    i,
                    dirOrg,
                    newOrgin,
                    fDirMinAreaLost,
                    fAreaLost,
                    fMinBetweenArea,
                    fBetweenArea);

                //Try packing from bottom
                FAILURE_RETURN(
                    FindChartPosition(
                        FromBottom,
                        atlasInfo,
                        pPackingInfo,
                        i,
                        newOrgin,
                        fBetweenArea,
                        fAreaLost));

                UpdateAreaLostInfo(
                    FromBottom,
                    dwDirMinRotationId,
                    i,
                    dirOrg,
                    newOrgin,
                    fDirMinAreaLost,
                    fAreaLost,
                    fMinBetweenArea,
                    fBetweenArea);
            }
        }

        // 3.2 Find the approach which causes less area lost
        size_t dwPackDirection = FromRight;
        for (size_t j=1; j<PACKING_DIRECTION_NUMBER; j++)
        {
            if (fDirMinAreaLost[j] < fDirMinAreaLost[dwPackDirection])
            {
                dwPackDirection = j;
            }
        }

    if (dwDirMinRotationId[dwPackDirection] == INVALID_INDEX)
    {
        DPF(0, "2d area %f", pChart->m_fChart2DArea);	
        DPF(0, "3d area %f", pChart->m_fChart3DArea);
        DPF(0, "Face number %Iu", pChart->m_dwFaceNumber);		
        DPF(0, "Vert number %Iu", pChart->m_dwVertNumber);

        for (size_t ii=0; ii<pChart->m_dwVertNumber; ii++)
        {
            DPF(0, "(%f, %f)", pChart->m_pVerts[ii].uv.x, pChart->m_pVerts[ii].uv.y);
        }
    }
     
        assert(dwDirMinRotationId[dwPackDirection] != INVALID_INDEX);

        // 3.3 Use the method gotten by last step to pack current chart
        pChart->RotateChartAroundCenter(dwDirMinRotationId[dwPackDirection], false);
        newOrgin = dirOrg[dwPackDirection];
        pVex = pChart->GetVertexBuffer();
        for (size_t i=0; i<pChart->GetVertexNumber(); i++)
        {
            pVex->uv.x += newOrgin.x;
            pVex->uv.y += newOrgin.y;
            pVex++;
        }
    
        ISOCHARTVERTEX* pOneBorderVertex
            = pPackingInfo->leftBorder[dwDirMinRotationId[dwPackDirection]][1];
        if (INVALID_VERT_ID == pOneBorderVertex->dwIDInRootMesh)
        {
            pVex = pChart->GetVertexBuffer();
            AdjustCornerBorder(
                pOneBorderVertex,
                pVex,
                pChart->GetVertexNumber());
        }

        // 3.4 Update current horizon lines.
        FAILURE_RETURN(
            UpdateAtlas(
                atlasInfo,
                *pPackingInfo,
                newOrgin,
                dwDirMinRotationId[dwPackDirection]) );
    }
    
    return hr;
}

// Packing chart with zero area to origin
void CIsochartMesh::PackingZeroAreaChart(CIsochartMesh* pChart)
{
    assert(pChart != 0);
    for (size_t i=0; i<pChart->m_dwVertNumber; i++)
    {
        assert(pChart->m_pVerts != 0);
        pChart->m_pVerts[i].uv.x = 0;
        pChart->m_pVerts[i].uv.y = 0;
    }
}

// Calculate borders of chart in each rotate direction.
HRESULT CIsochartMesh::CalculateChartBordersOfAllDirection(
    ATLASINFO& atlasInfo)
{
    HRESULT hr = S_OK;

    VERTEX_ARRAY border1;
    VERTEX_ARRAY border2;


    for (size_t dwRotationCount=0;
        dwRotationCount < CHART_ROTATION_NUMBER;
        dwRotationCount++)
    {
        // 1. Rotate the Chart by a special angle
        ISOCHARTVERTEX* pLeftVertex = nullptr;  // Left most vertex
        ISOCHARTVERTEX* pRightVertex = nullptr; // Right most vertex
        ISOCHARTVERTEX* pTopVertex = nullptr;   // Top most vertex
        ISOCHARTVERTEX* pBottomVertex = nullptr;// Bottom most vertex

        RotateChartAroundCenter(
            dwRotationCount,
            true, // Only rotate boundary vertex
            &pLeftVertex,
            &pRightVertex,
            &pTopVertex,
            &pBottomVertex);

        // 2. Get the top & bottom border of the rotated chart.
        assert(pLeftVertex != 0 && pRightVertex != 0 && pLeftVertex != pRightVertex);
        assert(pTopVertex != 0 && pBottomVertex != 0 && pTopVertex != pBottomVertex);

        bool bCanDecide1 = false;
        bool bCanDecide2 = false;
        // 3. Calculate chart borders in current direction
        FAILURE_RETURN(
            CalculateChartBorders(
                true,  // Calculate horizontal borders (top & bottom)
                m_pPackingInfo->bottomBorder[dwRotationCount],
                m_pPackingInfo->topBorder[dwRotationCount],
                pLeftVertex, // From left most vertex
                pRightVertex, // scan to right most vertex
                border1,
                border2,
                bCanDecide1));

        FAILURE_RETURN(
            CalculateChartBorders(
                false, // Calculate horizontal borders (left & right)
                m_pPackingInfo->leftBorder[dwRotationCount],
                m_pPackingInfo->rightBorder[dwRotationCount],
                pBottomVertex, // From bottom most vertex
                pTopVertex,    // scan to top most vertex
                border1,
                border2,
                bCanDecide2));

        if (!bCanDecide1 || !bCanDecide2)
        {
            DPF(1, "Setup corner boundaries..");
            FAILURE_RETURN(
                AddBoundingBoxBorder(
                atlasInfo, 
                *m_pPackingInfo, 
                dwRotationCount, 
                pLeftVertex, 
                pRightVertex, 
                pTopVertex, 
                pBottomVertex));
        }

        // 4. Check if the horizon and the vertical is valid.
#ifdef _DEBUG
        VERTEX_ARRAY& topBorder = m_pPackingInfo->topBorder[dwRotationCount];
        VERTEX_ARRAY& bottomBorder = m_pPackingInfo->bottomBorder[dwRotationCount];
        VERTEX_ARRAY& leftBorder = m_pPackingInfo->leftBorder[dwRotationCount];
        VERTEX_ARRAY& rightBorder = m_pPackingInfo->rightBorder[dwRotationCount];

        for (size_t i=0; i<topBorder.size()-1; i++)
        {
            assert(
                topBorder[i]->uv.x
                <= topBorder[i+1]->uv.x);
        }
        for (size_t i=0; i<bottomBorder.size()-1; i++)
        {
            assert(
                bottomBorder[i]->uv.x
                <= bottomBorder[i+1]->uv.x);
        }

        for (size_t i=0; i<leftBorder.size()-1; i++)
        {
            assert(
                leftBorder[i]->uv.y
                <= leftBorder[i+1]->uv.y);
        }

        for (size_t i=0; i<rightBorder.size()-1; i++)
        {
            assert(rightBorder[i]->uv.y
                <= rightBorder[i+1]->uv.y);
        }
#endif
    }
    return S_OK;
}


// Rotate chart and align left-bottom corner of chart's bounding box to origin
void CIsochartMesh::RotateChartAroundCenter(
    size_t dwRotationId,
    bool bOnlyRotateBoundaries, // Only need to rotate boundary vertex
    ISOCHARTVERTEX** ppLeftMostVertex,
    ISOCHARTVERTEX** ppRightMostVertex,
    ISOCHARTVERTEX** ppTopMostVertex,
    ISOCHARTVERTEX** ppBottomMostVertex)
{
    float fCos = g_PackingCosTable[dwRotationId];
    float fSin = g_PackingSinTable[dwRotationId];

    if (bOnlyRotateBoundaries)
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            if (m_pVerts[i].bIsBoundary)
            {
                RotateVertexAroundCenter(
                    m_pVerts[i].uv,
                    m_pPackingInfo->pVertUV[i],
                    m_pPackingInfo->fUVWidth[0]/2,
                    m_pPackingInfo->fUVHeight[0]/2,
                    fSin,
                    fCos);
            }
        }
    }
    else
    {
        for (size_t i=0; i<m_dwVertNumber; i++)
        {
            RotateVertexAroundCenter(
                m_pVerts[i].uv,
                m_pPackingInfo->pVertUV[i],
                m_pPackingInfo->fUVWidth[0]/2,
                m_pPackingInfo->fUVHeight[0]/2,
                fSin,
                fCos);
        }
    }

    XMFLOAT2 minVec;
    XMFLOAT2 maxVec;

    minVec.x = minVec.y = FLT_MAX;
    maxVec.x = maxVec.y = -FLT_MAX;

    ISOCHARTVERTEX* pVertex = m_pVerts;
    size_t dwLeft = 0, dwRight = 0, dwTop = 0, dwBottom = 0;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        if (pVertex->bIsBoundary)
        {
            if (pVertex->uv.x > maxVec.x)
            {
                maxVec.x = pVertex->uv.x;
                dwRight = i;
            }
            if (pVertex->uv.y > maxVec.y)
            {
                maxVec.y = pVertex->uv.y;
                dwTop = i;
            }

            if (pVertex->uv.x < minVec.x)
            {
                minVec.x = pVertex->uv.x;
                dwLeft = i;
            }
            if (pVertex->uv.y < minVec.y)
            {
                minVec.y = pVertex->uv.y;
                dwBottom = i;
            }
        }
        pVertex++;
    }
    // a.
    if (m_pVerts[dwLeft].uv.x == m_pVerts[dwBottom].uv.x)
    {
        dwLeft = dwBottom;
    }
    if (m_pVerts[dwLeft].uv.x == m_pVerts[dwTop].uv.x)
    {
        dwLeft = dwTop;
    }

    // b.
    if (m_pVerts[dwRight].uv.x == m_pVerts[dwTop].uv.x)
    {
        dwRight = dwTop;
    }
    if (m_pVerts[dwRight].uv.x == m_pVerts[dwBottom].uv.x)
    {
        dwRight = dwBottom;
    }

    // c.
    if ( m_pVerts[dwBottom].uv.y == m_pVerts[dwLeft].uv.y)
    {
        dwBottom = dwLeft;
    }
    if ( m_pVerts[dwBottom].uv.y == m_pVerts[dwRight].uv.y)
    {
        dwBottom = dwRight;
    }

    //d.
    if (m_pVerts[dwTop].uv.y == m_pVerts[dwRight].uv.y)
    {
        dwTop = dwRight;
    }
    if (m_pVerts[dwTop].uv.y == m_pVerts[dwLeft].uv.y)
    {
        dwTop = dwLeft;
    }

    m_pPackingInfo->fUVWidth[dwRotationId] = maxVec.x - minVec.x;
    m_pPackingInfo->fUVHeight[dwRotationId] = maxVec.y - minVec.y;

    pVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pVertex->uv.x -= minVec.x;
        pVertex->uv.y -= minVec.y;
        pVertex++;
    }

    if (ppLeftMostVertex)
    {
        *ppLeftMostVertex = m_pVerts+dwLeft;
    }

    if (ppRightMostVertex)
    {
        *ppRightMostVertex = m_pVerts+dwRight;
    }

    if (ppTopMostVertex)
    {
        *ppTopMostVertex = m_pVerts+dwTop;
    }

    if (ppBottomMostVertex)
    {
        *ppBottomMostVertex = m_pVerts+dwBottom;
    }
}


// Calculate horizontal or vertical borders of a chart in one
// rotation direction
HRESULT CIsochartMesh::CalculateChartBorders(
    bool bHorizontal,
    VERTEX_ARRAY& lowerBorder, // border with lower radial coordinates
    VERTEX_ARRAY& higherBorder,// border with higher radial coordinates
    ISOCHARTVERTEX* pStartVertex,
    ISOCHARTVERTEX* pEndVertex,
    VERTEX_ARRAY& workBorder1,
    VERTEX_ARRAY& workBorder2,
    bool& bCanDecide)
{
    HRESULT hr = S_OK;

    // 1. Find the first one of the two boundary edges connecting to
    // start vertex
    uint32_t dwFirstBoundaryIndex = 0;
    ISOCHARTEDGE* pBoundaryEdge = nullptr;
    for (uint32_t i=0; i<pStartVertex->edgeAdjacent.size(); i++)
    {
        pBoundaryEdge = &(m_edges[pStartVertex->edgeAdjacent[i]]);
        if (pBoundaryEdge->bIsBoundary)
        {
            dwFirstBoundaryIndex = i;
            break;
        }
    }
    assert(dwFirstBoundaryIndex < pStartVertex->edgeAdjacent.size());

    // 2. Scan to get the first border
    workBorder1.clear();
    FAILURE_RETURN(
        ScanAlongBoundayEdges(
            pStartVertex,
            pEndVertex,
            pBoundaryEdge,
            workBorder1));
    assert(workBorder1.size() > 1);

    // 3. Find the second one of the two boundary edges connecting to
    // start vertex
    pBoundaryEdge = nullptr;
    for (uint32_t i=dwFirstBoundaryIndex+1; i<pStartVertex->edgeAdjacent.size(); i++)
    {
        pBoundaryEdge = &(m_edges[pStartVertex->edgeAdjacent[i]]);
        if (pBoundaryEdge->bIsBoundary)
        {
            dwFirstBoundaryIndex = i;
            break;
        }
    }
    assert(dwFirstBoundaryIndex < pStartVertex->edgeAdjacent.size());

    // 4. Scan to get the second border
    workBorder2.clear();
    FAILURE_RETURN(
        ScanAlongBoundayEdges(
            pStartVertex,
            pEndVertex,
            pBoundaryEdge,
            workBorder2));
    assert(workBorder2.size() > 1);

    higherBorder.clear();
    lowerBorder.clear();

    // 5. Decide which border is higher border and which border is lower
    // border.
    // Under D3D coordinates systems, bottom border always on the clockwise
    // direction of top border; right border always on the clockwise  direction
    // of left border.

    float fDotValue1=1.0f, fDotValue2=1.0f;
    bool bCanDecide1 = false, bCanDecide2 = false;
    bool bFirstBorderOutside
        = IsB2OnClockwiseDirOfB1AtBegin(
            workBorder1, workBorder2, bCanDecide1, fDotValue1);
    bool bSecondBorderInSide
     = !IsB1OnClockwiseDirOfB2AtEnd(
            workBorder1, workBorder2, bCanDecide2, fDotValue2);

    if (!bCanDecide1 || !bCanDecide2)
    {
        bCanDecide = false;
        return hr;
    }

    if ((bFirstBorderOutside && !bSecondBorderInSide) 
        || (!bFirstBorderOutside && bSecondBorderInSide))
    {
        DPF(1, "Dot value 1 = %f, Dot value 2 = %f", fDotValue1, fDotValue2);
        if (fabsf(fDotValue1) < 0.1f && fabsf(fDotValue2) > 0.9f)
        {
            bSecondBorderInSide = bFirstBorderOutside;
        }
        else if (fabsf(fDotValue2) < 0.1f && fabsf(fDotValue1) > 0.9f)
        {
            bFirstBorderOutside = bSecondBorderInSide;
        }
        else
        {
            bCanDecide = false;
            return hr;
        }
    }
    bCanDecide = true;

    try
    {
        if (bFirstBorderOutside)
        {
            if (bHorizontal) // Higherborder is topborder, Lowerborder is bottomboder
            {
                higherBorder.insert(higherBorder.end(), workBorder1.cbegin(), workBorder1.cend());
                lowerBorder.insert(lowerBorder.end(), workBorder2.cbegin(), workBorder2.cend());
            }
            else
            {
                higherBorder.insert(higherBorder.end(), workBorder2.cbegin(), workBorder2.cend());
                lowerBorder.insert(lowerBorder.end(), workBorder1.cbegin(), workBorder1.cend());
            }
        }
        else
        {
            if (bHorizontal) // Higherborder is rightborder, Lowerborder is leftborder
            {
                higherBorder.insert(higherBorder.end(), workBorder2.cbegin(), workBorder2.cend());
                lowerBorder.insert(lowerBorder.end(), workBorder1.cbegin(), workBorder1.cend());
            }
            else
            {
                higherBorder.insert(higherBorder.end(), workBorder1.cbegin(), workBorder1.cend());
                lowerBorder.insert(lowerBorder.end(), workBorder2.cbegin(), workBorder2.cend());
            }
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // 6. Remove redundant vertices
    FAILURE_RETURN(
    RemoveRedundantVerticesInBorders(bHorizontal, true, lowerBorder));

    FAILURE_RETURN(
    RemoveRedundantVerticesInBorders(bHorizontal, false, higherBorder));

    return hr;
}

// Scan along  boundary edges of chart to find borders.
HRESULT CIsochartMesh::ScanAlongBoundayEdges(
    ISOCHARTVERTEX* pStartVertex,
    ISOCHARTVERTEX* pEndVertex,
    ISOCHARTEDGE* pStartEdge,
    VERTEX_ARRAY& scanVertexList)
{
    try
    {
        ISOCHARTEDGE* pBoundaryEdge = pStartEdge;

        scanVertexList.push_back(pStartVertex);

        ISOCHARTVERTEX* pVertex = pStartVertex;

        while (pVertex != pEndVertex)
        {
            if (pBoundaryEdge->dwVertexID[0] == pVertex->dwID)
            {
                pVertex = m_pVerts + pBoundaryEdge->dwVertexID[1];
            }
            else
            {
                pVertex = m_pVerts + pBoundaryEdge->dwVertexID[0];
            }

            scanVertexList.push_back(pVertex);

            ISOCHARTEDGE* pScanEdge = nullptr;
            for (size_t j = 0; j < pVertex->edgeAdjacent.size(); j++)
            {
                ISOCHARTEDGE* pTempEdge = &(m_edges[pVertex->edgeAdjacent[j]]);
                if (pTempEdge->bIsBoundary && pTempEdge != pBoundaryEdge)
                {
                    if (pScanEdge)
                    {
                        DPF(0, "Vertex %d has more than 2 boundary edges leaving it", pVertex->dwIDInRootMesh);
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    }

                    pScanEdge = pTempEdge;
                }
            }

            assert(pScanEdge != 0);
            _Analysis_assume_(pScanEdge != 0);

            assert(pScanEdge->bIsBoundary && pScanEdge != pBoundaryEdge);
            if (pVertex == pStartVertex)
            {
                DPF(0, "Chart has more than 2 boundaries");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }

            pBoundaryEdge = pScanEdge;
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

// Rotate chart border around center and align left-bottom corner
// of chart's bounding box to origin
void CIsochartMesh::RotateBordersAroundCenter(
    size_t dwRotationId)
{
    float fCos = g_PackingCosTable[dwRotationId];
    float fSin = g_PackingSinTable[dwRotationId];

    ISOCHARTVERTEX* pVertex;

    for (size_t i=0; i<m_pPackingInfo->topBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->topBorder[dwRotationId][i];
        RotateVertexAroundCenter(
            pVertex->uv,
            m_pPackingInfo->pVertUV[pVertex->dwID],
            m_pPackingInfo->fUVWidth[0]/2,
            m_pPackingInfo->fUVHeight[0]/2,
            fSin,
            fCos);
    }

    for (size_t i=0; i<m_pPackingInfo->bottomBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->bottomBorder[dwRotationId][i];
        RotateVertexAroundCenter(
            pVertex->uv,
            m_pPackingInfo->pVertUV[pVertex->dwID],
            m_pPackingInfo->fUVWidth[0]/2,
            m_pPackingInfo->fUVHeight[0]/2,
            fSin,
            fCos);
    }

    for (size_t i=0; i<m_pPackingInfo->rightBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->rightBorder[dwRotationId][i];
        RotateVertexAroundCenter(
            pVertex->uv,
            m_pPackingInfo->pVertUV[pVertex->dwID],
            m_pPackingInfo->fUVWidth[0]/2,
            m_pPackingInfo->fUVHeight[0]/2,
            fSin,
            fCos);
    }
    for (size_t i=0; i<m_pPackingInfo->leftBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->leftBorder[dwRotationId][i];
        RotateVertexAroundCenter(
            pVertex->uv,
            m_pPackingInfo->pVertUV[pVertex->dwID],
            m_pPackingInfo->fUVWidth[0]/2,
            m_pPackingInfo->fUVHeight[0]/2,
            fSin,
            fCos);
    }

    XMFLOAT2 minVector(FLT_MAX, FLT_MAX);
    XMFLOAT2 maxVector(-FLT_MAX, -FLT_MAX);

    for (size_t i=0; i<m_pPackingInfo->topBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->topBorder[dwRotationId][i];
        UpdateMinMaxVertex(pVertex->uv, minVector, maxVector);
    }

    for (size_t i=0; i<m_pPackingInfo->bottomBorder[dwRotationId].size(); i++)
    {
        pVertex = m_pPackingInfo->bottomBorder[dwRotationId][i];
        UpdateMinMaxVertex(pVertex->uv, minVector, maxVector);
    }

    pVertex = m_pVerts;
    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        if (pVertex->bIsBoundary)
        {
            pVertex->uv.x -= minVector.x;
            pVertex->uv.y -= minVector.y;
        }
        pVertex++;
    }
}


// Normalize atlas
void CIsochartMesh::NormalizeAtlas(
    ISOCHARTMESH_ARRAY& chartList,
    ATLASINFO& atlasInfo)
{
    float fScaleW = 0;
    float fScaleH = 0;

    if (atlasInfo.fBoxRight - atlasInfo.fBoxLeft >=
        (atlasInfo.fBoxTop - atlasInfo.fBoxBottom)*atlasInfo.fWidthHeightRatio)
    {
        fScaleW = atlasInfo.fBoxRight - atlasInfo.fBoxLeft;
        fScaleH = fScaleW / atlasInfo.fWidthHeightRatio;
    }
    else
    {
        fScaleH = atlasInfo.fBoxTop - atlasInfo.fBoxBottom;
        fScaleW = fScaleH * atlasInfo.fWidthHeightRatio;
    }

    if (IsInZeroRange(fScaleW) || IsInZeroRange(fScaleH))
    {
        return;
    }
 
    for (size_t i=0; i<chartList.size(); i++)
    {
        CIsochartMesh* pChart = chartList[i];
        ISOCHARTVERTEX* pVertex = pChart->m_pVerts;
        for (size_t j=0; j<pChart->m_dwVertNumber; j++)
        {
            pVertex->uv.x = (pVertex->uv.x - atlasInfo.fBoxLeft) / fScaleW;
            pVertex->uv.y = (pVertex->uv.y - atlasInfo.fBoxBottom)/ fScaleH;

            assert(_finite(pVertex->uv.x));
            assert(_finite(pVertex->uv.y));

            if (pVertex->uv.x < 0.0f)
            {
                pVertex->uv.x = 0.0f;
         }
            if (pVertex->uv.x > 1.0f)
            {
                pVertex->uv.x = 1.0f;
            }
            if (pVertex->uv.y < 0.0f)
            {
                pVertex->uv.y = 0.0f;
         }
            if (pVertex->uv.y > 1.0f)
            {
                pVertex->uv.y = 1.0f;
            }
            pVertex++;
        }
    }
}

// Scale chart UV-area to original 3D area
void CIsochartMesh::ScaleTo3DArea()
{
    m_fChart2DArea = CalculateChart2DArea();
    if (IsInZeroRange(m_fChart2DArea))
    {
        return;
    }

    float fSurfaceArea = CalculateChart3DArea();
    float fScale = IsochartSqrtf(fSurfaceArea / m_fChart2DArea);

    ScaleChart(fScale);
}

// Scale each chart to get the smallest stretch
// See more details of algorithm in [SGSH02]:4.6 section
void CIsochartMesh::OptimizeAtlasSignalStretch(
    ISOCHARTMESH_ARRAY& chartList)
{
    if (chartList.size() < 2)
    {
        return;
    }

    const float ShiftError = 1e-4f;

    float fTotal2DArea = 0;
    float fTotal = 0;

    for (size_t i=0; i<chartList.size(); i++)
    {
        CIsochartMesh* pChart = chartList[i];
        pChart->m_fChart2DArea = pChart->CalculateChart2DArea();
        fTotal2DArea += pChart->m_fChart2DArea;

        assert(_finite(pChart->m_fParamStretchL2) != 0);

        fTotal += IsochartSqrtf(
            (pChart->m_fParamStretchL2+ShiftError)*pChart->m_fChart2DArea);
    }
        
    if (IsInZeroRange(fTotal))
    {
        return;
    }

    float fScale2 = 0;

    for (size_t i=0; i<chartList.size(); i++)
    {
        CIsochartMesh* pChart = chartList[i];
        float fScale;

        if (IsInZeroRange(pChart->m_fChart2DArea))
        {
            fScale = 1;
        }
        else
        {
            fScale = IsochartSqrtf(
                (pChart->m_fParamStretchL2+ShiftError)/pChart->m_fChart2DArea)
                / fTotal;
            fScale *= fTotal2DArea;
            fScale2 = fScale;
            fScale = IsochartSqrtf(fScale);
        }

        pChart->ScaleChart(fScale);

        assert(_finite(pChart->m_fParamStretchL2) != 0);
    }
}

// Scale chart. If signal specified parametrization, need to adjust stretch
// of chart too.
void CIsochartMesh::ScaleChart(float fScale)
{
    ISOCHARTVERTEX* pVertex = m_pVerts;

    if (IsInZeroRange(fScale-1.0f))
    {
    return;
    }

    for (size_t i=0; i<m_dwVertNumber; i++)
    {
        pVertex->uv.x *= fScale;
        pVertex->uv.y *= fScale;
        pVertex++;
    }

    m_fChart2DArea *=  (fScale*fScale);
    if (!IsInZeroRange(fScale*fScale))
    {
        m_fParamStretchL2 /= (fScale*fScale);
        m_fParamStretchLn = m_fParamStretchL2;
    }
}

