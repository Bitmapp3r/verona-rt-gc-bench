// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <debug/harness.h>
#include <iostream>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>
#include <verona.h>

#define INCREF(o) \
  if constexpr (rt == RegionType::Rc) \
    incref(o);
#define DECREF(o) \
  if constexpr (rt == RegionType::Rc) \
    decref(o);
#define TRANSFER(o) \
  if constexpr (rt == RegionType::Rc) \
    incref(o); \
  decref(o);

namespace reproduction
{

class Node : public V<Node>
{
public:
  std::vector<Node*> to;

  void trace(ObjectStack& os)
  {
    for (Node* node : to)
    {
      if (node != nullptr)
        os.push(node);
    }
  }
  Node() {}
  Node(Node& obj)
  {
    for (Node* n : obj.to)
    {
      to.push_back(
        new Node(*n)); // should be recursively calling the copy constructor.
    }
  }
};

class Organism : public V<Organism>
{
public:
  int id;
  Node* this_root;
  Organism* next = nullptr;
  void trace(ObjectStack& st)
  {
    if (this_root != nullptr)
    {
      st.push(this_root);
    }
    if (next != nullptr)
    {
      st.push(next);
    }
  }
  Organism(int id) : id(id) {}

  static int orgNumber;
  static Organism* reproduce(Organism* a, Organism* b, int id)
  {
    std::random_device rd;
    std::mt19937 gen(rd()); // mersenne twister engine

    std::uniform_int_distribution<size_t> coin(0, 1);

    Organism* child = new Organism(id);
    // trivial reproduction for now:
    Node* child_node = new Node;
    for (Node* n : a->this_root->to)
    {
      if (coin(gen) == 0)
        child_node->to.push_back(new Node(*n));
    }
    for (Node* n : b->this_root->to)
    {
      if (coin(gen) == 0)
        child_node->to.push_back(new Node(*n));
    }
    child->this_root = child_node;
    return child;
  }
};

/*
NO incref or decref in these 2 functions is intended.
we don't want any objects to have a reference count of 0 at any point.
*/
Node* create_node_with_n_children(int n)
{
  // references on the stack are gone when this function finishes.
  Node* node = new Node;
  for (int i = 0; i < n; i++)
  {
    Node* child = create_node_with_n_children(n - 1);
    node->to.push_back(child);
  }
  return node;
}

// DO NOT DISCARD RETURN VALUE (if using reference counting)
Organism* create_organism(int id)
{
  Organism* org = new Organism(id);

  Node* root = create_node_with_n_children(4);
  org->this_root = root;

  return org;
}

template<RegionType rt>
void run_test(int numGenerations, int killPercentage, int popSize)
{
  // create initial population
  auto* grand_father = new (rt) Organism(0);
  {
    UsingRegion rr(grand_father);
    Organism* org = create_organism(1);
    grand_father->next = org;
    INCREF(org);
    DECREF(org);
    Organism* cur = org;
    for (int i = 0; i < popSize; i++)
    {
      Organism* next = create_organism(i + 2);
      cur->next = next;
      INCREF(next);

      cur = next;
    }
    cur->next = grand_father->next;
    INCREF(grand_father->next);
    std::cout << "created the ring\n";
    std::cout << "region size:" << debug_size() << std::endl;
  }

  std::random_device rd;
  std::mt19937 gen(rd()); // mersenne twister engine

  std::uniform_int_distribution<size_t> kill_roulette(1, 100);

  {
    UsingRegion rr(grand_father);

    Organism* prev = grand_father->next;
    Organism* cur = prev->next;

    for (int i = 0; i < numGenerations; i++)
    {
      // Killing
      int kill_count = 0;
      Organism* prev = grand_father->next;
      Organism* cur = prev->next;
      for (int j = 0; j < popSize; j++)
      {
        if (kill_roulette(gen) < killPercentage)
        {
          // kill the cur.
          /*
          before: prev --> cur --> cur.next
          after: prev --> cur.next
          - cur loses one reference (prev->next no longer points to it)
          - cur.next gains one reference (prev->next now points to it)
          */
          kill_count++;
          Organism* kill_me = cur;
          cur = cur->next;
          prev->next = cur;
          INCREF(cur);       // prev->next now references cur
          DECREF(kill_me);   // kill_me lost reference from prev->next
        }
        else
        {
          prev = cur;
          cur = cur->next;
        }
      }
      region_collect();
      std::cout << "kill count: " << kill_count << std::endl;
      std::cout << "region size after collecting:" << debug_size() << std::endl;

      // reproducing
      // we pick 2 parents by just stepping forward through the link list
      // an arbitrary number of times.
      Organism* p1 = grand_father->next;
      Organism* p2 = grand_father->next;
      int p1_incrate = 7;
      int p2_incrate = 11;

      // this formula insures the population remains somewhat stable.
      for (int j = 0; j < killPercentage * popSize / 100; j++)
      {
        for (int i = 0; i < p1_incrate; i++)
        {
          if (p1 == nullptr)
            p1 = grand_father->next;
          else
            p1 = p1->next;
        }

        for (int i = 0; i < p2_incrate; i++)
        {
          if (p2 == nullptr)
            p2 = grand_father->next;
          else
            p2 = p2->next;
        }
        // reproduce p1 and p2
        Organism* child = Organism::reproduce(p1, p2, 90);
        // insert child after p2.
        // before: p2 --> p2.next
        // after:  p2 --> child --> p2.next
        child->next = p2->next;
        INCREF(p2->next);  // child->next now references p2->next
        p2->next = child;
        INCREF(child);     // p2->next now references child
      }
      // std::cout << "new children: " << killPercentage * popSize / 100 <<
      // std::endl;
      std::cout << "after reproduction, ";
      std::cout << "region size:" << debug_size() << std::endl;
    }
  }
}

} // namespace reproduction