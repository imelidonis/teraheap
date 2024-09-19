#include "precompiled.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "gc/g1/g1FullGCTeraMarkTask.hpp"

G1FullGCTeraMarkTask::G1FullGCTeraMarkTask(G1FullCollector* collector) :
    G1FullGCTask("G1 Parallel Find Backward References", collector),
    _root_processor(G1CollectedHeap::heap(), collector->workers()),
    _terminator(collector->workers(), collector->array_queue_set()) {
  // TODO: (not sure if necessary) Need cleared claim bits for the roots processing
  ClassLoaderDataGraph::clear_claimed_marks();
}

void G1FullGCTeraMarkTask::work(uint worker_id) {
  Ticks start = Ticks::now();
  ResourceMark rm;

#ifdef TERA_CARDS
    H2ToH1G1PushContentsClosure cl;
    G1CollectedHeap::heap()->card_table()->h2_scavenge_contents_parallel(&cl, worker_id, _root_processor.n_workers(), true);
#endif // TERA_CARDS

  log_task("TeraMarking task", worker_id, start);
}

