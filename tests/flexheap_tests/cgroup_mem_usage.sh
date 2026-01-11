#!/bin/bash

# Usage: ./cgroup_mem_usage.sh output.csv
# Monitors memory stats for any active Java benchmark in your setup.

set -euo pipefail

OUTPUT=$1
CGROUP_MEM_STAT_FILE="/sys/fs/cgroup/memlim/memory.stat"

# Auto-detect benchmarks dynamically
# We match any Java process running one of your benchmark class names
BENCHMARK_PATTERNS="GCAndIOPhases|GCAndIOPhasesMixedBarrier|HeapPercentage"

echo "Waiting for benchmark process to start (${BENCHMARK_PATTERNS})..."

# Wait until at least one of the benchmarks is running
while [ "$(jps | grep -E "${BENCHMARK_PATTERNS}" | wc -l)" -lt 1 ]; do
  sleep 1
done

echo "timestamp,anon_bytes,file_bytes,kernel_bytes,pgscan_total,pgsteal_total,pgmajfault_total" >> "${OUTPUT}"

# Monitor until the benchmark finishes
while [ "$(jps | grep -E "${BENCHMARK_PATTERNS}" | wc -l)" -ge 1 ]; do
  TIMESTAMP=$(date +%s)

  # Read cgroup stats safely (if available)
  if [ -f "${CGROUP_MEM_STAT_FILE}" ]; then
    ANON_MEM=$(grep -E "^anon " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')
    FILE_MEM=$(grep -E "^file " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')
    KERNEL_MEM=$(grep -E "^kernel " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')
    PGSCAN_TOTAL=$(grep -E "^pgscan " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')
    PGSTEAL_TOTAL=$(grep -E "^pgsteal " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')
    PGMAJFAULT_TOTAL=$(grep -E "^pgmajfault " "${CGROUP_MEM_STAT_FILE}" | awk '{print $2}')

    echo "${TIMESTAMP},${ANON_MEM},${FILE_MEM},${KERNEL_MEM},${PGSCAN_TOTAL},${PGSTEAL_TOTAL},${PGMAJFAULT_TOTAL}" >> "${OUTPUT}"
  else
    echo "Missing ${CGROUP_MEM_STAT_FILE}" >&2
    exit 1
  fi

  sleep 1
done


