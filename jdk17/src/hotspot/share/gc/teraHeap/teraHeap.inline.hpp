#ifndef SHARE_GC_TERAHEAP_TERAHEAP_INLINE_HPP
#define SHARE_GC_TERAHEAP_TERAHEAP_INLINE_HPP

#include "gc/teraHeap/teraHeap.hpp"

#include "gc/shared/cardTable.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/stack.inline.hpp"

inline CardTable* TeraHeap::th_card_table() const {
  assert(_h2_card_table != NULL, "H2 card table is not set");
  return _h2_card_table;
}

// Pop a backward reference from mark stack `stack_id`. The object it
// points to is located in the Java Heap and we need to ensure that it will
// be kept alive.
inline oop* TeraHeap::h2_pop_back_ref_to_mark(uint stack_id) {
  Stack<oop *, mtGC>& s = _fgc_stacks[stack_id].mark;
  return s.is_empty() ? NULL : s.pop();
}

// Push a backward reference to the mark and adjust stacks of thread
// `worker_id`, which we use in the marking and pointer adjustment phases of
// the Full GC. Only this thread pushes to its stacks: no lock.
inline void TeraHeap::h2_push_backward_reference(uint worker_id, void *p, oop o, bool to_young) {
  assert(worker_id < h2_num_fgc_stacks(), "worker %u out of range", worker_id);

#ifdef TERA_DBG_PHASES
  stdprint << "BACKREF: pushing reference " << p << "\n";
#endif // TERA_DBG_PHASES

  if (TeraHeapStatistics)
    tera_stats->add_back_ref(worker_id, to_young);

  _fgc_stacks[worker_id].mark.push((oop *)p);
  _fgc_stacks[worker_id].adjust.push((oop *)p);
}

// Push a humongous region that is marked to move to H2 to the humongous
// stack of thread `worker_id`, to move it during the compaction phase.
inline void TeraHeap::h2_push_humongous_start(uint worker_id, HeapRegion *hr) {
  assert(worker_id < h2_num_fgc_stacks(), "worker %u out of range", worker_id);
  _fgc_stacks[worker_id].humongous.push(hr);
}

// Pop a backward reference from adjust stack `stack_id`
inline oop* TeraHeap::h2_pop_back_ref_to_adjust(uint stack_id) {
  Stack<oop *, mtGC>& s = _fgc_stacks[stack_id].adjust;
  return s.is_empty() ? NULL : s.pop();
}

// Pop the first region of a humongous object to move to H2 from humongous
// stack `stack_id`
inline HeapRegion *TeraHeap::h2_pop_humongous_start(uint stack_id) {
  Stack<HeapRegion *, mtGC>& s = _fgc_stacks[stack_id].humongous;
  return s.is_empty() ? NULL : s.pop();
}

inline void TeraHeap::mark_used_region(HeapWord *obj) {
  ::h2_mark_used((char *) obj);

  if (H2LivenessAnalysis)
    cast_to_oop(obj)->set_live();
}

// Check and record metadata for references involving a cross-heap edge (H1 <->
// H2) and/or a cross-H2-region edge during H1->H2 relocation.
//
// This helper is only meaningful while the current thread is processing an
// object that has an H2 destination (_group_state[thread_id].h2_addr != NULL). Otherwise
// it is a no-op.
//
// Behavior:
//  - If the referenced object (`obj`) is already in H2, record a
//    cross-H2-region dependency between the destination H2 region of the
//    relocating object and the H2 region containing `obj` (via group_regions()).
//  - If the referenced object is in H1, compute the corresponding field slot in
//    the H2 copy and mark the H2 card table for that slot as dirty, so the H2
//    side will rescan/remember this backward (H2->H1) reference.
//  - In some paths (e.g., backward pointer popped from th_adjust_stack where
//    h1_addr == NULL), do nothing because the card is already
//    dirty from a prior GC phase.
inline void TeraHeap::check_for_cross_heap_cross_h2_region_ref(uint thread_id, HeapWord *obj, void *obj_field) {
  const H2GroupState& g = _group_state[thread_id];

  // Object is not going to be moved to TeraHeap
  if (g.h2_addr == NULL)
    return;

  if (is_in_h2(obj)) {
    group_regions(g.h2_addr, obj);
    return;
  }

  // If it is an already backward pointer popped from th_adjust_stack
  // then do not mark the card as dirty because it is already marked
  // from minor gc.
  if (g.h1_addr == NULL)
    return;

  // Mark the H2 card table as dirty if obj is in H1 (backward
  // reference)
  size_t diff = (HeapWord *)obj_field - g.h1_addr;
  assert(diff > 0 && (diff <= (uint64_t) cast_to_oop(g.h1_addr)->size()),
         "Diff out of range: %lu", diff);
  HeapWord *h2_obj_field = g.h2_addr + diff;
  assert(is_in_h2(g.h2_addr), "Shoud be in H2");

  th_card_table()->th_write_ref_field(h2_obj_field);
}

// Check if the object `obj` is an instance, or an array of instances, of
// the following metadata classes:
// - Instance Mirror Klass
// - Instance Reference Klass
// - Instance Class Loader Klass
// These are the instance klass kinds other than _kind_other.
// If yes return true, otherwise false
inline bool TeraHeap::is_metadata(oop obj) {
  Klass* k = obj->klass();

  if (k->is_instance_klass()) {
    return !InstanceKlass::cast(k)->is_other_instance_klass();
  }

  if (k->is_objArray_klass()) {
    Klass* ek = ObjArrayKlass::cast(k)->element_klass();
    return ek->is_instance_klass() && !InstanceKlass::cast(ek)->is_other_instance_klass();
  }

  return false;
}

inline void TeraHeap::tag_root_for_h2(oop obj, uint64_t label) {
  // If the object is already in H2 then do not mark its teraflag
  if (is_in_h2(obj))
    return;

  // Class mirrors, Reference objects, class loaders (and arrays of them)
  // stay in H1, as for the objects that the GCs mark for H2
  if (is_metadata(obj))
    return;

  obj->mark_move_h2(label);
}

#endif // SHARE_GC_TERAHEAP_TERAHEAP_INLINE_HPP
