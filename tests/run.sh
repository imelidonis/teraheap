#!/usr/bin/env bash

#-e: exit on command failure
#-u: error on unset variables 
#-o pipefail: pipelines fail if any command fails
set -euo pipefail

. ./conf.sh

PARALLEL_GC_THREADS=(8)
ITER=1
TESTD="g1_full_gc"
GC_NAME="G1 Full GC"
X_FLAGS=()
SELECTED_TESTS=()
STOP_ON_FAIL=0
H1_SZ=0
H1_H2_SZ=0
JVM_FLAGS=()

# Usage
usage() {
  local code="${1:-1}"
  cat >&2 <<'EOF'
Usage:
  ./run.sh -n <iterations> [-m <mode>] [-t <gc_threads_csv>] -d [<evac|full>] [-x "<jvm flags>"]... [-s <test1,test2,...>] [-b] [-h]

Options:
  -n <iterations>       Number of iterations to run.
  -m <mode>             Execution mode. One of: default, int, c1, c2, debug, msgbox.
                        (If omitted, the script should use its default mode.)
  -t <gc_threads_csv>   Comma-separated list of Parallel GC thread counts (e.g., 1,2,4,8).
  -d <evac|full>        Select benchmark suite.
                        (If omitted, the script should run full GC benchmarks.)
  -x "<jvm flags>"      Extra JVM flags. You can pass multiple flags in one -x string
                        (space-separated), and you can also repeat -x multiple times.
                        Example: -x "-XX:+UnlockDiagnosticVMOptions -XX:+PrintCompilation" -x "-Dfoo=bar"
  -s <tests_csv>        Comma-separated list of specific tests/classes to run.
                        Example: -s ClassInstance,HashMap
  -b                    Stop the script on the first test failure.
  -h                    Show this help and exit.

Examples:
  ./run.sh -n 3 -m c2 -t 1,2,4 -d evac
  ./run.sh -n 1 -d full -b -s ClassInstance,TriggerImplicitGCs
  ./run.sh -n 5 -m int -d evac -x "-XX:+UnlockDiagnosticVMOptions -XX:+PrintInterpreter"
EOF
  exit "$code"
}

parse_test_dir() {
  local val=$1

  case "$val" in
    "evac")
      TESTD="g1_evacuations"
      GC_NAME="G1 Evacuations"
      EXEC+=( "${ONLY_EVAC_TESTS[@]}" )
      ;;
    "full")
      TESTD="g1_full_gc"
      GC_NAME="G1 Full GC"
      EXEC+=( "${ONLY_FULLGC_TESTS[@]}" )
      ;;
    *)
      echo "Error: invalid test dir '$val'. Expected: 'evac' or 'full'." >&2
      usage 1
      ;;
  esac
}

set_heap_size() {
  local app=$1

  case "$app" in
    ClassInstance|TriggerImplicitGCs)
      H1_SZ=2
      ;;
    Array_List)
      H1_SZ=10
      ;;
    HashMap|Array_List_String)
      H1_SZ=3
      ;;
    Test_H2_CM_YoungInterrupt|Test_H2_HumongousBackRefsStress)
      H1_SZ=4
      ;;
    Test_H2_FreePath_CM_YoungStorm|Test_H2_FreePath_CM_YoungStorm_Max_Frees|Test_H2_FreePath_CM_YoungStorm_EdgeStress)
      H1_SZ=70
      ;;
    *)
      H1_SZ=1
      ;;
  esac
      
  H1_H2_SZ=100
  H2_SIZE=$(echo $(( (H1_H2_SZ-H1_SZ)*1024*1024*1024 )))
}

# Export Enviroment Variables
export_env_vars() {
	PROJECT_DIR="$(pwd)/.."

	export LIBRARY_PATH="${PROJECT_DIR}/allocator/lib/:${LIBRARY_PATH:-}"
	export LD_LIBRARY_PATH="${PROJECT_DIR}/allocator/lib/:${LD_LIBRARY_PATH:-}"
	export PATH="${PROJECT_DIR}/allocator/include/:${PATH:-}"
	export C_INCLUDE_PATH="${PROJECT_DIR}/allocator/include/:${C_INCLUDE_PATH:-}"
	export CPLUS_INCLUDE_PATH="${PROJECT_DIR}/allocator/include/:${CPLUS_INCLUDE_PATH:-}"
}

drop_page_cache() {
  if ! sudo -n true 2>/dev/null; then
    echo "Skipping drop_page_cache: no sudo access (or sudo requires a password)." >&2
    return 0
  fi

  sudo sync
  sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
}

set_cmd_jvm_flags() {
  EXEC_CMD=( "${JAVA}" )

  if [[ "$TESTD" == "g1_evacuations" ]]; then
    X_FLAGS+=(
    "-Xbootclasspath/a:./Whitebox/wb.jar"
    "-XX:+UnlockDiagnosticVMOptions"
    "-XX:+WhiteBoxAPI"
    "-XX:InitialTenuringThreshold=5"
    "-XX:MaxTenuringThreshold=7"
    "-XX:MaxGCPauseMillis=200"
    "-XX:G1MixedGCCountTarget=4"
  )
  fi

  case "${MODE:-}" in
    ""|default)
      MODE="Default"
      JVM_FLAGS=( -server "${DEFAULT_FLAGS[@]}" "${X_FLAGS[@]}" )
      ;;
    interpreter)
      MODE="Interpreter"
      JVM_FLAGS=( "${INT_FLAGS[@]}" "${DEFAULT_FLAGS[@]}" "${X_FLAGS[@]}" )
      ;;
    c1)
      MODE="C1"
      JVM_FLAGS=( "${C1_FLAGS[@]}" "${DEFAULT_FLAGS[@]}" "${X_FLAGS[@]}" )
      ;;
    c2)
      MODE="C2"
      JVM_FLAGS=( "${C2_FLAGS[@]}" "${DEFAULT_FLAGS[@]}" "${X_FLAGS[@]}" )
      ;;
    debug)
      MODE="Default with GDB"
      EXEC_CMD=("gdb" "--args" "${EXEC_CMD}" )
      JVM_FLAGS=( "${DEFAULT_FLAGS[@]}" "-XX:+ShowMessageBoxOnError" "${X_FLAGS[@]}" )
      ;;
    msgbox)
      MODE="Default with Message Box"
      JVM_FLAGS=( "${DEFAULT_FLAGS[@]}" "-XX:+ShowMessageBoxOnError" "${X_FLAGS[@]}" )
      ;;
  esac
}

run_benchmark() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/java

  RUNTIME_FLAGS=(
    "${JVM_FLAGS[@]}"
    "-XX:ParallelGCThreads=${num_gc_thread}"
    "-XX:TeraHeapSize=${H2_SIZE}"
    "-Xmx${H1_H2_SZ}g"
    "-Xms${H1_SZ}g"
    "-Xlog:gc*:file=./${dir}/out/${class_file}_gc.log"
    "-Xlogth:./${dir}/out/${class_file}_teraheap.txt"
    "-XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log")

    "${EXEC_CMD[@]}" "${RUNTIME_FLAGS[@]}" \
      -cp "./${dir}/bin" "${class_file}" \
      > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
}

print_msg() {
  local gcThread=$1
  local iteration=$2

  echo 
  echo "___________________________________"
  echo "         Run Tests"
  echo 
  echo "Iteration:  ${iteration}"
  echo "GC:         ${GC_NAME}"
  echo "Mode:       ${MODE}"
  echo "GC Threads: ${gcThread}"
  echo "___________________________________"
  echo 
}

# Check for the input arguments
while getopts "n:m:t:d:x:s:bh" opt
do
  case "${opt}" in
    n)
      ITER=${OPTARG}
      ;;
    m)
      MODE=${OPTARG}
      case "$MODE" in
        default|int|c1|c2|debug|msgbox) ;;
        *)
          echo "Error: invalid MODE '$MODE'. Expected one of: default, int, c1, c2, debug, msgbox" >&2
          usage 1
          ;;
      esac
      ;;
    t)
      IFS=',' read -r -a PARALLEL_GC_THREADS <<< "$OPTARG"
      ;;
    d)
      parse_test_dir "$OPTARG"
      ;;
    b)
      STOP_ON_FAIL=1
      ;;
    x)
      # Split OPTARG on spaces into multiple flags
      # The "<<<" is used to read a string in bash
      read -r -a tmp <<< "$OPTARG"
      X_FLAGS+=( "${tmp[@]}" )
      ;;
    s)
      # Comma-separated list of test names (e.g., -s TestA,TestB,TestC)
      IFS=',' read -r -a SELECTED_TESTS <<< "$OPTARG"
      ;;
    h)
      usage 0
      ;;
    *)
      usage 1
      ;;
  esac
done

mkdir -p ${TESTD}/java/out

set_cmd_jvm_flags

export_env_vars

# Check if selected tests are provided. If yes then run only these
# tests
if (( ${#SELECTED_TESTS[@]} > 0 )); then
  EXEC=( "${SELECTED_TESTS[@]}" )
fi

for itr in $(seq 1 $ITER)
do
  for gcThread in "${PARALLEL_GC_THREADS[@]}"
  do
    print_msg "$gcThread" "$itr"

    for exec_file in "${EXEC[@]}"
    do
      drop_page_cache

      set_heap_size ${exec_file}

      run_benchmark "$exec_file" "$gcThread"

      ans=$?

      echo -ne "${exec_file} "

      if [ $ans -eq 0 ]
      then    
        echo -e '\e[30G \e[32;1mPASS\e[0m';
      else    
        echo -e '\e[30G \e[31;1mFAIL\e[0m';
        cp ${TESTD}/java/out/${exec_file}_out ${TESTD}/java/out/fail_${exec_file}_out
        cp ${TESTD}/java/out/${exec_file}_err ${TESTD}/java/out/fail_${exec_file}_err

        if (( STOP_ON_FAIL  ))
        then
          break
        fi
      fi
    done
  done
done
