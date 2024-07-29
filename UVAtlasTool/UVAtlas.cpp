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

//Uncomment to add support for OpenEXR (.exr)
//#define USE_OPENEXR

#ifdef USE_OPENEXR
// See <https://github.com/Microsoft/DirectXTex/wiki/Adding-OpenEXR> for details
#include "DirectXTexEXR.h"
#endif

using namespace DirectX;

namespace
{
    enum OPTIONS : uint64_t
    {
        OPT_RECURSIVE = 1,
        OPT_QUALITY,
        OPT_MAXCHARTS,
        OPT_MAXSTRETCH,
        OPT_LIMIT_MERGE_STRETCH,
        OPT_LIMIT_FACE_STRETCH,
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
        OPT_OUTPUTFILE,
        OPT_TOLOWER,
        OPT_SDKMESH,
        OPT_SDKMESH_V2,
        OPT_CMO,
        OPT_VBO,
        OPT_WAVEFRONT_OBJ,
        OPT_CLOCKWISE,
        OPT_FORCE_32BIT_IB,
        OPT_OVERWRITE,
        OPT_NODDS,
        OPT_FLIP,
        OPT_FLIPU,
        OPT_FLIPV,
        OPT_FLIPZ,
        OPT_VERT_NORMAL_FORMAT,
        OPT_VERT_UV_FORMAT,
        OPT_VERT_COLOR_FORMAT,
        OPT_SECOND_UV,
        OPT_VIZ_NORMALS,
        OPT_OUTPUT_REMAPPING,
        OPT_NOLOGO,
        OPT_FILELIST,
        OPT_MAX
    };

    static_assert(OPT_MAX <= 64, "dwOptions is a unsigned int bitfield");

    enum CHANNELS
    {
        CHANNEL_NONE = 0,
        CHANNEL_NORMAL,
        CHANNEL_COLOR,
        CHANNEL_TEXCOORD,
    };

    struct SConversion
    {
        std::wstring szSrc;
    };

    template<typename T>
    struct SValue
    {
        const wchar_t*  name;
        T               value;
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
        { L"sdkmesh",   OPT_SDKMESH },
        { L"sdkmesh2",  OPT_SDKMESH_V2 },
        { L"cmo",       OPT_CMO },
        { L"vbo",       OPT_VBO },
        { L"wf",        OPT_WAVEFRONT_OBJ },
        { L"cw",        OPT_CLOCKWISE },
        { L"ib32",      OPT_FORCE_32BIT_IB },
        { L"y",         OPT_OVERWRITE },
        { L"nodds",     OPT_NODDS },
        { L"flip",      OPT_FLIP },
        { L"flipu",     OPT_FLIPU },
        { L"flipv",     OPT_FLIPV },
        { L"flipz",     OPT_FLIPZ },
        { L"fn",        OPT_VERT_NORMAL_FORMAT },
        { L"fuv",       OPT_VERT_UV_FORMAT },
        { L"fc",        OPT_VERT_COLOR_FORMAT },
        { L"uv2",       OPT_SECOND_UV },
        { L"vn",        OPT_VIZ_NORMALS },
        { L"m",         OPT_OUTPUT_REMAPPING },
        { L"nologo",    OPT_NOLOGO },
        { L"flist",     OPT_FILELIST },
        { nullptr,      0 }
    };

    const SValue<uint32_t> g_vertexNormalFormats[] =
    {
        { L"float3",    DXGI_FORMAT_R32G32B32_FLOAT },
        { L"float16_4", DXGI_FORMAT_R16G16B16A16_FLOAT },
        { L"r11g11b10", DXGI_FORMAT_R11G11B10_FLOAT },
        { nullptr,      0 }
    };

    const SValue<uint32_t> g_vertexUVFormats[] =
    {
        { L"float2",    DXGI_FORMAT_R32G32_FLOAT },
        { L"float16_2", DXGI_FORMAT_R16G16_FLOAT },
        { nullptr,      0 }
    };

    const SValue<uint32_t> g_vertexColorFormats[] =
    {
        { L"bgra",      DXGI_FORMAT_B8G8R8A8_UNORM },
        { L"rgba",      DXGI_FORMAT_R8G8B8A8_UNORM },
        { L"float4",    DXGI_FORMAT_R32G32B32A32_FLOAT },
        { L"float16_4", DXGI_FORMAT_R16G16B16A16_FLOAT },
        { L"rgba_10",   DXGI_FORMAT_R10G10B10A2_UNORM },
        { L"r11g11b10", DXGI_FORMAT_R11G11B10_FLOAT },
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
    inline HANDLE safe_handle(HANDLE h) noexcept { return (h == INVALID_HANDLE_VALUE) ? nullptr : h; }

    struct find_closer { void operator()(HANDLE h) noexcept { assert(h != INVALID_HANDLE_VALUE); if (h) FindClose(h); } };

    using ScopedFindHandle = std::unique_ptr<void, find_closer>;

#ifdef __PREFAST__
#pragma prefast(disable : 26018, "Only used with static internal arrays")
#endif

    template<typename T>
    T LookupByName(const wchar_t *pName, const SValue<T> *pArray)
    {
        while (pArray->name)
        {
            if (!_wcsicmp(pName, pArray->name))
                return pArray->value;

            pArray++;
        }

        return 0;
    }

    void SearchForFiles(const std::filesystem::path& path, std::list<SConversion>& files, bool recursive)
    {
        // Process files
        WIN32_FIND_DATAW findData = {};
        ScopedFindHandle hFile(safe_handle(FindFirstFileExW(path.c_str(),
            FindExInfoBasic, &findData,
            FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH)));
        if (hFile)
        {
            for (;;)
            {
                if (!(findData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)))
                {
                    SConversion conv = {};
                    conv.szSrc = path.parent_path().append(findData.cFileName).native();
                    files.push_back(conv);
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }

        // Process directories
        if (recursive)
        {
            auto searchDir = path.parent_path().append(L"*");

            hFile.reset(safe_handle(FindFirstFileExW(searchDir.c_str(),
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
                        auto subdir = path.parent_path().append(findData.cFileName).append(path.filename().c_str());

                        SearchForFiles(subdir, files, recursive);
                    }
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }
    }

    void ProcessFileList(std::wifstream& inFile, std::list<SConversion>& files)
    {
        std::list<SConversion> flist;
        std::set<std::wstring> excludes;

        for (;;)
        {
            std::wstring fname;
            std::getline(inFile, fname);
            if (!inFile)
                break;

            if (fname[0] == L'#')
            {
                // Comment
            }
            else if (fname[0] == L'-')
            {
                if (flist.empty())
                {
                    wprintf(L"WARNING: Ignoring the line '%ls' in -flist\n", fname.c_str());
                }
                else
                {
                    std::filesystem::path path(fname.c_str() + 1);
                    auto& npath = path.make_preferred();
                    if (wcspbrk(fname.c_str(), L"?*") != nullptr)
                    {
                        std::list<SConversion> removeFiles;
                        SearchForFiles(npath, removeFiles, false);

                        for (auto& it : removeFiles)
                        {
                            std::wstring name = it.szSrc;
                            std::transform(name.begin(), name.end(), name.begin(), towlower);
                            excludes.insert(name);
                        }
                    }
                    else
                    {
                        std::wstring name = npath.c_str();
                        std::transform(name.begin(), name.end(), name.begin(), towlower);
                        excludes.insert(name);
                    }
                }
            }
            else if (wcspbrk(fname.c_str(), L"?*") != nullptr)
            {
                std::filesystem::path path(fname.c_str());
                SearchForFiles(path.make_preferred(), flist, false);
            }
            else
            {
                SConversion conv = {};
                std::filesystem::path path(fname.c_str());
                conv.szSrc = path.make_preferred().native();
                flist.push_back(conv);
            }
        }

        inFile.close();

        if (!excludes.empty())
        {
            // Remove any excluded files
            for (auto it = flist.begin(); it != flist.end();)
            {
                std::wstring name = it->szSrc;
                std::transform(name.begin(), name.end(), name.begin(), towlower);
                auto item = it;
                ++it;
                if (excludes.find(name) != excludes.end())
                {
                    flist.erase(item);
                }
            }
        }

        if (flist.empty())
        {
            wprintf(L"WARNING: No file names found in -flist\n");
        }
        else
        {
            files.splice(files.end(), flist);
        }
    }

    void PrintList(size_t cch, const SValue<uint32_t>* pValue)
    {
        while (pValue->name)
        {
            const size_t cchName = wcslen(pValue->name);

            if (cch + cchName + 2 >= 80)
            {
                wprintf(L"\n      ");
                cch = 6;
            }

            wprintf(L"%ls ", pValue->name);
            cch += cchName + 2;
            pValue++;
        }

        wprintf(L"\n");
    }

    void PrintLogo(bool versionOnly)
    {
        wchar_t version[32] = {};

        wchar_t appName[_MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, appName, _MAX_PATH))
        {
            const DWORD size = GetFileVersionInfoSizeW(appName, nullptr);
            if (size > 0)
            {
                auto verInfo = std::make_unique<uint8_t[]>(size);
                if (GetFileVersionInfoW(appName, 0, size, verInfo.get()))
                {
                    LPVOID lpstr = nullptr;
                    UINT strLen = 0;
                    if (VerQueryValueW(verInfo.get(), L"\\StringFileInfo\\040904B0\\ProductVersion", &lpstr, &strLen))
                    {
                        wcsncpy_s(version, reinterpret_cast<const wchar_t*>(lpstr), strLen);
                    }
                }
            }
        }

        if (!*version || wcscmp(version, L"1.0.0.0") == 0)
        {
            swprintf_s(version, L"%03d (library)", UVATLAS_VERSION);
        }

        if (versionOnly)
        {
            wprintf(L"uvatlastool version %ls\n", version);
        }
        else
        {
            wprintf(L"Microsoft (R) UVAtlas Command-line Tool Version %ls\n", version);
            wprintf(L"Copyright (C) Microsoft Corp.\n");
        #ifdef _DEBUG
            wprintf(L"*** Debug build ***\n");
        #endif
            wprintf(L"\n");
        }
    }

    void PrintUsage()
    {
        PrintLogo(false);

        static const wchar_t* const s_usage =
            L"Usage: uvatlas <options> [--] <files>\n"
            L"\n"
            L"   Input file type must be Wavefront Object (.obj)\n"
            L"\n"
            L"   Output file type:\n"
            L"       -sdkmesh        DirectX SDK .sdkmesh format (default)\n"
            L"       -sdkmesh2       .sdkmesh format version 2 (PBR materials)\n"
            L"       -cmo            Visual Studio Content Pipeline .cmo format\n"
            L"       -vbo            Vertex Buffer Object (.vbo) format\n"
            L"       -wf             WaveFront Object (.obj) format\n"
            L"\n"
            L"   -r                  wildcard filename search is recursive\n"
            L"   -q <level>          sets quality level to DEFAULT, FAST or QUALITY\n"
            L"   -n <number>         maximum number of charts to generate (def: 0)\n"
            L"   -st <float>         maximum amount of stretch 0.0 to 1.0 (def: 0.16667)\n"
            L"   -lms                enable limit merge stretch option\n"
            L"   -lfs                enable limit face stretch option\n"
            L"   -g <float>          the gutter width betwen charts in texels (def: 2.0)\n"
            L"   -w <number>         texture width (def: 512)\n"
            L"   -h <number>         texture height (def: 512)\n"
            L"   -ta | -ga           generate topological vs. geometric adjancecy (def: ta)\n"
            L"   -nn | -na | -ne     generate normals weighted by angle/area/equal\n"
            L"   -tt                 generate tangents\n"
            L"   -tb                 generate tangents & bi-tangents\n"
            L"   -cw                 faces are clockwise (defaults to counter-clockwise)\n"
            L"   -c                  generate mesh with colors showing charts\n"
            L"   -t                  generates a separate mesh with uvs - (*_texture)\n"
            L"   -vn                 with -t creates per vertex colors from normals\n"
            L"   -m                  generates a text file with vertex remapping (*_map)\n"
            L"   -it <filename>      calculate IMT for the mesh using this texture map\n"
            L"   -iv <channel>       calculate IMT using per-vertex data\n"
            L"                       NORMAL, COLOR, TEXCOORD\n"
            L"   -nodds              prevents extension renaming in exported materials\n"
            L"   -flip               reverse winding of faces\n"
            L"   -flipu              inverts the u texcoords\n"
            L"   -flipv              inverts the v texcoords\n"
            L"   -flipz              flips the handedness of the positions/normals\n"
            L"   -o <filename>       output filename\n"
            L"   -l                  force output filename to lower case\n"
            L"   -y                  overwrite existing output file (if any)\n"
            L"   -nologo             suppress copyright message\n"
            L"   -flist <filename>   use text file with a list of input files (one per line)\n"
            L"\n"
            L"       (sdkmesh/sdkmesh2 only)\n"
            L"   -ib32               use 32-bit index buffer\n"
            L"   -fn <normal-format> format to use for writing normals/tangents/normals\n"
            L"   -fuv <uv-format>    format to use for texture coordinates\n"
            L"   -fc <color-format>  format to use for writing colors\n"
            L"   -uv2                place UVs into a second texture coordinate channel\n"
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

    const wchar_t* GetErrorDesc(HRESULT hr)
    {
        static wchar_t desc[1024] = {};

        LPWSTR errorText = nullptr;

        const DWORD result = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
            nullptr, static_cast<DWORD>(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&errorText), 0, nullptr);

        *desc = 0;

        if (result > 0 && errorText)
        {
            swprintf_s(desc, L": %ls", errorText);

            size_t len = wcslen(desc);
            if (len >= 1)
            {
                desc[len - 1] = 0;
            }

            if (errorText)
                LocalFree(errorText);

            for (wchar_t* ptr = desc; *ptr != 0; ++ptr)
            {
                if (*ptr == L'\r' || *ptr == L'\n')
                {
                    *ptr = L' ';
                }
            }
        }

        return desc;
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
    CHANNELS perVertex = CHANNEL_NONE;
    UVATLAS uvOptions = UVATLAS_DEFAULT;
    UVATLAS uvOptionsEx = UVATLAS_DEFAULT;
    DXGI_FORMAT normalFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    DXGI_FORMAT uvFormat = DXGI_FORMAT_R32G32_FLOAT;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

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

        if (allowOpts
            && ('-' == pArg[0]) && ('-' == pArg[1]))
        {
            if (pArg[2] == 0)
            {
                // "-- " is the POSIX standard for "end of options" marking to escape the '-' and '/' characters at the start of filepaths.
                allowOpts = false;
            }
            else if (!_wcsicmp(pArg, L"--version"))
            {
                PrintLogo(true);
                return 0;
            }
            else if (!_wcsicmp(pArg, L"--help"))
            {
                PrintUsage();
                return 0;
            }
            else
            {
                wprintf(L"Unknown option: %ls\n", pArg);
                return 1;
            }
        }
        else if (allowOpts
            && (('-' == pArg[0]) || ('/' == pArg[0])))
        {
            pArg++;
            PWSTR pValue;

            for (pValue = pArg; *pValue && (':' != *pValue); pValue++);

            if (*pValue)
                *pValue++ = 0;

            const uint64_t dwOption = LookupByName(pArg, g_pOptions);

            if (!dwOption || (dwOptions & (uint64_t(1) << dwOption)))
            {
                wprintf(L"ERROR: unknown command-line option '%ls'\n\n", pArg);
                PrintUsage();
                return 1;
            }

            dwOptions |= (uint64_t(1) << dwOption);

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
                if (dwOptions & (uint64_t(1) << OPT_WEIGHT_BY_EQUAL))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (uint64_t(1) << OPT_NORMALS);
                break;

            case OPT_WEIGHT_BY_EQUAL:
                if (dwOptions & (uint64_t(1) << OPT_WEIGHT_BY_AREA))
                {
                    wprintf(L"Can only use one of nn, na, or ne\n");
                    return 1;
                }
                dwOptions |= (uint64_t(1) << OPT_NORMALS);
                break;

            case OPT_IMT_TEXFILE:
                if (dwOptions & (uint64_t(1) << OPT_IMT_VERTEX))
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
                if (dwOptions & (uint64_t(1) << OPT_IMT_TEXFILE))
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
                {
                    std::filesystem::path path(pValue);
                    outputFile = path.make_preferred().native();
                }
                break;

            case OPT_TOPOLOGICAL_ADJ:
                if (dwOptions & (uint64_t(1) << OPT_GEOMETRIC_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_GEOMETRIC_ADJ:
                if (dwOptions & (uint64_t(1) << OPT_TOPOLOGICAL_ADJ))
                {
                    wprintf(L"Cannot use both ta and ga at the same time\n");
                    return 1;
                }
                break;

            case OPT_SDKMESH:
            case OPT_SDKMESH_V2:
                if (dwOptions & ((uint64_t(1) << OPT_VBO) | (uint64_t(1) << OPT_CMO) | (uint64_t(1) << OPT_WAVEFRONT_OBJ)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                if (dwOption == OPT_SDKMESH_V2)
                {
                    dwOptions |= (uint64_t(1) << OPT_SDKMESH);
                }
                break;

            case OPT_CMO:
                if (dwOptions & (uint64_t(1) << OPT_SECOND_UV))
                {
                    wprintf(L"-uv2 is not supported by CMO\n");
                    return 1;
                }
                if (dwOptions & ((uint64_t(1) << OPT_VBO) | (uint64_t(1) << OPT_SDKMESH) | (uint64_t(1) << OPT_WAVEFRONT_OBJ)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                break;

            case OPT_VBO:
                if (dwOptions & (uint64_t(1) << OPT_SECOND_UV))
                {
                    wprintf(L"-uv2 is not supported by VBO\n");
                    return 1;
                }
                if (dwOptions & ((uint64_t(1) << OPT_SDKMESH) | (uint64_t(1) << OPT_CMO) | (uint64_t(1) << OPT_WAVEFRONT_OBJ)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                break;

            case OPT_WAVEFRONT_OBJ:
                if (dwOptions & (uint64_t(1) << OPT_SECOND_UV))
                {
                    wprintf(L"-uv2 is not supported by Wavefront OBJ\n");
                    return 1;
                }
                if (dwOptions & ((uint64_t(1) << OPT_VBO) | (uint64_t(1) << OPT_SDKMESH) | (uint64_t(1) << OPT_CMO)))
                {
                    wprintf(L"Can only use one of sdkmesh, cmo, vbo, or wf\n");
                    return 1;
                }
                break;

            case OPT_SECOND_UV:
                if (dwOptions & ((uint64_t(1) << OPT_VBO) | (uint64_t(1) << OPT_CMO) | (uint64_t(1) << OPT_WAVEFRONT_OBJ)))
                {
                    wprintf(L"-uv2 is only supported by sdkmesh\n");
                    return 1;
                }
                break;

            case OPT_VERT_NORMAL_FORMAT:
                normalFormat = static_cast<DXGI_FORMAT>(LookupByName(pValue, g_vertexNormalFormats));
                if (!normalFormat)
                {
                    wprintf(L"Invalid value specified with -fn (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_VERT_UV_FORMAT:
                uvFormat = static_cast<DXGI_FORMAT>(LookupByName(pValue, g_vertexUVFormats));
                if (!uvFormat)
                {
                    wprintf(L"Invalid value specified with -fuv (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_VERT_COLOR_FORMAT:
                colorFormat = static_cast<DXGI_FORMAT>(LookupByName(pValue, g_vertexColorFormats));
                if (!colorFormat)
                {
                    wprintf(L"Invalid value specified with -fc (%ls)\n", pValue);
                    wprintf(L"\n");
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
            SearchForFiles(path.make_preferred(), conversion, (dwOptions& (1 << OPT_RECURSIVE)) != 0);
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

    if (~dwOptions & (uint64_t(1) << OPT_NOLOGO))
        PrintLogo(false);

    // Process files
    for (auto pConv = conversion.begin(); pConv != conversion.end(); ++pConv)
    {
        std::filesystem::path curpath(pConv->szSrc);
        auto const ext = curpath.extension();

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
                (dwOptions & (uint64_t(1) << OPT_CLOCKWISE)) ? false : true,
                (dwOptions & (uint64_t(1) << OPT_NODDS)) ? false : true);
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

        if (dwOptions & (uint64_t(1) << OPT_FLIPU))
        {
            hr = inMesh->InvertUTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting u texcoord (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        if (dwOptions & (uint64_t(1) << OPT_FLIPV))
        {
            hr = inMesh->InvertVTexCoord();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed inverting v texcoord (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        if (dwOptions & (uint64_t(1) << OPT_FLIPZ))
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
            const float epsilon = (dwOptions & (uint64_t(1) << OPT_GEOMETRIC_ADJ)) ? 1e-5f : 0.f;

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
            dwOptions |= uint64_t(1) << OPT_NORMALS;
        }

        if (!inMesh->GetTangentBuffer() && (dwOptions & (uint64_t(1) << OPT_CMO)))
        {
            dwOptions |= uint64_t(1) << OPT_TANGENTS;
        }

        // Compute vertex normals from faces
        if ((dwOptions & (uint64_t(1) << OPT_NORMALS))
            || ((dwOptions & ((uint64_t(1) << OPT_TANGENTS) | (uint64_t(1) << OPT_CTF))) && !inMesh->GetNormalBuffer()))
        {
            CNORM_FLAGS flags = CNORM_DEFAULT;

            if (dwOptions & (uint64_t(1) << OPT_WEIGHT_BY_EQUAL))
            {
                flags |= CNORM_WEIGHT_EQUAL;
            }
            else if (dwOptions & (uint64_t(1) << OPT_WEIGHT_BY_AREA))
            {
                flags |= CNORM_WEIGHT_BY_AREA;
            }

            if (dwOptions & (uint64_t(1) << OPT_CLOCKWISE))
            {
                flags |= CNORM_WIND_CW;
            }

            hr = inMesh->ComputeNormals(flags);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing normals (flags:%lX, %08X%ls)\n", flags,
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        // Compute tangents and bitangents
        if (dwOptions & ((uint64_t(1) << OPT_TANGENTS) | (uint64_t(1) << OPT_CTF)))
        {
            if (!inMesh->GetTexCoordBuffer())
            {
                wprintf(L"\nERROR: Computing tangents/bi-tangents requires texture coordinates\n");
                return 1;
            }

            hr = inMesh->ComputeTangentFrame((dwOptions & (uint64_t(1) << OPT_CTF)) ? true : false);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed computing tangent frame (%08X%ls)\n",
                    static_cast<unsigned int>(hr), GetErrorDesc(hr));
                return 1;
            }
        }

        // Compute IMT
        std::unique_ptr<float[]> IMTData;
        if (dwOptions & ((uint64_t(1) << OPT_IMT_TEXFILE) | (uint64_t(1) << OPT_IMT_VERTEX)))
        {
            if (dwOptions & (uint64_t(1) << OPT_IMT_TEXFILE))
            {
                if (!inMesh->GetTexCoordBuffer())
                {
                    wprintf(L"\nERROR: Computing IMT from texture requires texture coordinates\n");
                    return 1;
                }

                std::filesystem::path tname(texFile);
                auto const txext = tname.extension();

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
                    const Image* img = iimage.GetImage(0, 0, 0);

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
                            reinterpret_cast<const float*>(img->pixels), img->width, img->height,
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

        hr = inMesh->UpdateFaces(nFaces, reinterpret_cast<const uint32_t*>(ib.data()));
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

            hr = inMesh->UpdateUVs(nVerts, texcoord.get(), (dwOptions & (uint64_t(1) << OPT_SECOND_UV)));
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to update with isochart UVs\n");
                return 1;
            }
        }

        if (dwOptions & (uint64_t(1) << OPT_COLOR_MESH))
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

        if (dwOptions & (uint64_t(1) << OPT_FLIP))
        {
            hr = inMesh->ReverseWinding();
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed reversing winding (%08X%ls)\n", static_cast<unsigned int>(hr), GetErrorDesc(hr));
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
            if (dwOptions & (uint64_t(1) << OPT_VBO))
            {
                wcscpy_s(outputExt, L".vbo");
            }
            else if (dwOptions & (uint64_t(1) << OPT_CMO))
            {
                wcscpy_s(outputExt, L".cmo");
            }
            else if (dwOptions & (uint64_t(1) << OPT_WAVEFRONT_OBJ))
            {
                wcscpy_s(outputExt, L".obj");
            }
            else
            {
                wcscpy_s(outputExt, L".sdkmesh");
            }

            outputFile = curpath.stem().native();
            outputFile.append(outputExt);
        }

        if (dwOptions & (uint64_t(1) << OPT_TOLOWER))
        {
            std::transform(outputFile.begin(), outputFile.end(), outputFile.begin(), towlower);
        }

        if (~dwOptions & (uint64_t(1) << OPT_OVERWRITE))
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

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (uint64_t(1) << OPT_FORCE_32BIT_IB)))
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
                (dwOptions & (uint64_t(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                (dwOptions & (uint64_t(1) << OPT_SDKMESH_V2)) ? true : false,
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

            if (!inMesh->Is16BitIndexBuffer() || (dwOptions & (uint64_t(1) << OPT_FORCE_32BIT_IB)))
            {
                wprintf(L"\nERROR: Visual Studio CMO only supports 16-bit indices\n");
                return 1;
            }

            hr = inMesh->ExportToCMO(outputFile.c_str(), inMaterial.size(), inMaterial.empty() ? nullptr : inMaterial.data());
        }
        else if (!_wcsicmp(outputExt, L".obj") || !_wcsicmp(outputExt, L"._obj"))
        {
            std::wstring mtlFilename;
            if ((dwOptions & (uint64_t(1) << OPT_COLOR_MESH))
                && !inMaterial.empty())
            {
                mtlFilename = curpath.stem().native();
                mtlFilename.append(L"_charts");

                if (dwOptions & (uint64_t(1) << OPT_TOLOWER))
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

                if (~dwOptions & (uint64_t(1) << OPT_OVERWRITE))
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

                for (auto const& mtl : inMaterial)
                {
                    // Minimal material output.
                    os << L"newmtl " << mtl.name << std::endl;
                    os << L"illum 1" << std::endl;
                    os << L"Ka " << mtl.ambientColor.x << L" " << mtl.ambientColor.y << L" " << mtl.ambientColor.z << std::endl;
                    os << L"Kd " << mtl.diffuseColor.x << L" " << mtl.diffuseColor.y << L" " << mtl.diffuseColor.z << std::endl << std::endl;
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
        if (dwOptions & (uint64_t(1) << OPT_OUTPUT_REMAPPING))
        {
            std::wstring mapFilename;
            mapFilename = curpath.stem().native();
            mapFilename.append(L"_map");

            if (dwOptions & (uint64_t(1) << OPT_TOLOWER))
            {
                std::transform(mapFilename.begin(), mapFilename.end(), mapFilename.begin(), towlower);
            }

            std::filesystem::path mapOutputPath(outputFile);
            mapOutputPath = mapOutputPath.parent_path().append(mapFilename);
            mapOutputPath.concat(L".txt");

            if (~dwOptions & (uint64_t(1) << OPT_OVERWRITE))
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
        if (dwOptions & (uint64_t(1) << OPT_UV_MESH))
        {
            const bool vizNormals = (dwOptions & (uint64_t(1) << OPT_VIZ_NORMALS)) != 0;
            const bool secondUVs = (dwOptions & (uint64_t(1) << OPT_SECOND_UV)) != 0;
            hr = inMesh->VisualizeUVs(secondUVs, vizNormals);
            if (FAILED(hr))
            {
                wprintf(L"\nERROR: Failed to create UV visualization mesh\n");
                return 1;
            }

            std::wstring uvFilename;
            uvFilename = curpath.stem().native();
            uvFilename.append(L"_texture");

            if (dwOptions & (uint64_t(1) << OPT_TOLOWER))
            {
                std::transform(uvFilename.begin(), uvFilename.end(), uvFilename.begin(), towlower);
            }

            std::filesystem::path uvOutputPath(outputFile);
            uvOutputPath = uvOutputPath.parent_path().append(uvFilename);
            uvOutputPath.concat(outputExt);

            if (~dwOptions & (uint64_t(1) << OPT_OVERWRITE))
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
                    (dwOptions & (uint64_t(1) << OPT_FORCE_32BIT_IB)) ? true : false,
                    (dwOptions & (uint64_t(1) << OPT_SDKMESH_V2)) ? true : false,
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
