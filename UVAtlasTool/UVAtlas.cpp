//--------------------------------------------------------------------------------------
// File: UVAtlas.cpp
//
// UVAtlas command-line tool (sample for UVAtlas library)
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//--------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005)
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOMCX
#define NOSERVICE
#define NOHELP
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if __cplusplus < 201703L
#error Requires C++17 (and /Zc:__cplusplus with MSVC)
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <locale>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <tuple>

#include <conio.h>

#include <dxgiformat.h>

#include "UVAtlas.h"
#include "DirectXTex.h"

#include "Mesh.h"

#define TOOL_VERSION UVATLAS_VERSION
#include "CmdLineHelpers.h"

// Uncomment to add support for OpenEXR (.exr)
// #define USE_OPENEXR

#ifdef USE_OPENEXR
// See <https://github.com/Microsoft/DirectXTex/wiki/Adding-OpenEXR> for details
#include "DirectXTexEXR.h"
#endif

using namespace Helpers;
using namespace DirectX;

namespace
{
    const wchar_t* g_ToolName = L"uvatlastool";
    const wchar_t* g_Description = L"Microsoft (R) UVAtlas Command-line Tool";

    enum OPTIONS : uint64_t
    {
        OPT_RECURSIVE = 1,
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
        OPT_TOLOWER,
        OPT_CLOCKWISE,
        OPT_FORCE_32BIT_IB,
        OPT_OVERWRITE,
        OPT_NODDS,
        OPT_FLIP,
        OPT_FLIPU,
        OPT_FLIPV,
        OPT_FLIPZ,
        OPT_SECOND_UV,
        OPT_VIZ_NORMALS,
        OPT_OUTPUT_REMAPPING,
        OPT_NOLOGO,
        OPT_FLAGS_MAX,
        OPT_QUALITY,
        OPT_MAXCHARTS,
        OPT_MAXSTRETCH,
        OPT_LIMIT_MERGE_STRETCH,
        OPT_LIMIT_FACE_STRETCH,
        OPT_GUTTER,
        OPT_WIDTH,
        OPT_HEIGHT,
        OPT_FILETYPE,
        OPT_OUTPUTFILE,
        OPT_FILELIST,
        OPT_VERT_NORMAL_FORMAT,
        OPT_VERT_UV_FORMAT,
        OPT_VERT_COLOR_FORMAT,
        OPT_SDKMESH,
        OPT_SDKMESH_V2,
        OPT_CMO,
        OPT_VBO,
        OPT_WAVEFRONT_OBJ,
        OPT_VERSION,
        OPT_HELP,
    };

    static_assert(OPT_FLAGS_MAX <= 64, "dwOptions is a unsigned int bitfield");

    enum class CHANNELS
    {
        NONE = 0,
        NORMAL,
        COLOR,
        TEXCOORD,
    };

    const XMFLOAT3 g_ColorList[8] =
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

    const SValue<uint64_t> g_pOptions[] =
    {
        { L"r",         OPT_RECURSIVE },
        { L"q",         OPT_QUALITY },
        { L"n",         OPT_MAXCHARTS },
        { L"st",        OPT_MAXSTRETCH },
        { L"lms",       OPT_LIMIT_MERGE_STRETCH },
        { L"lfs",       OPT_LIMIT_FACE_STRETCH },
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
        { L"l",         OPT_TOLOWER },
        { L"cw",        OPT_CLOCKWISE },
        { L"ib32",      OPT_FORCE_32BIT_IB },
        { L"y",         OPT_OVERWRITE },
        { L"ft",        OPT_FILETYPE },
        { L"nodds",     OPT_NODDS },
        { L"flip",      OPT_FLIP },
        { L"fn",        OPT_VERT_NORMAL_FORMAT },
        { L"fuv",       OPT_VERT_UV_FORMAT },
        { L"fc",        OPT_VERT_COLOR_FORMAT },
        { L"uv2",       OPT_SECOND_UV },
        { L"vn",        OPT_VIZ_NORMALS },
        { L"m",         OPT_OUTPUT_REMAPPING },
        { L"nologo",    OPT_NOLOGO },
        { L"flist",     OPT_FILELIST },

        // Legacy selection switches for file type (use -ft instead)
        { L"sdkmesh",   OPT_SDKMESH },
        { L"sdkmesh2",  OPT_SDKMESH_V2 },
        { L"cmo",       OPT_CMO },
        { L"vbo",       OPT_VBO },
        { L"wf",        OPT_WAVEFRONT_OBJ },

        // Deprecated options (recommend using new -- alternatives)
        { L"flipu",     OPT_FLIPU },
        { L"flipv",     OPT_FLIPV },
        { L"flipz",     OPT_FLIPZ },
        { nullptr,      0 }
    };

    const SValue<uint64_t> g_pOptionsLong[] =
    {
        { L"clockwise",                 OPT_CLOCKWISE },
        { L"color-format",              OPT_VERT_COLOR_FORMAT },
        { L"color-mesh",                OPT_COLOR_MESH },
        { L"file-list",                 OPT_FILELIST },
        { L"file-type",                 OPT_FILETYPE },
        { L"flip-face-winding",         OPT_FLIP },
        { L"flip-u",                    OPT_FLIPU },
        { L"flip-v",                    OPT_FLIPV },
        { L"flip-z",                    OPT_FLIPZ },
        { L"geometric-adjacency",       OPT_GEOMETRIC_ADJ },
        { L"gutter-width",              OPT_GUTTER },
        { L"height",                    OPT_HEIGHT },
        { L"help",                      OPT_HELP },
        { L"imt-tex-file",              OPT_IMT_TEXFILE },
        { L"imt-vertex",                OPT_IMT_VERTEX },
        { L"index-buffer-32-bit",       OPT_FORCE_32BIT_IB },
        { L"limit-face-stretch",        OPT_LIMIT_FACE_STRETCH },
        { L"limit-merge-stretch",       OPT_LIMIT_MERGE_STRETCH },
        { L"max-charts",                OPT_MAXCHARTS },
        { L"max-stretch",               OPT_MAXSTRETCH },
        { L"normal-format",             OPT_VERT_NORMAL_FORMAT },
        { L"normals-by-angle",          OPT_NORMALS },
        { L"normals-by-area",           OPT_WEIGHT_BY_AREA },
        { L"normals-by-equal",          OPT_WEIGHT_BY_EQUAL },
        { L"output-remap",              OPT_OUTPUT_REMAPPING },
        { L"overwrite",                 OPT_OVERWRITE },
        { L"quality",                   OPT_QUALITY },
        { L"tangent-frame",             OPT_CTF },
        { L"tangents",                  OPT_TANGENTS },
        { L"to-lowercase",              OPT_TOLOWER },
        { L"topological-adjacency",     OPT_TOPOLOGICAL_ADJ },
        { L"uv-format",                 OPT_VERT_UV_FORMAT },
        { L"uv-mesh",                   OPT_UV_MESH },
        { L"version",                   OPT_VERSION },
        { L"visualize-normals",         OPT_VIZ_NORMALS },
        { L"width",                     OPT_WIDTH },
        { nullptr,                      0 }
    };

    const SValue<DXGI_FORMAT> g_vertexNormalFormats[] =
    {
        { L"float3",    DXGI_FORMAT_R32G32B32_FLOAT },
        { L"float16_4", DXGI_FORMAT_R16G16B16A16_FLOAT },
        { L"r11g11b10", DXGI_FORMAT_R11G11B10_FLOAT },
        { nullptr,      DXGI_FORMAT_UNKNOWN }
    };

    const SValue<DXGI_FORMAT> g_vertexUVFormats[] =
    {
        { L"float2",    DXGI_FORMAT_R32G32_FLOAT },
        { L"float16_2", DXGI_FORMAT_R16G16_FLOAT },
        { nullptr,      DXGI_FORMAT_UNKNOWN }
    };

    const SValue<DXGI_FORMAT> g_vertexColorFormats[] =
    {
        { L"bgra",      DXGI_FORMAT_B8G8R8A8_UNORM },
        { L"rgba",      DXGI_FORMAT_R8G8B8A8_UNORM },
        { L"float4",    DXGI_FORMAT_R32G32B32A32_FLOAT },
        { L"float16_4", DXGI_FORMAT_R16G16B16A16_FLOAT },
        { L"rgba_10",   DXGI_FORMAT_R10G10B10A2_UNORM },
        { L"r11g11b10", DXGI_FORMAT_R11G11B10_FLOAT },
        { nullptr,      DXGI_FORMAT_UNKNOWN }
    };

    enum MESH_CODEC : uint32_t
    {
        CODEC_SDKMESH = 1,
        CODEC_SDKMESH_V2,
        CODEC_CMO,
        CODEC_VBO,
        CODEC_WAVEFRONT_OBJ,
    };

    const SValue<uint32_t> g_pMeshFileTypes[] = // valid formats to write to
    {
        { L"sdkmesh",   CODEC_SDKMESH },
        { L"sdkmesh2",  CODEC_SDKMESH_V2 },
        { L"cmo",       CODEC_CMO },
        { L"vbo",       CODEC_VBO },
        { L"obj",       CODEC_WAVEFRONT_OBJ },
        { L"_obj",      CODEC_WAVEFRONT_OBJ },
        { nullptr,      0 }
    };
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

HRESULT LoadFromOBJ(const wchar_t* szFilename,
    std::unique_ptr<Mesh>& inMesh, std::vector<Mesh::Material>& inMaterial,
    bool ccw, bool dds);

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

namespace
{
    void PrintUsage()
    {
        PrintLogo(false, g_ToolName, g_Description);

        static const wchar_t* const s_usage =
            L"Usage: uvatlas <options> [--] <files>\n"
            L"\n"
            L"   Input file type must be Wavefront Object (.obj)\n"
            L"\n"
            L"   -ft <filetype>, --file-type <filetype>  output file type\n"
            L"       sdkmesh:  DirectX SDK .sdkmesh format (default)\n"
            L"       sdkmesh2: sdkmesh format version 2 (PBR materials)\n"
            L"       cmo:      Visual Studio Content Pipeline .cmo format\n"
            L"       vbo:      Vertex Buffer Object (.vbo) format\n"
            L"       obj:      WaveFront Object (.obj) format\n"
            L"\n"
            L"   -r                  wildcard filename search is recursive\n"
            L"   -flist <filename>, --file-list <filename>\n"
            L"                       use text file with a list of input files (one per line)\n"
            L"\n"
            L"   -q <level>, --quality <level>       sets quality level to DEFAULT, FAST or QUALITY\n"
            L"   -n <number>, --max-charts <number>  maximum number of charts to generate (def: 0)\n"
            L"   -st <float>, --max-stretch <float>  maximum amount of stretch 0.0 to 1.0 (def: 0.16667)\n"
            L"   -lms, --limit-merge-stretch         enable limit merge stretch option\n"
            L"   -lfs, --limit-face-stretch          enable limit face stretch option\n"
            L"   -g <float>, --gutter-width <float>  the gutter width betwen charts in texels (def: 2.0)\n"
            L"   -w <number>, --width <number>       texture width (def: 512)\n"
            L"   -h <number>, --height <number>      texture height (def: 512)\n"
            L"\n"
            L"   -nn, --normal-by-angle   -na, --normal-by-area   -ne, --normal-by-equal\n"
            L"                                  generate normals weighted by angle/area/equal\n"
            L"   -tt, --tangents                generate tangents\n"
            L"   -tb, --tangent-frame           generate tangents & bi-tangents\n"
            L"   -cw, --clockwise               faces are clockwise (defaults to counter-clockwise)\n"
            L"\n"
            L"   -ta, --topological-adjacency -or- -ga, --geometric-adjacency\n"
            L"                                  generate topological vs. geometric adjacency (def: ta)\n"
            L"\n"
            L"   -c, --color-mesh               generate mesh with colors showing charts\n"
            L"   -t, --uv-mesh                  generates a separate mesh with uvs - (*_texture)\n"
            L"   -vn, --visualize-normals       with -t creates per vertex colors from normals\n"
            L"   -m, --output-remap             generates a text file with vertex remapping (*_map)\n"
            L"\n"
            L"   -it <filename>, --imt-tex-file <filename>\n"
            L"                                  calculate IMT for the mesh using this texture map\n"
            L"   -iv <channel>, --imt-vertex <channel>\n"
            L"                                  calculate IMT using per-vertex data\n"
            L"                                      NORMAL, COLOR, TEXCOORD\n"
            L"\n"
            L"   -nodds                         prevents extension renaming in exported materials\n"
            L"   -flip, --flip-face-winding     reverse winding of faces\n"
            L"   --flip-u                       inverts the u texcoords\n"
            L"   --flip-v                       inverts the v texcoords\n"
            L"   --flip-z                       flips the handedness of the positions/normals\n"
            L"   -o <filename>                  output filename\n"
            L"   -l, --to-lowercase             force output filename to lower case\n"
            L"   -y, --overwrite                overwrite existing output file (if any)\n"
            L"   -nologo                        suppress copyright message\n"
            L"\n"
            L"       (sdkmesh/sdkmesh2 only)\n"
            L"   -ib32, --index-buffer-32-bit   use 32-bit index buffer\n"
            L"   -fn <normal-format>, --normal-format <normal-format>\n"
            L"                                  format to use for writing normals/tangents/binormals\n"
            L"   -fuv <uv-format>, --uv-format <uv-format>\n"
            L"                                  format to use for texture coordinates\n"
            L"   -fc <color-format>, --color-format <color-format>\n"
            L"                                  format to use for writing colors\n"
            L"   -uv2                           place UVs into a second texture coordinate channel\n"
            L"\n"
            L"   '-- ' is needed if any input filepath starts with the '-' or '/' character\n";

        wprintf(L"%ls", s_usage);

        wprintf(L"\n   <normal-format>: ");
        PrintList(13, g_vertexNormalFormats);

        wprintf(L"\n   <uv-format>: ");
        PrintList(13, g_vertexUVFormats);

        wprintf(L"\n   <color-format>: ");
        PrintList(13, g_vertexColorFormats);
    }

    //--------------------------------------------------------------------------------------
    HRESULT __cdecl UVAtlasCallback(float fPercentDone)
    {
        static ULONGLONG s_lastTick = 0;

        const ULONGLONG tick = GetTickCount64();

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

//--------------------------------------------------------------------------------------
// Entry-point
//--------------------------------------------------------------------------------------
#ifdef __PREFAST__
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
    CHANNELS perVertex = CHANNELS::NONE;
    UVATLAS uvOptions = UVATLAS_DEFAULT;
    UVATLAS uvOptionsEx = UVATLAS_DEFAULT;
    DXGI_FORMAT normalFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    DXGI_FORMAT uvFormat = DXGI_FORMAT_R32G32_FLOAT;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t fileType = 0;

    std::wstring texFile;
    std::wstring outputFile;

    // Set locale for output since GetErrorDesc can get localized strings.
    std::locale::global(std::locale(""));

    // Initialize COM (needed for WIC)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        wprintf(L"Failed to initialize COM (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
        return 1;
    }

    // Process command line
    uint64_t dwOptions = 0;
    std::list<SConversion> conversion;
    bool allowOpts = true;

    for (int iArg = 1; iArg < argc; iArg++)
    {
        PWSTR pArg = argv[iArg];

        if (allowOpts && (('-' == pArg[0]) || ('/' == pArg[0])))
        {
            uint64_t dwOption = 0;
            PWSTR pValue = nullptr;

            if (('-' == pArg[0]) && ('-' == pArg[1]))
            {
                if (pArg[2] == 0)
                {
                    // "-- " is the POSIX standard for "end of options" marking to escape the '-' and '/' characters at the start of filepaths.
                    allowOpts = false;
                    continue;
                }
                else
                {
                    pArg += 2;

                    for (pValue = pArg; *pValue && (':' != *pValue) && ('=' != *pValue); ++pValue);

                    if (*pValue)
                        *pValue++ = 0;

                    dwOption = LookupByName(pArg, g_pOptionsLong);
                }
            }
            else
            {
                pArg++;

                for (pValue = pArg; *pValue && (':' != *pValue) && ('=' != *pValue); ++pValue);

                if (*pValue)
                    *pValue++ = 0;

                dwOption = LookupByName(pArg, g_pOptions);

                if (!dwOption)
                {
                    if (LookupByName(pArg, g_pOptionsLong))
                    {
                        wprintf(L"ERROR: did you mean `--%ls` (with two dashes)?\n", pArg);
                        return 1;
                    }
                }
            }

            switch (dwOption)
            {
            case 0:
                wprintf(L"ERROR: Unknown option: `%ls`\n\nUse %ls --help\n", pArg, g_ToolName);
                return 1;

            case OPT_QUALITY:
            case OPT_MAXCHARTS:
            case OPT_MAXSTRETCH:
            case OPT_LIMIT_MERGE_STRETCH:
            case OPT_LIMIT_FACE_STRETCH:
            case OPT_GUTTER:
            case OPT_WIDTH:
            case OPT_HEIGHT:
            case OPT_FILETYPE:
            case OPT_OUTPUTFILE:
            case OPT_FILELIST:
            case OPT_VERT_NORMAL_FORMAT:
            case OPT_VERT_UV_FORMAT:
            case OPT_VERT_COLOR_FORMAT:
            case OPT_SDKMESH:
            case OPT_SDKMESH_V2:
            case OPT_CMO:
            case OPT_VBO:
            case OPT_WAVEFRONT_OBJ:
                // These don't use flag bits
                break;

            case OPT_VERSION:
                PrintLogo(true, g_ToolName, g_Description);
                return 0;

            case OPT_HELP:
                PrintUsage();
                return 0;

            default:
                if (dwOptions & (UINT64_C(1) << dwOption))
                {
                    wprintf(L"ERROR: Duplicate option: `%ls`\n\n", pArg);
                    return 1;
                }

                dwOptions |= (UINT64_C(1) << dwOption);
                break;
            }

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
            case OPT_VERT_NORMAL_FORMAT:
            case OPT_VERT_UV_FORMAT:
            case OPT_VERT_COLOR_FORMAT:
            case OPT_FILELIST:
            case OPT_FILETYPE:
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

            default:
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

            case OPT_LIMIT_MERGE_STRETCH:
                uvOptionsEx |= UVATLAS_LIMIT_MERGE_STRETCH;
                break;

            case OPT_LIMIT_FACE_STRETCH:
                uvOptionsEx |= UVATLAS_LIMIT_FACE_STRETCH;
                break;

            case OPT_MAXCHARTS:
                if (swscanf_s(pValue, L"%zu", &maxCharts) != 1)
                {
                    wprintf(L"Invalid value specified with -n (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_MAXSTRETCH:
                if (swscanf_s(pValue, L"%f", &maxStretch) != 1 || maxStretch < 0.f || maxStretch > 1.f)
                {
                    wprintf(L"Invalid value specified with -st (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_GUTTER:
                if (swscanf_s(pValue, L"%f", &gutter) != 1 || gutter < 0.f)
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
                if (dwOptions & (UINT64_C(1) << OPT_WEIGHT_BY_EQUAL))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (UINT64_C(1) << OPT_NORMALS);
                break;

            case OPT_WEIGHT_BY_EQUAL:
                if (dwOptions & (UINT64_C(1) << OPT_WEIGHT_BY_AREA))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (UINT64_C(1) << OPT_NORMALS);
                break;

            case OPT_IMT_TEXFILE:
                if (dwOptions & (UINT64_C(1) << OPT_IMT_VERTEX))
                {
                    wprintf(L"Cannot use both if and iv at the same time\n");
                    return 1;
                }
                else
                {
                    std::filesystem::path path(pValue);
                    texFile = path.make_preferred().native();
                }
                break;

            case OPT_IMT_VERTEX:
                if (dwOptions & (UINT64_C(1) << OPT_IMT_TEXFILE))
                {
                    wprintf(L"Cannot use both if and iv at the same time\n");
                    return 1;
                }

                if (!_wcsicmp(pValue, L"COLOR"))
                {
                    perVertex = CHANNELS::COLOR;
                }
                else if (!_wcsicmp(pValue, L"NORMAL"))
                {
                    perVertex = CHANNELS::NORMAL;
                }
                else if (!_wcsicmp(pValue, L"TEXCOORD"))
                {
                    perVertex = CHANNELS::TEXCOORD;
                }
                else
                {
                    wprintf(L"Invalid value specified with -iv (%ls)\n", pValue);
                    return 1;
                }
                break;

            case OPT_OUTPUTFILE:
                {
                    std::filesystem::path path(pValue);
                    outputFile = path.make_preferred().native();
                }
                break;

            case OPT_FILETYPE:
                fileType = LookupByName(pValue, g_pMeshFileTypes);
                if (!fileType)
                {
                    wprintf(L"Invalid value specified with -ft (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_TOPOLOGICAL_ADJ:
                if (dwOptions & (UINT64_C(1) << OPT_GEOMETRIC_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_GEOMETRIC_ADJ:
                if (dwOptions & (UINT64_C(1) << OPT_TOPOLOGICAL_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_SDKMESH:
                if (fileType != 0 && fileType != CODEC_SDKMESH)
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                fileType = CODEC_SDKMESH;
                break;

            case OPT_SDKMESH_V2:
                if (fileType != 0 && fileType != CODEC_SDKMESH && fileType != CODEC_SDKMESH_V2)
                {
                    wprintf(L"-sdkmesh2 requires sdkmesh\n");
                    return 1;
                }
                fileType = CODEC_SDKMESH_V2;
                break;

            case OPT_CMO:
                if (fileType != 0 && fileType != CODEC_CMO)
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                fileType = CODEC_CMO;
                break;

            case OPT_VBO:
                if (fileType != 0 && fileType != CODEC_VBO)
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                fileType = CODEC_VBO;
                break;

            case OPT_WAVEFRONT_OBJ:
                if (fileType != 0 && fileType != CODEC_WAVEFRONT_OBJ)
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                fileType = CODEC_WAVEFRONT_OBJ;
                break;

            case OPT_SECOND_UV:
                if (fileType != CODEC_SDKMESH && fileType != CODEC_SDKMESH_V2)
                {
                    wprintf(L"-uv2 is only supported by sdkmesh\n");
                    return 1;
                }
                break;

            case OPT_VERT_NORMAL_FORMAT:
                normalFormat = LookupByName(pValue, g_vertexNormalFormats);
                if (!normalFormat)
                {
                    wprintf(L"Invalid value specified with -fn (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_VERT_UV_FORMAT:
                uvFormat = LookupByName(pValue, g_vertexUVFormats);
                if (!uvFormat)
                {
                    wprintf(L"Invalid value specified with -fuv (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_VERT_COLOR_FORMAT:
                colorFormat = LookupByName(pValue, g_vertexColorFormats);
                if (!colorFormat)
                {
                    wprintf(L"Invalid value specified with -fc (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_FILELIST:
                {
                    std::filesystem::path path(pValue);
                    std::wifstream inFile(path.make_preferred().c_str());
                    if (!inFile)
                    {
                        wprintf(L"Error opening -flist file %ls\n", pValue);
                        return 1;
                    }

                    inFile.imbue(std::locale::classic());

                    ProcessFileList(inFile, conversion);
                }
                break;

            default:
                break;
            }
        }
        else if (wcspbrk(pArg, L"?*") != nullptr)
        {
            const size_t count = conversion.size();
            std::filesystem::path path(pArg);
            SearchForFiles(path.make_preferred(), conversion, (dwOptions & (UINT64_C(1) << OPT_RECURSIVE)) != 0, nullptr);
            if (conversion.size() <= count)
            {
                wprintf(L"No matching files found for %ls\n", pArg);
                return 1;
            }
        }
        else
        {
            SConversion conv = {};
            std::filesystem::path path(pArg);
            conv.szSrc = path.make_preferred().native();
            conversion.push_back(conv);
        }
    }

    if (conversion.empty())
    {
        PrintUsage();
        return 0;
    }

    if (!outputFile.empty() && conversion.size() > 1)
    {
        wprintf(L"Cannot use -o with multiple input files\n");
        return 1;
    }

    if (~dwOptions & (UINT64_C(1) << OPT_NOLOGO))
        PrintLogo(false, g_ToolName, g_Description);

    if (!fileType)
        fileType = CODEC_SDKMESH;

    // Process files
    for (auto pConv = conversion.begin(); pConv != conversion.end(); ++pConv)
    {
        std::filesystem::path curpath(pConv->szSrc);
        const auto ext = curpath.extension();

        if (pConv != conversion.begin())
            wprintf(L"\n");

        wprintf(L"reading %ls", curpath.c_str());
        fflush(stdout);

        std::unique_ptr<Mesh> inMesh;
        std::vector<Mesh::Material> inMaterial;
        hr = E_NOTIMPL;
        if (_wcsicmp(ext.c_str(), L".vbo") == 0)
        {
            hr = Mesh::CreateFromVBO(curpath.c_str(), inMesh);
        }
        else if (_wcsicmp(ext.c_str(), L".sdkmesh") == 0)
        {
            wprintf(L"\nERROR: Importing SDKMESH files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext.c_str(), L".cmo") == 0)
        {
            wprintf(L"\nERROR: Importing Visual Studio CMO files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext.c_str(), L".x") == 0)
        {
            wprintf(L"\nERROR: Legacy Microsoft X files not supported\n");
            return 1;
        }
        else if (_wcsicmp(ext.c_str(), L".fbx") == 0)
        {
            wprintf(L"\nERROR: Autodesk FBX files not supported\n");
            return 1;
        }
        else
        {
            hr = LoadFromOBJ(curpath.c_str(), inMesh, inMaterial,
                (dwOptions & (UINT64_C(1) << OPT_CLOCKWISE)) ? false : true,
                (dwOptions & (UINT64_C(1) << OPT_NODDS)) ? false : true);
        }
        if (FAILED(hr))
        {
            wprintf(L" FAILED (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
            return 1;
        }

        size_t nVerts = inMesh->GetVertexCount();
        const size_t nFaces = inMesh->GetFaceCount();

        if (!nVerts || !nFaces)
        {
            wprintf(L"\nERROR: Invalid mesh\n");
            return 1;
        }

        assert(inMesh->GetPositionBuffer() != nullptr);
        assert(inMesh->GetIndexBuffer() != nullptr);

        wprintf(L"\n%zu vertices, %zu faces", nVerts, nFaces);

        if (dwOptions & (UINT64_C(1) << OPT_FLIPU))
        {
            hr = inMesh->InvertUTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting u texcoord (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        if (dwOptions & (UINT64_C(1) << OPT_FLIPV))
        {
            hr = inMesh->InvertVTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting v texcoord (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        if (dwOptions & (UINT64_C(1) << OPT_FLIPZ))
        {
            hr = inMesh->ReverseHandedness();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing handedness (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        // Prepare mesh for processing
        const size_t nVertsOriginal = nVerts;
        std::vector<uint32_t> dups;
        {
            // Adjacency
            const float epsilon = (dwOptions & (UINT64_C(1) << OPT_GEOMETRIC_ADJ)) ? 1e-5f : 0.f;

            hr = inMesh->GenerateAdjacency(epsilon);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed generating adjacency (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }

            // Validation
            std::wstring msgs;
            hr = inMesh->Validate(VALIDATE_BACKFACING | VALIDATE_BOWTIES, &msgs);
            if (!msgs.empty())
            {
                wprintf(L"\nWARNING: \n");
                wprintf(L"%ls", msgs.c_str());
            }

            // Clean
            hr = inMesh->Clean(dups, true);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed mesh clean (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
            else
            {
                nVerts = inMesh->GetVertexCount();
                if (nVerts != nVertsOriginal)
                {
                    wprintf(L" [%zu vertex dups] ", nVerts - nVertsOriginal);
                }
            }
        }

        if (!inMesh->GetNormalBuffer())
        {
            dwOptions |= UINT64_C(1) << OPT_NORMALS;
        }

        if (!inMesh->GetTangentBuffer() && (fileType == CODEC_CMO))
        {
            dwOptions |= UINT64_C(1) << OPT_TANGENTS;
        }

        // Compute vertex normals from faces
        if ((dwOptions & (UINT64_C(1) << OPT_NORMALS))
            || ((dwOptions & ((UINT64_C(1) << OPT_TANGENTS) | (UINT64_C(1) << OPT_CTF))) && !inMesh->GetNormalBuffer()))
        {
            CNORM_FLAGS flags = CNORM_DEFAULT;

            if (dwOptions & (UINT64_C(1) << OPT_WEIGHT_BY_EQUAL))
            {
                flags |= CNORM_WEIGHT_EQUAL;
            }
            else if (dwOptions & (UINT64_C(1) << OPT_WEIGHT_BY_AREA))
            {
                flags |= CNORM_WEIGHT_BY_AREA;
            }

            if (dwOptions & (UINT64_C(1) << OPT_CLOCKWISE))
            {
                flags |= CNORM_WIND_CW;
            }

            hr = inMesh->ComputeNormals(flags);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing normals (flags:%X, %08X%ls)\n",
                    static_cast<unsigned int>(flags),
                    static_cast<unsigned int>(hr),
                    GetErrorDesc(hr));
                return 1;
            }
        }

        // Compute tangents and bitangents
        if (dwOptions & ((UINT64_C(1) << OPT_TANGENTS) | (UINT64_C(1) << OPT_CTF)))
        {
            if (!inMesh->GetTexCoordBuffer())
            {
                wprintf(L"\nERROR: Computing tangents/bi-tangents requires texture coordinates\n");
                return 1;
            }

            hr = inMesh->ComputeTangentFrame((dwOptions & (UINT64_C(1) << OPT_CTF)) ? true : false);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing tangent frame (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        // Compute IMT
        std::unique_ptr<float[]> IMTData;
        if (dwOptions & ((UINT64_C(1) << OPT_IMT_TEXFILE) | (UINT64_C(1) << OPT_IMT_VERTEX)))
        {
            if (dwOptions & (UINT64_C(1) << OPT_IMT_TEXFILE))
            {
                if (!inMesh->GetTexCoordBuffer())
                {
                    wprintf(L"\nERROR: Computing IMT from texture requires texture coordinates\n");
                    return 1;
                }

                std::filesystem::path tname(texFile);
                const auto txext = tname.extension();

                ScratchImage iimage;

                if (_wcsicmp(txext.c_str(), L".dds") == 0)
                {
                    hr = LoadFromDDSFile(texFile.c_str(), DDS_FLAGS_NONE, nullptr, iimage);
                }
                else if (_wcsicmp(ext.c_str(), L".tga") == 0)
                {
                    hr = LoadFromTGAFile(texFile.c_str(), nullptr, iimage);
                }
                else if (_wcsicmp(ext.c_str(), L".hdr") == 0)
                {
                    hr = LoadFromHDRFile(texFile.c_str(), nullptr, iimage);
                }
            #ifdef USE_OPENEXR
                else if (_wcsicmp(ext.c_str(), L".exr") == 0)
                {
                    hr = LoadFromEXRFile(texFile.c_str(), nullptr, iimage);
                }
            #endif
                else
                {
                    hr = LoadFromWICFile(texFile.c_str(), WIC_FLAGS_NONE, nullptr, iimage);
                }
                if (FAILED(hr))
                {
                    wprintf(L"\nWARNING: Failed to load texture for IMT (%08X%ls):\n%ls\n",
                        static_cast<unsigned int>(hr), GetErrorDesc(hr), texFile.c_str());
                }
                else
                {
                    const Image *img = iimage.GetImage(0, 0, 0);

                    ScratchImage floatImage;
                    if (img->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
                    {
                        hr = Convert(*iimage.GetImage(0, 0, 0), DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT,
                            TEX_THRESHOLD_DEFAULT, floatImage);
                        if (FAILED(hr))
                        {
                            img = nullptr;
                            wprintf(L"\nWARNING: Failed converting texture for IMT (%08X%ls):\n%ls\n",
                                static_cast<unsigned int>(hr), GetErrorDesc(hr), texFile.c_str());
                        }
                        else
                        {
                            img = floatImage.GetImage(0, 0, 0);
                        }
                    }

                    if (img)
                    {
                        wprintf(L"\nComputing IMT from file %ls...\n", texFile.c_str());
                        IMTData.reset(new (std::nothrow) float[nFaces * 3]);
                        if (!IMTData)
                        {
                            wprintf(L"\nERROR: out of memory\n");
                            return 1;
                        }

                        hr = UVAtlasComputeIMTFromTexture(inMesh->GetPositionBuffer(), inMesh->GetTexCoordBuffer(), nVerts,
                            inMesh->GetIndexBuffer(), DXGI_FORMAT_R32_UINT, nFaces,
                            reinterpret_cast<const float *>(img->pixels), img->width, img->height,
                            UVATLAS_IMT_DEFAULT, UVAtlasCallback, IMTData.get());
                        if (FAILED(hr))
                        {
                            IMTData.reset();
                            wprintf(L"WARNING: Failed to compute IMT from texture (%08X%ls):\n%ls\n",
                                static_cast<unsigned int>(hr), GetErrorDesc(hr), texFile.c_str());
                        }
                    }
                }
            }
            else
            {
                const wchar_t *szChannel = L"*unknown*";
                const float *pSignal = nullptr;
                size_t signalDim = 0;
                size_t signalStride = 0;
                switch (perVertex)
                {
                case CHANNELS::NORMAL:
                    szChannel = L"normals";
                    if (inMesh->GetNormalBuffer())
                    {
                        pSignal = reinterpret_cast<const float *>(inMesh->GetNormalBuffer());
                        signalDim = 3;
                        signalStride = sizeof(XMFLOAT3);
                    }
                    break;

                case CHANNELS::COLOR:
                    szChannel = L"vertex colors";
                    if (inMesh->GetColorBuffer())
                    {
                        pSignal = reinterpret_cast<const float *>(inMesh->GetColorBuffer());
                        signalDim = 4;
                        signalStride = sizeof(XMFLOAT4);
                    }
                    break;

                case CHANNELS::TEXCOORD:
                    szChannel = L"texture coordinates";
                    if (inMesh->GetTexCoordBuffer())
                    {
                        pSignal = reinterpret_cast<const float *>(inMesh->GetTexCoordBuffer());
                        signalDim = 2;
                        signalStride = sizeof(XMFLOAT2);
                    }
                    break;

                default:
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
                        wprintf(L"WARNING: Failed to compute IMT from channel %ls (%08X%ls)\n",
                            szChannel, static_cast<unsigned int>(hr), GetErrorDesc(hr));
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
            uvOptions | uvOptionsEx, vb, ib,
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
                wprintf(L"\nERROR: Failed creating isocharts (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        wprintf(L"Output # of charts: %zu, resulting stretching %f, %zu verts\n", outCharts, double(outStretch), vb.size());

        assert((ib.size() / sizeof(uint32_t)) == (nFaces * 3));
        assert(facePartitioning.size() == nFaces);
        assert(vertexRemapArray.size() == vb.size());

        hr = inMesh->UpdateFaces(nFaces, reinterpret_cast<const uint32_t *>(ib.data()));
        if (FAILED(hr))
        {
            wprintf(L"\nERROR: Failed applying atlas indices (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
            return 1;
        }

        hr = inMesh->VertexRemap(vertexRemapArray.data(), vertexRemapArray.size());
        if (FAILED(hr))
        {
            wprintf(L"\nERROR: Failed applying atlas vertex remap (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
            return 1;
        }

        nVerts = vb.size();

    #ifdef _DEBUG
        std::wstring msgs;
        hr = inMesh->Validate(VALIDATE_DEFAULT, &msgs);
        if (!msgs.empty())
        {
            wprintf(L"\nWARNING: \n%ls\n", msgs.c_str());
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

            hr = inMesh->UpdateUVs(nVerts, texcoord.get(), (dwOptions & (UINT64_C(1) << OPT_SECOND_UV)));
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to update with isochart UVs\n");
                return 1;
            }
        }

        if (dwOptions & (UINT64_C(1) << OPT_COLOR_MESH))
        {
            inMaterial.clear();
            inMaterial.reserve(std::size(g_ColorList));

            for (size_t j = 0; j < std::size(g_ColorList) && (j < outCharts); ++j)
            {
                Mesh::Material mtl = {};

                wchar_t matname[32] = {};
                swprintf_s(matname, L"Chart%02zu", j + 1);
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
                attr[j] = *it % std::size(g_ColorList);
            }

            hr = inMesh->UpdateAttributes(nFaces, attr.get());
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed applying atlas attributes (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        if (dwOptions & (UINT64_C(1) << OPT_FLIP))
        {
            hr = inMesh->ReverseWinding();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing winding (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        // Write results
        wprintf(L"\n\t->\n");

        wchar_t outputExt[_MAX_EXT] = {};

        if (!outputFile.empty())
        {
            std::filesystem::path npath(outputFile);
            wcscpy_s(outputExt, npath.extension().c_str());
        }
        else
        {
            switch (fileType)
            {
            case CODEC_VBO:
                wcscpy_s(outputExt, L".vbo");
                break;

            case CODEC_CMO:
                wcscpy_s(outputExt, L".cmo");
                break;

            case CODEC_WAVEFRONT_OBJ:
                wcscpy_s(outputExt, L".obj");
                break;

            default:
                wcscpy_s(outputExt, L".sdkmesh");
                break;
            }

            outputFile.assign(curpath.stem());
            outputFile.append(outputExt);
        }

        if (dwOptions & (UINT64_C(1) << OPT_TOLOWER))
        {
            std::transform(outputFile.begin(), outputFile.end(), outputFile.begin(), towlower);
        }

        if (~dwOptions & (UINT64_C(1) << OPT_OVERWRITE))
        {
            if (GetFileAttributesW(outputFile.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                wprintf(L"\nERROR: Output file already exists, use -y to overwrite:\n'%ls'\n", outputFile.c_str());
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

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (UINT64_C(1) << OPT_FORCE_32BIT_IB)))
            {
                wprintf(L"\nERROR: VBO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToVBO(outputFile.c_str());
        }
        else if (!_wcsicmp(outputExt, L".sdkmesh"))
        {
            hr = inMesh->ExportToSDKMESH(
                outputFile.c_str(),
                inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data(),
                (dwOptions & (UINT64_C(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                (fileType == CODEC_SDKMESH_V2) ? true : false,
                normalFormat,
                uvFormat,
                colorFormat);
        }
        else if (!_wcsicmp(outputExt, L".cmo"))
        {
            if (!inMesh->GetNormalBuffer() || !inMesh->GetTexCoordBuffer() || !inMesh->GetTangentBuffer())
            {
                wprintf(L"\nERROR: Visual Studio CMO requires position, normal, tangents, and texcoord\n");
                return 1;
            }

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (UINT64_C(1) << OPT_FORCE_32BIT_IB)))
            {
                wprintf(L"\nERROR: Visual Studio CMO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToCMO(outputFile.c_str(), inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
        }
        else if (!_wcsicmp(outputExt, L".obj") || !_wcsicmp(outputExt, L"._obj"))
        {
            std::wstring mtlFilename;
            if ((dwOptions & (UINT64_C(1) << OPT_COLOR_MESH)) && !inMaterial.empty())
            {
                mtlFilename = curpath.stem().native();
                mtlFilename.append(L"_charts");

                if (dwOptions & (UINT64_C(1) << OPT_TOLOWER))
                {
                    std::transform(mtlFilename.begin(), mtlFilename.end(), mtlFilename.begin(), towlower);
                }

                inMesh->SetMTLFileName(mtlFilename);
            }

            hr = inMesh->ExportToOBJ(outputFile.c_str(), inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());

            if (!mtlFilename.empty())
            {
                std::filesystem::path mtlOutputPath(outputFile);
                mtlOutputPath = mtlOutputPath.parent_path();
                mtlOutputPath.append(mtlFilename);
                mtlOutputPath.concat(L".mtl");

                if (~dwOptions & (UINT64_C(1) << OPT_OVERWRITE))
                {
                    if (GetFileAttributesW(mtlOutputPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        wprintf(L"\nERROR: charts mtl file already exists, use -y to overwrite:\n'%ls'\n", mtlOutputPath.c_str());
                        return 1;
                    }
                }

                std::wofstream os;
                os.open(mtlOutputPath.c_str());
                if (!os)
                {
                    wprintf(L"\nERROR: Failed to create charts mtl file\n");
                    return 1;
                }

                os.imbue(std::locale::classic());

                for (const auto &mtl : inMaterial)
                {
                    // Minimal material output.
                    os << L"newmtl " << mtl.name << std::endl;
                    os << L"illum 1" << std::endl;
                    os << L"Ka " << mtl.ambientColor.x << L" " << mtl.ambientColor.y << L" " << mtl.ambientColor.z << std::endl;
                    os << L"Kd " << mtl.diffuseColor.x << L" " << mtl.diffuseColor.y << L" " << mtl.diffuseColor.z << std::endl
                        << std::endl;
                }

                os.close();

                if (os.bad())
                {
                    wprintf(L"\nERROR: Failed to write charts mtl file\n");
                    return 1;
                }
            }
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
            wprintf(L"\nERROR: Failed write (%08X%ls):-> '%ls'\n",
                static_cast<unsigned int>(hr), GetErrorDesc(hr), outputFile.c_str());
            return 1;
        }

        wprintf(L" %zu vertices, %zu faces written:\n'%ls'\n", nVerts, nFaces, outputFile.c_str());

        // Write out vertex remapping from original mesh
        if (dwOptions & (UINT64_C(1) << OPT_OUTPUT_REMAPPING))
        {
            std::wstring mapFilename;
            mapFilename = curpath.stem().native();
            mapFilename.append(L"_map");

            if (dwOptions & (UINT64_C(1) << OPT_TOLOWER))
            {
                std::transform(mapFilename.begin(), mapFilename.end(), mapFilename.begin(), towlower);
            }

            std::filesystem::path mapOutputPath(outputFile);
            mapOutputPath = mapOutputPath.parent_path().append(mapFilename);
            mapOutputPath.concat(L".txt");

            if (~dwOptions & (UINT64_C(1) << OPT_OVERWRITE))
            {
                if (GetFileAttributesW(mapOutputPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    wprintf(L"\nERROR: vertex remapping file already exists, use -y to overwrite:\n'%ls'\n", mapOutputPath.c_str());
                    return 1;
                }
            }

            std::wofstream os;
            os.open(mapOutputPath.c_str());
            if (!os)
            {
                wprintf(L"\nERROR: Failed to create vertex remapping file\n");
                return 1;
            }

            os.imbue(std::locale::classic());

            for (size_t j = 0; j < nVerts; ++j)
            {
                uint32_t oldIndex = vertexRemapArray[j];
                if (oldIndex == uint32_t(-1))
                    continue;

                if (oldIndex >= nVertsOriginal)
                {
                    oldIndex = dups[oldIndex - nVertsOriginal];
                }

                os << j << L"," << oldIndex << std::endl;
            }

            os.close();

            if (os.bad())
            {
                wprintf(L"\nERROR: Failed to write vertex remapping file\n");
                return 1;
            }
        }

        // Write out UV mesh visualization
        if (dwOptions & (UINT64_C(1) << OPT_UV_MESH))
        {
            const bool vizNormals = (dwOptions & (UINT64_C(1) << OPT_VIZ_NORMALS)) != 0;
            const bool secondUVs = (dwOptions & (UINT64_C(1) << OPT_SECOND_UV)) != 0;
            hr = inMesh->VisualizeUVs(secondUVs, vizNormals);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to create UV visualization mesh\n");
                return 1;
            }

            std::wstring uvFilename;
            uvFilename = curpath.stem().native();
            uvFilename.append(L"_texture");

            if (dwOptions & (UINT64_C(1) << OPT_TOLOWER))
            {
                std::transform(uvFilename.begin(), uvFilename.end(), uvFilename.begin(), towlower);
            }

            std::filesystem::path uvOutputPath(outputFile);
            uvOutputPath = uvOutputPath.parent_path().append(uvFilename);
            uvOutputPath.concat(outputExt);

            if (~dwOptions & (UINT64_C(1) << OPT_OVERWRITE))
            {
                if (GetFileAttributesW(uvOutputPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    wprintf(L"\nERROR: UV visualization mesh file already exists, use -y to overwrite:\n'%ls'\n", uvOutputPath.c_str());
                    return 1;
                }
            }

            hr = E_NOTIMPL;
            if (!_wcsicmp(outputExt, L".vbo"))
            {
                hr = inMesh->ExportToVBO(uvOutputPath.c_str());
            }
            else if (!_wcsicmp(outputExt, L".sdkmesh"))
            {
                hr = inMesh->ExportToSDKMESH(
                    uvOutputPath.c_str(),
                    inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data(),
                    (dwOptions & (UINT64_C(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                    (fileType == CODEC_SDKMESH_V2) ? true : false,
                    normalFormat,
                    uvFormat,
                    colorFormat);
            }
            else if (!_wcsicmp(outputExt, L".cmo"))
            {
                hr = inMesh->ExportToCMO(uvOutputPath.c_str(), inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
            }
            else if (!_wcsicmp(outputExt, L".obj") || !_wcsicmp(outputExt, L"._obj"))
            {
                if (secondUVs)
                {
                    wprintf(L"\nWARNING: WaveFront Object (.obj) not supported for UV visualization with uv2\n");
                }
                else if (vizNormals)
                {
                    wprintf(L"\nWARNING: WaveFront Object (.obj) not supported for UV visualization with vn (requires Vertex Colors)\n");
                }
                else
                {
                    hr = inMesh->ExportToOBJ(uvOutputPath.c_str(), inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
                }
            }
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed uv mesh write (%08X%ls):-> '%ls'\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr), uvOutputPath.c_str());
                return 1;
            }
            wprintf(L"uv mesh visualization '%ls'\n", uvOutputPath.c_str());
        }
    }

    return 0;
}
