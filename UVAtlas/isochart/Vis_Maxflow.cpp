//-------------------------------------------------------------------------------------
// UVAtlas  - Vis_Maxflow.cpp
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

#include "pch.h"
#include "vis_maxflow.h"

using namespace Isochart;

size_t CMaxFlow::Node::m_expect_degree = 3;

// reserve the memory for nodes and edges
// for better memory performance
void CMaxFlow::ReserveMemory(size_t nNodes, size_t nEdges, size_t nDegree)
{
    nodes.clear();
    edges.clear();

    Node::m_expect_degree = nDegree;
    if (nEdges == 0)
    {
        nEdges = nNodes * nDegree;
    }

    nodes.reserve(nNodes);
    edges.reserve(nEdges * 2);  // bi-directional edges, hence *2
}
    
bool CMaxFlow::InitGraphCut(size_t nNodes, size_t nEdges, size_t nDegree)
{
    nodes.clear();
    edges.clear();

    Node::m_expect_degree = nDegree;
    if (nEdges == 0)
    {
        nEdges = nNodes * nDegree;
    }

    try
    {
        nodes.resize(nNodes);
        edges.reserve(nEdges * 2);// bi-directional edges, hence *2
    }
    catch (std::bad_alloc&)
    {
        return false;
    }

    return true;
}

// add an empty node and return it's id
CMaxFlow::node_id CMaxFlow::AddNode()
{
    assert( (size_t)(m_nodeNumber) < nodes.size());

    node_id id = (node_id)(m_nodeNumber);
    m_nodeNumber++;
    return id;
    /*
    nodes.add(Node());
    return node_id(nodes.size() - 1);
    */
}

// add an edge connecting given node and given capacity
void CMaxFlow::AddEdge(
    node_id n0, node_id n1, cap_type c01, cap_type c10)
{
    // add a new edge, assign to n0 and weight c01
    edges.push_back(Edge());
    Edge& e0 = edges.back();
    edge_id eid0 = edge_id(edges.size() - 1);
    e0.cap = e0.res = c01;
    nodes[n0].edges.push_back(eid0);
    assert(nodes[n0].edges.size() <= 6);

    e0.n0 = n0;
    e0.n1 = n1;

    // add a new edge, assign to n1 and weight c10
    edges.push_back(Edge());
    Edge& e1 = edges.back();
    edge_id eid1 = edge_id(edges.size() - 1);
    e1.cap = e1.res = c10;
    nodes[n1].edges.push_back(eid1);
    assert(nodes[n1].edges.size() <= 6);
    e1.n0 = n1;
    e1.n1 = n0;

    assert(eid0 == reverse_edge(eid1));
    assert(eid1 == reverse_edge(eid0));
}

// reset the flow by clear the current flow
// reset the residual of n-links, and t-links
void CMaxFlow::ResetResident()
{
    current_flow = 0;
        
    for (size_t i=0; i<nodes.size(); i++)
    {
        nodes[i].resident = nodes[i].capacity;
    }

    for (size_t i=0; i<edges.size(); i++)
    {
        edges[i].res = edges[i].cap;
    }
}

// set a t-links capacity
void CMaxFlow::SetTweights(node_id id, cap_type sw, cap_type tw)
{
    Node& n = nodes[id];
    n.resident = n.capacity = sw - tw;
    current_flow += __min(sw, tw);

}

// initialize the graph so that the algorithm can run
void CMaxFlow::Initialization()
{
    // assume the residual has been reset        

    std::queue<node_id> empty1;
    orphan_list.swap(empty1);

    std::queue<node_id> empty2;
    active_list.swap(empty2);

    for (node_id k = 0; k <(node_id) nodes.size(); k ++)
    {
        // initialize each node
        // assume capacity and resident has benn assigned
        Node& n = nodes[k];

        // connect the node to either s or t node, or free
        if (n.resident > 0)
        {
            // connect to source
            n.set_to_s();
            n.set_parent_to_s();
            push_active(k); // put the node into active list
        }
        else if (n.resident < 0)
        {
            // connect to sink
            n.set_to_t();
            n.set_parent_to_t();
            push_active(k); // put the node into active list
        }
        else // n.resident == 0
        {
            n.set_free();
            n.set_no_parent();
        }
    }
}

// find an augment path in the graph,
bool CMaxFlow::FindAugmentPath()
{
    while (!active_list.empty())
    {
        // get the active node from queue
        const node_id nid = active_list.front();
        active_list.pop();
        Node& n = nodes[nid];

        // in case n has been in active list twice
        // the second time, n may be free
        if (n.is_free()) continue;

        // iterator all edges from n
        for(size_t i=0; i<n.edges.size(); i++)
        {
            const edge_id eid_nm = n.edges[i];
            const Edge& e_nm = edges[eid_nm];

            // find the n1 end
            const node_id mid = e_nm.n1;
            Node& m = nodes[mid];

            // if node n is connecting to source tree
            if (n.to_s())
            {
                // if m is in tree of source, ignore it
                if (m.to_s()) continue;

                // if res from n to m is positive, i.e. can flow
                if (e_nm.res > 0)
                {
                    // if node m is to_t, we've found a path
                    if (m.to_t())
                    {
                        // put n back into active list
                        push_active(nid);
                        trace_current_path(nid, mid, eid_nm);
                           
                        return true;
                    }
                    else
                    {
                        assert(m.is_free());

                        // add m to active list
                        push_active(mid);
                        m.set_parent_node(nid);
                        m.set_parent_edge(eid_nm);
                        m.next_level_of(n);
                        assert(e_nm.res > 0);
                        m.set_to_s();
                    }
                }
            }
            else
            {
                // otherwise, n must connecting to sink
                assert(n.to_t());
                    
                // if m is on same tree, ignore it
                if (m.to_t()) continue;

                // if res from m to n is positive, i.e. can flow
                const Edge& e_mn = edges[reverse_edge(eid_nm)];
                if (e_mn.res > 0)
                {
                    // if node m is to_s, we found a path
                    if (m.to_s())
                    {
                        // put n back into active list
                        push_active(nid);
                        trace_current_path(mid, nid, reverse_edge(eid_nm));
                        return true;
                    }
                    else
                    {
                        assert(m.is_free());

                        // add m to active list
                        push_active(mid);
                        m.set_parent_node(nid);
                        m.set_parent_edge(reverse_edge(eid_nm));
                        m.next_level_of(n);
                        assert(e_mn.res > 0);
                        m.set_to_t();
                    }
                }
            }
        }
    }

    return false;
}

void CMaxFlow::trace_current_path(node_id n_to_s, node_id m_to_t, edge_id eid_nm)
{
    current_path.clear();

    //// insert the edge from n to m
    //Node& nn = nodes[n_to_s];
    //edge_id the_eid = invalid_edge_id();
    //for (size_t k = 0; k < nn.edges.size(); k ++)
    //{
    //    const edge_id eid = nn.edges[k];
    //    const Edge& e = edges[eid];
    //    if (e.n1 == m_to_t)
    //    {
    //        the_eid = eid;
    //        break;
    //    }
    //}
    edge_id the_eid = eid_nm;
    assert(the_eid != invalid_edge_id() && edges[the_eid].res > 0);
    current_path.push_back(the_eid);

    // from n to s
    node_id qn = n_to_s;
    for (;;)
    {
        const Node& node_qn = nodes[qn];
        assert(node_qn.to_s());
        const node_id pm = node_qn.get_parent_node();

        // if hit s, stop
        if (pm == node_s)
        {
            // the end point
            ns_id = qn;
            break;
        }

        // insert the edge from pm to qn
        current_path.push_back(node_qn.get_parent_edge());

        // move to the next point
        qn = pm;
    }

    // from m to t
    node_id pm = m_to_t;
    for (;;)
    {
        const Node& node_pm = nodes[pm];
        assert(node_pm.to_t());
        qn = node_pm.get_parent_node();

        // if hit t, stop
        if (qn == node_t)
        {
            // the end point
            mt_id = pm;
            break;
        }

        // insert the edge from qn to pm
        current_path.push_back(node_pm.get_parent_edge());

        // move to next point
        pm = qn;
    }

}

// augment the path and find orphans
void CMaxFlow::AugmentCurrentPath()
{
    // assume ns_id, mt_id and current path is ready
    assert(!current_path.empty());

    // find two end point
    Node& ns = nodes[ns_id];
    assert(ns.resident > 0);

    Node& mt = nodes[mt_id];
    assert(mt.resident < 0);

    cap_type bottleneck = __min(ns.resident, -mt.resident);

    // find the bottleneck        
        
    for(size_t i=0; i<current_path.size(); i++)
    {
        // get the edge
        const edge_id eid = current_path[i];
        Edge& e = edges[eid];
        bottleneck = __min(bottleneck, e.res);
    }

    assert(bottleneck > 0);

    // augment the path by bottleneck
    ns.resident -= bottleneck;
    mt.resident += bottleneck;

    // find orphans
    assert(orphan_list.empty());

    if (ns.resident == 0)
    {
        ns.set_no_parent();
        mark_orphan(ns_id);
    }

    if (mt.resident == 0)
    {
        mt.set_no_parent();
        mark_orphan(mt_id);
    }

    for ( size_t i = 0; i < current_path.size(); i++)
    {
        // get the edge
        const edge_id eid = current_path[i];
        Edge& e = edges[eid];                   // e: p->q
        Edge& er = edges[reverse_edge(eid)];    // er: q->p
        e.res -= bottleneck;
        er.res += bottleneck;

        // if the edge is saturated
        if (e.res == 0)
        {
            Node& p = nodes[e.n0];
            Node& q = nodes[e.n1];

            // add an orphan
            if (p.to_s() && q.to_s())
            {
                q.set_no_parent();
                mark_orphan(e.n1);
            }
            else if (p.to_t() && q.to_t())
            {
                p.set_no_parent();
                mark_orphan(e.n0);
            }
        }
    }

    current_flow += bottleneck;
}

bool CMaxFlow::connecting_to_st(node_id qid) const
{
    if (nodes[qid].is_free()) return false;

    for (;;)
    {
        const Node& q = nodes[qid];

        node_id pid = q.get_parent_node();
        if (pid == no_parent) return false;

        if (pid == node_s || pid == node_t)
        {
            assert(q.resident != 0);
            return true;
        }

        // move one level up
        qid = pid;
    }
}

// adopt orphans, either connect to tree or free them
void CMaxFlow::AdoptOrphans()
{
    while (!orphan_list.empty())
    {
        node_id pid = orphan_list.front();
        orphan_list.pop();
        Node& p = nodes[pid];

        // find a parent_node
        node_id candidate_parent_node = invalid_node_id();
        edge_id candidate_parent_edge = invalid_edge_id();

        // the orphan must have zero resident
        // {liyin} I don't know why, but in experiments, it is
        // if not, we should collect parents from s or t.
        assert(p.resident == 0);

        //if (p.resident > 0)
        //{
        //    candidate_parent_node = node_s;
        //}
        //else if (p.resident < 0)
        //{
        //    candidate_parent_node = node_t;
        //}
        //else
        {
            int best_depth = 0;
            for (size_t k = 0; k < p.edges.size(); k ++)
            {
                edge_id eid_pq = p.edges[k];

                // q is a neighbor node
                node_id qid = edges[eid_pq].n1;
                const Node& q = nodes[qid];

                if (p.on_same_tree(q))
                {
                    if (p.to_s() && q.to_s())
                    {
                        // find the edge from q to p
                        edge_id eid_qp = reverse_edge(eid_pq);
                        const Edge& eqp = edges[eid_qp];

                        // if can not flow from q to p, ignore it
                        if (eqp.res == 0) continue;
                    }
                    else if (p.to_t() && q.to_t())
                    {
                        const Edge& epq = edges[eid_pq];

                        // if can not flow from p to q, ignore it
                        if (epq.res == 0) continue;
                    }

                    // must not be on an orphan tree
                    if (connecting_to_st(qid))
                    {
                        const int depth = nodes[qid].get_depth();
                        if (best_depth == 0 || best_depth > depth)
                        {
                            candidate_parent_node = qid;
                            candidate_parent_edge = 
                                p.to_s() ? reverse_edge(eid_pq) : eid_pq; 
                            best_depth = depth;
                        }
                    }
                }
            }
        }

        // if we find a parent_node, adopte the orphan
        if (candidate_parent_node != invalid_node_id())
        {
            //if (candidate_parent_node == node_s)
            //{
            //    p.set_parent_to_s();
            //}
            //else if (candidate_parent_node == node_t)
            //{
            //    p.set_parent_to_t();
            //}
            //else
            {
                // set the new parent_node of p
                p.set_parent_node(candidate_parent_node);
                p.set_parent_edge(candidate_parent_edge);
                p.next_level_of(nodes[candidate_parent_node]);
            }
        }
        else
        {
            // make its children orphan
            for (size_t k = 0; k < p.edges.size(); k ++)
            {
                edge_id eid_pq = p.edges[k];

                // q is a neighbor node
                node_id qid = edges[eid_pq].n1;
                Node& q = nodes[qid];

                // p and q must to same tree
                if (p.on_same_tree(q))
                {
                    // if q is a child, assign it as orphan
                    if (q.get_parent_node() == pid)
                    {
                        q.set_no_parent();
                        mark_orphan(qid);
                    }

                    // because p is going to be freed
                    //      all its neighbors connected through
                    //      anon saturated edges should be activated
                    edge_id eid_qp = reverse_edge(eid_pq);
                    if (edges[eid_qp].res > 0)
                    {
                        push_active(qid);
                    }
                }
            }

            // disgard p as free node
            p.set_free();
        }
    }
}
