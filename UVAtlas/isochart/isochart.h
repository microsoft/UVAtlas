//-------------------------------------------------------------------------------------
// UVAtlas - isochart.h
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#include "UVAtlas.h"
#include "isochartconfig.h"

// Isochart API for one-pass charting
// "Isochart API" is provided for the users want doing partition and packing in single action. 
// It's a wrapper of CisochartEngine
#define MAKE_STAGE(TOTAL, DONE, TODO) ( ((TOTAL) << 16) | ((DONE) << 8) | (TODO) )
#define STAGE_TOTAL(S) ( ((S) >> 16) & 0x000000ff)
#define STAGE_DONE(S) ( ((S) >> 8) & 0x000000ff)
#define STAGE_TODO(S) ( (S) & 0x000000ff)

#ifndef _LIMIT_FACENUM_USENEWGEODIST
#define _LIMIT_FACENUM_USENEWGEODIST 25000
#endif

namespace Isochart
{
    typedef float FLOAT3[IMT_DIM]; // Used to define IMT matrix

    // User-specified callback. Return E_FAIL to abort ongoing task
    typedef std::function<HRESULT __cdecl(float percentComplete)> LPISOCHARTCALLBACK;
    typedef std::function<HRESULT __cdecl(const DirectX::XMFLOAT2 * uv, size_t primitiveID, size_t signalDimension, void* userData, float* signalOut)> LPIMTSIGNALCALLBACK;

    // isochart options
    namespace ISOCHARTOPTION
    {
        // the default option, currently it only affects how isochart selects its geodesic distance computation algorithm.
        // when the face num of the mesh to be processed is below _LIMIT_FACENUM_USENEWGEODIST,
        // isochart use the new geodesic distance algorithm, 
        // otherwise the old [KS98] approach is applied
        constexpr unsigned int DEFAULT = 0x00;

        // all internal geodesic distance computation will use the [KS98] approach, this is fast but imprecise
        constexpr unsigned int GEODESIC_FAST = 0x01;

        // all internal geodesic distance computation tries to use the new approach implemented in geodesicdist.lib (except IMT is specified), this is precise but slower
        constexpr unsigned int GEODESIC_QUALITY = 0x02;
    };
    constexpr unsigned int OPTIONMASK_ISOCHART_GEODESIC = ISOCHARTOPTION::GEODESIC_FAST | ISOCHARTOPTION::GEODESIC_QUALITY;

    HRESULT
        isochart(
            const void* pVertexArray,
            size_t VertexCount,
            size_t VertexStride,
            DXGI_FORMAT IndexFormat,
            const void* pFaceIndexArray,
            size_t FaceCount,
            const FLOAT3* pIMTArray,
            size_t MaxChartNumber,
            float Stretch,
            size_t Width,
            size_t Height,
            float Gutter,
            const uint32_t* pOriginalAjacency,	// Reserved to support user-specified boundaries
            std::vector<DirectX::UVAtlasVertex>* ppVertexArrayOut,
            std::vector<uint8_t>* ppFaceIndexArrayOut,
            std::vector<uint32_t>* ppVertexRemapArrayOut,
            size_t* pChartNumberOut,
            float* pMaxStretchOut,
            // Callback parameters
            LPISOCHARTCALLBACK pCallback = nullptr,
            float Frequency = 0.01f,	// Call callback function each time completed 1% work of all task
            unsigned int dwOptions = ISOCHARTOPTION::DEFAULT);

    HRESULT
        isochartpartition(
            _In_reads_bytes_(VertexCount* VertexStride)  const void* pVertexArray,
            _In_                                        size_t VertexCount,
            _In_                                        size_t VertexStride,
            _In_                                        DXGI_FORMAT IndexFormat,
            _When_(IndexFormat == DXGI_FORMAT_R16_UINT, _In_reads_bytes_(FaceCount * 3 * sizeof(uint16_t)))
            _When_(IndexFormat != DXGI_FORMAT_R16_UINT, _In_reads_bytes_(FaceCount * 3 * sizeof(uint32_t))) const void* pFaceIndexArray,
            _In_                                        size_t FaceCount,
            _In_reads_opt_(FaceCount)                   const FLOAT3* pIMTArray,
            _In_                                        size_t MaxChartNumber,
            _In_                                        float Stretch,
            _In_reads_(FaceCount * 3)                     const uint32_t* pOriginalAjacency,
            _In_                                        std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
            _In_                                        std::vector<uint8_t>* pvFaceIndexArrayOut,
            _In_                                        std::vector<uint32_t>* pvVertexRemapArrayOut,
            _In_                                        std::vector<uint32_t>* pvAttributeIDOut,
            _In_                                        std::vector<uint32_t>* pvAdjacencyOut,
            _Out_opt_                                   size_t* pChartNumberOut,
            _Out_opt_                                   float* pMaxStretchOut,
            _In_                                        unsigned int Stage,
            _In_opt_                                    LPISOCHARTCALLBACK pCallback = nullptr,
            _In_                                        float Frequency = 0.01f,	// Call callback function each time completed 1% work of all task
            _In_opt_                                    const uint32_t* pSplitHint = nullptr, // Optional parameter, Generated by copying from the pOriginalAjacency
                                                                                              // and for each triangle, if the edge shared by itself and ajacency triangle
                                                                                              // CAN be splitted, set the that ajacency to -1.
                                                                                              // Usually, it's easier for user to specified the edge that CAN NOT be
                                                                                              // splitted, make sure to validate the input
            _In_                                        unsigned int dwOptions = ISOCHARTOPTION::DEFAULT);


    // Class IIsochartEngine for the advanced usage
    // Use CreateIsochartEngine() & ReleaseIsochartEngine to create/release 
    // IIsochartEngine instance.
    class IIsochartEngine
    {
    public:
        virtual ~IIsochartEngine() = default;

        static IIsochartEngine* CreateIsochartEngine();
        static void ReleaseIsochartEngine(IIsochartEngine* pIsochartEngine);

        virtual HRESULT Initialize(
            const void* pVertexArray,
            size_t VertexCount,
            size_t VertexStride,
            DXGI_FORMAT IndexFormat,
            const void* pFaceIndexArray,
            size_t FaceCount,
            const FLOAT3* pIMTArray,
            const uint32_t* pOriginalAjacency,
            const uint32_t* pSplitHint,
            unsigned int dwOptions) noexcept = 0;

        virtual HRESULT Free() noexcept = 0;

        virtual HRESULT  Partition(
            size_t MaxChartNumber,
            float Stretch,
            size_t& ChartNumberOut,
            float& MaxChartStretchOut,
            uint32_t* pFaceAttributeIDOut) noexcept = 0;

        virtual HRESULT Pack(
            size_t Width,
            size_t Height,
            float Gutter,
            const void* pOrigIndexBuffer,
            std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
            std::vector<uint8_t>* pvFaceIndexArrayOut,
            std::vector<uint32_t>* pvVertexRemapArrayOut,
            _In_opt_ std::vector<uint32_t>* pvAttributeID) noexcept = 0;

        virtual HRESULT SetCallback(
            LPISOCHARTCALLBACK pCallback,
            float Frequency) noexcept = 0;

        virtual HRESULT SetStage(
            unsigned int TotalStageCount,
            unsigned int DoneStageCount) noexcept = 0;

        virtual HRESULT ExportPartitionResult(
            std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
            std::vector<uint8_t>* pvFaceIndexArrayOut,
            std::vector<uint32_t>* pvVertexRemapArrayOut,
            std::vector<uint32_t>* pvAttributeIDOut,
            std::vector<uint32_t>* pvAdjacencyOut) noexcept = 0;

        virtual HRESULT InitializePacking(
            std::vector<DirectX::UVAtlasVertex>* pvVertexBuffer,
            size_t VertexCount,
            std::vector<uint8_t>* pvFaceIndexBuffer,
            size_t FaceCount,
            const uint32_t* pdwFaceAdjacentArrayIn) noexcept = 0;
    };

    /////////////////////////////////////////////////////////////////////////
    ///////////////////////IMT Computation////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////

    // Internal API, Compute IMT of one face from per-vertex signal
    HRESULT
        IMTFromPerVertexSignal(
            const DirectX::XMFLOAT3* pV3d,  // [In] surface coordinates of face's vertices
            const float* pfSignalArray,     // [In] An array of 3 * dwSignalDimension FLOATs
            size_t dwSignalDimension,       // [In] Dimension of signal
            FLOAT3* pfIMTArray);            // [Out] Result IMT

    HRESULT
        IMTFromTextureMap(
            const DirectX::XMFLOAT3* pV3d,	// [In] surface coordinates of face's vertices
            const DirectX::XMFLOAT2* pUV,	// [In] Texture coordinates of each vertices in the range of 0 to 1.0f
            size_t dwMaxSplitLevel,         // [In] Split into how many levels.
            float fMinVertexUvIDistance,	// [In] smallest vertices distance in uv plane
            size_t uPrimitiveId,            // [in] Primitive ID to pass to callback
            size_t dwSignalDimension,       // [In] Signal dimension 
            LPIMTSIGNALCALLBACK pfnGetSignal,// Callback function to get signal on special UV point
            void* lpTextureData,            // Texture data, can be accessed by pfnGetSignal
            FLOAT3* pfIMTArray);            // [Out] Result IMT

    HRESULT
        IMTFromTextureMapEx(
            const DirectX::XMFLOAT3* pV3d,	// [In] surface coordinates of face's vertices
            const DirectX::XMFLOAT2* pUV,	// [In] Texture coordinates of each vertices in the range of 0 to 1.0f
            size_t uPrimitiveId,            // [in] Primitive ID to pass to callback
            size_t dwSignalDimension,
            LPIMTSIGNALCALLBACK pfnGetSignal,
            void* lpTextureData,
            FLOAT3* pfIMTArray);            // [Out] Result IMT

}
