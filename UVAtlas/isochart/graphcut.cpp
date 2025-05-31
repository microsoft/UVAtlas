//-------------------------------------------------------------------------------------
// UVAtlas - graphcut.cpp
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#include "pch.h"
#include "graphcut.h"

using namespace Isochart;

CGraphcut::CGraphcut()
{}

CGraphcut::~CGraphcut()
{
    Clear();
}

void CGraphcut::Clear()
{
    graph.Reset();
}

CGraphcut::NODEHANDLE CGraphcut::AddNode()
{
    return graph.AddNode();
}

CGraphcut::NODEHANDLE CGraphcut::AddNode(float fSourceWeight, float fSinkWeight)
{
    CMaxFlow::node_id hnode = graph.AddNode();
    graph.SetTweights(hnode, fSourceWeight, fSinkWeight);
    return hnode;
}

HRESULT CGraphcut::AddEges(NODEHANDLE hFromNode, NODEHANDLE hToNode, float fWeight, float fReverseWeight)
{
    graph.AddEdge(hFromNode, hToNode, fWeight, fReverseWeight);
    return S_OK;
}

HRESULT CGraphcut::SetWeights(NODEHANDLE hNode, float fSourceWeight, float fSinkWeight)
{
    graph.SetTweights(hNode, fSourceWeight, fSinkWeight);
    return S_OK;
}

HRESULT CGraphcut::CutGraph(float &fMaxflow)
{
    graph.ComputeMaxFlow();
    fMaxflow = graph.GetFlow();
    return S_OK;
}

bool CGraphcut::IsInSourceDomain(NODEHANDLE hNode)
{
    return graph.TestToS(hNode);
}
