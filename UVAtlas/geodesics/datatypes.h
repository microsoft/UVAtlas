//-------------------------------------------------------------------------------------
// UVAtlas - datatypes.h
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

#include "minheap.hpp"

namespace GeodesicDist
{
    const size_t FLAG_INVALID_SIZE_T = size_t(-1) ;     // denote invalid pointer
    const uint32_t FLAG_INVALIDDWORD = uint32_t(-1) ;   // denote invalid face index, vertex index, edge index

    // used to access the position of each vertex in a mesh
    struct _vertex
    { 
        float x, y, z ;
    } ;

    // used to render the line strip showing the windows propagation
    struct _colorvertex
    {
        float x, y, z ;
        uint32_t color ;
    } ;

    // the 2d vector structure used in the algorithm
    struct DVector2
    {
        double x, y ;

        DVector2( const double &x, const double &y )
        {
            this->x = x ;
            this->y = y ;
        }
        DVector2()
        {
            x = y = 0 ;
        }   
        double Length() 
        {
            return sqrt( x*x + y*y ) ;
        }
    } ;

    // operations on the 2d vector
    inline void DVector2Minus( const DVector2 &L, const DVector2 &R, DVector2 &Res )
    {
        Res.x = L.x - R.x ;
        Res.y = L.y - R.y ;    
    }

    inline void DVector2Add( const DVector2 &L, const DVector2 &R, DVector2 &Res )
    {
        Res.x = L.x + R.x ;
        Res.y = L.y + R.y ;    
    }

    inline void DVector2ScalarMul( DVector2 &L, double R )
    {
        L.x *= R ;
        L.y *= R ;    
    }

    inline double DVector2CrossModulus( const DVector2 &L, const DVector2 &R )
    {       
        return fabs( L.x * R.y - L.y * R.x );
    }

    inline double DVector2Dot( const DVector2 &L, const DVector2 &R )
    {
        return L.x*R.x + L.y*R.y ;
    }

    struct Edge ;
    struct Vertex ;
    struct Face ;
    struct EdgeWindow ;

    // the vertex list
    typedef std::vector<Vertex> TypeVertexList;

    // the edge list
    typedef std::vector<Edge> TypeEdgeList;

    // the face list
    typedef std::vector<Face> TypeFaceList;

    // the windows heap, in each iteration, the window with minimal to-source-distance is popped off the heap and propagated
    typedef CMinHeap<double, EdgeWindow> TypeEdgeWindowsHeap ;

    // one window on an edge (see the paper)
    struct EdgeWindow
    {
        uint32_t dwTag ;
    
        uint32_t dwEdgeIdx;                                 // which edge this window is on, this is the index in TypeEdgeList (following are in the similar pattern)
        Edge *pEdge ;                                       // pointer to that edge in TypeEdgeList
    
        uint32_t dwMarkFromEdgeVertexIdx;                   // b0 count from this edge vertex
        Vertex *pMarkFromEdgeVertex ;                       // pointer to that vertex

        uint32_t dwPseuSrcVertexIdx;                        // the pseudo source vertex index
        Vertex *pPseuSrcVertex ;                            // pointer to that vertex

        double b0, b1 ;                                     // b0, b1, d0, d1 of the window, see the paper
        double d0, d1 ;
        DVector2 dv2Src ;                                   // the parameterized 2d pseudo source with regard to the edge this window is on

        double dPseuSrcToSrcDistance ;                      // the distance from the pseudo source to the real source
    
        uint32_t dwFaceIdxPropagatedFrom;                   // which face this window is propagated from, this is used to determine the next face the window will propagate to
        Face *pFacePropagatedFrom ;                         // pointer to that face

        Edge* pEdgePropagatedFrom ;                         // pointer to the edge that contain the window that produced this window

        double ksi ;                                        // the accumulated error used in approximate algorithm

        EdgeWindow()
        {
            memset( this, 0, sizeof(EdgeWindow) ) ;
        }
        EdgeWindow( const EdgeWindow &R ) 
        {
            memcpy( this, &R, sizeof(EdgeWindow) ) ;        
        }
        // trick constructor
        EdgeWindow( const uint32_t R ) 
        {
            if ( R == 0 )
                memset( this, 0, sizeof(EdgeWindow) ) ;
        }

        // setting an index also assigns appropriate pointer to the corresponding pointer field 
        void SetEdgeIdx(TypeEdgeList &EdgeList, const uint32_t index)
        {
            this->dwEdgeIdx = index;
            this->pEdge = &(EdgeList[index]) ;
        }    
        void SetPseuSrcVertexIdx(TypeVertexList &VertexList, const uint32_t index)
        {
            this->dwPseuSrcVertexIdx = index;
            this->pPseuSrcVertex = (index < VertexList.size()) ? &(VertexList[index]) : nullptr;
        }
        void SetMarkFromEdgeVertexIdx(TypeVertexList &VertexList, const uint32_t index)
        {
            this->dwMarkFromEdgeVertexIdx = index;
            this->pMarkFromEdgeVertex = &(VertexList[index]) ;
        }
        void SetFaceIdxPropagatedFrom(TypeFaceList &FaceList, const uint32_t index)
        {
            this->dwFaceIdxPropagatedFrom = index;
            this->pFacePropagatedFrom = &(FaceList[index]) ;
        }
    } ;

    struct Edge 
    {
        uint32_t dwVertexIdx0;                              // index of one vertex of the edge
        Vertex *pVertex0 ;                                  // pointer to that vertex
    
        uint32_t dwVertexIdx1;                              // index of another vertex of the edge
        Vertex *pVertex1 ;                                  // pointer to that vertex

        uint32_t dwAdjFaceIdx0;                             // index of one face that has this edge ( FLAG_INVALIDDWORD indicates no face adjacency )
        Face *pAdjFace0 ;                                   // pointer to that face ( nullptr indicates no face adjacency )

        uint32_t dwAdjFaceIdx1;                             // index of another face that has this edge
        Face *pAdjFace1 ;                                   // pointer to that face

        double dEdgeLength ;                                // the length of this edge

        Vertex *GetVertexByIdx( const uint32_t dwIdx )
        {        
            if ( dwIdx == dwVertexIdx0 )
                return pVertex0 ;
            else if ( dwIdx == dwVertexIdx1 )
                return pVertex1 ;
            else 
                return (Vertex*)FLAG_INVALID_SIZE_T ;
        }

        uint32_t GetAnotherVertexIdx(const uint32_t dwThisVertexIdx)
        {
            if ( dwThisVertexIdx != dwVertexIdx0 && dwThisVertexIdx != dwVertexIdx1 ) 
                return FLAG_INVALIDDWORD ;

            return (dwVertexIdx0 ^ dwVertexIdx1 ^ dwThisVertexIdx) ;
        }
        Vertex *GetAnotherVertex( const Vertex *pThisVertex )
        {
            if ( pThisVertex != pVertex0 && pThisVertex != pVertex1 )
                return (Vertex*)FLAG_INVALID_SIZE_T ;

            return (Vertex*) ((intptr_t) pVertex0 ^ (intptr_t) pVertex1 ^ (intptr_t) pThisVertex);
        }
        Vertex *GetAnotherVertex(const uint32_t dwThisVertexIdx)
        {
            if ( dwThisVertexIdx != dwVertexIdx0 && dwThisVertexIdx != dwVertexIdx1 ) 
                return (Vertex*)FLAG_INVALID_SIZE_T ;

            return (dwThisVertexIdx == dwVertexIdx0 ? pVertex1 : pVertex0) ;
        }

        Face* GetFace(const uint32_t idx) const
        {
            if ( idx != 0 && idx != 1 )
                return (Face*)FLAG_INVALID_SIZE_T ;

            return ( idx == 0 ? pAdjFace0 : pAdjFace1 ) ;
        }

        uint32_t GetFaceIdx(const uint32_t idx)
        {
            if ( idx != 0 && idx != 1 )
                return FLAG_INVALIDDWORD ;

            return ( idx == 0 ? dwAdjFaceIdx0 : dwAdjFaceIdx1 ) ;
        }

        uint32_t GetAnotherFaceIdx(const uint32_t dwThisFaceIdx)
        {
            if ( dwThisFaceIdx != dwAdjFaceIdx0 && dwThisFaceIdx != dwAdjFaceIdx1 )
                return FLAG_INVALIDDWORD ;

            return ( dwAdjFaceIdx0 ^ dwAdjFaceIdx1 ^ dwThisFaceIdx ) ;
        }
        Face *GetAnotherFace( const Face *pThisFace )
        {
            if ( pThisFace != pAdjFace0 && pThisFace != pAdjFace1 )
                return (Face*)FLAG_INVALID_SIZE_T ;

            return (Face*) ((intptr_t) pAdjFace0 ^ (intptr_t) pAdjFace1 ^ (intptr_t) pThisFace);
        }
        Face *GetAnotherFace(const uint32_t dwThisFaceIdx)
        {
            if ( dwThisFaceIdx != dwAdjFaceIdx0 && dwThisFaceIdx != dwAdjFaceIdx1 )
                return (Face*)FLAG_INVALID_SIZE_T ;
            return (dwThisFaceIdx == dwAdjFaceIdx0 ? pAdjFace1 : pAdjFace0) ;
        }

        bool HasVertexIdx(const uint32_t idx)
        {
            return ( dwVertexIdx0 == idx || dwVertexIdx1 == idx ) ;        
        }

        bool IsBoundary() const
        {
            return ( (!pAdjFace0) || (!pAdjFace1) ) ;
        }
    
        struct WindowListElement
        {
            TypeEdgeWindowsHeap::item_type* pHeapItem ;
            EdgeWindow theWindow ;

            WindowListElement( const TypeEdgeWindowsHeap::item_type * pHeapItem, const EdgeWindow &theWindow )
            {
                this->pHeapItem = const_cast<TypeEdgeWindowsHeap::item_type *>(pHeapItem) ;
                this->theWindow = theWindow ;
            }
            WindowListElement() : pHeapItem(nullptr) { }
        };

        // on the edge, there is a windows list, which stores windows that has propagated onto this edge
        // in addition, it also stores a reference to the same window in the windows heap, so we can modify the one stored in the heap (modification during window intersection)
        std::vector<WindowListElement> WindowsList;
    } ;

    struct Face
    {
        uint32_t dwEdgeIdx0;                // index of first edge of the face
        Edge *pEdge0 ;                      // pointer to that edge

        uint32_t dwEdgeIdx1;                // index of second edge of the face
        Edge *pEdge1 ;                      // pointer to that edge

        uint32_t dwEdgeIdx2;                // index of third edge of the face
        Edge *pEdge2 ;                      // pointer to that edge

        uint32_t dwVertexIdx0;              // index of the first vertex of the face
        Vertex *pVertex0 ;                  // pointer to that vertex

        uint32_t dwVertexIdx1;              // index of the second vertex of the face
        Vertex *pVertex1 ;                  // pointer to that vertex

        uint32_t dwVertexIdx2;              // index of the third vertex of the face
        Vertex *pVertex2 ;                  // pointer to that vertex

        Edge *GetOpposingEdge(const uint32_t dwVertexIdx)
        {
            if ( dwVertexIdx != dwVertexIdx0 && dwVertexIdx != dwVertexIdx1 && dwVertexIdx != dwVertexIdx2 )
                return (Edge*)FLAG_INVALID_SIZE_T ;

            if ( !pEdge0->HasVertexIdx(dwVertexIdx) )
                return pEdge0 ;
            else if ( !pEdge1->HasVertexIdx(dwVertexIdx) )
                return pEdge1 ;
            else
                return pEdge2 ;
        }

        uint32_t GetOpposingEdgeIdx(const uint32_t dwVertexIdx) const
        {
            if ( dwVertexIdx != dwVertexIdx0 && dwVertexIdx != dwVertexIdx1 && dwVertexIdx != dwVertexIdx2 )
                return FLAG_INVALIDDWORD ;

            if ( !pEdge0->HasVertexIdx(dwVertexIdx) )
                return dwEdgeIdx0 ;
            else if ( !pEdge1->HasVertexIdx(dwVertexIdx) )
                return dwEdgeIdx1 ;
            else
                return dwEdgeIdx2 ;
        }

        Vertex *GetOpposingVertex( const Edge *pEdge )
        {
            return (Vertex*) ((intptr_t) pVertex0 ^ (intptr_t) pVertex1 ^ (intptr_t) pVertex2 ^ (intptr_t) pEdge->pVertex0 ^ (intptr_t) pEdge->pVertex1);
        }

        uint32_t GetOpposingVertexIdx(TypeEdgeList &EdgeList, const uint32_t dwEdge)
        {
            return dwVertexIdx0 ^ dwVertexIdx1 ^ dwVertexIdx2 ^ EdgeList[dwEdge].dwVertexIdx0 ^ EdgeList[dwEdge].dwVertexIdx1 ;
        }
    
        void GetOtherTwoEdgesIdx(const uint32_t dwThisEdgeIdx, uint32_t &dwResEdgeIdx1, uint32_t &dwResEdgeIdx2)
        {
            if ( dwThisEdgeIdx == dwEdgeIdx0 )
            {
                dwResEdgeIdx1 = dwEdgeIdx1 ;
                dwResEdgeIdx2 = dwEdgeIdx2 ;
            } else
            if ( dwThisEdgeIdx == dwEdgeIdx1 )
            {
                dwResEdgeIdx1 = dwEdgeIdx0 ;
                dwResEdgeIdx2 = dwEdgeIdx2 ;
            } else
            if ( dwThisEdgeIdx == dwEdgeIdx2 )
            {
                dwResEdgeIdx1 = dwEdgeIdx0 ;
                dwResEdgeIdx2 = dwEdgeIdx1 ;
            } else
            {
                dwResEdgeIdx1 = dwResEdgeIdx2 = FLAG_INVALIDDWORD ;
            }
        }
        void GetOtherTwoEdges(const uint32_t dwThisEdgeIdx, Edge **ppResEdge1, Edge **ppResEdge2) const
        {
            if ( dwThisEdgeIdx == dwEdgeIdx0 )
            {
                *ppResEdge1 = pEdge1 ;
                *ppResEdge2 = pEdge2 ;
            } else
            if ( dwThisEdgeIdx == dwEdgeIdx1 )
            {
                *ppResEdge1 = pEdge0 ;
                *ppResEdge2 = pEdge2 ;
            } else
            if ( dwThisEdgeIdx == dwEdgeIdx2 )
            {
                *ppResEdge1 = pEdge0 ;
                *ppResEdge2 = pEdge1 ;
            } else
            {
                *ppResEdge1 = *ppResEdge2 = (Edge*)FLAG_INVALID_SIZE_T ;
            }
        }
    
        Face()
        {
            dwEdgeIdx0 = FLAG_INVALIDDWORD ;
            pEdge0 = nullptr ;
            dwEdgeIdx1 = FLAG_INVALIDDWORD ;
            pEdge1 = nullptr ;
            dwEdgeIdx2 = FLAG_INVALIDDWORD ;
            pEdge2 = nullptr ;
        }

        uint32_t &GetVertexIdx(const uint32_t idx)
        {
            if ( idx == 0 )
                return dwVertexIdx0 ;
            else if ( idx == 1 )
                return dwVertexIdx1 ;
            else if ( idx == 2 )
                return dwVertexIdx2 ;
            else
                //assert( false ) ;
                return (uint32_t&) FLAG_INVALIDDWORD;
        }

        uint32_t &GetEdgeIdx(const uint32_t idx)
        {
            if ( idx == 0 )
                return dwEdgeIdx0 ;
            else if ( idx == 1 )
                return dwEdgeIdx1 ;
            else if ( idx == 2 )
                return dwEdgeIdx2 ;  
            else
                //assert( false ) ;
                return (uint32_t&) FLAG_INVALIDDWORD;
        }

        Edge *GetEdge(const uint32_t idx)
        {
            if ( idx == 0 )
                return pEdge0 ;
            else if ( idx == 1 )
                return pEdge1 ;
            else if ( idx == 2 )
                return pEdge2 ;
            else
                //assert( false ) ;
                return (Edge*)FLAG_INVALID_SIZE_T ;
        }

        bool HasVertexIdx(const uint32_t idx)
        {
            return ( dwVertexIdx0 == idx || dwVertexIdx1 == idx || dwVertexIdx2 == idx ) ;                  
        }
    };

    // the 3d vector used in the algorithm
    struct DVector3
    {
        double x, y, z ;

        DVector3( const DVector3 &R )
        {
            x = R.x ;
            y = R.y ;
            z = R.z ;
        }
        DVector3()
        {
            x = y = z = 0 ;
        }
        DVector3( const DVector2 &R )
        {
            x = R.x ;
            y = R.y ;
            z = 0 ;
        }

        double Length() 
        {
            return sqrt( x*x + y*y + z*z ) ;
        }
    };

    inline void DVector3Minus( const DVector3 &L, const DVector3 &R, DVector3 &Res )
    {
        Res.x = L.x - R.x ;
        Res.y = L.y - R.y ;
        Res.z = L.z - R.z ;
    }

    inline void DVector3Add( const DVector3 &L, const DVector3 &R, DVector3 &Res )
    {
        Res.x = L.x + R.x ;
        Res.y = L.y + R.y ;
        Res.z = L.z + R.z ;
    }

    inline void DVector3ScalarMul( DVector3 &L, double R )
    {
        L.x *= R ;
        L.y *= R ;
        L.z *= R ;
    }

    inline void DVector3Cross( const DVector3 &L, const DVector3 &R, DVector3 &Res )
    {
        Res.x = L.y * R.z - L.z * R.y;
        Res.y = L.z * R.x - L.x * R.z;
        Res.z = L.x * R.y - L.y * R.x;
    }

    inline double DVector3Dot( const DVector3 &L, const DVector3 &R )
    {
        return L.x*R.x + L.y*R.y + L.z*R.z ;
    }

    struct Vertex : public DVector3
    {       
        bool bBoundary ;                            // whether this is a boundary point
        double dAngle ;                             // the sum of the angle on all the faces that share this vertex
    
        double dLengthOfWindowEdgeToThisVertex ;    // the geodesic distance of a vertex is reported by a window near this vertex, this is the length that how far away the window is from this vertex, ideally, this should be zero
        double dGeoDistanceToSrc ;                  // the geodesic distance from the vertex to source vertex
        Edge *pEdgeReportedGeoDist ;

        bool bUsed ;                                // whether this vertex has been referred to in the index buffer (some vertices in a mesh exist but are not referred to by any indices)

        bool bShadowBoundary ;

        std::vector<Face*> facesAdj;     // faces that use this vertex
        std::vector<Edge*> edgesAdj;     // edges that have this vertex

        Vertex()  
        {
            bBoundary = false ;
            dAngle = 0 ;
            dGeoDistanceToSrc = FLT_MAX ;
            dLengthOfWindowEdgeToThisVertex = FLT_MAX ;
            pEdgeReportedGeoDist = nullptr ;
            bShadowBoundary = false ;
            bUsed = false ;
        }
    
        // get the index in the vertex array
        size_t GetIdx( TypeVertexList &VertexList )
        {
            return ((intptr_t)this - (intptr_t) &(VertexList[0])) / sizeof(Vertex);
        }

        // is this vertex a saddle or boundary one?
        bool IsSaddleBoundary()
        {
            return (bBoundary || (dAngle > (2*DirectX::XM_PI))) ;
        }
    };
}
