//-------------------------------------------------------------------------------------
// UVAtlas - isochartengine.h
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//  
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#include "basemeshinfo.h"
#include "callbackschemer.h"
#include "maxheap.hpp"


namespace Isochart
{
class CCallbackSchemer;
class CIsochartMesh;

class CIsochartEngine : public IIsochartEngine
{
public:
    CIsochartEngine();
    virtual ~CIsochartEngine();

    // IIsochartEngine
    STDMETHODIMP Initialize(
        const void* pVertexArray,
        size_t VertexCount,
        size_t VertexStride,
        DXGI_FORMAT IndexFormat,
        const void* pFaceIndexArray,
        size_t FaceCount,
        const FLOAT3* pIMTArray,
        const uint32_t* pOriginalAjacency,
        const uint32_t* pSplitHint,
        DWORD dwOptions) override;

    STDMETHODIMP Free() override;

    STDMETHODIMP Partition(
        size_t MaxChartNumber,
        float Stretch,
        size_t& ChartNumberOut,
        float& MaxChartStretchOut,
        uint32_t* pFaceAttributeIDOut) override;

    STDMETHODIMP Pack(
        size_t Width,
        size_t Height,
        float Gutter,
        const void* pOrigIndexBuffer,
        std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
        std::vector<uint8_t>* pvFaceIndexArrayOut,
        std::vector<uint32_t>* pvVertexRemapArrayOut,
        _In_opt_ std::vector<uint32_t>* pvAttributeID) override;

    STDMETHODIMP SetCallback(
        LPISOCHARTCALLBACK pCallback,
        float Frequency) override;

    STDMETHOD(SetStage)(
        unsigned int TotalStageCount,
        unsigned int DoneStageCount) override;

    STDMETHODIMP ExportPartitionResult(
        std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
        std::vector<uint8_t>* pvFaceIndexArrayOut,
        std::vector<uint32_t>* pvVertexRemapArrayOut,
        std::vector<uint32_t>* pvAttributeIDOut,
        std::vector<uint32_t>* pvAdjacencyOut) override;

    STDMETHODIMP InitializePacking(
        std::vector<DirectX::UVAtlasVertex>* pvVertexBuffer,
        size_t VertexCount,
        std::vector<uint8_t>* pvFaceIndexBuffer,
        size_t FaceCount,
        const uint32_t* pdwFaceAdjacentArrayIn) override;

    HRESULT CreateEngineMutex();
private:
    enum EngineState
    {
        ISOCHART_ST_UNINITILAIZED, 
        ISOCHART_ST_INITIALIZED,
        ISOCHART_ST_PARTITIONED,
        ISOCHART_ST_PACKED
    };

    // Internal initialization
    HRESULT InitializeBaseInfo(
        const void* pfVertexArray,
        size_t dwVertexCount,
        size_t dwVertexStride,
        DXGI_FORMAT IndexFormat,
        const void* pdwFaceIndexArray,
        size_t dwFaceCount,
        const FLOAT3* pfIMTArray,
        const uint32_t* pdwOriginalAjacency,
        const uint32_t* pdwSplitHint);

    bool IsMaxChartNumberValid(
        size_t MaxChartNumber);

    HRESULT ApplyInitEngine(
        CBaseMeshInfo &baseInfo, 
        DXGI_FORMAT IndexFormat,
        const void* pFaceIndexArray,
        bool bIsForPartition); 

    // Internal partiton
    HRESULT InitializeCurrentChartHeap();
    HRESULT AddChildrenToCurrentChartHeap(
        CIsochartMesh* pChart);

    HRESULT PartitionByGlobalAvgL2Stretch(
        size_t MaxChartNumber,
        float Stretch,
        size_t& ChartNumberOut,
        float& MaxChartStretchOut,
        uint32_t* pFaceAttributeIDOut);

    HRESULT ParameterizeChartsInHeap(
        bool bFirstTime,
        size_t MaxChartNumber);

    HRESULT GenerateNewChartsToParameterize();

    HRESULT OptimizeParameterizedCharts(
        float Stretch,		
        float& fFinalGeoAvgL2Stretch);

    float GetCurrentStretchCriteria();

    // ExportXXXX
    HRESULT ExportCurrentCharts(
        std::vector<CIsochartMesh*> &finalChartList,
        uint32_t* pFaceAttributeIDOut);

    HRESULT ExportIsochartResult(
        std::vector<CIsochartMesh*> &finalChartList,
        std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
        std::vector<uint8_t>* pvFaceIndexArrayOut,
        std::vector<uint32_t>* pvVertexRemapArrayOut,
        std::vector<uint32_t>* pvAttributeIDOut,
        std::vector<uint32_t>* pvAdjacencyOut);

    template <typename IndexType>
    void ExportPackResultToOrgMesh(
        IndexType *pOrigIndex,
        std::vector<CIsochartMesh*> &finalChartList);


    HRESULT PrepareExportBuffers(
        std::vector<CIsochartMesh*> &finalChartList,
        DXGI_FORMAT& outFormat,
        std::vector<uint32_t>& notUsedVertList,
        std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
        std::vector<uint8_t>* pvFaceIndexArrayOut,
        std::vector<uint32_t>* pvVertexRemapArrayOut,
        std::vector<uint32_t>* pvAttributeIDOut,
        std::vector<uint32_t>* pvAdjacencyOut);

    HRESULT FillExportVertexBuffer(
        std::vector<CIsochartMesh*> &finalChartList,
        std::vector<uint32_t>& notUsedVertList,
        std::vector<DirectX::UVAtlasVertex>* pvVertexBuffer,
        std::vector<uint32_t>* pvMapBuffer);

    template <class INDEXTYPE>
    HRESULT FillExportFaceIndexBuffer(
        std::vector<CIsochartMesh*> &finalChartList,
        std::vector<uint8_t>* pvFaceBuffer);

    HRESULT FillExportFaceAttributeBuffer(
        std::vector<CIsochartMesh*> &finalChartList,
        std::vector<uint32_t>* pvAttributeBuffer);

    HRESULT FillExportFaceAdjacencyBuffer(
        std::vector<CIsochartMesh*> &finalChartList,
        std::vector<uint32_t>* pvAdjacencyBuffer);

    void AssignUVCoordinate(
        std::vector<CIsochartMesh*> &finalChartList);
    // ReleaseXXXX
    // There are 3 chart-sets in isochart engine: Init, Current, Final
    // (1). Charts in "Current Set" must not in "Final Set" and vice versa
    // (2). Charts in "Init Set" can also in "Current Set" or "Final Set".
    // (3). To avoid delete more than one times of charts in "Init Set". We
    //      make sure these charts can only deleted by ReleaseInitCharts.
    void ReleaseInitialCharts();

    void ReleaseCurrentCharts();

    void ReleaseFinalCharts();

    // Following methods guarantee Isochart instance is running an exclusive 
    // task.
    // Before each public method begins to run, 1 step must be done:
    // (1). Try to enter exclusive section, if another thread has entered, 
    //		return error code to indicate busy.
    
    // After each public method complete its work, 1 step must done:
    // (1). Leave exclusive section.

    HRESULT TryEnterExclusiveSection();

    void  LeaveExclusiveSection();

    // Indicate whether to consider IMT
    bool IsIMTSpecified() const
    {
        return (m_baseInfo.pfIMTArray != 0);
    }

private:
    // Basic information needed for parameterization.
    CBaseMeshInfo m_baseInfo;
    
    // Manage callback operation.
    CCallbackSchemer m_callbackSchemer;
    
    // The charts to be partitioned
    CMaxHeap<float, CIsochartMesh*> m_currentChartHeap;

    // The charts not to be partitioned anymore
    std::vector<CIsochartMesh*> m_finalChartList;

    // The charts generated by Initialize()
    std::vector<CIsochartMesh*> m_initChartList;

    float fExpectAvgL2SquaredStretch;
    size_t dwExpectChartCount;

    EngineState m_state;	// Indicate internal state.
    
    HANDLE m_hMutex;	// Mutex 

    DWORD m_dwOptions ;

    friend CIsochartMesh ;
 };

// Following functions Check parameters of each public method.

// Check Initialize parameters
bool CheckInitializeParameters(
    const void* pVertexArray,
    size_t VertexCount,
    size_t VertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    size_t FaceCount,
    const FLOAT3* pIMTArray,
    DWORD dwOptions);

// Check Partition parameters
bool CheckPartitionParameters(
    size_t MaxChartNumber,
    size_t FaceCount,
    float Stretch,
    size_t* pChartNumberOut,
    float* pMaxStretchOut,		
    uint32_t* pFaceAttributeIDOut);

// Check Pack parameters
bool CheckPackParameters(
    size_t Width,
    size_t Height,
    float Gutter,
    std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut);

// Check SetCallback parameters
bool CheckSetCallbackParameters(
    LPISOCHARTCALLBACK pCallback,  
    float Frequency);

bool CheckExportPartitionResultParameters(
    std::vector<DirectX::UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut);


bool CheckInitializePackingParameters(
    std::vector<DirectX::UVAtlasVertex>* pvVertexBuffer,
    size_t VertexCount,
    std::vector<uint8_t>* pvFaceIndexBuffer,
    size_t FaceCount,
    const uint32_t* pdwFaceAdjacentArrayIn);

bool CheckIMTOptimizeParameters(
    std::vector<DirectX::UVAtlasVertex>* pvVertexBuffer,
    size_t VertexCount,
    std::vector<uint8_t>* pvFaceIndexBuffer,
    size_t FaceCount,
    const FLOAT3* pIMTArray);

}
