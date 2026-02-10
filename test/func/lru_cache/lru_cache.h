// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../memory/memory.h" // Mainly for the C1 objects

namespace lru_cache
{
  /**
   * Test garbage collection with LRU (Least Recently Used) Cache structures.
   *
   * This tests a simple cache implemented as a doubly linked list, repeatedly
   * using insertions, removals, and accesses, verifying that removed entries
   * are collected, and the remaining entries are live.
   *
   * Structure: head <-> newest <-> ... <-> oldest <-> tail
   *
   * f1 = next pointer (->), f2 = prev pointer (<-)
   */

  // Inserts entry at front of cache - between head and previous newest elem
  void insert(C1* head, C1* entry)
  {
    entry->f1 = head->f1;
    entry->f2 = head;

    // should check head->f1 isn't null?
    head->f1->f2 = entry;
    head->f1 = entry;
  }

  // Remove last entry + return it
  C1* remove(C1* tail)
  {
    C1* last = tail->f2;

    last->f2->f1 = tail;
    tail->f2 = last->f2;

    last->f1 = nullptr;
    last->f2 = nullptr;

    return last;
  }

  // moves entry to front (e.g., when it's accessed)
  void move_to_front(C1* head, C1* entry)
  {
    entry->f1->f2 = entry->f2;
    entry->f2->f1 = entry->f1;

    insert(head, entry);
  }

  void test_lru_cache()
  {
    auto* head = new (RegionType::Trace) C1;

    {
      UsingRegion rr(head);

      // Setup basic LRU cache structure : head <-> tail
      auto* tail = new C1;
      head->f1 = tail;
      tail->f2 = head;

      check(debug_size() == 2); // head + tail

      // Fill cache to 3 entries
      auto* entry1 = new C1;
      auto* entry2 = new C1;
      auto* entry3 = new C1;

      insert(head, entry1); // oldest
      insert(head, entry2);
      insert(head, entry3);

      check(debug_size() == 5); // head + tail + 3 entries
      region_collect();
      check(debug_size() == 5); // nothing happened

      // Add new entry
      auto* entry4 = new C1;
      insert(head, entry4);

      check(debug_size() == 6); // head + tail + 4 entries
      remove(tail); // removes entry1
      check(debug_size() == 6); // head + tail + 3 entries
      region_collect();
      check(debug_size() == 5); // entry1 collected

      move_to_front(head, entry2);
      check(debug_size() == 5); // no change

      remove(tail); // removes entry3
      check(debug_size() == 5); // no change
      region_collect();
      check(debug_size() == 4); // entry3 collected

      // Clear everything
      head->f1 = tail;
      tail->f2 = head;

      check(debug_size() == 4); // no change
      region_collect();
      check(debug_size() == 2); // only head + tail remain
    }

    region_release(head);
    heap::debug_check_empty();
  }

  void run_test()
  {
    test_lru_cache();
  }
}
