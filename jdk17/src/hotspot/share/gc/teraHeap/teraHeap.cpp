#include "gc/teraHeap/teraHeap.hpp"
#include "gc/parallel/psVirtualspace.hpp"
#include "gc/parallel/psVirtualspace.hpp"
//#include "gc/parallel/psCompactionManager.hpp"
//#include "memory/card.hpp"
#include "memory/memRegion.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"


char *TeraHeap::_start_addr = NULL;
char *TeraHeap::_stop_addr = NULL;

Stack<oop *, mtGC> TeraHeap::_th_stack;
Stack<oop *, mtGC> TeraHeap::_th_adjust_stack;
Stack<HeapRegion *, mtGC> TeraHeap::_th_humongous_stack;

#ifdef DBG_LOST_REGION
static size_t page_size;
static int times = 0;

static void segv_handler(int sig, siginfo_t *si, void *arg) {
  void *addr = si->si_addr;
  if (times == 0) {
    times++;
    return;
  }

  fprintf(stderr, "SIGSEGV at address %p (si_code=%d)\n", addr, si->si_code);
  if (Universe::teraHeap()->is_in_h2(addr)) {
    uint64_t region_idx = region_containing_addr((char *)addr);
    fprintf(stderr, "L The address is in H2 in region %lu which is used=%d\n", region_idx, is_used(region_idx));
    struct region *region = get_region(region_idx);
    fprintf(stderr, "L Region: {\n");
    fprintf(stderr, "     - start:          %p\n", region->start_address);
    fprintf(stderr, "     - last alloc end: %p\n", region->last_allocated_end);
    fprintf(stderr, "  }\n");
  }
  _exit(128 + SIGSEGV);
}

void install_segv_handler() {
  page_size = sysconf(_SC_PAGESIZE);
  struct sigaction sa;
  sa.sa_sigaction = segv_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  if (sigaction(SIGSEGV, &sa, NULL) != 0) {
    perror("sigaction");
    exit(1);
  }
}
#endif // DBG_LOST_REGION

// Constructor of TeraHeap
TeraHeap::TeraHeap() {
  uint64_t align = CardTable::th_ct_max_alignment_constraint();

  if (AllocateH2At == NULL || H2FileSize == 0) {
    ShouldNotReachHere();
  }

  init(align, AllocateH2At, H2FileSize, H2MaxPartitions);

  _start_addr = start_addr_mem_pool();
  _stop_addr = stop_addr_mem_pool();

  h1_addr_arr = NEW_C_HEAP_ARRAY(HeapWord*, ParallelGCThreads, mtGC);
  h2_addr_arr = NEW_C_HEAP_ARRAY(HeapWord*, ParallelGCThreads, mtGC);

  for (uint i = 0; i < ParallelGCThreads; i++) {
    h1_addr_arr[i] = NULL;
    h2_addr_arr[i] = NULL;
  }

  obj_h1_addr = NULL;
  obj_h2_addr = NULL;

  if (TeraHeapStatistics) {
    tera_stats = new TeraStatistics();
  }
}

// Destructor of TeraHeap
TeraHeap::~TeraHeap() {
  // TODO: not called
  FREE_C_HEAP_ARRAY(HeapWord*, h1_addr_arr);
  FREE_C_HEAP_ARRAY(HeapWord*, h2_addr_arr);
}

// Return H2 start address
char* TeraHeap::h2_start_addr(void) {
	assert((char *)(_start_addr) != NULL, "H2 allocator is not initialized");
	return _start_addr;
}

// Return H2 stop address
char* TeraHeap::h2_end_addr(void) {
	assert((char *)(_start_addr) != NULL, "H2 allocator is not initialized");
	assert((char *)(_stop_addr) != NULL, "H2 allocator is not initialized");
	return _stop_addr;
}

// Get the top allocated address of the H2. This address depicts the
// end address of the last allocated object in the last region of
// H2.
char* TeraHeap::h2_top_addr(void) {
	return cur_alloc_ptr();
}

// Check if the TeraHeap is empty. If yes, return 'true', 'false' otherwise
bool TeraHeap::h2_is_empty() {
	return r_is_empty();
}

// Check if a pointer belongs to the TeraHeap.
bool TeraHeap::is_in_h2(const void* p) {
	const char* cp = (const char *) p;
	return cp >= _start_addr && cp < _stop_addr;
}

void TeraHeap::h2_clear_back_ref_stacks() {
	// if (TeraHeapStatistics)
	// 	back_ptrs_per_mgc = 0;

  _th_adjust_stack.clear(true);
  _th_stack.clear(true);
}

void TeraHeap::h2_clear_humongous_stack() {
  _th_humongous_stack.clear(true);
}

// Give advise to kernel to expect page references in sequential order
void TeraHeap::h2_enable_seq_faults() {
#if defined(FMAP_HYBRID)
	r_enable_huge_flts();
#elif defined(MADVISE_ON)
	r_enable_seq();
#endif
}

// Give advise to kernel to expect page references in random order
void TeraHeap::h2_enable_rand_faults() {
#if defined(FMAP_HYBRID)
	r_enable_regular_flts();
#elif defined(MADVISE_ON)
	r_enable_rand();
#endif
}

// Check if the first object `obj` in the H2 region is valid. If not
// that depicts that the region is empty
bool TeraHeap::check_if_valid_object(HeapWord *obj) {
  return is_before_last_object((char *)obj);
}

// Traverses all objects in H2 to check if they are valid.
bool TeraHeap::check_if_valid_h2() {
  if (h2_is_empty()) {
    return true;
  }

	HeapWord *next_region;
	HeapWord *obj_addr;
	oop obj;

	start_iterate_regions();

	next_region = (HeapWord *) get_next_region();

	while(next_region != NULL) {
		obj_addr = next_region;

		while (1) {
			obj = cast_to_oop(obj_addr);

      // crash if not valid
      oopDesc::verify(obj);

			if (!check_if_valid_object(obj_addr + obj->size()))
				break;

			obj_addr += obj->size();
		}

		next_region = (HeapWord *) get_next_region();
	}

  return true;
}

// Returns the ending address of the last object in the region obj
// belongs to
HeapWord* TeraHeap::get_last_object_end(HeapWord *obj) {
  return (HeapWord*) get_last_object((char *) obj);
}

// Checks if the address of obj is the beginning of a region
bool TeraHeap::is_start_of_region(HeapWord *obj) {
  return is_region_start((char *) obj);
}

// Retrurn the start address of the first object of the secific region
HeapWord *TeraHeap::get_first_object_in_region(HeapWord *addr) {
  return (HeapWord*) get_first_object((char*)addr);
}

// Add a new entry to `obj1` region dependency list that reference
// `obj2` region
void TeraHeap::group_regions(HeapWord *obj1, HeapWord *obj2) {
	if (is_in_the_same_group((char *) obj1, (char *) obj2)) 
		return;
	MutexLocker x(tera_heap_group_lock);
  references((char*) obj1, (char*) obj2);
}

// Update backward reference stacks that we use in marking and pointer
// adjustment phases of major GC.
void TeraHeap::h2_push_backward_reference(void *p, oop o) {
	MutexLocker x(tera_heap_lock);

#ifdef TERA_DBG_PHASES
  stdprint << "BACKREF: pushing reference " << p << "\n";
#endif // TERA_DBG_PHASES

  if (TeraHeapStatistics)
    tera_stats->add_back_ref();

  _th_stack.push((oop *)p);
  _th_adjust_stack.push((oop *)p);

  assert(!_th_stack.is_empty(), "Sanity Check");
  assert(!_th_adjust_stack.is_empty(), "Sanity Check");
}

// Add humongous region that are marked to move to H2 in a
// seperate stack to move them during the compaction phase.
void TeraHeap::h2_push_humongous_start(void *p) {
  MutexLocker x(tera_heap_lock);
  _th_humongous_stack.push((HeapRegion *)p);
  assert(!_th_humongous_stack.is_empty(), "Sanity Check");
}

// Resets the used field of all regions in H2
void TeraHeap::h2_reset_used_field(void) {
  reset_used();
}

// Prints all the region groups
void TeraHeap::print_region_groups(void) {
  print_groups();
}

void TeraHeap::h2_print_objects_per_region() {
	HeapWord *next_region;
	HeapWord *obj_addr;
	oop obj;

	start_iterate_regions();

	next_region = (HeapWord *) get_next_region();

	while(next_region != NULL) {
		obj_addr = next_region;

		while (1) {
			obj = cast_to_oop(obj_addr);

			fprintf(stderr, "[PLACEMENT] OBJ = %p | RDD = %d | PART_ID = %lu\n", 
           cast_from_oop<HeapWord *>(obj), obj->get_obj_group_id(), obj->get_obj_part_id());

			if (!check_if_valid_object(obj_addr + obj->size()))
				break;

			obj_addr += obj->size();
		}

		next_region = (HeapWord *) get_next_region();
	}
}

// Frees all unused regions
void TeraHeap::free_unused_regions(void) {
	CardTable* const ct = th_card_table();

  struct region_list *region = free_regions();
  while (region != NULL) {
    region_list* const next = region->next;

    HeapWord* const region_start        = reinterpret_cast<HeapWord*>(region->region_start);
    HeapWord* const last_alloc_start    = reinterpret_cast<HeapWord*>(region->last_allocated_start);
    HeapWord* const last_alloc_end_excl = reinterpret_cast<HeapWord*>(region->last_allocated_end);

    _start_array.th_region_reset(region_start, last_alloc_start);

    // `last_alloc_end_excl` is an exclusive end (one past the last valid heap
    // word). Convert to an inclusive end by subtracting one heap word.
    ct->th_clean_cards(region_start, last_alloc_end_excl - 1, true /* free regions */);

#ifdef DBG_PROTECT_FREE_REGIONS
    make_region_inaccessible(region->region_start, GCId::current());
#endif // DBG_PROTECT_FREE_REGIONS
    free(region);
    region = next;
  }

  if (TeraHeapStatistics)
    tera_stats->set_reclaimed_region_count(num_reclaimed_regions());
}

// Pop the objects that are in `_th_stack` and mark them as live
// object. These objects are located in the Java Heap and we need to
// ensure that they will be kept alive.
oop* TeraHeap::h2_get_next_back_reference() {
  return (_th_stack.is_empty() ? NULL : _th_stack.pop());
}

// Prints all active regions
void TeraHeap::print_h2_active_regions(void) {
  print_used_regions();
}

// Get the next backward reference from the stack to adjust
oop* TeraHeap::h2_adjust_next_back_reference() {
  return (!_th_adjust_stack.is_empty() ? _th_adjust_stack.pop() : NULL);
}

// Get the next humongous starting region from the stack to move the whole object to H2
HeapRegion *TeraHeap::h2_get_next_humongous_start() {
  return (!_th_humongous_stack.is_empty() ? _th_humongous_stack.pop() : NULL);
}

// Enable region groupping (multi-threaded)
void TeraHeap::thread_enable_groups(uint thread_id, HeapWord *old_addr, HeapWord* new_addr) {
  if (!EnableTeraHeap)
    return;
  assert(h1_addr_arr[thread_id] == NULL && h2_addr_arr[thread_id] == NULL, "Thread %d corrupted group state.", thread_id);

	h1_addr_arr[thread_id] = old_addr;
	h2_addr_arr[thread_id] = new_addr;
}

// Disables region groupping (multi-threaded)
void TeraHeap::thread_disable_groups(uint thread_id) {
  if (!EnableTeraHeap)
    return;
	h1_addr_arr[thread_id] = NULL;
	h2_addr_arr[thread_id] = NULL;
}

#ifdef PR_BUFFER
// Add an object 'obj' with size 'size' to the promotion buffer. 'New_adr' is
// used to know where the object will move to H2. We use promotion buffer to
// reduce the number of system calls for small sized objects.
void  TeraHeap::h2_promotion_buffer_insert(char* obj, char* new_adr, size_t size) {
	buffer_insert(obj, new_adr, size);
}

// At the end of the major GC flush and free all the promotion buffers.
void TeraHeap::h2_free_promotion_buffers() {
	free_all_buffers();
}
#endif

// Explicit (using systemcall) write 'data' with 'size' to the specific
// 'offset' in the file.
void TeraHeap::h2_write(char *data, char *offset, size_t size) {
	r_write(data, offset, size);
}

// Explicit (using systemcall) asynchronous write 'data' with 'size' to
// the specific 'offset' in the file.
void TeraHeap::h2_awrite(char *data, char *offset, size_t size) {
	r_awrite(data, offset, size);
}

// We need to ensure that all the writes in TeraHeap using asynchronous
// I/O have been completed succesfully.
int TeraHeap::h2_areq_completed() {
	return r_areq_completed();
}

// Fsync writes in TeraHeap
// We need to make an fsync when we use fastmap
void TeraHeap::h2_fsync() {
	r_fsync();
}

// Check if backward adjust stack is empty
bool TeraHeap::h2_is_empty_back_ref_stacks() {
  return _th_adjust_stack.is_empty();
}

// Get the group Id of the objects that belongs to this region. We
// locate the objects of the same group to the same region. We use the
// field 'p' of the object to identify in which region the object
// belongs to.
uint64_t TeraHeap::h2_get_region_groupId(void* p) {
	assert((char *) p != NULL, "Sanity check");
	return get_obj_group_id((char *) p);
}

// Get the partition Id of the objects that belongs to this region. We
// locate the objects of the same group to the same region. We use the
// field 'p' of the object to identify in which region the object
// belongs to.
uint64_t TeraHeap::h2_get_region_partId(void* p) {
	assert((char *) p != NULL, "Sanity check");
	return get_obj_part_id((char *) p);
}

#ifdef DBG_LOST_REGION
// Marks the region containing obj as used
void TeraHeap::mark_used_region(HeapWord *obj, char *from) {
    mark_used((char *) obj, from, GCId::current());

  if (H2LivenessAnalysis)
    cast_to_oop(obj)->set_live();
}
#else
void TeraHeap::mark_used_region(HeapWord *obj) {
    mark_used((char *) obj);

  if (H2LivenessAnalysis)
    cast_to_oop(obj)->set_live();
}
#endif // DBG_LOST_REGION

// Allocate new object 'obj' with 'size' in words in TeraHeap.
// Return the allocated 'pos' position of the object
char* TeraHeap::h2_add_object(oop obj, size_t size) {
	char *pos;			// Allocation position

	pos = allocate(size, (uint64_t)obj->get_obj_group_id(), (uint64_t)obj->get_obj_part_id());
	
	assert((HeapWord *) h2_top_addr() < (HeapWord*) _stop_addr, "H2 is Out of Memory\n");

	_start_array.th_allocate_block((HeapWord *)pos);

	return pos;
}

// Check and record metadata for references involving a cross-heap edge (H1 <->
// H2) and/or a cross-H2-region edge during H1->H2 relocation.
//
// This helper is only meaningful while the current thread is processing an
// object that has an H2 destination (h2_addr_arr[thread_id] != NULL). Otherwise
// it is a no-op.
//
// Behavior:
//  - If the referenced object (`obj`) is already in H2, record a
//  cross-H2-region dependency between the destination H2 region of the
//  relocating object and the H2 region containing `obj` (via group_regions()).
//  - If the referenced object is in H1, compute the corresponding field slot in
//    the H2 copy and mark the H2 card table for that slot as dirty, so the H2
//    side will rescan/remember this backward (H2->H1) reference.
//  - In some paths (e.g., backward pointer popped from th_adjust_stack where
//    h1_addr_arr[thread_id] == NULL), do nothing because the card is already
//    dirty from a prior GC phase.
void TeraHeap::check_for_cross_heap_cross_h2_region_ref(uint thread_id, HeapWord *obj, void *obj_field) {
	// Object is not going to be moved to TeraHeap
	if (h2_addr_arr[thread_id] == NULL) 
		return;

  if (is_in_h2(obj)) {
    group_regions(h2_addr_arr[thread_id], obj); //this has a lock
    return;
	}

  // If it is an already backward pointer popped from th_adjust_stack
  // then do not mark the card as dirty because it is already marked
  // from minor gc.
	if (h1_addr_arr[thread_id] == NULL) 
		return;

  // Mark the H2 card table as dirty if obj is in H1 (backward
  // reference)
	CardTable* const ct = th_card_table();

	size_t diff =  (HeapWord *)obj_field - h1_addr_arr[thread_id];
	assert(diff > 0 && (diff <= (uint64_t) cast_to_oop(h1_addr_arr[thread_id])->size()),
			"Diff out of range: %lu", diff);
	HeapWord *h2_obj_field = h2_addr_arr[thread_id] + diff;
	assert(is_in_h2(h2_addr_arr[thread_id]), "Shoud be in H2");

	ct->th_write_ref_field(h2_obj_field);
}

// Check if the object `obj` is an instance of the following
// metadata class:
// - Instance Mirror Klass
// - Instance Reference Klass
// - Instance Class Loader Klass
// If yes return true, otherwise false
bool TeraHeap::is_metadata(oop obj) {
  if (obj->klass()->is_instance_klass()) {
    InstanceKlass *ik = (InstanceKlass *) obj->klass();
	return (ik->is_mirror_instance_klass() || ik->is_reference_instance_klass() || ik->is_class_loader_instance_klass() );
  }

  if (obj->is_objArray()) {
    objArrayOop arr = objArrayOop(obj);
    if (arr->element_klass()->is_instance_klass()) {
      InstanceKlass *ik = (InstanceKlass *) arr->element_klass();
      return (ik->is_mirror_instance_klass() || ik->is_reference_instance_klass() || ik->is_class_loader_instance_klass());
    }
  }

  return false;
}

int TeraHeap::h2_continuous_regions(HeapWord *addr) {
  assert(is_in_h2(addr), "Error");
  return get_num_of_continuous_regions((char *)addr);
}

bool TeraHeap::h2_object_starts_in_region(HeapWord *obj) {
  return object_starts_from_region((char *)obj);
}

// Move object with size 'size' from source address 'src' to the h2
// destination address 'dst' 
void TeraHeap::h2_move_obj(HeapWord *src, HeapWord *dst, size_t size, bool is_in_fgc) {
  assert(src != NULL, "Src address should not be null");
  assert(dst != NULL, "Dst address should not be null");
  assert(size > 0, "Size should not be zero");

  if (is_in_fgc) {
    h2_promotion_buffer_insert((char *)src, (char *)dst, size);
    return;
  }

#if defined(SYNC)
  h2_write((char *)src, (char *)dst, size);
#elif defined(FMAP)
  h2_write((char *)src, (char *)dst, size);
#elif defined(ASYNC) && defined(PR_BUFFER)
  h2_promotion_buffer_insert((char *)src, (char *)dst, size);
#elif defined(ASYNC) && !defined(PR_BUFFER)
  h2_awrite((char *)src, (char *)dst, size);
#else
  // We use memcpy instead of memmove to avoid the extra copy of the
  // data in the buffer.
  memcpy(dst, src, size * 8);

  //you should not do cast_to_oop(src)->init_mark()
  //bcs the obj left behind in h1, must still have the forwarding ptr in its header
  //for the pointer adjustment (when other obj point to this obj during evac, they will be dereferenced based on that ptr)
//   cast_to_oop(dst)->init_mark(); 

#endif // SYNC

#ifdef TERA_DBG_PHASES
  {
    stdprint << "### Phase 4 Moved to H2 from " << src << " to " << dst << "\n";
  }
#endif // DEBUG
}

// Complete the transfer of the objects in H2
void TeraHeap::h2_complete_transfers() {
  h2_free_promotion_buffers();
  while(!h2_areq_completed());

#if defined(ASYNC) && defined(PR_BUFFER)
  h2_free_promotion_buffers();
  while(!h2_areq_completed());
#elif defined(ASYNC) && !defined(PR_BUFFER)
  while(!h2_areq_completed());
#elif defined(FMAP)
  th_fsync();
#endif
}
  
// Check if the group of regions in H2 is enabled
bool TeraHeap::is_h2_group_enabled() {
  return (obj_h1_addr != NULL  || obj_h2_addr != NULL);
}

// Tera statistics for objects that we move to H2, forward references,
// and backward references.
TeraStatistics* TeraHeap::get_tera_stats() {
  assert(TeraHeapStatistics, "TeraHeapStatistics not enabled!");
  return tera_stats;
}

// Make every card of H2 dirty
void TeraHeap::dirty_all_cards() {
  HeapWord* const start = reinterpret_cast<HeapWord*>(_start_addr);
  HeapWord* const end_excl = reinterpret_cast<HeapWord*>(_stop_addr);

  // Convert exclusive end to inclusive end.
  th_card_table()->th_dirty_cards(start, end_excl - 1);
}

CardTable* TeraHeap::th_card_table() {
  BarrierSet* bs = BarrierSet::barrier_set();
  CardTableBarrierSet* ctbs = barrier_set_cast<CardTableBarrierSet>(bs);
  return ctbs->th_card_table();
}
