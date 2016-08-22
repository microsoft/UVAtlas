//-------------------------------------------------------------------------------------
// UVAtlas - graphcut.h
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

#include "vis_maxflow.h"

namespace Isochart
{
    class CGraphcut
    {
        public:
            typedef int NODEHANDLE;

            CGraphcut();
            ~CGraphcut();

            HRESULT InitGraph(
                size_t dwNodeNumber)
            { 
                if (!graph.InitGraphCut(dwNodeNumber, 0, 6))
                {
                    return E_OUTOFMEMORY;
                }
                return S_OK;
            }
            
            NODEHANDLE AddNode();
            NODEHANDLE AddNode(
                float fSourceWeight, 
                float fSinkWeight);

            HRESULT AddEges(
                NODEHANDLE hFromNode, 
                NODEHANDLE hToNode, 
                float fWeight, 
                float fReverseWeight);
            
            HRESULT SetWeights(
                NODEHANDLE hNode, 
                float fSourceWeight, 
                float fSinkWeight);
            
            HRESULT CutGraph(float& fMaxflow);

            bool IsInSourceDomain(
                NODEHANDLE hNode);

            void Clear();

        private:
            struct NODE
            {
                bool bIsInSourceDomain;
                float fWeight;
            };

            struct EDGE
            {
                float fWeight;
            };


            CMaxFlow graph;
            
    };
}
