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

/*

Tests RegionTrace by creating a grid of nodes and deleting edges such that
nodes become unreachable from the root. These unreachable should be freed
by the garbage collector. At every step, we check:
    the number of unreachable nodes == number of freed nodes

We have a number of "walkers" walking across the grid, destroying edges where
they move. the root of the grid is the top left node.

grid size, number of steps to simulate and number of walkers are configurable.


*/

class Node : public V<Node>
{
public:
  Node* down = nullptr;
  Node* right = nullptr;

  Node* up = nullptr;
  Node* left = nullptr;

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

void kill_link_up(Node* n)
{
  if (!n->up)
    return;
  n->up->down = nullptr;
  n->up = nullptr;
}

void kill_link_right(Node* n)
{
  if (!n->right)
    return;
  n->right->left = nullptr;
  n->right = nullptr;
}
void kill_link_down(Node* n)
{
  if (!n->down)
    return;
  n->down->up = nullptr;
  n->down = nullptr;
}

void kill_link_left(Node* n)
{
  if (!n->left)
    return;
  n->left->right = nullptr;
  n->left = nullptr;
}

void isolate_node(Node* n)
{
  kill_link_up(n);
  kill_link_right(n);
  kill_link_down(n);
  kill_link_left(n);
}

enum class dir
{
  DOWN,
  RIGHT,
  UP,
  LEFT
};

void test_walker(int gridsize, int numsteps, int numwalkers)
{
  // create grid
  Node** grid = new Node*[gridsize * gridsize];

  grid[0] = new (RegionType::Trace) Node;
  Node* root = grid[0];

  {
    UsingRegion rr(root);

    for (int i = 0; i < gridsize; i++)
    {
      for (int j = 0; j < gridsize; j++)
      {
        if (i == 0 && j == 0)
          continue;
        grid[i * gridsize + j] = new Node;
      }
    }

    // linking the grid:
    // horizontal linking:
    for (int i = 0; i < gridsize; i++)
    {
      for (int j = 0; j < gridsize - 1; j++)
      {
        grid[i * gridsize + j]->right = grid[i * gridsize + j + 1];
      }
      for (int j = gridsize - 1; j > 0; j--)
      {
        grid[i * gridsize + j]->left = grid[i * gridsize + j - 1];
      }
    }

    for (int j = 0; j < gridsize; j++)
    {
      for (int i = 0; i < gridsize - 1; i++)
      {
        grid[i * gridsize + j]->down = grid[(i + 1) * gridsize + j];
      }
      for (int i = gridsize - 1; i > 0; i--)
      {
        grid[i * gridsize + j]->up = grid[(i - 1) * gridsize + j];
      }
    }
    // grid initialised.

    std::random_device rd;
    std::mt19937 gen(rd()); // mersenne twister engine

    Node** walkers = new Node*[numwalkers];
    std::uniform_int_distribution<size_t> cdist(0, gridsize - 1);
    for (int i = 0; i < numwalkers; i++)
    {
      // walkers assigned random position.
      walkers[i] = grid[cdist(gen) * gridsize + cdist(gen)];
    }

    for (int i = 0; i < numsteps; i++)
    {
      for (int j = 0; j < numwalkers; j++)
      {
        Node* walker = walkers[j];

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
          std::cout << "walker " << j << " is softlocked\n";
          walkers[j] = grid[cdist(gen) * gridsize + cdist(gen)];
          continue;
        }
        std::uniform_int_distribution<size_t> dist(0, options.size() - 1);
        dir choice = options[dist(gen)];
        std::cout << "walker " << j << " is ";
        switch (choice)
        {
          case dir::DOWN:
            std::cout << "walking down\n";
            walker = walker->down;
            if (destroyLink)
              kill_link_up(walker); // messy... i know
            break;
          case dir::RIGHT:
            std::cout << "walking right\n";
            walker = walker->right;
            if (destroyLink)
              kill_link_left(walker);
            break;
          case dir::UP:
            std::cout << "walking up\n";
            walker = walker->up;
            if (destroyLink)
              kill_link_down(walker);
            break;
          case dir::LEFT:
            std::cout << "walking left\n";
            walker = walker->left;
            if (destroyLink)
              kill_link_right(walker);
            break;
        }
      }

      int dead = numInaccessible(root, gridsize);
      region_collect();
      int alive = debug_size();
      std::cout << "unreachable: " << dead << ", reachable: " << alive
                << std::endl;
      check(
        dead + alive ==
        gridsize * gridsize); // <<< thats where testing actually happens.
    }

    delete[] grid;
  }
}