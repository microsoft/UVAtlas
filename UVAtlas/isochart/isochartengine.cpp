//-------------------------------------------------------------------------------------
// UVAtlas - Isochartengine.cpp
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

#include "pch.h"
#include "isochartengine.h"
#include "isochart.h"
#include "isochartmesh.h"

using namespace DirectX;
using namespace Isochart;

// Create instance of the class which implements the IIsochartEngine interface
IIsochartEngine* IIsochartEngine::CreateIsochartEngine()
{
    auto pEngine = new (std::nothrow) CIsochartEngine;
    if ( pEngine && FAILED(pEngine->CreateEngineMutex()))
    {
        delete pEngine;
        pEngine = nullptr;
    }
    return pEngine;
}

// Destroy the engine instance
void IIsochartEngine::ReleaseIsochartEngine(
    IIsochartEngine* pEngine)
{
    delete pEngine;
}


CIsochartEngine::CIsochartEngine():
    m_state(ISOCHART_ST_UNINITILAIZED),
    m_hMutex(nullptr),
    m_dwOptions( _OPTION_ISOCHART_DEFAULT )
{
}

CIsochartEngine::~CIsochartEngine()
{
    // if other thread is calling public method of CIsochartEngine this time, 
    // Free will return with "busy". So loop until free successfully.
    while(FAILED(Free())) 
    {
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
        // Busy-wait
#else
        SwitchToThread();
#endif
    }
    
    if (m_hMutex)
    {
        CloseHandle(m_hMutex);
    }
}

HRESULT CIsochartEngine::CreateEngineMutex()
{
    m_hMutex = CreateMutexEx(nullptr, nullptr, CREATE_MUTEX_INITIAL_OWNER, SYNCHRONIZE );
    if (!m_hMutex)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}
// ------------------------------------------------------------------------
// Initialize the isochart engine. Must be called before Partition, Optimize & Pack.
//-pMinChartNumber
//		size_t pointer. If specified, Initialize() pre-calculate the minimal
//		number of charts.In Partition(), MaxChartNumber should be always 
//		larger than MinChartNumber. Set to nullptr to skip pre-calculation. It
//		is faster for Initialization, but only used in "control by stretch
//		only" mode.
//-See Other Parameters at the comment lines of isochart(...)

HRESULT CIsochartEngine::Initialize(
    const void* pVertexArray,
    size_t VertexCount,
    size_t VertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    size_t FaceCount,
    const FLOAT3* pIMTArray,
    const uint32_t* pOriginalAjacency,
    const uint32_t* pSplitHint,
    DWORD dwOptions)
{
    DPF(1, "Initialize...");

    // 1. Check arguments and current state
    if (!CheckInitializeParameters(
        pVertexArray, 
        VertexCount, 
        VertexStride, 
        IndexFormat, 
        pFaceIndexArray, 
        FaceCount, 
        pIMTArray, 
        dwOptions))
    {
        return E_INVALIDARG;
    }

    m_dwOptions = dwOptions ;
    
    HRESULT hr = S_OK;	

    // 1. Check current state
    if (m_state != ISOCHART_ST_UNINITILAIZED)
    {
        return E_UNEXPECTED;
    }
    
    // 2. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }

    // 3. If engine is already initialized, return error code
    
    // 4. Prepare global basic information table. 
    if (FAILED(hr = InitializeBaseInfo(
                    pVertexArray, 
                    VertexCount, 
                    VertexStride, 
                    IndexFormat,
                    pFaceIndexArray, 
                    FaceCount, 
                    pIMTArray,
                    pOriginalAjacency,
                    pSplitHint)))
    {
        goto LEnd;
    }

    // 5. Internal initialization. Prepare the initial charts to be partitioned.
    if (FAILED(hr=ApplyInitEngine(
                    m_baseInfo, 
                    IndexFormat, 
                    pFaceIndexArray,
                    true)))
    {
        goto LEnd;
    }
    
    DPF(0, "Initially having %Iu separated charts", m_initChartList.size());
    
    // 6. If specified pMinChartNumber, perform precomputing
    m_state = ISOCHART_ST_INITIALIZED;
LEnd: 
    if (FAILED(hr))
    {
        Free();
        m_state = ISOCHART_ST_UNINITILAIZED;
    }
    
    LeaveExclusiveSection();
    
    return hr;
}

//  Release all buffers and reset CIsochartEngine
HRESULT CIsochartEngine::Free()
{
    HRESULT hr = S_OK;

    if (ISOCHART_ST_UNINITILAIZED == m_state)
    {
        return hr;
    }
    
    // 1. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }

    m_baseInfo.Free();
    ReleaseCurrentCharts();
    ReleaseFinalCharts();
    ReleaseInitialCharts();
    
    m_state = ISOCHART_ST_UNINITILAIZED;

    LeaveExclusiveSection();
    return hr;
}


//Partition by number or by stretch only. 
//Before calling this method, Initialize() should be called.
HRESULT CIsochartEngine::Partition(
    size_t MaxChartNumber,
    float Stretch,
    size_t& ChartNumberOut,
    float& MaxChartStretchOut,
    uint32_t* pFaceAttributeIDOut)
{
    DPF(1, "Partition....");

    HRESULT hr = S_OK;

    // 1.  Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }

    hr= PartitionByGlobalAvgL2Stretch(
        MaxChartNumber,
        Stretch,
        ChartNumberOut,
        MaxChartStretchOut,
        pFaceAttributeIDOut);

    if (FAILED(hr))
    {
        // If failed partition. Reset engine to initialized state
        ReleaseCurrentCharts();
        ReleaseFinalCharts();
        m_state = ISOCHART_ST_INITIALIZED;
    }
    else
    {
        m_state = ISOCHART_ST_PARTITIONED;
    }

    LeaveExclusiveSection();

    return hr;
}

// Check if MaxChartNumber is a valid value. 
bool CIsochartEngine::IsMaxChartNumberValid(
    size_t MaxChartNumber)
{
    if (MaxChartNumber != 0 &&
        (MaxChartNumber < m_initChartList.size() ||
        MaxChartNumber > m_baseInfo.dwFaceCount))
    {
        return false;
    }
    return true;
}

HRESULT CIsochartEngine::InitializeCurrentChartHeap()
{
    // 1. Prepare current chart list and finial chart list
    if (ISOCHART_ST_INITIALIZED != m_state)
    {
        // Partition has every been called. Need to do some clean.
        ReleaseCurrentCharts();
        ReleaseFinalCharts();
    }

    // Initialize current chart list. Charts in this list are candidates to be 
    // partitioned.
    for (size_t i=0; i<m_initChartList.size(); i++)
    {
        CIsochartMesh* pChart = m_initChartList[i];
        if (!m_currentChartHeap.insertData(pChart, 0))
        {
            return E_OUTOFMEMORY;	
        }
    }
    return S_OK;
}

HRESULT CIsochartEngine::ParameterizeChartsInHeap(
    bool bFirstTime,
    size_t MaxChartNumber)
{
    // 3.1 If Any charts needed to be partitioned
    while(!m_currentChartHeap.empty())
    {
        DPF(1,"Processed charts number is : %Iu", m_finalChartList.size()+m_currentChartHeap.size());		
        CIsochartMesh* pChart = m_currentChartHeap.cutTopData();
        assert(pChart != 0);
        _Analysis_assume_(pChart != 0);

        // Process current chart, if it's needed to be partitioned again, 
        // Just partition it.
        HRESULT hr = pChart->Partition();
        if (FAILED(hr))
        {
            return hr;
        }

        // If current chart has been partitoned, just children add to heap to be
        // processed later.
        if (pChart->HasChildren())
        {
            if (FAILED(hr=AddChildrenToCurrentChartHeap(pChart)))
            {
                delete pChart;
                return hr;
            }
            else
            {
                if (!pChart->IsInitChart())
                {
                    delete pChart;
                }
            }
        }
            
        // If A right parameterization (with acceptable face overturn) 
        // has been gotten, add current chart to final Chart List.
        else 
        {				
            try
            {
                m_finalChartList.push_back(pChart);
            }
            catch (std::bad_alloc&)
            {
                delete pChart;
                return E_OUTOFMEMORY;
            }

            if (bFirstTime)
            {
                if (FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt(pChart->GetFaceNumber())))
                    return hr;
            }
        }
    }		

    // 3.2 Update status
    if (bFirstTime)
    {
        HRESULT hr = m_callbackSchemer.FinishWorkAdapt();
        if ( FAILED(hr) )
            return hr;

        if (dwExpectChartCount > 0)
        {
            size_t dwStep = 0;
            if (MaxChartNumber > m_currentChartHeap.size()) 
            {
                dwStep = MaxChartNumber - m_currentChartHeap.size();
            }
            m_callbackSchemer.InitCallBackAdapt(dwStep, 0.70f, 0.40f);
        }
        else
        {
            m_callbackSchemer.InitCallBackAdapt(1, 0.40f, 0.40f);
        }
    }

    return S_OK;	
}

HRESULT CIsochartEngine::GenerateNewChartsToParameterize()
{
    CIsochartMesh* pChartWithMaxL2Stretch  = nullptr;
    uint32_t dwMaxIdx = 0;

    if (IsIMTSpecified())
    {
        float fMaxStretch;
        dwMaxIdx = CIsochartMesh::GetChartWidthLargestGeoAvgStretch(
            m_finalChartList,
            fMaxStretch);
    }
    else
    {
        dwMaxIdx = 
            CIsochartMesh::GetBestPartitionCanidate(m_finalChartList);
    }
    assert(INVALID_INDEX != dwMaxIdx);
            
    pChartWithMaxL2Stretch = m_finalChartList[dwMaxIdx];
    assert(pChartWithMaxL2Stretch != 0);
            
    HRESULT hr = pChartWithMaxL2Stretch->Bipartition3D();
    if (FAILED(hr))
    {
        return hr;
    }
    else
    {
        if (pChartWithMaxL2Stretch->HasChildren())
        {
            if (FAILED(
                hr=AddChildrenToCurrentChartHeap(pChartWithMaxL2Stretch)))
            {
                delete pChartWithMaxL2Stretch;
                return hr;
            }
            else
            {
                if (!pChartWithMaxL2Stretch->IsInitChart())
                {
                    delete pChartWithMaxL2Stretch;
                }
            }
        }				
    }
    m_finalChartList.erase(m_finalChartList.begin() + dwMaxIdx);
    return S_OK;
}

HRESULT CIsochartEngine::OptimizeParameterizedCharts(
    float Stretch,
    float& fFinalGeoAvgL2Stretch)
{
    HRESULT hr = S_OK;

    float fCurrAvgL2SquaredStretch;
    if (IsIMTSpecified())
    {		
        DPF(0, "Begin to optimize signal stretch");
        // Convert the input stretch to internal stretch,
        // When optimizeing IMT, more stretch is acceptable.
        CIsochartMesh::ConvertToInternalCriterion(
            Stretch, 
            fExpectAvgL2SquaredStretch,
            true);

        m_baseInfo.fExpectAvgL2SquaredStretch = fExpectAvgL2SquaredStretch;
        // Optimize siginal stretch, but don't break geometric stretch criterion
        FAILURE_RETURN(
        CIsochartMesh::OptimizeAllL2SquaredStretch(
            m_finalChartList, 
            true));

        // computer geometric stretch after optimize signal stretch
        CIsochartMesh::ComputeGeoAvgL2Stretch(
            m_finalChartList,
            true);

        // Compute average siginal stretch and use it to optimal scale each
        // chart to decrease total signal stretch. This step also can not break
        // geometric stretch criterion.
        fCurrAvgL2SquaredStretch = 
            CIsochartMesh::CalOptimalAvgL2SquaredStretch(
                m_finalChartList);		

        FAILURE_RETURN(
            CIsochartMesh::OptimalScaleChart(
                m_finalChartList, 
                fCurrAvgL2SquaredStretch,
                true));

        // Compute final geometric stretch
        fCurrAvgL2SquaredStretch = 
            CIsochartMesh::ComputeGeoAvgL2Stretch(
                m_finalChartList,
                false);
    }
    else
    {
        fCurrAvgL2SquaredStretch =
            CIsochartMesh::CalOptimalAvgL2SquaredStretch(m_finalChartList);		
        FAILURE_RETURN(
            CIsochartMesh::OptimalScaleChart(
                m_finalChartList, 
                fCurrAvgL2SquaredStretch,
                false));		
    }
    
    fFinalGeoAvgL2Stretch = fCurrAvgL2SquaredStretch;

    return hr;
}

float CIsochartEngine::GetCurrentStretchCriteria()
{
    if (IsIMTSpecified())
    {
        float fMaxStretch = 0;
        CIsochartMesh::GetChartWidthLargestGeoAvgStretch(
            m_finalChartList,
            fMaxStretch);

        return fMaxStretch;
    }
    else
    {
        return CIsochartMesh::CalOptimalAvgL2SquaredStretch(
            m_finalChartList);		
    }	
}


HRESULT CIsochartEngine::PartitionByGlobalAvgL2Stretch(
    size_t MaxChartNumber,
    float Stretch,
    size_t& ChartNumberOut,
    float& MaxChartStretchOut,
    uint32_t* pFaceAttributeIDOut)
{
    HRESULT hr = S_OK;

    // 1.  Check current state and parameter.
    if (ISOCHART_ST_UNINITILAIZED == m_state)
    {
        return E_UNEXPECTED;
    }

    if (!CheckPartitionParameters(
            MaxChartNumber, 
            m_baseInfo.dwFaceCount, 
            Stretch, 
            nullptr, 
            nullptr, 
            pFaceAttributeIDOut))
    {
        return E_INVALIDARG;
    }

    // 2. Prepare Internal criterion to stop partition.

    // 2.1 Stretch Criterion
    CIsochartMesh::ConvertToInternalCriterion(
        Stretch, 
        fExpectAvgL2SquaredStretch,
        false);

    m_baseInfo.fExpectAvgL2SquaredStretch = fExpectAvgL2SquaredStretch;
    
    // 2.2 Chart Number Criterion
    dwExpectChartCount = MaxChartNumber;

    // 3. Partition
    FAILURE_RETURN(InitializeCurrentChartHeap());
    float fCurrAvgL2SquaredStretch = INFINITE_STRETCH;

    m_callbackSchemer.InitCallBackAdapt(m_baseInfo.dwFaceCount, 0.40f, 0);

    bool bCountParition = true;
    bool bHasSatisfiedNumber = false;
    size_t dwLastChartNumber = 0;
    DPF(0, "Initial chart number %Iu\n", m_currentChartHeap.size());
    do
    {
        // 3.1. Generate initial parameterization for charts in current chart heap
        FAILURE_RETURN(
            ParameterizeChartsInHeap(bCountParition, MaxChartNumber));
        bCountParition = false;

        DPF(1,"Current charts number is : %Iu", m_finalChartList.size());

        // 3.2 Optimize all charts with right parameterization
        // chart 2d area will be compted in this function
        FAILURE_RETURN(
        CIsochartMesh::OptimizeAllL2SquaredStretch(
            m_finalChartList, 
            false));

        // 3.3 
        // For geometric case, get current optical average L^2 Squared Stretch
        // For signal case, get max average L^2 Squared stretch around the 
        // Charts
        fCurrAvgL2SquaredStretch = GetCurrentStretchCriteria();
    
        if (dwExpectChartCount != 0)
        {
            // 3.4 Reach the chart number criterion
            if (bHasSatisfiedNumber && 
                dwExpectChartCount <= m_finalChartList.size() )
            {
                break;
            }
            // 3.5 Break the chart number criterion
            if (dwExpectChartCount < m_finalChartList.size()
                && !bHasSatisfiedNumber)
            {
                ChartNumberOut = m_finalChartList.size();
                MaxChartStretchOut =
                    CIsochartMesh::ConvertToExternalStretch(
                        fCurrAvgL2SquaredStretch, false);
                DPF(0, "maximum chart number is too small to parameterize mesh.");
                return E_FAIL;
            }			
            bHasSatisfiedNumber = true;
        }

         // 3.6 If we don't reach the expected stretch criteria,
         // Selete a canidate to parition and parameterize the children.
        if (!CIsochartMesh::IsReachExpectedTotalAvgL2SqrStretch(
                fCurrAvgL2SquaredStretch,
                fExpectAvgL2SquaredStretch) 
            || m_finalChartList.size() < dwExpectChartCount)
        {
            FAILURE_RETURN(
                GenerateNewChartsToParameterize());
        }

        // 3.7 Update status
        if (dwExpectChartCount > 0)
        {
            size_t dwCurrentChartNumber (m_finalChartList.size() + m_currentChartHeap.size());
            hr = m_callbackSchemer.UpdateCallbackAdapt(dwCurrentChartNumber - dwLastChartNumber);
            dwLastChartNumber = dwCurrentChartNumber;
        }
        else
        {
            hr = m_callbackSchemer.UpdateCallbackDirectly(fExpectAvgL2SquaredStretch/fCurrAvgL2SquaredStretch);
        }
        FAILURE_RETURN(hr);
    }while(!m_currentChartHeap.empty());

    hr = m_callbackSchemer.FinishWorkAdapt();
    if ( FAILED(hr) )
        return hr;

    // 4. MergeChart
    if (m_finalChartList.size() > dwExpectChartCount)
    {
        DPF(0, "Charts before merge %Iu", m_finalChartList.size());
        dwLastChartNumber = m_finalChartList.size();
        m_callbackSchemer.InitCallBackAdapt((2 + m_finalChartList.size()), 0.20f, 0.80f);

        if (FAILED(
            hr = CIsochartMesh::MergeSmallCharts(
                    m_finalChartList,
                    dwExpectChartCount,
                    m_baseInfo,
                    m_callbackSchemer)))
        {
            return hr;
        }
        DPF(0, "Charts after merge %Iu", m_finalChartList.size());

        hr = m_callbackSchemer.FinishWorkAdapt();
        if ( FAILED(hr) )
            return hr;
    }

    // 5. Optimize parameterized charts.
    FAILURE_RETURN(
        OptimizeParameterizedCharts(Stretch, fCurrAvgL2SquaredStretch));
    
    // 6. Export current partition result by set the attribute id of each face 
    // in original mesh	
    if (pFaceAttributeIDOut)
    {
        hr = ExportCurrentCharts(
                m_finalChartList, 
                pFaceAttributeIDOut);
    }

    ChartNumberOut = m_finalChartList.size();
    MaxChartStretchOut =
            CIsochartMesh::ConvertToExternalStretch(
                    fCurrAvgL2SquaredStretch,
                    IsIMTSpecified());

    // detect closed surfaces which have not been correctly partitioned.
    for (size_t i = 0; i < m_finalChartList.size(); ++i)
    {
        if (m_finalChartList[i]->GetVertexNumber() > 0
            && !m_finalChartList[i]->HasBoundaryVertex())
        {
            DPF(0, "UVAtlas Internal error: Closed surface not correctly partitioned" );
            return E_FAIL;
        }
    }

    return hr;
}

HRESULT CIsochartEngine::AddChildrenToCurrentChartHeap(
    CIsochartMesh* pChart)
{
    HRESULT hr = S_OK;
    
    for (uint32_t i=0; i<pChart->GetChildrenCount(); i++)
    {
        CIsochartMesh* pChild = pChart->GetChild(i);
        assert( pChild != 0 );

        if (pChild->GetVertexNumber() == 4 && (pChild->GetVertexBuffer()[0]).dwIDInRootMesh == 228)
        {
            DPF(3, "hello...");
        }
        
        if (!m_currentChartHeap.insertData(pChild, 0))
        {
            return E_OUTOFMEMORY;
        }
    }
    pChart->UnlinkAllChildren();
    return hr;
}


// ----------------------------------------------------------------------------
//  function    Pack
//
//   Description:   Generate UV-atlas.
//
//   returns    S_OK if successful, else failure code
//

HRESULT CIsochartEngine::Pack(
    size_t Width,
    size_t Height,
    float Gutter,
    const void* pOrigIndexBuffer,
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    _In_opt_ std::vector<uint32_t>* pvAttributeID)
{
    DPF(1, "Packing Charts...");
    if (!CheckPackParameters(
        Width,  Height, Gutter,
        pvVertexArrayOut,
        pvFaceIndexArrayOut,
        pvVertexRemapArrayOut,
        pvAttributeID))
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    if (ISOCHART_ST_PACKED == m_state)
    {	
        return S_OK;
    }

    if (ISOCHART_ST_PARTITIONED != m_state)
    {
        DPF(0, "Need to partition");
        return E_FAIL;
    }

    // 1. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }
        
    m_callbackSchemer.InitCallBackAdapt( m_finalChartList.size() + 1, 0.95f, 0);

    if (FAILED(hr = CIsochartMesh::PackingCharts(
        m_finalChartList, 
        Width, 
        Height, 
        Gutter, 
        m_callbackSchemer)))
    {
        goto LEnd;
    }

    if (FAILED(hr=m_callbackSchemer.FinishWorkAdapt()))
    {
        goto LEnd;
    }

    m_callbackSchemer.InitCallBackAdapt(3, 0.05f, 0.95f);

    // Need to adjust vertex order
    if (pvVertexRemapArrayOut)
    {
        // this is probably broken, but the code path doesn't hit it
        hr = ExportIsochartResult(
                m_finalChartList, 
                pvVertexArrayOut, 
                pvFaceIndexArrayOut, 
                pvVertexRemapArrayOut, 
                nullptr,
                nullptr);		
    }
    else
    {		
        if( m_baseInfo.IndexFormat == DXGI_FORMAT_R16_UINT )
            ExportPackResultToOrgMesh<uint16_t>(
                    const_cast<uint16_t*>( reinterpret_cast<const uint16_t*>(pOrigIndexBuffer) ),
                    m_finalChartList);
        else
            ExportPackResultToOrgMesh<uint32_t>(
                    const_cast<uint32_t*>( reinterpret_cast<const uint32_t*>(pOrigIndexBuffer) ),
                    m_finalChartList);
    }

    if (FAILED(hr))
    {
        goto LEnd;
    }

    hr = m_callbackSchemer.FinishWorkAdapt();

    m_state = ISOCHART_ST_PACKED;

LEnd:
    LeaveExclusiveSection();
    return hr;
}

template <typename IndexType>
void CIsochartEngine::ExportPackResultToOrgMesh(
    IndexType *pOrigIndex,
    std::vector<CIsochartMesh*> &finalChartList)
{
    uint8_t* pVertex
        = reinterpret_cast<uint8_t*>(
        const_cast<void*>(m_baseInfo.pVertexArray));
    assert(m_baseInfo.dwVertexStride >= sizeof(UVAtlasVertex));

    for (size_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        const ISOCHARTFACE* pChartFaces = pChart->GetFaceBuffer();
        const ISOCHARTVERTEX* pChartVerts = pChart->GetVertexBuffer();
        
        for (size_t j = 0; j< pChart->GetFaceNumber(); j++)
        {
            for( size_t k = 0; k < 3; k++ )
            {
                size_t uId = pChartFaces[j].dwVertexID[k];
                size_t uOrigId = pOrigIndex[pChartFaces[j].dwIDInRootMesh * 3 + k];
                auto pVertexOut = reinterpret_cast<UVAtlasVertex*>(
                    pVertex +
                    m_baseInfo.dwVertexStride*uOrigId);

                pVertexOut->uv = pChartVerts[uId].uv;
            }
        }
    }
}

// -------------------------------------------------------------------------------
//  function    SetCallback
//
//   Description:   set callback function.
//
//   returns    S_OK if successful, else failure code
//
HRESULT CIsochartEngine::SetCallback(
    LPISOCHARTCALLBACK pCallback,  
    float Frequency)
{
    if (!CheckSetCallbackParameters(
        pCallback,
        Frequency))
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    
    // 1. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }

    m_callbackSchemer.SetCallback(
        pCallback, 
        Frequency);
    
    LeaveExclusiveSection();

    return hr;
}

HRESULT CIsochartEngine::SetStage(
    unsigned int TotalStageCount,
    unsigned int DoneStageCount)
{
    if (TotalStageCount < DoneStageCount)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    
    // 1. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }

    m_callbackSchemer.SetStage(
        TotalStageCount, 
        DoneStageCount);
    
    LeaveExclusiveSection();

    return hr;

}

HRESULT CIsochartEngine::ExportPartitionResult(
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut)
{

    if (!CheckExportPartitionResultParameters(
        pvVertexArrayOut,
        pvFaceIndexArrayOut,
        pvVertexRemapArrayOut,pvAttributeIDOut, pvAdjacencyOut))
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    
    // 1. Try to enter exclusive section
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }
    
    hr = ExportIsochartResult(
            m_finalChartList, 
            pvVertexArrayOut, 
            pvFaceIndexArrayOut, 
            pvVertexRemapArrayOut, 
            pvAttributeIDOut,
            pvAdjacencyOut);

    LeaveExclusiveSection();
    
    return hr;
}

HRESULT CIsochartEngine::InitializePacking(
    std::vector<UVAtlasVertex>* pvVertexBuffer,
    size_t VertexCount,
    std::vector<uint8_t>* pvFaceIndexBuffer,
    size_t FaceCount,
    const uint32_t* pdwFaceAdjacentArrayIn)
{
    if (!CheckInitializePackingParameters(
        pvVertexBuffer,
        VertexCount,
        pvFaceIndexBuffer,
        FaceCount,
        pdwFaceAdjacentArrayIn))
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    if (FAILED(hr = TryEnterExclusiveSection()))
    {
        return hr;
    }
    
    Free();

    size_t dwVertexStride = sizeof(UVAtlasVertex);

    DXGI_FORMAT IndexFormat = 
        (pvFaceIndexBuffer->size() / FaceCount == sizeof(uint32_t) * 3) ?
        DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    
    if(FAILED(hr = m_baseInfo.Initialize(
        pvVertexBuffer->data(), 
        VertexCount, 
        dwVertexStride,
        FaceCount,
        pdwFaceAdjacentArrayIn)))
    {
        goto LEnd;
    }

    m_baseInfo.IndexFormat = IndexFormat;

    if (FAILED(hr=ApplyInitEngine(
        m_baseInfo, 
        IndexFormat, 
        pvFaceIndexBuffer->data(),
        false)))
    {
        goto LEnd;
    }

    try
    {
        m_finalChartList.insert(m_finalChartList.end(), m_initChartList.cbegin(), m_initChartList.cend());
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
        goto LEnd;
    }
    m_initChartList.clear();

    AssignUVCoordinate(m_finalChartList);
    
    m_state = ISOCHART_ST_PARTITIONED;
LEnd:
    LeaveExclusiveSection();
    return hr;
}


/////////////////////////////////////////////////////////////////////
//////////////////  Private Methods ////////////////////////////////////
////////////////////////////////////////////////////////////////////


void CIsochartEngine::AssignUVCoordinate(
    std::vector<CIsochartMesh*> &finalChartList)
{
    auto pVertex = reinterpret_cast<const uint8_t*>(m_baseInfo.pVertexArray);
    assert(m_baseInfo.dwVertexStride >= sizeof(UVAtlasVertex));

    for (size_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        ISOCHARTVERTEX* pChartVertexBuffer = pChart->GetVertexBuffer();
        
        for (size_t j = 0; j< pChart->GetVertexNumber(); j++)
        {
            auto pVertexIn = reinterpret_cast<const UVAtlasVertex*>(
                pVertex +
                m_baseInfo.dwVertexStride*pChartVertexBuffer[j].dwIDInRootMesh);
            
            pChartVertexBuffer[j].uv.x = pVertexIn->uv.x;
            pChartVertexBuffer[j].uv.y = pVertexIn->uv.y;
        }
        pChart->SetParameterizedChart();
    }
}


HRESULT CIsochartEngine::InitializeBaseInfo(
    const void* pfVertexArray,
    size_t dwVertexCount,
    size_t dwVertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pdwFaceIndexArray,
    size_t dwFaceCount,
    const FLOAT3* pfIMTArray,
    const uint32_t* pdwOriginalAjacency,
    const uint32_t* pSplitHint)
{
    HRESULT hr = S_OK;
    
    m_callbackSchemer.InitCallBackAdapt(1, 0.05f, 0);
    
    if (FAILED(hr = m_baseInfo.Initialize(
                    pfVertexArray, 
                    dwVertexCount, 
                    dwVertexStride, 
                    IndexFormat,
                    pdwFaceIndexArray, 
                    dwFaceCount, 
                    pfIMTArray,
                    pdwOriginalAjacency,
                    pSplitHint)))
    {
        return hr;
    }

    if (FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt(1))) 
    {
        return hr;
    }
    
    hr = m_callbackSchemer.FinishWorkAdapt();

    return hr;
}


// -----------------------------------------------------------------------------
//  function   ApplyInitEngine 
//
//   Description:  
//    Internal function to initialize engine: 
//    (1) Check and separate multiple objects in the input mesh.Results are initial charts
//    (2) Check and cut multiple boundaries of initial charts.
//    (3) Caculated vertex importance order for each initial charts
//   returns   S_OK if successful, else failure code

HRESULT CIsochartEngine::ApplyInitEngine(
    CBaseMeshInfo &baseInfo,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    bool bIsForPartition)
{
    HRESULT hr = S_OK;
    
    // 1. Build Root Chart
    auto pRootChart = new (std::nothrow) CIsochartMesh(baseInfo, m_callbackSchemer, *this);
    if (!pRootChart)
    {
        return E_OUTOFMEMORY;
    }

    m_callbackSchemer.InitCallBackAdapt(4, 0.05f, 0.05f);
    hr = CIsochartMesh::BuildRootChart(
        baseInfo, 
        pFaceIndexArray,
        IndexFormat, 
        pRootChart,
        bIsForPartition);
    
    if (FAILED(hr))
    {
        if (hr != E_OUTOFMEMORY)
        {
            DPF(3, "Build Full Connection Faild, Non-manifold...");
        }
        delete pRootChart;
        return hr;
    }	

    if (FAILED(hr=m_callbackSchemer.FinishWorkAdapt()))
    {
        delete pRootChart;
        return hr;
    }

    // 2. Separate unconnected charts from original mesh. For each chart, 
    // Caculate Vertices importance

    m_callbackSchemer.InitCallBackAdapt(
        baseInfo.dwVertexCount*2+pRootChart->GetEdgeNumber(), 0.9f, 0.10f);

    m_currentChartHeap.SetManageMode(AUTOMATIC);
    if (!m_currentChartHeap.insertData(pRootChart, 0))
    {
        delete pRootChart;
        return E_OUTOFMEMORY;
    }
    size_t dwTestVertexCount = 0;
    size_t dwTestFaceCount = 0;
    while(!m_currentChartHeap.empty())
    {
        CIsochartMesh* pChart = m_currentChartHeap.cutTopData();
        assert(pChart != 0);
        _Analysis_assume_(pChart != 0);
        assert(!pChart->IsImportanceCaculationDone());

        if (FAILED(hr = pChart->PrepareProcessing(bIsForPartition)))
        {
            delete pChart;
            return hr;
        }

        DPF(3, "Separate to %Iu sub-charts", pChart->GetChildrenCount());
        // if original mesh has multiple sub-charts or current chart 
        // has multiple boundaies it will generate children.
        
        if (pChart->HasChildren())
        {
            for (uint32_t i = 0; i<pChart->GetChildrenCount(); i++)
            {
                CIsochartMesh* pChild = pChart->GetChild(i);
                assert( pChild != 0 );
                assert(!pChild->IsImportanceCaculationDone());

                if(!m_currentChartHeap.insertData(pChild, 0))
                {
                    delete pChart;
                    return E_OUTOFMEMORY;
                }
                pChart->UnlinkChild(i);
            }
            delete pChart;
        }
        else
        {
            assert(pChart->IsImportanceCaculationDone()
                || !bIsForPartition);
            try
            {
                m_initChartList.push_back(pChart);
            }
            catch(std::bad_alloc&)
            {
                delete pChart;
                return E_OUTOFMEMORY;
            }
            dwTestVertexCount += pChart->GetVertexNumber();
            dwTestFaceCount += pChart->GetFaceNumber();
        }
    }

    DPF(3, "Old Vert Number is %Iu, New Vert Number is %Iu",
        baseInfo.dwVertexCount,
        dwTestVertexCount);
    DPF(3, "Old Face Number is %Iu, New Face Number is %Iu",
        baseInfo.dwFaceCount,
        dwTestFaceCount);

    hr = m_callbackSchemer.FinishWorkAdapt();
    
    return hr;
}

// ----------------------------------------------------------------------------
//  function    ReleaseCurrentCharts 
//
//   Description:  release charts in current chart list. 
//   returns    
//
void CIsochartEngine::ReleaseCurrentCharts()
{
    while(!m_currentChartHeap.empty())
    {
        CIsochartMesh* pChart = m_currentChartHeap.cutTopData();
        assert(pChart != 0);
        _Analysis_assume_(pChart != 0);
        // Don't delete charts that also in init chart list here.
        if (!pChart->IsInitChart()) 
        {
            delete pChart;
        }
    }	
}

// ----------------------------------------------------------------------------
//  function   ReleaseFinalCharts 
//
//   Description:  release charts in final chart list. 

void CIsochartEngine::ReleaseFinalCharts()
{
    for (size_t i=0; i<m_finalChartList.size(); i++)
    {
        CIsochartMesh* pChart = m_finalChartList[i];
        // Don't delete charts that also in init chart list here.
        if (pChart && !pChart->IsInitChart())
        {
            delete pChart;
        }
        
    }
    m_finalChartList.clear();
}

// -----------------------------------------------------------------------------
//  function    ReleaseInitialCharts 
//
//   Description:  release charts in init chart list. 
//   returns    

void CIsochartEngine::ReleaseInitialCharts()
{
    for (size_t i=0; i<m_initChartList.size(); i++)
    {
        CIsochartMesh* pChart = m_initChartList[i];
        if( pChart )
        {
            assert(pChart->IsInitChart()); // Here, delete the init charts.
            delete pChart;
        }
    }
    m_initChartList.clear();
}


// -----------------------------------------------------------------------------
//  function    ExportCurrentCharts 
//
//  Description:  
//    Export current partition result by set face attribute for each face in 
//    original mesh.
//  returns    S_OK if successful, else failure code 
//
HRESULT CIsochartEngine::ExportCurrentCharts(
    std::vector<CIsochartMesh*> &finalChartList,	
    uint32_t* pFaceAttributeIDOut)
{
    for (uint32_t i = 0; i<finalChartList.size(); i++)
    {
        CIsochartMesh* pChart = finalChartList[i];
        assert(pChart != 0);

        ISOCHARTFACE* pChartFaceBuffer = pChart->GetFaceBuffer();

        for (size_t j = 0; j< pChart->GetFaceNumber(); j++)
        {
            assert(pChartFaceBuffer->dwIDInRootMesh 
                < m_baseInfo.dwFaceCount);
            pFaceAttributeIDOut[pChartFaceBuffer->dwIDInRootMesh] = i;
            pChartFaceBuffer++;
        }
    }

    return S_OK;
}


// ----------------------------------------------------------------------------
//  function    ExportIsochartResult 
//
//   Description:  Export final result
//   returns    S_OK if successful, else failure code 
//
 HRESULT CIsochartEngine::ExportIsochartResult(
    std::vector<CIsochartMesh*> &finalChartList,
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut)
{
    assert(pvVertexArrayOut != 0);
    assert(pvFaceIndexArrayOut != 0);
    assert(pvVertexRemapArrayOut != 0);

    DPF(3,"Export Isochart Result...");

    HRESULT hr = S_OK;
    
    
    DXGI_FORMAT outFormat = m_baseInfo.IndexFormat;
    std::vector<uint32_t> notUsedVertList;
    // 1. create all output buffers.
    hr = PrepareExportBuffers(
            finalChartList, 
            outFormat, 
            notUsedVertList,
            pvVertexArrayOut, 
            pvFaceIndexArrayOut, 
            pvVertexRemapArrayOut, 
            pvAttributeIDOut,
            pvAdjacencyOut);
    
    if (FAILED(hr))
    {
        goto LFail;
    }
    if (FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt(1)))
    {
        goto LFail;
    }

    // 2. Fill in output vertex buffer and vertex map buffer
    hr = FillExportVertexBuffer(
            finalChartList, 
            notUsedVertList,
            pvVertexArrayOut, 
            pvVertexRemapArrayOut);
    
    if (FAILED(hr))
    {
        goto LFail;
    }
    if (FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt(1)))
    {
        goto LFail;
    }
    notUsedVertList.clear();

    // 3 Fill in the output face index buffer
    if (DXGI_FORMAT_R16_UINT == outFormat)
    {
        hr = FillExportFaceIndexBuffer<uint16_t>(
                finalChartList, 
                pvFaceIndexArrayOut);
    }
    else
    {
        hr = FillExportFaceIndexBuffer<uint32_t>(
                finalChartList, 
                pvFaceIndexArrayOut);
    }		
    if (FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt(1)))
    {
        goto LFail;
    }

    // 4 Fill in the output face attribute buffer
    if (pvAttributeIDOut)
    {
        if (FAILED(hr = FillExportFaceAttributeBuffer(
                finalChartList, 
                pvAttributeIDOut)))
        {
            goto LFail;
        }
    }
    
    if (pvAdjacencyOut)
    {
        if (FAILED(hr = FillExportFaceAdjacencyBuffer(
                finalChartList, 
                pvAdjacencyOut)))
        {
            goto LFail;
        }
    }
    
    return S_OK;

LFail:
    pvVertexArrayOut->clear();
    pvFaceIndexArrayOut->clear();
    pvVertexRemapArrayOut->clear();

    if (pvAttributeIDOut)
    {
        pvAttributeIDOut->clear();
    }
    return hr;
}

HRESULT CIsochartEngine::PrepareExportBuffers(
    std::vector<CIsochartMesh*> &finalChartList,
    DXGI_FORMAT& outFormat,
    std::vector<uint32_t>& notUsedVertList,
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut)
{
    HRESULT hr = S_OK;
    
    assert(pvVertexArrayOut != 0);
    assert(pvFaceIndexArrayOut != 0);
    assert(pvVertexRemapArrayOut != 0);
    
    pvVertexArrayOut->clear();
    pvFaceIndexArrayOut->clear();
    pvVertexRemapArrayOut->clear();

    // 1. Compute the output vertices number 
    std::unique_ptr<bool[]> rgbVertUsed( new (std::nothrow) bool[m_baseInfo.dwVertexCount] );
    if (!rgbVertUsed)
    {
        return E_OUTOFMEMORY;
    }

    memset(rgbVertUsed.get(), 0, sizeof(bool)*m_baseInfo.dwVertexCount);

    try
    {
        size_t dwVertCount = 0;
        for (size_t i=0; i < finalChartList.size(); i++)
        {
            dwVertCount += finalChartList[i]->GetVertexNumber();
            ISOCHARTVERTEX* pVert = finalChartList[i]->GetVertexBuffer();
            for (size_t j = 0; j < finalChartList[i]->GetVertexNumber(); j++)
            {
                rgbVertUsed[pVert[j].dwIDInRootMesh] = true;
            }
        }
        for (uint32_t i = 0; i < m_baseInfo.dwVertexCount; i++)
        {
            if (!rgbVertUsed[i])
            {
                notUsedVertList.push_back(i);
            }
        }
        dwVertCount += notUsedVertList.size();

        rgbVertUsed.reset();

        if (DXGI_FORMAT_R16_UINT == m_baseInfo.IndexFormat &&
            dwVertCount > 0x0000ffff)
        {
            DPF(0, "Resulting mesh is too large to fit in 16-bit mesh.");
            return E_FAIL;
        }

        pvVertexArrayOut->resize(dwVertCount);

        // 2. Create output attribute buffer for each face.
        if (pvAttributeIDOut)
        {
            pvAttributeIDOut->resize(m_baseInfo.dwFaceCount);
        }

        if (pvAdjacencyOut)
        {
            pvAdjacencyOut->resize(m_baseInfo.dwFaceCount * 3);
        }

        // 3. Decide the output face index format.
        outFormat = m_baseInfo.IndexFormat;

        // 4. Create output face index buffer
        if (DXGI_FORMAT_R32_UINT == outFormat)
        {
            pvFaceIndexArrayOut->resize(3 * m_baseInfo.dwFaceCount * sizeof(uint32_t));
        }
        else
        {
            pvFaceIndexArrayOut->resize(3 * m_baseInfo.dwFaceCount * sizeof(uint16_t));
        }

        // 5. Create vertices map buffer.
        pvVertexRemapArrayOut->resize(dwVertCount);
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    return hr;
}

HRESULT CIsochartEngine::FillExportVertexBuffer(
    std::vector<CIsochartMesh*> &finalChartList,
    std::vector<uint32_t>& notUsedVertList,
    std::vector<UVAtlasVertex>* pvVertexBuffer,
    std::vector<uint32_t>* pvMapBuffer)
{
    assert(pvVertexBuffer != 0);
    assert(pvMapBuffer != 0);
    auto pVertex = reinterpret_cast<const uint8_t*>(m_baseInfo.pVertexArray);
    assert(m_baseInfo.dwVertexStride >= sizeof(XMFLOAT3));

    auto pVertexOut = pvVertexBuffer->data();

    uint32_t* pdwBaseMap = nullptr;
    uint32_t* pdwMap = nullptr;
    pdwBaseMap = pdwMap = pvMapBuffer->data();

    for (size_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        const ISOCHARTVERTEX* pChartVertexBuffer = pChart->GetVertexBuffer();
        
        for (size_t j = 0; j< pChart->GetVertexNumber(); j++)
        {
            auto pVertexIn = reinterpret_cast<const XMFLOAT3*>(
                static_cast<const void *>(
                pVertex +
                m_baseInfo.dwVertexStride*pChartVertexBuffer[j].dwIDInRootMesh));
            
            *pdwMap = pChartVertexBuffer[j].dwIDInRootMesh;
            pVertexOut->pos.x = pVertexIn->x;
            pVertexOut->pos.y = pVertexIn->y;
            pVertexOut->pos.z = pVertexIn->z;
            pVertexOut->uv.x = pChartVertexBuffer[j].uv.x;
            pVertexOut->uv.y = pChartVertexBuffer[j].uv.y;

            pVertexOut++;
            pdwMap++;
        }
    }

    // Export isolated vertices.
    for (size_t ii=0; ii<notUsedVertList.size(); ii++)
    {
        uint32_t dwIDInOriginalMesh = notUsedVertList[ii];

        auto pVertexIn = static_cast<const XMFLOAT3*>(
            static_cast<const void *>(
            pVertex +
            m_baseInfo.dwVertexStride*dwIDInOriginalMesh));

        *pdwMap = dwIDInOriginalMesh;		
        pVertexOut->pos.x = pVertexIn->x;
        pVertexOut->pos.y = pVertexIn->y;
        pVertexOut->pos.z = pVertexIn->z;
        pVertexOut->uv.x = 1.f;
        pVertexOut->uv.y = 1.f;
        pVertexOut++;
        pdwMap++;
    }
    
    return S_OK;
    
}

template <class INDEXTYPE>
HRESULT CIsochartEngine::FillExportFaceIndexBuffer(
    std::vector<CIsochartMesh*> &finalChartList,
    std::vector<uint8_t>* pvFaceBuffer)
{
    assert(pvFaceBuffer != 0);

    uint32_t dwFaceId = 0;
    size_t dwOffset = 0;

    auto pBaseFaces = reinterpret_cast<INDEXTYPE*>(pvFaceBuffer->data());

    INDEXTYPE* pFaces;
    for (size_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        const ISOCHARTFACE* pChartFaceBuffer = pChart->GetFaceBuffer();
        for (size_t j = 0; j< pChart->GetFaceNumber(); j++)
        {
            pFaces = pBaseFaces + pChartFaceBuffer[j].dwIDInRootMesh*3;
            pFaces[0]
                = static_cast<INDEXTYPE>(pChartFaceBuffer[j].dwVertexID[0]
                + dwOffset);

            pFaces[1]
                = static_cast<INDEXTYPE>(pChartFaceBuffer[j].dwVertexID[1]
                + dwOffset);

            pFaces[2]
                = static_cast<INDEXTYPE>(pChartFaceBuffer[j].dwVertexID[2]
                + dwOffset);

            dwFaceId++;
        }
        dwOffset += pChart->GetVertexNumber();
    }

    assert( dwFaceId == m_baseInfo.dwFaceCount);

    return S_OK;
}

HRESULT CIsochartEngine::FillExportFaceAttributeBuffer(
    std::vector<CIsochartMesh*> &finalChartList,
    std::vector<uint32_t>* pvAttributeBuffer)
{
    assert(pvAttributeBuffer != 0);
    
    uint32_t* pAttributeID = pvAttributeBuffer->data();

    uint32_t dwFaceID = 0;
    
    for (uint32_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        const ISOCHARTFACE* pChartFaceBuffer = pChart->GetFaceBuffer();
        for (uint32_t j = 0; j< pChart->GetFaceNumber(); j++)
        {
            dwFaceID = pChartFaceBuffer[j].dwIDInRootMesh;
            pAttributeID[dwFaceID] = i;
        }		
    }	

    return S_OK;
}

HRESULT CIsochartEngine::FillExportFaceAdjacencyBuffer(
    std::vector<CIsochartMesh*> &finalChartList,
    std::vector<uint32_t>* pvAdjacencyBuffer)
{
    assert(pvAdjacencyBuffer != 0);
    
    uint32_t* pdwAdj = pvAdjacencyBuffer->data();

    uint32_t dwFaceID = 0;
    
    for (size_t i=0; i<finalChartList.size(); i++)
    {
        CIsochartMesh *pChart = finalChartList[i];
        const ISOCHARTFACE* pChartFaces = pChart->GetFaceBuffer();
        auto& pChartEdges = pChart->GetEdgesList();
        for (size_t j = 0; j< pChart->GetFaceNumber(); j++)
        {
            dwFaceID = pChartFaces[j].dwIDInRootMesh;
            for (size_t k = 0; k < 3; k++)
            {
                ISOCHARTEDGE &pEdge = pChartEdges[pChartFaces[j].dwEdgeID[k]];
                if( pEdge.bIsBoundary )
                {
                    pdwAdj[dwFaceID * 3 + k] = uint32_t(-1);
                    if( !pEdge.bCanBeSplit )
                    {
                        DPF(0, "UVAtlas Internal error: Made non-splittable edge a boundary edge");
                        return E_FAIL;
                    }
                }
                else if( pEdge.dwFaceID[0] == j )
                    pdwAdj[dwFaceID*3 + k] = pChartFaces[pEdge.dwFaceID[1]].dwIDInRootMesh;
                else
                    pdwAdj[dwFaceID*3 + k] = pChartFaces[pEdge.dwFaceID[0]].dwIDInRootMesh;
            }
        }		
    }	

    return S_OK;
}

HRESULT CIsochartEngine::TryEnterExclusiveSection()
{
    // Other thread is using this object. 
    if (WaitForSingleObjectEx(m_hMutex, 0, FALSE) == WAIT_OBJECT_0)
    {
        return S_OK;
    }
    else
    {
        return E_ABORT;
    }
}

void  CIsochartEngine::LeaveExclusiveSection()
{
    if (m_hMutex)
    {
        ReleaseMutex(m_hMutex);
    }

}


bool Isochart::CheckInitializeParameters(
    const void* pVertexArray,
    size_t VertexCount,
    size_t VertexStride,
    DXGI_FORMAT IndexFormat,
    const void* pFaceIndexArray,
    size_t FaceCount,
    const FLOAT3* pIMTArray,
    DWORD dwOptions)	
{
    UNREFERENCED_PARAMETER(VertexCount);
    UNREFERENCED_PARAMETER(FaceCount);
    UNREFERENCED_PARAMETER(pIMTArray);

    if ( (dwOptions & _OPTION_ISOCHART_GEODESIC_FAST) && (dwOptions & _OPTION_ISOCHART_GEODESIC_QUALITY) )
        return false ;
    
    // 1. Vertex buffer
    if (!pVertexArray)
    {
        return false;
    }
    if (VertexStride < sizeof(float)*3)
    {
        return false;
    }
    // 2. Face buffer
    size_t dwFaceIndexSize = 0;
    if (IndexFormat == DXGI_FORMAT_R16_UINT)
    {
        dwFaceIndexSize = sizeof(uint16_t)*3;
    }
    else if (IndexFormat == DXGI_FORMAT_R32_UINT)
    {
        dwFaceIndexSize = sizeof(uint32_t) * 3;
    }
    else
    {
        return false;
    }
        
    if (!pFaceIndexArray)
    {
        return false;
    }

    return true;
}


bool Isochart::CheckPartitionParameters(
    size_t MaxChartNumber,
    size_t FaceCount,
    float Stretch,
    size_t* pChartNumberOut,
    float* pMaxStretchOut,
    uint32_t* pFaceAttributeIDOut)
{	
    UNREFERENCED_PARAMETER(pMaxStretchOut);
    UNREFERENCED_PARAMETER(pChartNumberOut);
    UNREFERENCED_PARAMETER(pFaceAttributeIDOut);

    // 4. MaxChartNumber must <= FaceCount
    if (MaxChartNumber > FaceCount)
    {
        return false;
    }

    // 5. Stretch must between 0.0 ~ 1.0
    if (Stretch > 1.0f || Stretch < 0.0f)
    {
        return false;
    }

    return true;
}

bool Isochart::CheckPackParameters(
    size_t Width,
    size_t Height,
    float Gutter,
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeID)
{
    UNREFERENCED_PARAMETER(pvVertexRemapArrayOut);
    UNREFERENCED_PARAMETER(pvAttributeID);

    // 6. Width , Height must larger than 0. Gutter must not be a negative
    if (Width == 0 || Height == 0 || Gutter < 0)
    {
        return false;
    }
    
    if (!pvVertexArrayOut )
    {
        return false;
    }

    if (!pvFaceIndexArrayOut)
    {
        return false;
    }

    return true;
}

bool Isochart::CheckSetCallbackParameters(
        LPISOCHARTCALLBACK pCallback,  
        float Frequency)
{
    UNREFERENCED_PARAMETER(pCallback);

    if (Frequency < 0 || Frequency > 1.0f)
    {
        return false;
    }

    return true;
}

bool Isochart::CheckExportPartitionResultParameters(
    std::vector<UVAtlasVertex>* pvVertexArrayOut,
    std::vector<uint8_t>* pvFaceIndexArrayOut,
    std::vector<uint32_t>* pvVertexRemapArrayOut,
    std::vector<uint32_t>* pvAttributeIDOut,
    std::vector<uint32_t>* pvAdjacencyOut)
{
    UNREFERENCED_PARAMETER(pvAttributeIDOut);
    UNREFERENCED_PARAMETER(pvAdjacencyOut);

    if (!pvVertexArrayOut)
    {
        return false;
    }

    if (!pvFaceIndexArrayOut)
    {
        return false;
    }

    if (!pvVertexRemapArrayOut)
    {
        return false;
    }

    return true;
}

bool Isochart::CheckInitializePackingParameters(
    std::vector<UVAtlasVertex>* pvVertexBuffer,
    size_t VertexCount,
    std::vector<uint8_t>* pvFaceIndexBuffer,
    size_t FaceCount,
    const uint32_t* pdwFaceAdjacentArrayIn)
{
    if ( !pvVertexBuffer )
    {
        return false;
    }

    if (!pvFaceIndexBuffer )
    {
        return false;
    }

    if (VertexCount == 0 || FaceCount == 0)
    {
        return false;
    }

    if (!pdwFaceAdjacentArrayIn)
    {
        return false;
    }

    return true;
}

bool Isochart::CheckIMTOptimizeParameters(
    std::vector<UVAtlasVertex>* pvVertexBuffer,
    size_t VertexCount,
    std::vector<uint8_t>* pvFaceIndexBuffer,
    size_t FaceCount,
    const FLOAT3* pIMTArray)
{
    if (!pvVertexBuffer)
    {
        return false;
    }

    if (!pvFaceIndexBuffer)
    {
        return false;
    }

    if (VertexCount == 0 || FaceCount == 0)
    {
        return false;
    }

    if (!pIMTArray)
    {
        return false;
    }

    return true;
}
