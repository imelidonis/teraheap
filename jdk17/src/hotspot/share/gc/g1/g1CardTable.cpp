/*
 * Copyright (c) 2001, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/g1/g1CardTable.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/shared/memset_with_concurrent_readers.hpp"
#include "logging/log.hpp"
#include "gc/g1/g1OopClosures.inline.hpp"

void G1CardTable::g1_mark_as_young(const MemRegion& mr) {
  CardValue *const first = byte_for(mr.start());
  CardValue *const last = byte_after(mr.last());

  memset_with_concurrent_readers(first, g1_young_gen, last - first);
}

#ifndef PRODUCT
void G1CardTable::verify_g1_young_region(MemRegion mr) {
  verify_region(mr, g1_young_gen,  true);
}
#endif

void G1CardTableChangedListener::on_commit(uint start_idx, size_t num_regions, bool zero_filled) {
  // Default value for a clean card on the card table is -1. So we cannot take advantage of the zero_filled parameter.
  MemRegion mr(G1CollectedHeap::heap()->bottom_addr_for_region(start_idx), num_regions * HeapRegion::GrainWords);
  _card_table->clear(mr);
}

void G1CardTable::initialize(G1RegionToSpaceMapper* mapper) {
  mapper->set_mapping_changed_listener(&_listener);

  _byte_map_size = mapper->reserved().byte_size();

  _guard_index = cards_required(_whole_heap.word_size()) - 1;
  _last_valid_index = _guard_index - 1;

  HeapWord* low_bound  = _whole_heap.start();
  HeapWord* high_bound = _whole_heap.end();

  _cur_covered_regions = 1;
  _covered[0] = _whole_heap;

  _byte_map = (CardValue*) mapper->reserved().start();
  _byte_map_base = _byte_map - (uintptr_t(low_bound) >> card_shift);
  assert(byte_for(low_bound) == &_byte_map[0], "Checking start of map");
  assert(byte_for(high_bound-1) <= &_byte_map[_last_valid_index], "Checking end of map");

  log_trace(gc, barrier)("G1CardTable::G1CardTable: ");
  log_trace(gc, barrier)("    &_byte_map[0]: " INTPTR_FORMAT "  &_byte_map[_last_valid_index]: " INTPTR_FORMAT,
                         p2i(&_byte_map[0]), p2i(&_byte_map[_last_valid_index]));
  log_trace(gc, barrier)("    _byte_map_base: " INTPTR_FORMAT,  p2i(_byte_map_base));
}

bool G1CardTable::is_in_young(oop obj) const {
  volatile CardValue* p = byte_for(obj);
  return *p == G1CardTable::g1_young_card_val();
}

bool G1CardTable::is_in_old(oop obj) const {
  volatile CardValue* p = byte_for(obj);
  return *p == G1CardTable::g1_old_card_val();
}

#ifdef TERA_CARDS

void G1CardTable::h2_scavenge_contents_parallel(
                            H2ToH1G1PushContentsClosure* cl,
													  uint stripe_number, //_worker_id
													  uint stripe_total,  //total gc threads
													  bool scan_old       //the old cards are scanned : full gc, mixed gc, young + initial marking gc
                            ) {
                     
  ObjectStartArray* start_array = Universe::teraHeap()->h2_start_array();
  HeapWord* space_top = (HeapWord *)Universe::teraHeap()->h2_top_addr();

  // HeapWord* space_top = (HeapWord *)Universe::teraHeap()->h2_top_addr_snapshot();

  assert(space_top != NULL , "snapshot of the tera heap top should have been taken");
  
	int ssize = TeraStripeSize; // Naked constant!  Default work unit = 8M
	int dirty_card_count = 0;
	oop* sp_top = (oop*)space_top;
	CardValue* start_card = byte_for(Universe::teraHeap()->h2_start_addr());
	CardValue* end_card = byte_for(sp_top - 1) + 1;

	assert(start_card != end_card, "Sanity check");

	 // Preventing scanning objects more than onece
	oop* last_scanned = NULL;

	// In the following loop, in the specified area (currently only H2)
	// process from bottom (start_card in the above variable declaration) to top
	// (end_card in the above variable declaration).

	// The width of the stripe ssize*stripe_total must be consistent with the
	// number of so that the complete slice is covered.
	//
	// However, it does not process everything from bottom to top, but divides
	// this into ParallelGCThreads and processes them in parallel.
	//
	// Your responsibility is indicated by the argument stripe_number.
	// Specifically, it is processed as follows.
	//  (1) Process ssize bytes from the address of start (start_card) + stripe_number * ssize
  //  (2) Next, advance by ssize * ParallelGCThreads and process ssize bytes.
	//      That is, ssize bytes from the address of the beginning + stripe_number * ssize + ssize * ParallelGCThreads)
	//  (3) Further advance by ssize * ParallelGCThreads. Repeat until the
	//  end_card is reached
	//
	// To be precise, start_card and end_card are not the start address or end
	// address itself to be processed, but the address of the entry in the
	// CardTableExtension corresponding to that address.
	// The value 512 of ssize corresponds to 8M because the size of 1 card is
	// 16K.
	size_t slice_width = ssize * stripe_total;

  // scan until the top of the tera heap is met
	for (CardValue* slice = start_card; slice < end_card; slice += slice_width) {

  	CardValue* worker_start_card = slice + stripe_number * ssize;
		if (worker_start_card >= end_card)
			return; // We're done.

    CardValue* worker_end_card = worker_start_card + 
      Universe::teraHeap()->h2_continuous_regions(addr_for(worker_start_card)) * ssize;
		if (worker_end_card > end_card) {
			worker_end_card = end_card;
		}

		// We do not want to scan objects more than once. In order to accomplish
		// this, we assert that any object with an object head inside our
		// 'slice' belongs to us. We may need to extend the range of scanned
		// cards if the last object continues into the next 'slice'
		//
		// Note! ending cards are exclusive!
		HeapWord* slice_start = addr_for(worker_start_card);
		HeapWord* slice_end = MIN2((HeapWord*) sp_top, addr_for(worker_end_card));

		// If there are not objects starting within the chunk, skip it.
		if (!start_array->th_object_starts_in_range(slice_start, slice_end))
			continue;
    
    
		// Update our beginning addr
    if (!Universe::teraHeap()->check_if_valid_object(slice_start) || 
      !Universe::teraHeap()->is_start_of_region(slice_start))
      continue;

    HeapWord* first_object = Universe::teraHeap()->get_first_object_in_region(slice_start);
    assert(oopDesc::is_oop_or_null(cast_to_oop(first_object)), "check for header");

		oop* first_object_within_slice = (oop*) first_object;

		assert(slice_end <= (HeapWord*)sp_top, "Last object in slice crosses space boundary");
		assert(is_valid_card_address(worker_start_card), "Invalid worker start card");
		assert(is_valid_card_address(worker_end_card), "Invalid worker end card");
		// Note that worker_start_card >= worker_end_card is legal, and happens when
		// an object spans an entire slice.
		assert(worker_start_card <= end_card, "worker start card beyond end card");
		assert(worker_end_card <= end_card, "worker end card beyond end card");

    // this thread scans from worker_start_card to worker_end_card 
    // those cards are in the threads' stripe
		CardValue* current_card = worker_start_card;
		while (current_card < worker_end_card) {
			
      // skip any clean cards (no back refs here)
      while (current_card < worker_end_card && th_card_is_clean(*current_card, scan_old)) {
        current_card++;
			}

      //dirty card found
			CardValue* first_unclean_card = current_card;
			// Find last dirty card in sequence
			while (current_card < worker_end_card && !th_card_is_clean(*current_card, scan_old)) {
				
        while (current_card < worker_end_card && !th_card_is_clean(*current_card, scan_old)) {
					current_card++;
        }
        
        if (current_card < worker_end_card) {
          // Some objects may be large enough to span several cards. If such
          // an object has more than one dirty card, separated by a clean card,
          // we will attempt to scan it twice. The test against "last_scanned"
          // prevents the redundant object scan, but it does not prevent newly
          // marked cards from being cleaned.
          HeapWord* last_object_in_dirty_region;
          size_t size_of_last_object;
          HeapWord* end_of_last_object;
          if (!Universe::teraHeap()->check_if_valid_object(addr_for(current_card)-1)) {
            end_of_last_object = Universe::teraHeap()->get_last_object_end(addr_for(current_card)- 1);
          }
          else 
          {
            last_object_in_dirty_region = start_array->th_object_start(addr_for(current_card)-1);
            size_of_last_object = cast_to_oop(last_object_in_dirty_region)->size();
            end_of_last_object = last_object_in_dirty_region + size_of_last_object;
          }

					CardValue* ending_card_of_last_object = byte_for(end_of_last_object);
					assert(ending_card_of_last_object <= worker_end_card, "ending_card_of_last_object is greater than worker_end_card");

					if (ending_card_of_last_object > current_card) {
						// This means the object spans the next complete card.
						// We need to bump the current_card to ending_card_of_last_object
						current_card = ending_card_of_last_object;
					}
				}
			}

      // This is the next clean card, after a series of dirty cards
			CardValue* following_clean_card = current_card;
      if (first_unclean_card < worker_end_card && Universe::teraHeap()->check_if_valid_object(addr_for(first_unclean_card))) {
        oop* p = NULL;
        if (Universe::teraHeap()->is_start_of_region(addr_for(first_unclean_card))){
          p = (oop*) addr_for(first_unclean_card); // p= the obj is at the beggining of the card 
        } else {
          p = (oop*) start_array->th_object_start(addr_for(first_unclean_card)); // p= the obj overlaps over 2 cards. we scroll back from the start of he card, to find he begining of he obj 
        }
        assert((HeapWord*)p <= addr_for(first_unclean_card), "checking");

				// "p" should always be >= "last_scanned" because newly GC
				// dirtied cards are no longer scanned again (see comments at
				// end of loop on the increment of "current_card"). Test that
				// hypothesis before removing this code.
				// If this code is removed, deal with the first time through the
				// loop when the last_scanned is the object starting in the
				// previous slices.
				assert((p >= last_scanned) ||
			    			(last_scanned == first_object_within_slice),
						"Should no longer be possible");
				if (p < last_scanned) {
					// Avoid scanning more than once; this can happen because
					// newgen cards set by GC may a different set than the
					// originally dirty set
					p = last_scanned;
				}

				oop* to = (oop*)addr_for(following_clean_card);
        
        if (to > sp_top) {
          to = sp_top;
        }
                
        assert(first_unclean_card >= worker_start_card, 
                "first unclean card is less than worker start card");
        assert(following_clean_card <= worker_end_card,
               "Following clean card: %p must be less than worker end card %p",
               following_clean_card, worker_end_card);

        //clear all the dirty cards in the sequence we found
        //dirty cards are the non-clean cards found for the corresponding collection
        //Later on they become old/young or stay clean
				while (first_unclean_card < following_clean_card) {
					*first_unclean_card++ = clean_card;
				}

        // p : the first obj in the dirty card sequence
        // to: the following-obj after the top-obj
        // traverse all the objects in between and find all the back ptrs
				while (p < to) {          
					oop m = cast_to_oop(p);

          assert(!Universe::teraHeap()->is_metadata(m), "its metadata");

					assert(oopDesc::is_oop_or_null(m), "check for header");

          //if its typeArray then it doesnt have refs in it. No need to scan it
          if(!m->klass()->is_typeArray_klass()){ 
            m->oop_iterate_backwards(cl);
            // cl->th_trim_queue_partially();           
          }



          if (!Universe::teraHeap()->check_if_valid_object((HeapWord *)p + m->size()))
            break;
          
					p += m->size();

				}

				last_scanned = p;
			}
			// "current_card" is still the "following_clean_card" or
			// the current_card is >= the worker_end_card so the
			// loop will not execute again.
			assert((current_card == following_clean_card) ||
					(current_card >= worker_end_card) || Universe::teraHeap()->check_if_valid_object(addr_for(current_card)), 
					"current_card should only be incremented if it still equals "
					"following_clean_card");
			// Increment current_card so that it is not processed again.
			// It may now be dirty because a old-to-young pointer was
			// found on it an updated.  If it is now dirty, it cannot be
			// be safely cleaned in the next iteration.
			current_card++;
		}
	} 
  
}

#endif // TERA_CARDS
