# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.11)

set(UVATLAS_VERSION 1.8.2)

project (UVAtlas
  VERSION ${UVATLAS_VERSION}
  DESCRIPTION "UVAtlas Isochart Atlas Library"
  HOMEPAGE_URL "https://go.microsoft.com/fwlink/?LinkID=512686"
  LANGUAGES CXX)

# To build this tool, you need the DirectXTex (http://go.microsoft.com/fwlink/?LinkId=248926) and
# DirectXMesh (http://go.microsoft.com/fwlink/?LinkID=324981) cmake packages available.
#
# Use vcpkg and set the required CMAKE_TOOLCHAIN_FILE path to vcpkg.cmake
#
# -or-
#
# cmake install the individual packages and add the per-configuration variables for directxmesh_DIR and directxtex_DIR
#
# Note to support Windows 7, turn off BUILD_DX12 build options for both libraries.
option(BUILD_TOOLS "Build UVAtlasTool" OFF)

# Enable the use of OpenMP
option(UVATLAS_USE_OPENMP "Build with OpenMP support" ON)

# Enable use of the Eigen and BLAS libraries (https://eigen.tuxfamily.org/)
option(ENABLE_USE_EIGEN "Use the Eigen & BLAS libraries" OFF)

option(ENABLE_CODE_ANALYSIS "Use Static Code Analysis on build" OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")

#--- Library
set(LIBRARY_HEADERS
    UVAtlas/inc/UVAtlas.h)

set(LIBRARY_SOURCES
    UVAtlas/maxheap.hpp
    UVAtlas/pch.h
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

add_library (${PROJECT_NAME} STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

source_group(inc REGULAR_EXPRESSION UVAtlas/inc/*.*)
source_group(geodesics REGULAR_EXPRESSION UVAtlas/geodesics/*.*)
source_group(isochart REGULAR_EXPRESSION UVAtlas/isochart/*.*)
source_group(isochart REGULAR_EXPRESSION UVAtlas/isochart/*.*)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/UVAtlas/inc>
  $<INSTALL_INTERFACE:include>)

target_include_directories(${PROJECT_NAME} PRIVATE UVAtlas UVAtlas/geodesics UVAtlas/isochart)

if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(${PROJECT_NAME} PRIVATE UVAtlas/pch.h)
endif()

if(MSVC)
    # Use max Warning Level
    string(REPLACE "/W3 " "/Wall " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(REPLACE "/W3 " "/Wall " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
    string(REPLACE "/W3 " "/Wall " CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})

    # Not using typeid or dynamic_cast, so disable RTTI to save binary size
    string(REPLACE "/GR " "/GR- " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(REPLACE "/GR " "/GR- " CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
    string(REPLACE "/GR " "/GR- " CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
endif()

if ((NOT WIN32) OR VCPKG_TOOLCHAIN)
    message("INFO: Using VCPKG for DirectX-Headers and DirectXMath.")
    find_package(directx-headers CONFIG REQUIRED)
    find_package(directxmath CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Microsoft::DirectX-Headers Microsoft::DirectXMath)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USING_DIRECTX_HEADERS)
endif()

if (ENABLE_USE_EIGEN)
    message("INFO: Using Eigen3 & Spectra for CSymmetricMatrix::GetEigen.")
    find_package(Eigen3 REQUIRED)
    find_package(spectra REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen Spectra::Spectra)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UVATLAS_USE_EIGEN)
endif()

#--- Package
include(CMakePackageConfigHelpers)

string(TOLOWER ${PROJECT_NAME} PACKAGE_NAME)

write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/cmake/${PACKAGE_NAME}-config-version.cmake
  VERSION ${UVATLAS_VERSION}
  COMPATIBILITY AnyNewerVersion)

install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}-targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/.nuget/${PROJECT_NAME}-config.cmake.in
  ${PROJECT_BINARY_DIR}/cmake/${PACKAGE_NAME}-config.cmake
  INSTALL_DESTINATION cmake/})

install(EXPORT ${PROJECT_NAME}-targets
  FILE ${PROJECT_NAME}-targets.cmake
  NAMESPACE Microsoft::
  DESTINATION cmake/)

install(FILES ${LIBRARY_HEADERS}
  DESTINATION include)

install(
  FILES
    ${PROJECT_BINARY_DIR}/cmake/${PACKAGE_NAME}-config.cmake
    ${PROJECT_BINARY_DIR}/cmake/${PACKAGE_NAME}-config-version.cmake
  DESTINATION cmake/)

#--- Command-line tool
if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
    find_package(directxmesh CONFIG REQUIRED COMPONENTS library utils)
    find_package(directxtex CONFIG REQUIRED)

    add_executable(uvatlastool
        UVAtlasTool/UVAtlas.cpp
        UVAtlasTool/Mesh.cpp
        UVAtlasTool/Mesh.h
        UVAtlasTool/MeshOBJ.cpp
        UVAtlasTool/SDKMesh.h)

    source_group(UVAtlasTool REGULAR_EXPRESSION UVAtlasTool/*.*)

    target_link_libraries(uvatlastool ${PROJECT_NAME} Microsoft::DirectXMesh Microsoft::DirectXTex Microsoft::DirectXMesh::Utilities ole32.lib version.lib)

    if (VCPKG_TOOLCHAIN)
      target_link_libraries(uvatlastool Microsoft::DirectXMath)
    endif()
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /fp:fast)
    if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
        target_compile_options(uvatlastool PRIVATE /fp:fast)
    endif()

    if (${CMAKE_SIZEOF_VOID_P} EQUAL "4" AND (NOT ${CMAKE_VS_PLATFORM_NAME} MATCHES "arm"))
        target_compile_options(${PROJECT_NAME} PRIVATE /arch:SSE2)
        if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
            target_compile_options(uvatlastool PRIVATE /arch:SSE2)
        endif()
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(WarningsLib "-Wpedantic" "-Wextra")
    target_compile_options(${PROJECT_NAME} PRIVATE ${WarningsLib})

    if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
        set(WarningsEXE ${WarningsLib} "-Wno-c++98-compat" "-Wno-c++98-compat-pedantic" "-Wno-switch" "-Wno-switch-enum" "-Wno-exit-time-destructors" "-Wno-switch" "-Wno-switch-enum" "-Wno-language-extension-token" "-Wno-missing-prototypes")
        target_compile_options(uvatlastool PRIVATE ${WarningsEXE})
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /JMC- /Zc:__cplusplus)

    if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
        target_compile_options(uvatlastool PRIVATE /permissive- /Zc:__cplusplus)
    endif()

   if(ENABLE_CODE_ANALYSIS)
      target_compile_options(${PROJECT_NAME} PRIVATE /analyze)
      if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
         target_compile_options(uvatlastool PRIVATE /analyze)
      endif()
   endif()

    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.26)
        target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /wd5105)

        if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
            target_compile_options(uvatlastool PRIVATE /Zc:preprocessor /wd5105)
        endif()
    endif()

    if(UVATLAS_USE_OPENMP)
        target_compile_options(${PROJECT_NAME} PRIVATE /openmp /Zc:twoPhase-)

        if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
            target_compile_options(uvatlastool PRIVATE /openmp /Zc:twoPhase-)
        endif()
    endif()

    if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
        set(WarningsEXE "/wd4365" "/wd4514" "/wd4625" "/wd4626" "/wd4627" "/wd4668" "/wd4710" "/wd4751" "/wd4820" "/wd5026" "/wd5027" "/wd5039" "/wd5045" "/wd4061" "/wd4062" "/wd5219")
        target_compile_options(uvatlastool PRIVATE ${WarningsEXE})
    endif()
endif()

if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE)

    if(WINDOWS_STORE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00)
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0601)
    endif()

    if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
        target_compile_definitions(uvatlastool PRIVATE _UNICODE UNICODE _WIN32_WINNT=0x0601)
    endif()
endif()

if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT uvatlastool)
else()
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()
