//-------------------------------------------------------------------------------------
// UVAtlas
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

// VS 2013 related Off by default warnings
#pragma warning(disable : 4619 4616 4350)
// C4619/4616 #pragma warning warnings
// C4350 behavior change

// Off by default warnings
#pragma warning(disable : 4061 4365 4571 4623 4625 4626 4668 4710 4711 4746 4820 4987 5026 5027 5031 5032 5039)
// C4061 enumerator 'X' in switch of enum 'X' is not explicitly handled by a case label
// C4365 signed/unsigned mismatch
// C4571 behavior change
// C4623 default constructor was implicitly defined as deleted
// C4625 copy constructor was implicitly defined as deleted
// C4626 assignment operator was implicitly defined as deleted
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

// Windows 8.1 SDK related Off by default warnings
#pragma warning(disable : 5029)
// C5029 nonstandard extension used

#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)

#include <windows.h>
#include <objbase.h>

#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <functional>
#include <vector>
#include <queue>

#include <DirectXMath.h>

#include "UVAtlas.h"

#ifdef _DEBUG
extern void __cdecl UVAtlasDebugPrintf(unsigned int lvl, _In_z_ _Printf_format_string_ LPCSTR szFormat, ...);
#define DPF UVAtlasDebugPrintf
#else
#define DPF(l,s,...)
#endif

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);   (p)=nullptr; } }
#endif    

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=nullptr; } }
#endif   
