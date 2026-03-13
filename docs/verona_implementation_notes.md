# README-VERONA-CONCEPTS.md
# Project Verona: Core Concepts & Lessons Learned

This guide covers the core architectural components of Project Verona used in our implementation, alongside crucial tips, quirks, and anti-patterns we discovered during development. Read this before modifying core threading or garbage collection logic.

**Reference Material:** * [Project Verona: Regular, Concurrent, and Distributed Programming (Microsoft Research)](https://www.microsoft.com/en-us/research/project/project-verona/)

---

## SystematicTestHarness: `sys` vs `con`

The `SystematicTestHarness` is responsible for setting up threads/cores and executing the program. 
* **⚠️ Lesson:** Avoid creating OS threads manually. Always use the harness so the Verona runtime can manage execution and deterministic testing properly.

There are two primary configurations:
* **Systematic (`sys`):** The program runs deterministically using a seed. Threads do not run truly concurrently here. This is used for reproducible testing.
* **Concurrent (`con`):** This configuration uses actual OS threads. Always use `con` to debug true concurrency problems and data races.

---

## Object Management, Regions, and GC

To create an object managed by Verona, inherit from `V`:
`class TypeName : V<TypeName>`

**Memory Management Functions:**
* `trace`: Implement this to add the references this object owns to an object stack.
* `finaliser` (Optional): Implement this to add sub-regions (regions this object holds a reference to).

**Creating a Region:**
An object becomes a region when initialized with placement new:
`TypeName* reg = new (rt) TypeName() // rt is a RegionType`
* **⚠️ Quirk (Suspended Regions):** When a region is "suspended," it isn't completely halted or removed from memory; it simply isn't at the top of the execution stack anymore.

**Garbage Collection (GC) Types:**
Our implementation utilizes several GC types: `RC`, `trace`, `arena`, and `semispace`. 

---

## Object Tags and States

All objects technically possess a 3-bit tag and can exist in 8 different states (check the source code for the full state machine). 

For Reference Counted (RC) isolated objects, the tag behavior is as follows:
* The tag is set to `ISO` initially.
* When the region is opened, the state changes to `OPEN_ISO`.
* Closing the region asserts that the object is `OPEN_ISO` and reverts it back to `ISO`.
* **⚠️ Quirk:** Only RC uses these specific in-object tags to manage isolated objects (other GCs do not). Be mindful of this when debugging GC, particularly regarding `ISO` objects occasionally appearing "unmarked" in certain GC states.

---

## Scheduling & Cowns

Each core has a dedicated scheduler thread pinned to it. The scheduler distributes work to worker threads and steals work from other scheduler threads to balance the load. If work is associated with cowns, the scheduler ensures that ordering behaviors are handled correctly.

**Creating and Scheduling Work (Best Practices):**
1. **Making Cowns:** Always prefer using `Vcown` to create cowns. There is an alternative method, but avoid it as it leads to unstable behavior.
2. **Creating Work:** Call `Behavior::make` (this internally calls `BehaviourCore::make`, which contains excellent source documentation). 
3. **Scheduling:** Schedule it by calling `Scheduler::schedule(behaviour->as_work())`.
4. **⚠️ Anti-Pattern:** The `schedule` lambda shouldn't be used directly. Stick to proper behavior creation.

# Relevant Papers & Resources

- [Verona: An Experimental Language for Safe Concurrent Programming](https://www.microsoft.com/en-us/research/project/verona/)
- [Concurrent Garbage Collection](https://www.microsoft.com/en-us/research/publication/concurrent-garbage-collection/)
