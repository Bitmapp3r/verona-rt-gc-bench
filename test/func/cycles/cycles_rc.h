// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../memory/memory.h"

namespace cycles_rc
{
  constexpr auto region_type = RegionType::Rc;
  using C = C1;

  void test_self_cycle()
  {
    std::cout << "Testing Self-Cycle...\n";
    auto* o = new (RegionType::Rc) C;
    {
      UsingRegion rc(o);
      auto* o1 = new C;

      // Create the Self-Loop
      o1->f1 = o1;

      // Mark as suspicious
      incref(o1);
      decref(o1);

      // Check GC worked
      check(debug_size() == 2); // o and o1 are still physically allocated.
      region_collect();
      check(debug_size() == 1); // o1 should now be identified as a cycle and killed.
    }
    region_release(o);
  }

  void test_diamond_cycle()
  {
    std::cout << "Testing Diamond Cycle (Multiple Internal Paths)...\n";
    auto* o = new (RegionType::Rc) C;
    {
      UsingRegion rc(o);
      auto* o1 = new C;
      auto* o2 = new C;
      auto* o3 = new C;
      auto* o4 = new C;

      // Diamond Shape
      o1->f1 = o2;
      o1->f2 = o3;

      o2->f1 = o4;
      o3->f1 = o4;
      incref(o4);

      o4->f1 = o1; // Close the loop

      // Mark as suspicious
      incref(o1);
      decref(o1);

      check(debug_size() == 5);
      region_collect();
      check(debug_size() == 1); // All 4 should die
    }
    region_release(o);
  }

  void test_deep_cycle()
  {
    std::cout << "Testing Deep Cycle...\n";
    auto* o = new (RegionType::Rc) C;
    {
      UsingRegion rc(o);

      C* head = new C;
      C* curr = head;

      // Create a chain of 1,000,000 objects
      for(int i=0; i < 1000000; ++i) {
        C* next = new C;
        curr->f1 = next;
        curr = next;
      }

      // Close the loop
      curr->f1 = head;

      // Mark as suspicious
      incref(head);
      decref(head);

      region_collect(); // Will this crash?
      check(debug_size() == 1);
    }
    region_release(o);
  }

  void test_multiple_cycles()
  {
    std::cout << "Testing Multiple Disconnected Cycles...\n";
    auto* o = new (RegionType::Rc) C;
    {
      UsingRegion rc(o);

      // Cycle A: o1 <-> o2
      auto* o1 = new C;
      auto* o2 = new C;
      o1->f1 = o2;
      o2->f1 = o1;

      // Mark as suspicious
      incref(o1);
      decref(o1);

      auto* o3 = new C;
      auto* o4 = new C;
      o3->f1 = o4;
      o4->f1 = o3;

      incref(o3);
      decref(o3);

      auto* o5 = new C;
      auto* o6 = new C;
      o5->f1 = o6;
      o6->f1 = o5;

      incref(o5);
      decref(o5);

      check(debug_size() == 7);
      region_collect();
      check(debug_size() == 1);
    }
    region_release(o);
  }

/*  void test_domino_cleanup() TODO
{
  std::cout << "Testing Domino Effect (A->a1->B->C->A)..." << std::endl;

  // Create Regions
  auto* regionA = new (RegionType::Rc) C;
  auto* regionB = new (RegionType::Rc) C;
  auto* regionC = new (RegionType::Rc) C;

  // Setup the Chain
  // Structure: A (Root) -> a1 (Inner) -> B (ISO) -> C (ISO) -> A (Root)

    // Link B -> C
    {
    UsingRegion rc(regionB);
    regionB->f1 = regionC;
    check(debug_size() == 1);
    }

    // Link C -> A
    {
    UsingRegion rc(regionC);
    regionC->f1 = regionA;
    check(debug_size() == 1);
    }

    // Link A -> a1 -> B
    {
    UsingRegion rc(regionA);
    auto* a1 = new C;
    a1->f1 = regionB;
    decref(a1);
    check(debug_size() == 1);
    }

  // a1 no longer exists, hence B should be gc'ed, leading to C to be gc'ed,
  // potentially leading to A being gc'ed?

  // CURRENT CONFUSION: a1 is being gc'ed which leads to B being gc'ed,
  // this can be seen by adding a region_release(regionC), this makes the heap
  // empty check pass without needing to release B. but C for some reason is
  // not touched.
  region_release(regionA);
  heap::debug_check_empty();
} */


  void run_test()
  {
    test_self_cycle();
    test_diamond_cycle();
    test_deep_cycle();
    test_multiple_cycles();
    //test_domino_cleanup(); TODO
  }
}