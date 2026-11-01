#include "gc/teraHeap/teraHeap.inline.hpp"
#include "memory/memRegion.hpp"
#include "memory/padded.inline.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/java.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "logging/log.hpp"
#include "utilities/powerOfTwo.hpp"


char *TeraHeap::_start_addr = NULL;
char *TeraHeap::_stop_addr = NULL;
size_t TeraHeap::_size = 0;

// Constructor of TeraHeap
TeraHeap::TeraHeap() {
  uint64_t align = CardTable::th_ct_max_alignment_constraint();

  if (AllocateH2At == NULL || H2FileSize == 0) {
    ShouldNotReachHere();
  }

  // The allocator needs a power of two (region index = shift) that is a
  // multiple of the H2 card-table alignment: the heap one page of cards
  // covers (32MB with 8KB cards and 4KB pages, 512MB with 64KB pages).
  // The default grows to the alignment on systems with large pages, so
  // the same command line works everywhere
  if (FLAG_IS_DEFAULT(H2RegionSize) && H2RegionSize < align) {
    FLAG_SET_ERGO(H2RegionSize, (size_t) align);
  }
  if (!is_power_of_2(H2RegionSize) || H2RegionSize < align || H2RegionSize % align != 0) {
    vm_exit_during_initialization(err_msg("H2RegionSize (" SIZE_FORMAT ") must be a power of two "
                                          "and a multiple of " UINT64_FORMAT " bytes",
                                          H2RegionSize, align));
  }
  if (H2FileSize / H2RegionSize < H2MaxPartitions) {
    vm_exit_during_initialization(err_msg("H2FileSize / H2RegionSize (" SIZE_FORMAT ") must be at "
                                          "least H2MaxPartitions (" UINTX_FORMAT ")",
                                          (size_t) (H2FileSize / H2RegionSize), H2MaxPartitions));
  }
  _h2_stripe_cards = H2RegionSize >> CardTable::th_card_shift;

  ::h2_init(align, AllocateH2At, H2FileSize, H2RegionSize, H2MaxPartitions, ParallelGCThreads,
            TeraH2GroupByGCThread);
  ::h2_protect_regions_on_free(ProtectH2RegionsOnFree ? 1 : 0);

  _start_addr = ::h2_start_addr_mem_pool();
  _stop_addr = ::h2_stop_addr_mem_pool();
  _size = _stop_addr - _start_addr;
  _h2_card_table = NULL;

  // NOTE: not freed, TeraHeap lives until the process ends
  _group_state = PaddedArray<H2GroupState, mtGC>::create_unfreeable(ParallelGCThreads);
  for (uint i = 0; i < ParallelGCThreads; i++) {
    _group_state[i].h1_addr = NULL;
    _group_state[i].h2_addr = NULL;
  }

  // NOTE: not freed, TeraHeap lives until the process ends
  _fgc_stacks = PaddedArray<H2FullGCStacks, mtGC>::create_unfreeable(ParallelGCThreads);

  // G1 learns the H2 copy cost from the per-worker H2 times and bytes,
  // also without the statistics
  tera_stats = NULL;
  if (TeraHeapStatistics || h2_cost_timers()) {
    tera_stats = new TeraStatistics();
  }

  _as_last_allocated = 0;
  _as_taken_total = 0;
  _as_freed = 0;
  _as_freed_total = 0;
  _as_free_ms = 0.0;
  _as_free_ms_total = 0.0;

  _fgc_write_mode = parse_fgc_write_mode(TeraH2FullGCWriteMode);

  _shadow_on = false;
  _shadow_mixed = false;
  _shadow_switch_skipped = 0;
  memset(&_shadow_at_start, 0, sizeof(_shadow_at_start));
  memset(&_shadow_at_last_end, 0, sizeof(_shadow_at_last_end));
  _shadow_start_ms = 0.0;
  _rand_faults_ms = 0.0;
  _alloc_state_ms = 0.0;
  h2_shadow_init();
}

void TeraHeap::h2_shadow_init() {
  if (!TeraH2Shadow)
    return;
#if defined(FMAP_HYBRID)
  // This build changes the huge page advice of the whole H2 range in young
  // GCs
  warning("TeraH2Shadow needs the default H2 write path (memcpy); disabled");
  return;
#else
  if (::h2_shadow_init(TeraH2ShadowPoolSize, (int) TeraH2ShadowPrecopyRounds) != 0) {
    warning("TeraH2Shadow is not available (see the [SHADOW] message); disabled");
    return;
  }
  ::h2_shadow_set_verify(TeraH2ShadowVerify);
  if (::h2_shadow_start_writer() != 0) {
    warning("TeraH2Shadow: cannot start the writer thread; disabled");
    return;
  }
  _shadow_on = true;
  log_info(gc, init)("TeraH2Shadow: pool " SIZE_FORMAT "M, %u pre-copy rounds",
                     TeraH2ShadowPoolSize / M, (uint) TeraH2ShadowPrecopyRounds);
#endif
}

void TeraHeap::h2_shadow_pause_start(bool mixed) {
  if (!_shadow_on)
    return;
  const jlong t0 = os::javaTimeNanos();
  struct h2_shadow_stats before;
  ::h2_shadow_get_stats(&before);
  ::h2_shadow_switch((uint32_t) TeraH2ShadowSwitchMaxChunks, (long) TeraH2ShadowSwitchMaxPages);
  ::h2_shadow_get_stats(&_shadow_at_start);
  _shadow_switch_skipped = _shadow_at_start.switch_skipped - before.switch_skipped;
  _shadow_mixed = mixed;
  ::h2_shadow_set_active(mixed);
  _shadow_start_ms = (os::javaTimeNanos() - t0) / (double) NANOSECS_PER_MILLISEC;
}

size_t TeraHeap::h2_shadow_budget_bytes() const {
  if (!_shadow_on || !_shadow_mixed)
    return 0;
  // Free chunks now (the switch at pause start and the initial evacuation
  // change them). Each allocating GC worker keeps one partly filled chunk
  // per region, so that many are held back.
  struct h2_shadow_stats now;
  ::h2_shadow_get_stats(&now);
  const uint free_chunks = now.free_chunks;
  return free_chunks > ParallelGCThreads ? (size_t) (free_chunks - ParallelGCThreads) * H2_SHADOW_CHUNK : 0;
}

void TeraHeap::h2_shadow_pause_end() {
  if (!_shadow_on)
    return;
  const jlong t0 = os::javaTimeNanos();
  ::h2_shadow_set_active(false);
  ::h2_shadow_pause_end();
  const double end_ms = (os::javaTimeNanos() - t0) / (double) NANOSECS_PER_MILLISEC;

  if (!TeraHeapStatistics)
    return;
  // ATTACHED / ATTACH_FAILED: in this pause; REDIRECTED_KB / WRITTEN_MB /
  // DEAD: since the previous pause end (writer, Full GCs, remarks).
  // START_MS / END_MS: time of the two shadow calls; RAND_FAULTS_MS /
  // ALLOC_STATE_MS: time of h2_enable_rand_faults() and
  // update_allocator_state() in this pause.
  struct h2_shadow_stats now;
  ::h2_shadow_get_stats(&now);
  const struct h2_shadow_stats& b = _shadow_at_start;
  const struct h2_shadow_stats& e = _shadow_at_last_end;
  thlog_or_tty->print_cr("[H2_SHADOW] | MIXED %d | SWITCH_MS %.3f | SWITCHED %u | DELTA_PAGES " UINT64_FORMAT
                         " | SWITCH_SKIPPED " UINT64_FORMAT " | ATTACHED " UINT64_FORMAT " | ATTACH_FAILED " UINT64_FORMAT
                         " | REDIRECTED_KB " UINT64_FORMAT " | WRITTEN_MB " UINT64_FORMAT " | DEAD " UINT64_FORMAT
                         " | OPEN %u | FULL %u | WRITING %u | READY %u | POOL_FREE %u | POOL %u"
                         " | START_MS %.3f | END_MS %.3f | RAND_FAULTS_MS %.3f | ALLOC_STATE_MS %.3f",
                         _shadow_mixed, b.last_switch_ms, b.last_switched, b.last_delta_pages,
                         _shadow_switch_skipped,
                         now.attached - b.attached, now.attach_failed - b.attach_failed,
                         (now.redirected_bytes - e.redirected_bytes) / K,
                         (now.written_bytes - e.written_bytes) / M, now.dead - e.dead,
                         now.n_open, now.n_full, now.n_writing, now.n_ready, now.free_chunks, now.pool_chunks,
                         _shadow_start_ms, end_ms, _rand_faults_ms, _alloc_state_ms);
  _shadow_at_last_end = now;
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
  return ::h2_cur_alloc_ptr();
}

// Check if the TeraHeap is empty. If yes, return 'true', 'false' otherwise
bool TeraHeap::h2_is_empty() {
  return ::h2_is_empty();
}

void TeraHeap::h2_clear_fgc_stacks() {
  for (uint i = 0; i < h2_num_fgc_stacks(); i++) {
    _fgc_stacks[i].mark.clear(true);
    _fgc_stacks[i].adjust.clear(true);
    _fgc_stacks[i].humongous.clear(true);
  }
}

bool TeraHeap::h2_fgc_stacks_empty() {
  for (uint i = 0; i < h2_num_fgc_stacks(); i++) {
    if (!_fgc_stacks[i].mark.is_empty() || !_fgc_stacks[i].adjust.is_empty() ||
        !_fgc_stacks[i].humongous.is_empty()) {
      return false;
    }
  }
  return true;
}

// Give advise to kernel to expect page references in random order
void TeraHeap::h2_enable_rand_faults() {
  const jlong t0 = os::javaTimeNanos();
#if defined(FMAP_HYBRID)
  ::h2_enable_regular_flts();
#elif defined(MADVISE_ON)
  ::h2_enable_rand();
#endif
  _rand_faults_ms = (os::javaTimeNanos() - t0) / (double) NANOSECS_PER_MILLISEC;
}

// Check if the first object `obj` in the H2 region is valid. If not
// that depicts that the region is empty
bool TeraHeap::check_if_valid_object(HeapWord *obj) {
  return ::h2_is_before_last_object((char *)obj);
}

// Traverses all objects in H2 to check if they are valid.
bool TeraHeap::check_if_valid_h2() {
  if (h2_is_empty()) {
    return true;
  }

  HeapWord *next_region;
  HeapWord *obj_addr;
  oop obj;

  ::h2_start_iterate_regions();

  next_region = (HeapWord *) ::h2_get_next_region();

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

    next_region = (HeapWord *) ::h2_get_next_region();
  }

  return true;
}

// Returns the ending address of the last object in the region obj
// belongs to
HeapWord* TeraHeap::get_last_object_end(HeapWord *obj) {
  return (HeapWord*) ::h2_get_last_object((char *) obj);
}

// Checks if the address of obj is the beginning of a region
bool TeraHeap::is_start_of_region(HeapWord *obj) {
  return ::h2_is_region_start((char *) obj);
}

// Retrurn the start address of the first object of the secific region
HeapWord *TeraHeap::get_first_object_in_region(HeapWord *addr) {
  return (HeapWord*) ::h2_get_first_object((char*)addr);
}

#ifdef ASSERT
void TeraHeap::push_humongous_to_verify(uint worker_id, HeapRegion *hr) {
  assert(worker_id < h2_num_fgc_stacks(), "worker %u out of range", worker_id);
  _fgc_stacks[worker_id].humongous_to_verify.push(hr);
}

bool TeraHeap::humongous_to_verify_empty() {
  for (uint i = 0; i < h2_num_fgc_stacks(); i++) {
    if (!_fgc_stacks[i].humongous_to_verify.is_empty()) {
      return false;
    }
  }
  return true;
}
#endif // ASSERT

// Resets the used field of all regions in H2
void TeraHeap::h2_reset_used_field(void) {
  ::h2_reset_used();
}

// h2_free_regions() callback: clear the start array and the cards of a freed
// region. `last_alloc_end` is an exclusive end (one past the last valid heap
// word).
void TeraHeap::h2_region_freed(char *region_start, char *last_alloc_start,
                               char *last_alloc_end, void *ctx) {
  TeraHeap* const th = static_cast<TeraHeap*>(ctx);
  HeapWord* const start               = reinterpret_cast<HeapWord*>(region_start);
  HeapWord* const last_start          = reinterpret_cast<HeapWord*>(last_alloc_start);
  HeapWord* const last_alloc_end_excl = reinterpret_cast<HeapWord*>(last_alloc_end);

  th->_start_array.th_region_reset(start, last_start);

  // Convert the exclusive end to an inclusive end by subtracting one heap word.
  th->th_card_table()->th_clean_cards(start, last_alloc_end_excl - 1, true /* free regions */);
}

// Frees all unused regions. The allocator protects the freed regions itself
// when ProtectH2RegionsOnFree is set.
void TeraHeap::free_unused_regions(void) {
  double start = TeraH2AllocatorStats ? os::elapsedTime() : 0.0;

  ::h2_free_regions(&TeraHeap::h2_region_freed, this);

  if (TeraHeapStatistics) {
    if (tera_stats->is_in_full_gc()) {
      tera_stats->set_reclaimed_region_count(::h2_num_reclaimed_regions());
    } else {
      thlog_or_tty->print_cr("[REMARK] | RECLAIMED_REGIONS = " SIZE_FORMAT, (size_t) ::h2_num_reclaimed_regions());
    }
  }

  if (TeraH2AllocatorStats) {
    double ms = (os::elapsedTime() - start) * 1000.0;
    _as_freed += ::h2_num_reclaimed_regions();
    _as_free_ms += ms;
    _as_free_ms_total += ms;
  }
}

void TeraHeap::print_h2_alloc_stats(const char *tag) {
  struct h2_alloc_stats s;
  ::h2_get_alloc_stats(&s);

  // Regions freed since the last print are counted in _as_freed, so the
  // regions taken are what the allocated count grew by plus those
  uint64_t taken = s.regions_allocated + _as_freed - _as_last_allocated;
  _as_taken_total += taken;
  _as_freed_total += _as_freed;

  double tail_mb = s.regions_allocated ? (double) s.tail_bytes / s.regions_allocated / M : 0.0;
  double deps_avg = s.regions_allocated ? (double) s.dep_links / s.regions_allocated : 0.0;

  outputStream *out = thlog_or_tty;
  out->print_cr("[%s] | H2_ALLOC REGIONS_ALLOCATED = %lu / %lu", tag, s.regions_allocated, s.regions_total);
  out->print_cr("[%s] | H2_ALLOC REGIONS_TAKEN = %lu (total %lu)", tag, taken, _as_taken_total);
  out->print_cr("[%s] | H2_ALLOC REGIONS_FREED = %lu (total %lu)", tag, _as_freed, _as_freed_total);
  out->print_cr("[%s] | H2_ALLOC TIME_TO_FREE %.3lf ms (total %.3lf ms)", tag, _as_free_ms, _as_free_ms_total);
  out->print_cr("[%s] | H2_ALLOC TAIL_BYTES = %lu (%.1lf MB per region)", tag, s.tail_bytes, tail_mb);
  out->print_cr("[%s] | H2_ALLOC DEPENDENCIES avg %.1lf, max %lu, regions with deps %lu / %lu",
                tag, deps_avg, s.dep_max, s.regions_with_deps, s.regions_allocated);
  out->print_cr("[%s] | H2_ALLOC SPAN_LINKS %lu (objects larger than a region: %lu)",
                tag, s.span_links, s.span_objects);
  out->flush();

  _as_last_allocated = s.regions_allocated;
  _as_freed = 0;
  _as_free_ms = 0.0;
}

#ifdef ASSERT
// Get the next humongous region to verify it is on the free list (only for debugging)
HeapRegion *TeraHeap::verify_next_humongous() {
  // Called by the VM thread after the Full GC tasks
  for (uint i = 0; i < h2_num_fgc_stacks(); i++) {
    Stack<HeapRegion *, mtGC>& s = _fgc_stacks[i].humongous_to_verify;
    if (!s.is_empty()) {
      return s.pop();
    }
  }
  return NULL;
}
#endif // ASSERT

// Allocate new object 'obj' with 'size' in words in TeraHeap.
// Return the allocated 'pos' position of the object
char* TeraHeap::h2_add_object(oop obj, size_t size, size_t worker_id) {
#ifdef ENABLE_SINGLE_THREAD_ALLOCATION
  MutexLocker x(tera_heap_lock);
#endif // ENABLE_SINGLE_THREAD_ALLOCATION

  char *pos;  // Allocation position

  pos = ::h2_allocate(size, (uint64_t)obj->get_obj_group_id(), (uint64_t)obj->get_obj_part_id(), worker_id);

  // `size` is in words: the object must end at or before the end of H2
  assert((HeapWord *) pos + size <= (HeapWord*) _stop_addr, "H2 is Out of Memory\n");

  _start_array.th_allocate_block((HeapWord *)pos);

  return pos;
}

int TeraHeap::h2_continuous_regions(HeapWord *addr) {
  assert(is_in_h2(addr), "Error");
  return ::h2_get_num_of_continuous_regions((char *)addr);
}

// Move object with size 'size' from source address 'src' to the h2
// destination address 'dst'
void TeraHeap::h2_move_obj(HeapWord *src, HeapWord *dst, size_t size, bool is_in_fgc) {
  assert(src != NULL, "Src address should not be null");
  assert(dst != NULL, "Dst address should not be null");
  assert(size > 0, "Size should not be zero");

  if (is_in_fgc) {
    switch (_fgc_write_mode) {
      case H2_WRITE_BUFFER_ASYNC:
        ::h2_buffer_insert((char *)src, (char *)dst, size);
        return;
      case H2_WRITE_ASYNC:
        ::h2_awrite((char *)src, (char *)dst, size);
        return;
      case H2_WRITE_SYNC:
        ::h2_write((char *)src, (char *)dst, size);
        return;
      case H2_WRITE_MEMCPY:
        break;
    }
  }

  // We use memcpy instead of memmove to avoid the extra copy of the
  // data in the buffer.
  memcpy(dst, src, size * 8);

  //you should not do cast_to_oop(src)->init_mark()
  //bcs the obj left behind in h1, must still have the forwarding ptr in its header
  //for the pointer adjustment (when other obj point to this obj during evac, they will be dereferenced based on that ptr)
//   cast_to_oop(dst)->init_mark();

#ifdef TERA_DBG_PHASES
  {
    stdprint << "### Phase 4 Moved to H2 from " << src << " to " << dst << "\n";
  }
#endif // DEBUG
}

// Complete the transfer of the objects in H2
void TeraHeap::h2_complete_transfers() {
  switch (_fgc_write_mode) {
    case H2_WRITE_BUFFER_ASYNC:
      // Write the objects left in the buffers, then wait for all writes
      ::h2_free_all_buffers();
      while (!::h2_areq_completed());
      break;
    case H2_WRITE_ASYNC:
      while (!::h2_areq_completed());
      break;
    case H2_WRITE_SYNC:     // pwrite returned: nothing in flight
    case H2_WRITE_MEMCPY:   // already in the H2 mapping
      break;
  }
}

TeraHeap::H2FullGCWriteMode TeraHeap::parse_fgc_write_mode(const char* mode) {
  H2FullGCWriteMode m;
  if (mode != NULL && strcmp(mode, "buffer-async") == 0) {
    m = H2_WRITE_BUFFER_ASYNC;
  } else if (mode != NULL && strcmp(mode, "async") == 0) {
    m = H2_WRITE_ASYNC;
  } else if (mode != NULL && strcmp(mode, "sync") == 0) {
    m = H2_WRITE_SYNC;
  } else if (mode != NULL && strcmp(mode, "memcpy") == 0) {
    m = H2_WRITE_MEMCPY;
  } else {
    vm_exit_during_initialization(err_msg("Unknown TeraH2FullGCWriteMode '%s'",
                                          mode == NULL ? "" : mode),
                                  "Use buffer-async, async, sync or memcpy");
    return H2_WRITE_BUFFER_ASYNC;
  }
  log_info(gc, init)("TeraHeap: Full GC write mode %s", mode);
  return m;
}

void TeraHeap::update_allocator_state() {
  const jlong t0 = os::javaTimeNanos();
  ::h2_update_allocator_global_state();
  _alloc_state_ms = (os::javaTimeNanos() - t0) / (double) NANOSECS_PER_MILLISEC;
  assert((HeapWord *) h2_top_addr() < (HeapWord*) _stop_addr, "H2 is Out of Memory\n");
}

// Make every card of H2 dirty
void TeraHeap::dirty_all_cards() {
  HeapWord* const start = reinterpret_cast<HeapWord*>(_start_addr);
  HeapWord* const end_excl = reinterpret_cast<HeapWord*>(_stop_addr);

  // Convert exclusive end to inclusive end.
  th_card_table()->th_dirty_cards(start, end_excl - 1);
}


void TeraHeap::print_sigsegv_info(void *siginfo) {
  void *addr = ((siginfo_t *)siginfo)->si_addr;
  fprintf(stderr, "SIGSEGV at address %p (si_code=%d)\n", addr, ((siginfo_t *)siginfo)->si_code);

  if (is_in_h2(addr)) {
    uint64_t region_idx = ::h2_region_containing_addr((char *)addr);
    fprintf(stderr, "L The address is in H2 in region %lu which is used=%d\n", region_idx, ::h2_is_used(region_idx));
    fprintf(stderr, "L Region: {\n");
    fprintf(stderr, "     - first object:   %p\n", ::h2_get_first_object((char *)addr));
    fprintf(stderr, "     - last alloc end: %p\n", ::h2_get_last_object((char *)addr));
    fprintf(stderr, "  }\n");
  }
}

bool TeraHeap::is_in_reclaimed_region(char *addr) {
  uint64_t region_idx = ::h2_region_containing_addr(addr);
  return (!::h2_is_used(region_idx));
}
