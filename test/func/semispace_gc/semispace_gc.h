// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../memory/memory.h"

namespace semispace_gc
{
  /**
   * IMPORTANT: After each region_collect() call with a SemiSpace region, ALL
   * C++ local pointers to objects in the region are INVALIDATED because the
   * copying GC physically moves objects. Every time we want to access objects
   * after a region_collect(), we must first re-read the root (entry point) from
   * the region context after each GC, and traverse from the updated root to
   * reach the desired interior objects.
   */

  // Helper to get the current root iso as a C1* from the region context.
  inline C1* get_root()
  {
    return (C1*)internal::RegionContext::get_entry_point();
  }

  /**
   * Test 1: Basic GC — create objects, make some unreachable, collect.
   *
   *        root
   *        /  \
   *       A    B
   *      / \
   *     C   D
   *
   * Prune left branch (A): A, C, D become garbage.
   */
  void test_basic_gc()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* A = new C1;
      auto* B = new C1;
      auto* C = new C1;
      auto* D = new C1;

      root->f1 = A;
      root->f2 = B;
      A->f1 = C;
      A->f2 = D;

      check(debug_size() == 5);

      // Prune left branch BEFORE GC so we don't need stale pointers.
      root->f1 = nullptr;

      // GC copies root and B; discards A, C, D.
      region_collect();
      root = get_root();

      check(debug_size() == 2); // root + B
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 2: GC with no garbage — everything reachable, just gets copied.
   */
  void test_no_garbage()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* a = new C1;
      auto* b = new C1;
      auto* c = new C1;
      root->f1 = a;
      a->f1 = b;
      b->f1 = c;

      check(debug_size() == 4);

      // Three GC cycles, nothing to collect.
      region_collect();
      root = get_root();
      check(debug_size() == 4);

      region_collect();
      root = get_root();
      check(debug_size() == 4);

      region_collect();
      root = get_root();
      check(debug_size() == 4);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 3: Multiple GC cycles with progressive pruning.
   *   root -> a -> b
   */
  void test_multiple_gc_cycles()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* a = new C1;
      auto* b = new C1;
      root->f1 = a;
      a->f1 = b;

      check(debug_size() == 3);

      // Cycle 1: no garbage.
      region_collect();
      root = get_root();
      check(debug_size() == 3);

      // Make b unreachable: root->f1 is the new 'a' after GC.
      root->f1->f1 = nullptr; // a->f1 = nullptr
      region_collect();
      root = get_root();
      check(debug_size() == 2); // root + a

      // Make a unreachable.
      root->f1 = nullptr;
      region_collect();
      root = get_root();
      check(debug_size() == 1); // only root

      // Allocate more after multiple GC cycles.
      auto* c = new C1;
      auto* d = new C1;
      root->f1 = c;
      c->f1 = d;
      check(debug_size() == 3);

      // GC with all reachable.
      region_collect();
      root = get_root();
      check(debug_size() == 3);

      // Make all children unreachable.
      root->f1 = nullptr;
      region_collect();
      root = get_root();
      check(debug_size() == 1);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 4: Shared references (DAG).
   *
   *     root
   *     /  \
   *    L    R
   *     \  /
   *     shared
   *
   * 'shared' only becomes garbage when BOTH paths are broken.
   */
  void test_shared_references()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* L = new C1;
      auto* R = new C1;
      auto* shared = new C1;

      root->f1 = L;
      root->f2 = R;
      L->f1 = shared;
      R->f1 = shared;

      check(debug_size() == 4);

      // GC, nothing to collect.
      region_collect();
      root = get_root();
      check(debug_size() == 4);

      // Break one path (L -> shared). Shared still reachable through R.
      root->f1->f1 = nullptr; // L->shared = nullptr
      region_collect();
      root = get_root();
      check(debug_size() == 4); // all still reachable

      // Break second path (R -> shared). Shared now unreachable.
      root->f2->f1 = nullptr; // R->shared = nullptr
      region_collect();
      root = get_root();
      check(debug_size() == 3); // root, L, R

      // Remove L and R.
      root->f1 = nullptr;
      root->f2 = nullptr;
      region_collect();
      root = get_root();
      check(debug_size() == 1);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 5: Chain — break in the middle.
   *
   * root -> N1 -> N2 -> N3 -> N4 -> N5
   */
  void test_chain_collection()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* n1 = new C1;
      auto* n2 = new C1;
      auto* n3 = new C1;
      auto* n4 = new C1;
      auto* n5 = new C1;

      root->f1 = n1;
      n1->f1 = n2;
      n2->f1 = n3;
      n3->f1 = n4;
      n4->f1 = n5;

      check(debug_size() == 6);

      // Break at n2->n3 (before GC, using stale-safe C++ pointer).
      n2->f1 = nullptr;
      region_collect();
      root = get_root();
      check(debug_size() == 3); // root, n1, n2

      // Break root->n1.
      root->f1 = nullptr;
      region_collect();
      root = get_root();
      check(debug_size() == 1);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 6: Only root, no children. GC is a trivial copy.
   */
  void test_root_only()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      check(debug_size() == 1);
      region_collect();
      root = get_root();
      check(debug_size() == 1);
      region_collect();
      root = get_root();
      check(debug_size() == 1);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 7: Allocate after GC — verify bump allocator works in the
   * swapped from-space.
   */
  void test_alloc_after_gc()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      auto* a = new C1;
      root->f1 = a;
      check(debug_size() == 2);

      // GC (no garbage).
      region_collect();
      root = get_root();
      check(debug_size() == 2);

      // Allocate new objects AFTER GC (in the new from-space).
      auto* b = new C1;
      auto* c = new C1;
      root->f2 = b;
      b->f1 = c;

      check(debug_size() == 4);

      // GC again — all reachable.
      region_collect();
      root = get_root();
      check(debug_size() == 4);

      // Make some garbage and collect.
      root->f2 = nullptr; // b and c become garbage
      region_collect();
      root = get_root();
      check(debug_size() == 2); // root + a
    }

    region_release(root);
    heap::debug_check_empty();
  }

  void run_test()
  {
    std::cout << "=== SemiSpace GC Tests ===" << std::endl;

    std::cout << "Test 1: Basic GC..." << std::endl;
    test_basic_gc();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 2: No garbage..." << std::endl;
    test_no_garbage();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 3: Multiple GC cycles..." << std::endl;
    test_multiple_gc_cycles();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 4: Shared references..." << std::endl;
    test_shared_references();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 5: Chain collection..." << std::endl;
    test_chain_collection();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 6: Root only..." << std::endl;
    test_root_only();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 7: Alloc after GC..." << std::endl;
    test_alloc_after_gc();
    std::cout << "  PASSED" << std::endl;

    std::cout << "=== All SemiSpace GC tests passed ===" << std::endl;
  }
}
