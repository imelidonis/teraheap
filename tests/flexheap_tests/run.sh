#!/bin/bash
set -euo pipefail

#########################################################
#  CONFIGURATION
#########################################################
MEM_BUDGET="${MEM_BUDGET:-7G}"          # Default memory cap
CGROUP_NAME="${CGROUP_NAME:-memlim}"    # Default cgroup name
JAVA=../../jdk17/build/linux-x86_64-server-release/jdk/bin/java
JAVAC=../../jdk17/build/linux-x86_64-server-release/jdk/bin/javac
H2_DIR="/gds"
H2_SIZE_GB=100
H2_SIZE_BYTES=$(( H2_SIZE_GB * 1024 * 1024 * 1024 ))

# Java test classes
CLASSES=("GCAndIOPhases" "GCAndIOPhasesMixedBarrier" "HeapPercentage")
BENCH_INDEX=""
RUN_ALL=false
DURATION=60
OUTROOT="./results"
PLOT_SCRIPT="./plot_actions.py"  # path to the Python plotting script

#########################################################
#  FUNCTIONS
#########################################################

usage() {
  echo "Usage: $0 [-b 0|1|2|all] [-d duration_secs] [-o output_dir] [-k] [-h]"
  echo ""
  echo "Options:"
  echo "  -b INDEX   Benchmark to run:"
  echo "               0: GCAndIOPhases"
  echo "               1: GCAndIOPhasesMixedBarrier"
  echo "               2: HeapPercentage"
  echo "               all: Run all benchmarks sequentially"
  echo "  -d SECS    Duration (seconds) passed to Java tests"
  echo "  -o DIR     Root output directory for all tests"
  echo "  -k         Kill background monitor processes"
  echo "  -h         Show this help message"
  exit 0
}

kill_back_process() {
  echo "Killing background monitor processes..."
  pkill -f "bash ./cgroup_mem_usage.sh" || true
  echo "Cleanup complete."
  exit 0
}

setup_cgroup() {
  echo "Creating cgroup '${CGROUP_NAME}' (limit: ${MEM_BUDGET})"
  sudo cgcreate -a dimbas:sudo -t dimbas:sudo -g memory:${CGROUP_NAME}
  sudo cgset -r memory.max="${MEM_BUDGET}" ${CGROUP_NAME}
}

delete_cgroup() {
  echo "Deleting cgroup '${CGROUP_NAME}'..."
  sudo cgdelete memory:${CGROUP_NAME} > /dev/null 2>&1 || true
}

run_one_benchmark() {
  local CLASS=$1
  echo ""
  echo "--------------------------------------------------------"
  echo "Running benchmark: ${CLASS}"
  echo "--------------------------------------------------------"

  local SRC="java/${CLASS}.java"
  local OUTDIR="${OUTROOT}/${CLASS}"
  mkdir -p "${OUTDIR}"

  echo "Compiling ${SRC}..."
  "$JAVAC" -d java "$SRC"

  mkdir -p "${H2_DIR}"
  test -w "${H2_DIR}" || { echo "ERROR: ${H2_DIR} not writable"; exit 1; }

  setup_cgroup

  JAVA_OPTS="-cp java \
  -XX:-ClassUnloading \
  -XX:-UseCompressedOops \
  -XX:-UseCompressedClassPointers \
  -XX:-ResizePLAB \
  -XX:+EnableTeraHeap \
  -XX:+UseG1GC \
  -XX:ParallelGCThreads=16 \
  -XX:TeraStripeSize=32768 \
  -Xlog:gc*:file=${OUTDIR}/gc.log \
  -Xmx6g \
  -Xms6g \
  -XX:TeraDRAMLimit=6442450944 \
  -XX:+DynamicHeapResizing \
  -XX:IntervalHistoryAmount=5 \
  -XX:+TeraHeapStatistics \
  -XX:AllocateH2At=${H2_DIR}/ \
  -XX:H2FileSize=${H2_SIZE_BYTES} \
  -Xlogth:${OUTDIR}/teraHeap.txt"

  echo "JAVA_OPTS: $JAVA_OPTS"
  echo "Output dir: ${OUTDIR}"

  echo "Starting cgroup memory monitor..."
  ./cgroup_mem_usage.sh "${OUTDIR}/cgroup_mem_usage.csv" > "${OUTDIR}/monitor.log" 2>&1 &
  MONITOR_PID=$!

  echo "Running ${CLASS} (duration=${DURATION}) inside cgroup..."
  set +e
  cgexec -g memory:${CGROUP_NAME} --sticky \
    "$JAVA" $JAVA_OPTS "$CLASS" "${DURATION}" > "${OUTDIR}/tmp.out" 2> "${OUTDIR}/tmp.err"
  EXIT_CODE=$?
  set -e

  echo "Stopping cgroup memory monitor..."
  kill "${MONITOR_PID}" 2>/dev/null || true
  delete_cgroup

  if [[ $EXIT_CODE -eq 0 ]]; then
    echo "${CLASS} completed successfully."
  else
    echo "${CLASS} exited with code ${EXIT_CODE}."
  fi

  echo "Results stored in: ${OUTDIR}"

  #########################################################
  #  PLOTTING SECTION
  #########################################################
  echo ""
  echo "Generating plots for ${CLASS}..."
  local PLOT_DIR="${OUTDIR}/plots"
  mkdir -p "${PLOT_DIR}"

  if [[ -f "${OUTDIR}/tmp.err" && -f "${OUTDIR}/cgroup_mem_usage.csv" ]]; then
    if [[ -x "${PLOT_SCRIPT}" || -f "${PLOT_SCRIPT}" ]]; then
      python3 "${PLOT_SCRIPT}" "${OUTDIR}/tmp.err" "${OUTDIR}/cgroup_mem_usage.csv" --out-dir "${PLOT_DIR}" || \
        echo "Plot generation failed for ${CLASS}"
    else
      echo "Plot script not found: ${PLOT_SCRIPT}"
    fi
  else
    echo "Missing tmp.err or cgroup_mem_usage.csv — skipping plots."
  fi

  echo "Plots stored in: ${PLOT_DIR}"
}

#########################################################
#  ARGUMENT PARSING
#########################################################
while getopts ":b:d:o:kh" opt; do
  case $opt in
    b)
      if [[ "$OPTARG" == "all" ]]; then
        RUN_ALL=true
      else
        BENCH_INDEX=$OPTARG
        if ! [[ "$BENCH_INDEX" =~ ^[0-2]$ ]]; then
          echo "Invalid benchmark index: $BENCH_INDEX"
          usage
        fi
      fi
      ;;
    d)
      DURATION=$OPTARG
      ;;
    o)
      OUTROOT=$OPTARG
      ;;
    k)
      kill_back_process
      ;;
    h|*)
      usage
      ;;
  esac
done

#########################################################
#  EXECUTION
#########################################################
if $RUN_ALL; then
  echo "Running all benchmarks sequentially..."
  for CLASS in "${CLASSES[@]}"; do
    run_one_benchmark "$CLASS"
  done
else
  if [[ -z "$BENCH_INDEX" ]]; then
    echo "Error: No benchmark specified (-b 0|1|2|all)"
    usage
  fi
  CLASS="${CLASSES[$BENCH_INDEX]}"
  run_one_benchmark "$CLASS"
fi

echo ""
echo "All benchmarks finished successfully!"

