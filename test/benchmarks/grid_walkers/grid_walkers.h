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

/**
 * This test creates a grid of nodes with a configurable number of "walkers"
 * moving across the grid. The root of the grid is the top left node.
 *
 * Each walker randomly moves to adjacent nodes, destroying edges as they move.
 * This causes nodes to become unreachable from the root.
 *
 * This tests RegionTrace by verifying that unreachable nodes are freed by the
 * garbage collector. At every step, we check that the number of unreachable
 * nodes equals the number of freed nodes. Grid size, number of steps, and
 * number of walkers are configurable.
 **/

class Node : public V<Node>
{
public:
  Node* down = nullptr;
  Node* right = nullptr;

  Node* up = nullptr;
  Node* left = nullptr;

  // Grid coordinates — needed to rebuild the grid array after SemiSpace GC.
  int gx = -1, gy = -1;

  Node() {}
  void trace(ObjectStack& st) const
  {
    if (down != nullptr)
      st.push(down);

    if (right != nullptr)
      st.push(right);

    // TODO figure out if we should remove this or not? .... something about
    // weak references
    if (up != nullptr)
      st.push(up);

    if (left != nullptr)
      st.push(left);
  }

  // SemiSpace GC: forward all four neighbor pointers.
  void relocate(Object* (*fwd)(Object*))
  {
    if (down != nullptr)
      down = (Node*)fwd(down);
    if (right != nullptr)
      right = (Node*)fwd(right);
    if (up != nullptr)
      up = (Node*)fwd(up);
    if (left != nullptr)
      left = (Node*)fwd(left);
  }
};

int numInaccessible(Node* root, int gridsize)
{
  std::unordered_set<Node*> seen;
  std::queue<Node*> next;
  next.push(root);
  while (!next.empty())
  {
    Node* cur = next.front();
    next.pop();
    if (seen.find(cur) != seen.end())
    { // if already seen
      continue;
    }
    seen.insert(cur); // not seen yet
    if (cur->down && seen.find(cur->down) == seen.end())
    {
      next.push(cur->down);
    }
    if (cur->right && seen.find(cur->right) == seen.end())
    {
      next.push(cur->right);
    }
    if (cur->up && seen.find(cur->up) == seen.end())
    {
      next.push(cur->up);
    }
    if (cur->left && seen.find(cur->left) == seen.end())
    {
      next.push(cur->left);
    }
  }
  return gridsize * gridsize - seen.size();
}

template<RegionType rt>
void kill_link_up(Node* n)
{
  if (!n->up)
    return;
  Node* old_up = n->up;
  old_up->down = nullptr;
  n->up = nullptr;
  if constexpr (rt == RegionType::Rc)
  {
    decref(n);       // old_up->down no longer references n
    decref(old_up);  // n->up no longer references old_up
  }
}

template<RegionType rt>
void kill_link_right(Node* n)
{
  if (!n->right)
    return;
  Node* old_right = n->right;
  old_right->left = nullptr;
  n->right = nullptr;
  if constexpr (rt == RegionType::Rc)
  {
    decref(n);         // old_right->left no longer references n
    decref(old_right); // n->right no longer references old_right
  }
}

template<RegionType rt>
void kill_link_down(Node* n)
{
  if (!n->down)
    return;
  Node* old_down = n->down;
  old_down->up = nullptr;
  n->down = nullptr;
  if constexpr (rt == RegionType::Rc)
  {
    decref(n);        // old_down->up no longer references n
    decref(old_down); // n->down no longer references old_down
  }
}

template<RegionType rt>
void kill_link_left(Node* n)
{
  if (!n->left)
    return;
  Node* old_left = n->left;
  old_left->right = nullptr;
  n->left = nullptr;
  if constexpr (rt == RegionType::Rc)
  {
    decref(n);       // old_left->right no longer references n
    decref(old_left); // n->left no longer references old_left
  }
}

template<RegionType rt>
void isolate_node(Node* n)
{
  kill_link_up<rt>(n);
  kill_link_right<rt>(n);
  kill_link_down<rt>(n);
  kill_link_left<rt>(n);
}

enum class dir
{
  DOWN,
  RIGHT,
  UP,
  LEFT
};

/**
 * Rebuild the grid array from root via BFS after SemiSpace GC.
 * Each Node carries its (gx, gy) coordinates, so we can place it
 * back into grid[gy * gridsize + gx].
 */
inline void rebuild_grid(Node* root, Node** grid, int gridsize)
{
  // Clear grid first
  for (int i = 0; i < gridsize * gridsize; i++)
    grid[i] = nullptr;

  std::unordered_set<Node*> seen;
  std::queue<Node*> q;
  q.push(root);
  seen.insert(root);

  while (!q.empty())
  {
    Node* cur = q.front();
    q.pop();

    if (cur->gx >= 0 && cur->gy >= 0)
      grid[cur->gy * gridsize + cur->gx] = cur;

    Node* neighbors[] = {cur->down, cur->right, cur->up, cur->left};
    for (Node* n : neighbors)
    {
      if (n && seen.find(n) == seen.end())
      {
        seen.insert(n);
        q.push(n);
      }
    }
  }
}

template<RegionType rt>
void run_test(int gridsize, int numsteps, int numwalkers)
{
  // create grid
  Node** grid = new Node*[gridsize * gridsize];

  grid[0] = new (rt) Node;
  Node* root = grid[0];

  {
    UsingRegion rr(root);

    for (int i = 0; i < gridsize; i++)
    {
      for (int j = 0; j < gridsize; j++)
      {
        if (i == 0 && j == 0)
        {
          root->gx = 0;
          root->gy = 0;
          continue;
        }
        grid[i * gridsize + j] = new Node;
        grid[i * gridsize + j]->gx = j;
        grid[i * gridsize + j]->gy = i;
      }
    }

    // linking the grid:
    // horizontal linking:
    for (int i = 0; i < gridsize; i++)
    {
      for (int j = 0; j < gridsize - 1; j++)
      {
        grid[i * gridsize + j]->right = grid[i * gridsize + j + 1];
        if constexpr (rt == RegionType::Rc)
          incref(grid[i * gridsize + j + 1]);
      }
      for (int j = gridsize - 1; j > 0; j--)
      {
        grid[i * gridsize + j]->left = grid[i * gridsize + j - 1];
        if constexpr (rt == RegionType::Rc)
          incref(grid[i * gridsize + j - 1]);
      }
    }

    for (int j = 0; j < gridsize; j++)
    {
      for (int i = 0; i < gridsize - 1; i++)
      {
        grid[i * gridsize + j]->down = grid[(i + 1) * gridsize + j];
        if constexpr (rt == RegionType::Rc)
          incref(grid[(i + 1) * gridsize + j]);
      }
      for (int i = gridsize - 1; i > 0; i--)
      {
        grid[i * gridsize + j]->up = grid[(i - 1) * gridsize + j];
        if constexpr (rt == RegionType::Rc)
          incref(grid[(i - 1) * gridsize + j]);
      }
    }
    // grid initialised.

    // RC: compensate surplus rc=1 on non-root nodes.
    if constexpr (rt == RegionType::Rc)
    {
      for (int idx = 1; idx < gridsize * gridsize; idx++)
        decref(grid[idx]);
    }

    std::random_device rd;
    std::mt19937 gen(rd()); // mersenne twister engine

    // SemiSpace GC: Track walker positions as grid indices instead of raw
    // Node* pointers, because interior pointers become stale after
    // region_collect(). We look up the current node from grid[] each step.
    int* walker_idx = new int[numwalkers];
    std::uniform_int_distribution<size_t> cdist(0, gridsize - 1);
    for (int i = 0; i < numwalkers; i++)
    {
      // walkers assigned random position.
      walker_idx[i] = static_cast<int>(cdist(gen) * gridsize + cdist(gen));
    }

    for (int i = 0; i < numsteps; i++)
    {
      for (int j = 0; j < numwalkers; j++)
      {
        Node* walker = grid[walker_idx[j]];

        // Walker's node may have been collected — reassign to a random cell.
        if (walker == nullptr)
        {
          walker_idx[j] = static_cast<int>(cdist(gen) * gridsize + cdist(gen));
          walker = grid[walker_idx[j]];
          if (walker == nullptr)
            continue; // still null, skip this step
        }

        std::uniform_int_distribution<size_t> bdist(0, 1);
        bool destroyLink = true;

        std::vector<dir> options;
        if (walker->down)
          options.push_back(dir::DOWN);
        if (walker->right)
          options.push_back(dir::RIGHT);
        if (walker->up)
          options.push_back(dir::UP);
        if (walker->left)
          options.push_back(dir::LEFT);

        if (options.size() == 0)
        {
        //   std::cout << "walker " << j << " is softlocked\n";
          walker_idx[j] = static_cast<int>(cdist(gen) * gridsize + cdist(gen));
          continue;
        }
        std::uniform_int_distribution<size_t> dist(0, options.size() - 1);
        dir choice = options[dist(gen)];
        // std::cout << "walker " << j << " is ";
        switch (choice)
        {
          case dir::DOWN:
            // std::cout << "walking down\n";
            walker = walker->down;
            if (destroyLink)
              kill_link_up<rt>(walker); // messy... i know
            break;
          case dir::RIGHT:
            // std::cout << "walking right\n";
            walker = walker->right;
            if (destroyLink)
              kill_link_left<rt>(walker);
            break;
          case dir::UP:
            // std::cout << "walking up\n";
            walker = walker->up;
            if (destroyLink)
              kill_link_down<rt>(walker);
            break;
          case dir::LEFT:
            // std::cout << "walking left\n";
            walker = walker->left;
            if (destroyLink)
              kill_link_right<rt>(walker);
            break;
        }
        // Update walker index from the node's stored coordinates.
        walker_idx[j] = walker->gy * gridsize + walker->gx;
      }

      int dead = numInaccessible(root, gridsize);
      region_collect();

      // SemiSpace GC: region_collect() may relocate Node objects.
      // Rebuild grid[] from root via BFS (each Node stores its coordinates).
      rebuild_grid(root, grid, gridsize);

      // Relocate any walker whose node was collected (grid entry is now null).
      for (int w = 0; w < numwalkers; w++)
      {
        if (grid[walker_idx[w]] == nullptr)
          walker_idx[w] = static_cast<int>(cdist(gen) * gridsize + cdist(gen));
      }

      int alive = debug_size();
      // std::cout << "unreachable: " << dead << ", reachable: " << alive
      //           << std::endl;
      check(
        dead + alive ==
        gridsize * gridsize); // <<< thats where testing actually happens.
    }

    // RC: sever all bidirectional links to/from the root so that its
    // internal reference count drops back to 0, leaving only the single
    // external (entry-point) reference.  release_internal() expects
    // entry_point_count == 1 and will abort() otherwise.
    // Note: Might be best to later find a way to not have the root be part of
    // the core algorithm/be able to be referenced as the root's refcount
    // appears to be managed internally and manual modification might cause
    // issues.
    if constexpr (rt == RegionType::Rc)
    {
      isolate_node<rt>(root);
    }

    delete[] grid;
    delete[] walker_idx;
  }
  region_release(root);
}
