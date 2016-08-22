//-------------------------------------------------------------------------------------
// UVAtlas - isochartconfig.h
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

namespace Isochart
{
////////////////////////////////////////////////////////////////////
//////////////////Parameterization Method /////////////////////////////
////////////////////////////////////////////////////////////////////

#define PARAM_TURN_ON_LSCM 0 // Turn on LSCM during parition
#define MERGE_TURN_ON_LSCM 0// Turn on LSCM during merge

//Perform LSCM only when the input stretch is larger than the criteria
const float SMALL_STRETCH_TO_TURNON_LSCM = 0.95f; 

#define PARAM_TURN_ON_BARYCENTRIC 1	// Turn on barycenteric method during partition
#define MERGE_TURN_ON_BARYCENTRIC 1	// Turn on barycenteric method during merge

//Perform Barycentric method only when the input stretch is larger than the criteria
const float SMALL_STRETCH_TO_TURNON_BARY = 0.95f;

////////////////////////////////////////////////////////////////////
//////////////////ISOMAP Configuration////////////////////////////////
////////////////////////////////////////////////////////////////////

// The minimal number of vertices in simplified mesh.

// Here is a trade-off:
// More vertices left caused more accurate parameterization, but 
// also more time cost.
// 80 is based on examination of Kun
const size_t MIN_PM_VERT_NUMBER = 85;

// A mesh must use at least MIN_LANDMARK_NUMBER vertices to apply
// isomap algorithm.
const size_t MIN_LANDMARK_NUMBER = 25;

// 1 means:
// Using the combination of signal and geodesic distance to apply isomap.
// 0 means:
// Using the geodesic distance to apply isomap.
#define USING_COMBINED_DISTANCE_TO_PARAMETERIZE 0

////////////////////////////////////////////////////////////////////
//////////////////IMT Configuration///////////////////////////////////
////////////////////////////////////////////////////////////////////

// 1 means:
// IMT stands for piecewise const signal reconstruction. ( 3 different coefficient )
// 1 means:
// IMT stands for piecewise linear signal reconstruction. ( 6 different coefficient )

// Note: 
// Now, we ony support piecewise constant imt now, so this marco must be 1
// we may support Piewise linear imt in future.
#define PIECEWISE_CONSTANT_IMT 1 

#if PIECEWISE_CONSTANT_IMT
const size_t IMT_DIM = 3;  // Piecewise constant imt
#else
const size_t IMT_DIM = 6; // Piecewise linear imt
#endif

////////////////////////////////////////////////////////////////////
////////////////Optimize Stretch Configuration///////////////////////////
////////////////////////////////////////////////////////////////////

// According to the experiemnt, parameterization with overlapping is
// not easy to be corrected during  L^2 geometic optimization. 

// 1 means:
// Overlapping checking will perform before
// L^2 geometic optimize. 

// 0 means:
// Don't check overlapping
#define CHECK_OVER_LAPPING_BEFORE_OPT_INFINIT 1

const float INFINITE_STRETCH = FLT_MAX;

// 1 means :
// Using dihedral angel as criterion to optimize boundary when paritition.
// 0 means:
// Using the combination of dihedral angle and stretch difference.
#define OPT_3D_BIPARTITION_BOUNDARY_BY_ANGLE 1

// When optimize chart by signal, just amplify the geometric stretch criteria
// to give more freedom when moving vertices
const float POW_OF_IMT_GEO_L2_STRETCH = 0.2f;

// 1 means :
// If current chart has been optimized, don't optimize it any more until it is paritioned again.
// 0 means:
// Always optimize current chart
#define OPT_CHART_L2_STRETCH_ONCE 1

// This parameter is used to control the signal stretch optimization. If we don't want to get 
// too large triangles, just set this parameter to a value in the range of 0 to 1.
const float FACE_MIN_L2_STRETCH = 0.0f;

//Using the optimal scaling method given by John Synder can generate too large or too small
//charts, the too large charts will waste large space, too small charts can easily give trouble
// to mip map.
const float OPTIMAL_SCALE_FACTOR = 2.5f;

// Optimize L^n Stretch
const size_t INFINITE_VERTICES_OPTIMIZE_COUNT = 12; //Times to optimize all infinite vertices
const size_t RAND_OPTIMIZE_INFINIT_COUNT = 12; // Times to randomly optimize one vertex 

// Optimize signal L^2 Stretch
const size_t L2_PREV_OPTIMIZESIG_COUNT = 6;	//Times to optimize all vertices before optimize whole charts.
const size_t L2_POST_OPTIMIZESIG_COUNT = 4;	//Times to optimize all vertices after optimize who charts
const size_t RAND_OPTIMIZE_L2_COUNT = 9;

// Optimize geometric L^2 stretch. For the boundary vertices, using L^n stretch optimization
// to replace L^2 stretch
const size_t L2_OPTIMIZE_COUNT = 7;
const size_t LN_OPTIMIZE_COUNT = 2;
const size_t RAND_OPTIMIZE_LN_COUNT = 9;
const float STRETCH_TO_STOP_LN_OPTIMIZE = 2.0f;

// When performing affine transformation to a face or a chart to decrease their signal stretch,
// using these paramters to avoid to much geometric distoration.
const float FACE_MAX_SCALE_FACTOR = 2.0f;
const float CHART_MAX_SCALE_FACTOR = 2.0f;

// How many overturn vertices are acceptable. Now we just don't want any overturn faces. After
// parameterization
const float Overturn_TOLERANCE = 0.0f;

// Chart must large than some criterion, this parameter used to avoid too small charts.
const float SMALLEST_CHART_PIXEL_AREA = 100.0f/(512*512);
////////////////////////////////////////////////////////////////////
////////////////Merage Charts Configuration////////////////////////////
////////////////////////////////////////////////////////////////////

const float MAX_MERGE_RATIO = 0.7f;
const float MAX_MERGE_FACE_NUMBER = 700;

////////////////////////////////////////////////////////////////////
////////////////Packing Charts Configuration////////////////////////////
////////////////////////////////////////////////////////////////////

// Becuase it is impossible to know the space ratio in finial uvatlas, we just use this
// parameter as an estimate value. Then we can compute the length of pixel in uvatlas.

// Larger value will generate larger pixel size. After experiment, 0.5 is a good estimation.
const float STANDARD_SPACE_RATE = 0.5f;

}
