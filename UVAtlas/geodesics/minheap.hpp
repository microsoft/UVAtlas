//-------------------------------------------------------------------------------------
// UVAtlas - minheap.hpp
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once
#include "maxheap.hpp"

namespace Internal
{
    // the "wrapper" class that reverse the ">" and "<" comparison operations
    template <class T>
    class ReverseComparison
    {
    public:
        ReverseComparison() :
            m_data{}
        {
        }
        ReverseComparison(const T& R)
        {
            m_data = R;
        }

        ReverseComparison& operator=(const ReverseComparison& R)
        {
            m_data = R.m_data;
            return *this;
        }
        bool operator>(const ReverseComparison& R)
        {
            return m_data < R.m_data;
        }
        bool operator<(const ReverseComparison& R)
        {
            return m_data > R.m_data;
        }

    private:
        T m_data;
    };
}

// the CMinHeap is actually a CMaxHeap, except the first template parameter is wrapped by ReverseComparison
template <class Ty1, class Ty2>
class CMinHeap : public Isochart::CMaxHeap<Internal::ReverseComparison<Ty1>, Ty2>
{
};
