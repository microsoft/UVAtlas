//-------------------------------------------------------------------------------------
// UVAtlas - ApproximateOneToAll.h
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#include "ExactOneToAll.h"


namespace GeodesicDist
{
    class CApproximateOneToAll : public CExactOneToAll
    {
    private:
        virtual void CutHeapTopData( EdgeWindow &EdgeWindowOut ) override;
    };
}
