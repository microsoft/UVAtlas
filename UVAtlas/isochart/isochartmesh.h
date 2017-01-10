//-------------------------------------------------------------------------------------
// UVAtlas - isochartmesh.h
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
#include "graphcut.h"
#include "isochart.h"
#include "isomap.h"
#include "isochartengine.h"
#include "isochartutil.h"
#include "sparsematrix.hpp"

#include "geodesics\ExactOneToAll.h"
#include "geodesics\ApproximateOneToAll.h"

namespace Isochart
{

const uint32_t INVALID_FACE_ID = 0xffffffff; // INVALID FACE ID
const uint32_t INVALID_VERT_ID = 0xffffffff; // INVALID VERTEX ID
const uint32_t INVALID_INDEX = 0xffffffff;   // INVALID_INDEX

// Vertex with MUST_RESERVE importance order must be reserved.
const int MUST_RESERVE = -1;

// The eigen values and vectors need to compute when
// processing the original charts which haven't been partitioned
// before.
const size_t ORIGINAL_CHART_EIGEN_DIMENSION = 10;

inline bool isInArray(const std::vector<uint32_t>& v, uint32_t item)
{
    return std::find(v.cbegin(), v.cend(), item) != v.cend();
}

inline void removeItem(std::vector<uint32_t>& v, uint32_t item)
{
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

inline bool addNoduplicateItem(std::vector<uint32_t>& v, uint32_t item)
{
    try
    {
        if (std::find(v.cbegin(), v.cend(), item) == v.cend())
            v.push_back(item);
        return true;
    }
    catch (std::bad_alloc&)
    {
        return false;
    }
}

class CIsochartMesh;
typedef std::vector<CIsochartMesh*> ISOCHARTMESH_ARRAY;

///////////////////////////////////////////////////////////////
//////////Main Structures in CIsochartMesh/////////////////////////
///////////////////////////////////////////////////////////////
struct ISOCHARTVERTEX
{
    uint32_t dwID;                  // Index in the vertex array of current mesh
    uint32_t dwIDInRootMesh;        //ID of this vertex in root chart
    uint32_t dwIDInFatherMesh;      //ID of this vertex in father chart

    DirectX::XMFLOAT2 uv;           //UV coordinate in texture map

    bool bIsLandmark;               // Is this vertex a landmark
    uint32_t dwIndexInLandmarkList; // For landmark, indicate its index in landmark list

    bool bIsBoundary;               // Is this vertex a boundary vertex

    int nImportanceOrder;           // Important order of this vertex
    float fGeodesicDistance;        //Using in Computing distance from this vertex to specified sourc
    float fDijikstraDistance;
    float fSignalDistance;          // Signal distance

    std::vector<uint32_t> vertAdjacent;// ID of vertices having edge between this vertex
    std::vector<uint32_t> faceAdjacent;// ID of faces using this vertex
    std::vector<uint32_t> edgeAdjacent;// ID of edges using this vertex
    uint32_t dwNextVertIDOnPath;    // The next vertex on the path to source.
};
typedef std::vector<ISOCHARTVERTEX*> VERTEX_ARRAY;

struct ISOCHARTFACE
{
    uint32_t dwID;              // Index in the faces array of current mesh
    uint32_t dwIDInRootMesh;    // The ID of this face in root chart
    uint32_t dwIDInFatherMesh;  // The ID of this face in face chart
    uint32_t dwVertexID[3];     // The ID of 3 vertices of this face
    uint32_t dwEdgeID[3];       // The ID of 3 edges of this face
};
typedef std::vector<ISOCHARTFACE*> FACE_ARRAY;

struct ISOCHARTEDGE
{
    uint32_t dwID;              // Index in the edge array of current mesh
    uint32_t dwVertexID[2];     // The ID of 2 vertices of this edge
    uint32_t dwFaceID[2];       // The ID of 2 faces at the two sides of the edge.
                                // if the edge has only one face beside it,
                                // dwFaceID[1] should be INVALID_FACE_ID
    uint32_t dwOppositVertID[2];// Vertex opposite to the edge in the face

    float fLength;              //The length of the edge
    float fSignalLength;        // The length adjusted by IMT
    bool bIsBoundary;           // Indicate if the edge a boundary.
    bool bCanBeSplit;           // Indicate if the edge can be splitted, boundary edges always be set to true
};
typedef std::vector<ISOCHARTEDGE*> EDGE_ARRAY;

class CCallbackSchemer;
class CIsoMap;

struct PACKINGINFO;

struct ATLASINFO;

struct CHARTOPTIMIZEINFO;

struct VERTOPTIMIZEINFO;

class CIsochartMesh
{
public:
    /////////////////////////////////////////////////////////////
    /////////////////Constructor and Decstructor/////////////////
    /////////////////////////////////////////////////////////////
    CIsochartMesh(
        const CBaseMeshInfo &baseInfo,
        CCallbackSchemer& callbackSchemer,
        const CIsochartEngine &IsochartEngine);

    CIsochartMesh(CIsochartMesh const&) = delete;
    CIsochartMesh& operator=(CIsochartMesh const&) = delete;

    ~CIsochartMesh();

    /////////////////////////////////////////////////////////////
    //////////////////////Class Public Method.///////////////////
    /////////////////////////////////////////////////////////////
public:
    static HRESULT BuildRootChart(
        CBaseMeshInfo& baseInfo,
        const void* pFaceIndexArray,
        DXGI_FORMAT IndexFormat,
        CIsochartMesh* pChart,
        bool bIsForPartition);

    static HRESULT MergeSmallCharts(
        ISOCHARTMESH_ARRAY &chartList,
        size_t dwExpectChartCount,
        const CBaseMeshInfo& baseInfo,
        CCallbackSchemer& callbackSchemer);

    static HRESULT CheckMerageResult(
        ISOCHARTMESH_ARRAY &chartList,
        CIsochartMesh* pOldChart1,
        CIsochartMesh* pOldChart2,
        CIsochartMesh* pNewChart,
        bool& bCanMerge);

    static HRESULT OptimizeAllL2SquaredStretch(
        ISOCHARTMESH_ARRAY &chartList,
        bool bOptimizeSignal);

    static HRESULT OptimalScaleChart(
        ISOCHARTMESH_ARRAY& chartList,
        float fOpticalAvgL2SquaredStretch,
        bool bOptimizeSignal);

    static float ComputeGeoAvgL2Stretch(
        ISOCHARTMESH_ARRAY& chartList,
        bool bReCompute);

    static HRESULT PackingCharts(
        ISOCHARTMESH_ARRAY &chartList,
        size_t dwWidth, 
        size_t dwHeight,
        float gutter,
        CCallbackSchemer& callbackSchemer);

    ////////////////////////////////////////////////////////
    ///////// Public Processing Conterol/////////////////////////
    ////////////////////////////////////////////////////////
    static void ConvertToInternalCriterion(
        float fStretch,
        float& fCriterion,
        bool bIsSignalSpecialized);

    static float ConvertToExternalStretch(
        float fTotalAvgL2SquaredStretch,
        bool bIsSignalSpecialized);

    static bool IsReachExpectedTotalAvgL2SqrStretch(
        float fCurrAvgL2SqrStretch,
        float fExpectRatio);

    static uint32_t GetBestPartitionCanidate(
        ISOCHARTMESH_ARRAY &chartList);

    static float CalOptimalAvgL2SquaredStretch(
        ISOCHARTMESH_ARRAY& chartList); // Scale each chart.

    static uint32_t GetChartWidthLargestGeoAvgStretch(
        ISOCHARTMESH_ARRAY &chartList,
        float& fMaxAvgL2Stretch);

    /////////////////////////////////////////////////////////////
    /////////////////Algorithm Public Methods///////////////////////
    /////////////////////////////////////////////////////////////
    HRESULT PrepareProcessing(
        bool bIsForPartition);

    HRESULT Partition();

    HRESULT Bipartition3D();
    HRESULT Bipartition2D();

    HRESULT ComputeBiParitionLandmark();
    /////////////////////////////////////////////////////////
    //////////////States & Property of Chart/////////////////////
    /////////////////////////////////////////////////////////
    bool IsImportanceCaculationDone()
    { 
        // Has computed the importance of vertices.
        return m_bVertImportanceDone; 
    }

    bool IsInitChart() const { return m_bIsInitChart; }
    bool IsOptimizedL2Stretch() const { return m_bOptimizedL2Stretch; }
    bool IsIMTSpecified() const { return m_baseInfo.pfIMTArray != 0; }
    bool HasBoundaryVertex() const;

    /////////////////////////////////////////////////////////////
    //////////////Basic Data Member Access Methods///////////////
    /////////////////////////////////////////////////////////////
    size_t GetVertexNumber(){ return m_dwVertNumber; }
    ISOCHARTVERTEX* GetVertexBuffer() const { return m_pVerts; };

    size_t GetFaceNumber(){ return m_dwFaceNumber; }
    ISOCHARTFACE* GetFaceBuffer() const { return m_pFaces; }

    size_t GetEdgeNumber(){ return m_dwEdgeNumber; }
    std::vector<ISOCHARTEDGE>& GetEdgesList() { return  m_edges; }

    float GetBoxDiagLen() { return m_fBoxDiagLen; }
    std::vector<uint32_t>& GetAdjacentChartList() { return m_adjacentChart; }

    void SetInitChart() {m_bIsInitChart = true; }
    void SetParameterizedChart() {m_bIsParameterized = true; }

    PACKINGINFO* GetPackingInfoBuffer() const;
    float GetChart2DArea() const { return m_fChart2DArea; }
    float GetChart3DArea() const { return m_fChart3DArea; }

    const CBaseMeshInfo& GetBaseMeshInfo() const { return m_baseInfo; }

    float GetBaseL2SquaredStretch() const { return m_fBaseL2Stretch; }
    float GetL2SquaredStretch() const  { return m_fParamStretchL2; }

    
    /////////////////////////////////////////////////////////////
    ///////////////////Children Access Methods///////////////////
    /////////////////////////////////////////////////////////////
    bool HasChildren() const { return m_children.size() > 0; }

    size_t GetChildrenCount() const { return m_children.size(); }

    CIsochartMesh* GetChild(uint32_t dwIndex) const
    {
        if ( dwIndex >= m_children.size())
        {
            return nullptr;
        }
        return m_children[dwIndex];
    }
    void UnlinkChild(uint32_t dwIndex)
    {
        m_children[dwIndex] = nullptr;
    }

    void UnlinkAllChildren()
    {
        m_children.clear();
    }

private:
    void Free();
    /////////////////////////////////////////////////////////////
    ///////////////////////////Tool-Methods//////////////////////
    /////////////////////////////////////////////////////////////

    static HRESULT IsParameterizationOverlapping(
        CIsochartMesh* pMesh,
        bool& bIsOverlapping);

    float CalculateUVFaceArea(
        ISOCHARTFACE& face) const;

    float CaculateUVDistanceSquare(
        DirectX::XMFLOAT2& v0,
        DirectX::XMFLOAT2& v1) const;

    float CalculateUVFaceArea(
        DirectX::XMFLOAT2& v0,
        DirectX::XMFLOAT2& v1,
        DirectX::XMFLOAT2& v2) const;

    float CalculateChart2DArea() const;
    float CalculateChart3DArea() const;

    void CalculateChartEdgeLength();

    float CalculateVextexDistance(
        ISOCHARTVERTEX& v0,
        ISOCHARTVERTEX& v1) const;

    void RotateChart(
            const DirectX::XMFLOAT2& center,
            float fAngle) const;

    void GetRotatedChartBoundingBox(
        const DirectX::XMFLOAT2& center,
        float fAngle,
        DirectX::XMFLOAT2& minBound,
        DirectX::XMFLOAT2& maxBound) const;

    void CalculateChartMinimalBoundingBox(
        size_t dwRotationCount,
        DirectX::XMFLOAT2& minBound,
        DirectX::XMFLOAT2& maxBound) const;

    void Vertex3DTo2D(
        uint32_t dwFaceIDInRootMesh,
        const DirectX::XMFLOAT3* pOrg,
        const DirectX::XMFLOAT3* p3D,
        DirectX::XMFLOAT2* p2D);

    float CalculateEdgeSignalLength(
        ISOCHARTEDGE& edge);

    float CalculateEdgeSignalLength(
        DirectX::XMFLOAT3* p3D0,
        DirectX::XMFLOAT3* p3D1,
        uint32_t dwAdjacentFaceID0,
        uint32_t dwAdjacentFaceID1);

    float CalculateSignalLengthOnOneFace(
        DirectX::XMFLOAT3* p3D0,
        DirectX::XMFLOAT3* p3D1,
        uint32_t dwFaceID);

    CIsochartMesh* CreateNewChart(
        VERTEX_ARRAY& vertList,
        std::vector<uint32_t>& faceList,
        bool bIsSubChart) const;

    HRESULT MoveTwoValueToHead(
        std::vector<uint32_t>& list,
        uint32_t dwIdx1,
        uint32_t dwIdx2);
    /////////////////////////////////////////////////////////////
    /////////////////////Build Full Connection Methods///////////
    /////////////////////////////////////////////////////////////
    HRESULT ReBuildRootChartByAdjacence();
    
    HRESULT BuildFullConnection(
        bool& bIsManifold);

    void ClearVerticesAdjacence();

    HRESULT FindAllEdges(
        bool& bIsManifold);

    HRESULT SetEdgeSplitAttribute();	

    bool IsAllFaceVertexOrderValid();

    HRESULT SortAdjacentVertices(
        bool& bIsManifold);

    bool SortAdjacentVerticesOfBoundaryVertex(
        ISOCHARTVERTEX* pVertex);

    bool SortAdjacentVerticesOfInternalVertex(
        ISOCHARTVERTEX* pVertex);

    void GetFaceAdjacentArray(
        uint32_t* pdwFaceAdjacentArray) const;

    HRESULT CleanNonmanifoldMesh(bool& bCleaned);

    /////////////////////////////////////////////////////////////
    ///////////////////Simplify Chart Methods////////////////////
    /////////////////////////////////////////////////////////////

    HRESULT PrepareSimpleChart(
        bool bIsForPartition,
        size_t& dwBoundaryNumber,
        bool& bIsSimpleChart);

    HRESULT CheckAndDivideMultipleObjects(
        bool &bHasMultiObjects);

    HRESULT ExtractIndependentObject(
        VERTEX_ARRAY& vertList,
        CIsochartMesh** ppChart) const;

    HRESULT CheckAndCutMultipleBoundaries(
        size_t &dwBoundaryNumber);

    HRESULT FindAllBoundaries(
        size_t &dwBoundaryNumber,
        VERTEX_ARRAY& allBoundaryList,
        std::vector<uint32_t>& boundaryRecord,
        uint32_t *pdwVertBoundaryID);

    HRESULT DecreaseBoundary(
        size_t& dwBoundaryNumber,
        VERTEX_ARRAY& allBoundaryList,
        std::vector<uint32_t>& boundaryRecord,
        uint32_t *pdwVertBoundaryID);

    HRESULT CalVertWithMinDijkstraDistanceToSrc(
        uint32_t dwSourceVertID,
        uint32_t& dwPeerVertID,
        uint32_t*pdwVertBoundaryID);

    HRESULT FindSplitPath(
        const std::vector<uint32_t>& dijkstraPath,
        std::vector<uint32_t>& splitPath);

    HRESULT FindFacesAffectedBySplit(
        const std::vector<uint32_t>& splitPath,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    HRESULT CalSplitInfoOfFirstSplitVert(
        const std::vector<uint32_t>& splitPath,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    HRESULT CalSplitInfoOfMiddleSplitVerts(
        const std::vector<uint32_t>& splitPath,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    HRESULT CalSplitInfoOfLastSplitVert(
        const std::vector<uint32_t>& splitPath,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    HRESULT AddToChangedFaceList(
        ISOCHARTVERTEX* pCurrVertex,
        std::vector<uint32_t>& vertListOnOneSide,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    CIsochartMesh* SplitVertices(
        const std::vector<uint32_t>& splitPath,
        std::vector<uint32_t>& changeFaceList,
        std::vector<uint32_t>& corresVertList);

    HRESULT CutChartAlongPath(
        std::vector<uint32_t>& dijkstraPath);

    HRESULT CalculateDijkstraPathToVertex(
        uint32_t dwSourceVertID,
        uint32_t* pdwFarestPeerVertID = nullptr) const;

    HRESULT CalMinPathBetweenBoundaries(
        VERTEX_ARRAY& allBoundaryList,
        std::vector<uint32_t>& boundaryRecord,
        uint32_t* pdwVertBoundaryID,
        std::vector<uint32_t>& minDijkstraPath);

    HRESULT CalMinPathToOtherBoundary(
        VERTEX_ARRAY& allBoundaryList,
        uint32_t dwStartIdx,
        uint32_t dwEndIdx,
        uint32_t* pdwVertBoundaryID,
        uint32_t& dwPeerVertID,
        float& fDistance);

    HRESULT RetreiveVertDijkstraPathToSource(
        uint32_t dwVertexID,
        std::vector<uint32_t>& dijkstraPath);

    /////////////////////////////////////////////////////////////
    ////////////////Calculatel Vertex Importance ////////////////////
    /////////////////////////////////////////////////////////////

    HRESULT CalculateVertImportanceOrder();

    /////////////////////////////////////////////////////////////
    ///////////////Isomap Processing Methods/////////////////////
    /////////////////////////////////////////////////////////////

    HRESULT IsomapParameterlization(
        bool& bIsLikePlane,
        size_t& dwPrimaryEigenDimension,
        size_t& dwMaxEigenDimension,
        float** ppfVertGeodesicDistance,
        float** ppfVertCombineDistance,
        float** ppfVertMappingCoord);

    HRESULT CalculateVertMappingCoord(
        const float* pfVertGeodesicDistance,
        size_t dwLandmarkNumber,
        size_t dwPrimaryEigenDimension,
        float* pfVertMappingCoord);

    HRESULT CalculateLandmarkVertices(
        size_t dwMinLandmarkNumber,
        size_t& dwLardmarkNumber);

    void CalculateGeodesicMatrix(
        std::vector<uint32_t>& vertList,
        const float* pfVertGeodesicDistance,
        float* pfGeodesicMatrix) const;
    
    HRESULT InitOneToAllEngine() ;
    
    HRESULT CalculateGeodesicDistance(
        std::vector<uint32_t>& vertList,
        float* pfVertCombineDistance,
        float* pfVertGeodesicDistance) const;

    void UpdateAdjacentVertexGeodistance(
        ISOCHARTVERTEX* pCurrentVertex,
        ISOCHARTVERTEX* pAdjacentVertex,
        const ISOCHARTEDGE& edgeBetweenVertex,
        bool* pbVertProcessed,
        bool bIsSignalDistance) const;

    HRESULT CalculateGeodesicDistanceToVertex(
        uint32_t dwSourceVertID,
        bool bIsSignalDistance,
        uint32_t* pdwFarestPeerVertID = nullptr) const;

    HRESULT CalculateGeodesicDistanceToVertexKS98(
        uint32_t dwSourceVertID,
        bool bIsSignalDistance,
        uint32_t* pdwFarestPeerVertID = nullptr) const;
    
    HRESULT CalculateGeodesicDistanceToVertexNewGeoDist(
        uint32_t dwSourceVertID,
        uint32_t* pdwFarestPeerVertID = nullptr);

    void CalculateGeodesicDistanceABC(
        ISOCHARTVERTEX* pVertexA,
        ISOCHARTVERTEX* pVertexB,
        ISOCHARTVERTEX* pVertexC) const;

    void CombineGeodesicAndSignalDistance(
        float* pfSignalDistance,
        const float* pfGeodesicDistance,
        size_t dwVertLandNumber) const;
    /////////////////////////////////////////////////////////////
    ////////////////////Common Partition Methods///////////////////
    /////////////////////////////////////////////////////////////
    HRESULT GenerateAllSubCharts(
        const uint32_t* pdwFaceChartID,
        size_t dwMaxSubchartCount,
        bool& bAllManifold);

    HRESULT BuildSubChart(
        std::vector<uint32_t>& faceList,  // faces to be partitioned into the same chart
        bool& bManifold);

    HRESULT GetAllVerticesInSubChart(
        const std::vector<uint32_t>& faceList,
        VERTEX_ARRAY& subChartVertList);

    HRESULT SmoothPartitionResult(
        size_t dwMaxSubchartCount,
        uint32_t* pdwFaceChartID,
        bool& bIsOptimized);

    void SmoothOneFace(
        ISOCHARTFACE* pFace,
        uint32_t* pdwFaceChartID);

    HRESULT MakePartitionValid(
        size_t dwMaxSubchartCount,
        uint32_t* pdwFaceChartID,
        bool& bIsPartitionValid);

    HRESULT SatifyUserSpecifiedRule(
        uint32_t* pdwFaceChartID,
        bool& bHasFalseEdge,
        bool& bIsModifiedPartition,
        bool& bIsSatifiedUserRule);

    HRESULT SatifyManifoldRule(
        size_t dwMaxSubchartCount,
        uint32_t* pdwFaceChartID,
        bool& bIsModifiedPartition,		
        bool& bIsManifold);

    // Before we split chart by its faces' sub-chart id, we want to change some faces' sub-chart
    // id to make sure edges can not be splitted will be kept. However, this operation may cause
    // non-manifold topology. So we should have check and decide the target sub chart ID.
    // For example, If fac0 and face1 share curEdge, 
    // The candidate target sub-chart id will be the same with either sub-chart id of face0 or face1,
    // we should decide to choose which one. if neither of them can not gurantee manifold, return INVALID_INDEX
    // in dwTargetSubChartID.
    HRESULT AdjustToSameChartID(
        uint32_t* pdwFaceChartID,
        size_t dwCongFaceCount,
        uint32_t* pdwCongFaceID,
        bool &bModified);

    HRESULT FindCongenerFaces(
        std::vector<uint32_t>& congenerFaceCategories,
        std::vector<uint32_t>& congenerFaceCategoryLen,
        bool &bHasFalseEdge);

    HRESULT MakeValidationAroundVertex(
        ISOCHARTVERTEX* pVertex,
        uint32_t* pdwFaceChartID,
        bool bDoneFix,	// false: just indicate non-manifold but not modify the pdwFaceChartID to fix the non-manifold
        bool& bIsFixedSomeNonmanifold);

    bool IsAdjacentFacesInOneChart(
        ISOCHARTVERTEX* pVertex,
        uint32_t* pdwFaceChartID,
        uint32_t& dwChartID1,
        uint32_t& dwChartID2);

    HRESULT TryConnectAllFacesInSameChart(
        FACE_ARRAY& unconnectedFaceList,
        FACE_ARRAY& connectedFaceList);

    void AdjustChartIDToAvoidNonmanifold(
        uint32_t* pdwFaceChartID,
        FACE_ARRAY& unconnectedFaceList,
        FACE_ARRAY& connectedFaceList,
        uint32_t dwOriginalChartID,
        uint32_t dwCandidateChartID1,
        uint32_t dwCandidateChartID2);
    /////////////////////////////////////////////////////////////
    ///////////////Partition Special Shape Methods///////////////
    /////////////////////////////////////////////////////////////
    HRESULT ProcessPlaneShape(
        bool& bPlaneShape);

    HRESULT ProcessPlaneLikeShape(
        size_t dwCalculatedDimension,
        size_t dwPrimaryEigenDimension,
        bool& bPlaneLikeShape);
    
    HRESULT ProcessTrivialShape(
        size_t dwPrimaryEigenDimension,
        bool& bTrivialShape);

    HRESULT ProcessSpecialShape(
        size_t dwBoundaryNumber,
        const float* pfVertGeodesicDistance,
        const float* pfVertCombineDistance,
        const float* pfVertMappingCoord,
        size_t dwPrimaryEigenDimension,
        size_t dwMaxEigenDimension,
        bool& bSpecialShape);

    HRESULT CheckCylinderLonghornShape(
        size_t dwBoundaryNumber,
        bool& bIsCylinder,
        bool& bIsLonghorn,
        uint32_t& dwLonghornExtremeVexID) const;
    
    uint32_t CaculateExtremeVertex() const;

    HRESULT CaculateDistanceToExtremeVertex(
        uint32_t dwVertexID,
        float& fAverageDistance,
        float& fMinDistance,
        float& fMaxDistance) const;

    HRESULT PartitionCylindricalShape(
        const float* pfVertGeodesicDistance,
        const float* pfVertMapCoord,
        size_t dwMapDim,
        bool& bIsPartitionSucceed);

    HRESULT PartitionLonghornShape(
        const float* pfVertGeodesicDistance,	
        uint32_t dwLonghornExtremeVexID,
        bool& bIsPartitionSucceed);

    void GroupByFaceSign(
        const float* pfVertMapCoord,
        size_t dwMapDimension,
        size_t dwComputeDimension,
        size_t& dwPossitiveFaceCount,
        size_t& dwNegativeFaceCount,
        uint32_t* pdwFaceChartID);

    
    /////////////////////////////////////////////////////////////
    /////////////////Partition General Shape Methods/////////////
    /////////////////////////////////////////////////////////////
    HRESULT ProcessGeneralShape(
        size_t dwPrimaryEigenDimension,
        size_t dwBoundaryNumber,
        const float* pfVertGeodesicDistance,
        const float* pfVertCombineDistance,
        const float* pfVertMappingCoord);

    HRESULT CalculateRepresentiveVertices(
        std::vector<uint32_t>& representativeVertsIdx,
        size_t dwPrimaryEigenDimension,
        const float* pfVertMappingCoord);

    HRESULT GetMainRepresentive(
        std::vector<uint32_t>& representativeVertsIdx,
        size_t dwNumber,
        const float* pfVertGeodesicDistance);

    HRESULT RemoveCloseRepresentiveVertices(
        std::vector<uint32_t>& representativeVertsIdx,
        size_t dwPrimaryEigenDimension,
        const float* pfVertGeodesicDistance);

    void ClusterFacesByParameterDistance(
        uint32_t* pdwFaceChartID,
        const float* pfVertParitionDistance,
        std::vector<uint32_t>& representativeVertsIdx);

    HRESULT PartitionGeneralShape(
        const float* pfVertGeodesicDistance,
        const float* pfVertCombineDistance,
        std::vector<uint32_t>& representativeVertsIdx,
        const bool bOptSubBoundaryByAngle,
        bool& bIsPartitionSucceed);

    HRESULT PartitionEachFace();

    HRESULT ReserveFarestTwoLandmarks(
        const float* pfVertGeodesicDistance);
    /////////////////////////////////////////////////////////////
    /////////////////Bipartition chart functions/////////////////
    /////////////////////////////////////////////////////////////
    HRESULT BiPartitionParameterlizeShape(
        const float* pfVertCombineDistance,
        std::vector<uint32_t>& representativeVertsIdx);

    HRESULT InsureBiPartition(
        uint32_t* pdwFaceChartID);

    HRESULT FindWatershed(
        const uint32_t* pdwFaceChartID,
        EDGE_ARRAY& internalEdgeList,
        EDGE_ARRAY& marginalEdgeList);

    HRESULT GetMaxLengthCutPathsInWatershed(
        EDGE_ARRAY& internalEdgeList,
        EDGE_ARRAY& marginalEdgeList,
        EDGE_ARRAY& cutPath);

    HRESULT GrowPartitionFromCutPath(
        EDGE_ARRAY& cutPath,
        uint32_t* pdwFaceChartID);
    /////////////////////////////////////////////////////////////
    //////////////Optimizing  Stretch Methods////////////////////
    /////////////////////////////////////////////////////////////

    float CalChartL2GeoSquaredStretch();
    float CalCharLnSquaredStretch();
    float CalCharBaseL2SquaredStretch();
    HRESULT OptimizeChartL2Stretch(bool bOptimizeSignal);

    HRESULT OptimizeWholeChart(float fMaxAvgGeoL2Stretch);

    HRESULT InitOptimizeInfo(
        bool bOptLn,
        bool bOptSignal,
        bool bUseBoundingBox,
        bool bOptBoundaryVert,
        bool bOptInternalVert,
        float fBarToStopOpt,
        size_t dwOptAllVertsTimes,
        size_t dwRandOptOneVertTimes,
        bool bCalStretch,
        CHARTOPTIMIZEINFO& optimizeInfo,
        bool& bCanOptimize);

    void ReleaseOptimizeInfo(
        CHARTOPTIMIZEINFO& optimizeInfo);

    HRESULT OptimizeStretch( 
        CHARTOPTIMIZEINFO& optimizeInfo); 

    HRESULT OptimizeGeoLnInfiniteStretch(
        bool& bSucceed);

    float CalculateVertexStretch(
        bool bOptLn,
        const ISOCHARTVERTEX* pVertex,
        const float* pfFaceStretch) const;

    float CalFaceSquraedStretch(
        bool bOptLn,
        bool bOptSignal,
        const ISOCHARTFACE* pFace,
        const DirectX::XMFLOAT2& v0,
        const DirectX::XMFLOAT2& v1,
        const DirectX::XMFLOAT2& v2,
        const float fScale,
        float& f2D,
        float* pfGeoM = nullptr) const;

    float CalFaceSigL2SquraedStretch(
        const ISOCHARTFACE* pFace,
        const DirectX::XMFLOAT2& v0,
        const DirectX::XMFLOAT2& v1,
        const DirectX::XMFLOAT2& v2,
        float& f2D,
        float* pM = nullptr,
        float* pGeoM = nullptr) const;

    float CalFaceGeoL2SquraedStretch(
        const ISOCHARTFACE* pFace,
        const DirectX::XMFLOAT2& v0,
        const DirectX::XMFLOAT2& v1,
        const DirectX::XMFLOAT2& v2,
        float& f2D) const;

    float CalFaceGeoLNSquraedStretch(
        const ISOCHARTFACE* pFace,
        const DirectX::XMFLOAT2& v0,
        const DirectX::XMFLOAT2& v1,
        const DirectX::XMFLOAT2& v2,
        const float fScale,
        float& f2D) const;

    float CalculateAverageEdgeLength();
    bool CalculateChart2DTo3DScale(
        float& fScale, 
        float& fChart3DArea, 
        float& fChart2DArea);

    HRESULT OptimizeVertexWithInfiniteStretch(
        CHARTOPTIMIZEINFO& optimizeInfo);

    HRESULT OptimizeAllVertex(
        CHARTOPTIMIZEINFO& optimizeInfo);

    size_t CollectInfiniteVerticesInHeap(
        CHARTOPTIMIZEINFO& optimizeInfo);

    HRESULT OptimizeVerticesInHeap(
        CHARTOPTIMIZEINFO& optimizeInfo);

    HRESULT OptimizeVertexParamStretch(
        ISOCHARTVERTEX* pOptimizeVertex,
        CHARTOPTIMIZEINFO& optimizeInfo,
        bool& bIsUpdated);

    void PrepareBoundaryVertOpt(
        CHARTOPTIMIZEINFO& optimizeInfo,
        VERTOPTIMIZEINFO& vertInfo);

    void PrepareInternalVertOpt(
        CHARTOPTIMIZEINFO& optimizeInfo,
        VERTOPTIMIZEINFO& vertInfo);

    bool OptimizeVertexStretchAroundCenter(
        CHARTOPTIMIZEINFO& optimizeInfo,
        VERTOPTIMIZEINFO& vertInfo);

    float GetFaceAreaAroundVertex(
        const ISOCHARTVERTEX* pOptimizeVertex,
        DirectX::XMFLOAT2& newUV) const;

    float CalcuateAdjustedVertexStretch(
        bool bOptLn,
        const ISOCHARTVERTEX* pVertex,
        const float* pfAdjFaceStretch) const;

    // confine vertex in the chart bounding box
    void LimitVertexToBoundingBox(
        const DirectX::XMFLOAT2& end,
        const DirectX::XMFLOAT2& minBound,
        const DirectX::XMFLOAT2& maxBound,
        DirectX::XMFLOAT2& result);

    void UpdateOptimizeResult(
        CHARTOPTIMIZEINFO& optimizeInfo,
        ISOCHARTVERTEX* pOptimizeVertex,
        DirectX::XMFLOAT2& vertexNewCoordinate,
        float fNewVertexStretch,
        float* fAdjacentFaceNewStretch);

    void TryAdjustVertexParamStretch(
        ISOCHARTVERTEX* pOptimizeVertex,
        bool bOptLn,
        bool bOptSignal,
        float fStretchScale,
        DirectX::XMFLOAT2& newUV,
        float& fStretch,
        float* pfFaceStretch) const;

    void
    ParameterizeOneFace(
        bool bForSignal,
        ISOCHARTFACE* pFace);

    /////////////////////////////////////////////////////////////
    //////////////Optimizing boundary Methods////////////////////
    /////////////////////////////////////////////////////////////

    // Optimize boundary by graph-cut
    HRESULT OptimizeBoundaryByAngle(
        uint32_t* pdwFaceChartID,
        size_t dwMaxSubchartCount,
        bool& bIsOptimized);

    bool CalculateEdgeAngleDistance(
        float* pfEdgeAngleDistance,
        float& fAverageAngleDistance) const;

    HRESULT CalculateFuzzyRegion(
        bool* pbIsFuzzyFatherFace);

    HRESULT FindNewBoundaryVert(
        std::vector<uint32_t>& canidateVertexList,
        bool* pbIsFuzzyVert);

    HRESULT SpreadFuzzyVert(
        std::vector<uint32_t>& canidateVertexList,
        std::vector<uint32_t>& levelVertCountList,
        bool* pbIsFuzzyVert);

    HRESULT ApplyGraphCutByAngle(
        uint32_t* pdwFaceChartID,
        const bool* pbIsFuzzyFatherFace,
        float* pfEdgeAngleDistance,
        float fAverageAngleDistance);

    HRESULT DriveGraphCutByAngle(
        CGraphcut& graphCut,
        uint32_t* pdwFaceGraphNodeID,
        uint32_t* pdwFaceChartID,
        const bool* pbIsFuzzyFatherFace,
        float* pfEdgeAngleDistance,
        float fAverageAngleDistance);

    HRESULT OptimizeOneBoundaryByAngle(
        uint32_t dwChartIdx1,
        uint32_t dwChartIdx2,
        CGraphcut& graphCut,
        uint32_t* pdwFaceGraphNodeID,
        uint32_t* pdwFaceChartID,
        const bool* pbIsFuzzyFatherFace,
        float* pfEdgeAngleDistance,
        float fAverageAngleDistance);

    HRESULT OptimizeBoundaryByStretch(
        const float* pfOldVertGeodesicDistance,
        uint32_t* pdwFaceChartID,
        size_t dwMaxSubchartCount,
        bool& bIsOptimized);

    HRESULT CalSubchartsFuzzyRegion(
        std::vector<uint32_t>& allLandmark,
        uint32_t* pdwFaceChartID,
        bool* pbIsFuzzyFatherFace,
        uint32_t* pdwChartFuzzyLevel);

    HRESULT CalParamDistanceToAllLandmarks(
        const float* pfOldGeodesicDistance,
        float* pfNewGeodesicDistance,
        std::vector<uint32_t>& allLandmark);

    HRESULT CalSubchartsLandmarkUV(
        float* pfNewGeodesicDistance,
        std::vector<uint32_t>& allLandmark,
        bool& bIsDone);

    HRESULT CalculateLandmarkAndFuzzyRegion(
        bool* pbIsFuzzyFatherFace,
        uint32_t& dwFuzzyLevel);

    HRESULT DecreaseLocalLandmark();

    HRESULT ApplyGraphCutByStretch(
        size_t dwLandmarkNumber,
        uint32_t* pdwFaceChartID,
        const bool* pbIsFuzzyFatherFace,
        const uint32_t* pdwChartFuzzyLevel,
        size_t dwDimension,
        float* pfVertGeodesicDistance,
        float* pfEdgeAngleDistance,
        float fAverageAngleDistance);

    HRESULT OptimizeOneBoundaryByAngle(
        uint32_t dwChartIdx1,
        uint32_t dwChartIdx2,
        CGraphcut& graphCut,
        uint32_t* pdwFaceGraphNodeID,
        uint32_t* pdwFaceChartID,
        const bool* pbIsFuzzyFatherFace,
        size_t dwDimension,
        float* pfVertGeodesicDistance,
        float* pfEdgeAngleDistance,
        float fAverageAngleDistance,
        float* pfWorkSpace,
        float* pfFaceStretchDiff);

    float CalculateFaceGeodesicDistortion(
        ISOCHARTFACE* pFatherFace,
        CIsochartMesh* pChart,
        float* pfWorkSpace,
        size_t dwDimension,
        float* pfVertGeodesicDistance) const;

    void CalculateVertGeodesicCoord(
        float* pfCoord,
        ISOCHARTVERTEX* pFatherVertex,
        CIsochartMesh* pChart,
        float* pfWorkSpace,
        size_t dwDimension,
        float* pfVertGeodesicDistance) const;

    HRESULT CalculateLandmarkUV(
        float* pfVertGeodesicDistance,
        const size_t dwSelectPrimaryDimension,
        size_t& dwCalculatedPrimaryDimension);

    HRESULT CalculateSubChartAdjacentChart(
        uint32_t dwSelfChartID,
        uint32_t* pdwFaceChartID);

    HRESULT ApplyBoundaryOptResult(
        uint32_t* pdwFaceChartID,
        uint32_t* pdwFaceChartIDBackup,
        size_t dwMaxSubchartCount,
        bool& bIsOptimized);

    /////////////////////////////////////////////////////////////
    ///////////////Merge charts helper functions ////////////////
    /////////////////////////////////////////////////////////////
    static void SortChartsByFaceNumber(
        ISOCHARTMESH_ARRAY& children);

    static HRESULT CalAdjacentChartsForEachChart(
        ISOCHARTMESH_ARRAY& children,
        const uint32_t* pdwFaceAdjacentArray,
        size_t dwFaceNumber);

    static HRESULT PerformMerging(
        ISOCHARTMESH_ARRAY& children,
        size_t dwExpectChartCount,
        size_t dwFaceNumber,
        CCallbackSchemer& callbackSchemer);

    static void ReleaseAllNewCharts(
        ISOCHARTMESH_ARRAY& children);

    static HRESULT MergeAdjacentChart(
        ISOCHARTMESH_ARRAY& children,
        uint32_t dwMainChartID,
        size_t dwTotalFaceNumber,
        bool* pbMergeFlag,
        DirectX::XMFLOAT3* pChartNormal,
        bool& bMerged);

    static HRESULT TryMergeChart(
        ISOCHARTMESH_ARRAY& children,
        const CIsochartMesh* pChart1,
        const CIsochartMesh* pChart2,
        CIsochartMesh** ppFinialChart);

    static HRESULT CollectSharedVerts(
        const CIsochartMesh* pChart1,
        const CIsochartMesh* pChart2,
        std::vector<uint32_t>& vertMap,
        std::vector<bool>& vertMark,
        VERTEX_ARRAY& sharedVertexList,
        VERTEX_ARRAY& anotherSharedVertexList,
        bool& bCanMerge);

    static HRESULT CheckMergingToplogy(
        VERTEX_ARRAY& sharedVertexList,
        bool& bIsManifold);

    static CIsochartMesh* MergeTwoCharts(
        const CIsochartMesh* pChart1,
        const CIsochartMesh* pChart2,
        std::vector<uint32_t>& vertMap,
        std::vector<bool>& vertMark,
        size_t dwReduantVertNumber);

    void CalculateAveragNormal(
        DirectX::XMFLOAT3* pVector) const;

    HRESULT CalculateAdjacentChart(
        uint32_t dwCurrentChartID,
        uint32_t* pdwFaceChartRootID,
        const uint32_t* pRootFaceAdjacentArray);

    HRESULT TryParameterize(bool& bSucceed);

    HRESULT CalculateBoundaryNumber(
        size_t &dwBoundaryNumber) const;

    HRESULT CalculateIsoParameterization();

    /////////////////////////////////////////////////////////////
    ///////////////Packing charts helper functions //////////////
    /////////////////////////////////////////////////////////////
    static float GuranteeSmallestChartArea(
        ISOCHARTMESH_ARRAY& chartList);

    static HRESULT PreparePacking(
        ISOCHARTMESH_ARRAY& chartList,
        size_t dwWidth,
        size_t dwHeight,
        float gutter,
        ATLASINFO& atlasInfo);

    static HRESULT PackingOneChart(
        CIsochartMesh* pChart,
        ATLASINFO& atlasInfo,
        size_t dwIteration);

    static void NormalizeAtlas(
        ISOCHARTMESH_ARRAY& chartList,
        ATLASINFO& atlasInfo);

    static HRESULT CreateChartsPackingBuffer(
        ISOCHARTMESH_ARRAY& chartList);

    static void DestroyChartsPackingBuffer(
        ISOCHARTMESH_ARRAY& chartList);

    static void AlignChartsWithLongestAxis(
        ISOCHARTMESH_ARRAY& chartList);

    static float CalculateAllPackingChartsArea(
        ISOCHARTMESH_ARRAY& chartList);

    static void SortCharts(
        ISOCHARTMESH_ARRAY& chartList);

    static void PackingZeroAreaChart(CIsochartMesh* pChart);

    HRESULT CreatePackingInfoBuffer();

    void DestroyPakingInfoBuffer();

    void AlignUVWithLongestAxis() const;
    void ScaleTo3DArea();

    HRESULT CalculateChartBordersOfAllDirection(
        ATLASINFO& atlasInfo);

    HRESULT CalculateChartBorders(
        bool bHorizontal,
        VERTEX_ARRAY& lowerBorder,
        VERTEX_ARRAY& higherBorder,
        ISOCHARTVERTEX* pStartVertex,
        ISOCHARTVERTEX* pEndVertex,
        VERTEX_ARRAY& workBorder1,
        VERTEX_ARRAY& workBorder2,
        bool& bCanDecide);

    HRESULT
    ScanAlongBoundayEdges(
        ISOCHARTVERTEX* pStartVertex,
        ISOCHARTVERTEX* pEndVertex,
        ISOCHARTEDGE* pStartEdge,
        VERTEX_ARRAY& scanVertexList);

    void RotateChartAroundCenter(
        size_t dwRotationId,
        bool bOnlyRotateBoundaries,
        ISOCHARTVERTEX** ppLeftMostVertex = nullptr,
        ISOCHARTVERTEX** ppRightMostVertex = nullptr,
        ISOCHARTVERTEX** ppTopMostVertex = nullptr,
        ISOCHARTVERTEX** ppBottomMostVertex = nullptr);

    void RotateBordersAroundCenter(
        size_t dwRotationId);

    static void
    OptimizeAtlasSignalStretch(
        ISOCHARTMESH_ARRAY& chartList);

    void ScaleChart(float fScale);

    /////////////////////////////////////////////////////////////
    ///////////////////////Clean Mthods//////////////////////////
    /////////////////////////////////////////////////////////////
    void DeleteChildren();

    /////////////////////////////////////////////////////////////
    //////////////////LSCM Parameterization////////////////////////
    /////////////////////////////////////////////////////////////
    HRESULT LSCMParameterization(
        bool& bIsOverLap);

    HRESULT FindTwoFarestBoundaryVertices(
        uint32_t& dwVertId1,
        uint32_t& dwVertId2);

    HRESULT AddFaceWeight(
        uint32_t dwFaceID,
        CSparseMatrix<double>& A,
        CSparseMatrix<double>& M,
        uint32_t dwBaseVertId1,
        uint32_t dwBaseVertId2);

    HRESULT InitializeLSCMEquation(
        CSparseMatrix<double>& A,
        CVector<double>& B,
        CVector<double>& U,		
        uint32_t dwBaseVertId1,
        uint32_t dwBaseVertId2);

    HRESULT EstimateSolution(
        CVector<double>& V);
    HRESULT AssignLSCMResult(
        CVector<double>& U,
        CVector<double>& X,
        uint32_t dwBaseVertId1,
        uint32_t dwBaseVertId2);

    HRESULT CheckLinearEquationParamResult(
        bool& bIsOverLap);
    /////////////////////////////////////////////////////////////
    //////////////////Barycentric Parameterization///////////////////
    /////////////////////////////////////////////////////////////
    HRESULT BarycentricParameterization(
        bool& bIsOverLap);

    HRESULT GenerateVertexMap(
        std::vector<uint32_t>& vertMap,
        size_t& dwBoundaryCount,
        size_t& dwInternalCount);

    HRESULT GenerateBoundaryCoord(
        std::vector<double>& boundTable,
        size_t dwBoundaryCount,
        const std::vector<uint32_t>& vertMap);

    HRESULT InitializeBarycentricEquation(
        CSparseMatrix<double>& A,
        CVector<double>& BU,
        CVector<double>& BV,
        const std::vector<double>& boundTable,
        const std::vector<uint32_t>& vertMap);

    HRESULT AssignBarycentricResult(
        CVector<double>& U,
        CVector<double>& V,
        const std::vector<double>& boundTable,
        const std::vector<uint32_t>& vertMap);

private:

    CCallbackSchemer& m_callbackSchemer;

    const CIsochartEngine &m_IsochartEngine ;

    // Mesh information
    const CBaseMeshInfo& m_baseInfo;

    size_t m_dwVertNumber;
    ISOCHARTVERTEX* m_pVerts;

    size_t m_dwFaceNumber;
    ISOCHARTFACE* m_pFaces;

    size_t m_dwEdgeNumber;
    std::vector<ISOCHARTEDGE> m_edges;

    CIsochartMesh* m_pFather;// Indicating where the chart derives from

    float m_fBoxDiagLen;

    std::vector<uint32_t> m_adjacentChart;
    ISOCHARTMESH_ARRAY m_children; 

    CIsoMap m_isoMap;
    std::vector<uint32_t> m_landmarkVerts;

    //m_fParamStretchL2 and m_fParamStretchLn bound the distortion of
    //parameterization.See more detail in :
    //Kun Zhou, John Synder, Baining Guo, Heung-Yeung Shum:
    //Iso-charts: Stretch-driven Mesh Parameterization using Spectral Analysis
    //Eurographics Symposium on Geometry Processing (2004)
    float m_fParamStretchL2;
    float m_fParamStretchLn;
    float m_fBaseL2Stretch;
    float m_fGeoL2Stretch;

    // Indicating vertex importance order has been calculated.
    bool m_bVertImportanceDone;
    bool m_bIsSubChart;

    // This attribute is used by isochart engine
    bool m_bIsInitChart;

    float m_fChart2DArea;
    float m_fChart3DArea;
    PACKINGINFO* m_pPackingInfo; //Structure used in packing.

    bool m_bIsParameterized; // Indicating mesh has been parameterized

    bool m_bOptimizedL2Stretch;	
    bool m_bOrderedLandmark;

    bool m_bNeedToClean;

    #if _USE_EXACT_ALGORITHM
    GeodesicDist::CExactOneToAll m_ExactOneToAllEngine ;
    #else
    GeodesicDist::CApproximateOneToAll m_ApproximateOneToAllEngine ;
    #endif
};

}

#include "meshcommon.inl"
