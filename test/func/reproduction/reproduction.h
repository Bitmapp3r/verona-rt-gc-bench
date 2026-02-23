// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <debug/harness.h>
#include <iostream>
#include <random>
#include <vector>
#include <verona.h>

namespace reproduction
{

#define LOGGING 0
#define LOG(x) if (LOGGING) { x; }

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

    ~Organism()
    {
      LOG(std::cout<< "Organism " << id << " destroyed\n");
    }

    void trace(ObjectStack& st)
    {
      if (root)
        st.push(root);
      if (next)
        st.push(next);
    }

    static Organism* reproduce(Organism* a, Organism* b, std::mt19937& gen)
    {
      
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

  Organism* make_organism(int depth)
  {
    auto* o = new Organism();
    o->root = make_tree(depth);
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

    if constexpr (rt == RegionType::Rc) {
      // incref(child->next);   // preserve reference
    }

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
    LOG(std::cout<< "trying to kill " << victim->id << "\n");
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
    std::cout<< "=========PRINTING RING=========\n";
    Organism* start = root;
    int j = 0;
    int i = 0;
    do
    {
      j++;
      i++;
      if (root)
        std::cout<< root->id << " -> ";
      else
        std::cout<< "null";
      root = root->next;
    } while (root != start && j < 1000);
    if (root)
      std::cout<< root->id << " -> ";
    else
      std::cout<< "null";
    std::cout<< "\n";
  }

  // ============================================================
  // Test driver
  // ============================================================



  template<RegionType rt>
  void run_test(int generations, int killPercent, int nodeTreeDepth, size_t seed = 0)
  {
    /*
    Keep nodeTreeDepth small. the larger it is, the exponentially bigger each organism is.
    A good number is ~7. any more than that and it's too much memory.
    Increase this number to give trace a hard time.
    */



    int initialPopSize = 10;
    Organism::counter = 0;
    auto* root = new (rt) Organism();
    int popCount = 1;
    {
      UsingRegion rr(root);

      // Build initial ring
      Organism* first = make_organism(nodeTreeDepth);
      popCount++;
      root->next = first;

      Organism* cur = first;

      for (int i = 0; i < initialPopSize - 1; i++)
      {
        auto* n = make_organism(nodeTreeDepth);
        popCount++;
        cur->next = n;
        cur = n;
      }

      cur->next = first;
      if constexpr (rt == RegionType::Rc)
        incref(first);

      LOG(std::cout<< "Initial region size: " << debug_size() << "\n"); 
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

        LOG(printRing(root->next));
        // ---- Killing phase ----
        int aliveCount = popCount;
        for (int i = 0; i < aliveCount; i++)
        {
          if (roulette(gen) < killPercent && cur != prev)
          {
            bool success = unlink_after<rt>(prev);
            cur = prev->next;
            if (success) {
              kills++;
              popCount--; 
            }
          }
          else
          {
            prev = cur;
            cur = cur->next;
          }
        }

        region_collect();

        LOG(std::cout << "Gen " << g
                  << " kills=" << kills
                  << " size=" << debug_size()
                  << "\n");

        // ---- Reproduction phase ----
        int births = (killPercent * popCount) / 100;

        Organism* p1 = root->next;
        Organism* p2 = root->next;

        for (int i = 0; i < births; i++)
        {
          p1 = p1->next;
          p2 = p2->next->next;

          auto* child = Organism::reproduce(p1, p2, gen);
          popCount++;
          link_after<rt>(p2, child);
        }

        LOG(std::cout << "After reproduction size="
                  << debug_size() << "\n");
      }
    }
  }
} // namespace reproduction
