#ifndef SHARE_GC_G1_G1FULLGCTERAMARKTASK_HPP
#define SHARE_GC_G1_G1FULLGCTERAMARKTASK_HPP

#include "gc/g1/g1FullCollector.hpp"
#include "gc/g1/g1FullGCTask.hpp"
#include "gc/g1/g1RootProcessor.hpp"
#include "gc/shared/taskTerminator.hpp"

class G1FullGCTeraMarkTask : public G1FullGCTask {
  G1RootProcessor          _root_processor;
  TaskTerminator           _terminator;

public:
  G1FullGCTeraMarkTask(G1FullCollector* collector);
  void work(uint worker_id);
};

#endif // SHARE_GC_G1_G1FULLGCTERAMARKTASK_HPP

