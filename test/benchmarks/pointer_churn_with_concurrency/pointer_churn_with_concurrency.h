// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "cpp/cown.h"
#include "cpp/when.h"
#include "region/region_base.h"
#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <random>
#include <vector>
#include <verona.h>

// Maximum number of outgoing edges per node
#define MAX_OUT_EDGES 4
using namespace verona::cpp;

namespace pointer_churn_with_concurrency
{
  /**
   * This test creates a directed graph of nodes in a chain from the root (id =
   * 0) node. Nodes are able to have a set number of outgoing edges to other,
   * non-root nodes. We mutate the graph by randomly adding, updating, or
   * attempting to remove outgoing edges to other nodes. This will result in
   * many changes to referenced nodes which can result in nodes/cycles that are
   * disconnected from the root - these may be garbage collected (if the GC
   * supports this) at certain intervals or immediately (depending on GC type).
   * The graph can prematurely collapse to just the root before being able to
   * mutate the given number of times, at which point we close and release the
   * region and repeat the process with a new region until we have mutated the
   * given number of times.
   **/

    // Graph node structure
  struct GraphNode : public V<GraphNode>
  {
    GraphNode* edges[MAX_OUT_EDGES] = {nullptr};
    size_t id;

    // Trace function for the trace GC
    void trace(ObjectStack& st) const
    {
      for (auto edge : edges)
      {
        if (edge != nullptr)
          st.push(edge);
      }
    }
  };

  struct Region : public V<Region>
  // This is a single region
  // This holds just the bridge node of that region
  {
    GraphNode* bridge;

    void trace(ObjectStack& st) const
    {
      if (bridge != nullptr)
        st.push(bridge);
    }
  };

  class RegionCown
  {
  public:
    RegionCown(Region* region, size_t region_id) : region(region), region_id(region_id) {}
    Region* region;
    size_t region_id;

    ~RegionCown()
    {
      region_release(region);
    }
  };

  /**
   * Helper function to find all nodes reachable from root via DFS.
   * This helps to only track nodes that are still alive after GC.
   */
  static void
  find_reachable_nodes(GraphNode* node, std::vector<GraphNode*>& reachable)
  {
    if (node == nullptr)
      return;

    // Check if already visited
    for (GraphNode* seen : reachable)
    {
      if (seen == node)
        return; // Already visited
    }

    reachable.push_back(node);

    // Recursively visit all edges
    for (auto & edge : node->edges)
    {
      if (edge != nullptr)
      {
        find_reachable_nodes(edge, reachable);
      }
    }
  }

  template<RegionType RT>
  cown_ptr<RegionCown> create_cown(size_t cown_num)
  {
      Region* graphRegion = new (RT) Region();
      auto cown = make_cown<RegionCown>(graphRegion, cown_num);
      return cown;
  }

  template<RegionType RT>
  void create_chain(size_t num_nodes, size_t inputSeed, size_t cown_num, Region*& graphRegion)
  {
      std::cout << "\n" << std::string(60, '=') << "\n";
      std::cout << "  REGION #" << cown_num << "\n";
      std::cout << std::string(60, '=') << "\n\n";

      {
        UsingRegion ur(graphRegion);
        auto* root = new GraphNode;
        graphRegion->bridge = root;
        root->id = 0;
        std::vector<GraphNode*> reachableNodes;
        GraphNode* prevNode = root;
        for (size_t i = 0; i < num_nodes - 1; i++)
        {
          GraphNode* node = new GraphNode;
          node->id = i + 1;
          prevNode->edges[0] = node;
          prevNode = node;
        }      // Essentially creates a chain of nodes, not so much a graph
      }
  }

  template<RegionType RT>
  size_t perform_mutations(size_t num_mutations, std::mt19937 rng, size_t cown_num, Region* graphRegion)
  {
      UsingRegion ur(graphRegion);
      auto root = graphRegion->bridge;
      std::uniform_int_distribution<size_t> rndOutEdgeInd(
        0, MAX_OUT_EDGES - 1);

      std::vector<GraphNode*> reachableNodes;
      while (num_mutations > 0)
      {
        reachableNodes.clear();
        // Find all reachable nodes first - this is our "live" set
        find_reachable_nodes(root, reachableNodes);

        if (reachableNodes.size() == 1)
        {
          std::cout << "\n    Only root node remaining, closing and "
                       "releasing region...\n";
          break;
        }

        std::uniform_int_distribution<size_t> rndSrcNodeInd(
          0, reachableNodes.size() - 1);

        /** MUST NOT include index 0 as it may lead to root node being
         *selected as destination, which would cause its ref count to be
         *changed. It appears that the root's ref count might be managed
         *internally and so manually modifying it like this may cause an error
         *- it's best to leave it from being referenced.
         **/
        std::uniform_int_distribution<size_t> rndDstNodeInd(
          1, reachableNodes.size() - 1);
        std::uniform_int_distribution<size_t> rndOutEdgeInd(
          0, MAX_OUT_EDGES - 1);
        GraphNode* edgeSrcNode = reachableNodes[rndSrcNodeInd(rng)];
        GraphNode* newEdgeDstNode = reachableNodes[rndDstNodeInd(rng)];

        size_t edgeIdx = rndOutEdgeInd(rng);
        GraphNode* oldEdgeDstNode = edgeSrcNode->edges[edgeIdx];

        if (rng() % 2 == 0) // Add/update edge
        {
          edgeSrcNode->edges[edgeIdx] = newEdgeDstNode;
          if constexpr (RT == RegionType::Rc) // Ref count adjustment for RC
          {
            incref(newEdgeDstNode);
          }
          if (oldEdgeDstNode != nullptr)
          {
            // Save ID before decref (which may deallocate the node)
            size_t oldId = oldEdgeDstNode->id;
            if constexpr (RT == RegionType::Rc)
            {
              decref(oldEdgeDstNode); // Ref count adjustment for RC
            }
            std::cout << "  [UPDATE] Node " << edgeSrcNode->id << ": "
                      << oldId << " → " << newEdgeDstNode->id << "\n";
          }
          else
          {
            std::cout << "  [ADD]    Node " << edgeSrcNode->id << " → Node "
                      << newEdgeDstNode->id << "\n";
          }
        }
        else // Remove edge
        {
          if (oldEdgeDstNode == nullptr)
          {
            std::cout << "  [SKIP]   No edge to remove from edge index "
                      << edgeIdx << " of Node " << edgeSrcNode->id << "\n";
          }
          else
          {
            // Save ID before decref (which may deallocate the node)
            size_t oldId = oldEdgeDstNode->id;
            edgeSrcNode->edges[edgeIdx] = nullptr;
            if constexpr (RT == RegionType::Rc)
            {
              decref(oldEdgeDstNode); // Ref count adjustment for RC
            }
            std::cout << "  [REMOVE] Node " << edgeSrcNode->id << " ╳→ Node "
                      << oldId << "\n";
          }
        }
        num_mutations--;
      }
      return num_mutations;
  }

  template<RegionType RT>
  void
  test_pointer_churn(size_t num_nodes, size_t mutation_per_iter, size_t inputSeed, size_t num_regions, size_t iterations)
  {
    std::vector<cown_ptr<RegionCown>> cowns;
    for (size_t cown_num = 0; cown_num < num_regions; cown_num++)
    {
      cown_ptr<RegionCown> cown = create_cown<RT>(cown_num);
      cowns.push_back(cown);
    }

    for (auto& cown : cowns)
    {
      when(cown)
        << [=](auto c) {
            create_chain<RT>(num_nodes, inputSeed, c->region_id, c->region);  // Add <RT>
        };
    }
    
    const size_t seed = inputSeed + static_cast<size_t>(RT) * 10000;
    std::mt19937 rng(seed);
    
    for (size_t i = 0; i < iterations; i++)
    {
      for (size_t cown_num = 0; cown_num < num_regions; cown_num++)
      {
        when(cowns[cown_num])
          << [=](auto c) {
            perform_mutations<RT>(mutation_per_iter, rng, cown_num, c->region);  // Add <RT>
        };
      }
    }
  }

  void run_test(
      size_t num_nodes = 500,
      size_t mutation_per_iter = 1000,
      size_t inputSeed = 0,
      size_t num_regions = 10,
      size_t iterations = 50)
  {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  POINTER CHURN WITH CONCURRENCY\n";
    std::cout << std::string(60, '=') << "\n";
    test_pointer_churn<RegionType::Rc>(num_nodes, mutation_per_iter, inputSeed, num_regions, iterations);
  }
}
