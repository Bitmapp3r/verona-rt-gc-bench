// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <functional>

#include "../object/object.h"
#include "externalreference.h"
#include "rememberedset.h"

namespace verona::rt
{
  using namespace snmalloc;

  /**
   * Please see region.h for the full documentation.
   *
   * This is the base class for concrete region implementations, and contains
   * all the common functionality. Because of difficulties with dependencies,
   * this class is intentionally minimal and contains no helpers---it is not
   * aware of any of the concrete region implementation classes.
   **/

  enum class RegionType
  {
    Trace,
    Arena,
    Rc,
    SemiSpace,
  };


  class RegionBase : public Object,
                     public ExternalReferenceTable,
                     public RememberedSet
  {
    friend class Freeze;
    friend class RegionTrace;
    friend class RegionArena;
    friend class RegionRc;
    friend class RegionSemiSpace;

  public:
    enum IteratorType
    {
      Trivial,
      NonTrivial,
      AllObjects,
    };

    enum ConcurrentState 
    {
      Open, 
      Closed,
      Collecting
    };

    std::atomic<ConcurrentState> state{Closed};
    std::atomic<size_t> owners{1};
    std::atomic<bool> isAlive{true};

    RegionBase() : Object() {
      //void* space = heap::alloc(sizeof(std::atomic<RegReleaseControl>));
      //release_control = new (space) std::atomic<RegReleaseControl>();
    }

    inline bool task_dec() { 
      int old_refcount = owners.fetch_sub(1, std::memory_order_acq_rel);
      Logging::cout() << "in task_dec: old_refcount = " << old_refcount << "\n";
      if (old_refcount == 1) {
        // actually free the region
        return true;
      }
      return false;
    }
    inline void task_inc() {
      owners.fetch_add(1, std::memory_order_relaxed);
      Logging::cout() << "task_inc\n";
    }
    
    private:
    
    inline void dealloc()
    {
      ExternalReferenceTable::dealloc();
      RememberedSet::dealloc();
      Object::dealloc();
    }
  };

#ifdef ENABLE_BENCHMARKING
  // Callback for region GC/release operations
  inline thread_local std::function<void(uint64_t, RegionType, size_t, size_t)>* 
    gc_callback = nullptr;

  inline void set_gc_callback(
    std::function<void(uint64_t, RegionType, size_t, size_t)>* callback)
  {
    gc_callback = callback;
  }

  inline std::function<void(uint64_t, RegionType, size_t, size_t)>*
  get_gc_callback()
  {
    return gc_callback;
  }
#endif // ENABLE_BENCHMARKING

} // namespace verona::rt