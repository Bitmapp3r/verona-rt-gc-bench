// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "../object/object.h"
#include "region_arena.h"
#include "region_base.h"
#include "region_rc.h"
#include "region_trace.h"
#include <test/measuretime.h>

namespace verona::rt
{
  inline thread_local RegionBase* active_region_md = nullptr;

  /**
   * Conceptually, objects are allocated within a region, and regions are
   * owned by cowns. Different regions may have different memory management
   * strategies, such as trace-based garbage collection, arena-based bump
   * allocation, or limited reference counting.
   *
   * The "entry point" or "root" of a region is an "Iso" (or isolated) object.
   * This Iso object is the only way to refer to a region, apart from external
   * references to objects inside a region.
   *
   * In the implementation, the Iso object points to a region metadata object.
   * This metadata object keeps track of the specific memory management
   * strategy and every object allocated within that region. It also contains
   * the remembered set and external reference table. The region metadata
   * object is always created, even if the only object in the region is the Iso
   * object.
   *
   * Because of circular dependencies, the implementation of regions is split
   * into multiple files:
   *
   *                            RegionBase (region_base.h)
   *                                       ^
   *                                       |
   *   Concrete region         RegionTrace (region_trace.h)
   *   implementations         RegionArena (region_arena.h)
   *                                       ^
   *                                       |
   *                                Region (region.h)
   *
   * RegionBase is the base class that contains all the common functionality
   * for regions, i.e. the remembered set and external reference table. All
   * region implementations inherit from RegionBase, and pointers to RegionBase
   * are passed around when the specific region type is unknown. As the base
   * class, RegionBase cannot refer to anything in the other region classes.
   *
   * The concrete region implementations are what actually implement the
   * memory management strategy for a region. They all inherit from RegionBase
   * and need to know the complete type of RegionBase. Anything that wants to
   * use a region will need to cast to the correct region implementation class.
   *
   * This class, Region, is just a collection of static helper methods. Thus,
   * it needs to know the complete types of RegionBase and the region
   * implementations, and none of the other region classes can refer to this
   * one.
   **/

  /**
   * NOTE: Search for "TODO(region)" for outstanding todos.
   **/
  using namespace snmalloc;

  /**
   * Helpers to convert a RegionType enum to a class at compile time.
   *
   * Example usage:
   *   using RegionClass = typename RegionType_to_class<region_type>::T;
   **/
  template<RegionType region_type>
  struct RegionType_to_class
  {
    using T = void;
  };

  template<>
  struct RegionType_to_class<RegionType::Trace>
  {
    using T = RegionTrace;
  };

  template<>
  struct RegionType_to_class<RegionType::Arena>
  {
    using T = RegionArena;
  };

  template<>
  struct RegionType_to_class<RegionType::Rc>
  {
    using T = RegionRc;
  };

  /**
   * Helper to capture stats, run an action, and report metrics.
   */
  template<typename Action>
  inline void with_region_stats(RegionBase* r, const char* op_name, Action&& action)
  {
    RegionType type;
    if (RegionTrace::is_trace_region(r))
      type = RegionType::Trace;
    else if (RegionArena::is_arena_region(r))
      type = RegionType::Arena;
    else if (RegionRc::is_rc_region(r))
      type = RegionType::Rc;
    else
      abort();

    // Capture memory stats before operation
    size_t mem_before = 0;
    size_t obj_before = 0;
    switch (type)
    {
      case RegionType::Trace:
        mem_before = ((RegionTrace*)r)->get_current_memory_used();
        for (auto p : *((RegionTrace*)r))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Arena:
        mem_before = ((RegionArena*)r)->get_current_memory_used();
        for (auto p : *((RegionArena*)r))
        {
          UNUSED(p);
          obj_before++;
        }
        break;
      case RegionType::Rc:
        mem_before = ((RegionRc*)r)->get_current_memory_used();
        obj_before = ((RegionRc*)r)->get_region_size();
        break;
    }

    MeasureTime m(true);
    action();
    uint64_t duration_ns = m.get_time().count();

    // Report via callback if set
    if (get_gc_callback() != nullptr)
    {
      (*get_gc_callback())(duration_ns, type, mem_before, obj_before);
    }
    else
    {
      Logging::cout() << op_name << " time: " << duration_ns << " ns"
                      << Logging::endl;
    }
  }

  class Region
  {
  public:
    // Don't accidentally instantiate a Region.
    Region() = delete;

    /**
     * Returns the type of region represented by the Iso object `o`.
     **/
    static RegionType get_type(Object* o)
    {
      if (RegionTrace::is_trace_region(o))
        return RegionType::Trace;
      else if (RegionArena::is_arena_region(o))
        return RegionType::Arena;
      else if (RegionRc::is_rc_region(o))
        return RegionType::Rc;

      abort();
    }

    /**
     * Release and deallocate the region represented by Iso object `o`.
     *
     * As we discover Iso pointers to other regions, we add them to our
     * worklist.
     **/
    static void release(Object* o)
    {
      //RegionBase* reg = r->get_region();
      RegionBase* reg = o->acq_region();
      std::cout << "acuired region in Region::release on object " << o << "\n";
      reg->isAlive.store(false, std::memory_order_release);

      if (reg->task_dec()) {
        std::cout << "physically releasing region\n";
        region_physical_release(o, reg);
        std::cout << "released region\n";
      } else {
        std::cout << "release region in Region::release on object " << o << "\n";
        o->rel_region(reg);
      }
    }

    /**
     * Returns the region metadata object for the given Iso object `o`.
     *
     * This method returns a RegionBase that will need to be cast to a
     * specific region implementation.
     **/
    static RegionBase* get(Object* o)
    {
      assert(o->debug_is_iso());
      return o->get_region();
    }

    
    static void logical_release_internal(Object* o, ObjectStack& collect) 
    {
      Logging::cout() << "logical release internal on object; " <<  o << "\n";
      //auto r = o->get_region();
      RegionBase* r = o->acq_region();
      r->isAlive.store(false, std::memory_order_release);
      
      if (r->task_dec()) {
        Logging::cout() << "physically releasing sub region\n";
        with_region_stats(r, "Subregion release", [&]() {
          release_internal(o, r, collect);
        });
      } else {
        o->rel_region(r);
      }
    }

    static void region_physical_release(Object* r, RegionBase* reg) {
      std::cout << "reached region_physical_release on object: " << r << "\n";
      with_region_stats(reg, "Region release", [&]() {

        assert(r->debug_is_iso() || r->is_opened());
        ObjectStack collect;
        Logging::cout() << "release on object: " << r << "\n";
        release_internal(r, reg, collect);

        while (!collect.empty())
        { 
          Logging::cout() << "hasdkfasdf\n";
          r = collect.pop();
          assert(r->debug_is_iso());
          logical_release_internal(r, collect);
        }
      });
  }


    
    private:
    /**
     * Internal method for releasing and deallocating regions, that takes
     * a worklist (represented by `f` and `collect`).
     *
     * We dispatch based on the type of region represented by `o`.
     **/
    static void release_internal(Object* o, RegionBase* reg, ObjectStack& collect)
    {
      Logging::cout() << "release internal on object: " << o << "\n"; 
      //auto r = o->get_region();
      switch (Region::get_type(reg))
      {
        case RegionType::Trace:
          ((RegionTrace*)reg)->release_internal(o, collect);
          return;
        case RegionType::Arena:
          ((RegionArena*)reg)->release_internal(o, collect);
          return;
        case RegionType::Rc:
        {
          ((RegionRc*)reg)->release_internal(o, collect);
          return;
        }
        default:
          abort();
      }
    }
  };

  inline size_t debug_get_ref_count(Object* o)
  {
    return o->get_ref_count();
  }

  RegionBase* Object::acq_region() {
    auto classs = get_class();
    /*  switch (classs) {
        case RegionMD::ISO:
          std::cout << "ISO\n";
          break; 
        case RegionMD::UNMARKED:  
          std::cout << "HUHH???\n";
          break;
        case RegionMD::MARKED:
          std::cout << "MARKED\n";
          break;
        case RegionMD::OPEN_ISO:
          std::cout << "OEPN ISO\n";
          break;
        case RegionMD::RC:
          std::cout << "RC\n";
          break;
        case RegionMD::NONATOMIC_RC:
          std::cout << "NON ATOMIC RC\n";
        default:
          std::cout << "nvm\n";
      }*/
      assert(classs == RegionMD::ISO || classs == RegionMD::OPEN_ISO);
      while(true) { 
        size_t state = get_header().rc.load();
        if ((RegionMD)(state & MASK) == OPEN_ISO) {
          snmalloc::Aal::pause();
          continue;
        } else {
          // its ISO so try and do the exchange!
          // WE CAN CALL GC SPECIFIC CODE HERE TO GET THE "NEW STATE"
          RegionBase* reg = (RegionBase*)(state & ~MASK);
          size_t new_state_rc = state;
          auto expected = RegionBase::Closed;
          bool success;
          switch (Region::get_type(reg)) {
            case RegionType::Trace:
            case RegionType::Arena:
              success = reg->state.compare_exchange_strong(expected, RegionBase::Open);
              break;
            case RegionType::Rc:
              new_state_rc = ((RegionRc*)reg)->open_state(this);
              success = get_header().rc.compare_exchange_strong(state, new_state_rc);
              break;
          }
          if (success) {
            if ((RegionMD)(state & MASK) == OPEN_ISO) { // old state  
              // try again
              std::cout << "This shouldn't be possible\n";
              continue;
            }
            //assert(get_class() == RegionMD::OPEN_ISO); <--- only wanted when debugging Rc
            //std::cout << "object: " << this << "region: " << reg << "\n";
            return reg;
            
          } else {
          snmalloc::Aal::pause();
          }
        }
      }  
    }

    // THERES A RACE CONDITION HERE
    void Object::rel_region(RegionBase* reg) {
      // not actual region release. but releasing us from opening the region ie close
      //std::cout << "object: " << this << "region: " << reg << "\n";
      while(true) {
        size_t state = get_header().rc.load();
        //RegionBase* reg = (RegionBase*)(state & ~MASK);
        size_t new_state = state;
        auto expected = RegionBase::Open;
        bool success;
        switch(Region::get_type(reg)) {
          case RegionType::Trace:
          case RegionType::Arena:
            success = reg->state.compare_exchange_strong(expected, RegionBase::Closed);
            assert(success && "Fatal: Region was not Open when trying to close");
            break;
          case RegionType::Rc:
            ((RegionRc*)reg)->entry_point_count.store(state >> SHIFT);
            new_state = ((RegionRc*)reg)->close_state(this);
            success = get_header().rc.compare_exchange_strong(state, new_state);
            break;
        } 
        if (success) {
          return;
        }
      }
    }

} // namespace verona::rt
