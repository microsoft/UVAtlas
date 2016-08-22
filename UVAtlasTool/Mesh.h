//--------------------------------------------------------------------------------------
// File: Mesh.h
//
// Mesh processing helper class
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=324981
//--------------------------------------------------------------------------------------

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include <stdint.h>

#if defined(_XBOX_ONE) && defined(_TITLE)
#include <d3d11_x.h>
#define DCOMMON_H_INCLUDED
#else
#include <d3d11_1.h>
#endif

#include <directxmath.h>

#include "directxmesh.h"

class Mesh
{
public:
    Mesh() : mnFaces(0), mnVerts(0) {}
    Mesh(Mesh&& moveFrom);
    Mesh& operator= (Mesh&& moveFrom);

    Mesh(Mesh const&) = delete;
    Mesh& operator= (Mesh const&) = delete;

    // Methods
    void Clear();

    HRESULT SetIndexData( _In_ size_t nFaces, _In_reads_(nFaces*3) const uint16_t* indices, _In_reads_opt_(nFaces) uint32_t* attributes = nullptr );
    HRESULT SetIndexData( _In_ size_t nFaces, _In_reads_(nFaces*3) const uint32_t* indices, _In_reads_opt_(nFaces) uint32_t* attributes = nullptr );

    HRESULT SetVertexData( _Inout_ DirectX::VBReader& reader, _In_ size_t nVerts );

    HRESULT Validate( _In_ DWORD flags, _In_opt_ std::wstring* msgs ) const;

    HRESULT Clean( _In_ bool breakBowties=false );

    HRESULT GenerateAdjacency( _In_ float epsilon );

    HRESULT ComputeNormals( _In_ DWORD flags );

    HRESULT ComputeTangentFrame( _In_ bool bitangents );

    HRESULT UpdateFaces( _In_ size_t nFaces, _In_reads_(nFaces*3) const uint32_t* indices );

    HRESULT UpdateAttributes( _In_ size_t nFaces, _In_reads_(nFaces*3) const uint32_t* attributes );

    HRESULT UpdateUVs( _In_ size_t nVerts, _In_reads_(nVerts) const DirectX::XMFLOAT2* uvs );

    HRESULT VertexRemap( _In_reads_(nNewVerts) const uint32_t* remap, _In_ size_t nNewVerts );

    HRESULT ReverseWinding();

    HRESULT InvertVTexCoord();

    HRESULT ReverseHandedness();

    HRESULT VisualizeUVs();

    // Accessors
    const uint32_t* GetAttributeBuffer() const { return mAttributes.get(); }
    const uint32_t* GetAdjacencyBuffer() const { return mAdjacency.get(); }
    const DirectX::XMFLOAT3* GetPositionBuffer() const { return mPositions.get(); }
    const DirectX::XMFLOAT3* GetNormalBuffer() const { return mNormals.get(); }
    const DirectX::XMFLOAT2* GetTexCoordBuffer() const { return mTexCoords.get(); }
    const DirectX::XMFLOAT4* GetTangentBuffer() const { return mTangents.get(); }
    const DirectX::XMFLOAT4* GetColorBuffer() const { return mColors.get(); }

    size_t GetFaceCount() const { return mnFaces; }
    size_t GetVertexCount() const { return mnVerts; }

    bool Is16BitIndexBuffer() const;

    const uint32_t* GetIndexBuffer() const { return mIndices.get(); }
    std::unique_ptr<uint16_t []> GetIndexBuffer16() const;

    HRESULT GetVertexBuffer(_Inout_ DirectX::VBWriter& writer ) const;

    // Save mesh to file
    struct Material
    {
        std::wstring        name;
        bool                perVertexColor;
        float               specularPower;
        float               alpha;
        DirectX::XMFLOAT3   ambientColor;
        DirectX::XMFLOAT3   diffuseColor;
        DirectX::XMFLOAT3   specularColor;
        DirectX::XMFLOAT3   emissiveColor;
        std::wstring        texture;
    };

    HRESULT ExportToVBO( _In_z_ const wchar_t* szFileName ) const;
    HRESULT ExportToCMO( _In_z_ const wchar_t* szFileName, _In_ size_t nMaterials, _In_reads_opt_(nMaterials) const Material* materials ) const;
    HRESULT ExportToSDKMESH( _In_z_ const wchar_t* szFileName, _In_ size_t nMaterials, _In_reads_opt_(nMaterials) const Material* materials  ) const;

    // Create mesh from file
    static HRESULT CreateFromVBO( _In_z_ const wchar_t* szFileName, _Inout_ std::unique_ptr<Mesh>& result );

private:
    size_t                                      mnFaces;
    size_t                                      mnVerts;
    std::unique_ptr<uint32_t[]>                 mIndices;
    std::unique_ptr<uint32_t[]>                 mAttributes;
    std::unique_ptr<uint32_t[]>                 mAdjacency;
    std::unique_ptr<DirectX::XMFLOAT3[]>        mPositions;
    std::unique_ptr<DirectX::XMFLOAT3[]>        mNormals;
    std::unique_ptr<DirectX::XMFLOAT4[]>        mTangents;
    std::unique_ptr<DirectX::XMFLOAT3[]>        mBiTangents;
    std::unique_ptr<DirectX::XMFLOAT2[]>        mTexCoords;
    std::unique_ptr<DirectX::XMFLOAT4[]>        mColors;
    std::unique_ptr<DirectX::XMFLOAT4[]>        mBlendIndices;
    std::unique_ptr<DirectX::XMFLOAT4[]>        mBlendWeights;
};