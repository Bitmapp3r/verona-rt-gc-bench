// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

/**
 * This test creates multiple regions with partially connected graphs (70%
 * connectivity) rather than fully connected graphs.
 *
 * During the churn phase, we traverse the graph to build a working set of
 * nodes, create new nodes, and randomly add edges between nodes in the working
 * set. This creates pointer churn where edges are added, updated, and removed
 * over time, unlike the arbitrary_nodes test which only removes edges.
 *
 * This tests GC behavior with ongoing mutation and periodic garbage collection,
 * where nodes can become unreachable as the graph structure changes.
 **/

#pragma once

#include "cpp/cown.h"
#include "cpp/when.h"
#include "func/ext_ref/ext_ref_basic.h"
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

namespace partially_connected
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
    int id;
    Node()
    {
      num_nodes++;
      id = num_nodes;
    }

    ~Node()
    {
      std::cout << "node " << id << " died\n";
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

  // Sentinel iso root that anchors the graph to the region.
  // The sentinel itself does NOT participate in the graph algorithm —
  // it simply holds a pointer (`bridge`) to the first actual graph node.
  // This keeps the iso root's internally-managed refcount safe from
  // manual incref/decref during graph mutation.
  class RegionRoot : public V<RegionRoot>
  {
  public:
    Node* bridge = nullptr;

    void trace(ObjectStack& st) const
    {
      if (bridge)
        st.push(bridge);
    }

    void relocate(Object* (*fwd)(Object*))
    {
      if (bridge)
        bridge = (Node*)fwd(bridge);
    }
  };

  // Cown that owns a region. The RegionRoot* root is the iso root of the
  // region (created via `new (rt) RegionRoot()`). Releasing the region
  // frees all objects.
  class RegionCown
  {
  public:
    RegionCown(RegionRoot* root) : root(root) {}
    RegionRoot* root;

    ~RegionCown()
    {
      region_release(root);
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

  std::pair<size_t, size_t> random_pair(int max)
  {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, max - 1);
    if (max == 1)
    {
      return std::make_pair(0, 0);
    }
    size_t first = dist(gen);
    size_t second;
    do
    {
      second = dist(gen);
    } while (first == second);
    return std::make_pair(first, second);
  }

  template<RegionType rt>
  inline void fully_connect(const std::vector<Node*>& nodes)
  // If you have an even number of nodes, you will have a
  // Euclidean graph. Euclidean graphs will return to the
  // root after traversal, so every other node will be garbage
  // after traversing and deleting the arcs (think of chinese postman problem).
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
  void partially_connect(const std::vector<Node*>& nodes)
  {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> dist(0.0, 1.0);
    float connectedness = 0.7f;
    for (Node* u : nodes)
    {
      if (!u)
        continue;

      for (Node* v : nodes)
      {
        if (!v || u == v)
          continue;

        if (dist(gen) < connectedness)
        {
          u->neighbours.push_back(v);
          if constexpr (rt == RegionType::Rc)
          {
            incref(v);
          }
        }
      }
    }
  }

  template<RegionType rt>
  std::vector<cown_ptr<RegionCown>>
  createGraph(int size, int regions, bool partial = false)
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
      // RegionRoot is the iso root — a sentinel that holds a pointer
      // to the first actual graph node (bridge). The sentinel does NOT
      // participate in the graph algorithm.
      RegionRoot* sentinel = new (rt) RegionRoot();
      auto ptr = make_cown<RegionCown>(sentinel);
      {
        UsingRegion ur(sentinel);

        // Create all graph nodes inside the region.
        std::vector<Node*> all_nodes;
        for (size_t i = 0; i < region_size; i++)
        {
          Node* node = new Node();
          all_nodes.push_back(node);
        }

        // The first graph node is the bridge — anchored from the sentinel.
        sentinel->bridge = all_nodes[0];
        if constexpr (rt == RegionType::Rc)
          incref(all_nodes[0]); // sentinel->bridge reference

        if (partial)
          partially_connect<rt>(all_nodes);
        else
          fully_connect<rt>(all_nodes);

        // For RC: every node was created with rc=1 (surplus — doesn't
        // correspond to any in-region pointer). Connection functions added
        // incref for every neighbour edge, and bridge got an extra incref
        // above. Decref once per node to compensate the surplus.
        if constexpr (rt == RegionType::Rc)
        {
          for (Node* n : all_nodes)
          {
            decref(n);
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
      // immediately freed — but callers may still use dst. Decrefs are
      // deferred and applied after traversal completes.
      return true;
    }
    return false;
  }

  template<RegionType rt>
  bool addArc(Node* src, Node* dst)
  {
    if (!src || !dst)
      return false;

    if (
      std::find(src->neighbours.begin(), src->neighbours.end(), dst) ==
      src->neighbours.end())
    {
      src->neighbours.push_back(dst);
      if constexpr (rt == RegionType::Rc)
      {
        incref(dst);
      }
    }
    return true;
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
  void traverse_region(RegionRoot* sentinel)
  {
    UsingRegion ur(sentinel);
    std::cout << "Traversing region" << std::endl;
    Node* cur = sentinel->bridge;

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
  }

  template<RegionType rt>
  void churn_region(RegionRoot* sentinel)
  {
    UsingRegion ur(sentinel);
    std::cout << "Churning Region" << std::endl;
    Node* cur = sentinel->bridge;
    std::vector<Node*> workingSet;
    int WORKING_SET_SIZE = 20;
    int CHURN_EPOCHS = 1;
    int NEW_NODES = 4;

    // For RC: collect removed edge targets for deferred decref.
    std::vector<Node*> removed_targets;

    for (int k = 0; k < CHURN_EPOCHS; k++)
    {
      workingSet.clear();
      while (cur && !cur->neighbours.empty() &&
             (int)workingSet.size() < WORKING_SET_SIZE)
      {
        Node* dst = random_element(cur->neighbours);
        workingSet.push_back(dst);
        if constexpr (rt == RegionType::Rc)
        {
          removed_targets.push_back(dst);
        }
        cur = traverse<rt>(cur, dst);
      }

      // create some nodes and add them to the working set.
      std::vector<Node*> new_node_list;
      int new_nodes = 0;
      while ((int)workingSet.size() < WORKING_SET_SIZE && new_nodes < NEW_NODES)
      {
        Node* n = new Node();
        workingSet.push_back(n);
        new_node_list.push_back(n);
        new_nodes++;
      }

      // link the working set together.
      if ((int)workingSet.size() > 2)
      {
        for (int i = 0; i < WORKING_SET_SIZE; i++)
        {
          auto [first, second] = random_pair(workingSet.size());
          addArc<rt>(workingSet.at(first), workingSet.at(second));
        }
      }

      // RC: new nodes were created with surplus rc=1. addArc may have added
      // incref for edges pointing TO them. Decref once per new node to
      // compensate the surplus. If a new node has no incoming edges, this
      // brings rc to 0 and frees it immediately.
      if constexpr (rt == RegionType::Rc)
      {
        for (Node* n : new_node_list)
        {
          decref(n);
        }
      }
    }

    // Apply deferred decrefs now that all mutations are complete.
    if constexpr (rt == RegionType::Rc)
    {
      for (Node* target : removed_targets)
      {
        decref(target);
      }
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
        when(regionCown) << [](auto c) { traverse_region<rt>(c->root); };
      }
    }
  }

  template<RegionType rt>
  void
  multi_churn(cown_ptr<RegionCown>& graph, int churnsPerCollection, int churns)
  {
    for (int i = 0; i < churns; i++)
    {
      when(graph) << [](auto c) { churn_region<rt>(c->root); };
      if (i > 0 && i % churnsPerCollection == 0)
      {
        when(graph) << [](auto c) {
          std::cout << "RUNNING GARBAGE COLLECTION\n";
          UsingRegion rr(c->root);
          region_collect();
        };
      }
    }
  }

  template<RegionType rt>
  void run_churn_test(int size, int regions)
  {
    {
      std::vector<cown_ptr<RegionCown>> graphRegions =
        createGraph<rt>(size, regions, true);

      for (cown_ptr<RegionCown>& regionCown : graphRegions)
      {
        multi_churn<rt>(regionCown, 4, 20);
      }
    }
  }
} // namespace partially_connected
