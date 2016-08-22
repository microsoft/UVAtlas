//-------------------------------------------------------------------------------------
// UVAtlas - UVAtlasRepacker.h
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

#include "callbackschemer.h"
#include "isochart.h"

namespace IsochartRepacker
{

// Represent the four sides of chart or UV atlas
const int UV_UPSIDE = 0;
const int UV_RIGHTSIDE = 1;
const int UV_DOWNSIDE = 2;
const int UV_LEFTSIDE = 3;

const size_t CHART_THRESHOLD = 30;
const size_t MAX_ITERATION = 200;

// the size of input vertex buffer unit
const size_t VertexSize = 20;

// specify the initial size of the atlas is 3*3 times larger 
// than the user defined atlas
const int INITIAL_SIZE_FACTOR = 2;

// convert the index buffer into this structure for convenience
// use template to handle 16-bit index and 32-bit index
template <class T>
struct _Triangle {
    T vertex[3];
};

// describe the chart edges
struct _EDGE {
    DirectX::XMFLOAT2 p1;
    DirectX::XMFLOAT2 p2;
    DirectX::XMFLOAT2 minP;
    DirectX::XMFLOAT2 maxP;
    _EDGE() = default;
    _EDGE(const DirectX::XMFLOAT2& _p1, const DirectX::XMFLOAT2& _p2) : p1(_p1), p2(_p2) {
        minP.x = std::min(p1.x, p2.x);
        maxP.x = std::max(p1.x, p2.x);
        minP.y = std::min(p1.y, p2.y);
        maxP.y = std::max(p1.y, p2.y);
    };
};

// save the information about the chart in a specific position and rotate angle
struct _PositionInfo {					
    DirectX::XMFLOAT2 basePoint;    // record one specific point in a corner point of tessellation grids
                                    // we can get the transformation matrix through this point
    DirectX::XMFLOAT2 minPoint;     // top left corner of bounding box
    DirectX::XMFLOAT2 maxPoint;     // bottom right corner of bounding box
    int numX;                       // number of grids in X direction
    int numY;                       // number of grids in Y direction
    DirectX::XMFLOAT2 adjustLen;    // make the chart in a best place
    float angle;                    // chart rotate angle from the original position
    std::vector<_EDGE> edges;       // describe the edges of each chart
};

// save chart information
struct ChartsInfo {
    float maxLength;                    // the max length of a chart in x direction or y direction
    bool valid;                         // if the chart information is valid
    float area;
    std::vector<_PositionInfo> PosInfo; // Position information of different angle
    ChartsInfo() : maxLength(0.0), valid(false), area(0.0) {} ;    
};

// 2-dimension matrix to describe the UV atlas
typedef std::vector<std::vector<uint8_t> > UVBoard;

// distance between chart edges and its corresponding bounding box edges
typedef std::vector<int> SpaceInfo[4];

struct UVATLASATTRIBUTERANGE
{
    uint32_t AttribId;
    uint32_t FaceStart;
    uint32_t FaceCount;
    uint32_t VertexStart;
    uint32_t VertexCount;
};

/***************************************************************************\
    Function Description:
        Pack mesh partitioning data into an atlas.
    
    Arguments:
        [in]	pVertexArray	-	Pointer to an input vertex buffer.
        [in]	VertexCount		-	Vertex number in pVertexArray.
        [in]	pFaceIndexArray	-	Pointer to an input index buffer.
        [in]	FaceCount		-	Face number of input data.
        [in]	pAttributeID	-	A pointer to an array of the final 
                                    face-partationing data. array Each 
                                    element contains one uint32_t per face
        [in]    pdwAdjacency    -   The output result adjacency from the previous isochart partition function
        [in]	Width			-	Texture width in pixel.
        [in]	Height			-	Texture height in pixel.
        [in]	Gutter			-	The minimum distance, in texels, 
                                    between two charts on the atlas. 
                                    The gutter is always scaled by the 
                                    width; so, if a gutter of 2.5 is 
                                    used on a 512x512 texture, then 
                                    the minimum distance between two 
                                    charts is 2.5 / 512.0 texels.
        [in]	pCallback		-	A pointer to a callback function 
                                    that is useful for monitoring progress.
        [in]	Frequency		-	Specify how often the function will call the 
                                    callback; a reasonable default value 
                                    is 0.0001f.
        [in]	iNumRotate		-	The tentative times of rotation on one
                                    chart between 0 and 90 degrees when put
                                    the	chart into atlas. The default value 
                                    is 5 which means the chart rotates one
                                    time every 90 / 5 degrees.

    Return Value:
        If the function succeeds, the return value is S_OK; otherwise, 
        the value is E_INVALIDARG.
\***************************************************************************/

HRESULT WINAPI isochartpack2(_In_                       std::vector<DirectX::UVAtlasVertex>* pvVertexArray,
                             _In_                       size_t VertexCount, 
                             _In_                       std::vector<uint8_t>* pvIndexFaceArray,
                             _In_                       size_t FaceCount, 
                             _In_reads_(FaceCount*3)    const uint32_t *pdwAdjacency,
                             _In_                       size_t Width, 
                             _In_                       size_t Height,
                             _In_                       float Gutter,
                             _In_                       unsigned int Stage,
                             _In_opt_                   Isochart::LPISOCHARTCALLBACK pCallback = nullptr, 
                             _In_                       float Frequency = 0.01f, 
                             _In_                       size_t iNumRotate = 5);

class CUVAtlasRepacker
{
public:
    CUVAtlasRepacker(std::vector<DirectX::UVAtlasVertex>* pvVertexArray,
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
                        size_t *pIterationTimes);

    ~CUVAtlasRepacker();
    
    bool SetCallback( Isochart::LPISOCHARTCALLBACK pCallback, float Frequency ) ;
    bool SetStage(unsigned int TotalStageCount, unsigned int DoneStageCount);
    HRESULT Repack();

private:

    template <class T>
    float GetTotalArea() const;

    bool DoTessellation(uint32_t ChartIndex, size_t AngleIndex);

    template <class T>
    HRESULT GenerateAdjacentInfo();
    
    HRESULT PrepareChartsInfo();

    template <class T>
    HRESULT GenerateNewBuffers();

    void SortCharts();

    HRESULT CreateUVAtlas();
    HRESULT PrepareRepack();
    void PutChart(uint32_t index);
    void UpdateSpaceInfo(int direction);
    void TryPut(int chartPutSide, int PutSide, int Rotation, int chartWidth, 
                int width, int from, int to, int chartSideLen);
    void PutChartInPosition(uint32_t index);
    void Normalize();
    void GrowChart(uint32_t chartindex, size_t angleindex, int layer);
    void CleanUp();
    void PrepareSpaceInfo(SpaceInfo &spaceInfo, UVBoard &board, int fromX, 
        int toX, int fromY, int toY, bool bNeglectGrows);
    HRESULT Initialize();
    void ComputeBoundingBox(std::vector<DirectX::XMFLOAT2>& Vec, DirectX::XMFLOAT2* minV, DirectX::XMFLOAT2* maxV);
    void ComputeFinalAtlasRect();
    void Reverse(std::vector<int>& data, size_t len);
    void OutPutPackResult();
    bool PossiblePack();
    void InitialSpacePercent();
    bool CheckAtlasRange();
    void GetChartPutPosition(uint32_t index);
    bool CheckUserInput();
    void ComputeChartsLengthInPixel();
    void AdjustEstimatedPercent();
    float GetChartArea(uint32_t index) const;


private:
    const uint32_t* m_pPartitionAdj;

    std::vector<DirectX::UVAtlasVertex>* m_pvVertexBuffer;  // pointer to the input vertex buffer
    std::vector<uint8_t>*	    m_pvIndexBuffer;            // pointer to the input index buffer
    std::vector<uint32_t>		m_vAttributeBuffer;         // pointer to the attribute buffer
                                                            // generated in GenerateNewBuffers according to m_pPartitionAdj
    float						m_EstimatedSpacePercent;    // the ratio of final charts area to the total area of UV atlas.
    bool						m_OutOfRange;               // if the current atlas is out of user defined atlas

    std::vector<uint32_t>		m_vAttributeID;			    // attribute buffer to be output
    std::vector<uint32_t>		m_vFacePartitioning;		// the output face partition information
                                                            // (output) the output face partitioning data
    bool						m_bDwIndex;					// whether the index buffer is uint32_t
    bool						m_bStopIteration;			// if the iterate operation should stop

    int							m_TexCoordOffset;			// the offset of the needed data in vertex buffer
    size_t                      m_iRotateNum;               // describe the rotation times of each chart when pack

    size_t						m_iNumCharts;				// the number of chart
    size_t						m_iNumVertices;             // the number of vertex
    size_t						m_iNumFaces;                // the number of faces (triangles)
    size_t						m_iNumBytesPerVertex;       // the number of bytes per vertex in vertex buffer

    float						m_fChartsTotalArea;         // the area of all the charts
                                                            // used to calculate the pixel length
    size_t						m_dwAtlasHeight;            // user defined atlas height in pixel
    size_t						m_dwAtlasWidth;             // user defined atlas width in pixel
    float						m_AspectRatio;              // the user defined ratio of atlas width and height
    int							m_iGutter;                  // the minimal distance between two chart

    bool						m_bRepacked;                // if the repack operation is over

    float						m_adjustFactor;
    float						m_packedArea;
    int							m_packedCharts;

    // describe the current atlas's range in X and Y coordinates
    int							m_fromX;					
    int							m_toX;						
    int							m_fromY;					
    int							m_toY;

    int							m_iIterationTimes;

    // describe the current chart's range in X and Y coordinates
    int							m_chartFromX;
    int							m_chartToX;
    int							m_chartFromY;
    int							m_chartToY;

    float						m_currAspectRatio;          // current aspect ratio of atlas width and height
    int							m_currRotate;               // current chart's rotate degrees

    // save the values during process
    size_t                      m_triedRotate;
    int							m_triedInternalSpace;
    int							m_triedPutPos;
    int							m_triedOverlappedLen;
    int							m_triedPutRotation;
    int							m_triedPutSide;
    float						m_triedAspectRatio;
    UVBoard						m_triedUVBoard;

    int							m_NormalizeLen;

    // the original atlas prepared for packing
    size_t						m_PreparedAtlasWidth;
    size_t						m_PreparedAtlasHeight;

    size_t						m_RealWidth;                // the repacked UV atlas width
    size_t						m_RealHeight;               // the repacked UV atlas height
    float						m_PixelWidth;               // the estimated pixel width

    SpaceInfo					m_SpaceInfo;                // the main UV board space information
    SpaceInfo					m_currSpaceInfo;            // current chart space information

    UVBoard						m_UVBoard;                  // the main UV board in which we want to pack charts
    UVBoard						m_currChartUVBoard;         // current chart UV board

    std::vector<DirectX::UVAtlasVertex> m_VertexBuffer;
    std::vector<uint32_t>               m_IndexBuffer;
    std::vector<uint32_t>               m_AdjacentInfo;
    std::vector<UVATLASATTRIBUTERANGE>  m_AttrTable;
    std::vector<uint32_t>               m_NewAdjacentInfo;

    std::vector<uint32_t>               m_IndexPartition;

    std::vector<ChartsInfo>             m_ChartsInfo;       // information of each chart
    std::vector<uint32_t>               m_SortedChartIndex; // sorted index of charts
    std::vector<DirectX::XMFLOAT4X4>    m_ResultMatrix;     // result matrix of each chart

    std::vector<std::vector<uint32_t> >    m_VertexAdjInfo;

    double *m_pPercentOur;    
    size_t *m_pFinalWidth;
    size_t *m_pFinalHeight;
    size_t *m_pOurChartNumber;
    size_t *m_pOurIterationTimes;

    Isochart::CCallbackSchemer m_callbackSchemer ;
};

}
