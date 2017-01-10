//--------------------------------------------------------------------------------------
// File: Mesh.cpp
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

#include "mesh.h"

#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
#include <UVAtlas.h>

using namespace DirectX;

namespace
{
    struct handle_closer { void operator()(HANDLE h) { if (h) CloseHandle(h); } };

    typedef public std::unique_ptr<void, handle_closer> ScopedHandle;

    inline HANDLE safe_handle(HANDLE h) { return (h == INVALID_HANDLE_VALUE) ? 0 : h; }

    template<typename T> inline HRESULT write_file(HANDLE hFile, const T& value)
    {
        DWORD bytesWritten;
        if (!WriteFile(hFile, &value, static_cast<DWORD>(sizeof(T)), &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        if (bytesWritten != sizeof(T))
            return E_FAIL;

        return S_OK;
    }

    inline HRESULT write_file_string(HANDLE hFile, const wchar_t* value)
    {
        UINT length = (value) ? static_cast<UINT>( wcslen(value)+1 ) : 1;

        DWORD bytesWritten;
        if (!WriteFile(hFile, &length, static_cast<DWORD>(sizeof(UINT)), &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        if (bytesWritten != sizeof(UINT))
            return E_FAIL;

        if (length > 0)
        {
            DWORD bytes = static_cast<DWORD>(sizeof(wchar_t) * length);

            if (!WriteFile(hFile, value, bytes, &bytesWritten, nullptr))
                return HRESULT_FROM_WIN32(GetLastError());

            if (bytesWritten != bytes)
                return E_FAIL;
        }
        else
        {
            wchar_t nul = 0;
            if (!WriteFile(hFile, &nul, sizeof(wchar_t), &bytesWritten, nullptr))
                return HRESULT_FROM_WIN32(GetLastError());

            if (bytesWritten != sizeof(wchar_t))
                return E_FAIL;
        }

        return S_OK;
    }

    inline UINT64 roundup4k(UINT64 value)
    {
        return ((value + 4095) / 4096) * 4096;
    }

    static const uint8_t g_padding[4096] = { 0 };
}

// Move constructor
Mesh::Mesh(Mesh&& moveFrom)
{
    *this = std::move(moveFrom);
}

// Move operator
Mesh& Mesh::operator= (Mesh&& moveFrom)
{
    if (this != &moveFrom)
    {
        mnFaces = moveFrom.mnFaces;
        mnVerts = moveFrom.mnVerts;
        mIndices.swap( moveFrom.mIndices );
        mAttributes.swap( moveFrom.mAttributes );
        mAdjacency.swap( moveFrom.mAdjacency );
        mPositions.swap( moveFrom.mPositions );
        mNormals.swap( moveFrom.mNormals );
        mTangents.swap( moveFrom.mTangents );
        mBiTangents.swap( moveFrom.mBiTangents );
        mTexCoords.swap( moveFrom.mTexCoords );
        mColors.swap( moveFrom.mColors );
        mBlendIndices.swap( moveFrom.mBlendIndices );
        mBlendWeights.swap( moveFrom.mBlendWeights );
    }
    return *this;
}


//--------------------------------------------------------------------------------------
void Mesh::Clear()
{
    mnFaces = mnVerts = 0;

    // Release face data
    mIndices.reset();
    mAttributes.reset();
    mAdjacency.reset();

    // Release vertex data
    mPositions.reset();
    mNormals.reset();
    mTangents.reset();
    mBiTangents.reset();
    mTexCoords.reset();
    mColors.reset();
    mBlendIndices.reset();
    mBlendWeights.reset();
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::SetIndexData( size_t nFaces, const uint16_t* indices, uint32_t* attributes )
{
    if ( !nFaces || !indices )
        return E_INVALIDARG;

    if ( ( uint64_t(nFaces) * 3 ) >= UINT32_MAX )
        return HRESULT_FROM_WIN32( ERROR_ARITHMETIC_OVERFLOW );

    // Release face data
    mnFaces = 0;
    mIndices.reset();
    mAttributes.reset();

    std::unique_ptr<uint32_t[]> ib(new (std::nothrow) uint32_t[nFaces * 3]);
    if (!ib)
        return E_OUTOFMEMORY;

    for( size_t j = 0; j < (nFaces*3); ++j )
    {
        if ( indices[ j ] == uint16_t(-1) )
        {
            ib[ j ] = uint32_t(-1);
        }
        else
        {
            ib[ j ] = indices[ j ];
        }
    }

    std::unique_ptr<uint32_t[]> attr;
    if ( attributes )
    {
        attr.reset( new (std::nothrow) uint32_t[ nFaces ] );
        if ( !attr )
            return E_OUTOFMEMORY;
        
        memcpy( attr.get(), attributes, sizeof(uint32_t) * nFaces);
    }

    mIndices.swap(ib);
    mAttributes.swap(attr);
    mnFaces = nFaces;

    return S_OK;
}

_Use_decl_annotations_
HRESULT Mesh::SetIndexData( size_t nFaces, const uint32_t* indices, uint32_t* attributes )
{
    if ( !nFaces || !indices )
        return E_INVALIDARG;

    if ( ( uint64_t(nFaces) * 3 ) >= UINT32_MAX )
        return HRESULT_FROM_WIN32( ERROR_ARITHMETIC_OVERFLOW );

    mnFaces = 0;
    mIndices.reset();
    mAttributes.reset();

    std::unique_ptr<uint32_t[]> ib( new (std::nothrow) uint32_t[ nFaces * 3] );
    if ( !ib )
        return E_OUTOFMEMORY;

    memcpy( ib.get(), indices, sizeof(uint32_t) * nFaces * 3 );

    std::unique_ptr<uint32_t[]> attr;
    if ( attributes )
    {
        attr.reset( new (std::nothrow) uint32_t[ nFaces ] );
        if ( !attr )
            return E_OUTOFMEMORY;
        
        memcpy( attr.get(), attributes, sizeof(uint32_t) * nFaces );
    }

    mIndices.swap(ib);
    mAttributes.swap(attr);
    mnFaces = nFaces;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::SetVertexData( _Inout_ DirectX::VBReader& reader, _In_ size_t nVerts )
{
    if ( !nVerts )
        return E_INVALIDARG;

    // Release vertex data
    mnVerts = 0;
    mPositions.reset();
    mNormals.reset();
    mTangents.reset();
    mBiTangents.reset();
    mTexCoords.reset();
    mColors.reset();
    mBlendIndices.reset();
    mBlendWeights.reset();

    // Load positions (required)
    std::unique_ptr<XMFLOAT3[]> pos( new (std::nothrow) XMFLOAT3[ nVerts ] );
    if (!pos)
        return E_OUTOFMEMORY;
    
    HRESULT hr = reader.Read(pos.get(), "SV_Position", 0, nVerts);
    if (FAILED(hr))
        return hr;
    
    // Load normals
    std::unique_ptr<XMFLOAT3[]> norms;
    auto e = reader.GetElement11("NORMAL", 0);
    if (e)
    {
        norms.reset(new (std::nothrow) XMFLOAT3[nVerts]);
        if (!norms)
            return E_OUTOFMEMORY;

        hr = reader.Read(norms.get(), "NORMAL", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load tangents
    std::unique_ptr<XMFLOAT4[]> tans1;
    e = reader.GetElement11("TANGENT", 0);
    if (e)
    {
        tans1.reset(new (std::nothrow) XMFLOAT4[nVerts]);
        if (!tans1)
            return E_OUTOFMEMORY;

        hr = reader.Read(tans1.get(), "TANGENT", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load bi-tangents
    std::unique_ptr<XMFLOAT3[]> tans2;
    e = reader.GetElement11("BINORMAL", 0);
    if (e)
    {
        tans2.reset(new (std::nothrow) XMFLOAT3[nVerts]);
        if (!tans2)
            return E_OUTOFMEMORY;

        hr = reader.Read(tans2.get(), "BINORMAL", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load texture coordinates
    std::unique_ptr<XMFLOAT2[]> texcoord;
    e = reader.GetElement11("TEXCOORD", 0);
    if (e)
    {
        texcoord.reset(new (std::nothrow) XMFLOAT2[nVerts]);
        if (!texcoord)
            return E_OUTOFMEMORY;

        hr = reader.Read(texcoord.get(), "TEXCOORD", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load vertex colors
    std::unique_ptr<XMFLOAT4[]> colors;
    e = reader.GetElement11("COLOR", 0);
    if (e)
    {
        colors.reset(new (std::nothrow) XMFLOAT4[nVerts]);
        if (!colors)
            return E_OUTOFMEMORY;

        hr = reader.Read(colors.get(), "COLOR", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load skinning bone indices
    std::unique_ptr<XMFLOAT4[]> blendIndices;
    e = reader.GetElement11("BLENDINDICES", 0);
    if (e)
    {
        blendIndices.reset(new (std::nothrow) XMFLOAT4[nVerts]);
        if (!blendIndices)
            return E_OUTOFMEMORY;

        hr = reader.Read(blendIndices.get(), "BLENDINDICES", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Load skinning bone weights
    std::unique_ptr<XMFLOAT4[]> blendWeights;
    e = reader.GetElement11("BLENDWEIGHT", 0);
    if (e)
    {
        blendWeights.reset(new (std::nothrow) XMFLOAT4[nVerts]);
        if (!blendWeights)
            return E_OUTOFMEMORY;

        hr = reader.Read(blendWeights.get(), "BLENDWEIGHT", 0, nVerts);
        if (FAILED(hr))
            return hr;
    }

    // Return values
    mPositions.swap( pos );
    mNormals.swap( norms );
    mTangents.swap( tans1 );
    mBiTangents.swap( tans2 );
    mTexCoords.swap( texcoord );
    mColors.swap( colors );
    mBlendIndices.swap( blendIndices );
    mBlendWeights.swap( blendWeights );
    mnVerts = nVerts;

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::Validate(DWORD flags, std::wstring* msgs) const
{
    if (!mnFaces || !mIndices || !mnVerts)
        return E_UNEXPECTED;

    return DirectX::Validate(mIndices.get(), mnFaces, mnVerts, mAdjacency.get(), flags, msgs);
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::Clean( _In_ bool breakBowties )
{
    if (!mnFaces || !mIndices || !mnVerts || !mPositions)
        return E_UNEXPECTED;

    std::vector<uint32_t> dups;
    HRESULT hr = DirectX::Clean(mIndices.get(), mnFaces, mnVerts, mAdjacency.get(), mAttributes.get(), dups, breakBowties);
    if (FAILED(hr))
        return hr;

    if (dups.empty())
    {
        // No vertex duplication is needed for mesh clean
        return S_OK;
    }

    size_t nNewVerts = mnVerts + dups.size();

    std::unique_ptr<XMFLOAT3[]> pos(new (std::nothrow) XMFLOAT3[nNewVerts]);
    if (!pos)
        return E_OUTOFMEMORY;

    memcpy(pos.get(), mPositions.get(), sizeof(XMFLOAT3) * mnVerts);

    std::unique_ptr<XMFLOAT3[]> norms;
    if (mNormals)
    {
        norms.reset(new (std::nothrow) XMFLOAT3[nNewVerts]);
        if (!norms)
            return E_OUTOFMEMORY;

        memcpy(norms.get(), mNormals.get(), sizeof(XMFLOAT3) * mnVerts);
    }

    std::unique_ptr<XMFLOAT4[]> tans1;
    if (mTangents)
    {
        tans1.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!tans1)
            return E_OUTOFMEMORY;

        memcpy(tans1.get(), mTangents.get(), sizeof(XMFLOAT4) * mnVerts);
    }

    std::unique_ptr<XMFLOAT3[]> tans2;
    if (mBiTangents)
    {
        tans2.reset(new (std::nothrow) XMFLOAT3[nNewVerts]);
        if (!tans2)
            return E_OUTOFMEMORY;

        memcpy(tans2.get(), mBiTangents.get(), sizeof(XMFLOAT3) * mnVerts);
    }

    std::unique_ptr<XMFLOAT2[]> texcoord;
    if (mTexCoords)
    {
        texcoord.reset(new (std::nothrow) XMFLOAT2[nNewVerts]);
        if (!texcoord)
            return E_OUTOFMEMORY;

        memcpy(texcoord.get(), mTexCoords.get(), sizeof(XMFLOAT2) * mnVerts);
    }

    std::unique_ptr<XMFLOAT4[]> colors;
    if (mColors)
    {
        colors.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!colors)
            return E_OUTOFMEMORY;

        memcpy(colors.get(), mColors.get(), sizeof(XMFLOAT4) * mnVerts);
    }

    std::unique_ptr<XMFLOAT4[]> blendIndices;
    if (mBlendIndices)
    {
        blendIndices.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!blendIndices)
            return E_OUTOFMEMORY;

        memcpy(blendIndices.get(), mBlendIndices.get(), sizeof(XMFLOAT4) * mnVerts);
    }

    std::unique_ptr<XMFLOAT4 []> blendWeights;
    if (mBlendWeights)
    {
        blendWeights.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!blendWeights)
            return E_OUTOFMEMORY;

        memcpy(blendWeights.get(), mBlendWeights.get(), sizeof(XMFLOAT4) * mnVerts);
    }

    size_t j = mnVerts;
    for (auto it = dups.begin(); it != dups.end() && (j < nNewVerts); ++it, ++j)
    {
        assert(*it < mnVerts);

        pos[ j ] = mPositions[ *it ];

        if (norms)
        {
            norms[ j ] = mNormals[ *it ];
        }

        if (tans1)
        {
            tans1[ j ] = mTangents[ *it ];
        }

        if (tans2)
        {
            tans2[ j ] = mBiTangents[*it];
        }

        if (texcoord)
        {
            texcoord.get() [ j] = mTexCoords[*it];
        }

        if (colors)
        {
            colors[j] = mColors[*it];
        }

        if (blendIndices)
        {
            blendIndices[j] = mBlendIndices[*it];
        }

        if (blendWeights)
        {
            blendWeights[j] = mBlendWeights[*it];
        }
    }

    mPositions.swap(pos);
    mNormals.swap(norms);
    mTangents.swap(tans1);
    mBiTangents.swap(tans2);
    mTexCoords.swap(texcoord);
    mColors.swap(colors);
    mBlendIndices.swap(blendIndices);
    mBlendWeights.swap(blendWeights);
    mnVerts = nNewVerts;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::GenerateAdjacency( _In_ float epsilon )
{
    if (!mnFaces || !mIndices || !mnVerts || !mPositions)
        return E_UNEXPECTED;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    mAdjacency.reset( new (std::nothrow) uint32_t[ mnFaces * 3 ] );
    if ( !mAdjacency )
        return E_OUTOFMEMORY;

    return DirectX::GenerateAdjacencyAndPointReps(mIndices.get(), mnFaces, mPositions.get(), mnVerts, epsilon, nullptr, mAdjacency.get());
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::ComputeNormals( _In_ DWORD flags )
{
    if (!mnFaces || !mIndices || !mnVerts || !mPositions)
        return E_UNEXPECTED;

    mNormals.reset( new (std::nothrow) XMFLOAT3[ mnVerts ] );
    if (!mNormals)
        return E_OUTOFMEMORY;

    return DirectX::ComputeNormals(mIndices.get(), mnFaces, mPositions.get(), mnVerts, flags, mNormals.get());
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::ComputeTangentFrame( _In_ bool bitangents )
{
    if (!mnFaces || !mIndices || !mnVerts || !mPositions || !mNormals || !mTexCoords)
        return E_UNEXPECTED;

    mTangents.reset();
    mBiTangents.reset();

    std::unique_ptr<XMFLOAT4[]> tan1( new (std::nothrow) XMFLOAT4[mnVerts] );
    if (!tan1)
        return E_OUTOFMEMORY;

    std::unique_ptr<XMFLOAT3[]> tan2;
    if (bitangents)
    {
        tan2.reset( new (std::nothrow) XMFLOAT3[mnVerts] );
        if (!tan2)
            return E_OUTOFMEMORY;

        HRESULT hr = DirectX::ComputeTangentFrame(mIndices.get(), mnFaces, mPositions.get(), mNormals.get(), mTexCoords.get(), mnVerts,
                                                  tan1.get(), tan2.get());
        if (FAILED(hr))
            return hr;
    }
    else
    {
        mBiTangents.reset();

        HRESULT hr = DirectX::ComputeTangentFrame(mIndices.get(), mnFaces, mPositions.get(), mNormals.get(), mTexCoords.get(), mnVerts,
                                                  tan1.get());
        if (FAILED(hr))
            return hr;
    }

    mTangents.swap( tan1 );
    mBiTangents.swap( tan2 );

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::UpdateFaces( size_t nFaces, const uint32_t* indices )
{
    if (!nFaces || !indices)
        return E_INVALIDARG;

    if (!mnFaces || !mIndices)
        return E_UNEXPECTED;

    if ( mnFaces != nFaces )
        return E_FAIL;

    if ((uint64_t(nFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    memcpy( mIndices.get(), indices, sizeof(uint32_t) * 3 * nFaces );

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::UpdateAttributes( size_t nFaces, const uint32_t* attributes )
{
    if (!nFaces || !attributes)
        return E_INVALIDARG;

    if (!mnFaces || !mIndices || !mnVerts || !mPositions)
        return E_UNEXPECTED;

    if ( mnFaces != nFaces )
        return E_FAIL;

    if ( !mAttributes )
    {
        std::unique_ptr<uint32_t[]> attr( new (std::nothrow) uint32_t[ nFaces ] );
        if ( !attr )
            return E_OUTOFMEMORY;
        
        memcpy( attr.get(), attributes, sizeof(uint32_t) * nFaces );

        mAttributes.swap(attr);
    }
    else
    {
        memcpy( mAttributes.get(), attributes, sizeof(uint32_t) * nFaces );
    }

    std::unique_ptr<uint32_t> remap(new (std::nothrow) uint32_t[mnFaces]);
    if (!remap)
        return E_OUTOFMEMORY;

    HRESULT hr = AttributeSort(mnFaces, mAttributes.get(), remap.get());
    if (FAILED(hr))
        return hr;

    if (mAdjacency)
    {
        hr = ReorderIBAndAdjacency(mIndices.get(), mnFaces, mAdjacency.get(), remap.get());
    }
    else
    {
        hr = ReorderIB(mIndices.get(), mnFaces, remap.get());
    }
    if (FAILED(hr))
        return hr;

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::UpdateUVs(size_t nVerts, const XMFLOAT2* uvs)
{
    if (!nVerts || !uvs)
        return E_INVALIDARG;

    if (!mnVerts || !mPositions)
        return E_UNEXPECTED;

    if (nVerts != mnVerts)
        return E_FAIL;

    if (!mTexCoords)
    {
        std::unique_ptr<XMFLOAT2 []> texcoord;
        texcoord.reset(new (std::nothrow) XMFLOAT2[mnVerts]);
        if (!texcoord)
            return E_OUTOFMEMORY;

        memcpy(texcoord.get(), uvs, sizeof(XMFLOAT2) * mnVerts);

        mTexCoords.swap(texcoord);
    }
    else
    {
        memcpy(mTexCoords.get(), uvs, sizeof(XMFLOAT2) * mnVerts);
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::VertexRemap( const uint32_t* remap, size_t nNewVerts )
{
    if (!remap || !nNewVerts)
        return E_INVALIDARG;

    if (!mnVerts || !mPositions)
        return E_UNEXPECTED;

    if (nNewVerts < mnVerts)
        return E_FAIL;

    std::unique_ptr<XMFLOAT3[]> pos(new (std::nothrow) XMFLOAT3[nNewVerts]);
    if (!pos)
        return E_OUTOFMEMORY;

    HRESULT hr = UVAtlasApplyRemap(mPositions.get(), sizeof(XMFLOAT3), mnVerts, nNewVerts, remap, pos.get() );
    if (FAILED(hr))
        return hr;

    std::unique_ptr<XMFLOAT3[]> norms;
    if (mNormals)
    {
        norms.reset(new (std::nothrow) XMFLOAT3[nNewVerts]);
        if (!norms)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mNormals.get(), sizeof(XMFLOAT3), mnVerts, nNewVerts, remap, norms.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT4[]> tans1;
    if (mTangents)
    {
        tans1.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!tans1)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mTangents.get(), sizeof(XMFLOAT4), mnVerts, nNewVerts, remap, tans1.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT3[]> tans2;
    if (mBiTangents)
    {
        tans2.reset(new (std::nothrow) XMFLOAT3[nNewVerts]);
        if (!tans2)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mBiTangents.get(), sizeof(XMFLOAT3), mnVerts, nNewVerts, remap, tans2.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT2[]> texcoord;
    if (mTexCoords)
    {
        texcoord.reset(new (std::nothrow) XMFLOAT2[nNewVerts]);
        if (!texcoord)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mTexCoords.get(), sizeof(XMFLOAT2), mnVerts, nNewVerts, remap, texcoord.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT4[]> colors;
    if (mColors)
    {
        colors.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!colors)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mColors.get(), sizeof(XMFLOAT4), mnVerts, nNewVerts, remap, colors.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT4[]> blendIndices;
    if (mBlendIndices)
    {
        blendIndices.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!blendIndices)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mBlendIndices.get(), sizeof(XMFLOAT4), mnVerts, nNewVerts, remap, blendIndices.get() );
        if (FAILED(hr))
            return hr;
    }

    std::unique_ptr<XMFLOAT4 []> blendWeights;
    if (mBlendWeights)
    {
        blendWeights.reset(new (std::nothrow) XMFLOAT4[nNewVerts]);
        if (!blendWeights)
            return E_OUTOFMEMORY;

        hr = UVAtlasApplyRemap(mBlendWeights.get(), sizeof(XMFLOAT4), mnVerts, nNewVerts, remap, blendWeights.get() );
        if (FAILED(hr))
            return hr;
    }

    mPositions.swap(pos);
    mNormals.swap(norms);
    mTangents.swap(tans1);
    mBiTangents.swap(tans2);
    mTexCoords.swap(texcoord);
    mColors.swap(colors);
    mBlendIndices.swap(blendIndices);
    mBlendWeights.swap(blendWeights);
    mnVerts = nNewVerts;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::ReverseWinding()
{
    if (!mIndices || !mnFaces)
        return E_UNEXPECTED;

    auto iptr = mIndices.get();
    for (size_t j = 0; j < mnFaces; ++j)
    {
        std::swap( *iptr, *(iptr + 2) );
        iptr += 3;
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::InvertVTexCoord()
{
    if (!mTexCoords)
        return E_UNEXPECTED;

    auto tptr = mTexCoords.get();
    for (size_t j = 0; j < mnVerts; ++j, ++tptr)
    {
        tptr->y = 1.f - tptr->y;
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::ReverseHandedness()
{
    if (!mPositions)
        return E_UNEXPECTED;

    auto ptr = mPositions.get();
    for (size_t j = 0; j < mnVerts; ++j, ++ptr)
    {
        ptr->z = -ptr->z;
    }

    if (mNormals)
    {
        auto nptr = mNormals.get();
        for (size_t j = 0; j < mnVerts; ++j, ++nptr)
        {
            nptr->z = -nptr->z;
        }
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::VisualizeUVs()
{
    if (!mnVerts || !mPositions || !mTexCoords)
        return E_UNEXPECTED;

    const XMFLOAT2* sptr = mTexCoords.get();
    XMFLOAT3* dptr = mPositions.get();
    for (size_t j = 0; j < mnVerts; ++j)
    {
        dptr->x = sptr->x;
        dptr->y = sptr->y;
        dptr->z = 0;
        ++sptr;
        ++dptr;
    }

    if (mNormals)
    {
        XMFLOAT3* nptr = mNormals.get();
        for (size_t j = 0; j < mnVerts; ++j)
        {
            XMStoreFloat3( nptr, g_XMIdentityR2 );
            ++nptr;
        }
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
bool Mesh::Is16BitIndexBuffer() const
{
    if (!mIndices || !mnFaces)
        return false;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return false;

    const uint32_t* iptr = mIndices.get();
    for (size_t j = 0; j < (mnFaces * 3); ++j )
    {
        uint32_t index = *(iptr++);
        if (index != uint32_t(-1)
            && (index >= UINT16_MAX))
        {
            return false;
        }
    }

    return true;
}


//--------------------------------------------------------------------------------------
std::unique_ptr<uint16_t[]> Mesh::GetIndexBuffer16() const
{
    std::unique_ptr<uint16_t[]> ib;

    if (!mIndices || !mnFaces)
        return ib;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return ib;

    size_t count = mnFaces * 3;

    ib.reset(new (std::nothrow) uint16_t[count]);
    if (!ib)
        return ib;

    const uint32_t* iptr = mIndices.get();
    for (size_t j = 0; j < count; ++j)
    {
        uint32_t index = *(iptr++);
        if (index == uint32_t(-1))
        {
            ib[j] = uint16_t(-1);
        }
        else if (index >= UINT16_MAX)
        {
            ib.reset();
            return ib;
        }
        else
        {
            ib[j] = static_cast<uint16_t>(index);
        }
    }

    return ib;
}


//--------------------------------------------------------------------------------------
HRESULT Mesh::GetVertexBuffer(_Inout_ DirectX::VBWriter& writer) const
{
    if (!mnVerts || !mPositions)
        return E_UNEXPECTED;

    HRESULT hr = writer.Write(mPositions.get(), "SV_Position", 0, mnVerts );
    if (FAILED(hr))
        return hr;

    if (mNormals)
    {
        auto e = writer.GetElement11("NORMAL", 0);
        if (e)
        {
            hr = writer.Write(mNormals.get(), "NORMAL", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mTangents)
    {
        auto e = writer.GetElement11("TANGENT", 0);
        if (e)
        {
            hr = writer.Write(mTangents.get(), "TANGENT", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mBiTangents)
    {
        auto e = writer.GetElement11("BINORMAL", 0);
        if (e)
        {
            hr = writer.Write(mBiTangents.get(), "BINORMAL", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mTexCoords)
    {
        auto e = writer.GetElement11("TEXCOORD", 0);
        if (e)
        {
            hr = writer.Write(mTexCoords.get(), "TEXCOORD", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mColors)
    {
        auto e = writer.GetElement11("COLOR", 0);
        if (e)
        {
            hr = writer.Write(mColors.get(), "COLOR", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mBlendIndices)
    {
        auto e = writer.GetElement11("BLENDINDICES", 0);
        if (e)
        {
            hr = writer.Write(mBlendIndices.get(), "BLENDINDICES", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    if (mBlendWeights)
    {
        auto e = writer.GetElement11("BLENDWEIGHT", 0);
        if (e)
        {
            hr = writer.Write(mBlendWeights.get(), "BLENDWEIGHT", 0, mnVerts);
            if (FAILED(hr))
                return hr;
        }
    }

    return S_OK;
}


//======================================================================================
// VBO
//======================================================================================

namespace VBO
{

#pragma pack(push,1)

    struct header_t
    {
        uint32_t numVertices;
        uint32_t numIndices;
    };

    struct vertex_t
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 textureCoordinate;
    };

#pragma pack(pop)

    static_assert(sizeof(header_t) == 8, "VBO header size mismatch");
    static_assert(sizeof(vertex_t) == 32, "VBO vertex size mismatch");
}; // namespace


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::ExportToVBO( const wchar_t* szFileName ) const
{
    using namespace VBO;

    if ( !szFileName )
        return E_INVALIDARG;

    if (!mnFaces || !mIndices || !mnVerts || !mPositions || !mNormals || !mTexCoords)
        return E_UNEXPECTED;

    if ( ( uint64_t(mnFaces) * 3 ) >= UINT32_MAX )
        return HRESULT_FROM_WIN32( ERROR_ARITHMETIC_OVERFLOW );

    if ( mnVerts >= UINT16_MAX )
        return HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED );

    // Setup VBO header
    header_t header;
    header.numVertices = static_cast<uint32_t>( mnVerts );
    header.numIndices = static_cast<uint32_t>( mnFaces*3 );

    // Setup vertices/indices for VBO

    std::unique_ptr<vertex_t[]> vb(new (std::nothrow) vertex_t[mnVerts]);
    std::unique_ptr<uint16_t[]> ib(new (std::nothrow) uint16_t[header.numIndices]);
    if (!vb || !ib)
        return E_OUTOFMEMORY;

    // Copy to VB
    auto vptr = vb.get();
    for (size_t j = 0; j < mnVerts; ++j, ++vptr)
    {
        vptr->position = mPositions[j];
        vptr->normal = mNormals[j];
        vptr->textureCoordinate = mTexCoords[j];
    }

    // Copy to IB
    auto iptr = ib.get();
    for (size_t j = 0; j < header.numIndices; ++j, ++iptr)
    {
        uint32_t index = mIndices[j];
        if (index == uint32_t(-1))
        {
            *iptr = uint16_t(-1);
        }
        else if (index >= UINT16_MAX)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        else
        {
            *iptr = static_cast<uint16_t>(index);
        }
    }

    // Write header and data
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
    ScopedHandle hFile(safe_handle(CreateFile2(szFileName, GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr)));
#else
    ScopedHandle hFile(safe_handle(CreateFileW(szFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)));
#endif
    if (!hFile)
        return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = write_file( hFile.get(), header );
    if (FAILED(hr))
        return hr;

    DWORD vertSize = static_cast<DWORD>( sizeof(vertex_t) * header.numVertices );

    DWORD bytesWritten;
    if (!WriteFile(hFile.get(), vb.get(), vertSize, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != vertSize)
        return E_FAIL;

    DWORD indexSize = static_cast<DWORD>( sizeof(uint16_t) * header.numIndices );

    if (!WriteFile(hFile.get(), ib.get(), indexSize, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != indexSize)
        return E_FAIL;

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::CreateFromVBO( const wchar_t* szFileName, std::unique_ptr<Mesh>& result )
{
    using namespace VBO;

    if (!szFileName)
        return E_INVALIDARG;

    result.reset();

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
    ScopedHandle hFile(safe_handle(CreateFile2(szFileName, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr)));
#else
    ScopedHandle hFile(safe_handle(CreateFileW(szFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)));
#endif
    if (!hFile)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Get the file size
    FILE_STANDARD_INFO fileInfo;
    if (!GetFileInformationByHandleEx(hFile.get(), FileStandardInfo, &fileInfo, sizeof(fileInfo)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // File is too big for 32-bit allocation, so reject read
    if (fileInfo.EndOfFile.HighPart > 0)
        return E_FAIL;

    // Need at least enough data to read the header
    if (fileInfo.EndOfFile.LowPart < sizeof(header_t))
        return E_FAIL;

    // Read VBO header
    DWORD bytesRead = 0;

    header_t header;
    if (!ReadFile(hFile.get(), &header, sizeof(header_t), &bytesRead, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (bytesRead != sizeof(header))
        return E_FAIL;

    if (!header.numVertices || !header.numIndices)
        return E_FAIL;

    result.reset( new (std::nothrow) Mesh );
    if (!result)
        return E_OUTOFMEMORY;

    // Read vertices/indices from VBO
    std::unique_ptr<vertex_t[]> vb(new (std::nothrow) vertex_t[header.numVertices]);
    std::unique_ptr<uint16_t[]> ib( new (std::nothrow) uint16_t[ header.numIndices ] );
    if (!vb || !ib)
        return E_OUTOFMEMORY;

    DWORD vertSize = static_cast<DWORD>( sizeof(vertex_t) * header.numVertices );

    if (!ReadFile(hFile.get(), vb.get(), vertSize, &bytesRead, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (bytesRead != vertSize)
        return E_FAIL;

    DWORD indexSize = static_cast<DWORD>(sizeof(uint16_t) * header.numIndices );

    if (!ReadFile(hFile.get(), ib.get(), indexSize, &bytesRead, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (bytesRead != indexSize)
        return E_FAIL;

    // Copy VB to result
    std::unique_ptr<XMFLOAT3[]> pos(new (std::nothrow) XMFLOAT3[header.numVertices]);
    std::unique_ptr<XMFLOAT3[]> norm(new (std::nothrow) XMFLOAT3[header.numVertices]);
    std::unique_ptr<XMFLOAT2[]> texcoord(new (std::nothrow) XMFLOAT2[header.numVertices]);
    if (!pos || !norm || !texcoord)
        return E_OUTOFMEMORY;

    auto vptr = vb.get();
    for (size_t j = 0; j < header.numVertices; ++j, ++vptr)
    {
        pos[ j ] = vptr->position;
        norm[ j ] = vptr->normal;
        texcoord[ j] = vptr->textureCoordinate;
    }

    // Copy IB to result
    std::unique_ptr<uint32_t[]> indices(new (std::nothrow) uint32_t[header.numIndices]);
    if (!indices)
        return E_OUTOFMEMORY;

    auto iptr = ib.get();
    for (size_t j = 0; j < header.numIndices; ++j, ++iptr)
    {
        uint16_t index = *iptr;
        if (index == uint16_t(-1))
            indices[ j ] = uint32_t(-1);
        else
            indices[ j ] = index;
    }

    result->mPositions.swap(pos);
    result->mNormals.swap(norm);
    result->mTexCoords.swap(texcoord);
    result->mIndices.swap(indices);
    result->mnVerts = header.numVertices;
    result->mnFaces = header.numIndices / 3;

    return S_OK;
}


//======================================================================================
// Visual Studio CMO
//======================================================================================

//--------------------------------------------------------------------------------------
// .CMO files are built by Visual Studio 2012 and an example renderer is provided
// in the VS Direct3D Starter Kit
// http://code.msdn.microsoft.com/Visual-Studio-3D-Starter-455a15f1
//--------------------------------------------------------------------------------------

namespace VSD3DStarter
{
    // .CMO files

    // UINT - Mesh count
    // { [Mesh count]
    //      UINT - Length of name
    //      wchar_t[] - Name of mesh (if length > 0)
    //      UINT - Material count
    //      { [Material count]
    //          UINT - Length of material name
    //          wchar_t[] - Name of material (if length > 0)
    //          Material structure
    //          UINT - Length of pixel shader name
    //          wchar_t[] - Name of pixel shader (if length > 0)
    //          { [8]
    //              UINT - Length of texture name
    //              wchar_t[] - Name of texture (if length > 0)
    //          }
    //      }
    //      BYTE - 1 if there is skeletal animation data present
    //      UINT - SubMesh count
    //      { [SubMesh count]
    //          SubMesh structure
    //      }
    //      UINT - IB Count
    //      { [IB Count]
    //          UINT - Number of USHORTs in IB
    //          USHORT[] - Array of indices
    //      }
    //      UINT - VB Count
    //      { [VB Count]
    //          UINT - Number of verts in VB
    //          Vertex[] - Array of vertices
    //      }
    //      UINT - Skinning VB Count
    //      { [Skinning VB Count]
    //          UINT - Number of verts in Skinning VB
    //          SkinningVertex[] - Array of skinning verts
    //      }
    //      MeshExtents structure
    //      [If skeleton animation data is not present, file ends here]
    //      UINT - Bone count
    //      { [Bone count]
    //          UINT - Length of bone name
    //          wchar_t[] - Bone name (if length > 0)
    //          Bone structure
    //      }
    //      UINT - Animation clip count
    //      { [Animation clip count]
    //          UINT - Length of clip name
    //          wchar_t[] - Clip name (if length > 0)
    //          float - Start time
    //          float - End time
    //          UINT - Keyframe count
    //          { [Keyframe count]
    //              Keyframe structure
    //          }
    //      }
    // }

#pragma pack(push,1)

    struct Material
    {
        DirectX::XMFLOAT4   Ambient;
        DirectX::XMFLOAT4   Diffuse;
        DirectX::XMFLOAT4   Specular;
        float               SpecularPower;
        DirectX::XMFLOAT4   Emissive;
        DirectX::XMFLOAT4X4 UVTransform;
    };

    const uint32_t MAX_TEXTURE = 8;

    struct SubMesh
    {
        UINT MaterialIndex;
        UINT IndexBufferIndex;
        UINT VertexBufferIndex;
        UINT StartIndex;
        UINT PrimCount;
    };

    const uint32_t NUM_BONE_INFLUENCES = 4;

    struct Vertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT4 Tangent;
        UINT color;
        DirectX::XMFLOAT2 TextureCoordinates;
    };

    struct SkinningVertex
    {
        UINT boneIndex[NUM_BONE_INFLUENCES];
        float boneWeight[NUM_BONE_INFLUENCES];
    };

    struct MeshExtents
    {
        float CenterX, CenterY, CenterZ;
        float Radius;

        float MinX, MinY, MinZ;
        float MaxX, MaxY, MaxZ;
    };

    struct Bone
    {
        INT ParentIndex;
        DirectX::XMFLOAT4X4 InvBindPos;
        DirectX::XMFLOAT4X4 BindPos;
        DirectX::XMFLOAT4X4 LocalTransform;
    };

    struct Clip
    {
        float StartTime;
        float EndTime;
        UINT  keys;
    };

    struct Keyframe
    {
        UINT BoneIndex;
        float Time;
        DirectX::XMFLOAT4X4 Transform;
    };

#pragma pack(pop)

}; // namespace

static_assert(sizeof(VSD3DStarter::Material) == 132, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::SubMesh) == 20, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::Vertex) == 52, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::SkinningVertex) == 32, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::MeshExtents) == 40, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::Bone) == 196, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::Clip) == 12, "CMO Mesh structure size incorrect");
static_assert(sizeof(VSD3DStarter::Keyframe) == 72, "CMO Mesh structure size incorrect");


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::ExportToCMO(const wchar_t* szFileName, size_t nMaterials, const Material* materials) const
{
    using namespace VSD3DStarter;

    if (!szFileName)
        return E_INVALIDARG;

    if (nMaterials > 0 && !materials)
        return E_INVALIDARG;

    if (!mnFaces || !mIndices || !mnVerts || !mPositions || !mNormals || !mTexCoords || !mTangents)
        return E_UNEXPECTED;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    if (mnVerts >= UINT16_MAX)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    UINT nIndices = static_cast<UINT>( mnFaces * 3 );

    // Setup vertices/indices for CMO
    std::unique_ptr<Vertex []> vb(new (std::nothrow) Vertex[mnVerts]);
    std::unique_ptr<uint16_t []> ib(new (std::nothrow) uint16_t[nIndices]);
    if (!vb || !ib)
        return E_OUTOFMEMORY;

    std::unique_ptr<SkinningVertex []> vbSkin;
    if (mBlendIndices && mBlendWeights)
    {
        vbSkin.reset(new (std::nothrow) SkinningVertex[mnVerts]);
        if (!vbSkin)
            return E_OUTOFMEMORY;
    }

    // Copy to VB
    auto vptr = vb.get();
    for (size_t j = 0; j < mnVerts; ++j, ++vptr)
    {
        vptr->Position = mPositions[j];
        vptr->Normal = mNormals[j];
        vptr->Tangent = mTangents[j];
        vptr->TextureCoordinates = mTexCoords[j];

        if (mColors)
        {
            XMVECTOR icolor = XMLoadFloat4(&mColors[j]);
            PackedVector::XMUBYTEN4 rgba;
            PackedVector::XMStoreUByteN4(&rgba, icolor);
            vptr->color = rgba.v;
        }
        else
            vptr->color = 0xFFFFFFFF;
    }

    // Copy to SkinVB
    auto sptr = vbSkin.get();
    if ( sptr )
    {
        for (size_t j = 0; j < mnVerts; ++j, ++sptr)
        {
            XMVECTOR v = XMLoadFloat4(&mBlendIndices[j]);
            XMStoreUInt4( reinterpret_cast<XMUINT4*>( &sptr->boneIndex[0] ), v);

            const XMFLOAT4* w = &mBlendWeights[j];
            sptr->boneWeight[0] = w->x;
            sptr->boneWeight[1] = w->y;
            sptr->boneWeight[2] = w->z;
            sptr->boneWeight[3] = w->w;
        }
    }

    // Copy to IB
    auto iptr = ib.get();
    for (size_t j = 0; j < nIndices; ++j, ++iptr)
    {
        uint32_t index = mIndices[j];
        if (index == uint32_t(-1))
        {
            *iptr = uint16_t(-1);
        }
        else if (index >= UINT16_MAX)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        else
        {
            *iptr = static_cast<uint16_t>(index);
        }
    }

    // Create CMO file
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
    ScopedHandle hFile(safe_handle(CreateFile2(szFileName, GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr)));
#else
    ScopedHandle hFile(safe_handle(CreateFileW(szFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)));
#endif
    if (!hFile)
        return HRESULT_FROM_WIN32(GetLastError());

    // Write 1 mesh, name based on the filename
    UINT n = 1;
    HRESULT hr = write_file(hFile.get(), n);
    if (FAILED(hr))
        return hr;

    {
        wchar_t fname[_MAX_FNAME];
        _wsplitpath_s(szFileName, nullptr, 0, nullptr, 0, fname, _MAX_FNAME, nullptr, 0);

        hr = write_file_string(hFile.get(), fname);
        if (FAILED(hr))
            return hr;
    }

    // Write materials
    static const Mesh::Material s_defMaterial = { L"default", false, 1.f, 1.f,
        XMFLOAT3(0.2f, 0.2f, 0.2f), XMFLOAT3(0.8f, 0.8f, 0.8f),
        XMFLOAT3(0.f, 0.f, 0.f), XMFLOAT3(0.f, 0.f, 0.f), L"" };

    UINT materialCount = 1;
    if (nMaterials > 0)
    {
        materialCount = static_cast<UINT>(nMaterials);
    }
    else
    {
        nMaterials = 1;
        materials = &s_defMaterial;
    }

    hr = write_file(hFile.get(), materialCount);
    if (FAILED(hr))
        return hr;

    for (UINT j = 0; j < materialCount; ++j)
    {
        auto& m = materials[j];

        if ( !m.name.empty() )
        {
            hr = write_file_string(hFile.get(), m.name.c_str() );
        }
        else
        {
            wchar_t name[64];
            swprintf_s(name, L"material%03u\n", j);
            hr = write_file_string(hFile.get(), name);
        }
        if (FAILED(hr))
            return hr;

        VSD3DStarter::Material mdata;
        memset( &mdata, 0, sizeof(mdata) );

        mdata.Ambient.x = m.ambientColor.x;
        mdata.Ambient.y = m.ambientColor.y;
        mdata.Ambient.z = m.ambientColor.z;
        mdata.Ambient.w = 1.f;

        mdata.Diffuse.x = m.diffuseColor.x;
        mdata.Diffuse.y = m.diffuseColor.y;
        mdata.Diffuse.z = m.diffuseColor.z;
        mdata.Diffuse.w = m.alpha;

        if (m.specularColor.x > 0.f || m.specularColor.y > 0.f || m.specularColor.z > 0.f)
        {
            mdata.Specular.x = m.specularColor.x;
            mdata.Specular.y = m.specularColor.y;
            mdata.Specular.z = m.specularColor.z;
            mdata.SpecularPower = ( m.specularPower <= 0.f ) ? 16.f : m.specularPower;
        }
        else
        {
            mdata.SpecularPower = 1.f;
        }
        mdata.Specular.w = 1.f;

        mdata.Emissive.x = m.emissiveColor.x;
        mdata.Emissive.y = m.emissiveColor.y;
        mdata.Emissive.z = m.emissiveColor.z;
        mdata.Emissive.w = 1.f;

        XMMATRIX id = XMMatrixIdentity();
        XMStoreFloat4x4(&mdata.UVTransform, id);

        hr = write_file(hFile.get(), mdata);
        if (FAILED(hr))
            return hr;

        if (m.specularColor.x > 0.f || m.specularColor.y > 0.f || m.specularColor.z > 0.f)
        {
            hr = write_file_string(hFile.get(), L"phong.dgsl");
        }
        else
        {
            hr = write_file_string(hFile.get(), L"lambert.dgsl");
        }
        if (FAILED(hr))
            return hr;

        hr = write_file_string(hFile.get(), m.texture.c_str() );
        if (FAILED(hr))
            return hr;

        for (size_t k = 1; k < MAX_TEXTURE; ++k)
        {
            hr = write_file_string(hFile.get(), L"" );
            if (FAILED(hr))
                return hr;
        }
    }

    BYTE sd = 0; // No skeleton/animation data
    hr = write_file(hFile.get(), sd);
    if (FAILED(hr))
        return hr;

    if (mAttributes)
    {
        auto subsets = ComputeSubsets(mAttributes.get(), mnFaces);

        n = static_cast<UINT>(subsets.size());
        hr = write_file(hFile.get(), n);
        if (FAILED(hr))
            return hr;

        size_t startIndex = 0;
        for (auto it = subsets.cbegin(); it != subsets.end(); ++it)
        {
            SubMesh smesh;
            smesh.MaterialIndex = mAttributes[it->first];
            if (smesh.MaterialIndex >= nMaterials)
                smesh.MaterialIndex = 0;

            smesh.IndexBufferIndex = 0;
            smesh.VertexBufferIndex = 0;
            smesh.StartIndex = static_cast<UINT>( startIndex );
            smesh.PrimCount = static_cast<UINT>( it->second );
            hr = write_file(hFile.get(), smesh);
            if (FAILED(hr))
                return hr;

            if ((startIndex + (it->second * 3)) > mnFaces * 3)
                return E_FAIL;

            startIndex += smesh.PrimCount * 3;
        }
    }
    else
    {
        n = 1;
        hr = write_file(hFile.get(), n);
        if (FAILED(hr))
            return hr;

        SubMesh smesh;
        smesh.MaterialIndex = 0;
        smesh.IndexBufferIndex = 0;
        smesh.VertexBufferIndex = 0;
        smesh.StartIndex = 0;
        smesh.PrimCount = static_cast<UINT>(mnFaces);

        hr = write_file(hFile.get(), smesh);
        if (FAILED(hr))
            return hr;
    }

    // Write indices (one IB shared across submeshes)
    n = 1;
    hr = write_file(hFile.get(), n);
    if (FAILED(hr))
        return hr;

    hr = write_file(hFile.get(), nIndices);
    if (FAILED(hr))
        return hr;

    DWORD indexSize = static_cast<DWORD>(sizeof(uint16_t) * nIndices);

    DWORD bytesWritten;
    if (!WriteFile(hFile.get(), ib.get(), indexSize, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != indexSize)
        return E_FAIL;

    // Write vertices (one VB shared across submeshes)
    n = 1;
    hr = write_file(hFile.get(), n);
    if (FAILED(hr))
        return hr;

    n = static_cast<UINT>( mnVerts );
    hr = write_file(hFile.get(), n);
    if (FAILED(hr))
        return hr;

    DWORD vertSize = static_cast<DWORD>(sizeof(Vertex) * mnVerts);

    if (!WriteFile(hFile.get(), vb.get(), vertSize, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != vertSize)
        return E_FAIL;

    // Write skinning vertices (one SkinVB shared across submeshes)
    if ( vbSkin )
    {
        n = 1;
        hr = write_file(hFile.get(), n);
        if (FAILED(hr))
            return hr;

        n = static_cast<UINT>(mnVerts);
        hr = write_file(hFile.get(), n);
        if (FAILED(hr))
            return hr;

        DWORD skinVertSize = static_cast<DWORD>(sizeof(SkinningVertex) * mnVerts);

        if (!WriteFile(hFile.get(), vbSkin.get(), skinVertSize, &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        if (bytesWritten != skinVertSize)
            return E_FAIL;
    }
    else
    {
        n = 0;
        hr = write_file(hFile.get(), n);
        if (FAILED(hr))
            return hr;
    }

    // Write extents
    {
        BoundingSphere sphere;
        BoundingSphere::CreateFromPoints(sphere, mnVerts, mPositions.get(), sizeof(XMFLOAT3));

        BoundingBox box;
        BoundingBox::CreateFromPoints(box, mnVerts, mPositions.get(), sizeof(XMFLOAT3));

        MeshExtents extents;
        extents.CenterX = sphere.Center.x;
        extents.CenterY = sphere.Center.y;
        extents.CenterZ = sphere.Center.z;
        extents.Radius = sphere.Radius;

        extents.MinX = box.Center.x - box.Extents.x;
        extents.MinY = box.Center.y - box.Extents.y;
        extents.MinZ = box.Center.z - box.Extents.z;

        extents.MaxX = box.Center.x + box.Extents.x;
        extents.MaxY = box.Center.y + box.Extents.y;
        extents.MaxZ = box.Center.z + box.Extents.z;

        hr = write_file(hFile.get(), extents);
        if (FAILED(hr))
            return hr;
    }

    // No skeleton data, so no animations

    return S_OK;
}
    


//======================================================================================
// SDKMESH
//======================================================================================

//--------------------------------------------------------------------------------------
// SDKMESH format is generated by the legacy DirectX SDK's Content Exporter and
// originally rendered by the DXUT helper class SDKMesh
//
// http://go.microsoft.com/fwlink/?LinkId=226208
//--------------------------------------------------------------------------------------
namespace DXUT
{
    // .SDKMESH files

    // SDKMESH_HEADER
    // SDKMESH_VERTEX_BUFFER_HEADER header->VertexStreamHeadersOffset
    // SDKMESH_INDEX_BUFFER_HEADER  header->IndexStreamHeadersOffset
    // SDKMESH_MESH                 header->MeshDataOffset
    // SDKMESH_SUBSET               header->SubsetDataOffset
    // SDKMESH_FRAME                header->FrameDataOffset
    // SDKMESH_MATERIAL             header->MaterialDataOffset
    // [header->NonBufferDataSize]
    // { [ header->NumVertexBuffers]
    //      VB data
    // }
    // { [ header->NumIndexBuffers]
    //      IB data
    // }


    // .SDDKANIM files

    // SDKANIMATION_FILE_HEADER
    // BYTE[] - Length of fileheader->AnimationDataSize

    // .SDKMESH uses Direct3D 9 decls, but only a subset of these is ever generated by the legacy DirectX SDK Content Exporter

    // D3DDECLUSAGE_POSITION / D3DDECLTYPE_FLOAT3
    // (D3DDECLUSAGE_BLENDWEIGHT / D3DDECLTYPE_UBYTE4N
    // D3DDECLUSAGE_BLENDINDICES / D3DDECLTYPE_UBYTE4)?
    // (D3DDECLUSAGE_NORMAL / D3DDECLTYPE_FLOAT3 or D3DDECLTYPE_FLOAT16_4)?
    // (D3DDECLUSAGE_COLOR / D3DDECLTYPE_D3DCOLOR)?
    // (D3DDECLUSAGE_TEXCOORD / D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT2 or D3DDECLTYPE_FLOAT16_2, D3DDECLTYPE_FLOAT3 or D3DDECLTYPE_FLOAT16_4, D3DDECLTYPE_FLOAT4 or D3DDECLTYPE_FLOAT16_4)*
    // (D3DDECLUSAGE_TANGENT / D3DDECLTYPE_FLOAT3 or D3DDECLTYPE_FLOAT16_4)?
    // (D3DDECLUSAGE_BINORMAL / D3DDECLTYPE_FLOAT3 or D3DDECLTYPE_FLOAT16_4)?

    enum D3DDECLUSAGE
    {
        D3DDECLUSAGE_POSITION = 0,
        D3DDECLUSAGE_BLENDWEIGHT = 1,
        D3DDECLUSAGE_BLENDINDICES = 2,
        D3DDECLUSAGE_NORMAL = 3,
        D3DDECLUSAGE_TEXCOORD = 5,
        D3DDECLUSAGE_TANGENT = 6,
        D3DDECLUSAGE_BINORMAL = 7,
        D3DDECLUSAGE_COLOR = 10,
    };

    enum D3DDECLTYPE
    {
        D3DDECLTYPE_FLOAT1 = 0,  // 1D float expanded to (value, 0., 0., 1.)
        D3DDECLTYPE_FLOAT2 = 1,  // 2D float expanded to (value, value, 0., 1.)
        D3DDECLTYPE_FLOAT3 = 2,  // 3D float expanded to (value, value, value, 1.)
        D3DDECLTYPE_FLOAT4 = 3,  // 4D float
        D3DDECLTYPE_D3DCOLOR = 4,  // 4D packed unsigned bytes mapped to 0. to 1. range
        // Input is in D3DCOLOR format (ARGB) expanded to (R, G, B, A)
        D3DDECLTYPE_UBYTE4 = 5,  // 4D unsigned byte
        D3DDECLTYPE_UBYTE4N = 8,  // Each of 4 bytes is normalized by dividing to 255.0
        D3DDECLTYPE_DEC3N = 14,  // 3D signed 10 10 10 format normalized and expanded to (v[0]/511.0, v[1]/511.0, v[2]/511.0, 1)
        D3DDECLTYPE_FLOAT16_2 = 15,  // Two 16-bit floating point values, expanded to (value, value, 0, 1)
        D3DDECLTYPE_FLOAT16_4 = 16,  // Four 16-bit floating point values

        D3DDECLTYPE_UNUSED = 17,  // When the type field in a decl is unused.
    };

#pragma pack(push,4)

    struct D3DVERTEXELEMENT9
    {
        WORD    Stream;     // Stream index
        WORD    Offset;     // Offset in the stream in bytes
        BYTE    Type;       // Data type
        BYTE    Method;     // Processing method
        BYTE    Usage;      // Semantics
        BYTE    UsageIndex; // Semantic index
    };

#pragma pack(pop)

    //--------------------------------------------------------------------------------------
    // Hard Defines for the various structures
    //--------------------------------------------------------------------------------------
    const uint32_t SDKMESH_FILE_VERSION = 101;
    const uint32_t MAX_VERTEX_ELEMENTS = 32;
    const uint32_t MAX_VERTEX_STREAMS = 16;
    const uint32_t MAX_FRAME_NAME = 100;
    const uint32_t MAX_MESH_NAME = 100;
    const uint32_t MAX_SUBSET_NAME = 100;
    const uint32_t MAX_MATERIAL_NAME = 100;
    const uint32_t MAX_TEXTURE_NAME = MAX_PATH;
    const uint32_t MAX_MATERIAL_PATH = MAX_PATH;
    const uint32_t INVALID_FRAME = uint32_t(-1);
    const uint32_t INVALID_MESH = uint32_t(-1);
    const uint32_t INVALID_MATERIAL = uint32_t(-1);
    const uint32_t INVALID_SUBSET = uint32_t(-1);
    const uint32_t INVALID_ANIMATION_DATA = uint32_t(-1);
    const uint32_t INVALID_SAMPLER_SLOT = uint32_t(-1);
    const uint32_t ERROR_RESOURCE_VALUE = 1;

    template<typename TYPE> bool IsErrorResource(TYPE data)
    {
        if ((TYPE) ERROR_RESOURCE_VALUE == data)
            return true;
        return false;
    }

    //--------------------------------------------------------------------------------------
    // Enumerated Types.  These will have mirrors in both D3D9 and D3D11
    //--------------------------------------------------------------------------------------
    enum SDKMESH_PRIMITIVE_TYPE
    {
        PT_TRIANGLE_LIST = 0,
        PT_TRIANGLE_STRIP,
        PT_LINE_LIST,
        PT_LINE_STRIP,
        PT_POINT_LIST,
        PT_TRIANGLE_LIST_ADJ,
        PT_TRIANGLE_STRIP_ADJ,
        PT_LINE_LIST_ADJ,
        PT_LINE_STRIP_ADJ,
        PT_QUAD_PATCH_LIST,
        PT_TRIANGLE_PATCH_LIST,
    };

    enum SDKMESH_INDEX_TYPE
    {
        IT_16BIT = 0,
        IT_32BIT,
    };

    enum FRAME_TRANSFORM_TYPE
    {
        FTT_RELATIVE = 0,
        FTT_ABSOLUTE,		//This is not currently used but is here to support absolute transformations in the future
    };

    //--------------------------------------------------------------------------------------
    // Structures.
    //--------------------------------------------------------------------------------------
#pragma pack(push,8)

    struct SDKMESH_HEADER
    {
        //Basic Info and sizes
        UINT Version;
        BYTE IsBigEndian;
        UINT64 HeaderSize;
        UINT64 NonBufferDataSize;
        UINT64 BufferDataSize;

        //Stats
        UINT NumVertexBuffers;
        UINT NumIndexBuffers;
        UINT NumMeshes;
        UINT NumTotalSubsets;
        UINT NumFrames;
        UINT NumMaterials;

        //Offsets to Data
        UINT64 VertexStreamHeadersOffset;
        UINT64 IndexStreamHeadersOffset;
        UINT64 MeshDataOffset;
        UINT64 SubsetDataOffset;
        UINT64 FrameDataOffset;
        UINT64 MaterialDataOffset;
    };

    struct SDKMESH_VERTEX_BUFFER_HEADER
    {
        UINT64 NumVertices;
        UINT64 SizeBytes;
        UINT64 StrideBytes;
        D3DVERTEXELEMENT9 Decl[MAX_VERTEX_ELEMENTS];
        union
        {
            UINT64 DataOffset;
            ID3D11Buffer* pVB11;
        };
    };

    struct SDKMESH_INDEX_BUFFER_HEADER
    {
        UINT64 NumIndices;
        UINT64 SizeBytes;
        UINT IndexType;
        union
        {
            UINT64 DataOffset;
            ID3D11Buffer* pIB11;
        };
    };

    struct SDKMESH_MESH
    {
        char Name[MAX_MESH_NAME];
        BYTE NumVertexBuffers;
        UINT VertexBuffers[MAX_VERTEX_STREAMS];
        UINT IndexBuffer;
        UINT NumSubsets;
        UINT NumFrameInfluences; //aka bones

        DirectX::XMFLOAT3 BoundingBoxCenter;
        DirectX::XMFLOAT3 BoundingBoxExtents;

        union
        {
            UINT64 SubsetOffset;
            INT* pSubsets;
        };
        union
        {
            UINT64 FrameInfluenceOffset;
            UINT* pFrameInfluences;
        };
    };

    struct SDKMESH_SUBSET
    {
        char Name[MAX_SUBSET_NAME];
        UINT MaterialID;
        UINT PrimitiveType;
        UINT64 IndexStart;
        UINT64 IndexCount;
        UINT64 VertexStart;
        UINT64 VertexCount;
    };

    struct SDKMESH_FRAME
    {
        char Name[MAX_FRAME_NAME];
        UINT Mesh;
        UINT ParentFrame;
        UINT ChildFrame;
        UINT SiblingFrame;
        DirectX::XMFLOAT4X4 Matrix;
        UINT AnimationDataIndex;		//Used to index which set of keyframes transforms this frame
    };

    struct SDKMESH_MATERIAL
    {
        char    Name[MAX_MATERIAL_NAME];

        // Use MaterialInstancePath
        char    MaterialInstancePath[MAX_MATERIAL_PATH];

        // Or fall back to d3d8-type materials
        char    DiffuseTexture[MAX_TEXTURE_NAME];
        char    NormalTexture[MAX_TEXTURE_NAME];
        char    SpecularTexture[MAX_TEXTURE_NAME];

        DirectX::XMFLOAT4 Diffuse;
        DirectX::XMFLOAT4 Ambient;
        DirectX::XMFLOAT4 Specular;
        DirectX::XMFLOAT4 Emissive;
        FLOAT Power;

        union
        {
            UINT64 Force64_1;			//Force the union to 64bits
            ID3D11Texture2D* pDiffuseTexture11;
        };
        union
        {
            UINT64 Force64_2;			//Force the union to 64bits
            ID3D11Texture2D* pNormalTexture11;
        };
        union
        {
            UINT64 Force64_3;			//Force the union to 64bits
            ID3D11Texture2D* pSpecularTexture11;
        };

        union
        {
            UINT64 Force64_4;			//Force the union to 64bits
            ID3D11ShaderResourceView* pDiffuseRV11;
        };
        union
        {
            UINT64 Force64_5;		    //Force the union to 64bits
            ID3D11ShaderResourceView* pNormalRV11;
        };
        union
        {
            UINT64 Force64_6;			//Force the union to 64bits
            ID3D11ShaderResourceView* pSpecularRV11;
        };
    };

    struct SDKANIMATION_FILE_HEADER
    {
        UINT Version;
        BYTE IsBigEndian;
        UINT FrameTransformType;
        UINT NumFrames;
        UINT NumAnimationKeys;
        UINT AnimationFPS;
        UINT64 AnimationDataSize;
        UINT64 AnimationDataOffset;
    };

    struct SDKANIMATION_DATA
    {
        DirectX::XMFLOAT3 Translation;
        DirectX::XMFLOAT4 Orientation;
        DirectX::XMFLOAT3 Scaling;
    };

    struct SDKANIMATION_FRAME_DATA
    {
        char FrameName[MAX_FRAME_NAME];
        union
        {
            UINT64 DataOffset;
            SDKANIMATION_DATA* pAnimationData;
        };
    };

#pragma pack(pop)

}; // namespace

static_assert(sizeof(DXUT::D3DVERTEXELEMENT9) == 8, "Direct3D9 Decl structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_HEADER) == 104, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_VERTEX_BUFFER_HEADER) == 288, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_INDEX_BUFFER_HEADER) == 32, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_MESH) == 224, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_SUBSET) == 144, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_FRAME) == 184, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKMESH_MATERIAL) == 1256, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKANIMATION_FILE_HEADER) == 40, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKANIMATION_DATA) == 40, "SDK Mesh structure size incorrect");
static_assert(sizeof(DXUT::SDKANIMATION_FRAME_DATA) == 112, "SDK Mesh structure size incorrect");


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::ExportToSDKMESH(const wchar_t* szFileName, size_t nMaterials, const Material* materials) const
{
    using namespace DXUT;

    if (!szFileName)
        return E_INVALIDARG;

    if (nMaterials > 0 && !materials)
        return E_INVALIDARG;

    if (!mnFaces || !mIndices || !mnVerts || !mPositions)
        return E_UNEXPECTED;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    // Build input layout/vertex decalaration
    static const D3D11_INPUT_ELEMENT_DESC s_elements [] =
    {
        { "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 0
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 1
        { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 2
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 3
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 4
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 5
        { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 6
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // 7
    };

    static const D3DVERTEXELEMENT9 s_decls [] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, // 0
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0 }, // 1
        { 0, 0, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0 }, // 2
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_TANGENT, 0 }, // 3
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_BINORMAL, 0 }, // 4
        { 0, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 }, // 5
        { 0, 0, D3DDECLTYPE_UBYTE4, 0, D3DDECLUSAGE_BLENDINDICES, 0 }, // 6
        { 0, 0, D3DDECLTYPE_UBYTE4N, 0, D3DDECLUSAGE_BLENDWEIGHT, 0 }, // 7
        { 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 },
    };

    static_assert((_countof(s_elements) + 1) == _countof(s_decls), "InputLayouts and Vertex Decls disagree");

    SDKMESH_VERTEX_BUFFER_HEADER vbHeader = { 0 };
    vbHeader.NumVertices = mnVerts;
    vbHeader.Decl[0] = s_decls[0];

    D3D11_INPUT_ELEMENT_DESC inputLayout[MAX_VERTEX_ELEMENTS] = { 0 };
    inputLayout[0] = s_elements[0];

    size_t nDecl = 1;
    size_t stride = sizeof(XMFLOAT3);

    if (mBlendIndices && mBlendWeights)
    {
        // BLENDWEIGHT
        vbHeader.Decl[nDecl] = s_decls[7];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[7];
        ++nDecl;
        stride += sizeof(UINT);

        // BLENDINDICES
        vbHeader.Decl[nDecl] = s_decls[6];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[6];
        ++nDecl;
        stride += sizeof(UINT);
    }

    if (mNormals)
    {
        vbHeader.Decl[nDecl] = s_decls[1];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[1];
        ++nDecl;
        stride += sizeof(XMFLOAT3);
    }

    if (mColors)
    {
        vbHeader.Decl[nDecl] = s_decls[2];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[2];
        ++nDecl;
        stride += sizeof(UINT);
    }

    if (mTexCoords)
    {
        vbHeader.Decl[nDecl] = s_decls[5];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[5];
        ++nDecl;
        stride += sizeof(XMFLOAT2);
    }

    if (mTangents)
    {
        vbHeader.Decl[nDecl] = s_decls[3];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[3];
        ++nDecl;
        stride += sizeof(XMFLOAT3);
    }

    if (mBiTangents)
    {
        vbHeader.Decl[nDecl] = s_decls[4];
        vbHeader.Decl[nDecl].Offset = static_cast<WORD>(stride);
        inputLayout[nDecl] = s_elements[4];
        ++nDecl;
        stride += sizeof(XMFLOAT3);
    }

    assert(nDecl < MAX_VERTEX_ELEMENTS);
    vbHeader.Decl[nDecl] = s_decls[_countof(s_decls) - 1];

    // Build vertex buffer
    std::unique_ptr<uint8_t> vb(new (std::nothrow) uint8_t[mnVerts * stride]);
    if (!vb)
        return E_OUTOFMEMORY;

    vbHeader.SizeBytes = mnVerts * stride;
    vbHeader.StrideBytes = stride;

    {
        VBWriter writer;

        HRESULT hr = writer.Initialize(inputLayout, nDecl);
        if (FAILED(hr))
            return hr;

        hr = writer.AddStream(vb.get(), mnVerts, 0, stride);
        if (FAILED(hr))
            return hr;

        hr = GetVertexBuffer(writer);
        if (FAILED(hr))
            return hr;
    }

    // Build index buffer
    SDKMESH_INDEX_BUFFER_HEADER ibHeader = { 0 };
    ibHeader.NumIndices = mnFaces * 3;

    std::unique_ptr<uint16_t[]> ib16;
    if (Is16BitIndexBuffer())
    {
        ibHeader.SizeBytes = mnFaces * 3 * sizeof(uint16_t);
        ibHeader.IndexType = IT_16BIT;

        ib16 = GetIndexBuffer16();
        if (!ib16)
            return E_OUTOFMEMORY;
    }
    else
    {
        ibHeader.SizeBytes = mnFaces * 3 * sizeof(uint32_t);
        ibHeader.IndexType = IT_32BIT;
    }

    // Build materials buffer
    std::unique_ptr<SDKMESH_MATERIAL[]> mats;
    if (!nMaterials)
    {
        mats.reset(new (std::nothrow) SDKMESH_MATERIAL[1]);
        if (!mats)
            return E_OUTOFMEMORY;

        memset(mats.get(), 0, sizeof(SDKMESH_MATERIAL));

        strcpy_s(mats[0].Name, "default");
        mats[0].Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.f);
        mats[0].Ambient = XMFLOAT4(0.2f, 02.f, 0.2f, 1.f);
        mats[0].Power = 1.f;
    }
    else
    {
        mats.reset(new (std::nothrow) SDKMESH_MATERIAL[nMaterials]);
        if (!mats)
            return E_OUTOFMEMORY;

        for (size_t j = 0; j < nMaterials; ++j)
        {
            auto m0 = &materials[j];
            auto m = &mats[j];

            memset( m, 0, sizeof(SDKMESH_MATERIAL) );

            int result = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                             m0->name.c_str(), -1,
                                             m->Name, MAX_MATERIAL_NAME, nullptr, FALSE);
            if (!result)
            {
                *m->Name = 0;
            }

            result = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                         m0->texture.c_str(), -1,
                                         m->DiffuseTexture, MAX_TEXTURE_NAME, nullptr, FALSE);
            if (!result)
            {
                *m->DiffuseTexture = 0;
            }

            m->Diffuse.x = m0->diffuseColor.x;
            m->Diffuse.y = m0->diffuseColor.y;
            m->Diffuse.z = m0->diffuseColor.z;
            m->Diffuse.w = m0->alpha;

            m->Ambient.x = m0->ambientColor.x;
            m->Ambient.y = m0->ambientColor.y;
            m->Ambient.z = m0->ambientColor.z;
            m->Ambient.w = 1.f;

            if (m0->specularColor.x > 0.f || m0->specularColor.y > 0.f || m0->specularColor.z > 0.f)
            {
                m->Specular.x = m0->specularColor.x;
                m->Specular.y = m0->specularColor.y;
                m->Specular.z = m0->specularColor.z;
                m->Power = ( m0->specularPower <= 0.f ) ? 16.f : m0->specularPower;
            }
            else
            {
                m->Power = 1.f;
            }

            m->Emissive.x = m0->emissiveColor.x;
            m->Emissive.y = m0->emissiveColor.y;
            m->Emissive.z = m0->emissiveColor.z;
        }
    }

    // Build subsets
    std::vector<SDKMESH_SUBSET> submeshes;
    std::vector<UINT> subsetArray;
    if (mAttributes)
    {
        auto subsets = ComputeSubsets(mAttributes.get(), mnFaces);

        UINT64 startIndex = 0;
        for (auto it = subsets.cbegin(); it != subsets.cend(); ++it)
        {
            subsetArray.push_back(static_cast<UINT>(submeshes.size()));

            SDKMESH_SUBSET s = { 0 };
            s.MaterialID = mAttributes[it->first];
            if (s.MaterialID >= nMaterials)
                s.MaterialID = 0;

            s.PrimitiveType = PT_TRIANGLE_LIST;
            s.IndexStart = startIndex;
            s.IndexCount = it->second * 3;
            s.VertexCount = mnVerts;
            submeshes.push_back(s);

            if ((startIndex + s.IndexCount) > mnFaces * 3)
                return E_FAIL;

            startIndex += s.IndexCount;
        }
    }
    else
    {
        SDKMESH_SUBSET s = { 0 };
        s.PrimitiveType = PT_TRIANGLE_LIST;
        s.IndexCount = mnFaces * 3;
        s.VertexCount = mnVerts;
        subsetArray.push_back(0);
        submeshes.push_back(s);
    }

    // Create file
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
    ScopedHandle hFile(safe_handle(CreateFile2(szFileName, GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr)));
#else
    ScopedHandle hFile(safe_handle(CreateFileW(szFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)));
#endif
    if (!hFile)
        return HRESULT_FROM_WIN32(GetLastError());

    // Write file header
    SDKMESH_HEADER header = { 0 };
    header.Version = SDKMESH_FILE_VERSION;
    header.IsBigEndian = 0;

    header.NumVertexBuffers = 1;
    header.NumIndexBuffers = 1;
    header.NumMeshes = 1;
    header.NumTotalSubsets = static_cast<UINT>( submeshes.size() );
    header.NumFrames = 1;
    header.NumMaterials = (nMaterials > 0) ? static_cast<UINT>(nMaterials) : 1;

    header.HeaderSize = sizeof(SDKMESH_HEADER) + sizeof(SDKMESH_VERTEX_BUFFER_HEADER) + sizeof(SDKMESH_INDEX_BUFFER_HEADER);

    size_t staticDataSize = sizeof(SDKMESH_MESH)
                            + header.NumTotalSubsets * sizeof(SDKMESH_SUBSET)
                            + sizeof(SDKMESH_FRAME)
                            + header.NumMaterials * sizeof(SDKMESH_MATERIAL);

    header.NonBufferDataSize = staticDataSize + subsetArray.size() * sizeof(UINT) + sizeof(UINT);

    header.BufferDataSize = roundup4k( vbHeader.SizeBytes ) + roundup4k( ibHeader.SizeBytes );

    header.VertexStreamHeadersOffset = sizeof(SDKMESH_HEADER);
    header.IndexStreamHeadersOffset = header.VertexStreamHeadersOffset + sizeof(SDKMESH_VERTEX_BUFFER_HEADER); 
    header.MeshDataOffset = header.IndexStreamHeadersOffset + sizeof(SDKMESH_INDEX_BUFFER_HEADER);
    header.SubsetDataOffset = header.MeshDataOffset + sizeof(SDKMESH_MESH);
    header.FrameDataOffset = header.SubsetDataOffset + header.NumTotalSubsets * sizeof(SDKMESH_SUBSET);
    header.MaterialDataOffset = header.FrameDataOffset + sizeof(SDKMESH_FRAME);

    HRESULT hr = write_file(hFile.get(), header);
    if (FAILED(hr))
        return hr;

    // Write buffer headers
    UINT64 offset = header.HeaderSize + header.NonBufferDataSize;

    vbHeader.DataOffset = offset;
    offset += roundup4k(vbHeader.SizeBytes);

    hr = write_file(hFile.get(), vbHeader);
    if (FAILED(hr))
        return hr;

    ibHeader.DataOffset = offset;
    offset += roundup4k(ibHeader.SizeBytes);

    hr = write_file(hFile.get(), ibHeader);
    if (FAILED(hr))
        return hr;

    // Write mesh headers
    assert(header.NumMeshes == 1);
    offset = header.HeaderSize + staticDataSize;

    SDKMESH_MESH meshHeader = { 0 };
    meshHeader.NumVertexBuffers = 1;
    meshHeader.NumFrameInfluences = 1;

    {
        BoundingBox box;
        BoundingBox::CreateFromPoints(box, mnVerts, mPositions.get(), sizeof(XMFLOAT3));

        meshHeader.BoundingBoxCenter = box.Center;
        meshHeader.BoundingBoxExtents = box.Extents;
    }

    meshHeader.NumSubsets = static_cast<UINT>(submeshes.size());
    meshHeader.SubsetOffset = offset;
    offset += meshHeader.NumSubsets * sizeof(UINT);
    meshHeader.FrameInfluenceOffset = offset;
    offset += sizeof(UINT);

    hr = write_file(hFile.get(), meshHeader);
    if (FAILED(hr))
        return hr;

    // Write subsets
    DWORD bytesToWrite = static_cast<DWORD>( sizeof(SDKMESH_SUBSET) * submeshes.size() );
    DWORD bytesWritten;
    if ( !WriteFile(hFile.get(), submeshes.data(), bytesToWrite, &bytesWritten, nullptr) )
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != bytesToWrite)
        return E_FAIL;

    // Write frames
    SDKMESH_FRAME frame = { 0 };
    strcpy_s( frame.Name, "root");
    frame.ParentFrame = frame.ChildFrame = frame.SiblingFrame = DWORD(-1);
    frame.AnimationDataIndex = INVALID_ANIMATION_DATA;
    XMMATRIX id = XMMatrixIdentity();
    XMStoreFloat4x4(&frame.Matrix, id);

    hr = write_file(hFile.get(), frame);
    if (FAILED(hr))
        return hr;

    // Write materials
    bytesToWrite = static_cast<DWORD>(sizeof(SDKMESH_MATERIAL) * ((nMaterials > 0) ? nMaterials : 1));
    if (!WriteFile(hFile.get(), mats.get(), bytesToWrite, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != bytesToWrite)
        return E_FAIL;

    // Write subset index list
    assert(meshHeader.NumSubsets == subsetArray.size());
    bytesToWrite = meshHeader.NumSubsets * sizeof(UINT);
    if (!WriteFile(hFile.get(), subsetArray.data(), bytesToWrite, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != bytesToWrite)
        return E_FAIL;

    // Write frame influence list
    assert(meshHeader.NumFrameInfluences == 1);
    UINT frameIndex = 0;
    hr = write_file(hFile.get(), frameIndex);
    if (FAILED(hr))
        return hr;

    // Write VB data
    bytesToWrite = static_cast<DWORD>(vbHeader.SizeBytes);
    if (!WriteFile(hFile.get(), vb.get(), bytesToWrite, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != bytesToWrite)
        return E_FAIL;

    bytesToWrite = static_cast<DWORD>( roundup4k(vbHeader.SizeBytes) - vbHeader.SizeBytes );
    if (bytesToWrite > 0)
    {
        assert(bytesToWrite < sizeof(g_padding));
        if (!WriteFile(hFile.get(), g_padding, bytesToWrite, &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        if (bytesWritten != bytesToWrite)
            return E_FAIL;
    }

    // Write IB data
    bytesToWrite = static_cast<DWORD>(ibHeader.SizeBytes);
    if (!WriteFile(hFile.get(), (ib16) ? static_cast<void*>( ib16.get() ) : static_cast<void*>( mIndices.get() ),
                   bytesToWrite, &bytesWritten, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());

    if (bytesWritten != bytesToWrite)
        return E_FAIL;

    bytesToWrite = static_cast<DWORD>(roundup4k(ibHeader.SizeBytes) - ibHeader.SizeBytes);
    if (bytesToWrite > 0)
    {
        assert(bytesToWrite < sizeof(g_padding));
        if (!WriteFile(hFile.get(), g_padding, bytesToWrite, &bytesWritten, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());

        if (bytesWritten != bytesToWrite)
            return E_FAIL;
    }

    return S_OK;
}
