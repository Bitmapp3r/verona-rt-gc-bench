// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../memory/memory.h"

namespace semispace_gc
{
  /**
   * The iso (root) object of a SemiSpace region is heap-allocated and
   * pinned — it never moves during region_collect() or semispace
   * growth.  This means the C++ pointer returned by
   * `new (RegionType::SemiSpace) T` stays valid for the lifetime of
   * the region, with no need to call get_root() after GC or growth.
   *
   * Non-root objects in from-space are still copied/relocated by GC
   * and growth, so C++ local pointers to interior objects become stale
   * after those operations.  Navigate from the (stable) root instead.
   */


  // ---------------------------------------------------------------------------
  // Object types used by the growth & large-object tests.
  // ---------------------------------------------------------------------------

  // A chunk that is ~128KB. Several of these will fill the initial 1MB space.
  static constexpr size_t CHUNK_BODY = 128 * 1024;
  struct Chunk : public V<Chunk>
  {
    Chunk* next = nullptr;
    uint8_t payload[CHUNK_BODY];

    void trace(ObjectStack& st) const
    {
      if (next != nullptr)
        st.push(next);
    }

    void relocate(Object* (*fwd)(Object*))
    {
      if (next != nullptr)
        next = (Chunk*)fwd(next);
    }
  };

  // An object whose aligned size exceeds LARGE_OBJECT_THRESHOLD (512 KB).
  // This should always go into the large object list.
  static constexpr size_t LARGE_BODY = 600 * 1024;
  struct LargeObj : public V<LargeObj>
  {
    uint8_t payload[LARGE_BODY];

    void trace(ObjectStack&) const {}
  };

  // A node that can hold pointers to both C1 and LargeObj objects.
  // This lets us build graphs involving large objects without type aliasing.
  struct MixedNode : public V<MixedNode>
  {
    MixedNode* child = nullptr;
    LargeObj* big = nullptr;

    void trace(ObjectStack& st) const
    {
      if (child != nullptr)
        st.push(child);
      if (big != nullptr)
        st.push(big);
    }

    void relocate(Object* (*fwd)(Object*))
    {
      if (child != nullptr)
        child = (MixedNode*)fwd(child);
      if (big != nullptr)
        big = (LargeObj*)fwd(big);
    }
  };

  // ---------------------------------------------------------------------------
  // Original functional tests
  // ---------------------------------------------------------------------------

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

      region_ensure_available(4 * vsizeof<C1>);

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

      // GC keeps pinned root and B; discards A, C, D.
      region_collect();

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

      region_ensure_available(3 * vsizeof<C1>);

      auto* a = new C1;
      auto* b = new C1;
      auto* c = new C1;

      root->f1 = a;
      a->f1 = b;
      b->f1 = c;

      check(debug_size() == 4);

      // Three GC cycles, nothing to collect.
      region_collect();
      check(debug_size() == 4);

      region_collect();
      check(debug_size() == 4);

      region_collect();
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

      region_ensure_available(2 * vsizeof<C1>);

      auto* a = new C1;
      auto* b = new C1;

      root->f1 = a;
      a->f1 = b;

      check(debug_size() == 3);

      // Cycle 1: no garbage.
      region_collect();
      check(debug_size() == 3);

      // Make b unreachable: root->f1 is the new 'a' after GC.
      root->f1->f1 = nullptr; // a->f1 = nullptr
      region_collect();
      check(debug_size() == 2); // root + a

      // Make a unreachable.
      root->f1 = nullptr;
      region_collect();
      check(debug_size() == 1); // only root

      // Allocate more after multiple GC cycles.
      region_ensure_available(2 * vsizeof<C1>);

      auto* c = new C1;
      auto* d = new C1;

      root->f1 = c;
      c->f1 = d;
      check(debug_size() == 3);

      // GC with all reachable.
      region_collect();
      check(debug_size() == 3);

      // Make all children unreachable.
      root->f1 = nullptr;
      region_collect();
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

      region_ensure_available(3 * vsizeof<C1>);

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
      check(debug_size() == 4);

      // Break one path (L -> shared). Shared still reachable through R.
      root->f1->f1 = nullptr; // L->shared = nullptr
      region_collect();
      check(debug_size() == 4); // all still reachable

      // Break second path (R -> shared). Shared now unreachable.
      root->f2->f1 = nullptr; // R->shared = nullptr
      region_collect();
      check(debug_size() == 3); // root, L, R

      // Remove L and R.
      root->f1 = nullptr;
      root->f2 = nullptr;
      region_collect();
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

      region_ensure_available(5 * vsizeof<C1>);

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
      check(debug_size() == 3); // root, n1, n2

      // Break root->n1.
      root->f1 = nullptr;
      region_collect();
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
      check(debug_size() == 1);
      region_collect();
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

      region_ensure_available(1 * vsizeof<C1>);

      auto* a = new C1;

      root->f1 = a;
      check(debug_size() == 2);

      // GC (no garbage).
      region_collect();
      check(debug_size() == 2);

      // Allocate new objects AFTER GC (in the new from-space).
      region_ensure_available(2 * vsizeof<C1>);

      auto* b = new C1;
      auto* c = new C1;

      root->f2 = b;
      b->f1 = c;

      check(debug_size() == 4);

      // GC again — all reachable.
      region_collect();
      check(debug_size() == 4);

      // Make some garbage and collect.
      root->f2 = nullptr; // b and c become garbage
      region_collect();
      check(debug_size() == 2); // root + a
    }

    region_release(root);
    heap::debug_check_empty();
  }

  // ---------------------------------------------------------------------------
  // Tests 8–11: Semispace growth, object placement, and sizing.
  // ---------------------------------------------------------------------------

  /**
   * Test 8: Small objects are bump-allocated in from-space (not in the large
   * object list), and the semispace does NOT grow while there is room.
   */
  void test_small_objects_in_fromspace()
  {
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      size_t initial_size = debug_semispace_size();
      check(initial_size == RegionSemiSpace::INITIAL_SEMISPACE_SIZE);

      // Large object list should be empty.
      check(debug_large_object_count() == 0);

      // Allocate several small objects. They should all go into from-space.
      region_ensure_available(3 * vsizeof<C1>);

      auto* a = new C1;
      auto* b = new C1;
      auto* c = new C1;

      root->f1 = a;
      a->f1 = b;
      b->f1 = c;

      check(debug_size() == 4);
      check(debug_large_object_count() == 0);

      // from-space usage should have increased, but semispace should NOT
      // have grown.
      check(debug_semispace_size() == initial_size);
      check(debug_fromspace_used() > 0);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 9: Large objects (> LARGE_OBJECT_THRESHOLD) are allocated into
   * the large object list, not bump-allocated in from-space.
   */
  void test_large_objects_in_large_list()
  {
    // Use C1 as the iso root. We allocate LargeObj inside the region.
    auto* root = new (RegionType::SemiSpace) C1;

    {
      UsingRegion rr(root);

      check(debug_large_object_count() == 0);
      size_t used_before = debug_fromspace_used();

      // Allocate a large object — should go to the large object list.
      auto* big = new LargeObj;
      UNUSED(big);

      check(debug_large_object_count() == 1);
      check(debug_size() == 2); // root + big

      // from-space usage should NOT have increased for the large object
      // (it went to the heap), so used should be the same.
      check(debug_fromspace_used() == used_before);

      // Semispace should NOT have grown.
      check(debug_semispace_size() == RegionSemiSpace::INITIAL_SEMISPACE_SIZE);

      // Allocate a second large object.
      auto* big2 = new LargeObj;
      UNUSED(big2);

      check(debug_large_object_count() == 2);
      check(debug_size() == 3);
      check(debug_fromspace_used() == used_before);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 10: The semispace grows (doubles) only when from-space cannot
   * fit a new allocation, and the new semispace size is correct.
   *
   * After every allocation that may trigger growth we walk the chain
   * from root to find the tail, since local pointers to non-root
   * from-space objects are invalidated by grow().  The root itself is
   * pinned and never moves.
   */
  void test_semispace_grows_when_full()
  {
    // Use a Chunk as the iso root so the root itself takes space.
    auto* root = new (RegionType::SemiSpace) Chunk;

    {
      UsingRegion rr(root);

      size_t ss = debug_semispace_size();
      check(ss == RegionSemiSpace::INITIAL_SEMISPACE_SIZE);

      // Keep allocating ~128KB chunks until the semispace must grow.
      // Each Chunk (with header + alignment) is a bit over CHUNK_BODY.
      // We keep track of how many fit before the first growth event.
      size_t count = 1; // root is already allocated

      while (debug_semispace_size() == ss)
      {
        auto* c = new Chunk;

        // The allocation above may have triggered grow(), which
        // invalidates all previous from-space pointers. Walk the
        // chain from the pinned root to find the current tail.
        Chunk* tail = root;
        while (tail->next != nullptr)
          tail = tail->next;
        tail->next = c;

        count++;
      }

      // After growth, the semispace should have doubled.
      check(debug_semispace_size() == ss * 2);

      // We should have allocated roughly ss / sizeof(Chunk) objects
      // before the growth triggered (sanity check — at least a few).
      check(count > 2);

      std::cout << "    (grew after " << count << " chunks)" << std::endl;

      // Allocate a few more — should fit without another growth event.
      size_t ss2 = debug_semispace_size();
      auto* extra = new Chunk;

      // Link 'extra' at the tail.
      Chunk* tail = root;
      while (tail->next != nullptr)
        tail = tail->next;
      tail->next = extra;

      check(debug_semispace_size() == ss2);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /**
   * Test 11: After GC collects garbage the semispace size is preserved
   * (it does not shrink), and memory-used / object-count are correct.
   * Also verifies that large objects are freed by GC when unreachable.
   *
   * Graph:  root -> a -> b
   *                  |    |
   *                 big1 big2
   */
  void test_sizes_after_gc()
  {
    auto* root = new (RegionType::SemiSpace) MixedNode;

    {
      UsingRegion rr(root);

      // Allocate small objects via MixedNode chain.
      region_ensure_available(2 * vsizeof<MixedNode>);

      auto* a = new MixedNode;
      auto* b = new MixedNode;
      root->child = a;
      a->child = b;
      check(debug_size() == 3);

      // Allocate large objects and attach to the chain.
      auto* big1 = new LargeObj;
      auto* big2 = new LargeObj;
      a->big = big1;
      b->big = big2;

      check(debug_size() == 5);          // root, a, b, big1, big2
      check(debug_large_object_count() == 2);

      // GC with all reachable — sizes should not change.
      region_collect();
      check(debug_size() == 5);
      check(debug_large_object_count() == 2);

      // Make b (and big2) unreachable: root->child is 'a' after GC.
      root->child->child = nullptr; // a->child = nullptr, kills b and big2
      region_collect();

      check(debug_size() == 3);          // root, a, big1
      check(debug_large_object_count() == 1); // only big1

      // Make big1 unreachable.
      root->child->big = nullptr; // a->big = nullptr
      region_collect();

      check(debug_size() == 2);          // root, a
      check(debug_large_object_count() == 0);

      // Semispace size should still be at least INITIAL (never shrinks).
      check(debug_semispace_size() >= RegionSemiSpace::INITIAL_SEMISPACE_SIZE);
    }

    region_release(root);
    heap::debug_check_empty();
  }

  // ---------------------------------------------------------------------------
  // Test runner
  // ---------------------------------------------------------------------------

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

    std::cout << "Test 8: Small objects in from-space..." << std::endl;
    test_small_objects_in_fromspace();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 9: Large objects in large list..." << std::endl;
    test_large_objects_in_large_list();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 10: Semispace grows when full..." << std::endl;
    test_semispace_grows_when_full();
    std::cout << "  PASSED" << std::endl;

    std::cout << "Test 11: Sizes after GC..." << std::endl;
    test_sizes_after_gc();
    std::cout << "  PASSED" << std::endl;

    std::cout << "=== All SemiSpace GC tests passed ===" << std::endl;
  }
}
