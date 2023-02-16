# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.20)

set(UVATLAS_VERSION 1.8.4)

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

# https://devblogs.microsoft.com/cppblog/spectre-mitigations-in-msvc/
option(ENABLE_SPECTRE_MITIGATION "Build using /Qspectre for MSVC" OFF)

option(DISABLE_MSVC_ITERATOR_DEBUGGING "Disable iterator debugging in Debug configurations with the MSVC CRT" OFF)

option(ENABLE_CODE_ANALYSIS "Use Static Code Analysis on build" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/CMake")

if(DEFINED VCPKG_TARGET_ARCHITECTURE)
    set(DIRECTX_ARCH ${VCPKG_TARGET_ARCHITECTURE})
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^[Ww][Ii][Nn]32$")
    set(DIRECTX_ARCH x86)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^[Xx]64$")
    set(DIRECTX_ARCH x64)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]$")
    set(DIRECTX_ARCH arm)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$")
    set(DIRECTX_ARCH arm64)
endif()

if(DEFINED XBOX_CONSOLE_TARGET)
  set(BUILD_TOOLS OFF)
endif()

include(GNUInstallDirs)

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
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

target_include_directories(${PROJECT_NAME} PRIVATE UVAtlas UVAtlas/geodesics UVAtlas/isochart)

if(NOT MINGW)
    target_precompile_headers(${PROJECT_NAME} PRIVATE UVAtlas/pch.h)
endif()

if(MINGW OR (NOT WIN32))
    find_package(directxmath CONFIG REQUIRED)
    find_package(directx-headers CONFIG REQUIRED)
else()
    find_package(directxmath CONFIG QUIET)
    find_package(directx-headers CONFIG QUIET)
endif()

if(directxmath_FOUND)
    message(STATUS "Using DirectXMath package")
    target_link_libraries(${PROJECT_NAME} PUBLIC Microsoft::DirectXMath)
endif()

if(directx-headers_FOUND)
    message(STATUS "Using DirectX-Headers package")
    target_link_libraries(${PROJECT_NAME} PUBLIC Microsoft::DirectX-Headers)
    target_compile_definitions(${PROJECT_NAME} PUBLIC USING_DIRECTX_HEADERS)
endif()

if(ENABLE_USE_EIGEN)
    message(STATUS "Using Eigen3 & Spectra for CSymmetricMatrix::GetEigen.")
    find_package(Eigen3 REQUIRED)
    find_package(spectra REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen Spectra::Spectra)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UVATLAS_USE_EIGEN)
endif()

include(CheckIncludeFileCXX)

if(DEFINED XBOX_CONSOLE_TARGET)
    message(STATUS "Building for Xbox Console Target: ${XBOX_CONSOLE_TARGET}")
    set(CMAKE_REQUIRED_QUIET ON)
    CHECK_INCLUDE_FILE_CXX(gxdk.h GXDK_HEADER)
    if(NOT GXDK_HEADER)
        message(FATAL_ERROR "Microsoft GDK with Xbox Extensions required to build for Xbox. See https://aka.ms/gdkx")
    endif()
    target_compile_definitions(${PROJECT_NAME} PUBLIC WINAPI_FAMILY=WINAPI_FAMILY_GAMES)
    if(XBOX_CONSOLE_TARGET STREQUAL "scarlett")
        CHECK_INCLUDE_FILE_CXX(d3d12_xs.h D3D12XS_HEADER)
        if(NOT D3D12XS_HEADER)
            message(FATAL_ERROR "Microsoft GDK with Xbox Extensions environment needs to be set for Xbox Series X|S.")
        endif()
        target_compile_definitions(${PROJECT_NAME} PUBLIC _GAMING_XBOX _GAMING_XBOX_SCARLETT)
    elseif(XBOX_CONSOLE_TARGET STREQUAL "xboxone")
        CHECK_INCLUDE_FILE_CXX(d3d12_x.h D3D12X_HEADER)
        if(NOT D3D12X_HEADER)
            message(FATAL_ERROR "Microsoft GDK with Xbox Extensions environment needs to be set for Xbox One.")
        endif()
        target_compile_definitions(${PROJECT_NAME} PUBLIC _GAMING_XBOX _GAMING_XBOX_XBOXONE)
    endif()
endif()

#--- Package
include(CMakePackageConfigHelpers)

string(TOLOWER ${PROJECT_NAME} PACKAGE_NAME)

write_basic_package_version_file(
  ${PACKAGE_NAME}-config-version.cmake
  VERSION ${UVATLAS_VERSION}
  COMPATIBILITY AnyNewerVersion)

install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/build/${PROJECT_NAME}-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PACKAGE_NAME})

install(EXPORT ${PROJECT_NAME}-targets
  FILE ${PROJECT_NAME}-targets.cmake
  NAMESPACE Microsoft::
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PACKAGE_NAME})

install(FILES ${LIBRARY_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PACKAGE_NAME})

#--- Command-line tool
if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
    set(TOOL_EXES uvatlastool)

    message(STATUS "Using DirectXMesh and DirectXTex (required for uvatlastool).")

    find_package(directxmesh CONFIG REQUIRED COMPONENTS library utils)
    find_package(directxtex CONFIG REQUIRED)

    add_executable(uvatlastool
        UVAtlasTool/UVAtlas.cpp
        UVAtlasTool/Mesh.cpp
        UVAtlasTool/Mesh.h
        UVAtlasTool/MeshOBJ.cpp
        UVAtlasTool/SDKMesh.h)
    target_link_libraries(uvatlastool
        ${PROJECT_NAME}
        ole32.lib version.lib
        Microsoft::DirectXMesh
        Microsoft::DirectXTex
        Microsoft::DirectXMesh::Utilities)
    source_group(uvatlastool REGULAR_EXPRESSION UVAtlasTool/*.*)

    if(directxmath_FOUND)
      target_link_libraries(uvatlastool Microsoft::DirectXMath)
    endif()
endif()

if(MSVC)
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE /Wall /GR- /fp:fast "$<$<NOT:$<CONFIG:DEBUG>>:/guard:cf>")
      target_link_options(${t} PRIVATE /DYNAMICBASE /NXCOMPAT)
    endforeach()

    if((${CMAKE_SIZEOF_VOID_P} EQUAL 4) AND (NOT ${DIRECTX_ARCH} MATCHES "^arm"))
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_link_options(${t} PRIVATE /SAFESEH)
      endforeach()
    endif()

    if(ENABLE_SPECTRE_MITIGATION
       AND (MSVC_VERSION GREATER_EQUAL 1913)
       AND (NOT WINDOWS_STORE)
       AND (NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang")))
      message(STATUS "Building Spectre-mitigated libraries.")
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_options(${t} PRIVATE "/Qspectre")
      endforeach()
    endif()

    if((MSVC_VERSION GREATER_EQUAL 1928)
       AND (CMAKE_SIZEOF_VOID_P EQUAL 8)
       AND ((NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang")) OR (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)))
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_options(${t} PRIVATE "$<$<NOT:$<CONFIG:DEBUG>>:/guard:ehcont>")
        target_link_options(${t} PRIVATE "$<$<NOT:$<CONFIG:DEBUG>>:/guard:ehcont>")
      endforeach()
    endif()
else()
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_definitions(${t} PRIVATE $<IF:$<CONFIG:DEBUG>,_DEBUG,NDEBUG>)
    endforeach()
endif()

if(XBOX_CONSOLE_TARGET STREQUAL "scarlett")
    target_compile_options(${PROJECT_NAME} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/favor:AMD64 /arch:AVX2,-march=znver2>)
elseif(XBOX_CONSOLE_TARGET STREQUAL "xboxone")
    target_compile_options(${PROJECT_NAME} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/favor:AMD64 /arch:AVX,-march=btver2)
elseif(NOT (${DIRECTX_ARCH} MATCHES "^arm"))
    if(${CMAKE_SIZEOF_VOID_P} EQUAL "4")
        set(ARCH_SSE2 $<$<CXX_COMPILER_ID:MSVC>:/arch:SSE2> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-msse2>)
    else()
        set(ARCH_SSE2 $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-msse2>)
    endif()

    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE ${ARCH_SSE2})
    endforeach()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(WarningsLib -Wall -Wpedantic -Wextra)
    target_compile_options(${PROJECT_NAME} PRIVATE ${WarningsLib})

    set(WarningsEXE ${WarningsLib} "-Wno-c++98-compat" "-Wno-c++98-compat-pedantic" "-Wno-switch" "-Wno-switch-enum" "-Wno-exit-time-destructors" "-Wno-switch" "-Wno-switch-enum" "-Wno-language-extension-token" "-Wno-missing-prototypes")
    foreach(t IN LISTS TOOL_EXES)
      target_compile_options(${t} PRIVATE ${WarningsEXE})
    endforeach()
elseif(MINGW)
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE "-Wno-ignored-attributes")
      target_link_options(${t} PRIVATE -municode)
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE /sdl /permissive- /JMC- /Zc:__cplusplus /Zc:inline)
    endforeach()

    if(ENABLE_CODE_ANALYSIS)
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_options(${t} PRIVATE /analyze)
      endforeach()
    endif()

    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
      message(STATUS "Building using Whole Program Optimization")
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_options(${t} PRIVATE /Gy /Gw)
      endforeach()
    endif()

    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.24)
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE /ZH:SHA_256)
        endforeach()
    endif()

    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.26)
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE /Zc:preprocessor /wd5105)
        endforeach()
    endif()

    if((CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.27) AND (NOT (${DIRECTX_ARCH} MATCHES "^arm")))
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_link_options(${t} PRIVATE /CETCOMPAT)
        endforeach()
    endif()

    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.28)
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE /Zc:lambda)
        endforeach()
    endif()

    if(UVATLAS_USE_OPENMP)
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE /openmp /Zc:twoPhase-)
        endforeach()
    endif()

    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.35)
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE /Zc:templateScope /Zc:checkGwOdr)
        endforeach()
    endif()

    set(WarningsEXE "/wd4365" "/wd4514" "/wd4625" "/wd4626" "/wd4627" "/wd4668" "/wd4710" "/wd4751" "/wd4820" "/wd5026" "/wd5027" "/wd5039" "/wd5045" "/wd4061" "/wd4062" "/wd5219")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.34)
        list(APPEND WarningsEXE "/wd5262" "/wd5264")
        target_compile_options(${PROJECT_NAME} PRIVATE "/wd5262")
    endif()

    foreach(t IN LISTS TOOL_EXES)
      target_compile_options(${t} PRIVATE ${WarningsEXE})
    endforeach()
endif()

if(WIN32)
    if(WINDOWS_STORE)
      target_compile_definitions(${PROJECT_NAME} PRIVATE WINAPI_FAMILY=WINAPI_FAMILY_APP)
    endif()

    if(WINDOWS_STORE OR (${DIRECTX_ARCH} MATCHES "^arm64") OR (DEFINED XBOX_CONSOLE_TARGET))
        set(WINVER 0x0A00)
    elseif(${DIRECTX_ARCH} MATCHES "^arm")
        set(WINVER 0x0602)
    else()
        set(WINVER 0x0601)
    endif()

    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_definitions(${t} PRIVATE _UNICODE UNICODE _WIN32_WINNT=${WINVER})
    endforeach()

    if(DISABLE_MSVC_ITERATOR_DEBUGGING)
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_definitions(${t} PRIVATE _ITERATOR_DEBUG_LEVEL=0)
      endforeach()
    endif()
endif()

if(BUILD_TOOLS AND WIN32 AND (NOT WINDOWS_STORE))
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT uvatlastool)
endif()

#--- Test suite
include(CTest)
if(BUILD_TESTING AND WIN32 AND (NOT WINDOWS_STORE) AND (EXISTS "${CMAKE_CURRENT_LIST_DIR}/Tests/CMakeLists.txt"))
  enable_testing()
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Tests)
endif()
