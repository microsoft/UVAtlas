//--------------------------------------------------------------------------------------
// File: UVAtlas.cpp
//
// UVAtlas command-line tool (sample for UVAtlas library)
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//--------------------------------------------------------------------------------------

#define NOMINMAX

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <memory>
#include <list>

#include <dxgiformat.h>

#include "uvatlas.h"
#include "directxtex.h"

#include "Mesh.h"
#include "WaveFrontReader.h"

using namespace DirectX;

enum OPTIONS    // Note: dwOptions below assumes 32 or less options.
{
    OPT_QUALITY = 1,
    OPT_MAXCHARTS,
    OPT_MAXSTRETCH,
    OPT_GUTTER,
    OPT_WIDTH,
    OPT_HEIGHT,
    OPT_TOPOLOGICAL_ADJ,
    OPT_GEOMETRIC_ADJ,
    OPT_NORMALS,
    OPT_WEIGHT_BY_AREA,
    OPT_WEIGHT_BY_EQUAL,
    OPT_TANGENTS,
    OPT_CTF,
    OPT_COLOR_MESH,
    OPT_UV_MESH,
    OPT_IMT_TEXFILE,
    OPT_IMT_VERTEX,
    OPT_SDKMESH,
    OPT_CMO,
    OPT_VBO,
    OPT_OUTPUTFILE,
    OPT_CLOCKWISE,
    OPT_OVERWRITE,
    OPT_NODDS,
    OPT_FLIP,
    OPT_FLIPTC,
    OPT_NOLOGO,
    OPT_MAX
};

static_assert( OPT_MAX <= 32, "dwOptions is a DWORD bitfield" );

enum CHANNELS
{
    CHANNEL_NONE = 0,
    CHANNEL_NORMAL,
    CHANNEL_COLOR,
    CHANNEL_TEXCOORD,
};

struct SConversion
{
    WCHAR szSrc [MAX_PATH];
};

struct SValue
{
    LPCWSTR pName;
    DWORD dwValue;
};

static const XMFLOAT3 g_ColorList[8] =
{
    XMFLOAT3( 1.0f, 0.5f, 0.5f ),
    XMFLOAT3( 0.5f, 1.0f, 0.5f ),
    XMFLOAT3( 1.0f, 1.0f, 0.5f ),
    XMFLOAT3( 0.5f, 1.0f, 1.0f ),
    XMFLOAT3( 1.0f, 0.5f, 0.75f ),
    XMFLOAT3( 0.0f, 0.5f, 0.75f ),
    XMFLOAT3( 0.5f, 0.5f, 0.75f ),
    XMFLOAT3( 0.5f, 0.5f, 1.0f ),
};

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

SValue g_pOptions[] = 
{
    { L"q",             OPT_QUALITY     },
    { L"n",             OPT_MAXCHARTS   },
    { L"st",            OPT_MAXSTRETCH  },
    { L"g",             OPT_GUTTER      },
    { L"w",             OPT_WIDTH       },
    { L"h",             OPT_HEIGHT      },
    { L"ta",            OPT_TOPOLOGICAL_ADJ },
    { L"ga",            OPT_GEOMETRIC_ADJ },
    { L"nn",            OPT_NORMALS     },
    { L"na",            OPT_WEIGHT_BY_AREA },
    { L"ne",            OPT_WEIGHT_BY_EQUAL },
    { L"tt",            OPT_TANGENTS    },
    { L"tb",            OPT_CTF         },
    { L"c",             OPT_COLOR_MESH  },
    { L"t",             OPT_UV_MESH     },
    { L"it",            OPT_IMT_TEXFILE },
    { L"iv",            OPT_IMT_VERTEX  },
    { L"o",             OPT_OUTPUTFILE  },
    { L"sdkmesh",       OPT_SDKMESH     },
    { L"cmo",           OPT_CMO         },
    { L"vbo",           OPT_VBO         },
    { L"cw",            OPT_CLOCKWISE   },
    { L"y",             OPT_OVERWRITE   },
    { L"nodds",         OPT_NODDS       },
    { L"flip",          OPT_FLIP        },
    { L"fliptc",        OPT_FLIPTC      },
    { L"nologo",        OPT_NOLOGO      },
    { nullptr,          0               }
};

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

#pragma prefast(disable : 26018, "Only used with static internal arrays")

DWORD LookupByName(const WCHAR *pName, const SValue *pArray)
{
    while(pArray->pName)
    {
        if(!_wcsicmp(pName, pArray->pName))
            return pArray->dwValue;

        pArray++;
    }

    return 0;
}

const WCHAR* LookupByValue(DWORD pValue, const SValue *pArray)
{
    while(pArray->pName)
    {
        if(pValue == pArray->dwValue)
            return pArray->pName;

        pArray++;
    }

    return L"";
}

void PrintLogo()
{
    wprintf( L"Microsoft (R) UVAtlas Command-line Tool\n");
    wprintf( L"Copyright (C) Microsoft Corp. All rights reserved.\n");
    wprintf( L"\n");
}


void PrintUsage()
{
    PrintLogo();

    wprintf( L"Usage: uvatlas <options> <files>\n");
    wprintf( L"\n");
    wprintf( L"   -q <level>          sets quality level to DEFAULT, FAST or QUALITY\n");
    wprintf( L"   -n <number>         maximum number of charts to generate (def: 0)\n");
    wprintf( L"   -st <float>         maximum amount of stretch 0.0 to 1.0 (def: 0.16667)\n");
    wprintf( L"   -g <float>          the gutter width betwen charts in texels (def: 2.0)\n");
    wprintf( L"   -w <number>         texture width (def: 512)\n");
    wprintf( L"   -h <number>         texture height (def: 512)\n");
    wprintf( L"   -ta | -ga           generate topological vs. geometric adjancecy (def: ta)\n");
    wprintf( L"   -nn | -na | -ne     generate normals weighted by angle/area/equal\n" );
    wprintf( L"   -tt                 generate tangents\n");
    wprintf( L"   -tb                 generate tangents & bi-tangents\n");
    wprintf( L"   -cw                 faces are clockwise (defaults to counter-clockwise)\n");
    wprintf( L"   -c                  generate mesh with colors showing charts\n");
    wprintf( L"   -t                  generates a separate mesh with uvs - (*_texture)\n");
    wprintf( L"   -it <filename>      calculate IMT for the mesh using this texture map\n");
    wprintf( L"   -iv <channel>       calculate IMT using per-vertex data\n");
    wprintf( L"                       NORMAL, COLOR, TEXCOORD\n");
    wprintf( L"   -sdkmesh|-cmo|-vbo  output file type\n");
    wprintf( L"   -nodds              prevents extension renaming in exported materials\n");
    wprintf( L"   -flip | -fliptc     reverse winding of faces and/or flips texcoords\n");
    wprintf( L"   -o <filename>       output filename\n");
    wprintf( L"   -y                  overwrite existing output file (if any)\n");
    wprintf( L"   -nologo             suppress copyright message\n");

    wprintf( L"\n");
}


//--------------------------------------------------------------------------------------
HRESULT __cdecl UVAtlasCallback( float fPercentDone  )
{
    static ULONGLONG s_lastTick = 0;

    ULONGLONG tick = GetTickCount64();

    if ( ( tick - s_lastTick ) > 1000 )
    {
        wprintf( L"%.2f%%   \r", fPercentDone * 100 );
        s_lastTick = tick;
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT LoadFromOBJ(const WCHAR* szFilename, std::unique_ptr<Mesh>& inMesh, std::vector<Mesh::Material>& inMaterial, DWORD options )
{
    WaveFrontReader<uint32_t> wfReader;
    HRESULT hr = wfReader.Load(szFilename, (options & (1 << OPT_CLOCKWISE)) ? false : true );
    if (FAILED(hr))
        return hr;

    inMesh.reset(new (std::nothrow) Mesh);
    if (!inMesh)
        return E_OUTOFMEMORY;

    if (wfReader.indices.empty() || wfReader.vertices.empty())
        return E_FAIL;

    hr = inMesh->SetIndexData(wfReader.indices.size() / 3, &wfReader.indices.front(),
                              wfReader.attributes.empty() ? nullptr : &wfReader.attributes.front());
    if (FAILED(hr))
        return hr;

    static const D3D11_INPUT_ELEMENT_DESC s_vboLayout [] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    static const D3D11_INPUT_ELEMENT_DESC s_vboLayoutAlt [] =
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

    hr = vbr.AddStream(&wfReader.vertices.front(), wfReader.vertices.size(), 0, sizeof(WaveFrontReader<uint32_t>::Vertex));
    if (FAILED(hr))
        return hr;

    hr = inMesh->SetVertexData(vbr, wfReader.vertices.size());
    if (FAILED(hr))
        return hr;

    if ( !wfReader.materials.empty() )
    {
        inMaterial.clear();
        inMaterial.reserve(wfReader.materials.size());

        for (auto it = wfReader.materials.cbegin(); it != wfReader.materials.cend(); ++it)
        {
            Mesh::Material mtl;
            memset(&mtl, 0, sizeof(mtl));

            mtl.name = it->strName;
            mtl.specularPower = (it->bSpecular) ? float(it->nShininess) : 1.f;
            mtl.alpha = it->fAlpha;
            mtl.ambientColor = it->vAmbient;
            mtl.diffuseColor = it->vDiffuse;
            mtl.specularColor = (it->bSpecular) ? it->vSpecular : XMFLOAT3(0.f, 0.f, 0.f);

            WCHAR texture[_MAX_PATH] = { 0 };
            if (*it->strTexture)
            {
                WCHAR txext[_MAX_EXT];
                WCHAR txfname[_MAX_FNAME];
                _wsplitpath_s(it->strTexture, nullptr, 0, nullptr, 0, txfname, _MAX_FNAME, txext, _MAX_EXT);

                if (!(options & (1 << OPT_NODDS)))
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


//--------------------------------------------------------------------------------------
// Entry-point
//--------------------------------------------------------------------------------------
#pragma prefast(disable : 28198, "Command-line tool, frees all memory on exit")

int __cdecl wmain(_In_ int argc, _In_z_count_(argc) wchar_t* argv[])
{
    // Parameters and defaults
    size_t maxCharts = 0;
    float maxStretch = 0.16667f;
    float gutter = 2.f;
    size_t width = 512;
    size_t height = 512; 
    CHANNELS perVertex = CHANNEL_NONE;
    DWORD uvOptions = UVATLAS_DEFAULT;

    WCHAR szTexFile[MAX_PATH] = { 0 };
    WCHAR szOutputFile[MAX_PATH] = { 0 };

    // Initialize COM (needed for WIC)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if( FAILED(hr) )
    {
        wprintf( L"Failed to initialize COM (%08X)\n", hr);
        return 1;
    }

    // Process command line
    DWORD dwOptions = 0;
    std::list<SConversion> conversion;

    for(int iArg = 1; iArg < argc; iArg++)
    {
        PWSTR pArg = argv[iArg];

        if(('-' == pArg[0]) || ('/' == pArg[0]))
        {
            pArg++;
            PWSTR pValue;

            for(pValue = pArg; *pValue && (':' != *pValue); pValue++);

            if(*pValue)
                *pValue++ = 0;

            DWORD dwOption = LookupByName(pArg, g_pOptions);

            if(!dwOption || (dwOptions & (1 << dwOption)))
            {
                wprintf( L"ERROR: unknown command-line option '%ls'\n\n", pArg);
                PrintUsage();
                return 1;
            }

            dwOptions |= (1 << dwOption);

            if ( OPT_NOLOGO != dwOption && OPT_OVERWRITE != dwOption
                 && OPT_CLOCKWISE != dwOption && OPT_NODDS != dwOption
                 && OPT_FLIP != dwOption && OPT_FLIPTC != dwOption
                 && OPT_NORMALS != dwOption && OPT_WEIGHT_BY_AREA != dwOption && OPT_WEIGHT_BY_EQUAL != dwOption
                 && OPT_TANGENTS != dwOption && OPT_CTF != dwOption
                 && OPT_TOPOLOGICAL_ADJ != dwOption && OPT_GEOMETRIC_ADJ != dwOption
                 && OPT_COLOR_MESH != dwOption && OPT_UV_MESH != dwOption
                 && OPT_SDKMESH != dwOption && OPT_CMO != dwOption && OPT_VBO != dwOption )
            {
                if(!*pValue)
                {
                    if((iArg + 1 >= argc))
                    {
                        wprintf( L"ERROR: missing value for command-line option '%ls'\n\n", pArg);
                        PrintUsage();
                        return 1;
                    }

                    iArg++;
                    pValue = argv[iArg];
                }
            }

            switch(dwOption)
            {
            case OPT_QUALITY:
                if ( !_wcsicmp( pValue, L"DEFAULT" ) )
                {
                    uvOptions = UVATLAS_DEFAULT;
                }
                else if ( !_wcsicmp( pValue, L"FAST" ) )
                {
                    uvOptions = UVATLAS_GEODESIC_FAST;
                }
                else if ( !_wcsicmp( pValue, L"QUALITY" ) )
                {
                    uvOptions = UVATLAS_GEODESIC_QUALITY;
                }
                else
                {
                    wprintf( L"Invalid value specified with -q (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_MAXCHARTS:
                if (swscanf_s(pValue, L"%Iu", &maxCharts) != 1)
                {
                    wprintf( L"Invalid value specified with -n (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_MAXSTRETCH:
                if (swscanf_s(pValue, L"%f", &maxStretch) != 1
                    || maxStretch < 0.f
                    || maxStretch > 1.f )
                {
                    wprintf( L"Invalid value specified with -st (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_GUTTER:
                if (swscanf_s(pValue, L"%f", &gutter) != 1
                    || gutter < 0.f )
                {
                    wprintf( L"Invalid value specified with -g (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_WIDTH:
                if (swscanf_s(pValue, L"%Iu", &width) != 1)
                {
                    wprintf( L"Invalid value specified with -w (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_HEIGHT:
                if (swscanf_s(pValue, L"%Iu", &height) != 1)
                {
                    wprintf( L"Invalid value specified with -h (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_WEIGHT_BY_AREA:
                if (dwOptions & (1 << OPT_WEIGHT_BY_EQUAL))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (1 << OPT_NORMALS);
                break;

            case OPT_WEIGHT_BY_EQUAL:
                if (dwOptions & (1 << OPT_WEIGHT_BY_AREA))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (1 << OPT_NORMALS);
                break;

            case OPT_IMT_TEXFILE:
                if ( dwOptions & (1 << OPT_IMT_VERTEX) )
                {
                    wprintf( L"Cannot use both if and iv at the same time\n" );
                    return 1;
                }

                wcscpy_s(szTexFile, MAX_PATH, pValue);
                break;

            case OPT_IMT_VERTEX:
                if ( dwOptions & (1 << OPT_IMT_TEXFILE) )
                {
                    wprintf( L"Cannot use both if and iv at the same time\n" );
                    return 1;
                }

                if ( !_wcsicmp( pValue, L"COLOR" ) )
                {
                    perVertex = CHANNEL_COLOR;
                }
                else if ( !_wcsicmp( pValue, L"NORMAL" ) )
                {
                    perVertex = CHANNEL_NORMAL;
                }
                else if ( !_wcsicmp( pValue, L"TEXCOORD" ) )
                {
                    perVertex = CHANNEL_TEXCOORD;
                }
                else
                {
                    wprintf( L"Invalid value specified with -iv (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_OUTPUTFILE:
                wcscpy_s(szOutputFile, MAX_PATH, pValue);
                break;

            case OPT_TOPOLOGICAL_ADJ:
                if (dwOptions & (1 << OPT_GEOMETRIC_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_GEOMETRIC_ADJ:
                if (dwOptions & (1 << OPT_TOPOLOGICAL_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_SDKMESH:
                if ( dwOptions & ( (1 << OPT_VBO) | (1 << OPT_CMO) ) )
                {
                    wprintf( L"Can only use one of sdkmesh, cmo, or vbo\n" );
                    return 1;
                }
                break;

            case OPT_CMO:
                if ( dwOptions & ( (1 << OPT_VBO) | (1 << OPT_SDKMESH) ) )
                {
                    wprintf( L"Can only use one of sdkmesh, cmo, or vbo\n" );
                    return 1;
                }
                break;

            case OPT_VBO:
                if ( dwOptions & ( (1 << OPT_SDKMESH) | (1 << OPT_CMO) ) )
                {
                    wprintf( L"Can only use one of sdkmesh, cmo, or vbo\n" );
                    return 1;
                }
                break;

            case OPT_FLIP:
                if ( dwOptions & (1 << OPT_FLIPTC) )
                {
                    wprintf(L"Can only use flip or fliptc\n");
                    return 1;
                }
                break;

            case OPT_FLIPTC:
                if (dwOptions & (1 << OPT_FLIP))
                {
                    wprintf(L"Can only use flip or fliptc\n");
                    return 1;
                }
                break;
            }
        }
        else
        {
            SConversion conv;
            wcscpy_s(conv.szSrc, MAX_PATH, pArg);

            conversion.push_back(conv);
        }
    }

    if(conversion.empty())
    {
        PrintUsage();
        return 0;
    }

    if ( *szOutputFile && conversion.size() > 1 )
    {
        wprintf( L"Cannot use -o with multiple input files\n");
        return 1;
    }

    if(~dwOptions & (1 << OPT_NOLOGO))
        PrintLogo();

    // Process files
    for( auto pConv = conversion.begin(); pConv != conversion.end(); ++pConv )
    {
        WCHAR ext[_MAX_EXT];
        WCHAR fname[_MAX_FNAME];
        _wsplitpath_s( pConv->szSrc, nullptr, 0, nullptr, 0, fname, _MAX_FNAME, ext, _MAX_EXT );

        if ( pConv != conversion.begin() )
            wprintf( L"\n");

        wprintf( L"reading %ls", pConv->szSrc );
        fflush(stdout);

        std::unique_ptr<Mesh> inMesh;
        std::vector<Mesh::Material> inMaterial;
        hr = E_NOTIMPL;
        if ( _wcsicmp( ext, L".vbo" ) == 0 )
        {
            hr = Mesh::CreateFromVBO( pConv->szSrc, inMesh );
        }
        else if ( _wcsicmp( ext, L".sdkmesh" ) == 0 )
        {
            wprintf(L"\nERROR: Importing SDKMESH files not supported\n");
            return 1;
        }
        else if ( _wcsicmp( ext, L".cmo" ) == 0 )
        {
            wprintf(L"\nERROR: Importing Visual Studio CMO files not supported\n");
            return 1;
        }
        else if ( _wcsicmp( ext, L".x" ) == 0 )
        {
            wprintf( L"\nERROR: Legacy Microsoft X files not supported\n");
            return 1;
        }
        else
        {
            hr = LoadFromOBJ(pConv->szSrc, inMesh, inMaterial, dwOptions);
        }
        if (FAILED(hr))
        {
            wprintf( L" FAILED (%08X)\n", hr);
            return 1;
        }

        size_t nVerts = inMesh->GetVertexCount();
        size_t nFaces = inMesh->GetFaceCount();

        if (!nVerts || !nFaces)
        {
            wprintf( L"\nERROR: Invalid mesh\n" );
            return 1;
        }

        assert(inMesh->GetPositionBuffer() != 0);
        assert(inMesh->GetIndexBuffer() != 0);

        wprintf(L"\nVerts: %Iu, nFaces: %Iu", nVerts, nFaces);

        // Prepare mesh for processing
        {
            // Adjacency
            float epsilon = (dwOptions & (1 << OPT_GEOMETRIC_ADJ)) ? 1e-5f : 0.f;

            hr = inMesh->GenerateAdjacency(epsilon);
            if (FAILED(hr))
            {
                wprintf( L"\nERROR: Failed generating adjacency (%08X)\n", hr );
                return 1;
            }

            // Validation
            std::wstring msgs;
            hr = inMesh->Validate( VALIDATE_BACKFACING | VALIDATE_BOWTIES, &msgs );
            if (!msgs.empty())
            {
                wprintf(L"\nWARNING: \n");
                wprintf(msgs.c_str());
            }

            // Clean
            hr = inMesh->Clean( true );
            if (FAILED(hr))
            {
                wprintf( L"\nERROR: Failed mesh clean (%08X)\n", hr );
                return 1;
            }
            else
            {
                size_t nNewVerts = inMesh->GetVertexCount();
                if (nVerts != nNewVerts)
                {
                    wprintf(L" [%Iu vertex dups] ", nNewVerts - nVerts);
                    nVerts = nNewVerts;
                }
            }
        }

        if (!inMesh->GetNormalBuffer())
        {
            dwOptions |= 1 << OPT_NORMALS;
        }

        if (!inMesh->GetTangentBuffer() && (dwOptions & (1 << OPT_CMO)))
        {
            dwOptions |= 1 << OPT_TANGENTS;
        }

        // Compute vertex normals from faces
        if ((dwOptions & (1 << OPT_NORMALS))
             || ((dwOptions & ((1 << OPT_TANGENTS) | (1 << OPT_CTF))) && !inMesh->GetNormalBuffer()) )
        {
            DWORD flags = CNORM_DEFAULT;

            if (dwOptions & (1 << OPT_WEIGHT_BY_EQUAL))
            {
                flags |= CNORM_WEIGHT_EQUAL;
            }
            else if (dwOptions & (1 << OPT_WEIGHT_BY_AREA))
            {
                flags |= CNORM_WEIGHT_BY_AREA;
            }

            if (dwOptions & (1 << OPT_CLOCKWISE))
            {
                flags |= CNORM_WIND_CW;
            }

            hr = inMesh->ComputeNormals( flags );
            if (FAILED(hr))
            {
                wprintf( L"\nERROR: Failed computing normals (flags:%1X, %08X)\n", flags, hr );
                return 1;
            }
        }

        // Compute tangents and bitangents
        if (dwOptions & ((1 << OPT_TANGENTS) | (1 << OPT_CTF)))
        {
            if (!inMesh->GetTexCoordBuffer())
            {
                wprintf( L"\nERROR: Computing tangents/bi-tangents requires texture coordinates\n" );
                return 1;
            }

            hr = inMesh->ComputeTangentFrame( (dwOptions & (1 << OPT_CTF)) ? true : false );
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing tangent frame (%08X)\n", hr);
                return 1;
            }
        }

        // Compute IMT
        std::unique_ptr<float[]> IMTData;
        if ( dwOptions & ((1 << OPT_IMT_TEXFILE) | (1 << OPT_IMT_VERTEX)) )
        {
            if (dwOptions & (1 << OPT_IMT_TEXFILE))
            {
                if (!inMesh->GetTexCoordBuffer())
                {
                    wprintf( L"\nERROR: Computing IMT from texture requires texture coordinates\n" );
                    return 1;
                }

                WCHAR txext[_MAX_EXT];
                _wsplitpath_s(szTexFile, nullptr, 0, nullptr, 0, nullptr, 0, txext, _MAX_EXT);

                ScratchImage iimage;

                if (_wcsicmp(txext, L".dds") == 0)
                {
                    hr = LoadFromDDSFile(szTexFile, DDS_FLAGS_NONE, nullptr, iimage);
                }
                else if (_wcsicmp(ext, L".tga") == 0)
                {
                    hr = LoadFromTGAFile(szTexFile, nullptr, iimage);
                }
                else
                {
                    hr = LoadFromWICFile(szTexFile, TEX_FILTER_DEFAULT, nullptr, iimage);
                }
                if (FAILED(hr))
                {
                    wprintf(L"\nWARNING: Failed to load texture for IMT (%08X):\n%ls\n", hr, szTexFile );
                }
                else
                {
                    const Image* img = iimage.GetImage(0, 0, 0);

                    ScratchImage floatImage;
                    if (img->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
                    {
                        hr = Convert(*iimage.GetImage(0, 0, 0), DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, 0.5f, floatImage);
                        if (FAILED(hr))
                        {
                            img = nullptr;
                            wprintf(L"\nWARNING: Failed converting texture for IMT (%08X):\n%ls\n", hr, szTexFile);
                        }
                        else
                        {
                            img = floatImage.GetImage(0, 0, 0);
                        }
                    }

                    if ( img )
                    {
                        wprintf(L"\nComputing IMT from file %ls...\n", szTexFile);
                        IMTData.reset(new (std::nothrow) float[nFaces * 3]);
                        if (!IMTData)
                        {
                            wprintf(L"\nERROR: out of memory\n");
                            return 1;
                        }

                        hr = UVAtlasComputeIMTFromTexture(inMesh->GetPositionBuffer(), inMesh->GetTexCoordBuffer(), nVerts,
                                                          inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
                                                          reinterpret_cast<const float*>( img->pixels ), img->width, img->height,
                                                          UVATLAS_IMT_DEFAULT, UVAtlasCallback, IMTData.get());
                        if (FAILED(hr))
                        {
                            IMTData.reset();
                            wprintf(L"WARNING: Failed to compute IMT from texture (%08X):\n%ls\n", hr, szTexFile);
                        }
                    }
                }
            }
            else
            {
                const WCHAR* szChannel = L"*unknown*";
                const float* pSignal = nullptr;
                size_t signalDim = 0;
                size_t signalStride = 0;
                switch (perVertex)
                {
                case CHANNEL_NORMAL:
                    szChannel = L"normals";
                    if (inMesh->GetNormalBuffer())
                    {
                        pSignal = reinterpret_cast<const float*>(inMesh->GetNormalBuffer());
                        signalDim = 3;
                        signalStride = sizeof(XMFLOAT3);
                    }
                    break;

                case CHANNEL_COLOR:
                    szChannel = L"vertex colors";
                    if (inMesh->GetColorBuffer())
                    {
                        pSignal = reinterpret_cast<const float*>(inMesh->GetColorBuffer());
                        signalDim = 4;
                        signalStride = sizeof(XMFLOAT4);
                    }
                    break;

                case CHANNEL_TEXCOORD:
                    szChannel = L"texture coordinates";
                    if (inMesh->GetTexCoordBuffer())
                    {
                        pSignal = reinterpret_cast<const float*>(inMesh->GetTexCoordBuffer());
                        signalDim = 2;
                        signalStride = sizeof(XMFLOAT2);
                    }
                    break;
                }

                if (!pSignal)
                {
                    wprintf(L"\nWARNING: Mesh does not have channel %ls for IMT\n", szChannel );
                }
                else
                {
                    wprintf(L"\nComputing IMT from %ls...\n", szChannel);

                    IMTData.reset(new (std::nothrow) float[nFaces * 3]);
                    if (!IMTData)
                    {
                        wprintf(L"\nERROR: out of memory\n");
                        return 1;
                    }
                
                    hr = UVAtlasComputeIMTFromPerVertexSignal( inMesh->GetPositionBuffer(), nVerts,
                                                               inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
                                                               pSignal, signalDim, signalStride, UVAtlasCallback, IMTData.get() );

                    if (FAILED(hr))
                    {
                        IMTData.reset();
                        wprintf(L"WARNING: Failed to compute IMT from channel %ls (%08X)\n", szChannel, hr );
                    }
                }
            }
        }
        else
        {
            wprintf(L"\n");
        }

        // Perform UVAtlas isocharting
        wprintf( L"Computing isochart atlas on mesh...\n" );

        std::vector<UVAtlasVertex> vb;
        std::vector<uint8_t> ib;
        float outStretch = 0.f;
        size_t outCharts = 0;
        std::vector<uint32_t> facePartitioning;
        std::vector<uint32_t> vertexRemapArray;
        hr = UVAtlasCreate( inMesh->GetPositionBuffer(), nVerts,
                            inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
                            maxCharts, maxStretch, width, height, gutter,
                            inMesh->GetAdjacencyBuffer(), nullptr,
                            IMTData.get(),
                            UVAtlasCallback, UVATLAS_DEFAULT_CALLBACK_FREQUENCY,
                            uvOptions, vb, ib,
                            &facePartitioning,
                            &vertexRemapArray,
                            &outStretch, &outCharts );
        if ( FAILED(hr) )
        {
            if ( hr == HRESULT_FROM_WIN32( ERROR_INVALID_DATA ) )
            {
                wprintf( L"\nERROR: Non-manifold mesh\n" );
                return 1;
            }
            else
            {
                wprintf( L"\nERROR: Failed creating isocharts (%08X)\n", hr );
                return 1;
            }
        }

        wprintf( L"Output # of charts: %Iu, resulting stretching %f, %Iu verts\n", outCharts, outStretch, vb.size());

        assert((ib.size() / sizeof(uint32_t) ) == (nFaces*3));
        assert(facePartitioning.size() == nFaces);
        assert(vertexRemapArray.size() == vb.size());

        hr = inMesh->UpdateFaces( nFaces, reinterpret_cast<const uint32_t*>( &ib.front() ) );
        if ( FAILED(hr) )
        {
            wprintf(L"\nERROR: Failed applying atlas indices (%08X)\n", hr);
            return 1;
        }

        hr = inMesh->VertexRemap( &vertexRemapArray.front(), vertexRemapArray.size() );
        if ( FAILED(hr) )
        {
            wprintf(L"\nERROR: Failed applying atlas vertex remap (%08X)\n", hr);
            return 1;
        }

        nVerts = vb.size();

#ifdef _DEBUG
        std::wstring msgs;
        hr = inMesh->Validate( VALIDATE_DEFAULT, &msgs );
        if (!msgs.empty())
        {
            wprintf(L"\nWARNING: \n");
            wprintf(msgs.c_str());
        }
#endif

        // Copy isochart UVs into mesh
        {
            std::unique_ptr<XMFLOAT2[]> texcoord( new (std::nothrow) XMFLOAT2[nVerts] );
            if (!texcoord)
            {
                wprintf(L"\nERROR: out of memory\n");
                return 1;
            }

            auto txptr = texcoord.get();
            size_t j = 0;
            for (auto it = vb.cbegin(); it != vb.cend() && j < nVerts; ++it, ++txptr)
            {
                *txptr = it->uv;
            }

            hr = inMesh->UpdateUVs( nVerts, texcoord.get() );
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to update with isochart UVs\n");
                return 1;
            }
        }

        if (dwOptions & (1 << OPT_COLOR_MESH))
        {
            inMaterial.clear();
            inMaterial.reserve( _countof(g_ColorList) );

            for( size_t j = 0; j < _countof(g_ColorList) && (j < outCharts); ++j )
            {
                Mesh::Material mtl;
                memset( &mtl, 0, sizeof(mtl) );

                WCHAR matname[32];
                wsprintf( matname, L"Chart%02Iu", j+1 );
                mtl.name = matname;
                mtl.specularPower = 1.f;
                mtl.alpha = 1.f;

                XMVECTOR v = XMLoadFloat3( &g_ColorList[j] );
                XMStoreFloat3( &mtl.diffuseColor, v );

                v = XMVectorScale( v, 0.2f );
                XMStoreFloat3( &mtl.ambientColor, v );

                inMaterial.push_back(mtl);
            }

            std::unique_ptr<uint32_t[]> attr( new (std::nothrow) uint32_t[ nFaces ] );
            if ( !attr )
            {
                wprintf(L"\nERROR: out of memory\n" );
                return 1;
            }

            size_t j = 0;
            for( auto it = facePartitioning.cbegin(); it != facePartitioning.cend(); ++it, ++j )
            {
                attr[j] = *it % _countof(g_ColorList);
            }

            hr = inMesh->UpdateAttributes( nFaces, attr.get() );
            if ( FAILED(hr) )
            {
                wprintf(L"\nERROR: Failed applying atlas attributes (%08X)\n", hr);
                return 1;
            }
        }

        if (dwOptions & ((1 << OPT_FLIP) | (1 << OPT_FLIPTC)))
        {
            hr = inMesh->ReverseWinding( (dwOptions & (1 << OPT_FLIPTC)) ? true : false );
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing winding (%08X)\n", hr);
                return 1;
            }
        }

        // Write results
        wprintf(L"\n\t->\n");

        WCHAR outputPath[ MAX_PATH ] = { 0 };
        WCHAR outputExt[ _MAX_EXT] = { 0 };

        if ( *szOutputFile )
        {
            wcscpy_s( outputPath, szOutputFile );
            
            _wsplitpath_s( szOutputFile, nullptr, 0, nullptr, 0, nullptr, 0, outputExt, _MAX_EXT );
        }
        else
        {
            if (dwOptions & (1 << OPT_VBO))
            {
                wcscpy_s(outputExt, L".vbo");
            }
            else if (dwOptions & (1 << OPT_CMO))
            {
                wcscpy_s(outputExt, L".cmo");
            }
            else
            {
                wcscpy_s(outputExt, L".sdkmesh");
            }

            WCHAR outFilename[ _MAX_FNAME ] = { 0 };
            wcscpy_s( outFilename, fname );

            _wmakepath_s( outputPath, nullptr, nullptr, outFilename, outputExt );
        }

        if ( ~dwOptions & (1 << OPT_OVERWRITE) )
        {
            if (GetFileAttributesW(outputPath) != INVALID_FILE_ATTRIBUTES)
            {
                wprintf(L"\nERROR: Output file already exists, use -y to overwrite:\n'%ls'\n", outputPath);
                return 1;
            }
        }

        if ( !_wcsicmp(outputExt, L".vbo") )
        {
            if (!inMesh->GetNormalBuffer() || !inMesh->GetTexCoordBuffer())
            {
                wprintf( L"\nERROR: VBO requires position, normal, and texcoord\n" );
                return 1;
            }

            if (!inMesh->Is16BitIndexBuffer())
            {
                wprintf(L"\nERROR: VBO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToVBO(outputPath);
        }
        else if ( !_wcsicmp(outputExt, L".sdkmesh") )
        {
            hr = inMesh->ExportToSDKMESH(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : &inMaterial.front());
        }
        else if ( !_wcsicmp(outputExt, L".cmo") )
        {
            if (!inMesh->GetNormalBuffer() || !inMesh->GetTexCoordBuffer() || !inMesh->GetTangentBuffer())
            {
                wprintf(L"\nERROR: Visual Studio CMO requires position, normal, tangents, and texcoord\n");
                return 1;
            }

            if (!inMesh->Is16BitIndexBuffer())
            {
                wprintf(L"\nERROR: Visual Studio CMO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToCMO(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : &inMaterial.front());
        }
        else if ( !_wcsicmp(outputExt, L".x") )
        {
            wprintf(L"\nERROR: Legacy Microsoft X files not supported\n");
            return 1;
        }
        else
        {
            wprintf(L"\nERROR: Unknown output file type '%ls'\n", outputExt);
            return 1;
        }

        if (FAILED(hr))
        {
            wprintf(L"\nERROR: Failed write (%08X):-> '%ls'\n", hr, outputPath);
            return 1;
        }

        wprintf(L" %Iu vertices, %Iu faces written:\n'%ls'\n", nVerts, nFaces, outputPath);

        // Write out UV mesh visualization
        if (dwOptions & (1 << OPT_UV_MESH))
        {
            hr = inMesh->VisualizeUVs();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to create UV visualization mesh\n");
                return 1;
            }

            WCHAR uvFilename[_MAX_FNAME] = { 0 };
            wcscpy_s(uvFilename, fname);
            wcscat_s(uvFilename, L"_texture");

            _wmakepath_s(outputPath, nullptr, nullptr, uvFilename, outputExt);

            if (!_wcsicmp(outputExt, L".vbo"))
            {
                hr = inMesh->ExportToVBO(outputPath);
            }
            else if (!_wcsicmp(outputExt, L".sdkmesh"))
            {
                hr = inMesh->ExportToSDKMESH(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : &inMaterial.front());
            }
            else if (!_wcsicmp(outputExt, L".cmo"))
            {
                hr = inMesh->ExportToCMO(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : &inMaterial.front());
            }
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed uv mesh write (%08X):-> '%ls'\n", hr, outputPath);
                return 1;
            }
            wprintf(L"uv mesh visualization '%ls'\n", outputPath);
        }
    }

    return 0;
}
