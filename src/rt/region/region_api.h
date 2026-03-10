// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "freeze.h"
#include "region.h"

#include <debug/logging.h>

namespace verona::rt::api
{
  namespace internal
  {
    class RegionContext
    {
      struct RegionFrame
      {
        Object* entry_point;
        RegionBase* region;
        RegionFrame* prev;
      };

      RegionFrame* top;

    public:
      static RegionContext& get_region_context()
      {
        static thread_local RegionContext context;
        return context;
      }

      static void push(Object* entry_point, RegionBase* region)
      {
        RegionFrame* frame = new RegionFrame();
        frame->entry_point = entry_point;
        frame->region = region;
        frame->prev = get_region_context().top;
        get_region_context().top = frame;
      }

      static void pop()
      {
        auto& t = get_region_context();
        RegionFrame* frame = t.top;
        t.top = frame->prev;
        delete frame;
      }

      static Object*& get_entry_point()
      {
        return get_region_context().top->entry_point;
      }

      static RegionBase* get_region()
      {
        return get_region_context().top->region;
      }
    };
  }

  using namespace internal;

  /**
   * Check if pointer points to a new region.
   */
  inline bool is_region_ref(Object* o)
  {
    // Check if iso
    if (!o->debug_is_iso())
      return false;
    // Check for entry point
    return RegionContext::get_entry_point() != o;
  }

  /**
   * Open supplied region, and return entry point.
   */
  inline void open_region(Object* r)
  {
    assert(r->debug_is_iso());
    auto md = r->get_region();
    RegionContext::push(r, md);
    switch (Region::get_type(md))
    {
      case RegionType::Trace:
      case RegionType::Arena:
      case RegionType::SemiSpace:
        break;
      case RegionType::Rc:
        ((RegionRc*)md)->open(r);
        break;
      default:
        abort();
    }
  }

  /**
   * Close current region
   */
  inline void close_region()
  {
    auto md = RegionContext::get_region();
    switch (Region::get_type(md))
    {
      case RegionType::Trace:
      case RegionType::Arena:
      case RegionType::SemiSpace:
        break;
      case RegionType::Rc:
        ((RegionRc*)md)->close(RegionContext::get_entry_point());
        break;
      default:
        abort();
    }
    RegionContext::pop();
  }

  class UsingRegion
  {
  public:
    UsingRegion(Object* r)
    {
      open_region(r);
    }

    ~UsingRegion()
    {
      // TODO: Check if we are in the same region as the one we opened.
      close_region();
    }
  };

  /**
   * Freeze region
   */
  template<typename T = Object>
  inline T* freeze(T* r)
  {
    // Check for trace region.
    Freeze::apply(r);
    return r;
  }

  /**
   * Add supplied region to the current region
   * and return the entry point.
   */
  template<typename T = Object>
  inline T* merge(T* r)
  {
    // Confirm regions are the same type
    assert(
      Region::get_type(r->get_region()) ==
      Region::get_type(RegionContext::get_region()));

    switch (Region::get_type(r->get_region()))
    {
      case RegionType::Trace:
        RegionTrace::merge(RegionContext::get_entry_point(), r);
        return r;
      case RegionType::Arena:
        RegionArena::merge(RegionContext::get_entry_point(), r);
        return r;
      case RegionType::Rc:
        abort();
      case RegionType::SemiSpace:
        abort(); // Merge not supported for semi-space regions
    }
    abort();
  }

  /**
   * Create external reference to o in the current region.
   */
  inline ExternalRef* create_external_reference(Object* o)
  {
    return ExternalRef::create(RegionContext::get_region(), o);
  }

  /**
   * Check if external reference is in the current region and still valid.
   */
  inline bool is_external_reference_valid(ExternalRef* e)
  {
    return e->is_in(RegionContext::get_region());
  }

  /**
   * Use external reference e, and return the object in the current region.
   */
  inline Object* use_external_reference(ExternalRef* e)
  {
    assert(is_external_reference_valid(e));
    return e->get();
  }

  /**
   * Create object in current region
   */
  inline Object* create_object(const Descriptor* d)
  {
    // Case analysis on type of region
    switch (Region::get_type(RegionContext::get_region()))
    {
      case RegionType::Trace:
        return RegionTrace::alloc(RegionContext::get_entry_point(), d);
      case RegionType::Arena:
        return RegionArena::alloc(RegionContext::get_entry_point(), d);
      case RegionType::Rc:
        return RegionRc::alloc((RegionRc*)RegionContext::get_region(), d);
      case RegionType::SemiSpace:
      {
        // The iso root is pinned on the heap and never moves during
        // growth, so no entry-point adjustment is needed.
        return RegionSemiSpace::alloc(
          RegionContext::get_entry_point(), d);
      }
    }
    // Unreachable as case is exhaustive
    abort();
  }

  /**
   * Ensure that at least `bytes` of bump-allocation capacity is
   * available in the current SemiSpace region's from-space.
   *
   * If the space needs to grow, it grows now (moving existing
   * objects), so the caller must call get_root() / get_entry_point()
   * afterwards to obtain the updated root pointer.  After that,
   * subsequent allocations totalling up to `bytes` are guaranteed
   * not to trigger another growth, keeping all returned pointers
   * valid.
   *
   * Only meaningful for SemiSpace regions; asserts on other types.
   **/
  inline void region_ensure_available(size_t bytes)
  {
    assert(
      Region::get_type(RegionContext::get_region()) == RegionType::SemiSpace);

    // The iso root is pinned on the heap and never moves during
    // growth, so no entry-point adjustment is needed.
    RegionSemiSpace::ensure_available(
      RegionContext::get_entry_point(), bytes);
  }

  inline void add_reference(Object*)
  {
    // TODO
  }

  inline void remove_reference(Object*)
  {
    // TODO
  }

  inline void incref(Object* o)
  {
    assert(Region::get_type(RegionContext::get_region()) == RegionType::Rc);
    RegionRc::incref(o);
  }

  inline void decref(Object* o)
  {
    assert(Region::get_type(RegionContext::get_region()) == RegionType::Rc);

    with_region_stats(RegionContext::get_region(), "Decref", [&]() {
      RegionRc::decref(o, (RegionRc*)RegionContext::get_region());
    });
  }

  template<typename T = Object>
  inline T* create_fresh_region(RegionType type, const Descriptor* d)
  {
    Object* entry_point = nullptr;
    switch (type)
    {
      case RegionType::Trace:
        entry_point = RegionTrace::create(d);
        break;
      case RegionType::Arena:
        entry_point = RegionArena::create(d);
        break;
      case RegionType::Rc:
        entry_point = RegionRc::create(d);
        break;
      case RegionType::SemiSpace:
        entry_point = RegionSemiSpace::create(d);
        break;
    }
    return {reinterpret_cast<T*>(entry_point)};
  }

  inline void set_entry_point(Object* o)
  {
    switch (Region::get_type(RegionContext::get_region()))
    {
      case RegionType::Trace:
        RegionTrace::swap_root(RegionContext::get_entry_point(), o);
        break;
      case RegionType::Arena:
        RegionArena::swap_root(RegionContext::get_entry_point(), o);
        break;
      case RegionType::Rc:
        abort(); // TODO
        break;
      case RegionType::SemiSpace:
        RegionSemiSpace::swap_root(RegionContext::get_entry_point(), o);
        break;
    }
    RegionContext::get_entry_point() = o;
  }

  inline void region_collect()
  {
    RegionType type = Region::get_type(RegionContext::get_region());
    Object* entry = RegionContext::get_entry_point();

    if (type == RegionType::Arena)
      return; // Arena has no GC to collect; skip measurement overhead.

    with_region_stats(RegionContext::get_region(), "Region collect", [&]() {
      switch (type)
      {
        case RegionType::Trace:
          RegionTrace::gc(entry);
          break;
        case RegionType::Rc:
          RegionRc::gc_cycles(
            entry,
            (RegionRc*)RegionContext::get_region());
          break;
        case RegionType::SemiSpace:
          RegionSemiSpace::gc(
            entry,
            (RegionSemiSpace*)RegionContext::get_region());
          break;
        default:
          break;
      }
    });
  }

  template<typename T = Object>
  inline void region_release(Object* r)
  {
    with_region_stats(r->get_region(), "Region release", [&]() {
          Region::release(r);
        });
  }

  /**
   * Return the size of the current region.
   *
   * For testing and debugging purposes only.
   **/
  inline size_t debug_size()
  {
    RegionBase* r = RegionContext::get_region();
    size_t count = 0;
    switch (Region::get_type(r))
    {
      case RegionType::Trace:
        for (auto p : *((RegionTrace*)r))
        {
          UNUSED(p);
          count++;
        }
        return count;
      case RegionType::Arena:
        for (auto p : *((RegionArena*)r))
        {
          UNUSED(p);
          count++;
        }
        return count;
      case RegionType::Rc:
        return ((RegionRc*)r)->get_region_size();
      case RegionType::SemiSpace:
        for (auto p : *((RegionSemiSpace*)r))
        {
          UNUSED(p);
          count++;
        }
        return count;
      default:
        abort();
    }
  }

  /**
   * Return the memory used by the current region in bytes.
   *
   * For testing and debugging purposes only.
   **/
  inline size_t debug_memory_used()
  {
    RegionBase* r = RegionContext::get_region();
    switch (Region::get_type(r))
    {
      case RegionType::Trace:
        return ((RegionTrace*)r)->get_current_memory_used();
      case RegionType::Arena:
        return ((RegionArena*)r)->get_current_memory_used();
      case RegionType::Rc:
        return ((RegionRc*)r)->get_current_memory_used();
      case RegionType::SemiSpace:
        return ((RegionSemiSpace*)r)->get_current_memory_used();
      default:
        abort();
    }
  }

  /**
   * Return the current semi-space size for a SemiSpace region.
   * Aborts if called on a non-SemiSpace region.
   * For testing and debugging purposes only.
   **/
  inline size_t debug_semispace_size()
  {
    RegionBase* r = RegionContext::get_region();
    assert(Region::get_type(r) == RegionType::SemiSpace);
    return ((RegionSemiSpace*)r)->get_semispace_size();
  }

  /**
   * Return the number of objects in the large object list.
   * Aborts if called on a non-SemiSpace region.
   * For testing and debugging purposes only.
   **/
  inline size_t debug_large_object_count()
  {
    RegionBase* r = RegionContext::get_region();
    assert(Region::get_type(r) == RegionType::SemiSpace);
    return ((RegionSemiSpace*)r)->get_large_object_count();
  }

  /**
   * Return the number of bytes used in from-space.
   * Aborts if called on a non-SemiSpace region.
   * For testing and debugging purposes only.
   **/
  inline size_t debug_fromspace_used()
  {
    RegionBase* r = RegionContext::get_region();
    assert(Region::get_type(r) == RegionType::SemiSpace);
    return ((RegionSemiSpace*)r)->get_fromspace_used();
  }
} // namespace verona::rt