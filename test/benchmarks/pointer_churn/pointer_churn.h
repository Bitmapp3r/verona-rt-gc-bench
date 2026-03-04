// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <debug/harness.h>
#include <iostream>
#include <random>
#include <vector>
#include <verona.h>

// Maximum number of outgoing edges per node
#define MAX_OUT_EDGES 4

namespace pointer_churn
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
      for (size_t i = 0; i < MAX_OUT_EDGES; i++)
      {
        if (edges[i] != nullptr)
          st.push(edges[i]);
      }
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
    for (size_t i = 0; i < MAX_OUT_EDGES; i++)
    {
      if (node->edges[i] != nullptr)
      {
        find_reachable_nodes(node->edges[i], reachable);
      }
    }
  }
  template<RegionType RT>
  void
  test_pointer_churn(size_t num_nodes, size_t num_mutations, size_t inputSeed)
  {
    // Number of initial nodes in the graph (including root)
    const size_t NUM_NODES = num_nodes;
    // Number of mutations to perform on the graph
    const size_t NUM_MUTATIONS = num_mutations;
    size_t NUM_MUTATIONS_REM = NUM_MUTATIONS;
    const size_t GC_INTERVAL = NUM_MUTATIONS / 100;

    // Use different seed for each GC type
    const size_t seed = inputSeed + static_cast<size_t>(RT) * 10000;
    std::mt19937 rng(seed);
    size_t region_number = 1; // Initial region number/ID

    while (NUM_MUTATIONS_REM > 0)
    {
      std::cout << "\n" << std::string(60, '=') << "\n";
      std::cout << "  REGION #" << region_number++
                << " | Mutations Remaining: " << NUM_MUTATIONS_REM << "\n";
      std::cout << std::string(60, '=') << "\n\n";
      // Open new region and setup the graph
      auto* root = new (RT) GraphNode;
      root->id = 0;
      std::vector<GraphNode*> reachableNodes;
      {
        UsingRegion ur(root);
        GraphNode* prevNode = root;
        for (size_t i = 0; i < NUM_NODES - 1; i++)
        {
          GraphNode* node = new GraphNode;
          node->id = i + 1;
          prevNode->edges[0] = node;
          prevNode = node;
        }
        // Sanity check that all nodes are allocated
        check(debug_size() == NUM_NODES);

        // Random distribution for selecting an edge index to mutate
        std::uniform_int_distribution<size_t> rndOutEdgeInd(
          0, MAX_OUT_EDGES - 1);

        while (NUM_MUTATIONS_REM > 0)
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

          if (NUM_MUTATIONS_REM % GC_INTERVAL == 0)
          {
            if constexpr (RT != RegionType::Arena)
            {
              region_collect(); // Collect garbage for non-arena regions
            }
            reachableNodes.clear();
            find_reachable_nodes(root, reachableNodes);
            std::cout << "  " << std::string(56, '-') << "\n";
            std::cout << "  [REGION STATS] Allocated: " << debug_size()
                      << " | Reachable: " << reachableNodes.size() << "\n";
            std::cout << "  " << std::string(56, '-') << "\n\n";
          }

          NUM_MUTATIONS_REM--;
        }
        // Final region stats once we finished mutating or graph has collapsed
        if constexpr (RT != RegionType::Arena)
        {
          region_collect();
        }
        reachableNodes.clear();
        find_reachable_nodes(root, reachableNodes);
        std::cout << "\n  " << std::string(56, '-') << "\n";
        std::cout << "  [REGION FINAL] Allocated: " << debug_size()
                  << " | Reachable: " << reachableNodes.size() << "\n";
        std::cout << "  " << std::string(56, '-') << "\n\n\n";
      }
      // Release region and repeat if we still have mutations to perform
      region_release(root);
    }
  }

  void run_test(
    const std::string& gc_type,
    size_t num_nodes,
    size_t num_mutations,
    size_t inputSeed)
  {
    if (gc_type == "trace")
    {
      std::cout << "\n╔═══════════════════════════════════════╗\n";
      std::cout << "║  Pointer Churn Test: Trace GC         ║\n";
      std::cout << "╚═══════════════════════════════════════╝\n";
      test_pointer_churn<RegionType::Trace>(
        num_nodes, num_mutations, inputSeed);
    }
    else if (gc_type == "arena")
    {
      std::cout << "\n╔═══════════════════════════════════════╗\n";
      std::cout << "║  Pointer Churn Test: Arena            ║\n";
      std::cout << "╚═══════════════════════════════════════╝\n";
      test_pointer_churn<RegionType::Arena>(
        num_nodes, num_mutations, inputSeed);
    }
    else
    {
      std::cout << "\n╔═══════════════════════════════════════╗\n";
      std::cout << "║  Pointer Churn Test: RC GC            ║\n";
      std::cout << "╚═══════════════════════════════════════╝\n";
      test_pointer_churn<RegionType::Rc>(num_nodes, num_mutations, inputSeed);
    }
  }
}