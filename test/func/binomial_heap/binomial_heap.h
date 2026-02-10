// Copyright Microsoft and Project Ververona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../binomial_heap/binomial_tree.h" // For Node objescts
#include "../memory/memory.h" // Mainly for the C1 objects

namespace binomial_heap
{
  /**
   * Test garbage collection with tree structures.
   *
   * This test creates a binary tree and progressively "prunes" branches
   * by breaking links, then verifies that GC collects the pruned subtrees.
   *
   * Tree structure before pruning:
   *          root
   *          /  \
   *         L1   R1
   *        /  \  / \
   *       L2  R2 L3 R3
   *
   * After pruning left branch: root only connects to R1 and its children
   * After pruning right of R1: only root and R1 remain
   */
  void test_tree_pruning()
  {
    // Root ISO object ID is 2 (The first ID/ID 1 may likely be the Region
    // Metadata object?)
    auto* root = new (RegionType::Trace) C1;

    {
      UsingRegion rr(root);

      // Build a binary tree (depth 2)
      //          root
      //          /  \
      //         L1   R1
      //        /  \  / \
      //       L2  R2 L3 R3

      auto* L1 = new C1;
      auto* R1 = new C1;
      auto* L2 = new C1;
      auto* R2 = new C1;
      auto* L3 = new C1;
      auto* R3 = new C1;

      // Link them up
      root->f1 = L1;
      root->f2 = R1;

      L1->f1 = L2;
      L1->f2 = R2;

      R1->f1 = L3;
      R1->f2 = R3;

      // Verify all 7 objects exist (root + 6 nodes)
      check(debug_size() == 7);

      // Run GC - nothing should be collected
      region_collect();
      check(debug_size() == 7);

      // Prune left branch by breaking link to L1
      root->f1 = nullptr;

      // Now L1, L2, R2 become unreachable (garbage)
      check(debug_size() == 7); // Still 7 in memory
      region_collect();
      check(debug_size() == 4); // Only root, R1, L3, R3 remain

      // Prune right subtree of R1
      R1->f1 = nullptr; // Break link to L3
      R1->f2 = nullptr; // Break link to R3

      check(debug_size() == 4);
      region_collect();
      check(debug_size() == 2); // Only root and R1 remain

      // Prune the last branch
      root->f2 = nullptr; // Break link to R1

      region_collect();
      check(debug_size() == 1); // Only root remains
    }

    region_release(root);
    heap::debug_check_empty();
  }

  /////////////// UNTESTED CODE BELOW //////////////////////////////////////////

  // /**
  //  * Test GC with multiple disjoint components.
  //  *
  //  * Creates several independent object chains that are all unreachable
  //  * from the root, then verifies GC collects them all.
  //  *
  //  * This tests that GC properly handles multiple garbage components
  //  * in a single collection cycle.
  //  */
  // void test_multiple_garbage_components()
  // {
  //   auto* root = new (RegionType::Trace) C1;

  //   {
  //     UsingRegion rr(root);

  //     // Create a reachable chain: root -> A1 -> A2 -> A3
  //     auto* A1 = new C1;
  //     auto* A2 = new C1;
  //     auto* A3 = new C1;
  //     root->f1 = A1;
  //     A1->f1 = A2;
  //     A2->f1 = A3;

  //     // Create garbage chain 1: G1a -> G1b -> G1c (unreachable)
  //     auto* G1a = new C1;
  //     auto* G1b = new C1;
  //     auto* G1c = new C1;
  //     G1a->f1 = G1b;
  //     G1b->f1 = G1c;

  //     // Create garbage chain 2: G2a -> G2b (unreachable)
  //     auto* G2a = new C1;
  //     auto* G2b = new C1;
  //     G2a->f1 = G2b;

  //     // Verify: 1 root + 3 reachable + 3 garbage1 + 2 garbage2 = 9
  //     check(debug_size() == 9);

  //     region_collect();

  //     // After GC: only root and reachable chain survive (1 + 3 = 4)
  //     check(debug_size() == 4);
  //   }

  //   region_release(root);
  //   heap::debug_check_empty();
  // }

  // /**
  //  * Test GC with shared references.
  //  *
  //  * Creates a DAG (Directed Acyclic Graph) where multiple parents
  //  * reference the same child. Verifies that shared child is only
  //  * collected when ALL parents become unreachable.
  //  *
  //  * Structure:
  //  *     root
  //  *     /  \
  //  *    L    R
  //  *     \  /
  //  *     shared
  //  */
  // void test_shared_references()
  // {
  //   auto* root = new (RegionType::Trace) C1;

  //   {
  //     UsingRegion rr(root);

  //     // Create shared structure
  //     auto* L = new C1;
  //     auto* R = new C1;
  //     auto* shared = new C1;

  //     // Both L and R point to shared
  //     root->f1 = L;
  //     root->f2 = R;

  //     L->f1 = shared;  // First path to shared
  //     R->f1 = shared;  // Second path to shared

  //     // Verify all 4 objects exist
  //     check(debug_size() == 4);

  //     region_collect();
  //     check(debug_size() == 4);  // All still reachable

  //     // Break left path to shared
  //     L->f1 = nullptr;

  //     // shared is STILL reachable through R
  //     region_collect();
  //     check(debug_size() == 4);  // All still live

  //     // Break right path to shared
  //     R->f1 = nullptr;

  //     // NOW shared becomes unreachable
  //     region_collect();
  //     check(debug_size() == 3);  // shared is collected

  //     // Break all paths to L and R
  //     root->f1 = nullptr;
  //     root->f2 = nullptr;

  //     region_collect();
  //     check(debug_size() == 1);  // Only root remains
  //   }

  //   region_release(root);
  //   heap::debug_check_empty();
  // }

  // /**
  //  * Stress test: Create a wide shallow tree and progressively
  //  * collect layers.
  //  *
  //  * Root has many children, collect them layer by layer.
  //  */
  // void test_wide_tree_collection()
  // {
  //   auto* root = new (RegionType::Trace) C1;

  //   {
  //     UsingRegion rr(root);

  //     constexpr int width = 10;
  //     C1* children[width];

  //     // Create 10 children of root, arranged in a linked list
  //     auto* current = root;
  //     for (int i = 0; i < width; i++)
  //     {
  //       children[i] = new C1;
  //       current->f1 = children[i];
  //       current = children[i];
  //     }

  //     // Verify: root + 10 children = 11
  //     check(debug_size() == 11);

  //     region_collect();
  //     check(debug_size() == 11);

  //     // Break the chain at position 5
  //     // This makes children[5..9] unreachable
  //     children[4]->f1 = nullptr;

  //     region_collect();
  //     check(debug_size() == 6);  // root + children[0..4]

  //     // Break at position 2
  //     children[1]->f1 = nullptr;

  //     region_collect();
  //     check(debug_size() == 3);  // root + children[0..1]

  //     // Break final link
  //     children[0]->f1 = nullptr;

  //     region_collect();
  //     check(debug_size() == 1);  // Only root
  //   }

  //   region_release(root);
  //   heap::debug_check_empty();
  // }

  //////////////////////////////////////////////////////////////////////////////

  void run_test()
  {
    // std::cout << "Running memory_tree::test_tree_pruning" << std::endl;
    test_tree_pruning();
    // test_multiple_garbage_components();
    // test_shared_references();
    // test_wide_tree_collection();
  }
}
