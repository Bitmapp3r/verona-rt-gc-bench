// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "cpp/cown.h"
#include "cpp/when.h"
#include "func/ext_ref/ext_ref_basic.h"
#include "region/region_api.h"
#include "region/region_base.h"

#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <random>
#include <unordered_set>
#include <vector>
#include <verona.h>

using namespace verona::cpp;

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

  class GraphRegionCown
  {
  public:
    GraphRegionCown(GraphRegion* graphRegion) : graphRegion(graphRegion) {}
    GraphRegion* graphRegion;

    ~GraphRegionCown()
    {
      region_release(graphRegion);
    }
  };

  std::vector<size_t> random_regions(size_t regions, size_t size)
  {
    if (regions > size)
      throw std::invalid_argument("regions must be <= size");

    std::random_device rd;
    std::mt19937 gen(rd());

    // Each region will have at least one node
    // We will randomly distribute the remaining nodes among the regions
    std::vector<size_t> result(regions, 1);

    std::uniform_int_distribution<size_t> dist(0, regions - 1);

    for (size_t i = 0; i < (size - regions); ++i)
    {
      size_t idx = dist(gen);
      result[idx]++;
    }

    return result;
  }

  inline void fully_connect(const std::vector<Node*>& nodes)
  // If you have an even number of nodes, you will have a
  // Euclidean graph. Euclidean graphs will return to the
  // root after traversal, so every other node will be garbage
  // after traversing and deleting the arcs (think of chinese postman problem).
  // TODO: Modify so that it partially connects the nodes,
  // so that you get clusters of nodes that will be disconnected
  // after traversal.
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
  
  template <RegionType rt>
  std::vector<cown_ptr<GraphRegionCown>> createGraph(int size, int regions)
  {
    std::vector<size_t> region_sizes = random_regions(regions, size);
    std::cout << "Region sizes: ";
    for (size_t size : region_sizes)
    {
      std::cout << size << " ";
    }
    std::cout << std::endl;

    std::vector<cown_ptr<GraphRegionCown>> graphRegions;
    for (size_t region_size : region_sizes)
    {
      GraphRegion* graphRegion = new (rt) GraphRegion();
      auto ptr = make_cown<GraphRegionCown>(graphRegion);
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

      graphRegions.push_back(ptr);
    }
    std::cout << "Finished creating graph regions" << std::endl;
    return graphRegions;
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
  }

  template <RegionType rt>
  void run_test(int size, int regions)
  {
    {
      std::vector<cown_ptr<GraphRegionCown>> graphRegions =
        createGraph<rt>(size, regions);

      for (cown_ptr<GraphRegionCown> graphRegionCown : graphRegions)
      {
        when(graphRegionCown)
          << [&](auto c) { traverse_region(c->graphRegion); };
      }
    }
  }
} // namespace arbitrary_nodes
