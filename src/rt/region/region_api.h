// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "freeze.h"
#include "region.h"

#include <debug/logging.h>
#include <functional>
#include <test/measuretime.h>

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
      std::function<void(uint64_t, RegionType, size_t, size_t)>* gc_callback;

    public:
      static RegionContext& get_region_context()
      {
        static thread_local RegionContext context;
        return context;
      }

      /**
       * Set a GC measurement callback for this thread's region context.
       * Callback receives: duration_ns, region_type, memory_bytes, object_count
       * Pass nullptr to disable collection and return to default logging.
       */
      static void set_gc_callback(
        std::function<void(uint64_t, RegionType, size_t, size_t)>* callback)
      {
        get_region_context().gc_callback = callback;
      }

      /**
       * Get the current GC measurement callback, or nullptr if none is set.
       */
      static std::function<void(uint64_t, RegionType, size_t, size_t)>*
      get_gc_callback()
      {
        return get_region_context().gc_callback;
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
    }
    // Unreachable as case is exhaustive
    abort();
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

    // Capture memory before operation for metrics
    size_t mem_before =
      ((RegionRc*)RegionContext::get_region())->get_current_memory_used();
    size_t obj_before =
      ((RegionRc*)RegionContext::get_region())->get_region_size();

    MeasureTime m(true);
    RegionRc::decref(o, (RegionRc*)RegionContext::get_region());

    uint64_t duration_ns = m.get_time().count();
    auto* callback = RegionContext::get_gc_callback();

    if (callback != nullptr)
    {
      // Route measurement to callback (for testing/metrics gathering)
      (*callback)(duration_ns, RegionType::Rc, mem_before, obj_before);
    }
    else
    {
      // Default logging behavior
      Logging::cout() << "Decref time: " << duration_ns << " ns"
                      << Logging::endl;
    }
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
    }
    RegionContext::get_entry_point() = o;
  }

  inline void region_collect()
  {
    RegionType type = Region::get_type(RegionContext::get_region());

    // Capture memory before GC for metrics
    size_t mem_before = 0;
    size_t obj_before = 0;
    switch (type)
    {
      case RegionType::Trace:
        mem_before = ((RegionTrace*)RegionContext::get_region())
                       ->get_current_memory_used();
        for (auto p : *((RegionTrace*)RegionContext::get_region()))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Arena:
        mem_before = ((RegionArena*)RegionContext::get_region())
                       ->get_current_memory_used();
        for (auto p : *((RegionArena*)RegionContext::get_region()))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Rc:
        mem_before =
          ((RegionRc*)RegionContext::get_region())->get_current_memory_used();
        obj_before =
          ((RegionRc*)RegionContext::get_region())->get_region_size();
        break;
    }

    MeasureTime m(true);

    switch (type)
    {
      case RegionType::Trace:
        // Other roots?
        RegionTrace::gc(RegionContext::get_entry_point());
        break;
      case RegionType::Arena:
        // Nothing to collect here!
        break;
      case RegionType::Rc:
        RegionRc::gc_cycles(
          RegionContext::get_entry_point(),
          (RegionRc*)RegionContext::get_region());
        break;
    }

    uint64_t duration_ns = m.get_time().count();
    auto* callback = RegionContext::get_gc_callback();

    if (callback != nullptr)
    {
      // Route measurement to callback (for testing/metrics gathering)
      (*callback)(duration_ns, type, mem_before, obj_before);
    }
    else
    {
      // Default logging behavior
      Logging::cout() << "Region GC/Dealloc time: " << duration_ns << " ns"
                      << Logging::endl;
    }
  }

  template<typename T = Object>
  inline void region_release(Object* r)
  {
    RegionType type = Region::get_type(r->get_region());

    // Capture memory before release for metrics
    size_t mem_before = 0;
    size_t obj_before = 0;
    switch (type)
    {
      case RegionType::Trace:
        mem_before = ((RegionTrace*)r->get_region())->get_current_memory_used();
        for (auto p : *((RegionTrace*)r->get_region()))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Arena:
        mem_before = ((RegionArena*)r->get_region())->get_current_memory_used();
        for (auto p : *((RegionArena*)r->get_region()))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Rc:
        mem_before = ((RegionRc*)r->get_region())->get_current_memory_used();
        obj_before = ((RegionRc*)r->get_region())->get_region_size();
        break;
    }

    MeasureTime m(true);
    Region::release(r);

    uint64_t duration_ns = m.get_time().count();
    auto* callback = RegionContext::get_gc_callback();

    if (callback != nullptr)
    {
      // Route measurement to callback (for testing/metrics gathering)
      (*callback)(duration_ns, type, mem_before, obj_before);
    }
    else
    {
      // Default logging behavior
      Logging::cout() << "Region release time: " << duration_ns << " ns"
                      << Logging::endl;
    }
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
      default:
        abort();
    }
  }
} // namespace verona::rt