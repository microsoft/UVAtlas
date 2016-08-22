//-------------------------------------------------------------------------------------
// UVAtlas - mathutils.h
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

#include "datatypes.h"

#define SQN(x) (x>=0 ? 1 : -1)

#define SQR(x) ((x) * (x))

#define SquredD3Dist(pt1, pt2) ( SQR((pt1).x-(pt2).x) + SQR((pt1).y-(pt2).y) + SQR((pt1).z-(pt2).z) )

#define SquredD2Dist(pt1, pt2) ( SQR((pt1).x-(pt2).x) + SQR((pt1).y-(pt2).y) )

namespace GeodesicDist
{
    double SqrtWithAssert(const double &x) ;

    double SqrtMin0(const double &x) ;

    double ComputeVertexAngleOnFace(const Face &face, const uint32_t dwVertexIdx);

    inline void ComputeSrcPtFromb0b1d0d1( const double &b0,
                                  const double &b1,
                                  const double &d0,
                                  const double &d1,
                                  DVector2 &res ) 
    {
        res.x = (-SQR(d0) + SQR(d1) + SQR(b0) - SQR(b1)) / (2 * (b0 - b1)) ;
        res.y = SqrtMin0( SQR(d0) - SQR(res.x-b0) ) ;
    }

    void ParameterizePt3ToPt2( const DVector3 &v3Origin, 
                              const DVector3 &v3OnePositivePt, 
                              const DVector3 &v3Pt,
                              DVector2 &ptRes ) ;

    void ParameterizePt2ToPt2( const DVector2 &v2Origin,
                              const DVector2 &v2OnePositivePt,
                              const DVector2 &v2Pt,
                              DVector2 &ptRes );

    void GetCommonPointOf2Lines( const DVector2 &pt1Line1, const DVector2 &pt2Line1,
                                const DVector2 &pt1Line2, const DVector2 &pt2Line2,
                                DVector2 &ptResult, bool &bResultPtWithinLineSeg1 ) ;

    void ComputePtOnLineWithDistance( const DVector3 &v3Pt1, 
                                     const DVector3 &v3Pt2, 
                                     const double &dDistanceAwayFromPt1,  
                                     DVector3 &v3Result ) ;

    double ComputeAngleBetween2Lines( const DVector3 &v3PtCommon,
                                     const DVector3 &v3Pt1,
                                     const DVector3 &v3Pt2 ) ;

}
