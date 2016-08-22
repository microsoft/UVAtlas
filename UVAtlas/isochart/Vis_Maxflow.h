//-------------------------------------------------------------------------------------
// UVAtlas - Vis_Maxflow.h
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

/*
    maxflow algortihm based on Boykov 2003 PAMI

Notes:
    1. If you know the number of nodes and edges, call ReserveMemory to
        improve the speed of graph construction
     
Usage:
    1. Use AddNode() to allocate nodes in graph
    2. Use AddEdge(node_id, node_id, cap_type, cap_type) to add n-links.
    3. Use SetTweights(node_id, cap_type, cap_type) to set the t-links
    4. Call compute MaxFlow() to compute s-t maxflow problem
    5. Call GetNode(id).to_s() to get the result label of node id.
    6. [optional] Call GetFlow() to access the final maxflow result. 
*/

#pragma once

#include <queue>
#include <vector>

namespace Isochart
{
    class CMaxFlow
    {
    public:
        // flow type is the flow across edges, from s to t, float type
        typedef float flow_type;

        // cap type is the capacity of each edge, bidirectional, signed
        typedef float cap_type;

        typedef int node_id;

        static node_id invalid_node_id() { return -1;}

    protected:
        typedef int edge_id;

        static edge_id invalid_edge_id() { return -1;}

        const static node_id node_t = -10;
        const static node_id node_s = -11;
        const static node_id no_parent = -20;

        struct Node;
        struct Edge;
    private:
        size_t m_nodeNumber;

    public:
        bool InitGraphCut(
            size_t nNodes,     // expected node number
            size_t nEdges, //
            size_t nDegree);
        // reserve the memory for nodes and edges
        // for better memory performance
        void ReserveMemory(
            size_t nNodes,          // expected node number
            size_t nEdges = 0,      // 0 means  node number * nDegree
            size_t nDegree = 6);    // expected out degree of each node

        // reset the whole graph, rebuild graph by addnode and addedge
        void Reset()
        {
            nodes.clear();
            edges.clear();
            current_flow = 0;
            m_nodeNumber = 0;
        }

        // reset only the residual, graph is not changed
        void ResetResident();

        // allocate a new node, return the node id for access later
        node_id AddNode();

        // allocate an edge, and it's capacity
        // c01, directional capacity from n0 to n1
        // c10, reversed capcity.
        void AddEdge(node_id n0, node_id n1, cap_type c01, cap_type c10);

        // set the t-link weight for the give node
        // sw = capacity to s node
        // tw = capacity to t node
        void SetTweights(node_id id, cap_type sw, cap_type tw);

        // the main algorithm
        void ComputeMaxFlow()
        {
            assert( m_nodeNumber == nodes.size());
            Initialization();
            while (FindAugmentPath())
            {
                AugmentCurrentPath();
                AdoptOrphans();
            }
        }

        // test the given node label, after ComputeMaxFlow
        // return true if the given node is to s.
        bool TestToS(node_id id) const { return nodes[id].to_s();}

        // return true if the given node is to t.
        bool TestToT(node_id id) const { return nodes[id].to_t();}

        // access the final flow result
        flow_type GetFlow() const { return current_flow;}

    protected:
        void Initialization();
        bool FindAugmentPath();
        void AugmentCurrentPath();
        void AdoptOrphans();

        flow_type current_flow;

        // the current path include two nodes for t-link
        // and a list of eges for n-link
        node_id ns_id, mt_id;
        std::vector<edge_id> current_path;

        // trace path, from n to s, and from m to t, m and n are adjacent
        void trace_current_path(node_id n_to_s, node_id m_to_t, edge_id eid_nm);

        // NOTE: a node can be active twice or more, insert in queue
        void push_active(node_id nid)
        {
            assert(nid >= 0);
            active_list.push(nid);
        }

        void mark_orphan(node_id nid) { orphan_list.push(nid);}

        std::queue<node_id> active_list;
        std::queue<node_id> orphan_list;

        typedef std::vector<Node> NodeList;
        typedef std::vector<Edge> EdgeList;

        NodeList nodes;
        EdgeList edges;

        // source and sink node do not has node structure, virtual
        // the node has information of
        // 1. the resident to the additional s, and t node
        //    positive is to source, and nagative is to sink
        // 2. to access neighboring edges
        struct Node
        {
        public:
            Node()
                : capacity(0), resident(0), depth(0)
                , parent_node(no_parent), parent_edge(no_parent)
                , m_iFlag(0), m_iActive(0)
            {
                edges.reserve(m_expect_degree);
            };

            cap_type capacity;
            cap_type resident; // resident > 0 to s; or < 0 to t;
            
            // the edges from this node to the n1 one
            std::vector<edge_id> edges;

            void set_to_s() { m_iFlag = TO_S;}
            void set_to_t() { m_iFlag = TO_T;}
            void set_free() { m_iFlag = FREE;}
            bool to_s()    const { return m_iFlag == TO_S;}
            bool to_t()    const { return m_iFlag == TO_T;}
            bool is_free() const { return m_iFlag == FREE;}

            bool on_same_tree(const Node& q) const
            {
                return !is_free() &&(m_iFlag == q.m_iFlag);
            }

            void set_no_parent()
            { 
                parent_node = no_parent;
                parent_edge = no_parent;
                depth = 0;
            }

            void set_parent_to_s()
            {
                parent_node = node_s;
                parent_edge = no_parent;
                depth = 1;
            }

            void set_parent_to_t()
            {
                parent_node = node_t;
                parent_edge = no_parent;
                depth = 1;
            }

            void set_parent_node(node_id n) { parent_node = n;}
            node_id get_parent_node() const { return parent_node;}

            void set_parent_edge(edge_id n) { parent_edge = n;}
            edge_id get_parent_edge() const { return parent_edge;}

            void next_level_of(const Node& n)
            {
                depth = n.depth + 1;
            }

            int get_depth() const { return depth;}

            static size_t m_expect_degree;

        protected:
            node_id parent_node;    // parent node on the tree
            edge_id parent_edge;    // the edge to parent node. always s->t

        private:
            typedef int FLAG;
            FLAG m_iFlag;
            FLAG m_iActive;
            int depth;   // distanst to either s or t
            

            const static FLAG FREE = 0;
            const static FLAG TO_S = 1;
            const static FLAG TO_T = 2;
        };

        // only n-links, t-links are virtual in node
        // edges must be inserted two by two
        // two of them are reverse directional
        // one from p to q, the n1 from q to p
        // they can be accessed by reverse_edge function
        struct Edge
        {
            cap_type cap;    // capacity
            cap_type res;    // resident
            node_id n0, n1;   // the node_id, edge is from n0 to n1
        };

        // reverse direction is accessed by odd and even transition
        const edge_id reverse_edge(edge_id eid) const
        {
            return eid ^ 0x1;
        }

        bool connecting_to_st(node_id qid) const;
    };
}
