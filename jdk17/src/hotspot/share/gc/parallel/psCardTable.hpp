/*
 * Copyright (c) 2001, 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_GC_PARALLEL_PSCARDTABLE_HPP
#define SHARE_GC_PARALLEL_PSCARDTABLE_HPP

#include "gc/shared/cardTable.hpp"
#include "oops/oop.hpp"
#include "gc/shared/gc_globals.hpp"

class MutableSpace;
class ObjectStartArray;
class PSPromotionManager;

#ifdef TERA_CARDS
class H2ToH1Closure;
#endif

class PSCardTable: public CardTable {
 private:
  // Support methods for resizing the card table.
  // resize_commit_uncommit() returns true if the pages were committed or
  // uncommitted
  bool resize_commit_uncommit(int changed_region, MemRegion new_region);
  void resize_update_card_table_entries(int changed_region,
                                        MemRegion new_region);
  void resize_update_committed_table(int changed_region, MemRegion new_region);
  void resize_update_covered_table(int changed_region, MemRegion new_region);

  void verify_all_young_refs_precise_helper(MemRegion mr);

  enum ExtendedCardValue {
    youngergen_card   = CT_MR_BS_last_reserved + 1,
#ifdef TERA_CARDS
    oldergen_card     = CT_MR_BS_last_reserved + 2,
#endif
    verify_card       = CT_MR_BS_last_reserved + 5
  };

 public:
  PSCardTable(MemRegion whole_heap) : CardTable(whole_heap) {}

#ifdef TERA_CARDS
  PSCardTable(MemRegion whole_heap, MemRegion th_whole_heap) : CardTable(whole_heap, th_whole_heap) {}
#endif

  static CardValue youngergen_card_val() { return youngergen_card; }
  static CardValue verify_card_val()     { return verify_card; }

  // Scavenge support
  void scavenge_contents_parallel(ObjectStartArray* start_array,
                                  MutableSpace* sp,
                                  HeapWord* space_top,
                                  PSPromotionManager* pm,
                                  uint stripe_number,
                                  uint stripe_total);

#ifdef TERA_CARDS
  // Scavenge support for TeraCache
  // 'is_young' field shows if we use this function during minor gc or
  // we use this function only to trace dirty objects of TeraCache
  
  
  void h2_scavenge_contents_parallel( H2ToH1Closure* cl,
                                  uint stripe_number,
                                  uint stripe_total, 
                                  bool scan_old
                                  // ,G1ParScanThreadState* pss
                                  );
#endif //TERA_CARDS

  bool addr_is_marked_imprecise(void *addr);
  bool addr_is_marked_precise(void *addr);

  void set_card_newgen(void* addr)   { CardValue* p = byte_for(addr); *p = verify_card; }

  // Testers for entries
  static bool card_is_dirty(int value)      { return value == dirty_card; }
  static bool card_is_newgen(int value)     { return value == youngergen_card; }
  static bool card_is_clean(int value)      { return value == clean_card; }
  static bool card_is_verify(int value)     { return value == verify_card; }

#ifdef TERA_CARDS
  static bool card_is_oldgen(int value)     {return value == oldergen_card; }

  static bool th_card_is_clean(int value, bool scan_old) {
#ifdef DISABLE_TRAVERSE_OLD_GEN
	  if (scan_old)
		  return card_is_clean(value); 

	  return (card_is_clean(value) || card_is_oldgen(value));
#else
	  return card_is_clean(value);
#endif
  }


    //##!! remove
  const char* card_flag(const void *p){
    CardValue* card = byte_for(p);
    char* result = NEW_RESOURCE_ARRAY( char, 8);

    if( card_is_oldgen(*card) ){
      int i=0;
      result[i++] = 'o';
      result[i++] = 'l';
      result[i++] = 'd';
      result[i++] = '\0';
      return result;
    }else if( card_is_newgen(*card) ){
      int i=0;
      result[i++] = 'y';
      result[i++] = 'o';
      result[i++] = 'u';
      result[i++] = 'n';
      result[i++] = 'g';
      result[i++] = '\0';      
      return result;
    }else if( card_is_dirty(*card) ){
      int i=0;
      result[i++] = 'd';
      result[i++] = 'i';
      result[i++] = 'r';
      result[i++] = 't';
      result[i++] = 'y';
      result[i++] = '\0';
      return result;
    }else if( card_is_verify(*card) ){
      int i=0;
      result[i++] = 'v';
      result[i++] = 'e';
      result[i++] = 'r';
      result[i++] = 'i';
      result[i++] = 'f';
      result[i++] = 'y';
      result[i++] = '\0';
      return result;
    }else  if( card_is_clean(*card) ){
      int i=0;
      result[i++] = 'c';
      result[i++] = 'l';
      result[i++] = 'e';
      result[i++] = 'a';
      result[i++] = 'n';
      result[i++] = '\0';      
      return result;
    }else{ 
      int i=0;
      result[i++] = 'i';
      result[i++] = 'd';
      result[i++] = 'k';
      result[i++] = '\0';
      return result;
    };
  }

#endif

  // Card marking
  void inline_write_ref_field_gc(void* field, oop new_val, bool promote_to_oldgen=false) {
    CardValue* byte = byte_for(field);
#ifdef TERA_CARDS
    if (EnableTeraHeap && Universe::is_field_in_h2(field)) {
      if (promote_to_oldgen) {
        // h2->h1 : if h1 is old => promote_to_oldgen=true
        //          if h1 is young => promote_to_oldgen=false
        Atomic::cmpxchg(byte, (CardValue)clean_card, (CardValue)oldergen_card);
        Atomic::cmpxchg(byte, (CardValue)dirty_card, (CardValue)oldergen_card);
        return;
      }
    }
    *byte = youngergen_card;
#else
    *byte = youngergen_card;
#endif
  }

  // ReduceInitialCardMarks support
  bool is_in_young(oop obj) const;

  // Adaptive size policy support
  // Allows adjustment of the base and size of the covered regions
  void resize_covered_region(MemRegion new_region);
  // Finds the covered region to resize based on the start address
  // of the covered regions.
  void resize_covered_region_by_start(MemRegion new_region);
  // Finds the covered region to resize based on the end address
  // of the covered regions.
  void resize_covered_region_by_end(int changed_region, MemRegion new_region);
  // Finds the lowest start address of a covered region that is
  // previous (i.e., lower index) to the covered region with index "ind".
  HeapWord* lowest_prev_committed_start(int ind) const;

#ifdef ASSERT

  bool is_valid_card_address(CardValue* addr) {
#ifdef TERA_CARDS
    if (EnableTeraHeap) {
      // Check if the address belongs to the address range of the Old Generation
      // or in the TeraCache
      return ((addr >= _byte_map) && (addr < _byte_map + _byte_map_size)
      ||
      ((addr >= _th_byte_map) && (addr < _th_byte_map + _th_byte_map_size)));
    } 
    return (addr >= _byte_map) && (addr < _byte_map + _byte_map_size);
#else
    return (addr >= _byte_map) && (addr < _byte_map + _byte_map_size);
#endif
  }

  #endif // ASSERT

  // Verification
  void verify_all_young_refs_imprecise();
  void verify_all_young_refs_precise();
};

#endif // SHARE_GC_PARALLEL_PSCARDTABLE_HPP
