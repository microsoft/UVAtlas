//-------------------------------------------------------------------------------------
// UVAtlas
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#ifdef _MSC_VER
// Off by default warnings
#pragma warning(disable : 4619 4616 4061 4365 4571 4623 4625 4626 4628 4668 4710 4711 4746 4820 4987 5026 5027 5031 5032 5039 5045 5264 26451 26812)
// C4619/4616 #pragma warning warnings
// C4061 enumerator 'X' in switch of enum 'X' is not explicitly handled by a case label
// C4365 signed/unsigned mismatch
// C4571 behavior change
// C4623 default constructor was implicitly defined as deleted
// C4625 copy constructor was implicitly defined as deleted
// C4626 assignment operator was implicitly defined as deleted
// C4628 digraphs not supported
// C4668 not defined as a preprocessor macro
// C4710 function not inlined
// C4711 selected for automatic inline expansion
// C4746 volatile access of '<expression>' is subject to /volatile:<iso|ms> setting
// C4820 padding added after data member
// C4987 nonstandard extension used
// C5026 move constructor was implicitly defined as deleted
// C5027 move assignment operator was implicitly defined as deleted
// C5031/5032 push/pop mismatches in windows headers
// C5039 pointer or reference to potentially throwing function passed to extern C function under - EHc
// C5045 Spectre mitigation warning
// C5264 'const' variable is not used
// 26451: Arithmetic overflow: Using operator '*' on a 4 byte value and then casting the result to a 8 byte value.
// 26812: The enum type 'x' is unscoped. Prefer 'enum class' over 'enum' (Enum.3).
#endif // _MSC_VER

#ifdef __clang__
#pragma clang diagnostic ignored "-Wc++98-compat"
#pragma clang diagnostic ignored "-Wc++98-compat-pedantic"
#pragma clang diagnostic ignored "-Wc++98-compat-local-type-template-args"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005)
#endif
#define NOMINMAX 1
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <Windows.h>
#include <objbase.h>

#ifdef __MINGW32__
#include <unknwn.h>
#endif

#else // !WIN32
#include <wsl/winadapter.h>
#include <directx/d3d12.h>
#endif

#define _USE_MATH_DEFINES
#include <cmath>

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <queue>
#include <tuple>

#ifdef UVATLAS_USE_EIGEN
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127 4244 4265 4456 4464 5220)
#endif
#include <Eigen/Dense>
#include <Spectra/SymEigsSolver.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4774)
#endif
#include <random>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifndef _WIN32
#include <mutex>
#include <thread>
#endif

#define _XM_NO_XMVECTOR_OVERLOADS_

#include <DirectXMath.h>

#include "UVAtlas.h"

#ifdef _DEBUG
extern void __cdecl UVAtlasDebugPrintf(unsigned int lvl, _In_z_ _Printf_format_string_ const char* szFormat, ...);
#define DPF UVAtlasDebugPrintf
#else
#define DPF(...)
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=nullptr; } }
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=nullptr; } }
#endif

// HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)
#define HRESULT_E_ARITHMETIC_OVERFLOW static_cast<HRESULT>(0x80070216L)

// HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
#define HRESULT_E_NOT_SUPPORTED static_cast<HRESULT>(0x80070032L)

// HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
#define HRESULT_E_INVALID_DATA static_cast<HRESULT>(0x8007000DL)
