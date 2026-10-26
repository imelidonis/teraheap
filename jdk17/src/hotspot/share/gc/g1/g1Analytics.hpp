/*
 * Copyright (c) 2016, 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_GC_G1_G1ANALYTICS_HPP
#define SHARE_GC_G1_G1ANALYTICS_HPP

#include "memory/allocation.hpp"
#include "utilities/globalDefinitions.hpp"

class TruncatedSeq;
class G1Predictions;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
// The last `capacity` samples of a TeraHeap quantity, in a ring buffer,
// with their median and a percentile updated on each add. With fewer than
// 8 samples both are the maximum sample (pessimistic).
class G1H2Samples : public CHeapObj<mtGC> {
  double* _samples;
  uint _capacity;
  uint _num;
  uint _next;
  double _percentile;
  double _median;

public:
  G1H2Samples(uint capacity);
  void add(double v, uintx percentile);
  uint num() const { return _num; }
  double percentile() const { return _percentile; }
  double median() const { return _median; }
};
#endif

class G1Analytics: public CHeapObj<mtGC> {
  const static int TruncatedSeqLength = 10;
  const static int NumPrevPausesForHeuristics = 10;
  const G1Predictions* _predictor;

  // These exclude marking times.
  TruncatedSeq* _recent_gc_times_ms;

  TruncatedSeq* _concurrent_mark_remark_times_ms;
  TruncatedSeq* _concurrent_mark_cleanup_times_ms;

  TruncatedSeq* _alloc_rate_ms_seq;
  double        _prev_collection_pause_end_ms;

  TruncatedSeq* _rs_length_diff_seq;
  TruncatedSeq* _concurrent_refine_rate_ms_seq;
  TruncatedSeq* _dirtied_cards_rate_ms_seq;
  // The ratio between the number of merged cards and actually scanned cards, for
  // young-only and mixed gcs.
  TruncatedSeq* _young_card_merge_to_scan_ratio_seq;
  TruncatedSeq* _mixed_card_merge_to_scan_ratio_seq;

  // The cost to scan a card during young-only and mixed gcs in ms.
  TruncatedSeq* _young_cost_per_card_scan_ms_seq;
  TruncatedSeq* _mixed_cost_per_card_scan_ms_seq;

  // The cost to merge a card during young-only and mixed gcs in ms.
  TruncatedSeq* _young_cost_per_card_merge_ms_seq;
  TruncatedSeq* _mixed_cost_per_card_merge_ms_seq;

  // The cost to copy a byte in ms.
  TruncatedSeq* _copy_cost_per_byte_ms_seq;
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  TruncatedSeq* _h2_copy_cost_per_byte_ms_seq;
#endif
  TruncatedSeq* _constant_other_time_ms_seq;
  TruncatedSeq* _young_other_cost_per_region_ms_seq;
  TruncatedSeq* _non_young_other_cost_per_region_ms_seq;

  TruncatedSeq* _pending_cards_seq;
  TruncatedSeq* _rs_length_seq;

  TruncatedSeq* _cost_per_byte_ms_during_cm_seq;
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  TruncatedSeq* _h2_cost_per_byte_ms_during_cm_seq;

  // The H2 copy cost per byte (ms) of the last TeraH2CostSamples mixed GCs
  // that moved enough bytes to H2 (TeraH2CostPercentile and median)
  G1H2Samples* _h2_cost_samples;

  // Share of the survivor bytes in the collection set that mixed GCs move
  // to H2: objects labeled for H2 after marking, which the marking did not
  // count in any region's h2_marked_bytes (median)
  G1H2Samples* _h2_survivor_share_samples;

  // Share of the eden bytes in the collection set that mixed GCs move to
  // H2: objects that are already labeled for H2 in eden (median)
  G1H2Samples* _h2_eden_share_samples;
#endif

  // Statistics kept per GC stoppage, pause or full.
  TruncatedSeq* _recent_prev_end_times_for_all_gcs_sec;

  // Cached values for long and short term pause time ratios. See
  // compute_pause_time_ratios() for how they are computed.
  double _long_term_pause_time_ratio;
  double _short_term_pause_time_ratio;

  // Returns whether the sequence have enough samples to get a "good" prediction.
  // The constant used is random but "small".
  bool enough_samples_available(TruncatedSeq const* seq) const;

  double predict_in_unit_interval(TruncatedSeq const* seq) const;
  size_t predict_size(TruncatedSeq const* seq) const;
  double predict_zero_bounded(TruncatedSeq const* seq) const;

  double oldest_known_gc_end_time_sec() const;
  double most_recent_gc_end_time_sec() const;

public:
  G1Analytics(const G1Predictions* predictor);

  double prev_collection_pause_end_ms() const {
    return _prev_collection_pause_end_ms;
  }

  double long_term_pause_time_ratio() const {
    return _long_term_pause_time_ratio;
  }

  double short_term_pause_time_ratio() const {
    return _short_term_pause_time_ratio;
  }

  uint number_of_recorded_pause_times() const {
    return NumPrevPausesForHeuristics;
  }

  void append_prev_collection_pause_end_ms(double ms) {
    _prev_collection_pause_end_ms += ms;
  }

  void set_prev_collection_pause_end_ms(double ms) {
    _prev_collection_pause_end_ms = ms;
  }

  void report_concurrent_mark_remark_times_ms(double ms);
  void report_concurrent_mark_cleanup_times_ms(double ms);
  void report_alloc_rate_ms(double alloc_rate);
  void report_concurrent_refine_rate_ms(double cards_per_ms);
  void report_dirtied_cards_rate_ms(double cards_per_ms);
  void report_cost_per_card_scan_ms(double cost_per_remset_card_ms, bool for_young_gc);
  void report_cost_per_card_merge_ms(double cost_per_card_ms, bool for_young_gc);
  void report_card_merge_to_scan_ratio(double cards_per_entry_ratio, bool for_young_gc);
  void report_rs_length_diff(double rs_length_diff);
  void report_cost_per_byte_ms(double cost_per_byte_ms, bool mark_or_rebuild_in_progress);
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  void report_h2_cost_per_byte_ms(double h2_cost_per_byte_ms, bool mark_or_rebuild_in_progress);

  // Add the H2 copy cost per byte of a mixed GC to the recent samples
  void report_h2_cost_sample(double h2_cost_per_byte_ms);

  uint h2_cost_num_samples() const { return _h2_cost_samples->num(); }
  // The TeraH2CostPercentile and the median of the recent H2 costs per
  // byte (ms). With fewer than 8 samples both are the maximum sample.
  double h2_cost_percentile_per_byte_ms() const { return _h2_cost_samples->percentile(); }
  double h2_cost_median_per_byte_ms() const { return _h2_cost_samples->median(); }

  // Add the share of the survivor bytes that a mixed GC moved to H2
  void report_h2_survivor_share(double share);
  // The median recent share (0 before the first sample)
  double h2_survivor_share() const { return _h2_survivor_share_samples->median(); }

  // Add the share of the eden bytes that a mixed GC moved to H2
  void report_h2_eden_share(double share);
  // The median recent share (0 before the first sample)
  double h2_eden_share() const { return _h2_eden_share_samples->median(); }
#endif
  void report_young_other_cost_per_region_ms(double other_cost_per_region_ms);
  void report_non_young_other_cost_per_region_ms(double other_cost_per_region_ms);
  void report_constant_other_time_ms(double constant_other_time_ms);
  void report_pending_cards(double pending_cards);
  void report_rs_length(double rs_length);

  double predict_alloc_rate_ms() const;
  int num_alloc_rate_ms() const;

  double predict_concurrent_refine_rate_ms() const;
  double predict_dirtied_cards_rate_ms() const;
  double predict_young_card_merge_to_scan_ratio() const;

  double predict_mixed_card_merge_to_scan_ratio() const;

  size_t predict_scan_card_num(size_t rs_length, bool for_young_gc) const;

  double predict_card_merge_time_ms(size_t card_num, bool for_young_gc) const;
  double predict_card_scan_time_ms(size_t card_num, bool for_young_gc) const;

  double predict_object_copy_time_ms_during_cm(size_t bytes_to_copy) const;
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  double predict_h2_object_copy_time_ms_during_cm(size_t h2_bytes_to_copy) const;
#endif

  double predict_object_copy_time_ms(size_t bytes_to_copy, bool during_concurrent_mark) const;
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  double predict_h2_object_copy_time_ms(size_t h2_bytes_to_copy, bool during_concurrent_mark) const;

  // Pessimistic H2 copy time: with the TeraH2CostPercentile of the recent
  // H2 costs, and never lower than predict_h2_object_copy_time_ms()
  double predict_h2_object_copy_time_ms_conservative(size_t h2_bytes_to_copy, bool during_concurrent_mark) const;
#endif

  double predict_constant_other_time_ms() const;

  double predict_young_other_time_ms(size_t young_num) const;

  double predict_non_young_other_time_ms(size_t non_young_num) const;

  double predict_remark_time_ms() const;

  double predict_cleanup_time_ms() const;

  size_t predict_rs_length() const;
  size_t predict_pending_cards() const;

  // Add a new GC of the given duration and end time to the record.
  void update_recent_gc_times(double end_time_sec, double elapsed_ms);
  void compute_pause_time_ratios(double end_time_sec, double pause_time_ms);
};

#endif // SHARE_GC_G1_G1ANALYTICS_HPP
