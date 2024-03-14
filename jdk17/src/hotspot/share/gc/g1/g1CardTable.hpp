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

#ifndef SHARE_GC_G1_G1CARDTABLE_HPP
#define SHARE_GC_G1_G1CARDTABLE_HPP

#include "gc/g1/g1OopClosures.hpp"
#include "gc/parallel/objectStartArray.inline.hpp"
#include "gc/g1/g1RegionToSpaceMapper.hpp"
#include "gc/shared/cardTable.hpp"
#include "oops/oopsHierarchy.hpp"
#include "utilities/macros.hpp"

class G1CardTable;
class G1RegionToSpaceMapper;

class G1CardTableChangedListener : public G1MappingChangedListener {
 private:
  G1CardTable* _card_table;
 public:
  G1CardTableChangedListener() : _card_table(NULL) { }

  void set_card_table(G1CardTable* card_table) { _card_table = card_table; }

  virtual void on_commit(uint start_idx, size_t num_regions, bool zero_filled);
};

class G1CardTable : public CardTable {
  friend class VMStructs;
  friend class G1CardTableChangedListener;

  G1CardTableChangedListener _listener;

public:
  enum G1CardValues {
    g1_young_gen = CT_MR_BS_last_reserved << 1,
    g1_old_gen = g1_young_gen + 1,

    // During evacuation we use the card table to consolidate the cards we need to
    // scan for roots onto the card table from the various sources. Further it is
    // used to record already completely scanned cards to avoid re-scanning them
    // when incrementally evacuating the old gen regions of a collection set.
    // This means that already scanned cards should be preserved.
    //
    // The merge at the start of each evacuation round simply sets cards to dirty
    // that are clean; scanned cards are set to 0x1.
    //
    // This means that the LSB determines what to do with the card during evacuation
    // given the following possible values:
    //
    // 11111111 - clean, do not scan
    // 00000001 - already scanned, do not scan
    // 00000000 - dirty, needs to be scanned.
    //
    g1_card_already_scanned = 0x1
  };

  static const size_t WordAllClean = SIZE_MAX;
  static const size_t WordAllDirty = 0;

  STATIC_ASSERT(BitsPerByte == 8);
  static const size_t WordAlreadyScanned = (SIZE_MAX / 255) * g1_card_already_scanned;

  G1CardTable(MemRegion whole_heap): CardTable(whole_heap), _listener() {
    _listener.set_card_table(this);
  }

  static CardValue g1_young_card_val() { return g1_young_gen; }
  static CardValue g1_old_card_val() { return g1_old_gen; }
  static CardValue g1_scanned_card_val() { return g1_card_already_scanned; }

  void verify_g1_young_region(MemRegion mr) PRODUCT_RETURN;
  void g1_mark_as_young(const MemRegion& mr);

  size_t index_for_cardvalue(CardValue const* p) const {
    return pointer_delta(p, _byte_map, sizeof(CardValue));
  }

  // Mark the given card as Dirty if it is Clean. Returns whether the card was
  // Clean before this operation. This result may be inaccurate as it does not
  // perform the dirtying atomically.
  inline bool mark_clean_as_dirty(CardValue* card);

  // Change Clean cards in a (large) area on the card table as Dirty, preserving
  // already scanned cards. Assumes that most cards in that area are Clean.
  // Returns the number of dirtied cards that were not yet dirty. This result may
  // be inaccurate as it does not perform the dirtying atomically.
  inline size_t mark_region_dirty(size_t start_card_index, size_t num_cards);

  // Change the given range of dirty cards to "which". All of these cards must be Dirty.
  inline void change_dirty_cards_to(size_t start_card_index, size_t num_cards, CardValue which);

  inline uint region_idx_for(CardValue* p);

  static size_t compute_size(size_t mem_region_size_in_words) {
    size_t number_of_slots = (mem_region_size_in_words / card_size_in_words);
    return ReservedSpace::allocation_align_size_up(number_of_slots);
  }

  // Returns how many bytes of the heap a single byte of the Card Table corresponds to.
  static size_t heap_map_factor() { return card_size; }

  void initialize() {}
  void initialize(G1RegionToSpaceMapper* mapper);

  virtual void resize_covered_region(MemRegion new_region) { ShouldNotReachHere(); }

  virtual bool is_in_young(oop obj) const;
  virtual bool is_in_old(oop obj) const;

  // Testers for entries
  static bool card_is_dirty(int value)      { return value == dirty_card; }
  static bool card_is_clean(int value)      { return value == clean_card; }

#ifdef TERA_CARDS
  static bool card_is_oldgen(int value)     {
    return value == G1CardTable::g1_old_card_val();
  }

  static bool th_card_is_clean(int value, bool scan_old) {
#ifdef DISABLE_TRAVERSE_OLD_GEN
	  if (scan_old)
		  return card_is_clean(value); 

	  return (card_is_clean(value) || card_is_oldgen(value));
#else
	  return card_is_clean(value);
#endif // DISABLE_TRAVERSE_OLD_GEN
  }

  void h2_scavenge_contents_parallel(H2ToH1G1PushContentsClosure* cl,
                                  uint stripe_number,
                                  uint stripe_total, 
                                  bool scan_old
                                  );
#endif // TERA_CARDS

  void inline_write_ref_field_gc(void* field, oop new_val, bool promote_to_oldgen=false) {
    CardValue* byte = byte_for(field);
#ifdef TERA_CARDS
    if (EnableTeraHeap && Universe::is_field_in_h2(field)) {
      if (promote_to_oldgen) {
        // h2->h1 : if h1 is old => promote_to_oldgen=true
        //          if h1 is young => promote_to_oldgen=false
        Atomic::cmpxchg(byte, (CardValue)clean_card, G1CardTable::g1_old_card_val());
        Atomic::cmpxchg(byte, (CardValue)dirty_card, G1CardTable::g1_old_card_val());
        return;
      }
    }
    *byte = G1CardTable::g1_young_card_val();
#else
    *byte = G1CardTable::g1_young_card_val();
#endif // TERA_CARDS
  }

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
#endif // TERA_CARDS
    return (addr >= _byte_map) && (addr < _byte_map + _byte_map_size);
  }
#endif // ASSERT
};

#endif // SHARE_GC_G1_G1CARDTABLE_HPP
