//--------------------------------------------------------------------------------------
// File: UVAtlas.cpp
//
// UVAtlas command-line tool (sample for UVAtlas library)
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//--------------------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <conio.h>

#include <fstream>
#include <memory>
#include <list>

#include <dxgiformat.h>

#include "UVAtlas.h"
#include "DirectXTex.h"

#include "Mesh.h"

//Uncomment to add support for OpenEXR (.exr)
//#define USE_OPENEXR

#ifdef USE_OPENEXR
// See <https://github.com/Microsoft/DirectXTex/wiki/Adding-OpenEXR> for details
#include "DirectXTexEXR.h"
#endif

using namespace DirectX;

enum OPTIONS
{
    OPT_RECURSIVE = 1,
    OPT_QUALITY,
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
    OPT_SDKMESH_V2,
    OPT_CMO,
    OPT_VBO,
    OPT_OUTPUTFILE,
    OPT_CLOCKWISE,
    OPT_FORCE_32BIT_IB,
    OPT_OVERWRITE,
    OPT_NODDS,
    OPT_FLIP,
    OPT_FLIPU,
    OPT_FLIPV,
    OPT_FLIPZ,
    OPT_NOLOGO,
    OPT_FILELIST,
    OPT_MAX
};

static_assert(OPT_MAX <= 64, "dwOptions is a DWORD64 bitfield");

enum CHANNELS
{
    CHANNEL_NONE = 0,
    CHANNEL_NORMAL,
    CHANNEL_COLOR,
    CHANNEL_TEXCOORD,
};

struct SConversion
{
    wchar_t szSrc[MAX_PATH];
};

struct SValue
{
    LPCWSTR pName;
    DWORD dwValue;
};

static const XMFLOAT3 g_ColorList[8] =
{
    XMFLOAT3(1.0f, 0.5f, 0.5f),
    XMFLOAT3(0.5f, 1.0f, 0.5f),
    XMFLOAT3(1.0f, 1.0f, 0.5f),
    XMFLOAT3(0.5f, 1.0f, 1.0f),
    XMFLOAT3(1.0f, 0.5f, 0.75f),
    XMFLOAT3(0.0f, 0.5f, 0.75f),
    XMFLOAT3(0.5f, 0.5f, 0.75f),
    XMFLOAT3(0.5f, 0.5f, 1.0f),
};

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

const SValue g_pOptions [] =
{
    { L"r",         OPT_RECURSIVE },
    { L"q",         OPT_QUALITY },
    { L"n",         OPT_MAXCHARTS },
    { L"st",        OPT_MAXSTRETCH },
    { L"g",         OPT_GUTTER },
    { L"w",         OPT_WIDTH },
    { L"h",         OPT_HEIGHT },
    { L"ta",        OPT_TOPOLOGICAL_ADJ },
    { L"ga",        OPT_GEOMETRIC_ADJ },
    { L"nn",        OPT_NORMALS },
    { L"na",        OPT_WEIGHT_BY_AREA },
    { L"ne",        OPT_WEIGHT_BY_EQUAL },
    { L"tt",        OPT_TANGENTS },
    { L"tb",        OPT_CTF },
    { L"c",         OPT_COLOR_MESH },
    { L"t",         OPT_UV_MESH },
    { L"it",        OPT_IMT_TEXFILE },
    { L"iv",        OPT_IMT_VERTEX },
    { L"o",         OPT_OUTPUTFILE },
    { L"sdkmesh",   OPT_SDKMESH },
    { L"sdkmesh2",  OPT_SDKMESH_V2 },
    { L"cmo",       OPT_CMO },
    { L"vbo",       OPT_VBO },
    { L"cw",        OPT_CLOCKWISE },
    { L"ib32",      OPT_FORCE_32BIT_IB },
    { L"y",         OPT_OVERWRITE },
    { L"nodds",     OPT_NODDS },
    { L"flip",      OPT_FLIP },
    { L"flipu",     OPT_FLIPU },
    { L"flipv",     OPT_FLIPV },
    { L"flipz",     OPT_FLIPZ },
    { L"nologo",    OPT_NOLOGO },
    { L"flist",     OPT_FILELIST },
    { nullptr,      0 }
};

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace
{
    inline HANDLE safe_handle(HANDLE h) { return (h == INVALID_HANDLE_VALUE) ? nullptr : h; }

    struct find_closer { void operator()(HANDLE h) { assert(h != INVALID_HANDLE_VALUE); if (h) FindClose(h); } };

    using ScopedFindHandle = std::unique_ptr<void, find_closer>;

#ifdef _PREFAST_
#pragma prefast(disable : 26018, "Only used with static internal arrays")
#endif

    DWORD LookupByName(const wchar_t *pName, const SValue *pArray)
    {
        while (pArray->pName)
        {
            if (!_wcsicmp(pName, pArray->pName))
                return pArray->dwValue;

            pArray++;
        }

        return 0;
    }


    void SearchForFiles(const wchar_t* path, std::list<SConversion>& files, bool recursive)
    {
        // Process files
        WIN32_FIND_DATAW findData = {};
        ScopedFindHandle hFile(safe_handle(FindFirstFileExW(path,
            FindExInfoBasic, &findData,
            FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH)));
        if (hFile)
        {
            for (;;)
            {
                if (!(findData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)))
                {
                    wchar_t drive[_MAX_DRIVE] = {};
                    wchar_t dir[_MAX_DIR] = {};
                    _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

                    SConversion conv;
                    _wmakepath_s(conv.szSrc, drive, dir, findData.cFileName, nullptr);
                    files.push_back(conv);
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }

        // Process directories
        if (recursive)
        {
            wchar_t searchDir[MAX_PATH] = {};
            {
                wchar_t drive[_MAX_DRIVE] = {};
                wchar_t dir[_MAX_DIR] = {};
                _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
                _wmakepath_s(searchDir, drive, dir, L"*", nullptr);
            }

            hFile.reset(safe_handle(FindFirstFileExW(searchDir,
                FindExInfoBasic, &findData,
                FindExSearchLimitToDirectories, nullptr,
                FIND_FIRST_EX_LARGE_FETCH)));
            if (!hFile)
                return;

            for (;;)
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (findData.cFileName[0] != L'.')
                    {
                        wchar_t subdir[MAX_PATH] = {};

                        {
                            wchar_t drive[_MAX_DRIVE] = {};
                            wchar_t dir[_MAX_DIR] = {};
                            wchar_t fname[_MAX_FNAME] = {};
                            wchar_t ext[_MAX_FNAME] = {};
                            _wsplitpath_s(path, drive, dir, fname, ext);
                            wcscat_s(dir, findData.cFileName);
                            _wmakepath_s(subdir, drive, dir, fname, ext);
                        }

                        SearchForFiles(subdir, files, recursive);
                    }
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }
    }


    void PrintLogo()
    {
        wprintf(L"Microsoft (R) UVAtlas Command-line Tool\n");
        wprintf(L"Copyright (C) Microsoft Corp. All rights reserved.\n");
#ifdef _DEBUG
        wprintf(L"*** Debug build ***\n");
#endif
        wprintf(L"\n");
    }


    void PrintUsage()
    {
        PrintLogo();

        wprintf(L"Usage: uvatlas <options> <files>\n");
        wprintf(L"\n");
        wprintf(L"   Input file type must be Wavefront OBJ\n\n");
        wprintf(L"   Output file type:\n");
        wprintf(L"       -sdkmesh        DirectX SDK .sdkmesh format (default)\n");
        wprintf(L"       -sdkmesh2       .sdkmesh format version 2 (PBR materials)\n");
        wprintf(L"       -cmo            Visual Studio Content Pipeline .cmo format\n");
        wprintf(L"       -vbo            Vertex Buffer Object (.vbo) format\n\n");
        wprintf(L"   -r                  wildcard filename search is recursive\n");
        wprintf(L"   -q <level>          sets quality level to DEFAULT, FAST or QUALITY\n");
        wprintf(L"   -n <number>         maximum number of charts to generate (def: 0)\n");
        wprintf(L"   -st <float>         maximum amount of stretch 0.0 to 1.0 (def: 0.16667)\n");
        wprintf(L"   -g <float>          the gutter width betwen charts in texels (def: 2.0)\n");
        wprintf(L"   -w <number>         texture width (def: 512)\n");
        wprintf(L"   -h <number>         texture height (def: 512)\n");
        wprintf(L"   -ta | -ga           generate topological vs. geometric adjancecy (def: ta)\n");
        wprintf(L"   -nn | -na | -ne     generate normals weighted by angle/area/equal\n");
        wprintf(L"   -tt                 generate tangents\n");
        wprintf(L"   -tb                 generate tangents & bi-tangents\n");
        wprintf(L"   -cw                 faces are clockwise (defaults to counter-clockwise)\n");
        wprintf(L"   -ib32               use 32-bit index buffer (SDKMESH only)\n");
        wprintf(L"   -c                  generate mesh with colors showing charts\n");
        wprintf(L"   -t                  generates a separate mesh with uvs - (*_texture)\n");
        wprintf(L"   -it <filename>      calculate IMT for the mesh using this texture map\n");
        wprintf(
            L"   -iv <channel>       calculate IMT using per-vertex data\n"
            L"                       NORMAL, COLOR, TEXCOORD\n");
        wprintf(L"   -nodds              prevents extension renaming in exported materials\n");
        wprintf(L"   -flip               reverse winding of faces\n");
        wprintf(L"   -flipu              inverts the u texcoords\n");
        wprintf(L"   -flipv              inverts the v texcoords\n");
        wprintf(L"   -flipz              flips the handedness of the positions/normals\n");
        wprintf(L"   -o <filename>       output filename\n");
        wprintf(L"   -y                  overwrite existing output file (if any)\n");
        wprintf(L"   -nologo             suppress copyright message\n");
        wprintf(L"   -flist <filename>   use text file with a list of input files (one per line)\n");

        wprintf(L"\n");
    }


    //--------------------------------------------------------------------------------------
    HRESULT __cdecl UVAtlasCallback(float fPercentDone)
    {
        static ULONGLONG s_lastTick = 0;

        ULONGLONG tick = GetTickCount64();

        if ((tick - s_lastTick) > 1000)
        {
            wprintf(L"%.2f%%   \r", double(fPercentDone) * 100);
            s_lastTick = tick;
        }

        if (_kbhit())
        {
            if (_getch() == 27)
            {
                wprintf(L"*** ABORT ***");
                return E_ABORT;
            }
        }

        return S_OK;
    }
}

extern HRESULT LoadFromOBJ(const wchar_t* szFilename, std::unique_ptr<Mesh>& inMesh, std::vector<Mesh::Material>& inMaterial, bool ccw, bool dds);

//--------------------------------------------------------------------------------------
// Entry-point
//--------------------------------------------------------------------------------------
#ifdef _PREFAST_
#pragma prefast(disable : 28198, "Command-line tool, frees all memory on exit")
#endif

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

    wchar_t szTexFile[MAX_PATH] = {};
    wchar_t szOutputFile[MAX_PATH] = {};

    // Initialize COM (needed for WIC)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        wprintf(L"Failed to initialize COM (%08X)\n", hr);
        return 1;
    }

    // Process command line
    DWORD64 dwOptions = 0;
    std::list<SConversion> conversion;

    for (int iArg = 1; iArg < argc; iArg++)
    {
        PWSTR pArg = argv[iArg];

        if (('-' == pArg[0]) || ('/' == pArg[0]))
        {
            pArg++;
            PWSTR pValue;

            for (pValue = pArg; *pValue && (':' != *pValue); pValue++);

            if (*pValue)
                *pValue++ = 0;

            DWORD dwOption = LookupByName(pArg, g_pOptions);

            if (!dwOption || (dwOptions & (DWORD64(1) << dwOption)))
            {
                wprintf(L"ERROR: unknown command-line option '%ls'\n\n", pArg);
                PrintUsage();
                return 1;
            }

            dwOptions |= (DWORD64(1) << dwOption);

            // Handle options with additional value parameter
            switch (dwOption)
            {
            case OPT_QUALITY:
            case OPT_MAXCHARTS:
            case OPT_MAXSTRETCH:
            case OPT_GUTTER:
            case OPT_WIDTH:
            case OPT_HEIGHT:
            case OPT_IMT_TEXFILE:
            case OPT_IMT_VERTEX:
            case OPT_OUTPUTFILE:
            case OPT_FILELIST:
                if (!*pValue)
                {
                    if ((iArg + 1 >= argc))
                    {
                        wprintf(L"ERROR: missing value for command-line option '%ls'\n\n", pArg);
                        PrintUsage();
                        return 1;
                    }

                    iArg++;
                    pValue = argv[iArg];
                }
                break;
            }

            switch (dwOption)
            {
            case OPT_QUALITY:
                if (!_wcsicmp(pValue, L"DEFAULT"))
                {
                    uvOptions = UVATLAS_DEFAULT;
                }
                else if (!_wcsicmp(pValue, L"FAST"))
                {
                    uvOptions = UVATLAS_GEODESIC_FAST;
                }
                else if (!_wcsicmp(pValue, L"QUALITY"))
                {
                    uvOptions = UVATLAS_GEODESIC_QUALITY;
                }
                else
                {
                    wprintf(L"Invalid value specified with -q (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_MAXCHARTS:
                if (swscanf_s(pValue, L"%zu", &maxCharts) != 1)
                {
                    wprintf(L"Invalid value specified with -n (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_MAXSTRETCH:
                if (swscanf_s(pValue, L"%f", &maxStretch) != 1
                    || maxStretch < 0.f
                    || maxStretch > 1.f)
                {
                    wprintf(L"Invalid value specified with -st (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_GUTTER:
                if (swscanf_s(pValue, L"%f", &gutter) != 1
                    || gutter < 0.f)
                {
                    wprintf(L"Invalid value specified with -g (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_WIDTH:
                if (swscanf_s(pValue, L"%zu", &width) != 1)
                {
                    wprintf(L"Invalid value specified with -w (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_HEIGHT:
                if (swscanf_s(pValue, L"%zu", &height) != 1)
                {
                    wprintf(L"Invalid value specified with -h (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_WEIGHT_BY_AREA:
                if (dwOptions & (DWORD64(1) << OPT_WEIGHT_BY_EQUAL))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (DWORD64(1) << OPT_NORMALS);
                break;

            case OPT_WEIGHT_BY_EQUAL:
                if (dwOptions & (DWORD64(1) << OPT_WEIGHT_BY_AREA))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (DWORD64(1) << OPT_NORMALS);
                break;

            case OPT_IMT_TEXFILE:
                if (dwOptions & (DWORD64(1) << OPT_IMT_VERTEX))
                {
                    wprintf(L"Cannot use both if and iv at the same time\n");
                    return 1;
                }

                wcscpy_s(szTexFile, MAX_PATH, pValue);
                break;

            case OPT_IMT_VERTEX:
                if (dwOptions & (DWORD64(1) << OPT_IMT_TEXFILE))
                {
                    wprintf(L"Cannot use both if and iv at the same time\n");
                    return 1;
                }

                if (!_wcsicmp(pValue, L"COLOR"))
                {
                    perVertex = CHANNEL_COLOR;
                }
                else if (!_wcsicmp(pValue, L"NORMAL"))
                {
                    perVertex = CHANNEL_NORMAL;
                }
                else if (!_wcsicmp(pValue, L"TEXCOORD"))
                {
                    perVertex = CHANNEL_TEXCOORD;
                }
                else
                {
                    wprintf(L"Invalid value specified with -iv (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_OUTPUTFILE:
                wcscpy_s(szOutputFile, MAX_PATH, pValue);
                break;

            case OPT_TOPOLOGICAL_ADJ:
                if (dwOptions & (DWORD64(1) << OPT_GEOMETRIC_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_GEOMETRIC_ADJ:
                if (dwOptions & (DWORD64(1) << OPT_TOPOLOGICAL_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_SDKMESH:
            case OPT_SDKMESH_V2:
                if (dwOptions & ((DWORD64(1) << OPT_VBO) | (DWORD64(1) << OPT_CMO)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, or vbo\n");
                    return 1;
                }
                if (dwOption == OPT_SDKMESH_V2)
                {
                    dwOptions |= (DWORD64(1) << OPT_SDKMESH);
                }
                break;

            case OPT_CMO:
                if (dwOptions & ((DWORD64(1) << OPT_VBO) | (DWORD64(1) << OPT_SDKMESH)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, or vbo\n");
                    return 1;
                }
                break;

            case OPT_VBO:
                if (dwOptions & ((DWORD64(1) << OPT_SDKMESH) | (DWORD64(1) << OPT_CMO)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, or vbo\n");
                    return 1;
                }
                break;

            case OPT_FILELIST:
                {
                    std::wifstream inFile(pValue);
                    if (!inFile)
                    {
                        wprintf(L"Error opening -flist file %ls\n", pValue);
                        return 1;
                    }
                    wchar_t fname[1024] = {};
                    for (;;)
                    {
                        inFile >> fname;
                        if (!inFile)
                            break;

                        if (*fname == L'#')
                        {
                            // Comment
                        }
                        else if (*fname == L'-')
                        {
                            wprintf(L"Command-line arguments not supported in -flist file\n");
                            return 1;
                        }
                        else if (wcspbrk(fname, L"?*") != nullptr)
                        {
                            wprintf(L"Wildcards not supported in -flist file\n");
                            return 1;
                        }
                        else
                        {
                            SConversion conv;
                            wcscpy_s(conv.szSrc, MAX_PATH, fname);
                            conversion.push_back(conv);
                        }

                        inFile.ignore(1000, '\n');
                    }
                    inFile.close();
                }
                break;
            }
        }
        else if (wcspbrk(pArg, L"?*") != nullptr)
        {
            size_t count = conversion.size();
            SearchForFiles(pArg, conversion, (dwOptions & (DWORD64(1) << OPT_RECURSIVE)) != 0);
            if (conversion.size() <= count)
            {
                wprintf(L"No matching files found for %ls\n", pArg);
                return 1;
            }
        }
        else
        {
            SConversion conv;
            wcscpy_s(conv.szSrc, MAX_PATH, pArg);

            conversion.push_back(conv);
        }
    }

    if (conversion.empty())
    {
        PrintUsage();
        return 0;
    }

    if (*szOutputFile && conversion.size() > 1)
    {
        wprintf(L"Cannot use -o with multiple input files\n");
        return 1;
    }

    if (~dwOptions & (DWORD64(1) << OPT_NOLOGO))
        PrintLogo();

    // Process files
    for (auto pConv = conversion.begin(); pConv != conversion.end(); ++pConv)
    {
        wchar_t ext[_MAX_EXT];
        wchar_t fname[_MAX_FNAME];
        _wsplitpath_s(pConv->szSrc, nullptr, 0, nullptr, 0, fname, _MAX_FNAME, ext, _MAX_EXT);

        if (pConv != conversion.begin())
            wprintf(L"\n");

        wprintf(L"reading %ls", pConv->szSrc);
        fflush(stdout);

        std::unique_ptr<Mesh> inMesh;
        std::vector<Mesh::Material> inMaterial;
        hr = E_NOTIMPL;
        if (_wcsicmp(ext, L".vbo") == 0)
        {
            hr = Mesh::CreateFromVBO(pConv->szSrc, inMesh);
        }
        else if (_wcsicmp(ext, L".sdkmesh") == 0)
        {
            wprintf(L"\nERROR: Importing SDKMESH files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext, L".cmo") == 0)
        {
            wprintf(L"\nERROR: Importing Visual Studio CMO files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext, L".x") == 0)
        {
            wprintf(L"\nERROR: Legacy Microsoft X files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext, L".fbx") == 0)
        {
            wprintf(L"\nERROR: Autodesk FBX files not supported\n");
            return 1;
        }
        else
        {
            hr = LoadFromOBJ(pConv->szSrc, inMesh, inMaterial,
                (dwOptions & (1 << OPT_CLOCKWISE)) ? false : true,
                (dwOptions & (1 << OPT_NODDS)) ? false : true);
        }
        if (FAILED(hr))
        {
            wprintf(L" FAILED (%08X)\n", hr);
            return 1;
        }

        size_t nVerts = inMesh->GetVertexCount();
        size_t nFaces = inMesh->GetFaceCount();

        if (!nVerts || !nFaces)
        {
            wprintf(L"\nERROR: Invalid mesh\n");
            return 1;
        }

        assert(inMesh->GetPositionBuffer() != nullptr);
        assert(inMesh->GetIndexBuffer() != nullptr);

        wprintf(L"\n%zu vertices, %zu faces", nVerts, nFaces);

        if (dwOptions & (DWORD64(1) << OPT_FLIPU))
        {
            hr = inMesh->InvertUTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting u texcoord (%08X)\n", hr);
                return 1;
            }
        }

        if (dwOptions & (DWORD64(1) << OPT_FLIPV))
        {
            hr = inMesh->InvertVTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting v texcoord (%08X)\n", hr);
                return 1;
            }
        }

        if (dwOptions & (DWORD64(1) << OPT_FLIPZ))
        {
            hr = inMesh->ReverseHandedness();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing handedness (%08X)\n", hr);
                return 1;
            }
        }

        // Prepare mesh for processing
        {
            // Adjacency
            float epsilon = (dwOptions & (DWORD64(1) << OPT_GEOMETRIC_ADJ)) ? 1e-5f : 0.f;

            hr = inMesh->GenerateAdjacency(epsilon);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed generating adjacency (%08X)\n", hr);
                return 1;
            }

            // Validation
            std::wstring msgs;
            hr = inMesh->Validate(VALIDATE_BACKFACING | VALIDATE_BOWTIES, &msgs);
            if (!msgs.empty())
            {
                wprintf(L"\nWARNING: \n");
                wprintf(msgs.c_str());
            }

            // Clean
            hr = inMesh->Clean(true);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed mesh clean (%08X)\n", hr);
                return 1;
            }
            else
            {
                size_t nNewVerts = inMesh->GetVertexCount();
                if (nVerts != nNewVerts)
                {
                    wprintf(L" [%zu vertex dups] ", nNewVerts - nVerts);
                    nVerts = nNewVerts;
                }
            }
        }

        if (!inMesh->GetNormalBuffer())
        {
            dwOptions |= DWORD64(1) << OPT_NORMALS;
        }

        if (!inMesh->GetTangentBuffer() && (dwOptions & (DWORD64(1) << OPT_CMO)))
        {
            dwOptions |= DWORD64(1) << OPT_TANGENTS;
        }

        // Compute vertex normals from faces
        if ((dwOptions & (DWORD64(1) << OPT_NORMALS))
            || ((dwOptions & ((DWORD64(1) << OPT_TANGENTS) | (DWORD64(1) << OPT_CTF))) && !inMesh->GetNormalBuffer()))
        {
            DWORD flags = CNORM_DEFAULT;

            if (dwOptions & (DWORD64(1) << OPT_WEIGHT_BY_EQUAL))
            {
                flags |= CNORM_WEIGHT_EQUAL;
            }
            else if (dwOptions & (DWORD64(1) << OPT_WEIGHT_BY_AREA))
            {
                flags |= CNORM_WEIGHT_BY_AREA;
            }

            if (dwOptions & (DWORD64(1) << OPT_CLOCKWISE))
            {
                flags |= CNORM_WIND_CW;
            }

            hr = inMesh->ComputeNormals(flags);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing normals (flags:%1X, %08X)\n", flags, hr);
                return 1;
            }
        }

        // Compute tangents and bitangents
        if (dwOptions & ((DWORD64(1) << OPT_TANGENTS) | (DWORD64(1) << OPT_CTF)))
        {
            if (!inMesh->GetTexCoordBuffer())
            {
                wprintf(L"\nERROR: Computing tangents/bi-tangents requires texture coordinates\n");
                return 1;
            }

            hr = inMesh->ComputeTangentFrame((dwOptions & (DWORD64(1) << OPT_CTF)) ? true : false);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing tangent frame (%08X)\n", hr);
                return 1;
            }
        }

        // Compute IMT
        std::unique_ptr<float[]> IMTData;
        if (dwOptions & ((DWORD64(1) << OPT_IMT_TEXFILE) | (DWORD64(1) << OPT_IMT_VERTEX)))
        {
            if (dwOptions & (DWORD64(1) << OPT_IMT_TEXFILE))
            {
                if (!inMesh->GetTexCoordBuffer())
                {
                    wprintf(L"\nERROR: Computing IMT from texture requires texture coordinates\n");
                    return 1;
                }

                wchar_t txext[_MAX_EXT];
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
                else if (_wcsicmp(ext, L".hdr") == 0)
                {
                    hr = LoadFromHDRFile(szTexFile, nullptr, iimage);
                }
#ifdef USE_OPENEXR
                else if (_wcsicmp(ext, L".exr") == 0)
                {
                    hr = LoadFromEXRFile(szTexFile, nullptr, iimage);
                }
#endif
                else
                {
                    hr = LoadFromWICFile(szTexFile, TEX_FILTER_DEFAULT, nullptr, iimage);
                }
                if (FAILED(hr))
                {
                    wprintf(L"\nWARNING: Failed to load texture for IMT (%08X):\n%ls\n", hr, szTexFile);
                }
                else
                {
                    const Image* img = iimage.GetImage(0, 0, 0);

                    ScratchImage floatImage;
                    if (img->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
                    {
                        hr = Convert(*iimage.GetImage(0, 0, 0), DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, floatImage);
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

                    if (img)
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
                            reinterpret_cast<const float*>(img->pixels), img->width, img->height,
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
                const wchar_t* szChannel = L"*unknown*";
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
                    wprintf(L"\nWARNING: Mesh does not have channel %ls for IMT\n", szChannel);
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

                    hr = UVAtlasComputeIMTFromPerVertexSignal(inMesh->GetPositionBuffer(), nVerts,
                        inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
                        pSignal, signalDim, signalStride, UVAtlasCallback, IMTData.get());

                    if (FAILED(hr))
                    {
                        IMTData.reset();
                        wprintf(L"WARNING: Failed to compute IMT from channel %ls (%08X)\n", szChannel, hr);
                    }
                }
            }
        }
        else
        {
            wprintf(L"\n");
        }

        // Perform UVAtlas isocharting
        wprintf(L"Computing isochart atlas on mesh...\n");

        std::vector<UVAtlasVertex> vb;
        std::vector<uint8_t> ib;
        float outStretch = 0.f;
        size_t outCharts = 0;
        std::vector<uint32_t> facePartitioning;
        std::vector<uint32_t> vertexRemapArray;
        hr = UVAtlasCreate(inMesh->GetPositionBuffer(), nVerts,
            inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
            maxCharts, maxStretch, width, height, gutter,
            inMesh->GetAdjacencyBuffer(), nullptr,
            IMTData.get(),
            UVAtlasCallback, UVATLAS_DEFAULT_CALLBACK_FREQUENCY,
            uvOptions, vb, ib,
            &facePartitioning,
            &vertexRemapArray,
            &outStretch, &outCharts);
        if (FAILED(hr))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA))
            {
                wprintf(L"\nERROR: Non-manifold mesh\n");
                return 1;
            }
            else
            {
                wprintf(L"\nERROR: Failed creating isocharts (%08X)\n", hr);
                return 1;
            }
        }

        wprintf(L"Output # of charts: %zu, resulting stretching %f, %zu verts\n", outCharts, double(outStretch), vb.size());

        assert((ib.size() / sizeof(uint32_t)) == (nFaces * 3));
        assert(facePartitioning.size() == nFaces);
        assert(vertexRemapArray.size() == vb.size());

        hr = inMesh->UpdateFaces(nFaces, reinterpret_cast<const uint32_t*>(ib.data()));
        if (FAILED(hr))
        {
            wprintf(L"\nERROR: Failed applying atlas indices (%08X)\n", hr);
            return 1;
        }

        hr = inMesh->VertexRemap(vertexRemapArray.data(), vertexRemapArray.size());
        if (FAILED(hr))
        {
            wprintf(L"\nERROR: Failed applying atlas vertex remap (%08X)\n", hr);
            return 1;
        }

        nVerts = vb.size();

#ifdef _DEBUG
        std::wstring msgs;
        hr = inMesh->Validate(VALIDATE_DEFAULT, &msgs);
        if (!msgs.empty())
        {
            wprintf(L"\nWARNING: \n");
            wprintf(msgs.c_str());
        }
#endif

        // Copy isochart UVs into mesh
        {
            std::unique_ptr<XMFLOAT2[]> texcoord(new (std::nothrow) XMFLOAT2[nVerts]);
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

            hr = inMesh->UpdateUVs(nVerts, texcoord.get());
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to update with isochart UVs\n");
                return 1;
            }
        }

        if (dwOptions & (DWORD64(1) << OPT_COLOR_MESH))
        {
            inMaterial.clear();
            inMaterial.reserve(_countof(g_ColorList));

            for (size_t j = 0; j < _countof(g_ColorList) && (j < outCharts); ++j)
            {
                Mesh::Material mtl = {};

                wchar_t matname[32] = {};
                swprintf_s(matname, L"Chart%02Iu", j + 1);
                mtl.name = matname;
                mtl.specularPower = 1.f;
                mtl.alpha = 1.f;

                XMVECTOR v = XMLoadFloat3(&g_ColorList[j]);
                XMStoreFloat3(&mtl.diffuseColor, v);

                v = XMVectorScale(v, 0.2f);
                XMStoreFloat3(&mtl.ambientColor, v);

                inMaterial.push_back(mtl);
            }

            std::unique_ptr<uint32_t[]> attr(new (std::nothrow) uint32_t[nFaces]);
            if (!attr)
            {
                wprintf(L"\nERROR: out of memory\n");
                return 1;
            }

            size_t j = 0;
            for (auto it = facePartitioning.cbegin(); it != facePartitioning.cend(); ++it, ++j)
            {
                attr[j] = *it % _countof(g_ColorList);
            }

            hr = inMesh->UpdateAttributes(nFaces, attr.get());
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed applying atlas attributes (%08X)\n", hr);
                return 1;
            }
        }

        if (dwOptions & (DWORD64(1) << OPT_FLIP))
        {
            hr = inMesh->ReverseWinding();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing winding (%08X)\n", hr);
                return 1;
            }
        }

        // Write results
        wprintf(L"\n\t->\n");

        wchar_t outputPath[MAX_PATH] = {};
        wchar_t outputExt[_MAX_EXT] = {};

        if (*szOutputFile)
        {
            wcscpy_s(outputPath, szOutputFile);

            _wsplitpath_s(szOutputFile, nullptr, 0, nullptr, 0, nullptr, 0, outputExt, _MAX_EXT);
        }
        else
        {
            if (dwOptions & (DWORD64(1) << OPT_VBO))
            {
                wcscpy_s(outputExt, L".vbo");
            }
            else if (dwOptions & (DWORD64(1) << OPT_CMO))
            {
                wcscpy_s(outputExt, L".cmo");
            }
            else
            {
                wcscpy_s(outputExt, L".sdkmesh");
            }

            wchar_t outFilename[_MAX_FNAME] = {};
            wcscpy_s(outFilename, fname);

            _wmakepath_s(outputPath, nullptr, nullptr, outFilename, outputExt);
        }

        if (~dwOptions & (DWORD64(1) << OPT_OVERWRITE))
        {
            if (GetFileAttributesW(outputPath) != INVALID_FILE_ATTRIBUTES)
            {
                wprintf(L"\nERROR: Output file already exists, use -y to overwrite:\n'%ls'\n", outputPath);
                return 1;
            }
        }

        if (!_wcsicmp(outputExt, L".vbo"))
        {
            if (!inMesh->GetNormalBuffer() || !inMesh->GetTexCoordBuffer())
            {
                wprintf(L"\nERROR: VBO requires position, normal, and texcoord\n");
                return 1;
            }

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (DWORD64(1) << OPT_FORCE_32BIT_IB)))
            {
                wprintf(L"\nERROR: VBO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToVBO(outputPath);
        }
        else if (!_wcsicmp(outputExt, L".sdkmesh"))
        {
            hr = inMesh->ExportToSDKMESH(
                outputPath,
                inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data(),
                (dwOptions & (DWORD64(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                (dwOptions & (DWORD64(1) << OPT_SDKMESH_V2)) ? true : false);
        }
        else if (!_wcsicmp(outputExt, L".cmo"))
        {
            if (!inMesh->GetNormalBuffer() || !inMesh->GetTexCoordBuffer() || !inMesh->GetTangentBuffer())
            {
                wprintf(L"\nERROR: Visual Studio CMO requires position, normal, tangents, and texcoord\n");
                return 1;
            }

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (DWORD64(1) << OPT_FORCE_32BIT_IB)))
            {
                wprintf(L"\nERROR: Visual Studio CMO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToCMO(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
        }
        else if (!_wcsicmp(outputExt, L".obj"))
        {
            hr = inMesh->ExportToOBJ(outputPath);
        }
        else if (!_wcsicmp(outputExt, L".x"))
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

        wprintf(L" %zu vertices, %zu faces written:\n'%ls'\n", nVerts, nFaces, outputPath);

        // Write out UV mesh visualization
        if (dwOptions & (DWORD64(1) << OPT_UV_MESH))
        {
            hr = inMesh->VisualizeUVs();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to create UV visualization mesh\n");
                return 1;
            }

            wchar_t uvFilename[_MAX_FNAME] = {};
            wcscpy_s(uvFilename, fname);
            wcscat_s(uvFilename, L"_texture");

            _wmakepath_s(outputPath, nullptr, nullptr, uvFilename, outputExt);

            if (!_wcsicmp(outputExt, L".vbo"))
            {
                hr = inMesh->ExportToVBO(outputPath);
            }
            else if (!_wcsicmp(outputExt, L".sdkmesh"))
            {
                hr = inMesh->ExportToSDKMESH(
                    outputPath,
                    inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data(),
                    (dwOptions & (DWORD64(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                    (dwOptions & (DWORD64(1) << OPT_SDKMESH_V2)) ? true : false);
            }
            else if (!_wcsicmp(outputExt, L".cmo"))
            {
                hr = inMesh->ExportToCMO(outputPath, inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
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
