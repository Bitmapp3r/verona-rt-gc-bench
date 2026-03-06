// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "freeze.h"
#include "region.h"

#include "../cpp/behaviour.h"
#include <debug/logging.h>
#include <functional>
#include <atomic>

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

  /*
  regions can be in one 3 states: Open. Closed, Collecting
  4 state transitions:
  normal behaviours:
  Closed -> Open
  Open -> Closed


  GCing:
  Closed -> Collecting
  Collecting -> Closed

  when closing a region, we schedule a gc task
  in future, we'll only schedule when the region size goes above a threshold.
  and we'll only haev 1 gc task in flight for each region.

  race conditions:
  open region <---> gc task

  open region is used by both the normal behaviours and gc task.
  same with close region



  issue:
  race condition between region_release and gc task
  TOCTTOA bug
  basically we don't wanna gc if the region is dead
  in gc task:
  if region not dead:
    open region for garbage collection

  ^^^ between those 2 lines, the region may be freed. the fix? reference count the region and the final
  user of the region will delete it. this may cause a redundant garbage collection call. but thats the best we can do.

  we changed code in region_api,

  we spawn the behaviour using behavior api

  for now open region may fail if we're in the wrong state. may change it to either spin loop or fail but reschedule.
  for opening a region for work we can't really reschedule...have to spin.
  */
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

  template<typename T = Object>
  void region_physical_release(Object* r);

  template<typename T = Object>
  inline void region_release(Object* r);


/**
   * Open supplied region, and return entry point.
   * Opening a region may fail if opening for collecting and already collecting or open.
   */
  inline bool open_region(Object* r, bool forWork = true)
  {
    assert(r->debug_is_iso());
    auto md = r->get_region();

    if (forWork) {
      // Transition: Closed -> Open (Spin-wait until Closed)
      auto expected = RegionBase::Closed;
      
      // std::memory_order_acquire ensures we see all memory writes from the thread that closed it.
      bool once = true;
      while (!md->state.compare_exchange_weak(expected, RegionBase::Open, std::memory_order_acquire)) {
        // If CAS fails, 'expected' is updated to the current state (Open or Collecting)
        snmalloc::Aal::pause();
        if (once) {
          std::cout << "opening region but region is already open (probably GCing)\n";
          once = false;
        }
        expected = RegionBase::Closed; // Reset for the next attempt
      }
      // Successfully in Open state.
      
    } else {
      // Transition: Closed -> Collecting (Try exactly once)
      auto expected = RegionBase::Closed;
      
      // If it's not Closed, we fail immediately and return false.
      if (!md->state.compare_exchange_strong(expected, RegionBase::Collecting, std::memory_order_acquire)) {
        std::cout << "Failed to open for GC. State was: " 
                        << (expected == RegionBase::Open ? "Open" : "Collecting") << "\n";
        return false;   
      }
      // Successfully in Collecting state.
    }

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
    return true;
  }



  void close_region(bool);

  class UsingRegion // GCing requires opening the region.
  {
    bool forWork;
  public:
    bool isOpen = false;
    UsingRegion(Object* r, bool forWork = true) : forWork(forWork)
    {
      isOpen = open_region(r, forWork);
    }

    ~UsingRegion()
    {
      // TODO: Check if we are in the same region as the one we opened. <--- not my TODO.
      if (isOpen) {
        close_region(forWork);
      }
    }
  };
  void region_collect();

inline bool check_gc_condition(Object* o);

inline void schedule_gc(Object* entry) {
    auto reg = entry->get_region();
    
    // Early exit if the region is already dead before we even schedule
    if (!reg->isAlive.load(std::memory_order_relaxed)) {
      return;
    }

    auto gc_task = [entry]() {
      RegionBase* reg = entry->get_region();
      if (!check_gc_condition(entry)) {
        goto task_dec;
      }
      // Check if region was killed while we were sitting in the scheduler queue
      if (reg->isAlive.load(std::memory_order_acquire)) {
        
        UsingRegion rr(entry, false); // false = forGC
        
        // rr.isOpen must be populated by the return value of open_region
        if (rr.isOpen) {
          std::cout << "RUNNING GC\n";
          region_collect();
          // Region is automatically closed by rr destructor here
        } else {
          Logging::cout() << "GC Task aborted: Region was busy.\n";
        }
      }

task_dec:
      // CRITICAL PATH: This always runs, preventing the "Early Return Leak"
      if (reg->task_dec()) {
        Logging::cout() << "Refcount hit 0 in GC Task. Physically releasing region.\n";
        region_physical_release(entry);
      }
    };

    auto* gc_behaviour = Behaviour::make(0, std::move(gc_task));
    assert(gc_behaviour != nullptr);
    Work* gc_work = gc_behaviour->as_work();
    assert(gc_work != nullptr);

    Logging::cout() << "Scheduling GC Task\n";
    
    // Increment the refcount BEFORE handing it off to the scheduler
    reg->task_inc();
    Scheduler::schedule(gc_work, true);
  }


  inline bool check_gc_condition(Object* o) {
    return true;
    assert(o->debug_is_iso());
    auto md = o->get_region();
    switch (Region::get_type(md)) {
      case RegionType::Trace:
        return RegionTrace::gc_condition(o);
      default:
        return true;
    }
  }


  /**
   * Close current region
   */
  inline void close_region(bool forWork = true)
  {
    auto md = RegionContext::get_region();
    Object* entry = RegionContext::get_entry_point();

    // std::memory_order_release ensures all our work inside the region is visible 
    // to the next thread that acquires it.
    if (forWork) {
      auto expected = RegionBase::Open;
      bool success = md->state.compare_exchange_strong(expected, RegionBase::Closed, std::memory_order_release);
      assert(success && "Fatal: Region was not Open when trying to close for Work");
    } else {
      auto expected = RegionBase::Collecting;
      bool success = md->state.compare_exchange_strong(expected, RegionBase::Closed, std::memory_order_release);
      assert(success && "Fatal: Region was not Collecting when trying to close for GC");
    }

    switch (Region::get_type(md))
    {
      case RegionType::Trace:
      case RegionType::Arena:
        break;
      case RegionType::Rc:
        ((RegionRc*)md)->close(entry);
        break;
      default:
        abort();
    }

    // Schedule GC after a normal behavior if conditions are met
    if (forWork && check_gc_condition(entry)) {
      schedule_gc(entry);
    }
    
    RegionContext::pop();
  }


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
    RegionBase* r = RegionContext::get_region();
    Object* entry = RegionContext::get_entry_point();

    with_region_stats(r, "Region collect", [&]() {
      switch (Region::get_type(r))
      {
        case RegionType::Trace:
          RegionTrace::gc(entry);
          break;
        case RegionType::Arena:
          // Nothing to collect here!
          break;
        case RegionType::Rc:
          RegionRc::gc_cycles(entry, (RegionRc*)r);
          break;
      }
    });
  }


  template<typename T>
  inline void region_physical_release(Object* r) {
    Logging::cout() << "reached region_physical_release on object: " << r << "\n";
    with_region_stats(r->get_region(), "Region release", [&]() {
      Region::release(r);
    });
  }


  template<typename T>
  inline void region_release(Object* r)
  {
    Logging::cout() << "reached region_release on object " << r << "\n";
    RegionBase* reg = r->get_region();
    reg->isAlive.store(false, std::memory_order_release);

    if (reg->task_dec()) {
      //Logging::cout() << "physically releasing region\n";
      region_physical_release(r);
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
