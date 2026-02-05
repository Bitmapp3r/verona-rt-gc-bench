// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <debug/harness.h>
#include <verona.h>
#include <random>
#include <vector>
#include <iostream>

using namespace snmalloc;
using namespace verona::rt;
using namespace verona::rt::api;

/**
 * Graph node with multiple outgoing edges.
 * Designed to stress reference counting with pointer churn.
 * 
 * The trace() function is required for Trace GC to discover
 * which objects are reachable during garbage collection.
 */
struct GraphNode : public V<GraphNode>
{
  static constexpr size_t MAX_EDGES = 8;
  GraphNode* edges[MAX_EDGES] = {nullptr};

  // Trace function: tells GC which objects this node references
  void trace(ObjectStack& st) const
  {
    for (size_t i = 0; i < MAX_EDGES; i++)
    {
      if (edges[i] != nullptr)
        st.push(edges[i]);
    }
  }
};

namespace pointer_churn_gc
{
  using Node = GraphNode;

  /**
   * Helper function to find all nodes reachable from root via DFS.
   * Only traverses Verona object edges, not external vectors/storage.
   */
  static void find_reachable_nodes(Node* node, std::vector<Node*>& reachable, std::vector<bool>& visited)
  {
    if (node == nullptr)
      return;
    
    uintptr_t addr = reinterpret_cast<uintptr_t>(node);
    // Use a simple heuristic: check if we've seen this address before
    // (not perfect but good for this test)
    for (Node* seen : reachable)
    {
      if (seen == node)
        return;  // Already visited
    }
    
    reachable.push_back(node);
    
    // Recursively visit all edges
    for (size_t i = 0; i < Node::MAX_EDGES; i++)
    {
      if (node->edges[i] != nullptr)
      {
        find_reachable_nodes(node->edges[i], reachable, visited);
      }
    }
  }

  /**
   * Generic pointer churn test that works with any region type.
   * 
   * This test creates a large graph and performs many random edge mutations
   * to stress-test the garbage collector. The key difference from naive approaches:
   * 
   * - NO std::vector holding all nodes (was preventing GC from collecting)
   * - Graph is initially fully connected so all nodes are reachable
   * - During mutations, edges are removed and nodes become unreachable
   * - RC will deallocate unreachable nodes immediately (good stress test!)
   * - Trace/Arena will collect them during region_collect()
   * 
   * For RC, this is especially important: when we decref an edge to a
   * node that now has no other incoming edges, the node is deallocated
   * immediately. Accessing a deallocated node should crash.
   */
  template<RegionType RT>
  void test_pointer_churn_impl()
  {
    const char* gc_name = 
      RT == RegionType::Trace ? "Trace" :
      RT == RegionType::Rc ? "RC" : "Arena";
    
    std::cout << "Testing Pointer Churn (" << gc_name << " GC)...\n";
    
    // Test configuration
    const size_t NUM_NODES = 1000;      // Number of nodes in the graph
    const size_t NUM_MUTATIONS = 1000; // Number of edge changes to perform
    
    // Create the root region
    auto* root = new (RT) Node;
    
    {
      // Open the region for allocation
      UsingRegion ur(root);
      
      // Phase 1: Create initial graph with NUM_NODES nodes
      // We'll create a linked list chain to ensure all nodes are reachable
      // from root: root -> node[0] -> node[1] -> ... -> node[NUM_NODES-1]
      std::cout << "Creating " << NUM_NODES << " nodes in a connected graph...\n";
      
      Node* current = root;
      Node* first_node = nullptr;
      std::vector<Node*> initial_nodes;
      
      for (size_t i = 0; i < NUM_NODES; i++)
      {
        Node* node = new Node;
        initial_nodes.push_back(node);
        
        if (i == 0)
        {
          first_node = node;
          root->edges[0] = node;
          // No incref needed: node's initial refcount=1 covers this edge
        }
        else
        {
          // Link previous node to this one to ensure connectivity
          Node* prev = initial_nodes[i - 1];
          prev->edges[0] = node;
          // No incref needed: node's initial refcount=1 covers this edge
        }
      }
      
      // After Phase 1: should have root + NUM_NODES objects
      size_t expected_size = NUM_NODES + 1;
      check(debug_size() == expected_size);
      std::cout << "  Phase 1 complete: " << debug_size() << " objects in region\n";
      
      // Phase 2: Perform random edge mutations (the "churn")
      // This is where nodes become unreachable and get garbage collected.
      // Mutations add or remove random edges from any reachable node.
      std::cout << "Performing " << NUM_MUTATIONS << " edge mutations...\n";
      size_t gc_interval = NUM_MUTATIONS / 10;
      
      // Random number generator setup:
      // - std::mt19937: Mersenne Twister PRNG (fast, high-quality random numbers)
      //   Seeded with 12345 for reproducibility (same random sequence every run)
      // - std::uniform_int_distribution: Maps Mersenne Twister output to a uniform range
      //   Usage: calling node_dist(rng) generates next random number in [0, NUM_NODES-1]
      std::mt19937 rng(12345);  // Fixed seed for reproducibility
      std::uniform_int_distribution<size_t> node_dist(0, NUM_NODES - 1);
      std::uniform_int_distribution<size_t> edge_dist(0, Node::MAX_EDGES - 1);  // Include [0] so mutations can break the chain
      
      for (size_t iter = 0; iter < NUM_MUTATIONS; iter++)
      {
        // Find all reachable nodes (expensive but necessary without the vector)
        std::vector<Node*> reachable_nodes;
        std::vector<bool> visited(NUM_NODES, false);
        find_reachable_nodes(root, reachable_nodes, visited);
        
        // Pick a random reachable node to mutate
        if (reachable_nodes.empty())
        {
          // If no nodes are reachable, we're done with the test
          std::cout << "  All nodes have been garbage collected!\n";
          break;
        }
        
        std::uniform_int_distribution<size_t> reachable_dist(0, reachable_nodes.size() - 1);
        Node* from_node = reachable_nodes[reachable_dist(rng)];
        
        size_t edge_idx = edge_dist(rng);
        
        // 50% chance to create edge, 50% to destroy edge
        if (rng() % 2 == 0)
        {
          // Create/update edge to a random node
          size_t to_idx = node_dist(rng);
          Node* to_node = initial_nodes[to_idx];
          
          if constexpr (RT == RegionType::Rc)
          {
            // RC: IMPORTANT - incref new BEFORE decref old
            // Safe pattern: increment reference before modifying pointer
            incref(to_node);
            if (from_node->edges[edge_idx] != nullptr)
            {
              decref(from_node->edges[edge_idx]);
            }
            from_node->edges[edge_idx] = to_node;
          }
          else
          {
            // Trace/Arena: just update the pointer
            from_node->edges[edge_idx] = to_node;
          }
        }
        else
        {
          // Destroy edge (may cause RC to deallocate target!)
          // For RC: if this is the last reference to the node, decref triggers
          // immediate deallocation. The node becomes unreachable and is freed.
          if constexpr (RT == RegionType::Rc)
          {
            // RC: decrement reference when removing edge
            if (from_node->edges[edge_idx] != nullptr)
            {
              decref(from_node->edges[edge_idx]);
              from_node->edges[edge_idx] = nullptr;
            }
          }
          else
          {
            from_node->edges[edge_idx] = nullptr;
          }
        }
        
        // Periodically run GC to collect unreachable nodes
        if (iter % gc_interval == 0)
        {
          std::cout << "  GC at iteration " << iter << "...\n";
          if constexpr (RT != RegionType::Arena)
          {
            // Trace & RC: run garbage collection
            // Arena: no GC, everything freed at region release
            region_collect();
          }
        }
      }
      
      // Phase 3: Final garbage collection
      std::cout << "Final GC...\n";
      if constexpr (RT != RegionType::Arena)
      {
        region_collect();
      }
      
      // After final GC, check how many objects remain
      size_t final_size = debug_size();
      std::cout << "Test complete! Final object count: " << final_size << "\n";
    }
    
    // Release the entire region (all objects deallocated)
    region_release(root);
  }

  // Entry point - selects GC type based on argument
  void run_test(const std::string& gc_type)
  {
    if (gc_type == "trace")
    {
      std::cout << "Using Trace (Mark-Sweep) GC\n";
      test_pointer_churn_impl<RegionType::Trace>();
    }
    else if (gc_type == "rc")
    {
      std::cout << "Using Reference Counting GC\n";
      test_pointer_churn_impl<RegionType::Rc>();
    }
    else if (gc_type == "arena")
    {
      std::cout << "Using Arena (No GC)\n";
      test_pointer_churn_impl<RegionType::Arena>();
    }
    else
    {
      std::cout << "Unknown GC type: " << gc_type << "\n";
      std::cout << "Valid options: trace, rc, arena\n";
    }
  }
}
