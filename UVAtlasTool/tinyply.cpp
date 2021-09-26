// This file exists to create a nice static or shared library via cmake
// but can otherwise be omitted if you prefer to compile tinyply
// directly into your own project.
#define TINYPLY_IMPLEMENTATION
#include "tinyply.h"
#include "Mesh.h"


#include <cwchar>
#include <iterator>
#include <new>
#include <fstream>

#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstring>
#include <iterator>


 


using namespace DirectX;

namespace
{


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
    } // namespace


    inline std::vector<uint8_t> read_file_binary(const std::wstring& pathToFile)
    {
        std::ifstream file(pathToFile, std::ios::binary);
        std::vector<uint8_t> fileBufferBytes;

        if (file.is_open())
        {
            file.seekg(0, std::ios::end);
            size_t sizeBytes = file.tellg();
            file.seekg(0, std::ios::beg);
            fileBufferBytes.resize(sizeBytes);
            if (file.read((char*)fileBufferBytes.data(), sizeBytes)) return fileBufferBytes;
        }
        else throw std::runtime_error("could not open binary ifstream to path ");
        return fileBufferBytes;
    }

    struct memory_buffer : public std::streambuf
    {
        char* p_start{ nullptr };
        char* p_end{ nullptr };
        size_t size;

        memory_buffer(char const* first_elem, size_t size)
            : p_start(const_cast<char*>(first_elem)), p_end(p_start + size), size(size)
        {
            setg(p_start, p_start, p_end);
        }

        pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override
        {
            if (dir == std::ios_base::cur) gbump(static_cast<int>(off));
            else setg(p_start, (dir == std::ios_base::beg ? p_start : p_end) + off, p_end);
            return gptr() - p_start;
        }

        pos_type seekpos(pos_type pos, std::ios_base::openmode which) override
        {
            return seekoff(pos, std::ios_base::beg, which);
        }
    };

    struct memory_stream : virtual memory_buffer, public std::istream
    {
        memory_stream(char const* first_elem, size_t size)
            : memory_buffer(first_elem, size), std::istream(static_cast<std::streambuf*>(this)) {}
    };

    class manual_timer
    {
        std::chrono::high_resolution_clock::time_point t0;
        double timestamp{ 0.0 };
    public:
        void start() { t0 = std::chrono::high_resolution_clock::now(); }
        void stop() { timestamp = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0; }
        const double& get() { return timestamp; }
    };

    struct float2 { float x, y; };
    struct float3 { float x, y, z; };
    struct double3 { double x, y, z; };
    struct uint3 { uint32_t x, y, z; };
    struct uint4 { uint32_t x, y, z, w; };

    struct geometry
    {
        std::vector<float3> vertices;
        std::vector<float3> normals;
        std::vector<float2> texcoords;
        std::vector<uint3> triangles;
    };

#pragma pack(push,1)

    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 textureCoordinate;
    };


#pragma pack(pop)



    std::wstring ProcessTextureFileName(const wchar_t* inName, bool dds)
    {
        if (!inName || !*inName)
            return std::wstring();

        wchar_t txext[_MAX_EXT] = {};
        wchar_t txfname[_MAX_FNAME] = {};
        _wsplitpath_s(inName, nullptr, 0, nullptr, 0, txfname, _MAX_FNAME, txext, _MAX_EXT);

        if (dds)
        {
            wcscpy_s(txext, L".dds");
        }

        wchar_t texture[_MAX_PATH] = {};
        _wmakepath_s(texture, nullptr, nullptr, txfname, txext);
        return std::wstring(texture);
    }
}


_Use_decl_annotations_
HRESULT Mesh::CreateFromPLY(const wchar_t* szFileName, std::unique_ptr<Mesh>& result, bool preload_into_memory) noexcept
{



    const std::wstring& filepath = szFileName;

    std::cout << "........................................................................\n";
    std::wcout << "Now Reading: " << filepath << std::endl;

    std::unique_ptr<std::istream> file_stream;
    std::vector<uint8_t> byte_buffer;

    try
    {
        // For most files < 1gb, pre-loading the entire file upfront and wrapping it into a
        // stream is a net win for parsing speed, about 40% faster.
        if (preload_into_memory)
        {
            byte_buffer = read_file_binary(filepath);
            file_stream.reset(new memory_stream((char*)byte_buffer.data(), byte_buffer.size()));
        }
        else
        {
            file_stream.reset(new std::ifstream(filepath, std::ios::binary));
        }

        if (!file_stream || file_stream->fail()) throw std::runtime_error("file_stream failed to open ");

        file_stream->seekg(0, std::ios::end);
        const float size_mb = file_stream->tellg() * float(1e-6);
        file_stream->seekg(0, std::ios::beg);

        PlyFile file;
        file.parse_header(*file_stream);

        std::cout << "\t[ply_header] Type: " << (file.is_binary_file() ? "binary" : "ascii") << std::endl;
        for (const auto& c : file.get_comments()) std::cout << "\t[ply_header] Comment: " << c << std::endl;
        for (const auto& c : file.get_info()) std::cout << "\t[ply_header] Info: " << c << std::endl;

        for (const auto& e : file.get_elements())
        {
            std::cout << "\t[ply_header] element: " << e.name << " (" << e.size << ")" << std::endl;
            for (const auto& p : e.properties)
            {
                std::cout << "\t[ply_header] \tproperty: " << p.name << " (type=" << tinyply::PropertyTable[p.propertyType].str << ")";
                if (p.isList) std::cout << " (list_type=" << tinyply::PropertyTable[p.listType].str << ")";
                std::cout << std::endl;
            }
        }

        // Because most people have their own mesh types, tinyply treats parsed data as structured/typed byte buffers.
        // See examples below on how to marry your own application-specific data structures with this one.
        std::shared_ptr<PlyData> vertices, normals, colors, texcoords, faces, tripstrip;

        // The header information can be used to programmatically extract properties on elements
        // known to exist in the header prior to reading the data. For brevity of this sample, properties
        // like vertex position are hard-coded:
        try { vertices = file.request_properties_from_element("vertex", { "x", "y", "z" }); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        try { normals = file.request_properties_from_element("vertex", { "nx", "ny", "nz" }); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        try { colors = file.request_properties_from_element("vertex", { "red", "green", "blue", "alpha" }); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        try { colors = file.request_properties_from_element("vertex", { "r", "g", "b", "a" }); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        try { texcoords = file.request_properties_from_element("vertex", { "u", "v" }); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        // Providing a list size hint (the last argument) is a 2x performance improvement. If you have
        // arbitrary ply files, it is best to leave this 0.
        try { faces = file.request_properties_from_element("face", { "vertex_indices" }, 3); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        // Tristrips must always be read with a 0 list size hint (unless you know exactly how many elements
        // are specifically in the file, which is unlikely);
        try { tripstrip = file.request_properties_from_element("tristrips", { "vertex_indices" }, 0); }
        catch (const std::exception& e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        manual_timer read_timer;

        read_timer.start();
        file.read(*file_stream);
        read_timer.stop();

        const float parsing_time = read_timer.get() / 1000.f;
        std::cout << "\tparsing " << size_mb << "mb in " << parsing_time << " seconds [" << (size_mb / parsing_time) << " MBps]" << std::endl;

        if (vertices)   std::cout << "\tRead " << vertices->count << " total vertices " << std::endl;
        if (normals)    std::cout << "\tRead " << normals->count << " total vertex normals " << std::endl;
        if (colors)     std::cout << "\tRead " << colors->count << " total vertex colors " << std::endl;
        if (texcoords)  std::cout << "\tRead " << texcoords->count << " total vertex texcoords " << std::endl;
        if (faces)      std::cout << "\tRead " << faces->count << " total faces (triangles) " << std::endl;
        if (tripstrip)  std::cout << "\tRead " << (tripstrip->buffer.size_bytes() / tinyply::PropertyTable[tripstrip->t].stride) << " total indicies (tristrip) " << std::endl;

        //////////////


        result.reset(new (std::nothrow) Mesh);
        if (!result)
            return E_OUTOFMEMORY;

        std::unique_ptr<XMFLOAT3[]> pos(new (std::nothrow) XMFLOAT3[vertices->count]);
        std::unique_ptr<XMFLOAT3[]> norm(new (std::nothrow) XMFLOAT3[vertices->count]);
        std::unique_ptr<XMFLOAT2[]> texcoord(new (std::nothrow) XMFLOAT2[vertices->count]);
        if (!pos || !norm || !texcoord)
            return E_OUTOFMEMORY;


        const size_t numVerticesBytesVert = vertices->buffer.size_bytes();
        std::vector<float3> verts(vertices->count);
        std::memcpy(verts.data(), vertices->buffer.get(), numVerticesBytesVert);

        const size_t numVerticesBytesNorm = normals->buffer.size_bytes();
        std::vector<float3> norms(normals->count);
        std::memcpy(norms.data(), normals->buffer.get(), numVerticesBytesNorm);


        std::vector<float3> texs;
        if (texcoords) {
            const size_t numVerticesBytesTex = texcoords->buffer.size_bytes();
            texs.resize(texcoords->count);
            std::memcpy(texs.data(), texcoords->buffer.get(), numVerticesBytesTex);
        }



        for (size_t j = 0; j < vertices->count; ++j) {
            pos[j] = XMFLOAT3(verts[j].x, verts[j].y, verts[j].z);
            norm[j] = XMFLOAT3(norms[j].x, norms[j].y, norms[j].z);
            if (texcoords)
                texcoord[j] = XMFLOAT2(texs[j].x, texs[j].y);

            int dede = 0;
        }




        /// //// FACES /////

        unsigned long numIndices = faces->count;
        // Copy IB to result
        std::unique_ptr<uint32_t[]> indices(new (std::nothrow) uint32_t[numIndices * 3]);// header.numIndices]);
        if (!indices)
            return E_OUTOFMEMORY;


        // long numIndicestest = numVerticesBytesFaces / 4 / 3;// int on 4 bytes
        uint32_t* ptr = (uint32_t*)faces->buffer.get();

        size_t bitssize = faces->buffer.size_bytes();


        long b = bitssize / numIndices;
        long bytesperelement = b / 3;

        for (int i = 0; i < faces->count * 3; i++) {
            uint32_t d = ptr[i];
            indices[i] = d;
        }


        result->mPositions.swap(pos);
        result->mNormals.swap(norm);
        result->mTexCoords.swap(texcoord);
        result->mIndices.swap(indices);
        result->mnVerts = vertices->count;
        result->mnFaces = numIndices;

        int dede = 0;









    }
    catch (const std::exception& e)
    {
        std::cerr << "Caught tinyply exception: " << e.what() << std::endl;
    }


    return S_OK;
    /*
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

    result.reset(new (std::nothrow) Mesh);
    if (!result)
        return E_OUTOFMEMORY;

    // Read vertices/indices from VBO
    std::unique_ptr<vertex_t[]> vb(new (std::nothrow) vertex_t[header.numVertices]);
    std::unique_ptr<uint16_t[]> ib(new (std::nothrow) uint16_t[header.numIndices]);
    if (!vb || !ib)
        return E_OUTOFMEMORY;

    auto vertSize = static_cast<DWORD>(sizeof(vertex_t) * header.numVertices);

    if (!ReadFile(hFile.get(), vb.get(), vertSize, &bytesRead, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (bytesRead != vertSize)
        return E_FAIL;

    auto indexSize = static_cast<DWORD>(sizeof(uint16_t) * header.numIndices);

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
        pos[j] = vptr->position;
        norm[j] = vptr->normal;
        texcoord[j] = vptr->textureCoordinate;
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
            indices[j] = uint32_t(-1);
        else
            indices[j] = index;
    }

    result->mPositions.swap(pos);
    result->mNormals.swap(norm);
    result->mTexCoords.swap(texcoord);
    result->mIndices.swap(indices);
    result->mnVerts = header.numVertices;
    result->mnFaces = header.numIndices / 3;

    return S_OK;
    */
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT Mesh::ExportToPLY(const wchar_t* szFileNameIn/*, size_t nMaterials, const Material* materials*/) const
{
    std::wstring szFileName(szFileNameIn);

    using namespace VBO;


    geometry cube;// = make_cube_geometry();

    //if (!szFileName)
      //  return E_INVALIDARG;

    if (!mnFaces || !mIndices || !mnVerts || !mPositions || !mNormals || !mTexCoords)
        return E_UNEXPECTED;

    if ((uint64_t(mnFaces) * 3) >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    if (mnVerts >= UINT32_MAX)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    // Setup VBO header
    header_t header;
    header.numVertices = static_cast<uint32_t>(mnVerts);
    header.numIndices = static_cast<uint32_t>(mnFaces * 3);

    // Setup vertices/indices for VBO

    std::unique_ptr<vertex_t[]> vb(new (std::nothrow) vertex_t[mnVerts]);
    std::unique_ptr<uint16_t[]> ib(new (std::nothrow) uint16_t[header.numIndices]);
    if (!vb || !ib)
        return E_OUTOFMEMORY;


    // Copy to VB

    for (size_t j = 0; j < mnVerts; ++j)
    {

        float3 vert;
        vert.x = mPositions[j].x;
        vert.y = mPositions[j].y;
        vert.z = mPositions[j].z;


        float3 norm;
        norm.x = mNormals[j].x;
        norm.y = mNormals[j].y;
        norm.z = mNormals[j].z;

        float2 tex;
        tex.x = mTexCoords[j].x;
        tex.y = mTexCoords[j].y;


        cube.vertices.push_back(vert);
        cube.normals.push_back(norm);
        cube.texcoords.push_back(tex);

    }



    std::unique_ptr<uint32_t[]> ib2(new (std::nothrow) uint32_t[header.numIndices]);
    if (!vb || !ib)
        return E_OUTOFMEMORY;


    /*
    // Copy to VB
    auto vptr = vb.get();
    for (size_t j = 0; j < mnVerts; ++j, ++vptr)
    {
        vptr->position = mPositions[j];
        vptr->normal = mNormals[j];
        vptr->textureCoordinate = mTexCoords[j];
    }*/



    // Copy to IB
    auto iptr = ib2.get();

    /*
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
    }*/




    /*
    for (size_t face = 0; face < mnFaces; ++face)
    {
        std::vector<uint32_t> mList;
        for (size_t point = 0; point < 3; ++point)
        {
            uint32_t i = mIndices[face * 3 + point] + 1;
            mList.push_back(i);
        }

        cube.triangles.push_back({ mList[0],mList[0 + 1],mList[0 + 2] });

    }*/






    // auto iptr = mIndices.get();
    for (size_t j = 0; j < header.numIndices; j += 3)
    {


        uint32_t indice1 = iptr[j];
        uint32_t indice2 = iptr[j + 1];
        uint32_t indice3 = iptr[j + 2];

        //cube.triangles.push_back({ indice1,indice2,indice3 });

        cube.triangles.push_back({ mIndices[j],mIndices[j + 1],mIndices[j + 2] });


    }




    /*
    for (size_t j = 0; j < header.numIndices; j += 4)
    {

         cube.triangles.push_back({ mIndices[j],mIndices[j + 1],mIndices[j + 2] });
         cube.triangles.push_back({ mIndices[j],mIndices[j + 2],mIndices[j + 3] });


    }*/






    /*
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
    }*/


    /*

    const struct CubeVertex { float3 position, normal; float2 texCoord; } verts[] = {
    { { -1, -1, -1 },{ -1, 0, 0 },{ 0, 0 } },{ { -1, -1, +1 },{ -1, 0, 0 },{ 1, 0 } },{ { -1, +1, +1 },{ -1, 0, 0 },{ 1, 1 } },{ { -1, +1, -1 },{ -1, 0, 0 },{ 0, 1 } },
    { { +1, -1, +1 },{ +1, 0, 0 },{ 0, 0 } },{ { +1, -1, -1 },{ +1, 0, 0 },{ 1, 0 } },{ { +1, +1, -1 },{ +1, 0, 0 },{ 1, 1 } },{ { +1, +1, +1 },{ +1, 0, 0 },{ 0, 1 } },
    { { -1, -1, -1 },{ 0, -1, 0 },{ 0, 0 } },{ { +1, -1, -1 },{ 0, -1, 0 },{ 1, 0 } },{ { +1, -1, +1 },{ 0, -1, 0 },{ 1, 1 } },{ { -1, -1, +1 },{ 0, -1, 0 },{ 0, 1 } },
    { { +1, +1, -1 },{ 0, +1, 0 },{ 0, 0 } },{ { -1, +1, -1 },{ 0, +1, 0 },{ 1, 0 } },{ { -1, +1, +1 },{ 0, +1, 0 },{ 1, 1 } },{ { +1, +1, +1 },{ 0, +1, 0 },{ 0, 1 } },
    { { -1, -1, -1 },{ 0, 0, -1 },{ 0, 0 } },{ { -1, +1, -1 },{ 0, 0, -1 },{ 1, 0 } },{ { +1, +1, -1 },{ 0, 0, -1 },{ 1, 1 } },{ { +1, -1, -1 },{ 0, 0, -1 },{ 0, 1 } },
    { { -1, +1, +1 },{ 0, 0, +1 },{ 0, 0 } },{ { -1, -1, +1 },{ 0, 0, +1 },{ 1, 0 } },{ { +1, -1, +1 },{ 0, 0, +1 },{ 1, 1 } },{ { +1, +1, +1 },{ 0, 0, +1 },{ 0, 1 } } };

    std::vector<uint4> quads = { { 0, 1, 2, 3 },{ 4, 5, 6, 7 },{ 8, 9, 10, 11 },{ 12, 13, 14, 15 },{ 16, 17, 18, 19 },{ 20, 21, 22, 23 } };

    for (auto& q : quads)
    {
        cube.triangles.push_back({ q.x,q.y,q.z });
        cube.triangles.push_back({ q.x,q.z,q.w });
    }

    for (int i = 0; i < 24; ++i)
    {
        cube.vertices.push_back(verts[i].position);
        cube.normals.push_back(verts[i].normal);
        cube.texcoords.push_back(verts[i].texCoord);
    }*/


    //here get geometry

    std::filebuf fb_binary;
    fb_binary.open(szFileName + L"-binary.ply", std::ios::out | std::ios::binary);
    std::ostream outstream_binary(&fb_binary);
    if (outstream_binary.fail()) throw std::runtime_error("failed to open ");

    std::filebuf fb_ascii;
    fb_ascii.open(szFileName + L"-ascii.ply", std::ios::out);
    std::ostream outstream_ascii(&fb_ascii);
    if (outstream_ascii.fail()) throw std::runtime_error("failed to open ");

    PlyFile cube_file;

    cube_file.add_properties_to_element("vertex", { "x", "y", "z" },
        Type::FLOAT32, cube.vertices.size(), reinterpret_cast<uint8_t*>(cube.vertices.data()), Type::INVALID, 0);

    cube_file.add_properties_to_element("vertex", { "nx", "ny", "nz" },
        Type::FLOAT32, cube.normals.size(), reinterpret_cast<uint8_t*>(cube.normals.data()), Type::INVALID, 0);

    cube_file.add_properties_to_element("vertex", { "u", "v" },
        Type::FLOAT32, cube.texcoords.size(), reinterpret_cast<uint8_t*>(cube.texcoords.data()), Type::INVALID, 0);

    cube_file.add_properties_to_element("face", { "vertex_indices" },
        Type::UINT32, cube.triangles.size(), reinterpret_cast<uint8_t*>(cube.triangles.data()), Type::UINT8, 3);

    cube_file.get_comments().push_back("generated by tinyply 2.3");

    // Write an ASCII file
    cube_file.write(outstream_ascii, false);

    // Write a binary file
    cube_file.write(outstream_binary, true);





     
}

 

