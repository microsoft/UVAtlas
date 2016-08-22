//-------------------------------------------------------------------------------------
// UVAtlas - ExactOneToAll.cpp
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
#include "ExactOneToAll.h"
#include "datatypes.h"
#include "mathutils.h"

using namespace GeodesicDist;

CExactOneToAll::CExactOneToAll()
{
    m_EdgeWindowsHeap.SetManageMode( Isochart::AUTOMATIC ) ;
}

void CExactOneToAll::SetSrcVertexIdx( const uint32_t dwSrcVertexIdx )
{
    m_dwSrcVertexIdx = dwSrcVertexIdx ;
    
    while ( !m_EdgeWindowsHeap.empty() )
    {
        delete m_EdgeWindowsHeap.cutTop() ;
    }

    for (size_t i = 0; i < m_VertexList.size(); ++i)
    {
        m_VertexList[i].dGeoDistanceToSrc = FLT_MAX ;
        m_VertexList[i].dLengthOfWindowEdgeToThisVertex = FLT_MAX ;
        m_VertexList[i].pEdgeReportedGeoDist = nullptr ;
        m_VertexList[i].bShadowBoundary = false ;
    }
    
    for (uint32_t i = 0; i < m_EdgeList.size(); ++i)
    {       
        Edge &thisEdge = m_EdgeList[i] ;

        thisEdge.WindowsList.clear() ;

        if ( !thisEdge.HasVertexIdx(dwSrcVertexIdx) && 
             (
                (thisEdge.pAdjFace0 && thisEdge.pAdjFace0->HasVertexIdx(dwSrcVertexIdx) )
                || 
                (thisEdge.pAdjFace1 && thisEdge.pAdjFace1->HasVertexIdx(dwSrcVertexIdx) ) 
             )
           )
        {            
            EdgeWindow tmpEdgeWindow ;

            // generate a window covering the whole edge as one of the initial windows
            tmpEdgeWindow.SetEdgeIdx( m_EdgeList, i ) ;
            tmpEdgeWindow.dPseuSrcToSrcDistance = 0 ;
            tmpEdgeWindow.b0 = 0 ;
            tmpEdgeWindow.b1 = tmpEdgeWindow.pEdge->dEdgeLength ;
            tmpEdgeWindow.d0 = sqrt( SquredD3Dist( *tmpEdgeWindow.pEdge->pVertex0, m_VertexList[dwSrcVertexIdx] ) ) ;
            tmpEdgeWindow.d1 = sqrt( SquredD3Dist( *tmpEdgeWindow.pEdge->pVertex1, m_VertexList[dwSrcVertexIdx] ) ) ;
            ParameterizePt3ToPt2( *tmpEdgeWindow.pEdge->pVertex0, *tmpEdgeWindow.pEdge->pVertex1, m_VertexList[dwSrcVertexIdx], tmpEdgeWindow.dv2Src ) ;
            tmpEdgeWindow.SetPseuSrcVertexIdx( m_VertexList, dwSrcVertexIdx ) ;
            tmpEdgeWindow.SetMarkFromEdgeVertexIdx( m_VertexList, tmpEdgeWindow.pEdge->dwVertexIdx0 ) ;
            if ( tmpEdgeWindow.pEdge->pAdjFace0->HasVertexIdx(dwSrcVertexIdx) )
                tmpEdgeWindow.SetFaceIdxPropagatedFrom( m_FaceList, tmpEdgeWindow.pEdge->dwAdjFaceIdx0 ) ;
            else
                tmpEdgeWindow.SetFaceIdxPropagatedFrom( m_FaceList, tmpEdgeWindow.pEdge->dwAdjFaceIdx1 ) ;
            
            AddWindowToHeapAndEdge( tmpEdgeWindow ) ;
        }
    }

    m_VertexList[m_dwSrcVertexIdx].dGeoDistanceToSrc = 0 ;
}

void CExactOneToAll::AddWindowToHeapAndEdge( const EdgeWindow &WindowToAdd )
{
    // add the new window to heap and the edge
    TypeEdgeWindowsHeap::item_type *pItem = 
        new TypeEdgeWindowsHeap::item_type(std::min(WindowToAdd.d0, WindowToAdd.d1) + WindowToAdd.dPseuSrcToSrcDistance, WindowToAdd);

    m_EdgeWindowsHeap.insert( pItem ) ;
    WindowToAdd.pEdge->WindowsList.push_back(Edge::WindowListElement(pItem, WindowToAdd));

    // update the geodesic distance on vertices affected by this new window
    WindowToAdd.pMarkFromEdgeVertex->dGeoDistanceToSrc = 
        std::min(WindowToAdd.pMarkFromEdgeVertex->dGeoDistanceToSrc, WindowToAdd.d0 + WindowToAdd.dPseuSrcToSrcDistance);
    WindowToAdd.pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex = 0 ;
    if ( WindowToAdd.pMarkFromEdgeVertex->dGeoDistanceToSrc == (WindowToAdd.d0 + WindowToAdd.dPseuSrcToSrcDistance) )
    {
        WindowToAdd.pMarkFromEdgeVertex->pEdgeReportedGeoDist = WindowToAdd.pEdge ;
    }

    WindowToAdd.pEdge->GetAnotherVertex( WindowToAdd.dwMarkFromEdgeVertexIdx )->dGeoDistanceToSrc =
        std::min(WindowToAdd.pEdge->GetAnotherVertex(WindowToAdd.dwMarkFromEdgeVertexIdx)->dGeoDistanceToSrc, WindowToAdd.d1 + WindowToAdd.dPseuSrcToSrcDistance);
    WindowToAdd.pEdge->GetAnotherVertex( WindowToAdd.dwMarkFromEdgeVertexIdx )->dLengthOfWindowEdgeToThisVertex = 0 ;
    if ( WindowToAdd.pEdge->GetAnotherVertex( WindowToAdd.dwMarkFromEdgeVertexIdx )->dGeoDistanceToSrc == (WindowToAdd.d1 + WindowToAdd.dPseuSrcToSrcDistance) )
    {
        WindowToAdd.pEdge->GetAnotherVertex( WindowToAdd.dwMarkFromEdgeVertexIdx )->pEdgeReportedGeoDist = WindowToAdd.pEdge ;
    }
}

// pop off one window from the heap and unreference the corresponding one on the edge
void CExactOneToAll::CutHeapTopData( EdgeWindow &EdgeWindowOut )
{
    TypeEdgeWindowsHeap::item_type *pItem = m_EdgeWindowsHeap.cutTop() ;       

    for (size_t i = 0; i < pItem->m_data.pEdge->WindowsList.size(); ++i)
        if ( pItem->m_data.pEdge->WindowsList[i].pHeapItem == pItem )
        {
            pItem->m_data.pEdge->WindowsList[i].pHeapItem = nullptr ;
            break ;
        }

    EdgeWindowOut = pItem->m_data ;

    delete pItem ;
}

void CExactOneToAll::Run()
{
    InternalRun();
}

void CExactOneToAll::InternalRun()
{    
    DVector2 w0, w1, w2, e0, e1, e2 ;
    uint32_t dwFacePropagateTo, dwThirdPtIdxOnFacePropagateTo, dwEdgeIdxPropagateTo0, dwEdgeIdxPropagateTo1, dwPtE1Idx ;
    Face *pFacePropageteTo ;
    Edge *pEdge0, *pEdge1 ; 
    Vertex *pThridPtOnFacePropagateTo, *pPtE1 ;
    DVector2 w0_to_e0_e2, w0_to_e1_e2, w1_to_e0_e2, w1_to_e1_e2 ;
    bool bW2W0OnE0E2, bW2W0OnE1E2, bW2W1OnE0E2, bW2W1OnE1E2 ;

    // the main propagation loop
    while ( !m_EdgeWindowsHeap.empty() )
    {
        tmpWindow0.dwEdgeIdx = FLAG_INVALIDDWORD ;
        
        CutHeapTopData( WindowToBePropagated ) ;

        if ( !WindowToBePropagated.pEdge->pAdjFace0 || !WindowToBePropagated.pEdge->pAdjFace1 )
        {
            // this is a boundary edge, no need to propagate
            continue ;
        }

        if ( fabs(WindowToBePropagated.b0 - WindowToBePropagated.b1) <= FLT_EPSILON )
        {            
            // we don't process these too small windows
            continue ;
        }

        //pPtE0 = WindowToBePropagated.pMarkFromEdgeVertex;

        dwFacePropagateTo = WindowToBePropagated.pEdge->GetAnotherFaceIdx( WindowToBePropagated.dwFaceIdxPropagatedFrom ) ;
        pFacePropageteTo = &m_FaceList[dwFacePropagateTo] ;

        pFacePropageteTo->GetOtherTwoEdges( WindowToBePropagated.dwEdgeIdx, &pEdge0, &pEdge1 ) ;
        pFacePropageteTo->GetOtherTwoEdgesIdx( WindowToBePropagated.dwEdgeIdx, dwEdgeIdxPropagateTo0, dwEdgeIdxPropagateTo1 ) ;
        if ( !pEdge0->HasVertexIdx(WindowToBePropagated.dwMarkFromEdgeVertexIdx) )
        {
            std::swap( pEdge0, pEdge1 ) ;
            std::swap(dwEdgeIdxPropagateTo0, dwEdgeIdxPropagateTo1);
        }

        dwThirdPtIdxOnFacePropagateTo = pEdge0->GetAnotherVertexIdx( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;
        pThridPtOnFacePropagateTo = &m_VertexList[dwThirdPtIdxOnFacePropagateTo] ;

        dwPtE1Idx = WindowToBePropagated.pEdge->GetAnotherVertexIdx( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;
        pPtE1 = &m_VertexList[dwPtE1Idx] ;

        w0.x = WindowToBePropagated.b0 ;
        w0.y = 0 ;
        w1.x = WindowToBePropagated.b1 ;
        w1.y = 0 ;
        
        // the parameterized pseudo source w2 may be computed by b0, b1, d1 and d0
        //ComputeSrcPtFromb0b1d0d1( WindowToBePropagated.b0, WindowToBePropagated.b1, WindowToBePropagated.d0, WindowToBePropagated.d1, w2 ) ;

        // however, in this improved version, it is stored directly
        w2 = WindowToBePropagated.dv2Src ;

        e0.x = e0.y = 0 ;
        e1.x = WindowToBePropagated.pEdge->dEdgeLength ;
        e1.y = 0 ;
        if ( w1.x > e1.x )
        {
            w1.x = e1.x ;
        }
        ParameterizePt3ToPt2( *WindowToBePropagated.pMarkFromEdgeVertex,
            *pPtE1, 
            *pThridPtOnFacePropagateTo, e2 ) ;
        e2.y = -e2.y ;

        GetCommonPointOf2Lines( e0, e2, w2, w0, w0_to_e0_e2, bW2W0OnE0E2 ) ;
        if ( w0_to_e0_e2.x == FLT_MAX && fabs(w0.x-e0.x) < FLT_EPSILON )
        {
            bW2W0OnE0E2 = true ;
            w0_to_e0_e2 = e0 ;
        }

        GetCommonPointOf2Lines( e1, e2, w2, w0, w0_to_e1_e2, bW2W0OnE1E2 ) ;        
        GetCommonPointOf2Lines( e0, e2, w2, w1, w1_to_e0_e2, bW2W1OnE0E2 ) ;        

        GetCommonPointOf2Lines( e1, e2, w2, w1, w1_to_e1_e2, bW2W1OnE1E2 ) ;      
        if ( w1_to_e1_e2.x == FLT_MAX && fabs(e1.x - w1.x) < FLT_EPSILON )
        {
            bW2W1OnE1E2 = true ;
            w1_to_e1_e2 = e1 ;
        }

        // this is the first figure shown
        if ( bW2W0OnE0E2 && bW2W1OnE1E2 && !bW2W1OnE0E2 && !bW2W0OnE1E2 )
        {
            // the first possible new window
            if ( (tmpWindow0.b1 = sqrt( SquredD2Dist( w0_to_e0_e2, e2 ) )) > FLT_EPSILON )
            {            
                if ( w0.x == e0.x )
                    tmpWindow0.b1 = pEdge0->dEdgeLength ;

                tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwPseuSrcVertexIdx ) ;                
                tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo0 ) ;                
                tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance ;
                tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;
                tmpWindow0.b0 = 0 ;
                tmpWindow0.d0 = sqrt( SquredD2Dist( w2, e2) ) ;
                if ( w0.x == e0.x )
                {
                    tmpWindow0.d1 = sqrt( SquredD2Dist( e0, w2 ) ) ;                    
                } else
                {                
                    tmpWindow0.d1 = sqrt( SquredD2Dist( w0_to_e0_e2, w2 ) ) ;                    
                }
                ParameterizePt2ToPt2( e2, e0, w2, tmpWindow0.dv2Src ) ;
                tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;
                
                tmpWindow0.ksi = WindowToBePropagated.ksi ;
                tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

                if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                {                
                    ProcessNewWindow( &tmpWindow0 ) ;
                }                
            }

            // the second possible new window
            if ( (tmpWindow0.b1 = sqrt( SquredD2Dist( w1_to_e1_e2, e2 ) )) > FLT_EPSILON )
            {            
                if ( w1.x == e1.x )
                    tmpWindow0.b1 = pEdge1->dEdgeLength ;

                tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwPseuSrcVertexIdx ) ;                
                tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo1 ) ;                
                tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance ;
                tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;                
                tmpWindow0.b0 = 0 ;             
                tmpWindow0.d0 = sqrt( SquredD2Dist( w2, e2) ) ;
                if ( w1.x == e1.x )
                {
                    tmpWindow0.d1 = sqrt( SquredD2Dist( e1, w2 ) ) ;                    
                } else
                {                
                    tmpWindow0.d1 = sqrt( SquredD2Dist( w1_to_e1_e2, w2 ) ) ;                    
                }
                ParameterizePt2ToPt2( e2, e1, w2, tmpWindow0.dv2Src ) ;
                tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;
                
                tmpWindow0.ksi = WindowToBePropagated.ksi ;
                tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

                if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                { 
                    ProcessNewWindow( &tmpWindow0 ) ;
                }
            }
        } else

        // this is the second figure shown in the mail
        if ( bW2W0OnE1E2 && bW2W1OnE1E2 )
        {
            tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwPseuSrcVertexIdx ) ;            
            tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo1 ) ;            
            tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;            
            tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance ;
            tmpWindow0.b0 = sqrt( SquredD2Dist( w0_to_e1_e2, e2 ) ) ;
            if ( tmpWindow0.b0 < FLT_EPSILON )
            {
                tmpWindow0.b0 = 0 ;
            }
            if ( w1.x == e1.x )
            {
                tmpWindow0.b1 = pEdge1->dEdgeLength ;  //SqrtWithAssert( SquredD2Dist( e1, e2 ) ) ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( e1, w2 ) ) ;                
            } else
            {            
                tmpWindow0.b1 = sqrt( SquredD2Dist( w1_to_e1_e2, e2 ) ) ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( w1_to_e1_e2, w2 ) ) ;                
            }
            ParameterizePt2ToPt2( e2, e1, w2, tmpWindow0.dv2Src ) ;
            tmpWindow0.d0 = sqrt( SquredD2Dist( w0_to_e1_e2, w2 ) ) ;            
            tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;
            
            tmpWindow0.ksi = WindowToBePropagated.ksi ;
            tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

            if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
            { 
                ProcessNewWindow( &tmpWindow0 ) ; 
            }

            if ( w0.x == e0.x && WindowToBePropagated.pMarkFromEdgeVertex->IsSaddleBoundary() )
            {
                tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;                
                tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo1 ) ;                
                tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance + WindowToBePropagated.d0 ;
                tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;                
                tmpWindow0.b0 = 0 ;
                tmpWindow0.b1 = sqrt( SquredD2Dist( w0_to_e1_e2, e2 ) ) ;
                tmpWindow0.d0 = pEdge0->dEdgeLength ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( w0_to_e1_e2, e0 ) ) ;
                tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;  
                //ParameterizePt2ToPt2( e2, e1, e0, tmpWindow0.dv2Src ) ;
                ParameterizePt3ToPt2( *pThridPtOnFacePropagateTo, *pPtE1, *WindowToBePropagated.pMarkFromEdgeVertex, tmpWindow0.dv2Src ) ;
                
                tmpWindow0.ksi = WindowToBePropagated.ksi ;
                tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

                if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                {                   
                    ProcessNewWindow( &tmpWindow0 ) ;
                }

                // process the "saddle shadow" (an uncovered issue in the paper)
                if ( !pThridPtOnFacePropagateTo->bShadowBoundary && !pEdge0->IsBoundary() )
                {
                    pThridPtOnFacePropagateTo->bShadowBoundary = true ;

                    Edge *pBridgeEdge = pEdge0 ;
                    Face *pShadowFace = pBridgeEdge->GetAnotherFace( dwFacePropagateTo ) ;
                    uint32_t dwShadowFace = pBridgeEdge->GetAnotherFaceIdx( dwFacePropagateTo ) ;
                    Edge *pShadowEdge = pShadowFace->GetOpposingEdge( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;
                    uint32_t dwShadowEdge = pShadowFace->GetOpposingEdgeIdx( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;

                    uint32_t dwThisShadowVertex = dwThirdPtIdxOnFacePropagateTo ;
                    Vertex *pNextShadowVertex = pShadowEdge->GetAnotherVertex( dwThirdPtIdxOnFacePropagateTo ) ;

                    std::vector<uint32_t> shadowEdges;
                    std::vector<uint32_t> shadowFaces;
                    for (;;)
                    {
                        shadowEdges.push_back(dwShadowEdge);
                        shadowFaces.push_back(dwShadowFace);

                        if ( pNextShadowVertex == pThridPtOnFacePropagateTo || 
                             pNextShadowVertex->bShadowBoundary /*||
                             pBridgeEdge->IsBoundary()*/
                           )
                            break ;

                        pBridgeEdge = pShadowFace->GetOpposingEdge( dwThisShadowVertex ) ;
                        if ( pBridgeEdge->IsBoundary() )
                            break ;

                        dwThisShadowVertex = pShadowEdge->GetAnotherVertexIdx( dwThisShadowVertex ) ;
                        pShadowFace = pBridgeEdge->GetAnotherFace( pShadowFace ) ;
                        dwShadowFace = pBridgeEdge->GetAnotherFaceIdx( dwShadowFace ) ;
                        pShadowEdge = pShadowFace->GetOpposingEdge( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ; 
                        dwShadowEdge = pShadowFace->GetOpposingEdgeIdx( WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ; 
                        pNextShadowVertex = pShadowEdge->GetAnotherVertex( dwThisShadowVertex ) ;
                    }

                    if ( pNextShadowVertex != pThridPtOnFacePropagateTo && 
                         (pNextShadowVertex->bShadowBoundary || pBridgeEdge->IsBoundary())
                       )
                    {
                        for (size_t v = 0; v < shadowEdges.size(); ++v)
                        {                                                        
                            //EdgeWindow newWindow ;

                            tmpWindow0.SetEdgeIdx( m_EdgeList, shadowEdges[v] ) ;
                            tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, shadowFaces[v] ) ;
                            tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, tmpWindow0.pEdge->dwVertexIdx0 ) ;
                            tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwMarkFromEdgeVertexIdx ) ;
                            tmpWindow0.b0 = 0 ;
                            tmpWindow0.b1 = tmpWindow0.pEdge->dEdgeLength ;
                            tmpWindow0.d0 = sqrt( SquredD3Dist(*tmpWindow0.pEdge->pVertex0, *WindowToBePropagated.pMarkFromEdgeVertex) ) ;
                            tmpWindow0.d1 = sqrt( SquredD3Dist(*tmpWindow0.pEdge->pVertex1, *WindowToBePropagated.pMarkFromEdgeVertex) ) ;
                            tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance + WindowToBePropagated.d0 ;
                            ParameterizePt3ToPt2( *tmpWindow0.pEdge->pVertex0, *tmpWindow0.pEdge->pVertex1, *tmpWindow0.pPseuSrcVertex, tmpWindow0.dv2Src ) ;
                            tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;   
                            tmpWindow0.ksi = WindowToBePropagated.ksi ;

                            if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                            {
                                ProcessNewWindow( &tmpWindow0 ) ;
                            }
                        }                                               
                    }
                }
            }


        }
        else

        // this is the third figure shown in the mail
        if ( bW2W0OnE0E2 && bW2W1OnE0E2 )
        {
            tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, WindowToBePropagated.dwPseuSrcVertexIdx ) ;            
            tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo0 ) ;            
            tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;            
            tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance ;
            tmpWindow0.b0 = sqrt( SquredD2Dist( w1_to_e0_e2, e2 ) ) ;
            if ( tmpWindow0.b0 < FLT_EPSILON )
            {
                tmpWindow0.b0 = 0 ;
            }
            if ( w0.x == e0.x )
            {
                tmpWindow0.b1 = pEdge0->dEdgeLength ; //SqrtWithAssert( SquredD2Dist( e0, e2 ) ) ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( e0, w2 ) ) ;
            } else
            {            
                tmpWindow0.b1 = sqrt( SquredD2Dist( w0_to_e0_e2, e2 ) ) ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( w0_to_e0_e2, w2 ) ) ;
            }
            ParameterizePt2ToPt2( e2, e0, w2, tmpWindow0.dv2Src ) ;
            tmpWindow0.d0 = sqrt( SquredD2Dist( w1_to_e0_e2, w2 ) ) ;            
            tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;
            
            tmpWindow0.ksi = WindowToBePropagated.ksi ;
            tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

            if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
            { 
                ProcessNewWindow( &tmpWindow0 ) ;
            }

            if ( w1.x == e1.x && pPtE1->IsSaddleBoundary() )
            {
                tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, dwPtE1Idx ) ;                
                tmpWindow0.SetEdgeIdx( m_EdgeList, dwEdgeIdxPropagateTo0 ) ;                
                tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, dwFacePropagateTo ) ;                
                tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance + WindowToBePropagated.d1 ;
                tmpWindow0.b0 = 0 ;
                tmpWindow0.b1 = sqrt( SquredD2Dist( w1_to_e0_e2, e2 ) ) ;
                tmpWindow0.d0 = pEdge1->dEdgeLength ;
                tmpWindow0.d1 = sqrt( SquredD2Dist( w1_to_e0_e2, e1 ) ) ;
                tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, dwThirdPtIdxOnFacePropagateTo ) ;  
                ParameterizePt3ToPt2( *pThridPtOnFacePropagateTo, *WindowToBePropagated.pMarkFromEdgeVertex, *pPtE1, tmpWindow0.dv2Src ) ;
                
                tmpWindow0.ksi = WindowToBePropagated.ksi ;
                tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ;

                if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                {                    
                    ProcessNewWindow( &tmpWindow0 ) ;
                }

                // process the "saddle shadow" (an uncovered issue in the paper)
                if ( !pThridPtOnFacePropagateTo->bShadowBoundary && !pEdge1->IsBoundary() )
                {
                    pThridPtOnFacePropagateTo->bShadowBoundary = true ;

                    Edge *pBridgeEdge = pEdge1 ;
                    Face *pShadowFace = pBridgeEdge->GetAnotherFace( dwFacePropagateTo ) ;
                    uint32_t dwShadowFace = pBridgeEdge->GetAnotherFaceIdx( dwFacePropagateTo ) ;
                    uint32_t dwE1 = WindowToBePropagated.pEdge->GetAnotherVertexIdx(WindowToBePropagated.dwMarkFromEdgeVertexIdx) ;
                    Edge *pShadowEdge = pShadowFace->GetOpposingEdge( dwE1 ) ;
                    uint32_t dwShadowEdge = pShadowFace->GetOpposingEdgeIdx( dwE1 ) ;

                    uint32_t dwThisShadowVertex = dwThirdPtIdxOnFacePropagateTo ;
                    Vertex *pNextShadowVertex = pShadowEdge->GetAnotherVertex( dwThirdPtIdxOnFacePropagateTo ) ;

                    std::vector<uint32_t> shadowEdges;
                    std::vector<uint32_t> shadowFaces;
                    for (;;)
                    {
                        shadowEdges.push_back(dwShadowEdge);
                        shadowFaces.push_back(dwShadowFace);

                        if ( pNextShadowVertex == pThridPtOnFacePropagateTo || 
                             pNextShadowVertex->bShadowBoundary /*||
                             pBridgeEdge->IsBoundary()*/
                           )
                            break ;

                        pBridgeEdge = pShadowFace->GetOpposingEdge( dwThisShadowVertex ) ;
                        if ( pBridgeEdge->IsBoundary() )
                            break ;

                        dwThisShadowVertex = pShadowEdge->GetAnotherVertexIdx( dwThisShadowVertex ) ;
                        pShadowFace = pBridgeEdge->GetAnotherFace( pShadowFace ) ;
                        dwShadowFace = pBridgeEdge->GetAnotherFaceIdx( dwShadowFace ) ;
                        pShadowEdge = pShadowFace->GetOpposingEdge( dwE1 ) ; 
                        dwShadowEdge = pShadowFace->GetOpposingEdgeIdx( dwE1 ) ; 
                        pNextShadowVertex = pShadowEdge->GetAnotherVertex( dwThisShadowVertex ) ;
                    }
        
                    if ( pNextShadowVertex != pThridPtOnFacePropagateTo && 
                         (pNextShadowVertex->bShadowBoundary || pBridgeEdge->IsBoundary())
                       )
                    {
                        for (size_t v = 0; v < shadowEdges.size(); ++v)
                        {                                                        
                            //EdgeWindow newWindow ;

                            tmpWindow0.SetEdgeIdx( m_EdgeList, shadowEdges[v] ) ;
                            tmpWindow0.SetFaceIdxPropagatedFrom( m_FaceList, shadowFaces[v] ) ;
                            tmpWindow0.SetMarkFromEdgeVertexIdx( m_VertexList, tmpWindow0.pEdge->dwVertexIdx0 ) ;
                            tmpWindow0.SetPseuSrcVertexIdx( m_VertexList, dwE1 ) ;
                            tmpWindow0.b0 = 0 ;
                            tmpWindow0.b1 = tmpWindow0.pEdge->dEdgeLength ;
                            tmpWindow0.d0 = sqrt( SquredD3Dist(*tmpWindow0.pEdge->pVertex0, m_VertexList[dwE1]) ) ;
                            tmpWindow0.d1 = sqrt( SquredD3Dist(*tmpWindow0.pEdge->pVertex1, m_VertexList[dwE1]) ) ;
                            tmpWindow0.dPseuSrcToSrcDistance = WindowToBePropagated.dPseuSrcToSrcDistance + WindowToBePropagated.d1 ;
                            ParameterizePt3ToPt2( *tmpWindow0.pEdge->pVertex0, *tmpWindow0.pEdge->pVertex1, *tmpWindow0.pPseuSrcVertex, tmpWindow0.dv2Src ) ;
                            tmpWindow0.pEdgePropagatedFrom = WindowToBePropagated.pEdge ; 
                            tmpWindow0.ksi = WindowToBePropagated.ksi ;

                            if ( tmpWindow0.b1 - tmpWindow0.b0 > FLT_EPSILON )
                            {
                                ProcessNewWindow( &tmpWindow0 ) ;
                            }                            
                        }                                               
                    }
                }
            }
        }           
    }
    
    for (size_t i = 0; i < m_VertexList.size(); ++i)
    {
        if ( m_VertexList[i].bUsed )
        {
            if ( m_VertexList[i].dGeoDistanceToSrc == FLT_MAX )
            {
                for (size_t j = 0; j < m_VertexList[i].edgesAdj.size(); ++j)
                {                    
                    Edge *pEdge ;                    
                    pEdge = m_VertexList[i].edgesAdj[j] ;                    
                        
                    for (size_t l = 0; l < pEdge->WindowsList.size(); ++l)
                    {
                        EdgeWindow &theWindow = pEdge->WindowsList[l].theWindow ;                                

                        if ( theWindow.dwMarkFromEdgeVertexIdx == i )
                        {
                            if ( theWindow.b0 < theWindow.pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex )
                            {
                                theWindow.pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex = theWindow.b0 ;
                                theWindow.pMarkFromEdgeVertex->dGeoDistanceToSrc = theWindow.d0 + theWindow.dPseuSrcToSrcDistance ;                                    
                            } else
                            if ( theWindow.b0 == theWindow.pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex )
                            {
                                theWindow.pMarkFromEdgeVertex->dGeoDistanceToSrc = 
                                    std::min(theWindow.pMarkFromEdgeVertex->dGeoDistanceToSrc, theWindow.d0 + theWindow.dPseuSrcToSrcDistance);
                            }

                            if ( theWindow.pMarkFromEdgeVertex->dGeoDistanceToSrc == (theWindow.d0 + theWindow.dPseuSrcToSrcDistance) )
                            {
                                theWindow.pMarkFromEdgeVertex->pEdgeReportedGeoDist = theWindow.pEdge ;
                            }
                        }
                        else
                        {
                            Vertex *pAnotherPt = &m_VertexList[i] ;
                            if ( theWindow.b1 > (theWindow.pEdge->dEdgeLength - pAnotherPt->dLengthOfWindowEdgeToThisVertex) )
                            {
                                pAnotherPt->dLengthOfWindowEdgeToThisVertex = theWindow.pEdge->dEdgeLength - theWindow.b1 ;
                                pAnotherPt->dGeoDistanceToSrc = theWindow.d1 + theWindow.dPseuSrcToSrcDistance ;
                            }
                            else if ( theWindow.b1 == (theWindow.pEdge->dEdgeLength - pAnotherPt->dLengthOfWindowEdgeToThisVertex) )
                            {
                                pAnotherPt->dGeoDistanceToSrc = 
                                    std::min(pAnotherPt->dGeoDistanceToSrc, theWindow.d1 + theWindow.dPseuSrcToSrcDistance);
                            }

                            if ( pAnotherPt->dGeoDistanceToSrc == theWindow.d1 + theWindow.dPseuSrcToSrcDistance )
                            {
                                pAnotherPt->pEdgeReportedGeoDist = theWindow.pEdge ;
                            }
                        }
                    }         
                }
            }
        }
    }
}

void CExactOneToAll::ProcessNewWindow( EdgeWindow *pNewEdgeWindow )
{
    std::vector<EdgeWindow> NewWindowsList;
    NewWindowsList.push_back(*pNewEdgeWindow);

    size_t j = 0;

    while ( j < NewWindowsList.size() )
    {
        pNewEdgeWindow = &NewWindowsList[j] ;

        size_t i;
        bool bExistingWindowChanged, bNewWindowChanged, bExistingWindowNotAvailable, bNewWindowNotAvailable ;
        EdgeWindow WindowToBeInserted ;

        bNewWindowNotAvailable = false ;

        for ( i = 0; i < pNewEdgeWindow->pEdge->WindowsList.size(); ++i )
        {        
            TypeEdgeWindowsHeap::item_type *pExistingWindowItem ;

            bExistingWindowChanged = false ;
            bNewWindowChanged = false ;
            bExistingWindowNotAvailable = false ;

            // get a copy of current window on edge
            pExistingWindowItem = 
                new TypeEdgeWindowsHeap::item_type( 
                std::min(pNewEdgeWindow->pEdge->WindowsList[i].theWindow.d0, pNewEdgeWindow->pEdge->WindowsList[i].theWindow.d1) + pNewEdgeWindow->pEdge->WindowsList[i].theWindow.dPseuSrcToSrcDistance,
                pNewEdgeWindow->pEdge->WindowsList[i].theWindow 
                ) ;

            // the copy of current window on edge is then tested with the new window for intersection
            // after this test, the copy is possibly changed
            IntersectWindow( &pExistingWindowItem->m_data, 
                pNewEdgeWindow, &bExistingWindowChanged, &bNewWindowChanged, &bExistingWindowNotAvailable, &bNewWindowNotAvailable ) ;        

            if ( m_NewExistingWindow.b1 - m_NewExistingWindow.b0 > 0 ) // m_NewExistingWindow is modified in IntersectWindow
                WindowToBeInserted = m_NewExistingWindow ;
            if ( m_AnotherNewWindow.b1 - m_AnotherNewWindow.b0 > 0 ) // m_AnotherNewWindow is modified in IntersectWindow
            {
                NewWindowsList.push_back(m_AnotherNewWindow);
                // new allocations may occur in the above add operation, so pNewEdgeWindow must be reset here
                pNewEdgeWindow = &NewWindowsList[j] ;
            }

            bool bDontDelete ;
            bDontDelete = false ;

            // after the intersection operation, if the existing window has been changed, 
            // remove the old one from the heap (if it is in heap) and update the one on edge
            if ( bExistingWindowChanged )
            {
                // whether the window is in heap
                if ( pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem )
                {
                    // get the item in heap and remove it from the heap
                    TypeEdgeWindowsHeap::item_type *pHeapItem = pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem ;

                    m_EdgeWindowsHeap.remove( pHeapItem ) ;
                    delete pHeapItem ;

                    // if the existing window still available (b0<b1), we insert the updated one into heap again
                    // and update the one on edge correspondingly
                    if ( !bExistingWindowNotAvailable )
                    {                
                        pNewEdgeWindow->pEdge->WindowsList[i].theWindow = pExistingWindowItem->m_data ;
                        pExistingWindowItem->m_weight = std::min(pExistingWindowItem->m_data.d0, pExistingWindowItem->m_data.d1) + pExistingWindowItem->m_data.dPseuSrcToSrcDistance;
                        m_EdgeWindowsHeap.insert( pExistingWindowItem ) ;
                        pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem = pExistingWindowItem ;                    
                        bDontDelete = true ;
                    } else
                    {
                        // we set a flag here, that this window on edge is to be removed
                        pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem = (TypeEdgeWindowsHeap::item_type*)FLAG_INVALID_SIZE_T ;
                    }
                } else
                {
                    // the window is not in heap, so we just update the one on edge
                    if ( !bExistingWindowNotAvailable )
                    {
                        pNewEdgeWindow->pEdge->WindowsList[i].theWindow = pExistingWindowItem->m_data ;
                    } else
                        pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem = (TypeEdgeWindowsHeap::item_type*)FLAG_INVALID_SIZE_T ;
                }                        
            }        

            if ( !bDontDelete )
                delete pExistingWindowItem ;

            // if the new window is already unavailable during this iteration, we break ;
            if ( bNewWindowNotAvailable )
                break ;
        }

        i = 0 ;
        while ( i < pNewEdgeWindow->pEdge->WindowsList.size() )
        {
            // test the remove flag set above, and erase the invalidated window from this edge
            if ( (size_t)(pNewEdgeWindow->pEdge->WindowsList[i].pHeapItem) == FLAG_INVALID_SIZE_T )
            {
                pNewEdgeWindow->pEdge->WindowsList.erase(pNewEdgeWindow->pEdge->WindowsList.begin() + i);
            }
            else
            {
                ++i ;
            }
        }

        if ( WindowToBeInserted.b1 - WindowToBeInserted.b0 > 0 )
        {
            TypeEdgeWindowsHeap::item_type *pNewWindowItem = 
                new TypeEdgeWindowsHeap::item_type(std::min(WindowToBeInserted.d0, WindowToBeInserted.d1)+WindowToBeInserted.dPseuSrcToSrcDistance, WindowToBeInserted);

            m_EdgeWindowsHeap.insert( pNewWindowItem ) ;

            WindowToBeInserted.pEdge->WindowsList.push_back(Edge::WindowListElement(pNewWindowItem, pNewWindowItem->m_data));

            // update the geodesic distance on vertices affected by this new window
            if ( WindowToBeInserted.b0 < 0.01 )
            {
                if ( (WindowToBeInserted.d0 + WindowToBeInserted.dPseuSrcToSrcDistance) < WindowToBeInserted.pMarkFromEdgeVertex->dGeoDistanceToSrc )
                {
                    WindowToBeInserted.pMarkFromEdgeVertex->dGeoDistanceToSrc = WindowToBeInserted.d0 + WindowToBeInserted.dPseuSrcToSrcDistance ;
                    WindowToBeInserted.pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex = WindowToBeInserted.b0 ;
                    WindowToBeInserted.pMarkFromEdgeVertex->pEdgeReportedGeoDist = WindowToBeInserted.pEdge ;
                }
            }

            Vertex *pAnotherPt = WindowToBeInserted.pEdge->GetAnotherVertex( WindowToBeInserted.dwMarkFromEdgeVertexIdx ) ;
            if ( WindowToBeInserted.b1 > (WindowToBeInserted.pEdge->dEdgeLength - 0.01) )
            {
                if ( (WindowToBeInserted.d1 + WindowToBeInserted.dPseuSrcToSrcDistance) < pAnotherPt->dGeoDistanceToSrc )
                {
                    pAnotherPt->dGeoDistanceToSrc = WindowToBeInserted.d1 + WindowToBeInserted.dPseuSrcToSrcDistance ;
                    pAnotherPt->dLengthOfWindowEdgeToThisVertex = WindowToBeInserted.pEdge->dEdgeLength - WindowToBeInserted.b1 ;
                    pAnotherPt->pEdgeReportedGeoDist = WindowToBeInserted.pEdge ;
                }                
            }
        }

        // after intersect the new window with all the existing windows on edge, if the new window is still available
        // add it to the edge and heap
        if ( !bNewWindowNotAvailable/*pNewEdgeWindow->b0 < pNewEdgeWindow->b1*/ )
        {
            TypeEdgeWindowsHeap::item_type *pNewWindowItem = 
                new TypeEdgeWindowsHeap::item_type( std::min(pNewEdgeWindow->d0, pNewEdgeWindow->d1)+pNewEdgeWindow->dPseuSrcToSrcDistance, *pNewEdgeWindow );

            m_EdgeWindowsHeap.insert( pNewWindowItem ) ;

            pNewEdgeWindow->pEdge->WindowsList.push_back(Edge::WindowListElement(pNewWindowItem, pNewWindowItem->m_data));

            // update the geodesic distance on vertices affected by this new window
            if ( pNewEdgeWindow->b0 < 0.01 )
            {
                if ( (pNewEdgeWindow->d0 + pNewEdgeWindow->dPseuSrcToSrcDistance) < pNewEdgeWindow->pMarkFromEdgeVertex->dGeoDistanceToSrc )
                {
                    pNewEdgeWindow->pMarkFromEdgeVertex->dGeoDistanceToSrc = pNewEdgeWindow->d0 + pNewEdgeWindow->dPseuSrcToSrcDistance ;
                    pNewEdgeWindow->pMarkFromEdgeVertex->dLengthOfWindowEdgeToThisVertex = pNewEdgeWindow->b0 ;
                    pNewEdgeWindow->pMarkFromEdgeVertex->pEdgeReportedGeoDist = pNewEdgeWindow->pEdge ;
                }                
            }

            Vertex *pAnotherPt = pNewEdgeWindow->pEdge->GetAnotherVertex( pNewEdgeWindow->dwMarkFromEdgeVertexIdx ) ;
            if ( pNewEdgeWindow->b1 > (pNewEdgeWindow->pEdge->dEdgeLength - 0.01) )
            {
                if ( (pNewEdgeWindow->d1 + pNewEdgeWindow->dPseuSrcToSrcDistance) < pAnotherPt->dGeoDistanceToSrc )
                {
                    pAnotherPt->dGeoDistanceToSrc = pNewEdgeWindow->d1 + pNewEdgeWindow->dPseuSrcToSrcDistance ;
                    pAnotherPt->dLengthOfWindowEdgeToThisVertex = pNewEdgeWindow->pEdge->dEdgeLength - pNewEdgeWindow->b1 ;
                    pAnotherPt->pEdgeReportedGeoDist = pNewEdgeWindow->pEdge ;
                }                
            }
        }

        ++j ;
    }
}

// [see "intersection of overlapping windows" of the paper]
void CExactOneToAll::IntersectWindow( EdgeWindow *pExistingWindow, 
                                      EdgeWindow *pNewWindow, 
                                      bool *pExistingWindowChanged, 
                                      bool *pNewWindowChanged,
                                      bool *pExistingWindowNotAvailable,
                                      bool *pNewWindowNotAvailable )
{
    memset( &m_NewExistingWindow, 0, sizeof(EdgeWindow) ) ;
    memset( &m_AnotherNewWindow, 0, sizeof(EdgeWindow) ) ;    
    
    if ( pNewWindow->b1 <= pNewWindow->b0 )
        return ;

    // flags that both existing window and new window have not yet been changed
    *pExistingWindowChanged = false ;
    *pNewWindowChanged = false ;
    *pExistingWindowNotAvailable = false ;
    *pNewWindowNotAvailable = false ;

    // although the existing window and the new window are on the same edge of the mesh,
    // their b0 may count from different edge vertices (out from the two edge vertex)
    // if this is the case, adjust so that both window's b0 count from the same edge vertex 

    // also, after this if statement, all the windows on this edge should be count from the same vertex of edge (because the new window will be adjusted correspond to all the existing one)
    if ( pExistingWindow->dwMarkFromEdgeVertexIdx != pNewWindow->dwMarkFromEdgeVertexIdx )
    {
        pNewWindow->dwMarkFromEdgeVertexIdx = pExistingWindow->dwMarkFromEdgeVertexIdx ; 
        pNewWindow->pMarkFromEdgeVertex = pExistingWindow->pMarkFromEdgeVertex ;
        std::swap(pNewWindow->d0, pNewWindow->d1);
        std::swap(pNewWindow->b0, pNewWindow->b1);
        pNewWindow->b0 = std::max<double>(pNewWindow->pEdge->dEdgeLength - pNewWindow->b0, 0);
        if ( pNewWindow->b0 < FLT_EPSILON )
        {
            pNewWindow->b0 = 0 ;
        }
        pNewWindow->b1 = pNewWindow->pEdge->dEdgeLength - pNewWindow->b1 ;
        pNewWindow->dv2Src.x = pNewWindow->pEdge->dEdgeLength - pNewWindow->dv2Src.x ;
    }

    double a = std::min(std::min(std::min(pExistingWindow->b0, pExistingWindow->b1), pNewWindow->b0), pNewWindow->b1);
    double b = std::max(std::max(std::max(pExistingWindow->b0, pExistingWindow->b1), pNewWindow->b0), pNewWindow->b1);

    // compute the intersection length
    double IntersectionLength = 
        (pExistingWindow->b1 - pExistingWindow->b0 + pNewWindow->b1 - pNewWindow->b0) - 
        (b - a) ;

    // no intersection
    if ( IntersectionLength <= 0 )
    {
        return ;
    }    

    // the "pseudo source pt" of the existing window and new window
    DVector2 ExistingWindowSrc ;
    DVector2 NewWindowSrc ; 
    
    ExistingWindowSrc = pExistingWindow->dv2Src ;    
    NewWindowSrc = pNewWindow->dv2Src ;    
    
    // the new window is almost the same as the existing one, simply drop the new window
    if ( SquredD2Dist(ExistingWindowSrc, NewWindowSrc) < FLT_EPSILON &&
         fabs(pExistingWindow->b0-pNewWindow->b0) < FLT_EPSILON && 
         fabs(pExistingWindow->b1-pNewWindow->b1) < FLT_EPSILON && 
         fabs(pExistingWindow->dPseuSrcToSrcDistance-pNewWindow->dPseuSrcToSrcDistance) < FLT_EPSILON )
    {
        *pNewWindowChanged = true ;
        *pNewWindowNotAvailable = true ;

        return ;
    }

    const double dErrorOverlapLength = 0.00001 ;

    // the new window is within the existing window    
    if ( pNewWindow->b0 > pExistingWindow->b0 && pNewWindow->b1 < pExistingWindow->b1 )
    {
        if ( pNewWindow->b0 - pExistingWindow->b0 > dErrorOverlapLength )
        {
            m_NewExistingWindow.b0 = pExistingWindow->b0 ;
            m_NewExistingWindow.b1 = pNewWindow->b0 ;
            m_NewExistingWindow.dv2Src = pExistingWindow->dv2Src ;
            m_NewExistingWindow.d0 = pExistingWindow->d0 ;
            m_NewExistingWindow.SetEdgeIdx( m_EdgeList, pExistingWindow->dwEdgeIdx ) ;
            m_NewExistingWindow.SetFaceIdxPropagatedFrom( m_FaceList, pExistingWindow->dwFaceIdxPropagatedFrom ) ;
            m_NewExistingWindow.SetMarkFromEdgeVertexIdx( m_VertexList, pExistingWindow->dwMarkFromEdgeVertexIdx ) ;
            m_NewExistingWindow.SetPseuSrcVertexIdx( m_VertexList, pExistingWindow->dwPseuSrcVertexIdx ) ;            
            m_NewExistingWindow.d1 = sqrt( SquredD2Dist( DVector2(m_NewExistingWindow.b1, 0), m_NewExistingWindow.dv2Src ) ) ;
            m_NewExistingWindow.dPseuSrcToSrcDistance = pExistingWindow->dPseuSrcToSrcDistance ;

            m_NewExistingWindow.ksi = pExistingWindow->ksi ;
            m_NewExistingWindow.pEdgePropagatedFrom = pExistingWindow->pEdgePropagatedFrom ;
            
            pExistingWindow->b0 = pNewWindow->b0 ;
            pExistingWindow->d0 = sqrt( SquredD2Dist( DVector2(pExistingWindow->b0, 0), ExistingWindowSrc ) ) ;

            *pExistingWindowChanged = true ;
        } else
        {
            pExistingWindow->b0 = pNewWindow->b0 ;
            pExistingWindow->d0 = sqrt( SquredD2Dist( DVector2(pExistingWindow->b0, 0), ExistingWindowSrc ) ) ;

            *pExistingWindowChanged = true ;
        }
    }

    // the existing window is within the new window    
    if ( pExistingWindow->b0 > pNewWindow->b0 && pExistingWindow->b1 < pNewWindow->b1 )
    {
        if ( pExistingWindow->b0 - pNewWindow->b0 > dErrorOverlapLength )
        {
            m_AnotherNewWindow.b0 = pNewWindow->b0 ;
            m_AnotherNewWindow.b1 = pExistingWindow->b0 ;
            m_AnotherNewWindow.dv2Src = pNewWindow->dv2Src ;
            m_AnotherNewWindow.d0 = pNewWindow->d0 ;
            m_AnotherNewWindow.SetEdgeIdx( m_EdgeList, pNewWindow->dwEdgeIdx ) ;
            m_AnotherNewWindow.SetFaceIdxPropagatedFrom( m_FaceList, pNewWindow->dwFaceIdxPropagatedFrom ) ;
            m_AnotherNewWindow.SetMarkFromEdgeVertexIdx( m_VertexList, pNewWindow->dwMarkFromEdgeVertexIdx ) ;
            m_AnotherNewWindow.SetPseuSrcVertexIdx( m_VertexList, pNewWindow->dwPseuSrcVertexIdx ) ;
            m_AnotherNewWindow.d1 = sqrt( SquredD2Dist( DVector2(m_AnotherNewWindow.b1, 0), m_AnotherNewWindow.dv2Src ) ) ;
            m_AnotherNewWindow.dPseuSrcToSrcDistance = pNewWindow->dPseuSrcToSrcDistance ;

            m_AnotherNewWindow.ksi = pNewWindow->ksi ;
            m_AnotherNewWindow.pEdgePropagatedFrom = pNewWindow->pEdgePropagatedFrom ;

            pNewWindow->b0 = pExistingWindow->b0 ;
            pNewWindow->d0 = sqrt( SquredD2Dist( DVector2(pNewWindow->b0, 0), NewWindowSrc ) ) ;

            *pNewWindowChanged = true ;
        } else
        {
            pNewWindow->b0 = pExistingWindow->b0 ;
            pNewWindow->d0 = sqrt( SquredD2Dist( DVector2(pNewWindow->b0, 0), NewWindowSrc ) ) ;

            *pNewWindowChanged = true ;
        }
    }

    // the position of the intersection's start point 
    // there are two possibilities:
    // the intersection starts from pExistingWindow->b0 or
    // the intersection starts from pNewWindow->b0
    double IntersectionStart ; 
    bool bStartFromNewWindowB0 = false ;

    // intersection is from pNewWindow->b0 up to length IntersectionLength
    if ( pNewWindow->b0 > pExistingWindow->b0 && pNewWindow->b0 < pExistingWindow->b1 )
    {
        IntersectionStart = pNewWindow->b0 ;
        bStartFromNewWindowB0 = true ;
    } else

    // intersection is from pExistingWindow->b0 up to length IntersectionLength
    if ( pExistingWindow->b0 > pNewWindow->b0 && pExistingWindow->b0 < pNewWindow->b1 )
    {
        IntersectionStart = pExistingWindow->b0 ;
        bStartFromNewWindowB0 = false ;
    } else 

    // pNewWindow->b0 == pExistingWindow->b0
    {
        IntersectionStart = pNewWindow->b0 ;
        bStartFromNewWindowB0 = true ;

        if ( pNewWindow->b1 < pExistingWindow->b1 )
        {
            IntersectionStart = pExistingWindow->b0 ;
            bStartFromNewWindowB0 = false ;
        }
    }

    // we consider this to be an error overlap, if the overlap is too small
    if ( IntersectionLength > 0 && IntersectionLength <= dErrorOverlapLength/*0*/ )
    {
        // we adjust the extent of the new window to eliminate the overlap
        if ( bStartFromNewWindowB0 )
        {
            pNewWindow->b0 += IntersectionLength ;
            pNewWindow->d0 = sqrt( SquredD2Dist( DVector2(pNewWindow->b0, 0), NewWindowSrc) ) ;

            if ( pNewWindow->b0 >= pNewWindow->b1 ) 
            {
                *pNewWindowNotAvailable = true ;
            }

            *pNewWindowChanged = true ;
            return ;
        } else
        {
            pNewWindow->b1 -= IntersectionLength ;
            pNewWindow->d1 = sqrt( SquredD2Dist( DVector2(pNewWindow->b1, 0), NewWindowSrc) ) ;

            if ( pNewWindow->b0 >= pNewWindow->b1 ) 
            {
                *pNewWindowNotAvailable = true ;
            }

            *pNewWindowChanged = true ;
            return ;
        }
    }

    bool bNoSolution = false ;

    bNoSolution = true ;  // force no solution, in reality, this produces very good results and also reduces process time
    
    if ( bNoSolution )
    {
        // whether the distance function of the new window is larger than that of the existing window everywhere in the intersection        
        if ( (sqrt(SquredD2Dist( DVector2(IntersectionStart+IntersectionLength/2, 0), NewWindowSrc )) + pNewWindow->dPseuSrcToSrcDistance) >
             (sqrt(SquredD2Dist( DVector2(IntersectionStart+IntersectionLength/2, 0), ExistingWindowSrc )) + pExistingWindow->dPseuSrcToSrcDistance) )        
        {
            if ( pNewWindow->b0 == pExistingWindow->b0 && pNewWindow->b1 == pExistingWindow->b1 )
            {
                *pNewWindowNotAvailable = true ;
                *pNewWindowChanged = true ;

                return ;
            }
            
            if ( !bStartFromNewWindowB0 )
            {
                pNewWindow->b1 -= IntersectionLength ;
                if ( pNewWindow->b1 <= pNewWindow->b0 )
                {
                    //assert(false) ;
                    *pNewWindowNotAvailable = true ;
                    *pNewWindowChanged = true ;

                    return ;
                } else
                {
                    pNewWindow->d1 = sqrt( SquredD2Dist( DVector2(pNewWindow->b1, 0), NewWindowSrc ) ) ;
                }            
            } else
            {
                pNewWindow->b0 += IntersectionLength ;
                if ( pNewWindow->b0 >= pNewWindow->b1 )
                {
                    //assert(false) ;
                    *pNewWindowNotAvailable = true ;
                    *pNewWindowChanged = true ;

                    return ;
                } else
                {
                    pNewWindow->d0 = sqrt( SquredD2Dist( DVector2(pNewWindow->b0, 0), NewWindowSrc ) ) ;
                }
            }
            *pNewWindowChanged = true ;
        } else
        // the distance function of the existing window is larger than that of the new window everywhere in the intersection
        {
            if ( pNewWindow->b0 == pExistingWindow->b0 && pNewWindow->b1 == pExistingWindow->b1 )
            {
                *pExistingWindowNotAvailable = true ;
                *pExistingWindowChanged = true ;

                return ;
            }
            
            if ( bStartFromNewWindowB0 )
            {
                pExistingWindow->b1 -= IntersectionLength ;
                if ( pExistingWindow->b1 <= pExistingWindow->b0 )
                {
                    //assert(false) ;
                    *pExistingWindowNotAvailable = true ;
                    *pExistingWindowChanged = true ;
                } else
                {
                    pExistingWindow->d1 = sqrt( SquredD2Dist( DVector2(pExistingWindow->b1, 0), ExistingWindowSrc ) ) ;
                }   
            } else
            {
                pExistingWindow->b0 += IntersectionLength ;
                if ( pExistingWindow->b0 >= pExistingWindow->b1 )
                {
                    //assert(false) ;
                    *pExistingWindowNotAvailable = true ;
                    *pExistingWindowChanged = true ;
                } else
                {
                    pExistingWindow->d0 = sqrt( SquredD2Dist( DVector2(pExistingWindow->b0, 0), ExistingWindowSrc ) ) ;
                }
            }
            *pExistingWindowChanged = true ;
        }
    }
}

void CExactOneToAll::GenerateWindowsAroundSaddleOrBoundaryVertex( const EdgeWindow &iwindow,
                                                                  const uint32_t dwSaddleOrBoundaryVertexId,
                                                                  std::vector<EdgeWindow> &WindowsOut)
{
    WindowsOut.clear() ;
    
    for (size_t i = 0; i < m_VertexList[dwSaddleOrBoundaryVertexId].facesAdj.size(); ++i)
    {
        EdgeWindow tmpWindow ;

        tmpWindow.SetEdgeIdx( m_EdgeList, m_VertexList[dwSaddleOrBoundaryVertexId].facesAdj[i]->GetOpposingEdgeIdx(dwSaddleOrBoundaryVertexId) ) ;
        tmpWindow.SetFaceIdxPropagatedFrom(m_FaceList, (uint32_t) ((intptr_t) m_VertexList[dwSaddleOrBoundaryVertexId].facesAdj[i] - (intptr_t) &m_FaceList[0]) / sizeof(Face));
        tmpWindow.SetMarkFromEdgeVertexIdx( m_VertexList, tmpWindow.pEdge->dwVertexIdx0 ) ;
        tmpWindow.SetPseuSrcVertexIdx( m_VertexList, dwSaddleOrBoundaryVertexId ) ;
        tmpWindow.b0 = 0 ;
        tmpWindow.b1 = tmpWindow.pEdge->dEdgeLength ;
        tmpWindow.d0 = sqrt( SquredD3Dist(*tmpWindow.pEdge->pVertex0, m_VertexList[dwSaddleOrBoundaryVertexId]) ) ;
        tmpWindow.d1 = sqrt( SquredD3Dist(*tmpWindow.pEdge->pVertex1, m_VertexList[dwSaddleOrBoundaryVertexId]) ) ;
        tmpWindow.dPseuSrcToSrcDistance = (iwindow.dwMarkFromEdgeVertexIdx == dwSaddleOrBoundaryVertexId ? iwindow.d0 : iwindow.d1)
            + iwindow.dPseuSrcToSrcDistance ;
        ParameterizePt3ToPt2( *tmpWindow.pEdge->pVertex0, *tmpWindow.pEdge->pVertex1, m_VertexList[dwSaddleOrBoundaryVertexId], tmpWindow.dv2Src ) ; 

        tmpWindow.ksi = iwindow.ksi ;
        tmpWindow.pEdgePropagatedFrom = iwindow.pEdge ;

        tmpWindow.dwTag = 1 ;
        
        WindowsOut.push_back(tmpWindow);
    }
}
