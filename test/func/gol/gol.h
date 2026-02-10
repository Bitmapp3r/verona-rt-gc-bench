// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <debug/harness.h>
#include <iostream>
#include <vector>
#include <verona.h>

using namespace snmalloc;
using namespace verona::rt;
using namespace verona::rt::api;

namespace gol
{
  struct Cell : public V<Cell>
  {
    int x, y;
    Cell(int x_, int y_) : x(x_), y(y_) {}
    void trace(ObjectStack& st) const {}
  };

  struct SimRoot : public V<SimRoot>
  {
    std::vector<Cell*> live_cells;

    void trace(ObjectStack& st) const
    {
      for (auto* c : live_cells)
      {
        if (c)
          st.push(c);
      }
    }
  };

  inline int
  count_neighbors(const std::vector<Cell*>& grid, int size, int x, int y)
  {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++)
    {
      for (int dx = -1; dx <= 1; dx++)
      {
        if (dx == 0 && dy == 0)
          continue;
        int nx = (x + dx + size) % size;
        int ny = (y + dy + size) % size;
        if (grid[ny * size + nx] != nullptr)
          count++;
      }
    }
    return count;
  }

  inline void run_test(int size, int generations)
  {
    // Use our new container root
    auto* root = new (RegionType::Trace) SimRoot();

    {
      UsingRegion rr(root);

      std::vector<Cell*> current_grid(size * size, nullptr);
      std::vector<Cell*> next_grid(size * size, nullptr);

      auto set_cell = [&](int x, int y) {
        if (x < size && y < size)
          current_grid[y * size + x] = new Cell(x, y);
      };

      // Initialize R-pentomino pattern
      int cx = size / 2;
      int cy = size / 2;
      set_cell(cx + 1, cy);
      set_cell(cx + 2, cy);
      set_cell(cx, cy + 1);
      set_cell(cx + 1, cy + 1);
      set_cell(cx + 1, cy + 2);

      root->live_cells = current_grid;

      std::cout << "Game of Life initialized. Grid: " << size << "x" << size
                << "\n";
      check(debug_size() == 6);

      for (int gen = 0; gen < generations; gen++)
      {
        for (int y = 0; y < size; y++)
        {
          for (int x = 0; x < size; x++)
          {
            int neighbors = count_neighbors(current_grid, size, x, y);
            Cell* current_cell = current_grid[y * size + x];

            if (current_cell)
            {
              // Survive Rule (Allocates NEW to create garbage)
              if (neighbors == 2 || neighbors == 3)
                next_grid[y * size + x] = new Cell(x, y);
              else
                next_grid[y * size + x] = nullptr;
            }
            else
            {
              // Birth Rule
              if (neighbors == 3)
                next_grid[y * size + x] = new Cell(x, y);
              else
                next_grid[y * size + x] = nullptr;
            }
          }
        }

        current_grid = next_grid;
        root->live_cells = current_grid;

        int heap_size = debug_size();
        std::cout << "Heap size before region collect: " << heap_size << "\n";

        region_collect();

        int actual_alive_count = 0;
        for (auto* c : current_grid)
        {
          if (c)
            actual_alive_count++;
        }

        heap_size = debug_size();
        std::cout << "Heap size after region collect: " << heap_size << "\n";

        if (heap_size != actual_alive_count + 1)
        { // +1 for SimRoot
          std::cout << "FAILURE at Gen " << gen << "\n";
          std::cout << "Heap: " << heap_size
                    << " | Expected: " << (actual_alive_count + 1) << "\n";
          check(heap_size == actual_alive_count + 1);
        }
      }
      std::cout << "Simulation survived " << generations << " generations.\n";
    }
    region_release(root);
    heap::debug_check_empty();
  }

  void run_test()
  {
    run_test(8, 10);
  }

}