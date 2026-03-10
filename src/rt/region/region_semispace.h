// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "../object/object.h"
#include "region_base.h"

#include <cstddef>
#include <cstring>

namespace verona::rt
{
  using namespace snmalloc;

  /**
   * Please see region.h for the full documentation.
   *
   * This is a concrete implementation of a region using a Cheney-style
   * semi-space copying garbage collector. This class inherits from RegionBase.
   *
   * The collector maintains two equally-sized memory spaces:
   *   - from-space: where objects are currently allocated (bump allocation)
   *   - to-space: where live objects are copied during GC
   *
   * During GC, the collector uses two pointers into to-space:
   *   - scan: points to the next object to be scanned (BFS queue front)
   *   - free: points to where the next copied object will go (BFS queue back)
   *
   * Objects are copied from from-space to to-space. After copying, a forwarding
   * pointer is installed in the old object's header (using the MARKED tag with
   * the new address in the upper bits). After GC, the spaces are swapped -
   * to-space becomes the new from-space.
   *
   * Objects that are too large to fit in a semi-space are allocated separately
   * via heap::alloc and tracked in a linked list ("large object list"). Large
   * objects are not copied; they are kept in place. If a large object is
   * reachable from the root, it is marked. We later traverse the large object
   * ring to create a new ring of just the marked (live) objects and deallocate
   * the objects in the old ring. 
   **/
  class RegionSemiSpace : public RegionBase
  {
  public:
    template<RegionBase::IteratorType type>
    class iterator;

    // Initial semi-space size: 1MB each.
    static constexpr size_t INITIAL_SEMISPACE_SIZE = 1024 * 1024;

    // Objects larger than this are placed in the large object list
    // and are never copied during GC (they are marked/unmarked in place).
    // This is fixed so behaviour doesn't change as the semispace grows.
    static constexpr size_t LARGE_OBJECT_THRESHOLD = INITIAL_SEMISPACE_SIZE / 2;

  private:
    friend class Region;

    /// Current size of each semi-space (grows dynamically).
    size_t semispace_size;

    /// Pointer to from-space (where objects currently live).
    std::byte* from_space;

    /// Pointer to to-space (used during GC).
    std::byte* to_space;

    /// Bump pointer: next free byte in from-space for allocation.
    std::byte* alloc_ptr;

    /// End of from-space.
    std::byte* alloc_end;

    /// Total memory used by objects (for metrics).
    size_t current_memory_used = 0;

    /// Linked list of large objects (too big for semi-space).
    /// Uses Object::next pointers. Null-terminated.
    Object* large_objects = nullptr;

    /// The iso (root) object, heap-allocated and pinned — it never
    /// moves during GC or semispace growth.  This lets callers keep
    /// a stable C++ pointer to the root across those operations.
    Object* pinned_iso_ = nullptr;

    /// Delta from the most recent grow() call.  Consumed by
    /// create_object() in region_api.h to update the entry point
    /// in RegionContext (which grow() cannot access directly due
    /// to header dependency order).
    ptrdiff_t last_grow_delta_ = 0;

    /// Thread-local from-space bounds used by forward_if_moved callback
    /// during GC so it can distinguish forwarded from-space objects from
    /// merely-marked large objects (both use the MARKED class tag).
    static inline thread_local std::byte* gc_from_space_ = nullptr;
    static inline thread_local size_t gc_from_size_ = 0;

    /// Thread-local state used by adjust_for_grow callback during
    /// from-space reallocation to adjust pointer fields by the address
    /// delta between old and new from-space.
    static inline thread_local std::byte* grow_old_from_ = nullptr;
    static inline thread_local size_t grow_old_used_ = 0;
    static inline thread_local ptrdiff_t grow_delta_ = 0;

    explicit RegionSemiSpace()
    : RegionBase(),
      semispace_size(INITIAL_SEMISPACE_SIZE),
      from_space(nullptr),
      to_space(nullptr),
      alloc_ptr(nullptr),
      alloc_end(nullptr)
    {
      // Allocate two semi-spaces.
      from_space = (std::byte*)heap::alloc(semispace_size);
      to_space = (std::byte*)heap::alloc(semispace_size);
      alloc_ptr = from_space;
      alloc_end = from_space + semispace_size;
    }

    static const Descriptor* desc()
    {
      static constexpr Descriptor desc = {
        vsizeof<RegionSemiSpace>, nullptr, nullptr, nullptr};
      return &desc;
    }

  public:
    inline static RegionSemiSpace* get(Object* o)
    {
      assert(o->debug_is_iso());
      assert(is_semispace_region(o->get_region()));
      return (RegionSemiSpace*)o->get_region();
    }

    inline static bool is_semispace_region(Object* o)
    {
      return o->is_type(desc());
    }

    size_t get_current_memory_used() const
    {
      return current_memory_used;
    }

    /**
     * Return and clear the address delta from the most recent grow().
     * If grow() was not called (or delta was zero), returns 0.
     * Used by create_object() in region_api.h to update the entry
     * point after an allocation that triggered growth.
     **/
    ptrdiff_t consume_grow_delta()
    {
      ptrdiff_t d = last_grow_delta_;
      last_grow_delta_ = 0;
      return d;
    }

    /**
     * Returns the current size of each semi-space (from-space and to-space).
     * This starts at INITIAL_SEMISPACE_SIZE and doubles when growth occurs.
     **/
    size_t get_semispace_size() const
    {
      return semispace_size;
    }

    /**
     * Returns the number of objects in the large object list.
     * For testing/debugging only.
     **/
    size_t get_large_object_count() const
    {
      size_t count = 0;
      Object* lo = large_objects;
      while (lo != nullptr)
      {
        count++;
        lo = lo->get_next();
      }
      return count;
    }

    /**
     * Returns the number of bytes currently used in from-space
     * (i.e. how much has been bump-allocated).
     **/
    size_t get_fromspace_used() const
    {
      return static_cast<size_t>(alloc_ptr - from_space);
    }

    /**
     * Creates a new semi-space region by allocating Object `o` of type `desc`.
     * The object is initialised as the Iso object for that region.
     * Returns a pointer to `o`.
     **/
    template<size_t size = 0>
    static Object* create(const Descriptor* desc)
    {
      // Allocate and construct region metadata object.
      void* p = heap::alloc<vsizeof<RegionSemiSpace>>();
      Object* o = Object::register_object(p, RegionSemiSpace::desc());
      auto reg = new (o) RegionSemiSpace();

      // Allocate the iso (root) object on the heap so it is pinned —
      // it will never be moved by GC or semispace growth.
      size_t sz = snmalloc::bits::align_up(desc->size, Object::ALIGNMENT);
      void* iso_mem = heap::alloc(sz);
      Object* iso = Object::register_object(iso_mem, desc);
      assert(Object::debug_is_aligned(iso));

      iso->init_iso();
      iso->set_region(reg);
      reg->pinned_iso_ = iso;
      reg->current_memory_used += desc->size;

      return iso;
    }

    /**
     * Allocates an object of type `desc` in the region represented by
     * Iso object `in`. Returns a pointer to the new object.
     **/
    template<size_t size = 0>
    static Object* alloc(Object* in, const Descriptor* desc)
    {
      RegionSemiSpace* reg = get(in);
      Object* o = reg->alloc_internal(desc);
      assert(Object::debug_is_aligned(o));
      return o;
    }

    /**
     * Ensure that at least `bytes` of from-space are available for
     * future bump allocations, growing the semi-spaces if necessary.
     * This does NOT allocate any objects — it only reserves capacity.
     *
     * Call this before a batch of allocations so that none of them
     * will trigger grow() and invalidate earlier pointers.
     **/
    static void ensure_available(Object* in, size_t bytes)
    {
      RegionSemiSpace* reg = get(in);
      size_t remaining =
        static_cast<size_t>(reg->alloc_end - reg->alloc_ptr);
      if (bytes > remaining)
      {
        reg->grow(bytes);
      }
    }

    /**
     * Insert the Object `o` into the RememberedSet of `into`'s region.
     **/
    template<TransferOwnership transfer = NoTransfer>
    static void insert(Object* into, Object* o)
    {
      assert(o->debug_is_immutable() || o->debug_is_shared());
      RegionSemiSpace* reg = get(into);
      Object::RegionMD c;
      o = o->root_and_class(c);
      reg->RememberedSet::insert<transfer>(o);
    }

    /**
     * Swap the Iso (root) object of the region.
     **/
    static void swap_root(Object* prev, Object* next)
    {
      assert(prev != next);
      assert(prev->debug_is_iso());
      assert(next->debug_is_mutable());
      RegionSemiSpace* reg = get(prev);
      UNUSED(reg);

      // Clear iso status on old root.
      prev->init_next(nullptr);

      // Set new root's iso status.
      next->init_iso();
      next->set_region(prev->get_region());
    }

    /**
     * Run Cheney-style semi-space garbage collection.
     *
     * Algorithm:
     *   1. Trace the pinned iso root's fields (root stays in place).
     *   2. Cheney scan loop — use scan/free pointers as a BFS queue:
     *      - For each scanned to-space object, trace its fields.
     *      - From-space objects are copied to to-space (forwarding pointer
     *        installed); large objects are marked live in place.
     *      - ISO, IMMUTABLE, SHARED fields are left as-is / remembered.
     *   3. Update all pointers in to-space objects to forwarded addresses.
     *      Also update the pinned root's pointers.
     *   4. Update pointers in live large objects; finalize/free dead ones.
     *   5. Finalize and destruct dead objects in old from-space.
     *   6. Swap spaces. The iso root pointer is unchanged (pinned).
     **/
    static Object* gc(Object* o, RegionSemiSpace* reg)
    {
      assert(o->debug_is_iso());
      assert(o == reg->pinned_iso_);

      Logging::cout() << "SemiSpace GC called for: " << o << Logging::endl;

      std::byte* to_start = reg->to_space;
      std::byte* scan = to_start;
      std::byte* free_ptr = to_start;
      std::byte* to_end = to_start + reg->semispace_size;

      // Phase 1: The iso root is pinned on the heap — don't copy it.
      // Trace its fields and copy/mark reachable children.
      {
        ObjectStack fields;
        o->trace(fields);

        while (!fields.empty())
        {
          Object* field = fields.pop();
          switch (field->get_class())
          {
            case Object::ISO:
              break;

            case Object::UNMARKED:
            {
              if (is_in_space(field, reg->from_space, reg->semispace_size))
              {
                Object* new_obj = copy_object(field, free_ptr, to_end);
                assert(new_obj != nullptr);
              }
              else if (is_large_object(field, reg))
              {
                field->mark();
              }
              break;
            }

            case Object::MARKED:
              break;

            case Object::SCC_PTR:
            {
              Object* imm = field->immutable();
              reg->RememberedSet::mark(imm);
              break;
            }

            case Object::RC:
            case Object::SHARED:
            {
              reg->RememberedSet::mark(field);
              break;
            }

            default:
              break;
          }
        }
      }

      // Phase 2: Cheney scan loop.
      // scan walks through to-space objects; free_ptr is where next copy goes.
      // From-space objects are copied on first encounter; large objects are
      // marked as live in place (they are handled separately in Phase 4).
      while (scan < free_ptr)
      {
        Object* current = Object::object_start(scan);
        size_t obj_size =
          snmalloc::bits::align_up(current->size(), Object::ALIGNMENT);

        // Trace this object's fields.
        ObjectStack fields;
        current->trace(fields);

        while (!fields.empty())
        {
          Object* field = fields.pop();
          switch (field->get_class())
          {
            case Object::ISO:
              // Subregion pointer — don't copy, leave as-is.
              break;

            case Object::UNMARKED:
            {
              // Live object in from-space, not yet copied.
              if (is_in_space(field, reg->from_space, reg->semispace_size))
              {
                Object* new_obj = copy_object(field, free_ptr, to_end);
                // to-space is the same size as from-space and we only
                // copy live objects, so this should never fail.
                assert(new_obj != nullptr);
              }
              else if (is_large_object(field, reg))
              {
                // Large object: mark it as live (use MARKED tag).
                field->mark();
              }
              break;
            }

            case Object::MARKED:
            {
              // Already copied (forwarded) or marked large object.
              // Will be fixed up in the pointer update pass.
              break;
            }

            case Object::SCC_PTR:
            {
              Object* imm = field->immutable();
              reg->RememberedSet::mark(imm);
              break;
            }

            case Object::RC:
            case Object::SHARED:
            {
              reg->RememberedSet::mark(field);
              break;
            }

            default:
              break;
          }
        }

        scan += obj_size;
      }

      // Set from-space context for forward_if_moved callback.
      gc_from_space_ = reg->from_space;
      gc_from_size_ = reg->semispace_size;

      // Phase 3: Update all pointers in to-space objects to new addresses.
      update_all_pointers(to_start, free_ptr, reg);

      // Also update the pinned root's pointers (it references from-space
      // objects that have been forwarded to to-space).
      update_object_pointers(o, reg);

      // Phase 4: Update pointers in large objects and collect dead large
      // objects.
      Object* live_large = nullptr;
      {
        Object* lo = reg->large_objects;
        while (lo != nullptr)
        {
          Object* next_lo = lo->get_next_any_mark();
          if (lo->get_class() == Object::MARKED)
          {
            // Live large object — unmark and update its pointers.
            lo->unmark();
            update_object_pointers(lo, reg);
            lo->init_next(live_large);
            live_large = lo;
          }
          else
          {
            // Dead large object — finalize and deallocate.
            ObjectStack dead_isos;
            reg->current_memory_used -= lo->size();
            if (!lo->is_trivial())
            {
              lo->finalise(nullptr, dead_isos);
              lo->destructor();
            }
            lo->dealloc();
          }
          lo = next_lo;
        }
      }
      reg->large_objects = live_large;

      // Phase 5: Finalize dead objects in old from-space.
      // All non-forwarded objects in from-space are dead (the pinned root
      // was never in from-space, so it is unaffected).
      // We need to run finalisers before destructors for non-trivial objects.
      {
        // First pass: finalisers for non-trivial dead objects in from-space.
        std::byte* p = reg->from_space;
        std::byte* end = reg->alloc_ptr;
        ObjectStack dummy_isos;
        while (p < end)
        {
          Object* obj = Object::object_start(p);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);

          if (obj->get_class() != Object::MARKED)
          {
            // This object was NOT forwarded — it's dead.
            if (!obj->is_trivial())
            {
              obj->finalise(nullptr, dummy_isos);
            }
          }
          p += obj_size;
        }

        // Second pass: destructors for non-trivial dead objects.
        p = reg->from_space;
        while (p < end)
        {
          Object* obj = Object::object_start(p);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);

          if (obj->get_class() != Object::MARKED)
          {
            if (!obj->is_trivial())
            {
              obj->destructor();
            }
          }
          p += obj_size;
        }
      }

      // Phase 6: Swap spaces.
      // Old from-space is now free. New from-space is to-space (with live
      // data). Both spaces are always equal in size.
      std::byte* old_from = reg->from_space;
      reg->from_space = reg->to_space;
      reg->to_space = old_from;
      reg->alloc_ptr = free_ptr;
      reg->alloc_end = reg->from_space + reg->semispace_size;

      // Update memory used: pinned root + from-space live data + large objects.
      reg->current_memory_used = o->size();
      {
        std::byte* p = reg->from_space;
        while (p < reg->alloc_ptr)
        {
          Object* obj = Object::object_start(p);
          reg->current_memory_used += obj->size();
          p += snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);
        }
        // Add large objects.
        Object* lo = reg->large_objects;
        while (lo != nullptr)
        {
          reg->current_memory_used += lo->size();
          lo = lo->get_next();
        }
      }

      // Sweep the remembered set.
      reg->RememberedSet::sweep();

      // The root is pinned — no address change, no need to re-init iso.
      Logging::cout() << "SemiSpace GC complete. Iso (pinned): " << o
                      << Logging::endl;

      return o;
    }

  private:
    /**
     * Allocate an object of type `desc` in the from-space.
     * If the object is too large for the semi-space, allocate via heap.
     **/
    Object* alloc_internal(const Descriptor* desc)
    {
      size_t sz = snmalloc::bits::align_up(desc->size, Object::ALIGNMENT);
      current_memory_used += desc->size;

      if (sz > LARGE_OBJECT_THRESHOLD)
      {
        // Large object: allocate via heap and add to large object list.
        void* p = heap::alloc(desc->size);
        Object* o = Object::register_object(p, desc);
        o->init_next(large_objects);
        large_objects = o;
        return o;
      }

      // Check if we have space in from-space; grow if needed.
      if (alloc_ptr + sz > alloc_end)
      {
        grow(sz);
      }

      void* p = alloc_ptr;
      alloc_ptr += sz;
      Object* o = Object::register_object(p, desc);
      o->init_next(nullptr);
      return o;
    }

    /**
     * Grow both semi-spaces so that at least `needed` additional bytes
     * can be allocated in from-space. The size is doubled repeatedly
     * until the requirement is met.
     *
     * After the raw byte copy, all pointer fields inside from-space
     * objects (and large objects / the pinned root that reference
     * from-space) are adjusted by the address delta so they remain
     * valid. The pinned root itself is heap-allocated and does not
     * move.
     *
     * Steps:
     *   1. Compute a new size (at least double the current size).
     *   2. Allocate a new from-space, copy existing live data, free old.
     *   3. Allocate a new to-space, free old.
     *   4. Update alloc_ptr / alloc_end.
     *   5. Adjust all pointer fields by the address delta.
     **/
    void grow(size_t needed)
    {
      size_t used = static_cast<size_t>(alloc_ptr - from_space);
      size_t new_size = semispace_size;
      while (new_size < used + needed)
        new_size *= 2;

      // Even if used + needed fits, we still need to grow.
      if (new_size == semispace_size)
        new_size *= 2;

      Logging::cout() << "SemiSpace grow: " << semispace_size << " -> "
                      << new_size << Logging::endl;

      // Remember old from-space address before freeing it.
      std::byte* old_from = from_space;

      // Grow from-space: allocate new, copy live data, free old.
      std::byte* new_from = (std::byte*)heap::alloc(new_size);
      std::memcpy(new_from, from_space, used);
      heap::dealloc(from_space, semispace_size);

      // Grow to-space: just reallocate (no live data in to-space).
      heap::dealloc(to_space, semispace_size);
      std::byte* new_to = (std::byte*)heap::alloc(new_size);

      ptrdiff_t delta = new_from - old_from;

      from_space = new_from;
      to_space = new_to;
      alloc_ptr = new_from + used;
      alloc_end = new_from + new_size;
      semispace_size = new_size;

      // If the address changed, adjust every pointer that referred to
      // the old from-space.
      if (delta != 0)
      {
        // Set thread-local state for the adjust_for_grow callback.
        grow_old_from_ = old_from;
        grow_old_used_ = used;
        grow_delta_ = delta;

        // Walk all objects in the new from-space.
        std::byte* p = new_from;
        while (p < alloc_ptr)
        {
          Object* obj = Object::object_start(p);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);

          // Fix pointer fields via relocate if available.
          auto* desc = obj->get_descriptor();
          if (desc->relocate != nullptr)
          {
            desc->relocate(obj, adjust_for_grow);
          }
          else
          {
            // Fallback: scan body word-by-word (best-effort).
            size_t body_size = obj->size() - sizeof(Object::Header);
            auto* body = (Object**)obj;
            size_t num_words = body_size / sizeof(Object*);

            for (size_t i = 0; i < num_words; i++)
            {
              auto* word = (std::byte*)body[i];
              if (word != nullptr && word >= old_from &&
                  word < old_from + used)
              {
                body[i] = (Object*)(word + delta);
              }
            }
          }

          p += obj_size;
        }

        // Fix pointer fields in large objects that reference from-space.
        Object* lo = large_objects;
        while (lo != nullptr)
        {
          auto* desc = lo->get_descriptor();
          if (desc->relocate != nullptr)
          {
            desc->relocate(lo, adjust_for_grow);
          }
          else
          {
            size_t body_size = lo->size() - sizeof(Object::Header);
            auto* body = (Object**)lo;
            size_t num_words = body_size / sizeof(Object*);

            for (size_t i = 0; i < num_words; i++)
            {
              auto* word = (std::byte*)body[i];
              if (word != nullptr && word >= old_from &&
                  word < old_from + used)
              {
                body[i] = (Object*)(word + delta);
              }
            }
          }
          lo = lo->get_next();
        }

        // Fix pointer fields in the pinned root that reference from-space.
        if (pinned_iso_ != nullptr)
        {
          auto* desc = pinned_iso_->get_descriptor();
          if (desc->relocate != nullptr)
          {
            desc->relocate(pinned_iso_, adjust_for_grow);
          }
          else
          {
            size_t body_size =
              pinned_iso_->size() - sizeof(Object::Header);
            auto* body = (Object**)pinned_iso_;
            size_t num_words = body_size / sizeof(Object*);

            for (size_t i = 0; i < num_words; i++)
            {
              auto* word = (std::byte*)body[i];
              if (word != nullptr && word >= old_from &&
                  word < old_from + used)
              {
                body[i] = (Object*)(word + delta);
              }
            }
          }
        }

        // The pinned root never moves, so no entry-point delta is needed.
      }
    }

    /**
     * Grow-time forwarding callback passed to Descriptor::relocate.
     * Adjusts pointers that fell in the old from-space by the address
     * delta; all other pointers are returned unchanged.
     **/
    static Object* adjust_for_grow(Object* o)
    {
      auto addr = (std::byte*)o;
      if (addr >= grow_old_from_ && addr < grow_old_from_ + grow_old_used_)
        return (Object*)(addr + grow_delta_);
      return o;
    }

    /**
     * Check if an object is in a given memory space.
     **/
    static bool is_in_space(Object* o, std::byte* space_start, size_t space_sz)
    {
      auto addr = (std::byte*)o->real_start();
      return addr >= space_start && addr < (space_start + space_sz);
    }

    /**
     * Check if an object is a large object (allocated outside semi-spaces).
     **/
    static bool is_large_object(Object* o, RegionSemiSpace* reg)
    {
      return !is_in_space(o, reg->from_space, reg->semispace_size) &&
        !is_in_space(o, reg->to_space, reg->semispace_size);
    }

    /**
     * Copy an object from from-space to to-space.
     * Installs a forwarding pointer in the old object using the MARKED tag.
     * Returns a pointer to the new object in to-space.
     * Returns nullptr if there's not enough space.
     **/
    static Object* copy_object(Object* old_obj, std::byte*& free_ptr,
                               std::byte* to_end)
    {
      size_t obj_size =
        snmalloc::bits::align_up(old_obj->size(), Object::ALIGNMENT);

      if (free_ptr + obj_size > to_end)
        return nullptr;

      // Copy the entire object (header + body) to to-space.
      void* src = old_obj->real_start();
      void* dst = free_ptr;
      std::memcpy(dst, src, obj_size);

      Object* new_obj = Object::object_start(dst);
      // Ensure the new object is UNMARKED (mutable) — clear any tags.
      new_obj->init_next(nullptr);

      free_ptr += obj_size;

      // Install forwarding pointer in old object.
      // We use the MARKED bit plus the new address in the upper bits.
      // This works because the header `bits` field stores tag in low bits
      // and payload in upper bits — same pattern as ISO/set_region.
      old_obj->set_forwarding_pointer(new_obj);

      return new_obj;
    }

    /**
     * Update all object pointers in to-space to point to the new locations.
     **/
    static void update_all_pointers(std::byte* to_start, std::byte* to_end,
                                    RegionSemiSpace* reg)
    {
      std::byte* p = to_start;
      while (p < to_end)
      {
        Object* obj = Object::object_start(p);
        size_t obj_size =
          snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);
        update_object_pointers(obj, reg);
        p += obj_size;
      }
    }

    /**
     * Forwarding callback passed to Descriptor::relocate.
     * Returns the new address if the object was moved (forwarded),
     * otherwise returns the original address unchanged.
     *
     * Only from-space objects can be forwarded.  Large objects that were
     * merely marked as live (also using the MARKED tag) are NOT in
     * from-space and must be left untouched.
     **/
    static Object* forward_if_moved(Object* o)
    {
      if (is_in_space(o, gc_from_space_, gc_from_size_) && is_forwarded(o))
        return get_forwarding_target(o);
      return o;
    }

    /**
     * Update all pointer fields in a single object to forward to new
     * addresses.
     *
     * If the object's Descriptor provides a `relocate` function, we use it
     * for exact pointer updates — the object itself knows which of its
     * fields are pointers and updates them directly via the forwarding
     * callback. This is 100% correct with no risk of false matches.
     *
     * If `relocate` is not available, falls back to a best-effort body
     * scan that checks each word against from-space bounds and forwarding
     * status. This fallback can theoretically produce false positives if
     * a non-pointer integer field coincidentally matches a forwarded
     * from-space address.
     **/
    static void update_object_pointers(Object* obj, RegionSemiSpace* reg)
    {
      auto* descriptor = obj->get_descriptor();
      if (descriptor->relocate != nullptr)
      {
        // Exact pointer update via relocate callback.
        descriptor->relocate(obj, forward_if_moved);
        return;
      }

      // Fallback: scan body word-by-word (best-effort).
      size_t body_size = obj->size() - sizeof(Object::Header);
      auto* body = (Object**)obj;
      size_t num_words = body_size / sizeof(Object*);

      for (size_t i = 0; i < num_words; i++)
      {
        Object* word = body[i];
        if (word != nullptr &&
            is_in_space(word, reg->from_space, reg->semispace_size) &&
            is_forwarded(word))
        {
          body[i] = get_forwarding_target(word);
        }
      }
    }

    /**
     * Check if an object has been forwarded (has a forwarding pointer).
     * A forwarded object has MARKED tag in its header bits.
     **/
    static bool is_forwarded(Object* o)
    {
      return o->get_class() == Object::MARKED;
    }

    /**
     * Get the forwarding target of a forwarded object.
     **/
    static Object* get_forwarding_target(Object* o)
    {
      assert(is_forwarded(o));
      return (Object*)(o->get_header().bits & ~Object::MASK);
    }

    /**
     * Release and deallocate all objects within the region.
     **/
    void release_internal(Object* o, ObjectStack& collect)
    {
      assert(o->debug_is_iso());

      Logging::cout() << "Region release: semispace region: " << o
                      << Logging::endl;

      // Run finalisers on all non-trivial objects in from-space.
      {
        std::byte* p = from_space;
        while (p < alloc_ptr)
        {
          Object* obj = Object::object_start(p);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);
          if (!obj->is_trivial())
          {
            obj->finalise(o, collect);
          }
          p += obj_size;
        }

        // Finalisers for large objects.
        Object* lo = large_objects;
        while (lo != nullptr)
        {
          if (!lo->is_trivial())
            lo->finalise(o, collect);
          lo = lo->get_next();
        }

        // Finaliser for the pinned root.
        if (pinned_iso_ != nullptr && !pinned_iso_->is_trivial())
        {
          pinned_iso_->finalise(o, collect);
        }
      }

      // Run destructors on all non-trivial objects.
      {
        std::byte* p = from_space;
        while (p < alloc_ptr)
        {
          Object* obj = Object::object_start(p);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);
          if (!obj->is_trivial())
          {
            obj->destructor();
          }
          p += obj_size;
        }

        Object* lo = large_objects;
        while (lo != nullptr)
        {
          if (!lo->is_trivial())
            lo->destructor();
          lo = lo->get_next();
        }

        // Destructor for the pinned root.
        if (pinned_iso_ != nullptr && !pinned_iso_->is_trivial())
        {
          pinned_iso_->destructor();
        }
      }

      // Deallocate large objects.
      {
        Object* lo = large_objects;
        while (lo != nullptr)
        {
          Object* next = lo->get_next();
          lo->dealloc();
          lo = next;
        }
      }

      // Deallocate the pinned root.
      if (pinned_iso_ != nullptr)
      {
        pinned_iso_->dealloc();
        pinned_iso_ = nullptr;
      }

      // Deallocate both semi-spaces.
      heap::dealloc(from_space, semispace_size);
      heap::dealloc(to_space, semispace_size);

      // Sweep the RememberedSet.
      RememberedSet::sweep();

      // Deallocate region metadata.
      dealloc();
    }

  public:
    /**
     * Iterator over all objects in the semi-space region.
     * Yields the pinned root first, then from-space objects, then large
     * objects.
     **/
    template<IteratorType type = AllObjects>
    class iterator
    {
      friend class RegionSemiSpace;

      static_assert(
        type == Trivial || type == NonTrivial || type == AllObjects);

      /// Iteration phase: pinned root -> from-space -> large objects.
      enum class Phase { PinnedRoot, FromSpace, LargeObjects, Done };

      iterator(RegionSemiSpace* r)
      : reg(r), arena_ptr(r->from_space), ptr(nullptr),
        phase(Phase::PinnedRoot)
      {
        // Try the pinned root first.
        if (reg->pinned_iso_ != nullptr && matches_filter(reg->pinned_iso_))
        {
          ptr = reg->pinned_iso_;
        }
        else
        {
          // Skip past pinned root phase.
          phase = Phase::FromSpace;
          advance_from_space();
        }
      }

      iterator(RegionSemiSpace* r, std::nullptr_t)
      : reg(r), arena_ptr(nullptr), ptr(nullptr), phase(Phase::Done)
      {}

    public:
      iterator operator++()
      {
        switch (phase)
        {
          case Phase::PinnedRoot:
            // Finished pinned root, move to from-space.
            phase = Phase::FromSpace;
            ptr = nullptr;
            advance_from_space();
            break;

          case Phase::FromSpace:
          {
            // Move to next object in from-space.
            size_t obj_size =
              snmalloc::bits::align_up(ptr->size(), Object::ALIGNMENT);
            arena_ptr = ptr->real_start() + obj_size;
            ptr = nullptr;
            advance_from_space();
            break;
          }

          case Phase::LargeObjects:
            if (ptr != nullptr)
            {
              ptr = ptr->get_next();
              skip_to_valid_large();
            }
            break;

          case Phase::Done:
            break;
        }
        return *this;
      }

      inline bool operator!=(const iterator& other) const
      {
        return ptr != other.ptr;
      }

      inline Object* operator*() const
      {
        return ptr;
      }

    private:
      RegionSemiSpace* reg;
      std::byte* arena_ptr;
      Object* ptr;
      Phase phase;

      static bool matches_filter(Object* obj)
      {
        if constexpr (type == Trivial)
          return obj->is_trivial();
        else if constexpr (type == NonTrivial)
          return !obj->is_trivial();
        else
          return true;
      }

      void advance_from_space()
      {
        // Try from-space.
        while (arena_ptr < reg->alloc_ptr)
        {
          Object* obj = Object::object_start(arena_ptr);
          size_t obj_size =
            snmalloc::bits::align_up(obj->size(), Object::ALIGNMENT);

          if (matches_filter(obj))
          {
            ptr = obj;
            return;
          }
          arena_ptr += obj_size;
        }

        // Then try large objects.
        phase = Phase::LargeObjects;
        ptr = reg->large_objects;
        skip_to_valid_large();
      }

      void skip_to_valid_large()
      {
        while (ptr != nullptr)
        {
          if (matches_filter(ptr))
            return;
          ptr = ptr->get_next();
        }
        // Exhausted.
        phase = Phase::Done;
      }
    };

    template<IteratorType type = AllObjects>
    inline iterator<type> begin()
    {
      return {this};
    }

    template<IteratorType type = AllObjects>
    inline iterator<type> end()
    {
      return {this, nullptr};
    }

  private:
    bool debug_is_in_region(Object* o)
    {
      for (auto p : *this)
      {
        if (p == o)
          return true;
      }
      return false;
    }
  };
} // namespace verona::rt
