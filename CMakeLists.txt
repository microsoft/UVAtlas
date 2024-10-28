# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.20)

set(UVATLAS_VERSION 1.8.9)

if(WINDOWS_STORE OR (DEFINED XBOX_CONSOLE_TARGET))
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
endif()

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

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(WINDOWS_STORE OR (DEFINED XBOX_CONSOLE_TARGET))
  set(BUILD_TOOLS OFF)
endif()

include(GNUInstallDirs)
include(build/CompilerAndLinker.cmake)

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

if(UVATLAS_USE_OPENMP)
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
  else()
    set(UVATLAS_USE_OPENMP OFF)
  endif()
endif()

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
    target_link_libraries(${PROJECT_NAME} PUBLIC Eigen3::Eigen Spectra::Spectra)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UVATLAS_USE_EIGEN)
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

# Create pkg-config file
include(build/JoinPaths.cmake)
# from: https://github.com/jtojnar/cmake-snips#concatenating-paths-when-building-pkg-config-files
join_paths(UVATLAS_INCLUDEDIR_FOR_PKG_CONFIG "\${prefix}" "${CMAKE_INSTALL_INCLUDEDIR}")
join_paths(UVATLAS_LIBDIR_FOR_PKG_CONFIG "\${prefix}"     "${CMAKE_INSTALL_LIBDIR}")

set(UVATLAS_DEP_L "")
if(ENABLE_USE_EIGEN)
  list(APPEND UVATLAS_DEP_L "eigen3")
endif()
if(directxmath_FOUND)
  list(APPEND UVATLAS_DEP_L "DirectXMath")
endif()
if(directx-headers_FOUND)
  list(APPEND UVATLAS_DEP_L "DirectX-Headers")
endif()
list(LENGTH UVATLAS_DEP_L DEP_L)
if(DEP_L)
  string(REPLACE ";" ", " UVATLAS_DEP " ${UVATLAS_DEP_L}")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/build/UVAtlas.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/UVAtlas.pc" @ONLY)

# Install the pkg-config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/UVAtlas.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

#--- Command-line tool
if(BUILD_TOOLS AND WIN32)
    set(TOOL_EXES uvatlastool)

    message(STATUS "Using DirectXMesh and DirectXTex (required for uvatlastool).")

    find_package(directxmesh CONFIG REQUIRED COMPONENTS library utils)
    find_package(directxtex CONFIG REQUIRED)

    add_executable(uvatlastool
        UVAtlasTool/UVAtlas.cpp
        UVAtlasTool/uvatlas.rc
        UVAtlasTool/settings.manifest
        UVAtlasTool/CmdLineHelpers.h
        UVAtlasTool/Mesh.cpp
        UVAtlasTool/Mesh.h
        UVAtlasTool/MeshOBJ.cpp
        UVAtlasTool/SDKMesh.h)
    target_compile_features(uvatlastool PRIVATE cxx_std_17)
    target_link_libraries(uvatlastool PRIVATE
        ${PROJECT_NAME}
        ole32.lib version.lib
        Microsoft::DirectXMesh
        Microsoft::DirectXTex
        Microsoft::DirectXMesh::Utilities)
    source_group(uvatlastool REGULAR_EXPRESSION UVAtlasTool/*.*)
endif()

if(directxmath_FOUND)
    foreach(t IN LISTS TOOL_EXES)
      target_link_libraries(${t} PRIVATE Microsoft::DirectXMath)
    endforeach()
endif()

if(TOOL_EXES)
  message(STATUS "Building tools: ${TOOL_EXES}")
endif()

if(MSVC)
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE /Wall /EHsc /GR-)
    endforeach()
endif()

foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
  target_compile_definitions(${t} PRIVATE ${COMPILER_DEFINES})
  target_compile_options(${t} PRIVATE ${COMPILER_SWITCHES})
  target_link_options(${t} PRIVATE ${LINKER_SWITCHES})
endforeach()

if(MINGW)
    foreach(t IN LISTS TOOL_EXES)
      target_link_options(${t} PRIVATE -municode)
    endforeach()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|IntelLLVM")
    set(WarningsLib -Wall -Wpedantic -Wextra)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.0)
        list(APPEND WarningsLib "-Wno-unsafe-buffer-usage")
    endif()
    target_compile_options(${PROJECT_NAME} PRIVATE ${WarningsLib})

    set(WarningsEXE ${WarningsLib} "-Wno-c++98-compat" "-Wno-c++98-compat-pedantic" "-Wno-switch" "-Wno-switch-enum" "-Wno-exit-time-destructors" "-Wno-switch" "-Wno-switch-enum" "-Wno-language-extension-token" "-Wno-missing-prototypes")
    foreach(t IN LISTS TOOL_EXES)
      target_compile_options(${t} PRIVATE ${WarningsEXE})
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_options(${t} PRIVATE "-Wno-ignored-attributes" "-Walloc-size-larger-than=4GB")
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 14)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    if(ENABLE_CODE_ANALYSIS)
      message(STATUS "Building with Code Analysis (PREFIX)")
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_options(${t} PRIVATE /analyze /WX)
      endforeach()
    endif()

    if(ENABLE_SPECTRE_MITIGATION
       AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.13)
       AND (NOT WINDOWS_STORE))
        message(STATUS "Building Spectre-mitigated libraries")
        foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
          target_compile_options(${t} PRIVATE "/Qspectre")
        endforeach()
    endif()

    set(WarningsEXE "/wd4365" "/wd4514" "/wd4571" "/wd4625" "/wd4626" "/wd4627" "/wd4668" "/wd4710" "/wd4711" "/wd4751" "/wd4774" "/wd4820" "/wd5026" "/wd5027" "/wd5039" "/wd5045" "/wd4061" "/wd4062" "/wd5219")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.34)
      list(APPEND WarningsEXE "/wd5262" "/wd5264")
      target_compile_options(${PROJECT_NAME} PRIVATE "/wd5262")
    endif()
    foreach(t IN LISTS TOOL_EXES)
      target_compile_options(${t} PRIVATE ${WarningsEXE})
    endforeach()
endif()

if(WIN32)
    if(WINDOWS_STORE OR (${DIRECTX_ARCH} MATCHES "^arm64") OR (DEFINED XBOX_CONSOLE_TARGET))
        set(WINVER 0x0A00)
    elseif(${DIRECTX_ARCH} MATCHES "^arm")
        set(WINVER 0x0602)
    else()
        set(WINVER 0x0601)
    endif()

    foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
      target_compile_definitions(${t} PRIVATE _WIN32_WINNT=${WINVER})
    endforeach()

    if(DISABLE_MSVC_ITERATOR_DEBUGGING)
      foreach(t IN LISTS TOOL_EXES ITEMS ${PROJECT_NAME})
        target_compile_definitions(${t} PRIVATE _ITERATOR_DEBUG_LEVEL=0)
      endforeach()
    endif()
endif()

if(BUILD_TOOLS AND WIN32)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT uvatlastool)
endif()

if(BUILD_TOOLS AND (NOT VCPKG_TOOLCHAIN))
    foreach(t IN LISTS TOOL_EXES)
      install(TARGETS ${t} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()
endif()

#--- Test suite
if(WIN32 AND (NOT WINDOWS_STORE) AND (NOT (DEFINED XBOX_CONSOLE_TARGET)))
    include(CTest)
    if(BUILD_TESTING AND (EXISTS "${CMAKE_CURRENT_LIST_DIR}/Tests/CMakeLists.txt"))
        enable_testing()
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Tests)
    endif()
endif()
