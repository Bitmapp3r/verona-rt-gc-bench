// Copyright Microsoft and Project Ververona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../memory/memory.h" // Mainly for the C3 objects

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
   * c1 = next pointer (->), c2 = prev pointer (<-)
   */
  
  // Inserts entry at front of cache - between head and previous newest elem
  void insert(C3* head, C3* entry)
  {
    entry->c1 = head->c1;
    entry->c2 = head;

    // should check head->c1 isn't null?
    head->c1->c2 = entry;
    head->c1 = entry;
  }

  // Remove last entry + return it
  C3* remove(C3* tail)
  {
    C3* last = tail->c2;

    last->c2->c1 = tail;
    tail->c2 = last->c2;

    last->c1 = nullptr;
    last->c2 = nullptr;

    return last;
  }


  // moves entry to front (e.g., when it's accessed)
  void move_to_front(C3* head, C3* entry)
  {
    entry->c1->c2 = entry->c2;
    entry->c2->c1 = entry->c1;

    insert(head, entry);
  }


  void test_lru_cache()
  {
    auto* head = new (RegionType::Trace) C3; 
    
    {
      UsingRegion rr(head);
      
      // Setup basic LRU cache structure : head <-> tail
      auto* tail = new C3;
      head->c1 = tail;
      tail->c2 = head;

      check(debug_size() == 2); // head + tail
      
      // Fill cache to 3 entries
      auto* entry1 = new C3;
      auto r1 = new (RegionType::Trace) F3;
      {
        UsingRegion ur(r1);
        entry1->f1 = r1;
        entry1->f2 = new F3;
      }

      auto* entry2 = new C3;
      auto r2 = new (RegionType::Trace) F3;
      {
        UsingRegion ur(r2);
        entry2->f1 = r2;
        entry2->f2 = new F3;
      }

      auto* entry3 = new C3;
      auto r3 = new (RegionType::Trace) F3;
      {
        UsingRegion ur(r3);
        entry3->f1 = r3;
        entry3->f2 = new F3;
      }

      insert(head, entry1); // oldest
      insert(head, entry2);
      insert(head, entry3);

      check(debug_size() == 5); // head + tail + 3 entries
      region_collect();
      check(debug_size() == 5); // nothing happened

      // Add new entry
      auto* entry4 = new C3;
      auto r4 = new (RegionType::Trace) F3;
      {
        UsingRegion ur(r4);
        entry4->f1 = r4;
        entry4->f2 = new F3;
      }
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
      head->c1 = tail;
      tail->c2 = head;

      check(debug_size() == 4); // no change
      region_collect();
      check(debug_size() == 2); // only head + tail remain
    }
    
    region_release(head);
    // heap::debug_check_empty(); // BROKEN
  }

  void run_test()
  {
    test_lru_cache();
  }
}
