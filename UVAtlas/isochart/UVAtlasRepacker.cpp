//-------------------------------------------------------------------------------------
// UVAtlas - UVAtlasRepacker.cpp
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

#include "uvatlasrepacker.h"
#include "UVAtlas.h"

using namespace DirectX;
using namespace Isochart;
using namespace IsochartRepacker;

_Use_decl_annotations_
HRESULT WINAPI IsochartRepacker::isochartpack2(std::vector<UVAtlasVertex>* pvVertexArray,
                             size_t VertexCount, 
                             std::vector<uint8_t>* pvIndexFaceArray,
                             size_t FaceCount,
                             const uint32_t *pdwAdjacency,
                             size_t Width,
                             size_t Height,
                             float Gutter,
                             unsigned int Stage,
                             LPISOCHARTCALLBACK pCallback, 
                             float Frequency, 
                             size_t iNumRotate)
{
    HRESULT hr = S_OK ;
    
    if (Width < 1 || Height < 1 || Gutter < 1 || iNumRotate <= 0)
        return E_INVALIDARG;

    CUVAtlasRepacker repacker(pvVertexArray, VertexCount, pvIndexFaceArray, 
        FaceCount, pdwAdjacency, iNumRotate, Width, Height, Gutter,
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if ( !repacker.SetCallback( pCallback, Frequency ) )
        return E_INVALIDARG ;

    unsigned int dwTotalStage = STAGE_TOTAL(Stage);
    unsigned int dwDoneStage = STAGE_DONE(Stage);

    if ( !repacker.SetStage( dwTotalStage, dwDoneStage ) )
        return E_INVALIDARG ;

    if ( FAILED(hr = repacker.Repack()) )
        return hr ;

    return S_OK;
}

//-------------------------------------------------------------------------
//	Constructor and destructor of CUVAtlasRepacker
//-------------------------------------------------------------------------

CUVAtlasRepacker::CUVAtlasRepacker(std::vector<UVAtlasVertex>* pvVertexArray,
                                    size_t VertexCount, 
                                    std::vector<uint8_t>* pvFaceIndexArray,
                                    size_t FaceCount,
                                    const uint32_t *pdwAdjacency,
                                    size_t iNumRotate,
                                    size_t Width,
                                    size_t Height,
                                    float Gutter,
                                    double *pPercentOur,    
                                    size_t *pFinalWidth,
                                    size_t *pFinalHeight,
                                    size_t *pChartNumber,
                                    size_t *pIterationTimes) :
            m_iNumFaces(FaceCount),
            m_iNumVertices(VertexCount),
            m_pvVertexBuffer(pvVertexArray),
            m_pvIndexBuffer(pvFaceIndexArray),
            m_iRotateNum(iNumRotate),
            m_pPartitionAdj(pdwAdjacency),
            m_dwAtlasWidth(Width),
            m_dwAtlasHeight(Height),
            m_iGutter((int)Gutter),
            m_pPercentOur(pPercentOur),
            m_pFinalWidth(pFinalWidth),
            m_pFinalHeight(pFinalHeight),
            m_pOurChartNumber(pChartNumber),
            m_pOurIterationTimes(pIterationTimes)
{		                    
}

/***************************************************************************\
    Function Description:
        Destructor of CUVAtlasRepacker.
    
    Arguments:
    Return Value:
\***************************************************************************/
CUVAtlasRepacker::~CUVAtlasRepacker()
{
}



//-------------------------------------------------------------------------
//	Public functions provided by CUVAtlasRepacker
//-------------------------------------------------------------------------

/***************************************************************************\
    Function Description:
        Public function provided for the user to produce UV atlas.
    
    Arguments:
            
    Return Value:
        If the function succeeds, the return value is S_OK
\***************************************************************************/
HRESULT CUVAtlasRepacker::Repack()
{
    HRESULT hr = S_OK ;
    
    DPF(3, "Pack preparing...");
    if ( FAILED(hr = Initialize()) )
        return hr ;
    DPF(3, "Ready\n");	

    do {
        if ( m_iIterationTimes <= 9 )
            m_callbackSchemer.InitCallBackAdapt( m_iNumCharts, 0.090f, (float)(m_iIterationTimes*0.090+0.05) ) ;
        
        m_OutOfRange = false;
        if ( FAILED( hr = CreateUVAtlas()) )
            return hr ;
        DPF(3, "Estimated Space Percent = %.3f%%", m_EstimatedSpacePercent * 100);

        if ( m_iIterationTimes <= 9 )
        {
            if ( FAILED( hr = m_callbackSchemer.FinishWorkAdapt()) ) 
                return hr ;
        }

        if (m_OutOfRange)
        {
            m_iIterationTimes++;
            AdjustEstimatedPercent();
            DPF(3, "Current packing is aborted.");
            DPF(3, "Adjusting estimated percent and restart packing...\n");
        }

    } while (!m_bStopIteration && m_OutOfRange);
    if (m_bStopIteration)
    {
        return E_INVALIDARG;
    }

    if ( m_iIterationTimes > 9 )
    {
        if ( FAILED( hr = m_callbackSchemer.FinishWorkAdapt()) ) 
            return hr ;
    }

    m_callbackSchemer.InitCallBackAdapt( 3, 0.05f, 0.95f ) ;
    
    ComputeFinalAtlasRect();
    if ( FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    Normalize();
    if ( FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    OutPutPackResult();
    if ( FAILED(hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    if ( FAILED(hr = m_callbackSchemer.FinishWorkAdapt()) )
        return hr ;

    if (m_bDwIndex)
    {
        double percentOur = GetTotalArea<uint32_t>();
        DPF(0, "Final space utilization ratio after pack = %.3f%%", percentOur * 100);
        
        if ( m_pPercentOur )
            *m_pPercentOur = percentOur ;
    }
    else
    {
        double percentOur = GetTotalArea<uint16_t>();
        DPF(0, "Final space utilization ratio after pack = %.3f%%", percentOur * 100);

        if ( m_pPercentOur )
            *m_pPercentOur = percentOur ;
    }

    if ( m_pFinalHeight )
        *m_pFinalHeight = m_RealHeight;

    if ( m_pFinalWidth )
        *m_pFinalWidth = m_RealWidth ;

    if ( m_pOurChartNumber )
        *m_pOurChartNumber = m_iNumCharts ;

    if ( m_pOurIterationTimes )
        *m_pOurIterationTimes = m_iIterationTimes + 1 ;        	

    DPF(0, "Final X and Y = %Iu, %Iu\n", m_RealHeight, m_RealWidth);

    m_bRepacked = true;

    return hr;
}

// added for checking and setting parameters for callback
bool CUVAtlasRepacker::SetCallback( LPISOCHARTCALLBACK pCallback, float Frequency ) 
{
    if (Frequency < 0 || Frequency > 1.0f)
    {
        return false;
    }

    m_callbackSchemer.SetCallback(
        pCallback, 
        Frequency);

    return true;
}

// added for checking and setting parameters for managing stages in progress
bool CUVAtlasRepacker::SetStage(unsigned int TotalStageCount, unsigned int DoneStageCount)
{
    if ( TotalStageCount < DoneStageCount )
    {
        return false;
    }

    m_callbackSchemer.SetStage( TotalStageCount, DoneStageCount ) ;

    return true;
}

//-------------------------------------------------------------------------
//	private functions
//-------------------------------------------------------------------------

/***************************************************************************\
    Function Description:
        Create the uv atlas.

    Arguments:
    Return Value:
\***************************************************************************/
HRESULT CUVAtlasRepacker::CreateUVAtlas()
{
    HRESULT hr = S_OK ;

    if ( FAILED( hr = PrepareRepack()) )
        return hr ;

    m_packedArea = m_ChartsInfo[m_SortedChartIndex[0]].area;
    m_packedCharts = 1;
    for (size_t i = 1; i < m_iNumCharts; i++)
    {
        PutChart(m_SortedChartIndex[i]);
        if (!m_OutOfRange)
        {			
            if ( FAILED( hr = m_callbackSchemer.UpdateCallbackAdapt( 1 ) ) )
                return hr ;

            m_packedCharts++;
            m_packedArea += m_ChartsInfo[m_SortedChartIndex[i]].area;
        } 
        else
        {
            break;
        }
    }
    
    return hr ;
}

/***************************************************************************\
    Function Description:
        This function adjust the estimated percent which represent the 
        ratio of final charts area to the total area of UV atlas.

    Arguments:
    Return Value:		
\***************************************************************************/
void CUVAtlasRepacker::AdjustEstimatedPercent()
{
    float oldp = m_EstimatedSpacePercent;

    if (m_iNumCharts < CHART_THRESHOLD)
    {
        m_EstimatedSpacePercent *= m_adjustFactor;
        m_EstimatedSpacePercent -= 0.005f;
    }
    else
    {
        float unpackedArea = 1.0f - m_packedArea / m_fChartsTotalArea;
        float unpackedCharts = 1.0f - (float) m_packedCharts / m_iNumCharts;
        DPF(3, "Unpacked area ratio= %.4f\tunpacked charts ratio= %.4f", unpackedArea, unpackedCharts);

        float factor = unpackedArea / 4.0f + unpackedCharts / 10.0f;
                
        if (factor < 0.02f)
            factor = 0.01f;
        if (factor > 0.2f)
            factor = 0.2f;

        m_EstimatedSpacePercent -= factor;
        //m_EstimatedSpacePercent /= 1.02f;

        if (m_iIterationTimes > MAX_ITERATION)
        {
            m_bStopIteration = true;
            return;
        }
    }

    if (m_EstimatedSpacePercent <= 0)
        m_EstimatedSpacePercent = oldp * 0.9f;

    m_PixelWidth = (float) sqrt(m_fChartsTotalArea / 
        (m_EstimatedSpacePercent * m_dwAtlasWidth * m_dwAtlasHeight));
}

/***************************************************************************\
    Function Description:
        Compute the final width and height of result uv atlas.
    
    Arguments:		
    Return Value:		
\***************************************************************************/
void CUVAtlasRepacker::ComputeFinalAtlasRect()
{
    int numX = m_toX - m_fromX - m_iGutter * 2;
    int numY = m_toY - m_fromY - m_iGutter * 2;
    if ((float)numY / (float)numX > m_AspectRatio)
    {
        m_NormalizeLen = numY;
        numX = (int)floorf((float)numY / m_AspectRatio + 0.5f);
    } else {
        m_NormalizeLen = numX;
        numY = (int)floorf((float)numX * m_AspectRatio + 0.5f);
    }
    m_RealWidth = numX;
    m_RealHeight = numY;
}

/***************************************************************************\
    Function Description:
        Find a suitable estimated percent
    
    Arguments:
    Return Value:
\***************************************************************************/
void CUVAtlasRepacker::InitialSpacePercent()
{
    const float AdjustFactor = 1.01f;

    for(;;)
    {
        m_PixelWidth = (float)sqrt(m_fChartsTotalArea / 
            (m_EstimatedSpacePercent * m_dwAtlasWidth * m_dwAtlasHeight));
        ChartsInfo *pCInfo = (ChartsInfo *)&(m_ChartsInfo[m_SortedChartIndex[0]]);
        _PositionInfo *pPosInfo = (_PositionInfo *)&(pCInfo->PosInfo[0]);

        int numX = (int)ceilf((pPosInfo->maxPoint.x - pPosInfo->minPoint.x) / m_PixelWidth);
        int numY = (int)ceilf((pPosInfo->maxPoint.y - pPosInfo->minPoint.y) / m_PixelWidth);

        if (numX <= (int)m_dwAtlasWidth && numY <= (int)m_dwAtlasHeight)
            break;
        m_EstimatedSpacePercent /= AdjustFactor;
    }
}

/***************************************************************************\
    Function Description:
        After fix a new space ratio, we must compute the new chart's width
        and height in pixel.
    
    Arguments:	
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::ComputeChartsLengthInPixel()
{
    // compute the width and height of the new chart in pixel 
    for (size_t i = 0; i < m_iNumCharts; i++)
    {
        ChartsInfo *pCInfo = (ChartsInfo *)&(m_ChartsInfo[i]);
        if (!pCInfo->valid) continue;
        for (size_t j = 0; j < m_iRotateNum; j++)
        {
            _PositionInfo *pPosInfo = (_PositionInfo *)&(pCInfo->PosInfo[j]);

            // compute the width and height of current chart
            int numX = (int)ceilf((pPosInfo->maxPoint.x - pPosInfo->minPoint.x) / m_PixelWidth);
            int numY = (int)ceilf((pPosInfo->maxPoint.y - pPosInfo->minPoint.y) / m_PixelWidth);
            if (!numX) numX++;
            if (!numY) numY++;

            // adjust the chart to be in the middle of the bounding box in pixel
            // to avoid one side is too close and the other side is too far from bounding box edges
            float adjustX = (numX * m_PixelWidth - (pPosInfo->maxPoint.x - pPosInfo->minPoint.x)) / 2.0f;
            float adjustY = (numY * m_PixelWidth - (pPosInfo->maxPoint.y - pPosInfo->minPoint.y)) / 2.0f;

            pPosInfo->adjustLen.x = adjustX;
            pPosInfo->adjustLen.y = adjustY;

            // the base point is used to compute the rotate matrix of the chart when the 
            // chart is rotated 90, 180 or 270 degrees
            pPosInfo->basePoint = XMFLOAT2(pPosInfo->minPoint.x - m_iGutter * m_PixelWidth - adjustX,
                pPosInfo->minPoint.y - m_iGutter * m_PixelWidth - adjustY);

            // the length should be added by gutter space of two sides
            pPosInfo->numX = numX + 2 * m_iGutter;
            pPosInfo->numY = numY + 2 * m_iGutter;
        }
    }
}


/***************************************************************************\
    Function Description:
        Prepare the information of charts.
    
    Arguments:
    
    Return Value:
        TRUE if success.
        FALSE otherwise.		
\***************************************************************************/
HRESULT CUVAtlasRepacker::PrepareRepack()
{
    // debug use
    CleanUp();

    // initialize UVAtlas space
    for (size_t i = 0; i < m_PreparedAtlasHeight; i++)
        memset((void*)&(m_UVBoard[i][0]), 0, sizeof(uint8_t) * m_PreparedAtlasWidth);

    // find the index of the longest chart
    uint32_t index = m_SortedChartIndex[0];

    //	After fix a new space ratio, we must compute the new chart's width
    //	and height in pixel.
    ComputeChartsLengthInPixel();

    // we use the longest chart's length to compute the chart's UV atlas size
    // it need to be 2 times longer to make sure the chart will not exceed when it rotates
    int numX = m_ChartsInfo[index].PosInfo[0].numX;
    int numY = m_ChartsInfo[index].PosInfo[0].numY;
    int size = 2 * std::max(numX, numY);
    if (size <= 0)
        return E_INVALIDARG ;

    // we make the chart UV atlas array fixed large enough size to save the time 
    // needed to resize the array when the changing chart
    try
    {
        m_currChartUVBoard.resize(size);
        m_triedUVBoard.resize(size);

        for (size_t j = 0; j < m_currChartUVBoard.size(); j++)
        {
            m_currChartUVBoard[j].resize(size);
            m_triedUVBoard[j].resize(size);
        }

        for (size_t i = 0; i < 4; i++)
        {
            m_currSpaceInfo[i].resize(size);
            m_SpaceInfo[i].resize(std::max(m_PreparedAtlasWidth, m_PreparedAtlasHeight));
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // do tessellation on the longest chart and put it into the atlas first
    DoTessellation(index, 0);

    // compute the aspect ratio and chart range after put on the first chart
    m_currAspectRatio = (float)numY / (float)numX;
    m_fromY = (int)m_UVBoard.size() / 2 - numY / 2;
    m_toY = m_fromY + numY;
    m_fromX = (int)m_UVBoard[0].size() / 2 - numX / 2;
    m_toX = m_fromX + numX;

    // put the longest chart into the atlas first
    for (int i =  m_fromY; i < m_toY; i++)
        for (int j =  m_fromX; j < m_toX; j++)
            m_UVBoard[i][j] = m_currChartUVBoard[i - m_fromY][j - m_fromX];

    // save the first chart's transform matrix
    XMStoreFloat4x4(&m_ResultMatrix[index], XMMatrixTranslation(
        m_PixelWidth * m_fromX - m_ChartsInfo[index].PosInfo[0].basePoint.x,
        m_PixelWidth * m_fromY - m_ChartsInfo[index].PosInfo[0].basePoint.y,
        0.0f));

    // prepare the space information of UV atlas
    PrepareSpaceInfo(m_SpaceInfo, m_UVBoard, m_fromX, m_toX, m_fromY, m_toY, false);

    return S_OK ;
}

/***************************************************************************\
    Function Description:
        Check if the pack operation is possible
    
    Arguments:	
    Return Value:
        TRUE if possible;
        FALSE otherwise.
\***************************************************************************/
bool CUVAtlasRepacker::PossiblePack()
{
    if (m_dwAtlasHeight / (m_iGutter + 1) * m_dwAtlasWidth / 
            (m_iGutter + 1) <= m_iNumCharts)
    {
        DPF(0, "Warning : \nGutter is too large or the atlas resolution is too small.\n");
        DPF(0, "Chart number = %Iu", m_iNumCharts);
        DPF(0, "Gutter = %d", m_iGutter);
        DPF(0, "User specified atlas : width = %Iu, height = %Iu", m_dwAtlasWidth, m_dwAtlasHeight);
        DPF(0, "The theoretic maximum charts the atlas can hold is %Iu\n", 
            m_dwAtlasHeight / (m_iGutter + 1) * m_dwAtlasWidth / (m_iGutter + 1));
        DPF(0, "So it is impossible to pack it into user specified atlas.\n");
        return false;
    }

    return true;
}

/***************************************************************************\
    Function Description:
        Check whether the input is valid.
    
    Arguments:	
    Return Value:
        TRUE if valid;
        FALSE otherwise.
\***************************************************************************/
bool CUVAtlasRepacker::CheckUserInput()
{
    if (!m_pvVertexBuffer)
    {
        DPF( 0, "Pack input vertex buffer pointer is nullptr!" ) ;
        return false;
    }
    if (m_pvVertexBuffer->size() != m_iNumVertices)
    {
        DPF( 0, "Pack input vertex structure should be (x,y,z,u,v)" ) ;
        return false;
    }

    if (!m_pvIndexBuffer)
    {
        DPF( 0, "Pack input face index buffer pointer is nullptr!" ) ;
        return false;
    }
    if (m_pvIndexBuffer->size() != m_iNumFaces * 3 * sizeof(uint32_t) &&
        m_pvIndexBuffer->size() != m_iNumFaces * 3 * sizeof(uint16_t))
    {
        DPF( 0, "Pack input face index buffer is neither a uint16_t array nor a uint32_t array" ) ;
        return false;
    }

    if ( m_iNumVertices == 0 || m_iNumFaces == 0 )
    {
        return false;
    }

    m_iNumBytesPerVertex = sizeof(UVAtlasVertex);
    m_TexCoordOffset = sizeof(XMFLOAT3);

    return true;
}

/***************************************************************************\
    Function Description:
        Do some initialization work.
    
    Arguments:
    Return Value:
        S_OK if possible;			
\***************************************************************************/
HRESULT CUVAtlasRepacker::Initialize()
{
    HRESULT hr = S_OK ;
    
    // check whether the input is valid
    if ( !CheckUserInput() )
        return E_INVALIDARG ;

    m_callbackSchemer.InitCallBackAdapt( 3, 0.05f, 0 ) ;

    // set initial estimated space percent
    m_EstimatedSpacePercent = 0.6f;
    m_bStopIteration = false;
    m_iIterationTimes = 0;
    m_fChartsTotalArea = 0;
    m_AspectRatio = (float)m_dwAtlasHeight / (float)m_dwAtlasWidth;

    // check if the mesh use 16-bit index or 32-bit index
    m_bDwIndex = (m_pvIndexBuffer->size() / m_iNumFaces) == (sizeof(uint32_t) * 3);

    // generate our own adjacent buffer, index buffer,
    // vertex buffer and attribute buffer
    if (m_bDwIndex) {
        if (FAILED(hr = GenerateAdjacentInfo<uint32_t>()))
            return hr ;
        if (FAILED(hr = GenerateNewBuffers<uint32_t>()))
            return hr ;
    } else {
        if ( FAILED(hr = GenerateAdjacentInfo<uint16_t>()) )
            return hr ;
        if ( FAILED(hr = GenerateNewBuffers<uint16_t>() ) )
            return hr ;
    }

    if ( !PossiblePack() )
        return E_INVALIDARG ;

    if ( FAILED( hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    try
    {
        m_ChartsInfo.resize(m_iNumCharts);
        for (size_t i = 0; i < m_iNumCharts; i++)
        {
            m_ChartsInfo[i].PosInfo.resize(m_iRotateNum);
        }
        m_SortedChartIndex.resize(m_iNumCharts);
        m_ResultMatrix.resize(m_iNumCharts);

        m_PreparedAtlasWidth = INITIAL_SIZE_FACTOR * m_dwAtlasWidth + 2 * m_iGutter;
        m_PreparedAtlasHeight = INITIAL_SIZE_FACTOR * m_dwAtlasHeight + 2 * m_iGutter;

        // initial UVAtlas space
        m_UVBoard.resize(m_PreparedAtlasHeight);
        for (size_t i = 0; i < m_PreparedAtlasHeight; i++)
        {
            m_UVBoard[i].resize(m_PreparedAtlasWidth);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    if ( FAILED( hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    if ( FAILED(hr = PrepareChartsInfo() ) )
        return hr ;

    // sort the chart by length
    SortCharts();

    if ( FAILED( hr = m_callbackSchemer.UpdateCallbackAdapt( 1 )) )
        return hr ;

    //	Find a suitable estimated percent
    InitialSpacePercent();

    if ( FAILED( hr = m_callbackSchemer.FinishWorkAdapt()) )
        return hr ;

    return hr;
}

/***************************************************************************\
    Function Description:
        When one pack operation is aborted, this step is to clean up some
        unused information.
    
    Arguments:
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::CleanUp()
{
}


/***************************************************************************\
    Function Description:
        Generate the adjacent info for input mesh or vertex buffer.
        We do not use the GenerateAdjacency.
        Use template to handle the 16-bit index and 32-bit index buffer.
    
    Arguments:	
    Return Value:			
\***************************************************************************/
template <class T>
HRESULT CUVAtlasRepacker::GenerateAdjacentInfo()
{
    auto ib = reinterpret_cast<const _Triangle<T> *>( m_pvIndexBuffer->data() );

    try
    {
        m_AdjacentInfo.resize(3 * m_iNumFaces);

        if (m_pPartitionAdj)
        {
            memcpy(m_AdjacentInfo.data(), m_pPartitionAdj, m_iNumFaces * 3 * sizeof(uint32_t));
            return S_OK;
        }

        // generate a vertex adjacent information to keep all the 
        // faces that connected with one vertex together
        m_VertexAdjInfo.resize(m_iNumVertices);

        for (uint32_t i = 0; i < m_iNumFaces; i++)
        {
            m_VertexAdjInfo[ib[i].vertex[0]].push_back(i);
            m_VertexAdjInfo[ib[i].vertex[1]].push_back(i);
            m_VertexAdjInfo[ib[i].vertex[2]].push_back(i);
        }
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    // initialize the adjacent information
    for (size_t i = 0; i < m_iNumFaces; i++)
    {
        m_AdjacentInfo[3 * i] = uint32_t(-1);
        m_AdjacentInfo[3 * i + 1] = uint32_t(-1);
        m_AdjacentInfo[3 * i + 2] = uint32_t(-1);
    }

    // generate adjacent information
    static const int order[3][2] = { {0, 1}, {1, 2}, {0, 2} };

    for (uint32_t i = 0; i < m_iNumFaces - 1; i++)
    {
        for (uint32_t j = i + 1; j < m_iNumFaces; j++)
        {
            for (size_t m = 0; m < 3; m++) if (m_AdjacentInfo[i * 3 + m] == uint32_t(-1))
                for (size_t n = 0; n < 3; n++) if (m_AdjacentInfo[j * 3 + n] == uint32_t(-1))
                    if (ib[i].vertex[order[m][0]] == ib[j].vertex[order[n][0]] && 
                        ib[i].vertex[order[m][1]] == ib[j].vertex[order[n][1]] ||
                        ib[i].vertex[order[m][0]] == ib[j].vertex[order[n][1]] && 
                        ib[i].vertex[order[m][1]] == ib[j].vertex[order[n][0]])
                        // if two triangles have two common vertices, they are adjacent
                    {
                        m_AdjacentInfo[i * 3 + m] = j;
                        m_AdjacentInfo[j * 3 + n] = i;
                        m = 3;
                        break;
                    }
            if (m_AdjacentInfo[i * 3] != -1 && m_AdjacentInfo[i * 3 + 1] != -1 && 
                m_AdjacentInfo[i * 3 + 2] != -1)
                break;
        }
    }

    return S_OK ;
}

/***************************************************************************\
    Function Description:
        Generate some necessary information about the input mesh.
        Use template to handle the 16-bit index and 32-bit index buffer.
        This function create our sorted vertex buffer, index buffer and 
        adjacent information buffer. 
        The buffer is sorted by put together all the faces that belong to 
        the same chart. 

    Arguments:	
    Return Value:	
\***************************************************************************/
template <class T>
HRESULT CUVAtlasRepacker::GenerateNewBuffers()
{
    // create an attribute buffer
    m_vAttributeBuffer.resize(m_iNumFaces);

    auto pAB = m_vAttributeBuffer.data();
    for (size_t i = 0; i < m_iNumFaces; i++)
        pAB[i] = uint32_t(-1);

    try
    {
        m_NewAdjacentInfo.resize(m_iNumFaces * 3);
        m_VertexBuffer.reserve(m_iNumVertices);
        m_IndexBuffer.reserve(m_iNumFaces * 3);
        m_IndexPartition.resize(m_iNumVertices);

        for (size_t i = 0; i < m_iNumVertices; i++)
            m_IndexPartition[i] = uint32_t(-1);

        auto pVB = reinterpret_cast<const uint8_t*>( m_pvVertexBuffer->data() );
        auto pIB = reinterpret_cast<const uint8_t*>( m_pvIndexBuffer->data() );

        UVATLASATTRIBUTERANGE ar;

        std::unique_ptr<bool[]> bUsedFace( new (std::nothrow) bool[ m_iNumFaces ] );
        if ( !bUsedFace )
            return E_OUTOFMEMORY;

        memset( bUsedFace.get(), 0, sizeof(bool)*m_iNumFaces);

        std::vector<uint32_t> ab;
        int num = 0;
        int indexnum = 0;
        int facestart = 0;
        for (uint32_t i = 0; i < m_iNumFaces; i++)
        {
            if (pAB[i] == -1)
            {
                ab.clear();
                if (!bUsedFace[i]) 
                {
                    ab.push_back(i);
                    bUsedFace[i] = true;
                }
                size_t t = 0;

                // use broad-first search algorithm to find chart
                // store the result into m_pAttributeBuffer 
                if (m_pPartitionAdj)
                {
                    while(t < ab.size())
                    {
                        pAB[ab[t]] = num;
                        for (uint32_t j = 0; j < 3; j++)
                        {
                            uint32_t index = 3 * ab[t] + j;
                            if (m_AdjacentInfo[index] != -1 && !bUsedFace[m_AdjacentInfo[index]])
                            {
                                ab.push_back(m_AdjacentInfo[index]);
                                bUsedFace[m_AdjacentInfo[index]] = true;
                            }
                        }
                        t++;
                    }			
                }
                else
                {
                    while(t < ab.size())
                    {
                     pAB[ab[t]] = num;
                        for (size_t j = 0; j < 3; j++)
                        {
                            uint32_t index = *(T *) (pIB + (3 * ab[t] + j) * sizeof(T));
                            for (size_t k = 0; k < m_VertexAdjInfo[index].size(); k++)
                                if (!bUsedFace[m_VertexAdjInfo[index][k]])
                                {
                                    ab.push_back(m_VertexAdjInfo[index][k]);
                                    bUsedFace[m_VertexAdjInfo[index][k]] = true;
                                }
                        }
                        t++;
                    }
                }

                // after found a set of vertices that belong to the same chart we store them 
                // continuously in new vertex buffers.
                ar.VertexStart = (uint32_t) m_VertexBuffer.size();

                // iterate every triangle in the same chart
                for (size_t j = 0; j < ab.size(); j++)
                {

                    // find the original index of the triangle's vertex
                    uint32_t index1 = *(T *) (pIB + 3 * ab[j] * sizeof(T));
                    uint32_t index2 = *(T *) (pIB + (3 * ab[j] + 1) * sizeof(T));
                    uint32_t index3 = *(T *) (pIB + (3 * ab[j] + 2) * sizeof(T));

                    // copy the original adjacent information continuously in new adjacent buffer
                    memcpy(&m_NewAdjacentInfo[j * 3 + facestart * 3], 
                        &m_AdjacentInfo[3 * ab[j]], sizeof(uint32_t) * 3);

                    // copy the original index information continuously in new index buffer
                    m_IndexBuffer.push_back(index1);
                    m_IndexBuffer.push_back(index2);
                    m_IndexBuffer.push_back(index3);
                
                    // find the original UV coordinates of each vertex
                    XMFLOAT2 *p1 = (XMFLOAT2 *)(pVB + index1 * m_iNumBytesPerVertex + m_TexCoordOffset);
                    XMFLOAT2 *p2 = (XMFLOAT2 *)(pVB + index2 * m_iNumBytesPerVertex + m_TexCoordOffset);
                    XMFLOAT2 *p3 = (XMFLOAT2 *)(pVB + index3 * m_iNumBytesPerVertex + m_TexCoordOffset);

                    // find the original 3D coordinates of each vertex
                    XMFLOAT3 *pp1 = (XMFLOAT3 *)(pVB + index1 * m_iNumBytesPerVertex);
                    XMFLOAT3 *pp2 = (XMFLOAT3 *)(pVB + index2 * m_iNumBytesPerVertex);
                    XMFLOAT3 *pp3 = (XMFLOAT3 *)(pVB + index3 * m_iNumBytesPerVertex);

                    // create an index partition buffer which store each vertex's original position
                    // to recover the vertex buffer after repacking.
                    if (m_IndexPartition[index1] == -1) 
                    {
                        m_IndexPartition[index1] = indexnum++; 
                        UVAtlasVertex vert;
                        vert.pos.x = pp1->x;
                        vert.pos.y = pp1->y;
                        vert.pos.z = pp1->z;
                        vert.uv.x = p1->x;
                        vert.uv.y = p1->y;
                        m_VertexBuffer.push_back(vert);
                    }
                    if (m_IndexPartition[index2] == -1) 
                    {
                        m_IndexPartition[index2] = indexnum++;
                        UVAtlasVertex vert;
                        vert.pos.x = pp2->x;
                        vert.pos.y = pp2->y;
                        vert.pos.z = pp2->z;
                        vert.uv.x = p2->x;
                        vert.uv.y = p2->y; 
                        m_VertexBuffer.push_back(vert);
                    }
                    if (m_IndexPartition[index3] == -1) 
                    {
                        m_IndexPartition[index3] = indexnum++;
                        UVAtlasVertex vert;
                        vert.pos.x = pp3->x;
                        vert.pos.y = pp3->y;
                        vert.pos.z = pp3->z;
                        vert.uv.x = p3->x;
                        vert.uv.y = p3->y;
                        m_VertexBuffer.push_back(vert);
                    }
                }

                // store the newly found chart's information into m_AttrTable which
                // represent the attribute table
                ar.VertexCount = static_cast<uint32_t>(m_VertexBuffer.size() - ar.VertexStart);
                ar.FaceCount = static_cast<uint32_t>(ab.size());
                ar.FaceStart = facestart;
                ar.AttribId = num;
                facestart += static_cast<int>(ab.size());
                m_AttrTable.push_back(ar);
                num++;
            }
        }
        m_iNumCharts = num;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK ;
}

/***************************************************************************\
    Function Description:
        Found the top left corner and bottom right corner.

    Arguments:
        [in]	Vec		-	A array of XMFLOAT2.
        [out]	minV	-	top left corner in XMFLOAT2 format.
        [out]	maxV	-	bottom right corner in XMFLOAT2 format.
    
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::ComputeBoundingBox(
    std::vector<XMFLOAT2>& Vec,
    XMFLOAT2* minV, XMFLOAT2* maxV)
{
    for (size_t o = 0; o < Vec.size(); ++o)
    {
        if (Vec[o].x < minV->x) minV->x = Vec[o].x;
        if (Vec[o].x > maxV->x) maxV->x = Vec[o].x;
        if (Vec[o].y < minV->y) minV->y = Vec[o].y;
        if (Vec[o].y > maxV->y) maxV->y = Vec[o].y;
    }
}


/***************************************************************************\
    Function Description:
        Set each chart into its best position (the bounding box is smallest).
        Save the chart edges information for later use.

    Arguments:	
    Return Value:
        TRUE if possible;
        FALSE otherwise.	
\***************************************************************************/
HRESULT CUVAtlasRepacker::PrepareChartsInfo()
{
    XMMATRIX bestMatrix = XMMatrixIdentity();
    float RotateAngle = 5.0f;

    std::vector<XMFLOAT2> OutVec;

    // iterate each chart to tessellate it
    for (uint32_t i = 0; i < m_iNumCharts; i++)
    {
        // find best angle to rotate the chart to the best position
        try
        {
            OutVec.resize(m_AttrTable[i].VertexCount);
        }
        catch (std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        float minArea = 1e10;

        for (size_t j = 1; j <= (90 / RotateAngle); j++)
        {
            float angle = j * RotateAngle / 180.0f * XM_PI;
            if (angle > XM_PI / 2.0f)
                angle = XM_PI / 2.0f;
            XMMATRIX rotateMatrix = XMMatrixRotationZ(angle);

            XMVector2TransformCoordStream(
                &OutVec[0], 
                sizeof(XMFLOAT2),
                &m_VertexBuffer[m_AttrTable[i].VertexStart].uv,
                VertexSize, 
                m_AttrTable[i].VertexCount,
                rotateMatrix
                );

            XMFLOAT2 minV(1e10f, 1e10f);
            XMFLOAT2 maxV(-1e10f, -1e10f);
            ComputeBoundingBox(OutVec, &minV, &maxV);

            // check if the chart has only one point
            // we ignore it in our packing process
            if ((maxV.x == minV.x) && (maxV.y == minV.y))
            {
                m_ChartsInfo[i].valid = false;
                goto NEXT;
            }

            // find the smallest bounding box
            if ( (maxV.x - minV.x) * (maxV.y - minV.y) < minArea)
            {
                minArea = (maxV.x - minV.x) * (maxV.y - minV.y);
                bestMatrix = rotateMatrix;
            }
        }

        // copy the rotated vertex buffer back into the original one
        XMVector2TransformCoordStream(
            &m_VertexBuffer[m_AttrTable[i].VertexStart].uv,
            VertexSize, 
            &m_VertexBuffer[m_AttrTable[i].VertexStart].uv,
            VertexSize, 
            m_AttrTable[i].VertexCount,
            bestMatrix
            );

        m_ChartsInfo[i].valid = true;
        m_ChartsInfo[i].area = GetChartArea(i);
        m_fChartsTotalArea += m_ChartsInfo[i].area;

        // rotate the chart to different position and store the 
        // edges and other useful information
        for (size_t j = 0; j < m_iRotateNum; j++) 
        {
            float angle = j * XM_PI / m_iRotateNum / 2.0f;
            XMMATRIX rotateMatrix = XMMatrixRotationZ(angle);

            XMVector2TransformCoordStream(
                &OutVec[0], 
                sizeof(XMFLOAT2),
                &m_VertexBuffer[m_AttrTable[i].VertexStart].uv,
                VertexSize, 
                m_AttrTable[i].VertexCount,
                rotateMatrix
                );

            XMFLOAT2 minV(1e10f, 1e10f);
            XMFLOAT2 maxV(-1e10f, -1e10f);
            ComputeBoundingBox(OutVec, &minV, &maxV);

            m_ChartsInfo[i].PosInfo[j].angle = angle;
            m_ChartsInfo[i].PosInfo[j].maxPoint = maxV;
            m_ChartsInfo[i].PosInfo[j].minPoint = minV;
            if (j == 0)
                m_ChartsInfo[i].maxLength = std::max(maxV.x - minV.x, maxV.y - minV.y);

            // find the outer edges of every chart for later tessellation
            for (uint32_t k = 0; k < m_AttrTable[i].FaceCount; k++)
            {
                uint32_t Base = (k + m_AttrTable[i].FaceStart) * 3;

                int a = m_IndexPartition[m_IndexBuffer[Base]];
                int b = m_IndexPartition[m_IndexBuffer[Base + 1]];
                int c = m_IndexPartition[m_IndexBuffer[Base + 2]];

                int indexbase = m_AttrTable[i].VertexStart; 

                XMFLOAT2 &Vertex1 = *reinterpret_cast<XMFLOAT2*>(&OutVec[a - indexbase]);
                XMFLOAT2 &Vertex2 = *reinterpret_cast<XMFLOAT2*>(&OutVec[b - indexbase]);
                XMFLOAT2 &Vertex3 = *reinterpret_cast<XMFLOAT2*>(&OutVec[c - indexbase]);

                // handle the situation when the triangle have two uniform vertices
                // bases on our experiment we just recognize every line segment as one edge
                if (((Vertex1.x == Vertex2.x) && (Vertex1.y == Vertex2.y)) ||
                    ((Vertex1.x == Vertex3.x) && (Vertex1.y == Vertex3.y)) ||
                    ((Vertex3.x == Vertex2.x) && (Vertex3.y == Vertex2.y)))
                {
                    m_ChartsInfo[i].PosInfo[j].edges.clear();
                    for (uint32_t t = 0; t < m_AttrTable[i].FaceCount; t++)
                    {
                        uint32_t Base0 = (t + m_AttrTable[i].FaceStart) * 3;

                        int a0 = m_IndexPartition[m_IndexBuffer[Base0]];
                        int b0 = m_IndexPartition[m_IndexBuffer[Base0 + 1]];
                        int c0 = m_IndexPartition[m_IndexBuffer[Base0 + 2]];

                        int indexbase0 = m_AttrTable[i].VertexStart; 

                        if (a0 >= indexbase0 && b0 >= indexbase0 && c0 >= indexbase0)
                        {
                            XMFLOAT2 &vert1 = *reinterpret_cast<XMFLOAT2*>(&OutVec[a0 - indexbase0]);
                            XMFLOAT2 &vert2 = *reinterpret_cast<XMFLOAT2*>(&OutVec[b0 - indexbase0]);
                            XMFLOAT2 &vert3 = *reinterpret_cast<XMFLOAT2*>(&OutVec[c0 - indexbase0]);
                            m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(vert1, vert2));
                            m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(vert2, vert3));
                            m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(vert3, vert1));
                        }
                        else 
                            return E_FAIL;
                    }
                    break;
                }

                // if the triangle has a edge without adjacent triangle
                // the edge is one outer edge
                if (m_NewAdjacentInfo[Base] == -1)
                    m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(Vertex1, Vertex2));
                if (m_NewAdjacentInfo[Base + 1] == -1)
                    m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(Vertex2, Vertex3));
                if (m_NewAdjacentInfo[Base + 2] == -1)
                    m_ChartsInfo[i].PosInfo[j].edges.push_back(_EDGE(Vertex3, Vertex1));
            }
        }
        NEXT:;
    }

    return S_OK ;
}

/***************************************************************************\
    Function Description:
        Sort the charts by the their length.
    
    Arguments:	
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::SortCharts()
{
    for (uint32_t i = 0; i < m_iNumCharts; i++)
        m_SortedChartIndex[i] = i;

    std::sort(m_SortedChartIndex.begin(), m_SortedChartIndex.end(),
        [=](size_t a, size_t b)
        {
            return (m_ChartsInfo[a].maxLength > m_ChartsInfo[b].maxLength);
        });
}

/***************************************************************************\
    Function Description:
        Compute the distance between the chart edge to its corresponding
        edge of bounding box.
    
    Arguments:
        [in/out]	spaceInfo	-	array into which the result will be 
                                    saved.
        [in]		board		-	uv board of the chart which will be 
                                    processed.
        [in]		fromX, toX, fromY, toY
                                -	the top left corner and bottom right 
                                    corner of uv board.
        [in]		bNeglectGrows	-	whether to count the edges growed 
                                    by program
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::PrepareSpaceInfo(SpaceInfo &spaceInfo, 
                                        UVBoard &board, int fromX, 
                                        int toX, int fromY, int toY, 
                                        bool bNeglectGrows)
{
    // top
    for (int i = fromX; i < toX; i++)
    {
        int j = fromY;
        if (bNeglectGrows) while(j < toY && board[j++][i] != 1);
        else while(j < toY && board[j++][i] == 0);
        spaceInfo[UV_UPSIDE][i] = j - fromY - 1;
    }

    // bottom
    for (int i = fromX; i < toX; i++)
    {
        int j = toY;
        if (bNeglectGrows) while(j > fromY && board[--j][i] != 1);
        else while(j > fromY && board[--j][i] == 0);
        spaceInfo[UV_DOWNSIDE][i] = toY - j - 1;
    }	

    // left
    for (int i = fromY; i < toY; i++)
    {
        int j = fromX;
        if (bNeglectGrows) while(j < toX && board[i][j++] != 1);
        else while(j < toX && board[i][j++] == 0);
        spaceInfo[UV_LEFTSIDE][i] = j - fromX - 1;
    }	

    // right
    for (int i = fromY; i < toY; i++)
    {
        int j = toX;
        if (bNeglectGrows) while(j > fromX && board[i][--j] != 1);
        else while(j > fromX && board[i][--j] == 0);
        spaceInfo[UV_RIGHTSIDE][i] = toX - j - 1;
    }
}

/***************************************************************************\
    Function Description:
        Reverse the array.
    
    Arguments:	
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::Reverse(std::vector<int>& data, size_t len)
{
    for (size_t i = 0; i < len / 2; i++)
    {
        std::swap(data[i], data[len - i - 1]);
    }
} 

/***************************************************************************\
    Function Description:
        Find a best position and angle to put current chart into atlas.
    
    Arguments:
        [in]	index	-	the index of chart to be packed
    
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::PutChart(uint32_t index)
{
    ChartsInfo *pCInfo = &m_ChartsInfo[index];

    if (!pCInfo->valid) 
        return;

    m_triedInternalSpace = (int)1e8;

    for (uint32_t i = 0; i < m_iRotateNum; i++) 
    {
        // for every position of chart, first do tessellation on it
        // then try to put it into the atlas after rotate 0, 90, 180, 270 degrees
        _PositionInfo *pPosInfo = (_PositionInfo*)&(pCInfo->PosInfo[i]);
        DoTessellation(index, i);
        PrepareSpaceInfo(m_currSpaceInfo, m_currChartUVBoard, 
            0, pPosInfo->numX, 0, pPosInfo->numY, true);

        m_currRotate = i;

        int PutSide = 0;
        if (m_currAspectRatio > m_AspectRatio) // put on left or right side
            PutSide = 0;
        else if (m_currAspectRatio < m_AspectRatio)
            PutSide = 1;
        else
            PutSide = (int) floorf(rand() + 0.5f);

        if (PutSide == 0) // put on left or right side
        {	
            if (i == 0) m_triedAspectRatio = -1e10;
            // try to put left side
            TryPut(UV_RIGHTSIDE, UV_LEFTSIDE, 0, pPosInfo->numX, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numY);
            TryPut(UV_UPSIDE, UV_LEFTSIDE, 90, pPosInfo->numY, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numX);

            // try to put right side
            TryPut(UV_LEFTSIDE, UV_RIGHTSIDE, 0, pPosInfo->numX, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numY);
            TryPut(UV_DOWNSIDE, UV_RIGHTSIDE, 90, pPosInfo->numY, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numX);

            // try to put left side
            Reverse(m_currSpaceInfo[UV_LEFTSIDE], pPosInfo->numY);
            TryPut(UV_LEFTSIDE, UV_LEFTSIDE, 180, pPosInfo->numX, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numY);
            Reverse(m_currSpaceInfo[UV_DOWNSIDE], pPosInfo->numX);
            TryPut(UV_DOWNSIDE, UV_LEFTSIDE, 270, pPosInfo->numY, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numX);

            // try to put right side
            Reverse(m_currSpaceInfo[UV_RIGHTSIDE], pPosInfo->numY);
            TryPut(UV_RIGHTSIDE, UV_RIGHTSIDE, 180, pPosInfo->numX, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numY);
            Reverse(m_currSpaceInfo[UV_UPSIDE], pPosInfo->numX);
            TryPut(UV_UPSIDE, UV_RIGHTSIDE, 270, pPosInfo->numY, 
                m_toX - m_fromX, m_fromY, m_toY, pPosInfo->numX);
        } 
        else // put on top or bottom side
        {
            if (i == 0) m_triedAspectRatio = 1e10;
            // try to put top side
            TryPut(UV_DOWNSIDE, UV_UPSIDE, 0, pPosInfo->numY, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numX);
            TryPut(UV_LEFTSIDE, UV_UPSIDE, 270, pPosInfo->numX, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numY);

            // try to put down side
            TryPut(UV_RIGHTSIDE, UV_DOWNSIDE, 270, pPosInfo->numX, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numY);
            TryPut(UV_UPSIDE, UV_DOWNSIDE, 0, pPosInfo->numY, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numX);

            // try to put top side
            Reverse(m_currSpaceInfo[UV_RIGHTSIDE], pPosInfo->numY);
            TryPut(UV_RIGHTSIDE, UV_UPSIDE, 90, pPosInfo->numX, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numY);
            Reverse(m_currSpaceInfo[UV_UPSIDE], pPosInfo->numX);
            TryPut(UV_UPSIDE, UV_UPSIDE, 180, pPosInfo->numY, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numX);

            // try to put down side
            Reverse(m_currSpaceInfo[UV_LEFTSIDE], pPosInfo->numY);
            TryPut(UV_LEFTSIDE, UV_DOWNSIDE, 90, pPosInfo->numX, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numY);
            Reverse(m_currSpaceInfo[UV_DOWNSIDE], pPosInfo->numX);
            TryPut(UV_DOWNSIDE, UV_DOWNSIDE, 180, pPosInfo->numY, 
                m_toY - m_fromY, m_fromX, m_toX, pPosInfo->numX);
        }

        // save the best chart position at present
        if (m_triedRotate == i) {
            for (int j = 0; j < pPosInfo->numY; j++)
                for (int k = 0; k < pPosInfo->numX; k++)
                    m_triedUVBoard[j][k] = m_currChartUVBoard[j][k];
        }
    }

    PutChartInPosition(index);
}


/***************************************************************************\
    Function Description:
        Find the best position to put one rotated chart into the atlas.
    
    Arguments:
        [in]	chartPutSide	-	Specify which side of the chart will face 
                                    the atlas edges.
                                    It will be the following :
                                        UV_UPSIDE = 0;
                                        UV_RIGHTSIDE = 1;
                                        UV_DOWNSIDE = 2;
                                        UV_LEFTSIDE = 3;
        [in]	PutSide			-	Specify which side of the atlas will face 
                                    the chart edges.
        [in]	Rotation		-	Specify the degrees which the chart has
                                    rotated.
        [in]	chartWidth		-	Specify the width of another side to 
                                    chartPutside.
        [in]	width			-	Specify the width of another side to
                                    PutSide.
        [in]	from, to		-	Describe the PutSide ranges.
        [in]	chartSideLen	-	Describe the chartPutSide ranges.		
    
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::TryPut(int chartPutSide, int PutSide,
                              int Rotation, int chartWidth, int width, 
                              int from, int to, int chartSideLen)
{
    auto& chartSpaceInfo = m_currSpaceInfo[chartPutSide];
    auto& spaceInfo = m_SpaceInfo[PutSide];

    if (chartSideLen > to - from)
        return;

    int posNum = to - chartSideLen + 1;
    for (int i = from; i < posNum; i++)
    {
        // find the nearest distance of chart and atlas
        int minDistant = (int)1e8;
        int internalSpace = 0;
        for (int j = m_iGutter; j < chartSideLen - m_iGutter; j++)
        {
            int distant = spaceInfo[i + j] + chartSpaceInfo[j];
            internalSpace += distant;
            if (distant < minDistant)
                minDistant = distant;
        }
        internalSpace -= minDistant * chartSideLen;

        // compute the new ratio of width and height
        float ratio;
        if (minDistant <= chartWidth)
            if (PutSide == UV_UPSIDE || PutSide == UV_DOWNSIDE)
                ratio =  float(width + chartWidth - minDistant) / float(to - from);
            else
                ratio =  float(to - from) / float(width + chartWidth - minDistant);
        else
            if (PutSide == UV_UPSIDE || PutSide == UV_DOWNSIDE)
                ratio =  float(width) / float(to - from);
            else
                ratio =  float(to - from) / float(width);

        // accept the new putting position if 
        //	1.	new ratio is more closer to user specified one
        //	2.	the ratio is the same but the internal space is smaller than before
        //	3.	the ratio and the internal space are all the same but the position 
        //		is more inside than before	
        if ((ratio < m_triedAspectRatio && (PutSide == UV_UPSIDE || PutSide == UV_DOWNSIDE)) || 
            (ratio > m_triedAspectRatio && (PutSide == UV_LEFTSIDE || PutSide == UV_RIGHTSIDE)) ||
            ((fabs(ratio - m_triedAspectRatio) < 1e-6f) && 
            (internalSpace < m_triedInternalSpace || 
            (abs(internalSpace - m_triedInternalSpace) < m_triedInternalSpace * 0.05f && 
            m_triedOverlappedLen < minDistant))))
        {
            m_triedRotate = m_currRotate;
            m_triedAspectRatio = ratio;
            m_triedInternalSpace = internalSpace;
            m_triedPutRotation = Rotation;
            m_triedPutPos = i;
            m_triedOverlappedLen = minDistant;
            m_triedPutSide = PutSide;
        }
    }
}

/***************************************************************************\
    Function Description:
        Check if the current atlas is out of user defined range.
    
    Arguments:	
    Return Value:
        TRUE if not out of range.
        FALSE otherwise.	
\***************************************************************************/
bool CUVAtlasRepacker::CheckAtlasRange()
{
    int minX = (m_chartFromX < m_fromX) ? m_chartFromX : m_fromX;
    int minY = (m_chartFromY < m_fromY) ? m_chartFromY : m_fromY;
    int maxX = (m_chartToX > m_toX) ? m_chartToX : m_toX;
    int maxY = (m_chartToY > m_toY) ? m_chartToY : m_toY;

    int tmpX = maxX - minX - 2 * m_iGutter;
    int tmpY = maxY - minY - 2 * m_iGutter;

    if (tmpX > (int) m_dwAtlasWidth || tmpY > (int) m_dwAtlasHeight)
    {
        m_OutOfRange = true;

        if (m_iNumCharts < CHART_THRESHOLD)
        {
            if (tmpX > (int) m_dwAtlasWidth)
                m_adjustFactor = (float) (m_dwAtlasWidth) / tmpX;
            if (tmpY > (int) m_dwAtlasHeight)
                m_adjustFactor = (float) m_dwAtlasHeight / tmpY;
            m_adjustFactor *= m_adjustFactor;
        }

        return false;
    }

    return true;
}

/***************************************************************************\
    Function Description:
        Get coordinates ranges where current chart should be put.
    
    Arguments:	
        [in]	index	-	the index of current chart

    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::GetChartPutPosition(uint32_t index)
{
    _PositionInfo *pPosInfo = 
        (_PositionInfo *)&(m_ChartsInfo[index].PosInfo[m_triedRotate]);

    switch (m_triedPutSide)
    {
    case UV_UPSIDE:
        m_chartFromX = m_triedPutPos;  
        if (m_triedPutRotation == 0 || m_triedPutRotation == 180)
            m_chartFromY = m_fromY - pPosInfo->numY + m_triedOverlappedLen;
        else
            m_chartFromY = m_fromY - pPosInfo->numX + m_triedOverlappedLen;
        break;
    case UV_RIGHTSIDE:
        m_chartFromX = m_toX - m_triedOverlappedLen; 
        m_chartFromY = m_triedPutPos;
        break;
    case UV_DOWNSIDE:
        m_chartFromX = m_triedPutPos;
        m_chartFromY = m_toY - m_triedOverlappedLen; 
        break;
    case UV_LEFTSIDE:
        m_chartFromY = m_triedPutPos;
        if (m_triedPutRotation == 0 || m_triedPutRotation == 180)
            m_chartFromX = m_fromX + m_triedOverlappedLen - pPosInfo->numX;
        else
            m_chartFromX = m_fromX + m_triedOverlappedLen - pPosInfo->numY;
        break;
    }

    if (m_triedPutRotation == 0 || m_triedPutRotation == 180) {
        m_chartToX = m_chartFromX + pPosInfo->numX;
        m_chartToY = m_chartFromY + pPosInfo->numY;
    } else if (m_triedPutRotation == 90 || m_triedPutRotation == 270) {
        m_chartToX = m_chartFromX + pPosInfo->numY;
        m_chartToY = m_chartFromY + pPosInfo->numX;
    }
}

/***************************************************************************\
    Function Description:
        Put the current chart into the atlas.
    
    Arguments:
        [in]	index	-	The index of chart to be put.
    
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::PutChartInPosition(uint32_t index)
{
    GetChartPutPosition(index);
    if (!CheckAtlasRange()) return;

    _PositionInfo *pPosInfo = 
        (_PositionInfo *)&(m_ChartsInfo[index].PosInfo[m_triedRotate]);

    XMMATRIX matrixRotate;
    matrixRotate = XMMatrixRotationZ(m_triedPutRotation / 180.0f * XM_PI);
    XMStoreFloat2(&(pPosInfo->basePoint), XMVector2TransformCoord(XMLoadFloat2(&(pPosInfo->basePoint)),
        matrixRotate));
    matrixRotate = XMMatrixRotationZ(m_triedPutRotation / 180.0f * XM_PI +
        pPosInfo->angle);

    m_currAspectRatio = m_triedAspectRatio;
    XMMATRIX transMatrix = XMMatrixIdentity();;
    switch (m_triedPutRotation)
    {
    case 0:
        for (int i = m_chartFromY; i < m_chartToY; i++)
            for (int j = m_chartFromX; j < m_chartToX; j++)
                if (m_UVBoard[i][j] != 1 && m_triedUVBoard[i - m_chartFromY][j - m_chartFromX])
                    m_UVBoard[i][j] = 
                        m_triedUVBoard[i - m_chartFromY][j - m_chartFromX];
        transMatrix = XMMatrixTranslation(
            m_PixelWidth * m_chartFromX - pPosInfo->basePoint.x,
            m_PixelWidth * m_chartFromY - pPosInfo->basePoint.y, 0.0f);
        break;
    case 90:
        for (int i = m_chartFromY; i < m_chartToY; i++)
            for (int j = m_chartFromX; j < m_chartToX; j++)
                if (m_UVBoard[i][j] != 1 && m_triedUVBoard[m_chartToX - j - 1][i - m_chartFromY])
                    m_UVBoard[i][j] = 
                        m_triedUVBoard[m_chartToX - j - 1][i - m_chartFromY];
        transMatrix = XMMatrixTranslation(
            m_PixelWidth * m_chartToX - pPosInfo->basePoint.x,
            m_PixelWidth * m_chartFromY - pPosInfo->basePoint.y, 0.0f);
        break;
    case 180:
        for (int i = m_chartFromY; i < m_chartToY; i++)
            for (int j = m_chartFromX; j < m_chartToX; j++)
                if (m_UVBoard[i][j] != 1 && m_triedUVBoard[m_chartToY - i - 1][m_chartToX - j - 1])
                    m_UVBoard[i][j] = 
                        m_triedUVBoard[m_chartToY - i - 1][m_chartToX - j - 1];
        transMatrix = XMMatrixTranslation(
            m_PixelWidth * m_chartToX - pPosInfo->basePoint.x,
            m_PixelWidth * m_chartToY - pPosInfo->basePoint.y, 0.0f);
        break;
    case 270:
        for (int i = m_chartFromY; i < m_chartToY; i++)
            for (int j = m_chartFromX; j < m_chartToX; j++)
                if (m_UVBoard[i][j] != 1 && m_triedUVBoard[j - m_chartFromX][m_chartToY - i - 1])
                    m_UVBoard[i][j] = 
                        m_triedUVBoard[j - m_chartFromX][m_chartToY - i - 1];
        transMatrix = XMMatrixTranslation(
            m_PixelWidth * m_chartFromX - pPosInfo->basePoint.x,
            m_PixelWidth * m_chartToY - pPosInfo->basePoint.y, 0.0f);
        break;
    }

    XMStoreFloat4x4(&m_ResultMatrix[index], matrixRotate * transMatrix);
    UpdateSpaceInfo(m_triedPutSide);
}

/***************************************************************************\
    Function Description:
        Update the distance between the atlas edges and the corresponding
        edges of newly created atlas.
    
    Arguments:
        [in]	direction	-	The side which need to be update.		
    
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::UpdateSpaceInfo(int direction)
{
    int minX = (m_chartFromX < m_fromX) ? m_chartFromX : m_fromX;
    int minY = (m_chartFromY < m_fromY) ? m_chartFromY : m_fromY;
    int maxX = (m_chartToX > m_toX) ? m_chartToX : m_toX;
    int maxY = (m_chartToY > m_toY) ? m_chartToY : m_toY;

    switch (direction)
    {
    case UV_UPSIDE:
        if (m_chartFromY < m_fromY) {
            for (int i = m_fromX; i < m_chartFromX; i++)
                m_SpaceInfo[UV_UPSIDE][i] += m_fromY - m_chartFromY;
            for (int i = m_chartToX; i < m_toX; i++)
                m_SpaceInfo[UV_UPSIDE][i] += m_fromY - m_chartFromY;
        }
        for (int i = m_chartFromX; i < m_chartToX; i++)
        {
            int j = minY;
            while (j < maxY && m_UVBoard[j++][i] == 0);
            m_SpaceInfo[UV_UPSIDE][i] = j - minY - 1;
        }
        for (int i = m_chartFromY; i < m_chartToY; i++)
        {
            int j = minX;
            while (j < maxX && m_UVBoard[i][j++] == 0);
            m_SpaceInfo[UV_LEFTSIDE][i] = j - minX - 1;
            j = maxX;
            while (j > minX && m_UVBoard[i][--j] == 0);
            m_SpaceInfo[UV_RIGHTSIDE][i] = maxX - j - 1;
        }
        break;
    case UV_DOWNSIDE:
        if (m_toY < m_chartToY) {
            for (int i = m_fromX; i < m_chartFromX; i++)
                m_SpaceInfo[UV_DOWNSIDE][i] += m_chartToY - m_toY;
            for (int i = m_chartToX; i < m_toX; i++) 
                m_SpaceInfo[UV_DOWNSIDE][i] += m_chartToY - m_toY;
        }
        for (int i = m_chartFromX; i < m_chartToX; i++)
        {
            int j = maxY;
            while (j > minY && m_UVBoard[--j][i] == 0);
            m_SpaceInfo[UV_DOWNSIDE][i] = maxY - j - 1;
        }
        for (int i = m_chartFromY; i < m_chartToY; i++)
        {
            int j = minX;
            while (j < maxX && m_UVBoard[i][j++] == 0);
            m_SpaceInfo[UV_LEFTSIDE][i] = j - minX - 1;
            j = maxX;
            while (j > minX && m_UVBoard[i][--j] == 0);
            m_SpaceInfo[UV_RIGHTSIDE][i] = maxX - j - 1;
        }		
        break;
    case UV_LEFTSIDE:
        if (m_chartFromX < m_fromX) {
            for (int i = m_fromY; i < m_chartFromY; i++)
                m_SpaceInfo[UV_LEFTSIDE][i] += m_fromX - m_chartFromX;
            for (int i = m_chartToY; i < m_toY; i++)
                m_SpaceInfo[UV_LEFTSIDE][i] += m_fromX - m_chartFromX;
        }
        for (int i = m_chartFromY; i < m_chartToY; i++)
        {
            int j = minX;
            while (j < maxX && m_UVBoard[i][j++] == 0);
            m_SpaceInfo[UV_LEFTSIDE][i] = j - minX - 1;
        }
        for (int i = m_chartFromX; i < m_chartToX; i++)
        {
            int j = minY;
            while (j < maxY && m_UVBoard[j++][i] == 0);
            m_SpaceInfo[UV_UPSIDE][i] = j - minY - 1;
            j = maxY;
            while (j > minY && m_UVBoard[--j][i] == 0);
            m_SpaceInfo[UV_DOWNSIDE][i] = maxY - j - 1;
        }	
        break;
    case UV_RIGHTSIDE:
        if (m_chartToX > m_toX) {
            for (int i = m_fromY; i < m_chartFromY; i++)
                m_SpaceInfo[UV_RIGHTSIDE][i] += m_chartToX - m_toX;
            for (int i = m_chartToY; i < m_toY; i++)
                m_SpaceInfo[UV_RIGHTSIDE][i] += m_chartToX - m_toX;
        }
        for (int i = m_chartFromY; i < m_chartToY; i++)
        {
            int j = maxX;
            while (j > minX && m_UVBoard[i][--j] == 0);
            m_SpaceInfo[UV_RIGHTSIDE][i] = maxX - j - 1;
        }
        for (int i = m_chartFromX; i < m_chartToX; i++)
        {
            int j = minY;
            while (j < maxY && m_UVBoard[j++][i] == 0);
            m_SpaceInfo[UV_UPSIDE][i] = j - minY - 1;
            j = maxY;
            while (j > minY && m_UVBoard[--j][i] == 0);
            m_SpaceInfo[UV_DOWNSIDE][i] = maxY - j - 1;
        }	
        break;
    }

    m_fromX = minX;
    m_fromY = minY;
    m_toX = maxX;
    m_toY = maxY;
}


/***************************************************************************\
    Function Description:
        Normalize the result UV coordinates.
    
    Arguments:
    Return Value:
\***************************************************************************/
void CUVAtlasRepacker::Normalize()
{
    XMMATRIX transMatrix, scalMatrix, matrix;

    transMatrix = XMMatrixTranslation(-m_PixelWidth * (m_fromX + m_iGutter),
        -m_PixelWidth * (m_fromY + m_iGutter), 0.0f);
    scalMatrix = XMMatrixScaling(1.0f / m_PixelWidth / m_NormalizeLen,
        1.0f / m_PixelWidth / m_NormalizeLen, 0.0f);

    for (size_t i = 0; i < m_iNumCharts; i++)
    {
        if (m_ChartsInfo[i].valid) 
        {
            matrix = XMLoadFloat4x4(&m_ResultMatrix[i]) * transMatrix * scalMatrix;
            XMVector2TransformCoordStream(
                &m_VertexBuffer[m_AttrTable[i].VertexStart].uv, 
                VertexSize,
                &m_VertexBuffer[m_AttrTable[i].VertexStart].uv, 
                VertexSize, 
                m_AttrTable[i].VertexCount,
                matrix
                );
        } 
        else 
        {
            for (size_t j = 0; j < m_AttrTable[i].VertexCount; j++)
            {
                m_VertexBuffer[j + m_AttrTable[i].VertexStart].uv.x = 0.0f;
                m_VertexBuffer[j + m_AttrTable[i].VertexStart].uv.y = 0.0f;
            }
        }
    }
}

/***************************************************************************\
    Function Description:
        Output the result into user specified buffer.
    
    Arguments:	
    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::OutPutPackResult()
{
    m_vFacePartitioning.resize(m_iNumFaces);
    memcpy(m_vFacePartitioning.data(), m_vAttributeBuffer.data(), m_iNumFaces * sizeof(uint32_t));

    m_vAttributeID.resize(m_iNumFaces);
    memcpy(m_vAttributeID.data(), m_vAttributeBuffer.data(), m_iNumFaces * sizeof(uint32_t));

    // copy the new UV coordinates from our vertex buffer to the original 
    // vertex buffer according to the original vertex order
    auto pVB = reinterpret_cast<uint8_t*>( m_pvVertexBuffer->data() );
    for (size_t i = 0; i < m_IndexPartition.size(); i++)
    {
        // handle the situation when the chart has only one vertex.
        // we just move all these vertex UV into coordinates (0, 0)
        if (m_IndexPartition[i] == -1)
        {
            float *pp = (float *)(pVB + i * m_iNumBytesPerVertex);
            *pp = 0;
            *(pp + 1) = 0;
            *(pp + 2) = 0;
            continue;
        }

        memcpy(pVB + i * m_iNumBytesPerVertex + m_TexCoordOffset, 
            (uint8_t*)&(m_VertexBuffer[m_IndexPartition[i]].uv), 
            sizeof(XMFLOAT2));
    }
}


/***************************************************************************\
Function Description:
Return the area of specified chart

Arguments:	
index	:	the index of chart

Return Value:
The area of specified chart
\***************************************************************************/
float CUVAtlasRepacker::GetChartArea(uint32_t index) const
{
    float Area = 0;
    float currArea = 0;
    size_t faceEnd = m_AttrTable[index].FaceStart + m_AttrTable[index].FaceCount;

    for (size_t i = m_AttrTable[index].FaceStart; i < faceEnd; i++)
    {
        const XMFLOAT2 *p1 = &m_VertexBuffer[m_IndexPartition[m_IndexBuffer[3 * i]]].uv;
        const XMFLOAT2 *p2 = &m_VertexBuffer[m_IndexPartition[m_IndexBuffer[3 * i + 1]]].uv;
        const XMFLOAT2 *p3 = &m_VertexBuffer[m_IndexPartition[m_IndexBuffer[3 * i + 2]]].uv;
        currArea = fabs((p1->x - p3->x)*(p2->y - p3->y) - (p2->x - p3->x)*(p1->y - p3->y)) / 2;
        Area += currArea;
    }

    return Area;
}

/***************************************************************************\
    Function Description:
        Compute the total area of all the charts.
    
    Arguments:	
    Return Value:	
\***************************************************************************/
template <class T>
float CUVAtlasRepacker::GetTotalArea() const
{
    auto pVB = reinterpret_cast<const uint8_t*>( m_pvVertexBuffer->data() );
    auto pIB = reinterpret_cast<const uint8_t*>( m_pvIndexBuffer->data() );

    float Area = 0;
    for (size_t i = 0; i < m_iNumFaces; i++)
    {
        XMFLOAT2 *p1 =
            (XMFLOAT2 *)(pVB + *(T *)(pIB + 3 * i * sizeof(T)) *
            m_iNumBytesPerVertex + m_TexCoordOffset);
        XMFLOAT2 *p2 =
            (XMFLOAT2 *)(pVB + *(T *)(pIB + (3 * i + 1) * sizeof(T)) *
            m_iNumBytesPerVertex + m_TexCoordOffset);
        XMFLOAT2 *p3 =
            (XMFLOAT2 *)(pVB + *(T *)(pIB + (3 * i + 2) * sizeof(T)) *
            m_iNumBytesPerVertex + m_TexCoordOffset);
        float s = fabs((p1->x - p3->x)*(p2->y - p3->y) - 
                    (p2->x - p3->x)*(p1->y - p3->y)) / 2;
        Area += s;
    }

    return Area;
}

/***************************************************************************\
    Function Description:
        Do tessellation on every charts we found.
    
    Arguments:
        [in]	ChartIndex	-	The index of chart into the m_ChartsInfo.
        [in]	AngleIndex	-	Specify the angle the chart is rotated.
            
    Return Value:
        TRUE if success;
        FALSE otherwise.	
\***************************************************************************/
bool CUVAtlasRepacker::DoTessellation(uint32_t ChartIndex, size_t AngleIndex)
{
    ChartsInfo *pCInfo = (ChartsInfo *)&(m_ChartsInfo[ChartIndex]);
    _PositionInfo *pPosInfo = (_PositionInfo *)&(pCInfo->PosInfo[AngleIndex]);

    int numX = pPosInfo->numX;
    int numY = pPosInfo->numY;

    XMFLOAT2 minP;
    XMStoreFloat2(&minP, XMLoadFloat2(&pPosInfo->minPoint) - XMLoadFloat2(&pPosInfo->adjustLen));

    // initialize the current chart atlas
    for (int i = 0; i < numY; i++)
        for (int j = 0; j < numX; j++) 
            m_currChartUVBoard[i][j] = 0;

    // do tessellation by test the intersection of chart edges and the grids
    int numgrid = 0;
    for (size_t i = 0; i < pPosInfo->edges.size(); i++)
    {
        // get one edge, it is constituted by two points
        XMFLOAT2 *p1 = &(pPosInfo->edges[i].p1);
        XMFLOAT2 *p2 = &(pPosInfo->edges[i].p2);
        XMFLOAT2 *pMinP = &(pPosInfo->edges[i].minP);
        XMFLOAT2 *pMaxP = &(pPosInfo->edges[i].maxP);

        // find the edge's bounding box
        // the intersection test will be made in the bounding box range
        int fromX = (int) floorf((pMinP->x - minP.x) / m_PixelWidth);
        int toX = (int) ceilf((pMaxP->x  - minP.x) / m_PixelWidth);
        int fromY = (int) floorf((pMinP->y - minP.y) / m_PixelWidth);
        int toY = (int) ceilf((pMaxP->y - minP.y) / m_PixelWidth);

        int m, n;
        if (toX - fromX <= 1 && toY - fromY <= 1)
        {
            m_currChartUVBoard[fromY + m_iGutter][fromX + m_iGutter] = 1;
            numgrid++;
            continue;
        }
        else if (toX - fromX <= 1)
        {
            n = (int) floorf((p1->x - minP.x) / m_PixelWidth);
            for (m = fromY + 1; m < toY; m++)
            {
                m_currChartUVBoard[m + m_iGutter][n + m_iGutter] = 1;
                m_currChartUVBoard[m + m_iGutter - 1][n + m_iGutter] = 1;
                numgrid += 2;
            }
            continue;
        } 
        else if (toY - fromY <= 1)
        {
            m = (int) floorf((p1->y - minP.y) / m_PixelWidth);
            for (n = fromX + 1; n < toX; n++)
            {
                m_currChartUVBoard[m + m_iGutter][n + m_iGutter] = 1;
                m_currChartUVBoard[m + m_iGutter][n + m_iGutter - 1] = 1;
                numgrid += 2;
            }
            continue;		
        }

        float slope = (p2->y - p1->y) / (p2->x - p1->x);
        float b = p1->y - p1->x * slope;
        float x, y;

        if (fabs(slope) < 1.0f)
        {
            for (n = fromX + 1; n < toX; n++)
            {
                x = minP.x + n * m_PixelWidth;
                y = slope * x + b;
                m = (int) floorf((y - minP.y) / m_PixelWidth);

                m_currChartUVBoard[m + m_iGutter][n + m_iGutter] = 1;
                m_currChartUVBoard[m + m_iGutter][n + m_iGutter - 1] = 1;
                numgrid += 2;
            }
        }
        else
        {
            for (m = fromY + 1; m < toY; m++)
            {
                y = minP.y + m * m_PixelWidth;
                x = (y - b) / slope;

                n = (int) floorf((x - minP.x) / m_PixelWidth);

                m_currChartUVBoard[m + m_iGutter][n + m_iGutter] = 1;
                m_currChartUVBoard[m + m_iGutter - 1][n + m_iGutter] = 1;
                numgrid += 2;
            }
        }
    }

    if (numgrid == 0 && numX != m_iGutter * 2 && numY != m_iGutter * 2)
        return false;

    // Grow the specified chart by the length of gutter 
    // to make the chart not be too close.
    GrowChart(ChartIndex, AngleIndex, m_iGutter);

    return true;
}

/***************************************************************************\
    Function Description:
        Grow the specified chart by the length of gutter to make the chart
        will not be too close.
    
    Arguments:
        [in]	chartindex	-	The index of chart into the m_ChartsInfo.
        [in]	angleindex	-	Specify the angle the chart is rotated.
        [in]	layer		-	Specify the layer needed to grow. Actually 
                                it is gutter.	

    Return Value:	
\***************************************************************************/
void CUVAtlasRepacker::GrowChart(uint32_t chartindex, size_t angleindex, int layer)
{
    size_t numY = m_ChartsInfo[chartindex].PosInfo[angleindex].numY;
    size_t numX = m_ChartsInfo[chartindex].PosInfo[angleindex].numX;
    for (int i = 0; i < layer; i++)
    {
        for (size_t m = 0; m < numY; m++)
        {
            for (size_t n = 0; n < numX; n++)
            {
                if (m_currChartUVBoard[m][n] == i + 1)
                {
                    for (int j = -1; j < 2; j++)
                    {
                        if ( (ptrdiff_t(m)+j) < 0 || (m+j) >= m_currChartUVBoard.size() )
                            continue;

                        for (int k = -1; k < 2; k++)
                        {
                            if ( (ptrdiff_t(n)+k) < 0 || (n+k) >= m_currChartUVBoard[m+j].size() )
                                continue;

                            if (m_currChartUVBoard[m+j][n+k] == 0)
                                m_currChartUVBoard[m+j][n+k] = static_cast<uint8_t>( i + 2 );
                        }
                    }
                }
            }
        }
    }
}
