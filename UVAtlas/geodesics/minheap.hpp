//-------------------------------------------------------------------------------------
// UVAtlas - minheap.hpp
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
#include "maxheap.hpp"

// the "wrapper" class that reverse the ">" and "<" comparison operations
template <class _Ty1>
class _ReverseComparison
{
public:
    _ReverseComparison()
    {
        memset( &m_data, 0, sizeof(_Ty1) ) ;
    }
    _ReverseComparison( const _Ty1 &R )
    {
        m_data = R ;
    }

    _ReverseComparison &operator=( const _ReverseComparison &R )
    {
        m_data = R.m_data ;
        return *this ;
    }
    bool operator>( const _ReverseComparison &R )
    {
        return m_data < R.m_data ;
    }
    bool operator<( const _ReverseComparison &R )
    {
        return m_data > R.m_data ;
    }

private:
    _Ty1 m_data ;
};

// the CMinHeap is actually a CMaxHeap, except the first template parameter is wrapped by _ReverseComparison
template <class _Ty1, class _Ty2>
class CMinHeap : public Isochart::CMaxHeap<_ReverseComparison<_Ty1>, _Ty2>
{
};
