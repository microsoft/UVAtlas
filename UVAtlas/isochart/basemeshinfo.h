//-------------------------------------------------------------------------------------
// UVAtlas - basemeshinfo.h
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

#include "isochart.h"

// The original mesh information shared by CIsochartEngine methods
namespace Isochart
{

class CBaseMeshInfo
{
public:
    CBaseMeshInfo();
    ~CBaseMeshInfo();

    HRESULT Initialize( // Userd for building up data structure for partition
        const void* pfVertexArrayIn,
        size_t dwVertexCountIn,
        size_t dwVertexStrideIn,
        DXGI_FORMAT IndexFormatIn,
        const void* pdwFaceIndexArrayIn,
        size_t dwFaceCountIn,
        const FLOAT3* pfIMTArrayIn,
        const uint32_t* pdwFaceAdjacentArrayIn,
        const uint32_t*pdwSplitHintIn);

    HRESULT Initialize( //used for building up data structure for packing
        const void* pfVertexArrayIn,
        size_t dwVertexCountIn,
        size_t dwVertexStrideIn,
        size_t dwFaceCountIn,
        const uint32_t* pdwFaceAdjacentArrayIn);

    void Free();

    ////////// Attributes//////////////
    
    // Input information
    const void* pVertexArray;	// Pointer to original vertex buffer
    size_t dwVertexCount;		// Input vertex count
    size_t dwVertexStride;		// Stride of each vertex in pVertexArray
    size_t dwFaceCount;		 	// Input face count
    DXGI_FORMAT IndexFormat;		// DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT
    const FLOAT3* pfIMTArray;	// IMT array	

    const uint32_t *pdwOriginalFaceAdjacentArray;
    

    // Information calculated by initialization
    DirectX::XMFLOAT3* pVertPosition; 	//Internal vertex position, (by scale original position in pVertexArray) 

    DirectX::XMFLOAT3* pFaceNormalArray; // Normal of each face

    DirectX::XMFLOAT2* pFaceCanonicalUVCoordinate; // UV coordinates of each face after canonical transform
    DirectX::XMFLOAT3* pFaceCanonicalParamAxis;	//The X-axis and Y-axix used to tranform a 3D point on a  											
    
    float* pfFaceAreaArray;			// Area of each face
    uint32_t* pdwFaceAdjacentArray; 	// The 3 neighbors of each face.

    float fMeshArea;
    float fBoxDiagLen;				// Diagonal length of the mesh bounding box	

    float fOverturnTolerance;
    float fExpectAvgL2SquaredStretch;
    float fExpectMinAvgL2SquaredStretch; // Only used to optimize signal stretch.
    
    float fRatioOfSigToGeo;
    
    bool bIsFaceAdjacenctArrayReady;

    const uint32_t* pdwSplitHint;	// specified by user, all the edges can be splitted has the corresponding adjacency -1
private:
    HRESULT CopyAndScaleInputVertices();

    template <class INDEXTYPE>
    HRESULT ComputeInputFaceAttributes(
        const void* pdwFaceIndexArrayIn,
        const uint32_t* pdwFaceAdjacentArrayIn);

    // 
    // The order of the vertices is the same as the order in face index buffer.
    // congruent transform a triangle from 3D space to 2D space
    void CaculateCanonicalCoordinates(
        const DirectX::XMFLOAT3* pv3D0,	
        const DirectX::XMFLOAT3* pv3D1,	
        const DirectX::XMFLOAT3* pv3D2,	
        DirectX::XMFLOAT2* pv2D0,
        DirectX::XMFLOAT2* pv2D1,
        DirectX::XMFLOAT2* pv2D2,
        DirectX::XMFLOAT3* pAxis);
};

}
