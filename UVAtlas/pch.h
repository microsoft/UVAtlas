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
