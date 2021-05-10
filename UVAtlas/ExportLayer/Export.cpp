#include "pch.h"
#include "UVAtlas.h"

#if defined(_MSC_VER)
#   define EXPORT_C_API __declspec(dllexport)
#else
#	define EXPORT_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum uvatlas_options {
        UVATLAS_DEFAULT = 0x00,
        UVATLAS_GEODESIC_FAST = 0x01,
        UVATLAS_GEODESIC_QUALITY = 0x02,
        UVATLAS_LIMIT_MERGE_STRETCH = 0x04,
        UVATLAS_LIMIT_FACE_STRETCH = 0x08,
        _UVATLAS_FORCE_U32 = 0x7FFFFFFF
    } uvatlas_options;

    typedef struct uvatlas_result {
        int32_t                 result;
        uint32_t                verticesCount;
        uint32_t                indicesCount;
        DirectX::UVAtlasVertex* vertices;
        uint32_t* indices;
        uint32_t* facePartitioning;
        uint32_t* vertexRemapArray;
        float                   stretch;
        uint32_t                charts;
    } uvatlas_result;

    EXPORT_C_API uvatlas_result* uvatlas_create_uint32(
        const DirectX::XMFLOAT3* positions, size_t nVerts,
        const uint32_t* indices, size_t nFaces,
        size_t maxChartNumber, float maxStretch,
        size_t width, size_t height,
        float gutter,
        const uint32_t* adjacency, const uint32_t* falseEdgeAdjacency,
        const float* pIMTArray,
        /*std::function<HRESULT(float percentComplete)> statusCallBack,*/
        float callbackFrequency,
        uvatlas_options options)
    {
        uvatlas_result* atlas_result = new uvatlas_result();

        std::vector<DirectX::UVAtlasVertex> vertexBuffer;
        std::vector<uint8_t> indexBuffer;
        std::vector<uint32_t> facePartitioning;
        std::vector<uint32_t> vertexRemapArray;
        size_t charts;

        atlas_result->result = DirectX::UVAtlasCreate(positions, nVerts,
            indices, DXGI_FORMAT_R32_UINT, nFaces,
            maxChartNumber, maxStretch,
            width, height,
            gutter,
            adjacency, falseEdgeAdjacency,
            pIMTArray,
            nullptr,
            callbackFrequency,
            (DirectX::UVATLAS)options,
            vertexBuffer,
            indexBuffer,
            &facePartitioning,
            &vertexRemapArray,
            &atlas_result->stretch,
            &charts);

        if (FAILED(atlas_result->result))
        {
            return atlas_result;
        }

        auto newIB = reinterpret_cast<const uint32_t*>(&indexBuffer.front());

        atlas_result->verticesCount = static_cast<uint32_t>(vertexBuffer.size());
        atlas_result->indicesCount = static_cast<uint32_t>(nFaces * 3);
        atlas_result->vertices = new DirectX::UVAtlasVertex[atlas_result->verticesCount];
        atlas_result->indices = new uint32_t[atlas_result->indicesCount];
        atlas_result->facePartitioning = new uint32_t[atlas_result->verticesCount];
        atlas_result->vertexRemapArray = new uint32_t[atlas_result->verticesCount];
        atlas_result->charts = static_cast<uint32_t>(charts);
        std::copy(vertexBuffer.begin(), vertexBuffer.end(), atlas_result->vertices);
        memcpy(atlas_result->indices, newIB, atlas_result->indicesCount * sizeof(uint32_t));
        std::copy(facePartitioning.begin(), facePartitioning.end(), atlas_result->facePartitioning);
        std::copy(vertexRemapArray.begin(), vertexRemapArray.end(), atlas_result->vertexRemapArray);
        return atlas_result;
    }

    EXPORT_C_API void uvatlas_delete(uvatlas_result* result)
    {
        delete[] result->vertices;
        delete[] result->indices;
        delete[] result->facePartitioning;
        delete[] result->vertexRemapArray;
        delete result;
        result = nullptr;
    }

    EXPORT_C_API HRESULT UVAtlasApplyRemap(
        const void* vbin,
        size_t stride,
        size_t nVerts,
        size_t nNewVerts,
        const uint32_t* vertexRemap,
        void* vbout)
    {
        return DirectX::UVAtlasApplyRemap(vbin, stride, nVerts, nNewVerts, vertexRemap, vbout);
    }

#ifdef __cplusplus
}
#endif
