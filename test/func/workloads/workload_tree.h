// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <debug/harness.h>
#include <verona.h>
#include <iostream>

using namespace snmalloc;
using namespace verona::rt;
using namespace verona::rt::api;

namespace workload_tree
{

/**
 * Workload 1: Tree Transformation (AST-style)
 * 
 * Pattern: Build immutable tree → Transform to new tree → Old tree becomes garbage
 * 
 * Expected performance:
 *   - Arena: BEST - build all at once, release all at once
 *   - Trace: GOOD - must trace live tree, sweep dead
 *   - RC: WORST - incref/decref on every node during construction
 * 
 * Use case: Compiler AST transformations, functional data structures
 */

// RC helper macros - compile-time eliminated when not using RC
#define INCREF(o) if constexpr (rt == RegionType::Rc) { if (o != nullptr) incref(o); }
#define DECREF(o) if constexpr (rt == RegionType::Rc) { if (o != nullptr) decref(o); }

// Simulates realistic pointer assignment overhead.
// In a real RC language, assigning to a field creates a new reference (incref)
// and the local variable going out of scope drops a reference (decref).
// This may push to Lins cycle detection stack even if refcount doesn't hit 0.
#define TRANSFER_REF(o) do { INCREF(o); DECREF(o); } while(0)

// Simple binary tree node
struct TreeNode : public V<TreeNode>
{
  TreeNode* left;
  TreeNode* right;
  int value;

  TreeNode() : left(nullptr), right(nullptr), value(0) {}

  void trace(ObjectStack& st) const
  {
    if (left != nullptr)
      st.push(left);
    if (right != nullptr)
      st.push(right);
  }
};

/**
 * Discard a tree to make it garbage.
 * For RC: decref root, which cascades through trace() to free entire tree.
 * For Trace: just nulls the pointer, GC will collect later.
 * For Arena: just nulls the pointer, no collection possible.
 */
template<RegionType rt>
inline void discard_tree(TreeNode*& root)
{
  DECREF(root);    // RC: cascades via trace(), Trace/Arena: no-op
  root = nullptr;
}

/**
 * Build a complete binary tree of given depth.
 * Returns the root node.
 * 
 * For depth=10: 2^10 - 1 = 1023 nodes
 * For depth=15: 2^15 - 1 = 32767 nodes
 * For depth=20: 2^20 - 1 = 1048575 nodes
 */
template<RegionType rt>
inline TreeNode* build_tree(int depth, int start_value = 0)
{
  if (depth <= 0)
    return nullptr;

  auto* node = new TreeNode();
  node->value = start_value;

  // Build children recursively
  auto* left = build_tree<rt>(depth - 1, start_value * 2 + 1);
  auto* right = build_tree<rt>(depth - 1, start_value * 2 + 2);

  // Assign children - simulate realistic RC overhead
  node->left = left;
  node->right = right;
  TRANSFER_REF(left);
  TRANSFER_REF(right);

  return node;
}

/**
 * Transform tree: increment all values by delta.
 * Creates a NEW tree (old one becomes garbage).
 * 
 * This simulates functional/immutable tree transformation.
 */
template<RegionType rt>
inline TreeNode* transform_tree(TreeNode* old_root, int delta)
{
  if (old_root == nullptr)
    return nullptr;

  auto* node = new TreeNode();
  node->value = old_root->value + delta;

  auto* left = transform_tree<rt>(old_root->left, delta);
  auto* right = transform_tree<rt>(old_root->right, delta);

  // Assign children - simulate realistic RC overhead
  node->left = left;
  node->right = right;
  TRANSFER_REF(left);
  TRANSFER_REF(right);

  return node;
}

/**
 * Count nodes in tree (for verification).
 */
inline size_t count_nodes(TreeNode* root)
{
  if (root == nullptr)
    return 0;
  return 1 + count_nodes(root->left) + count_nodes(root->right);
}

/**
 * Sum all values in tree (for verification).
 */
inline int sum_values(TreeNode* root)
{
  if (root == nullptr)
    return 0;
  return root->value + sum_values(root->left) + sum_values(root->right);
}

#undef INCREF
#undef DECREF
#undef TRANSFER_REF

/**
 * Run the tree transformation test.
 */
template<RegionType rt>
void run_test(int depth = 10, int transforms = 5)
{
  auto* root = new (rt) TreeNode();

  {
    UsingRegion rr(root);

    TreeNode* current = build_tree<rt>(depth);
    root->left = current;

    std::cout << "Tree built. Nodes: " << count_nodes(current) << "\n";
    std::cout << "Heap size: " << debug_size() << "\n";

    for (int i = 0; i < transforms; i++)
    {
      TreeNode* next = transform_tree<rt>(current, 1);
      discard_tree<rt>(current);
      current = next;
      root->left = current;

      int heap_before = debug_size();
      std::cout << "Heap size before collect: " << heap_before << "\n";

      region_collect();
      // Add timing to decref

      int heap_after = debug_size();
      std::cout << "Heap size after collect: " << heap_after << "\n";

      // Arena has no GC, so skip the heap size check
      if constexpr (rt != RegionType::Arena)
      {
        size_t expected = count_nodes(current) + 1; // +1 for root
        check(heap_after == static_cast<int>(expected));
      }
    }

    std::cout << "Completed " << transforms << " transforms.\n";
  }

  region_release(root);
  heap::debug_check_empty();
}

} // namespace workload_tree
