# UVAtlas Isochart Atlas Library
#
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
#
# http://go.microsoft.com/fwlink/?LinkID=512686

cmake_minimum_required (VERSION 3.11)

project (UVAtlas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/CMake")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/CMake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/CMake")

add_library (${PROJECT_NAME} STATIC
    UVAtlas/maxheap.hpp
    UVAtlas/pch.h
    UVAtlas/inc/UVAtlas.h
    UVAtlas/geodesics/ApproximateOneToAll.cpp
    UVAtlas/geodesics/ApproximateOneToAll.h
    UVAtlas/geodesics/datatypes.h
    UVAtlas/geodesics/ExactOneToAll.cpp
    UVAtlas/geodesics/ExactOneToAll.h
    UVAtlas/geodesics/mathutils.cpp
    UVAtlas/geodesics/mathutils.h
    UVAtlas/geodesics/minheap.hpp
    UVAtlas/isochart/barycentricparam.cpp
    UVAtlas/isochart/basemeshinfo.cpp
    UVAtlas/isochart/basemeshinfo.h
    UVAtlas/isochart/callbackschemer.h
    UVAtlas/isochart/graphcut.cpp
    UVAtlas/isochart/graphcut.h
    UVAtlas/isochart/imtcomputation.cpp
    UVAtlas/isochart/isochart.cpp
    UVAtlas/isochart/isochart.h
    UVAtlas/isochart/isochartconfig.h
    UVAtlas/isochart/isochartengine.cpp
    UVAtlas/isochart/isochartengine.h
    UVAtlas/isochart/isochartmesh.cpp
    UVAtlas/isochart/isochartmesh.h
    UVAtlas/isochart/isochartutil.cpp
    UVAtlas/isochart/isochartutil.h
    UVAtlas/isochart/isomap.cpp
    UVAtlas/isochart/isomap.h
    UVAtlas/isochart/lscmparam.cpp
    UVAtlas/isochart/mergecharts.cpp
    UVAtlas/isochart/meshapplyisomap.cpp
    UVAtlas/isochart/meshcommon.inl
    UVAtlas/isochart/meshoptimizeboundaries.cpp
    UVAtlas/isochart/meshoptimizestretch.cpp
    UVAtlas/isochart/meshpartitionchart.cpp
    UVAtlas/isochart/packingcharts.cpp
    UVAtlas/isochart/progressivemesh.cpp
    UVAtlas/isochart/progressivemesh.h
    UVAtlas/isochart/sparsematrix.hpp
    UVAtlas/isochart/SymmetricMatrix.hpp
    UVAtlas/isochart/UVAtlas.cpp
    UVAtlas/isochart/UVAtlasRepacker.cpp
    UVAtlas/isochart/UVAtlasRepacker.h
    UVAtlas/isochart/vertiter.cpp
    UVAtlas/isochart/vertiter.h
    UVAtlas/isochart/Vis_Maxflow.cpp
    UVAtlas/isochart/Vis_Maxflow.h
)

source_group(inc REGULAR_EXPRESSION UVAtlas/inc/*.*)
source_group(geodesics REGULAR_EXPRESSION UVAtlas/geodesics/*.*)
source_group(isochart REGULAR_EXPRESSION UVAtlas/isochart/*.*)
source_group(isochart REGULAR_EXPRESSION UVAtlas/isochart/*.*)

target_include_directories(${PROJECT_NAME} PUBLIC UVAtlas/inc)
target_include_directories(${PROJECT_NAME} PRIVATE UVAtlas/ UVAtlas/geodesics UVAtlas/isochart)

# Use max Warning Level 
string(REPLACE "/W3" "/Wall" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE "/W3" "/Wall" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
string(REPLACE "/W3" "/Wall" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})

add_executable(uvatlastool
    UVAtlasTool/uvatlas.cpp
    UVAtlasTool/Mesh.cpp
    UVAtlasTool/Mesh.h
    UVAtlasTool/MeshOBJ.cpp
    UVAtlasTool/SDKMesh.h)
target_link_libraries(uvatlastool ${PROJECT_NAME})
target_include_directories(uvatlastool PRIVATE ../DirectXMesh/DirectXMesh ../DirectXMesh/Utilities ../DirectXTex/DirectXTex)

target_compile_options(${PROJECT_NAME} PRIVATE /fp:fast)
target_compile_options(uvatlastool PRIVATE /fp:fast)

if (${CMAKE_SIZEOF_VOID_P} EQUAL "4")
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:SSE2)
    target_compile_options(uvatlastool PRIVATE /arch:SSE2)
endif()

if ( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
    set(WarningsLib "-Wpedantic" "-Wextra")
    target_compile_options(${PROJECT_NAME} PRIVATE ${WarningsLib})

    set(WarningsEXE ${WarningsLib} "-Wno-c++98-compat" "-Wno-c++98-compat-pedantic" "-Wno-exit-time-destructors" "-Wno-switch" "-Wno-switch-enum" "-Wno-language-extension-token" "-Wno-missing-prototypes")
    target_compile_options(uvatlastool PRIVATE ${WarningsEXE})
endif()
if ( CMAKE_CXX_COMPILER_ID MATCHES "MSVC" )
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /JMC- /Zc:__cplusplus)
    target_compile_options(uvatlastool PRIVATE /permissive- /Zc:__cplusplus)

    set(WarningsEXE "/wd4365" "/wd4710" "/wd4820" "/wd5039" "/wd5045")
    target_compile_options(uvatlastool PRIVATE ${WarningsEXE})
endif()

# We use Windows 7 here
add_compile_definitions(_UNICODE UNICODE _WIN32_WINNT=0x0601)
