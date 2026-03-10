// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

/**
 * This test creates multiple regions, each containing a fully connected graph
 * of nodes.
 *
 * Starting from the region's bridge node, we randomly select and traverse an
 * outgoing edge, then remove that edge. This continues until the current node
 * has no outgoing edges.
 *
 * This tests GC behavior as nodes become unreachable during edge removal.
 **/

#pragma once

#include "cpp/cown.h"
#include "cpp/when.h"
#include "region/region_api.h"
#include "region/region_base.h"

#include <algorithm>
#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <random>
#include <vector>
#include <verona.h>

using namespace verona::cpp;

namespace arbitrary_nodes
{

  template<typename T>
  const T& random_element(const std::vector<T>& v)
  {
    if (v.empty())
    {
      throw std::out_of_range("random_element: empty vector");
    }

    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, v.size() - 1);
    return v[dist(gen)];
  }

  inline int num_nodes = 0;
  class Node : public V<Node>
  {
  public:
    std::vector<Node*> neighbours;

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

    // SemiSpace GC: forward all neighbour pointers in-place.
    // std::vector has no self-referential pointers, so after memcpy
    // the internal buffer pointer is a valid heap address that can
    // be safely updated in-place (no reallocation needed).
    void relocate(Object* (*fwd)(Object*))
    {
      for (auto& node : neighbours)
      {
        if (node != nullptr)
          node = (Node*)fwd(node);
      }
    }
  };

  // Cown that owns a region. The Node* root is the iso root of the region
  // (created via `new (rt) Node()`). Releasing the region frees all objects
  // in it.
  class RegionCown
  {
  public:
    RegionCown(Node* root) : root(root) {}
    Node* root;

    ~RegionCown()
    {
      region_release(root);
    }
  };

  std::vector<size_t> random_regions(size_t regions, size_t size)
  {
    if (regions > size)
      throw std::invalid_argument("regions must be <= size, received " + std::to_string(regions) + " and " + std::to_string(size));

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

  template<RegionType rt>
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

        u->neighbours.push_back(v);
        if constexpr (rt == RegionType::Rc)
        {
          incref(v);
        }
      }
    }
  }

  template<RegionType rt>
  std::vector<cown_ptr<RegionCown>> createGraph(int size, int regions)
  {
    std::vector<size_t> region_sizes = random_regions(regions, size);
    std::cout << "Region sizes: ";
    for (size_t s : region_sizes)
    {
      std::cout << s << " ";
    }
    std::cout << std::endl;

    std::vector<cown_ptr<RegionCown>> graphRegions;
    for (size_t region_size : region_sizes)
    {
      // The first Node is the iso root (the "bridge" node).
      // `new (rt) Node()` creates a region with this Node as the entry point.
      Node* root = new (rt) Node();
      auto ptr = make_cown<RegionCown>(root);
      {
        UsingRegion ur(root);

        // local vector of nodes in this region
        std::vector<Node*> all_nodes;
        all_nodes.push_back(root);

        for (size_t i = 0; i != region_size - 1; i++)
        {
          Node* node = new Node();
          all_nodes.push_back(node);
        }

        fully_connect<rt>(all_nodes);

        // For RC: non-root nodes were created with rc=1, but that initial
        // count doesn't correspond to any in-region pointer (only the local
        // all_nodes vector held them). fully_connect added incref for every
        // neighbour edge, so the initial rc=1 is surplus. Decref once to
        // compensate. The root (iso entry point) is fine as-is.
        if constexpr (rt == RegionType::Rc)
        {
          for (size_t i = 1; i < all_nodes.size(); i++)
          {
            decref(all_nodes[i]);
          }
        }
      }

      graphRegions.push_back(ptr);
    }
    std::cout << "Finished creating graph regions" << std::endl;
    return graphRegions;
  }

  template<RegionType rt>
  bool removeArc(Node* src, Node* dst)
  {
    if (!src || !dst)
      return false;

    auto it = std::find(src->neighbours.begin(), src->neighbours.end(), dst);
    if (it != src->neighbours.end())
    {
      std::swap(*it, src->neighbours.back());
      src->neighbours.pop_back();
      // For RC: do NOT decref here. If decref causes rc to hit 0, dst is
      // immediately freed by dealloc_object — but traverse() returns dst as
      // the next cur, causing use-after-free. Decrefs are deferred and
      // applied after the traversal loop completes.
      return true;
    }
    return false;
  }

  template<RegionType rt>
  Node* traverse(Node* cur, Node* dst)
  {
    if (removeArc<rt>(cur, dst))
    {
      std::cout << "Traversed from " << cur << " to " << dst << std::endl;
      return dst;
    }
    return nullptr;
  }

  template<RegionType rt>
  void traverse_region(Node* root)
  {
    UsingRegion ur(root);
    std::cout << "Traversing region" << std::endl;
    Node* cur = root;

    // For RC: collect removed edge targets so we can decref them after
    // the walk is done, avoiding use-after-free from immediate deallocation.
    std::vector<Node*> removed_targets;

    while (cur && cur->neighbours.size() > 0)
    {
      std::cout << "Current node: " << cur << " has " << cur->neighbours.size()
                << " outgoing edges" << std::endl;
      Node* dst = random_element(cur->neighbours);
      if constexpr (rt == RegionType::Rc)
      {
        removed_targets.push_back(dst);
      }
      cur = traverse<rt>(cur, dst);
    }

    // Apply deferred decrefs now that traversal is complete.
    if constexpr (rt == RegionType::Rc)
    {
      for (Node* target : removed_targets)
      {
        decref(target);
      }
    }

    if constexpr (rt != RegionType::Arena)
    {
      region_collect();
    }
  }

  template<RegionType rt>
  void run_test(int size, int regions)
  {
    {
      std::vector<cown_ptr<RegionCown>> graphRegions =
        createGraph<rt>(size, regions);

      for (cown_ptr<RegionCown> regionCown : graphRegions)
      {
        when(regionCown)
          << [](auto c) { traverse_region<rt>(c->root); };
      }
    }
  }
} // namespace arbitrary_nodes
