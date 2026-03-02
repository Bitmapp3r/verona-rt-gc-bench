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
    int id;
    Node()
    {
      num_nodes++;
      id = num_nodes;
    }

    ~Node() {std::cout << "node " << id << " died\n";}

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

  std::pair<size_t, size_t> random_pair(int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, max - 1);
    if (max == 1) {
      return std::make_pair(0, 0);
    }
    size_t first = dist(gen);
    size_t second;
    do {
      second = dist(gen);
    } while (first == second);
    return std::make_pair(first, second);
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

  void partially_connect(const std::vector<Node*>& nodes) {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> dist(0.0, 1.0);
    float connectedness = 0.7f;
    // nodes.neighbours should have size connectedness% of nodes.size
    for (Node* u : nodes) {
      if (!u) continue;

      for (Node* v : nodes) {
        if (!v || u == v) continue;

        if (dist(gen) < connectedness)
          u->neighbours.insert(v);
      }
    }
  }

  template<RegionType rt>
  std::vector<cown_ptr<GraphRegionCown>> createGraph(int size, int regions, bool partial = false)
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

        if (partial)
          partially_connect(all_nodes);
        else 
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

  bool addArc(Node* src, Node* dst) 
  {
    if (!src || !dst)
      return false;

    if (src->neighbours.find(dst) == src->neighbours.end()) {
      src->neighbours.insert(dst);
    }
    return true;
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


  void churn_region(GraphRegion* graphRegion) 
  {
    UsingRegion ur(graphRegion);
    std::cout << "Churning Region" << std::endl;
    Node* cur = graphRegion->bridge;
    std::vector<Node*> workingSet;
    int WORKING_SET_SIZE = 20;
    int CHURN_EPOCHS =  1;
    int NEW_NODES = 4;
    // traverse the graph picking up references in an array. then modify those nodes between each other
    // have a chance to remove edge when traversing aswell
    for (int k = 0; k < CHURN_EPOCHS; k++) {
      workingSet.clear();
      while (cur && !cur->neighbours.empty() && workingSet.size() < WORKING_SET_SIZE) {
        Node* dst = random_element(cur->neighbours);
        workingSet.push_back(dst);
        cur = traverse(cur, dst);
      }

      // lets create some nodes and add them to the working set.
      int new_nodes = 0;
      while (workingSet.size() < WORKING_SET_SIZE && new_nodes < NEW_NODES) {
        workingSet.push_back(new Node());
        new_nodes++;
      }

           
      // link the working set together.
      if (workingSet.size() > 2) {
        for (int i = 0; i < WORKING_SET_SIZE; i++) {
          // pick 2 random nodes;
          auto [first, second] = random_pair(workingSet.size());
          addArc(workingSet.at(first), workingSet.at(second));
        }
      }
    }
  }



  template<RegionType rt>
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

  void start_collect(cown_ptr<GraphRegionCown> graph, long delay) {

  }

  void multi_churn(cown_ptr<GraphRegionCown>& graph, int churnsPerCollection, int churns) {
    when (graph) 
      << [=](auto c) {
        churn_region(c->graphRegion);
        for (int i = 0; i < 10; i++) {
          when (graph) << [](auto c) {churn_region(c->graphRegion);};
          if (i % churnsPerCollection == 0) {
            when (graph) << [](auto c) {
              std::cout << "RUNNING GARBAGE COLLECTION\n";
              UsingRegion rr(c->graphRegion);
              region_collect();};       
          }
        }
      };
  }

  template<RegionType rt> 
  void run_churn_test(int size, int regions) 
  {
    {
      std::vector<cown_ptr<GraphRegionCown>> graphRegions = 
        createGraph<rt>(size, regions, true);
      
      for (cown_ptr<GraphRegionCown>& graphRegionCown : graphRegions) {
        multi_churn(graphRegionCown, 4, 20);
      }
    }
  }
} // namespace arbitrary_nodes



// might be causing isssue because of size 1 regions?????