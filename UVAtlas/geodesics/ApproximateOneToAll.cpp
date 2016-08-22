//-------------------------------------------------------------------------------------
// UVAtlas - ApproximateOneToAll.cpp
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
#include "ApproximateOneToAll.h"
#include "mathutils.h"

using namespace GeodesicDist;

// this does the same job of CExactOneToAll::CutHeapTopData, except that, this does the window merging in addition
void CApproximateOneToAll::CutHeapTopData( EdgeWindow &EdgeWindowOut )
{
    for (;;)
    {
        TypeEdgeWindowsHeap::item_type *pItem = m_EdgeWindowsHeap.cutTop() ;       

        uint32_t dwIdxSelf = FLAG_INVALIDDWORD ;
        for (uint32_t i = 0; i < pItem->m_data.pEdge->WindowsList.size(); ++i)
        {        
            if ( !pItem->m_data.pEdge->WindowsList[i].pHeapItem )
            {
                continue ;
            }
            
            if ( pItem->m_data.pEdge->WindowsList[i].pHeapItem == pItem )
            {
                // here we get a byproduct, because we actually need the idx of the popped off window itself
                dwIdxSelf = i ;
                
                // when searching for a window adjacent to the popped off (from the heap) window, skip the window itself on the edge
                continue ;
            }
            
            // in pWindowLeft and pWindowRight, one is the the popped off window itself, the other one is the possible found adjacent window
            EdgeWindow *pWindowLeft = &(pItem->m_data) ;
            EdgeWindow *pWindowRight = &(pItem->m_data.pEdge->WindowsList[i].theWindow) ;
                    
            if ( (pWindowLeft->b0 == pWindowRight->b1 || pWindowLeft->b1 == pWindowRight->b0) /*&&
                 (pWindowLeft->dwFaceIdxPropagatedFrom == pWindowRight->dwFaceIdxPropagatedFrom)*/ )
            {
                // found an adjacent window

                if ( pWindowLeft->b0 == pWindowRight->b1 )
                {
                    std::swap( pWindowLeft, pWindowRight ) ;
                }

                double b1pie = pWindowRight->b1 ;
                double b0pie = pWindowLeft->b0 ;
                double D1 = pWindowRight->dPseuSrcToSrcDistance + SqrtMin0( SquredD2Dist(DVector2(b1pie, 0), pWindowRight->dv2Src) ) ;
                double D0 = pWindowLeft->dPseuSrcToSrcDistance + SqrtMin0( SquredD2Dist(DVector2(b0pie, 0), pWindowLeft->dv2Src) ) ;

                if (fabs(D1 - D0) < DBL_EPSILON)
                {
                    continue;	// prevent divide-by-zero on very narrow windows
                }

                double alpha = (b1pie - b0pie) / (D1 - D0) ;
                double beta = ( SQR(b0pie) - SQR(b1pie) - SQR(D0) + SQR(D1) ) / ( 2*(D1 - D0) ) ;
                double A = SQR( alpha ) - 1 ;
                double B = 2 * alpha * (beta - D0) + 2*b0pie ;
                double C = SQR(D0 - beta) - SQR(b0pie) ;
                
                DVector2 ptRes ;
                bool bTmp;
                GetCommonPointOf2Lines( pWindowLeft->dv2Src, DVector2(pWindowLeft->b0, 0), 
                                        pWindowRight->dv2Src, DVector2(pWindowRight->b1, 0), ptRes, bTmp ) ;

                double sigma = FLT_MAX ;
                DVector2 spie ;            
                
                double x1, x2, x3;

                x1 = -beta /alpha;
                x2 = (D0 - beta) /alpha;
                x3 = (D1 - beta)/alpha;

                if (alpha < 0)
                {
                    if (x3 > x2)
                        x2 = x3;

                    if (x2 > x1)
                        continue;
                }
                else
                {
                    if (x3 < x2)
                        x2 = x3;
                    
                    if (x1 > x2)
                        continue;
                }
                
                DVector2 spietmp ;
                spietmp.x  = (x1 + x2)*0.5;
                
                spietmp.y = A * SQR(spietmp.x) + B * spietmp.x + C ;
                if ( spietmp.y < 0 )
                {
                    continue ;
                }
                spietmp.y = sqrt( spietmp.y ) ;

                DVector2 P0, Q0, P1, Q1 ;
                DVector2Minus( pWindowLeft->dv2Src, DVector2(pWindowLeft->b0, 0), P0 ) ;
                DVector2Minus( spietmp, DVector2(pWindowLeft->b0, 0), Q0 ) ;
                DVector2Minus( pWindowRight->dv2Src, DVector2(pWindowRight->b1, 0), P1 ) ;
                DVector2Minus( spietmp, DVector2(pWindowRight->b1, 0), Q1 ) ;

                DVector3 tmpv0, tmpv1 ;
                DVector3Cross( DVector3(Q0), DVector3(P0), tmpv0 ) ;
                DVector3Cross( DVector3(Q1), DVector3(P1), tmpv1 ) ;

                // spietmp.y must < ptRes.y (if ptRes exists, which means two line segments passed into the following GetCommonPointOf2Lines are not parallel, and ptRes.y is positive)				
                bTmp = true ;
                if ( (ptRes.x < FLT_MAX) && (ptRes.y > 0) )
                {
                    if ( spietmp.y > ptRes.y )
                        bTmp = false ;
                }

                // the new computed pseudosource is within the yellow region
                if ( ((SQN(tmpv0.z) != SQN(tmpv1.z)) || (tmpv0.z == 0) || (tmpv1.z == 0)) && bTmp )
                {					
                    sigma = alpha * spietmp.x + beta ;
                    spie = spietmp ;
                }
                
                if ( sigma == FLT_MAX )
                {
                    // this adjacent window cannot fulfill all the criteria listed in the paper, continue to try the next window on edge                
                    continue ;
                }

                double diflargest = 0 ;
                double Dp = FLT_MAX ;
                for (size_t ca = 0; ca < 6; ++ca)
                {
                    DVector2 tmpp ;
                    tmpp.y = 0 ;

                    double tmpdif = 0 ;
                    double tmpDp = 0 ;
                    
                    switch ( ca ) 
                    {
                        case 0:
                            tmpp.x = pWindowLeft->b0 ;
                            tmpDp = pWindowLeft->dPseuSrcToSrcDistance + pWindowLeft->d0 ;
                            tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            break ;

                        case 1:
                            tmpp.x = pWindowLeft->b1 ;
                            tmpDp = pWindowLeft->dPseuSrcToSrcDistance + pWindowLeft->d1 ;
                            tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            break ;

                        case 2:
                            tmpp.x = pWindowRight->b0 ;
                            tmpDp = pWindowRight->dPseuSrcToSrcDistance + pWindowRight->d0 ;
                            tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            break ;

                        case 3:
                            tmpp.x = pWindowRight->b1 ;
                            tmpDp = pWindowRight->dPseuSrcToSrcDistance + pWindowRight->d1 ;
                            tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            break ;

                        case 4:
                            {
                                double A0 = SQR(spie.y) - SQR(pWindowLeft->dv2Src.y) ;
                                double B0 = 2 * (spie.x * SQR(pWindowLeft->dv2Src.y) - pWindowLeft->dv2Src.x * SQR(spie.y)) ;
                                double C0 = SQR(pWindowLeft->dv2Src.x) * SQR(spie.y) - SQR(spie.x) * SQR(pWindowLeft->dv2Src.y) ;

                                if ( A0 > FLT_EPSILON || A0 < -FLT_EPSILON )
                                {
                                    double discriminant = SQR(B0)-4*A0*C0;

                                    if ( discriminant > 0 )
                                    {
                                        tmpp.x = (-B0+SqrtMin0(discriminant))/(2*A0) ;

                                        if ( tmpp.x < pWindowLeft->b0 || tmpp.x > pWindowLeft->b1 )
                                        {
                                            tmpp.x = (-B0-SqrtMin0(discriminant))/(2*A0) ;

                                            if ( tmpp.x < pWindowLeft->b0 || tmpp.x > pWindowLeft->b1 )
                                            {
                                                continue ;
                                            } 
                                        }
                                    } else
                                    {
                                        continue ;
                                    }
                                } else
                                {
                                    if ( B0 != 0 )
                                    {                                
                                        tmpp.x = -C0/B0 ;

                                        if ( tmpp.x < pWindowLeft->b0 || tmpp.x > pWindowLeft->b1 )
                                        {
                                            continue ;
                                        }
                                    } else
                                    {
                                        continue ;
                                    }
                                }

                                tmpDp = pWindowLeft->dPseuSrcToSrcDistance + SqrtMin0( SquredD2Dist(pWindowLeft->dv2Src, tmpp) ) ;
                                tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            }
                            break ;

                        case 5:
                            {
                                double A0 = SQR(spie.y) - SQR(pWindowRight->dv2Src.y) ;
                                double B0 = 2 * (spie.x * SQR(pWindowRight->dv2Src.y) - pWindowRight->dv2Src.x * SQR(spie.y)) ;
                                double C0 = SQR(pWindowRight->dv2Src.x) * SQR(spie.y) - SQR(spie.x) * SQR(pWindowRight->dv2Src.y) ;

                                if ( A0 > FLT_EPSILON || A0 < -FLT_EPSILON )
                                {
                                    double discriminant = SQR(B0)-4*A0*C0;

                                    if ( discriminant > 0 )
                                    {
                                        tmpp.x = (-B0+SqrtMin0(discriminant))/(2*A0) ;

                                        if ( tmpp.x < pWindowRight->b0 || tmpp.x > pWindowRight->b1 )
                                        {
                                            tmpp.x = (-B0-SqrtMin0(discriminant))/(2*A0) ;

                                            if ( tmpp.x < pWindowRight->b0 || tmpp.x > pWindowRight->b1 )
                                            {
                                                continue ;
                                            } 
                                        }
                                    } else
                                    {
                                        continue ;
                                    }
                                } else
                                {
                                    if ( B0 != 0 )
                                    {                                
                                        tmpp.x = -C0/B0 ;

                                        if ( tmpp.x < pWindowRight->b0 || tmpp.x > pWindowRight->b1 )
                                        {
                                            continue ;
                                        }
                                    } else
                                    {
                                        continue ;
                                    }
                                }

                                tmpDp = pWindowRight->dPseuSrcToSrcDistance + SqrtMin0( SquredD2Dist(pWindowRight->dv2Src, tmpp) ) ;
                                tmpdif = fabs( sigma + SqrtMin0( SquredD2Dist(spie, tmpp) ) - tmpDp ) ;
                            }
                            break ;
                    }

                    if ( tmpdif > diflargest )
                    {
                        diflargest = tmpdif ;
                        Dp = tmpDp ;
                    }
                }

                double ksi ;
                ksi = std::max(pWindowRight->ksi, pWindowLeft->ksi) + diflargest;

                //double	ksi = 0.0;

                if ( ksi/Dp < 0.01 && diflargest/Dp < 0.01*0.1)
                {
                    // allow the merge

                    if ( dwIdxSelf == FLAG_INVALIDDWORD )
                    {
                        // the idx of the popped off window is not yet set, so search for it here
                        // we only need to search from i + 1 (rather than from 0), because the previous ones have already been searched

                        for (uint32_t t = i + 1; t < pItem->m_data.pEdge->WindowsList.size(); ++t)
                            if ( pItem->m_data.pEdge->WindowsList[t].pHeapItem == pItem )
                            {
                                dwIdxSelf = t ;

                                break ;
                            }
                    }

                    // remove the found adjacent window from the heap and from the edge it is on
                    m_EdgeWindowsHeap.remove( pItem->m_data.pEdge->WindowsList[i].pHeapItem ) ;
                    delete pItem->m_data.pEdge->WindowsList[i].pHeapItem ;
                    pItem->m_data.pEdge->WindowsList.erase(pItem->m_data.pEdge->WindowsList.begin() + i);
                    if ( dwIdxSelf > i )
                    {
                        --dwIdxSelf ;
                    }
                    
                    EdgeWindow *pTheWindow = &(pItem->m_data.pEdge->WindowsList[dwIdxSelf].theWindow) ;
                    
                    pTheWindow->b0 = b0pie ;
                    pTheWindow->b1 = b1pie ;
                    pTheWindow->dPseuSrcToSrcDistance = sigma ;
                    pTheWindow->dv2Src = spie ;
                    pTheWindow->d0 = SqrtMin0( SquredD2Dist(DVector2(b0pie, 0), spie) ) ;
                    pTheWindow->d1 = SqrtMin0( SquredD2Dist(DVector2(b1pie, 0), spie) ) ;
                    pTheWindow->ksi = ksi ;
                    pTheWindow->dwPseuSrcVertexIdx = FLAG_INVALIDDWORD;
                    pTheWindow->pPseuSrcVertex = nullptr;
                    /*pTheWindow->pHeapItem = nullptr ;

                    EdgeWindowOut = *pTheWindow ;
                    delete pItem ;
                    return ;*/

                    pItem->m_data.pEdge->WindowsList[dwIdxSelf].pHeapItem =
                        new TypeEdgeWindowsHeap::item_type( std::min(pTheWindow->d0, pTheWindow->d1)+pTheWindow->dPseuSrcToSrcDistance, *pTheWindow );
                    m_EdgeWindowsHeap.insert( pItem->m_data.pEdge->WindowsList[dwIdxSelf].pHeapItem ) ;
                    delete pItem ;

                    // continue to pop the next window in heap and test whether any merge is possible                    
                    goto l_outter_while_again ;
                }
            }
        }   

        if ( dwIdxSelf == FLAG_INVALIDDWORD )
        {
            for (size_t i = 0; i < pItem->m_data.pEdge->WindowsList.size(); ++i)
            {
                if ( pItem->m_data.pEdge->WindowsList[i].pHeapItem == pItem )
                {
                    pItem->m_data.pEdge->WindowsList[i].pHeapItem = nullptr;
                    break ;
                }
            }

            EdgeWindowOut = pItem->m_data ;
            delete pItem ;
            return ;
        }

        pItem->m_data.pEdge->WindowsList[dwIdxSelf].pHeapItem = nullptr;
        EdgeWindowOut = pItem->m_data ;
        delete pItem ;

        return ;

l_outter_while_again:
        ;
    }
}
