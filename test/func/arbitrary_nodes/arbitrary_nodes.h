#pragma once

#include "func/ext_ref/ext_ref_basic.h"

#include <algorithm>
#include <debug/harness.h>
#include <iostream>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <verona.h>

inline int num_nodes = 0;
class Node : public V<Node>{
public:
  std::unordered_set<Node*> nodes;
  int id;

  Node ()
  {
    num_nodes++;
  }
  void trace(ObjectStack& st) const {
    for (Node* node : nodes)
    {
      if (not (node == nullptr))
        st.push(node);
    }
  }
};

struct ONodes : public V<ONodes>
// This is a single region
// This single region contains many nodes
// Hence the name ONodes
{
  std::vector<Node*> nodes;

  void trace(ObjectStack& st) const
  {
    for (Node* node : nodes)
    {
      if (node == nullptr)
        st.push(node);
    }
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
      if (o_nodes == nullptr)
        st.push(o_nodes);
    }
  }
};

inline ORoot* o_root;

int numInaccessible(Node* root) {
  std::unordered_set<Node*> seen;
  std::queue<Node*> next;
  next.push(root);
  while (!next.empty()) {
    Node* cur = next.front();
    next.pop();
    if (seen.find(cur) != seen.end()) { // if already seen
      continue;
    }
    seen.insert(cur);                // not seen yet
    for (Node* node : cur->nodes)
    {
      if (node && seen.find(node) == seen.end()) {
        next.push(node);
      }
    }

  }
  return num_nodes - seen.size();
}


std::vector<size_t> random_regions(size_t regions, size_t size) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> dist(0, size);

  std::vector<size_t> cuts(regions + 1);
  cuts[0] = 0;
  cuts[regions] = size;

  for (size_t i = 1; i < regions; ++i) {
    cuts[i] = dist(gen);
  }

  std::sort(cuts.begin(), cuts.end());

  std::vector<size_t> result(regions);
  for (size_t i = 0; i < regions; ++i)
  {
    result[i] = cuts[i + 1] - cuts[i];
  }

  return result;
}


void kill_node(Node* src, Node* dst) {
  if (src->nodes.find(dst) != src->nodes.end())
    return;
  src->nodes.erase(dst);
}

inline void fully_connect(const ONodes* o_nodes)
{
  for (Node* u : o_nodes->nodes)
  {
    if (u == nullptr) continue;

    for (Node* v : o_nodes->nodes)
    {
      if (v == nullptr) continue;
      if (u == v) continue;

      u->nodes.insert(v);
    }
  }
}

void createGraph(int size, int regions)
{
  o_root = new (RegionType::Trace) ORoot;
  std::vector<size_t> region_sizes = random_regions(regions, size);

  std::vector<ONodes*> otraces = std::vector<ONodes*>(regions);
  for (size_t region_size : region_sizes)
  {
    ONodes* o_nodes = new (RegionType::Trace) ONodes();
    {
      UsingRegion ur(o_nodes);
      for (size_t i = 0; i != region_size; i++)
      {
        Node* node = new Node();
        o_nodes->nodes.push_back(node);
      }

      fully_connect(o_nodes);
    }

    o_root->o_nodeses.push_back(o_nodes);
  }
}
