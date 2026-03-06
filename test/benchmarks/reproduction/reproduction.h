// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

/**
 * This test creates a ring of Organisms, each containing a tree of Nodes. The
 * test simulates multiple generations with a configurable population size.
 *
 * Each generation has a killing phase where organisms are randomly removed from
 * the ring based on a kill percentage, followed by a reproduction phase where
 * new organisms are created by combining genetic material (node trees) from two
 * parent organisms.
 *
 * This tests region-based GC with a dynamic population that grows and shrinks,
 * where garbage collection runs after the killing phase to reclaim unreachable
 * organisms and their associated node trees.
 **/

#pragma once

#include <debug/harness.h>
#include <iostream>
#include <random>
#include <vector>
#include <verona.h>

namespace reproduction
{

  // ============================================================
  // Node
  // ============================================================

  class Node : public V<Node>
  {
  public:
    std::vector<Node*> to;

    Node() = default;

    Node(const Node& other)
    {
      for (auto* n : other.to)
        to.push_back(new Node(*n));
    }

    void trace(ObjectStack& st)
    {
      for (auto* n : to)
        if (n)
          st.push(n);
    }
  };

  // ============================================================
  // Organism
  // ============================================================

  class Organism : public V<Organism>
  {
  public:
    int id;
    Node* root = nullptr;
    Organism* next = nullptr;

    static int counter;

    Organism() : id(counter++) {}

    void trace(ObjectStack& st)
    {
      if (root)
        st.push(root);
      if (next)
        st.push(next);
    }

    static Organism* reproduce(Organism* a, Organism* b)
    {
      static thread_local std::mt19937 gen{std::random_device{}()};
      std::uniform_int_distribution<int> coin(0, 1);

      auto* child = new Organism();
      auto* r = new Node();

      for (auto* n : a->root->to)
        if (coin(gen) == 0)
          r->to.push_back(new Node(*n));

      for (auto* n : b->root->to)
        if (coin(gen) == 0)
          r->to.push_back(new Node(*n));

      child->root = r;
      return child;
    }
  };

  int Organism::counter = 0;

  // ============================================================
  // Tree creation
  // ============================================================

  Node* make_tree(int depth)
  {
    auto* n = new Node;
    if (depth == 0)
      return n;

    for (int i = 0; i < depth; i++)
      n->to.push_back(make_tree(depth - 1));

    return n;
  }

  Organism* make_organism()
  {
    auto* o = new Organism();
    o->root = make_tree(4);
    return o;
  }

  // ============================================================
  // Ring utilities
  // ============================================================

  template<RegionType rt>
  void link_after(Organism* pos, Organism* child)
  {
    // child takes over pos->next position
    child->next = pos->next;
    pos->next = child;
  }

  template<RegionType rt>
  bool unlink_after(Organism* prev)
  {
    auto* victim = prev->next;
    if (victim->id == 1)
    {
      return false;
    }
    // remove victim from ring
    if (prev->next == victim->next)
    {
      return false;
    }
    Logging::cout() << "trying to kill " << victim->id << "\n";
    prev->next = victim->next;

    if constexpr (rt == RegionType::Rc)
    {
      incref(victim->next); // preserve next
      decref(victim); // drop victim
    }
    return true;
  }

  void printRing(Organism* root)
  {
    Logging::cout() << "=========PRINTING RING=========\n";
    Organism* start = root;
    int i = 0;
    do
    {
      i++;
      if (root)
        Logging::cout() << root->id << " -> ";
      else
        Logging::cout() << "null";
      root = root->next;
    } while (root != start);
    if (root)
      Logging::cout() << root->id << " -> ";
    else
      Logging::cout() << "null";
    Logging::cout() << "\n";
  }

  // ============================================================
  // Test driver
  // ============================================================

  template<RegionType rt>
  void run_test(int generations, int killPercent, int popSize, size_t seed = 0)
  {
    auto* root = new (rt) Organism();

    {
      UsingRegion rr(root);

      // Build initial ring
      Organism* first = make_organism();
      root->next = first;

      Organism* cur = first;

      for (int i = 0; i < popSize - 1; i++)
      {
        auto* n = make_organism();
        cur->next = n;
        cur = n;
      }

      cur->next = first;
      if constexpr (rt == RegionType::Rc)
        incref(first);

      // Logging::cout() << "Initial region size: " << debug_size() << "\n";
    }

    if (seed == 0)
      seed = std::random_device{}();
    std::mt19937 gen{static_cast<std::mt19937::result_type>(seed)};
    std::uniform_int_distribution<int> roulette(1, 100);

    {
      UsingRegion rr(root);

      for (int g = 0; g < generations; g++)
      {
        Organism* prev = root->next;
        Organism* cur = prev->next;

        int kills = 0;

        // ---- Killing phase ----
        for (int i = 0; i < popSize; i++)
        {
          if (roulette(gen) < killPercent && cur != prev)
          {
            bool success = unlink_after<rt>(prev);
            cur = prev->next;
            if (success)
              kills++;
          }
          else
          {
            prev = cur;
            cur = cur->next;
          }
        }

        region_collect();

        Logging::cout() << "Gen " << g
                  << " kills=" << kills
                  << " size=" << debug_size()
                  << "\n";

        // ---- Reproduction phase ----
        int births = (killPercent * popSize) / 100;

        Organism* p1 = root->next;
        Organism* p2 = root->next;

        for (int i = 0; i < births; i++)
        {
          p1 = p1->next;
          p2 = p2->next->next;

          auto* child = Organism::reproduce(p1, p2);
          link_after<rt>(p2, child);
        }

        Logging::cout() << "After reproduction size="
                  << debug_size() << "\n";
      }
    }
  }
} // namespace reproduction
