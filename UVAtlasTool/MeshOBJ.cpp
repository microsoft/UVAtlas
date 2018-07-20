//--------------------------------------------------------------------------------------
// File: MeshOBJ.cpp
//
// Helper code for loading Mesh data from Wavefront OBJ
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=324981
// http://go.microsoft.com/fwlink/?LinkID=512686
//--------------------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)

#include "Mesh.h"
#include "WaveFrontReader.h"

using namespace DirectX;

HRESULT LoadFromOBJ(
    const wchar_t* szFilename,
    std::unique_ptr<Mesh>& inMesh,
    std::vector<Mesh::Material>& inMaterial,
    bool ccw,
    bool dds)
{
    WaveFrontReader<uint32_t> wfReader;
    HRESULT hr = wfReader.Load(szFilename, ccw);
    if (FAILED(hr))
        return hr;

    inMesh.reset(new (std::nothrow) Mesh);
    if (!inMesh)
        return E_OUTOFMEMORY;

    if (wfReader.indices.empty() || wfReader.vertices.empty())
        return E_FAIL;

    hr = inMesh->SetIndexData(wfReader.indices.size() / 3, wfReader.indices.data(),
        wfReader.attributes.empty() ? nullptr : wfReader.attributes.data());
    if (FAILED(hr))
        return hr;

    static const D3D11_INPUT_ELEMENT_DESC s_vboLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    static const D3D11_INPUT_ELEMENT_DESC s_vboLayoutAlt[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    const D3D11_INPUT_ELEMENT_DESC* layout = s_vboLayout;
    size_t nDecl = _countof(s_vboLayout);

    if (!wfReader.hasNormals && !wfReader.hasTexcoords)
    {
        nDecl = 1;
    }
    else if (wfReader.hasNormals && !wfReader.hasTexcoords)
    {
        nDecl = 2;
    }
    else if (!wfReader.hasNormals && wfReader.hasTexcoords)
    {
        layout = s_vboLayoutAlt;
        nDecl = _countof(s_vboLayoutAlt);
    }

    VBReader vbr;
    hr = vbr.Initialize(layout, nDecl);
    if (FAILED(hr))
        return hr;

    hr = vbr.AddStream(wfReader.vertices.data(), wfReader.vertices.size(), 0, sizeof(WaveFrontReader<uint32_t>::Vertex));
    if (FAILED(hr))
        return hr;

    hr = inMesh->SetVertexData(vbr, wfReader.vertices.size());
    if (FAILED(hr))
        return hr;

    if (!wfReader.materials.empty())
    {
        inMaterial.clear();
        inMaterial.reserve(wfReader.materials.size());

        for (auto it = wfReader.materials.cbegin(); it != wfReader.materials.cend(); ++it)
        {
            Mesh::Material mtl = {};

            mtl.name = it->strName;
            mtl.specularPower = (it->bSpecular) ? float(it->nShininess) : 1.f;
            mtl.alpha = it->fAlpha;
            mtl.ambientColor = it->vAmbient;
            mtl.diffuseColor = it->vDiffuse;
            mtl.specularColor = (it->bSpecular) ? it->vSpecular : XMFLOAT3(0.f, 0.f, 0.f);
            mtl.emissiveColor = XMFLOAT3(0.f, 0.f, 0.f);

            wchar_t texture[_MAX_PATH] = {};
            if (*it->strTexture)
            {
                wchar_t txext[_MAX_EXT];
                wchar_t txfname[_MAX_FNAME];
                _wsplitpath_s(it->strTexture, nullptr, 0, nullptr, 0, txfname, _MAX_FNAME, txext, _MAX_EXT);

                if (dds)
                {
                    wcscpy_s(txext, L".dds");
                }

                _wmakepath_s(texture, nullptr, nullptr, txfname, txext);
            }

            mtl.texture = texture;

            inMaterial.push_back(mtl);
        }
    }

    return S_OK;
}
