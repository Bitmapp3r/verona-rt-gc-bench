// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once
#ifdef ENABLE_BENCHMARKING
#include <functional>
#endif

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
  };

  class RegionBase : public Object,
                     public ExternalReferenceTable,
                     public RememberedSet
  {
    friend class Freeze;
    friend class RegionTrace;
    friend class RegionArena;
    friend class RegionRc;

  public:
    enum IteratorType
    {
      Trivial,
      NonTrivial,
      AllObjects,
    };

    RegionBase() : Object() {}

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