// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "func/ext_ref/ext_ref_basic.h"
#include "region/region_api.h"

#include <algorithm>
#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>
#include <verona.h>

namespace arbitrary_nodes
{

  template<typename T>
  const T& random_element(const std::unordered_set<T>& s)
  {
    if (s.empty())
    {
      throw std::out_of_range("random_element: empty set");
    }

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, s.size() - 1);

    auto it = s.begin();
    std::advance(it, dist(gen));
    return *it;
  }

  inline int num_nodes = 0;
  class Node : public V<Node>
  {
  public:
    std::unordered_set<Node*> neighbours;
    int id;

    Node()
    {
      num_nodes++;
    }
    void trace(ObjectStack& st) const
    {
      for (Node* node : neighbours)
      {
        if (node != nullptr)
          st.push(node);
      }
    }
  };

  struct GraphRegion : public V<GraphRegion>
  // This is a single region
  // This holds just the bridge node of that region
  {
    Node* bridge;

    void trace(ObjectStack& st) const
    {
      if (bridge != nullptr)
        st.push(bridge);
    }
  };

  struct RegionRoot : public V<RegionRoot>
  // This is the root of all the other regions
  // All the other regions are GraphRegion
  {
    std::vector<GraphRegion*> graphRegions;
    void trace(ObjectStack& st) const
    {
      for (GraphRegion* graphRegion : graphRegions)
      {
        if (graphRegion != nullptr)
          st.push(graphRegion);
      }
    }
  };

  inline RegionRoot* root;

  std::vector<size_t> random_regions(size_t regions, size_t size)
  {
    if (regions > size)
      throw std::invalid_argument("regions must be <= size");

    std::random_device rd;
    std::mt19937 gen(rd());

    // We choose cuts in [1, size-1], not including 0 or size
    std::uniform_int_distribution<size_t> dist(1, size - 1);

    std::vector<size_t> cuts(regions + 1);
    cuts[0] = 0;
    cuts[regions] = size;

    for (size_t i = 1; i < regions; ++i)
    {
      cuts[i] = dist(gen);
    }

    std::sort(cuts.begin(), cuts.end());

    std::vector<size_t> result(regions);
    for (size_t i = 0; i < regions; ++i)
    {
      result[i] = cuts[i + 1] - cuts[i]; // always >= 1
    }

    return result;
  }

  void kill_node(Node* src, Node* dst)
  {
    if (src->neighbours.find(dst) != src->neighbours.end())
      return;
    src->neighbours.erase(dst);
  }

  inline void fully_connect(const std::vector<Node*>& nodes)
  // THIS WILL NOT WORK IF YOU HAVE AN EVEN NUMBER OF NODES
  // THIS WILL LEAD TO A EUCLIDEAN GRAPH
  // YOU WILL NOT GET STUCK ANYWHERE AND WILL ALWAYS BE ABLE
  // TO RETURN BACK TO THE ROOT NODE
  // MODIFY LATER TO ONLY CONNECT A SMALL PROPORTION OF NODES
  // TODO
  {
    for (Node* u : nodes)
    {
      if (u == nullptr)
        continue;

      for (Node* v : nodes)
      {
        if (v == nullptr)
          continue;
        if (u == v)
          continue;

        u->neighbours.insert(v);
      }
    }
  }

  void createGraph(int size, int regions)
  {
    root = new (RegionType::Trace) RegionRoot;
    std::vector<size_t> region_sizes = random_regions(regions, size);
    std::cout << "Region sizes: ";
    for (size_t size : region_sizes)
    {
      std::cout << size << " ";
    }
    std::cout << std::endl;
    std::vector<GraphRegion*> otraces = std::vector<GraphRegion*>(regions);
    for (size_t region_size : region_sizes)
    {
      if (region_size == 0)
        continue;
      GraphRegion* graphRegion = new (RegionType::Trace) GraphRegion();
      {
        UsingRegion ur(graphRegion);
        Node* bridge = new Node();
        graphRegion->bridge = bridge;

        // local vector of nodes in this region
        std::vector<Node*> all_nodes;
        all_nodes.push_back(bridge);

        for (size_t i = 0; i != region_size - 1; i++)
        {
          Node* node = new Node();
          all_nodes.push_back(node);
        }

        fully_connect(all_nodes);
      }

      root->graphRegions.push_back(graphRegion);
    }
  }

  bool removeArc(Node* src, Node* dst)
  {
    if (!src || !dst)
      return false;

    if (src->neighbours.find(dst) != src->neighbours.end())
    {
      src->neighbours.erase(dst);
      return true;
    }
    return false;
  }

  Node* traverse(Node* cur, Node* dst)
  {
    if (removeArc(cur, dst))
    {
      std::cout << "Traversed from " << cur << " to " << dst << std::endl;
      return dst;
    }
    return nullptr;
  }

  void traverse_region(GraphRegion* graphRegion)
  {
    UsingRegion ur(graphRegion);
    std::cout << "Traversing region" << std::endl;
    Node* cur = graphRegion->bridge;

    while (cur && cur->neighbours.size() > 0)
    {
      std::cout << "Current node: " << cur << " has " << cur->neighbours.size()
                << " outgoing edges" << std::endl;
      Node* dst = random_element(cur->neighbours);
      cur = traverse(cur, dst);
    }

    int debug_size = verona::rt::api::debug_size();
    region_collect();
    int new_debug_size = verona::rt::api::debug_size();
    std::cout << "Debug size before: " << debug_size << std::endl;
    std::cout << "Debug size after: " << new_debug_size << std::endl;
  }

  void run_test(int size, int regions)
  {
    createGraph(size, regions);
    std::cout << "got here";

    for (GraphRegion* graphRegion : root->graphRegions)
    {
      traverse_region(graphRegion);
    }
  }

  inline void run_test()
  {
    // Placeholder test function
    std::cout << "Running arbitrary_nodes test...\n";
  }

} // namespace arbitrary_nodes
