//-------------------------------------------------------------------------------------
// UVAtlas - ApproximateOneToAll.h
//
// Copyright (c) Microsoft Corporation.
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
        void CutHeapTopData(EdgeWindow& EdgeWindowOut) override;
    };
}
