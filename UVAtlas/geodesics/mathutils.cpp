//-------------------------------------------------------------------------------------
// UVAtlas - mathutils.cpp
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
#include "mathutils.h"

using namespace GeodesicDist;

double GeodesicDist::SqrtWithAssert(const double &x)
{ 
    assert(x>=0);  
    return sqrt(x) ;
}

double GeodesicDist::SqrtMin0(const double &x)
{
    return sqrt(std::max<double>(x, 0));
}

double GeodesicDist::ComputeVertexAngleOnFace(const Face &face, const uint32_t dwVertexIdx)
{
    uint32_t dwOpposingEdge = face.GetOpposingEdgeIdx(dwVertexIdx);

    Edge *pEdge1, *pEdge2 ;
    face.GetOtherTwoEdges( dwOpposingEdge, &pEdge1, &pEdge2 ) ;

    Vertex *pThisVertex = pEdge1->GetVertexByIdx( dwVertexIdx ) ;
    Vertex *pEndVertex1 = pEdge1->GetAnotherVertex( dwVertexIdx ) ;
    Vertex *pEndVertex2 = pEdge2->GetAnotherVertex( dwVertexIdx ) ;

    return ComputeAngleBetween2Lines( *pThisVertex, *pEndVertex1, *pEndVertex2 ) ;    
}

void GeodesicDist::ParameterizePt3ToPt2(const DVector3 &v3Origin,
                           const DVector3 &v3OnePositivePt, 
                           const DVector3 &v3Pt,
                           DVector2 &ptRes )
{
    DVector3 P; 
    DVector3Minus( v3Pt, v3Origin, P ) ;
    
    DVector3 Q; 
    DVector3Minus( v3OnePositivePt, v3Origin, Q ) ;
    
    double lengthQ = Q.Length() ;
    
    DVector3 PCrossQ ; 
    DVector3Cross( P, Q, PCrossQ ) ;

    ptRes.x = DVector3Dot( P, Q ) / lengthQ ;
    ptRes.y = PCrossQ.Length() / lengthQ ;
}

void GeodesicDist::ParameterizePt2ToPt2(const DVector2 &v2Origin,
                           const DVector2 &v2OnePositivePt,
                           const DVector2 &v2Pt,
                           DVector2 &ptRes )
{
    DVector2 P; 
    DVector2Minus( v2Pt, v2Origin, P ) ;

    DVector2 Q; 
    DVector2Minus( v2OnePositivePt, v2Origin, Q ) ;

    double lengthQ = Q.Length() ;    

    ptRes.x = DVector2Dot( P, Q ) / lengthQ ;
    ptRes.y = DVector2CrossModulus( P, Q ) / lengthQ ;
}

void GeodesicDist::GetCommonPointOf2Lines(const DVector2 &pt1Line1,
                             const DVector2 &pt2Line1,
                             const DVector2 &pt1Line2, 
                             const DVector2 &pt2Line2,
                             DVector2 &ptResult, 
                             bool &bResultPtWithinLineSeg1 )
{
    double d = (pt1Line1.y-pt2Line1.y)*(pt1Line2.x-pt2Line2.x)-(pt1Line1.x-pt2Line1.x)*(pt1Line2.y-pt2Line2.y) ;

    // test if these two lines are parallel
    if ( fabs(d) < FLT_EPSILON )
    {
        ptResult.x = FLT_MAX ;
        ptResult.y = FLT_MAX ;

        bResultPtWithinLineSeg1 = false ;

        return ;
    }

    double t = (-pt1Line2.y*pt2Line2.x+pt1Line1.y*(-pt1Line2.x+pt2Line2.x)+pt1Line1.x*(pt1Line2.y-pt2Line2.y)+pt1Line2.x*pt2Line2.y) / -d ;

    ptResult.x = (pt2Line1.x-pt1Line1.x)*t+pt1Line1.x ;
    ptResult.y = (pt2Line1.y-pt1Line1.y)*t+pt1Line1.y ;

    bResultPtWithinLineSeg1 = (t >= 0 && t <= 1.0) ;
}

void GeodesicDist::ComputePtOnLineWithDistance(const DVector3 &v3Pt1,
                                  const DVector3 &v3Pt2, 
                                  const double &dDistanceAwayFromPt1,  
                                  DVector3 &v3Result )
{
    DVector3 tmp ;

    DVector3Minus( v3Pt2, v3Pt1, tmp ) ;
    DVector3ScalarMul( tmp, 1/tmp.Length()*dDistanceAwayFromPt1 ) ;
    DVector3Add( tmp, v3Pt1, v3Result ) ;    
}

double GeodesicDist::ComputeAngleBetween2Lines(const DVector3 &v3PtCommon,
                                  const DVector3 &v3Pt1,
                                  const DVector3 &v3Pt2 )
{
    DVector3 P ;
    DVector3 Q ;

    DVector3Minus( v3Pt1, v3PtCommon, P ) ;
    DVector3Minus( v3Pt2, v3PtCommon, Q ) ;

    return acos( DVector3Dot( P, Q ) / ( P.Length() * Q.Length() ) ) ;
}
