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
    std::unordered_set<Node*> nodes;
    int id;

    Node()
    {
      num_nodes++;
    }
    void trace(ObjectStack& st) const
    {
      for (Node* node : nodes)
      {
        if (node != nullptr)
          st.push(node);
      }
    }
  };

  struct ONodes : public V<ONodes>
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

  struct ORoot : public V<ORoot>
  // This is the root of all the other regions
  // All the other regions are ONodes
  // Hence it contains an o_nodeses
  {
    std::vector<ONodes*> o_nodeses;
    void trace(ObjectStack& st) const
    {
      for (ONodes* o_nodes : o_nodeses)
      {
        if (o_nodes != nullptr)
          st.push(o_nodes);
      }
    }
  };

  inline ORoot* o_root;

  int numInaccessible(Node* root)
  {
    std::unordered_set<Node*> seen;
    std::queue<Node*> next;
    next.push(root);
    while (!next.empty())
    {
      Node* cur = next.front();
      next.pop();
      if (seen.find(cur) != seen.end())
      { // if already seen
        continue;
      }
      seen.insert(cur); // not seen yet
      for (Node* node : cur->nodes)
      {
        if (node && seen.find(node) == seen.end())
        {
          next.push(node);
        }
      }
    }
    return num_nodes - seen.size();
  }

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
    if (src->nodes.find(dst) != src->nodes.end())
      return;
    src->nodes.erase(dst);
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

        u->nodes.insert(v);
      }
    }
  }

  void createGraph(int size, int regions)
  {
    o_root = new (RegionType::Trace) ORoot;
    std::vector<size_t> region_sizes = random_regions(regions, size);
    std::cout << "Region sizes: ";
    for (size_t size : region_sizes)
    {
      std::cout << size << " ";
    }
    std::cout << std::endl;
    std::vector<ONodes*> otraces = std::vector<ONodes*>(regions);
    for (size_t region_size : region_sizes)
    {
      if (region_size == 0)
        continue;
      ONodes* o_nodes = new (RegionType::Trace) ONodes();
      {
        UsingRegion ur(o_nodes);
        Node* bridge = new Node();
        o_nodes->bridge = bridge;

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

      o_root->o_nodeses.push_back(o_nodes);
    }
  }

  bool removeArc(Node* src, Node* dst)
  {
    if (!src || !dst)
      return false;

    if (src->nodes.find(dst) != src->nodes.end())
    {
      src->nodes.erase(dst);
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

  void traverse_region(ONodes* o_nodes)
  {
    UsingRegion ur(o_nodes);
    std::cout << "Traversing region" << std::endl;
    Node* cur = o_nodes->bridge;

    while (cur && cur->nodes.size() > 0)
    {
      std::cout << "Current node: " << cur << " has " << cur->nodes.size()
                << " outgoing edges" << std::endl;
      Node* dst = random_element(cur->nodes);
      // nodes.erase(
      //   std::remove(
      //     nodes.begin(), nodes.end(), cur),
      //     nodes.end()
      // );
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

    for (ONodes* o_nodes : o_root->o_nodeses)
    {
      traverse_region(o_nodes);
    }
  }

  inline void run_test()
  {
    // Placeholder test function
    std::cout << "Running arbitrary_nodes test...\n";
  }

} // namespace arbitrary_nodes
