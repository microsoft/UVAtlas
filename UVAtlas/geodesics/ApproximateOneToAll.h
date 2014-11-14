//-------------------------------------------------------------------------------------
// UVAtlas - ApproximateOneToAll.h
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

#include "ExactOneToAll.h"

#pragma warning(push)
#pragma warning(disable : 4481)
// VS 2010 considers 'override' to be a extension, but it's part of C++11 as of VS 2012

namespace GeodesicDist
{
    class CApproximateOneToAll : public CExactOneToAll
    {
    private:
        virtual void CutHeapTopData( EdgeWindow &EdgeWindowOut ) override;
    };
}

#pragma warning(pop)