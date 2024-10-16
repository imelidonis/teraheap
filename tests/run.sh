#!/usr/bin/env bash

# Use this script to run tests 
# By default: it runs the java tests of G1 Full GC

PARALLEL_GC_THREADS=2
# REGION_SIZE / 2^(TERA_CARD_SIZE) -> found in sharedDefines.hpp
STRIPE_SIZE=32768

# JAVA="../jdk17/build/linux-x86_64-server-slowdebug/jdk/bin/java"
JAVA="../jdk17/build/linux-x86_64-server-release/jdk/bin/java"

# Flag to set which GC the jvm will use
GC="UseG1GC"
 
# Java files should be under: ${TESTD}/${EXEC_DIR_NAME}
TESTD="g1_full_gc"
EXEC_DIR_NAME="java"

FLAGS="-XX:+EnableTeraHeap \
  -XX:TeraStripeSize=${STRIPE_SIZE} \
  -XX:-ClassUnloading \
  -XX:-UseCompressedOops \
  -XX:-UseCompressedClassPointers"

# Extra flags that may be useful in some cases
X_FLAGS=""

# NOTE: you can add exclusive tests after check_args
# If you want to run a single test, you should overwrite EXEC
# variable after last set.
EXEC_JAVA=("Array" "Array_List" "Array_List_Int" "List_Large" "MultiList" \
	"Simple_Lambda" "Extend_Lambda" "Test_Reflection" "Test_Reference" \
	"HashMap" "Rehashing" "Clone" "Groupping" "MultiHashMap" \
	"Test_WeakHashMap" "ClassInstance")

EXEC_PHASES=("Phase1_MarkOneObject" "Phase1_MarkSubObject" \
  "Phase2_GiveAddressesFromH2" "Phase3_UpdateReferences" \
  "SimpleOneObj" "SimpleOneBackward")

# Export Enviroment Variables
export_env_vars() {
	PROJECT_DIR="$(pwd)/.."

	export LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LD_LIBRARY_PATH
	export PATH=${PROJECT_DIR}/allocator/include/:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$C_INCLUDE_PATH
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$CPLUS_INCLUDE_PATH
}

clear_env() {
  echo "Clearing env..."
  local proj=$(pwd)
  
  echo "Clear H2 file..."
  cd /mnt/fmap
  rm -f h2-100.heap
  fallocate -l 100G h2-100.heap

  echo "Droping Caches..."
  sudo sync
  sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

  echo "Done!"
  cd "$proj"
}

# Run tests using only interpreter mode
function interpreter_mode() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/${EXEC_DIR_NAME}

	${JAVA} ${FLAGS} ${X_FLAGS} -server \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly -XX:+PrintInterpreter -XX:+PrintNMethods \
		-Djava.compiler=NONE \
		-XX:+ShowMessageBoxOnError \
		-XX:+$GC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:+TeraHeapStatistics \
		-Xlogth:llarge_teraCache.txt \
		-XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log \
    -cp ./${dir}/bin ${class_file} \
    > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
}

# Run tests using only C1 compiler
function c1_mode() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/${EXEC_DIR_NAME}

  ${JAVA} ${FLAGS} ${X_FLAGS} \
    -XX:+UnlockDiagnosticVMOptions \
    -XX:+PrintAssembly -XX:+PrintCompilation -XX:+PrintNMethods -XX:+LogCompilation \
    -XX:+ShowMessageBoxOnError \
    -XX:TieredStopAtLevel=3 \
    -XX:+$GC \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:+TeraHeapStatistics \
    -Xlogth:llarge_teraCache.txt \
    -XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log \
    -cp ./${dir}/bin ${class_file} \
    > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
}
	 
# Run tests using C2 compiler
function c2_mode() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/${EXEC_DIR_NAME}

  ${JAVA} ${FLAGS} ${X_FLAGS} \
    -server \
    -XX:+UnlockDiagnosticVMOptions \
    -XX:+PrintNMethods -XX:+PrintCompilation -XX:+PrintOptoAssembly -XX:+PrintAssembly \
    -XX:+LogCompilation \
    -XX:+ShowMessageBoxOnError \
    -XX:+$GC \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:+TeraHeapStatistics \
    -Xlogtc:llarge_teraCache.txt \
    -XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log \
    -cp ./${dir}/bin ${class_file} \
    > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
} 

# Run tests using all compilers
function run_tests() {
  local class_file=$1
  local num_gc_thread=$2
  local dir="${TESTD}/${EXEC_DIR_NAME}"

  ${JAVA} ${FLAGS} ${X_FLAGS} \
    -server \
    -XX:+"$GC" \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:+TeraHeapStatistics \
    -Xlogth:llarge_teraCache.txt \
    -XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log \
    -cp ./${dir}/bin ${class_file} \
    > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
}

# Run tests using gdb
function run_tests_debug() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/${EXEC_DIR_NAME}

  gdb --args ${JAVA} ${FLAGS} ${X_FLAGS} \
    -server \
    -XX:+ShowMessageBoxOnError \
    -XX:+$GC \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:+TeraHeapStatistics \
    -Xlogth:llarge_teraCache.txt \
    -XX:ErrorFile=./${dir}/out/${class_file}_hs_err.log \
    -cp ./${dir}/bin ${class_file}
}

# Run tests using all compilers
function run_tests_msg_box() {
  local class_file=$1
  local num_gc_thread=$2
  local dir=${TESTD}/${EXEC_DIR_NAME}

	${JAVA} ${FLAGS} ${X_FLAGS} \
		-server \
		-XX:+ShowMessageBoxOnError \
		-XX:+$GC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
    -XX:+TeraHeapStatistics \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-Xlogth:llarge_teraCache.txt -cp ./${dir}/bin ${class_file} \
    > ./${dir}/out/${class_file}_err 2>&1 > ./${dir}/out/${class_file}_out
}

# Usage
usage() {
  echo
  echo "Usage:"
  echo -n "      $0 [option ...] [-h]"
  echo
  echo "Options:"
  echo "      -m  Mode (0: Default, 1: Interpreter, 2: C1, 3: C2, 4: gdb, 5: ShowMessageBoxOnError)"
  echo "      -t  Number of GC threads (2, 4, 8, 16, 32)"
  echo "      -d  Directory of tests"
  echo "      -g  GC tests (g1evac: minor/major gc, g1full: full gc, ps: parallel scavenge gc)"
  echo "      -h  Show usage"
  echo

  exit 1
}

check_args() {
  # Check if required options are present
  if [[ -z "$MODE" || "${#PARALLEL_GC_THREADS[@]}" -eq 0 ]]; then
    echo "Usage: $0 -m <mode> -t <#gcThreads,#gcThreads,#gcThreads> -d <dir> -g <tests> [-h]"
    exit 1
  fi

  if [[ -z "$EXEC_DIR_NAME" ]]; then
    echo "Usage: $0 -m <mode> -t <#gcThreads,#gcThreads,#gcThreads> -d <dir> -g <tests> [-h]"
    exit 1
  fi

  if [[ -z "$TESTD" ]]; then
    echo "Usage: $0 -m <mode> -t <#gcThreads,#gcThreads,#gcThreads> -d <dir> -g <tests> [-h]"
    exit 1
  fi
}

print_msg() {
  local gcThread=$1
  local mode_value
  local gc_name

  case "${MODE}" in
    0)
      mode_value="Default"
      ;;
    1)
      mode_value="Interpreter"
      ;;
    2)
      mode_value="C1"
      ;;
    3)
      mode_value="C2"
      ;;
    5)
      mode_value="Message Box"
  esac

  case "$TESTD" in
    g1_evacuations)
      gc_name="G1 Evacuations"
      ;;
    g1_full_gc)
      gc_name="G1 Full GC"
      ;;
    parallel_gc)
      gc_name="Parallel Scavenge"
      ;;
  esac

  echo 
  echo "___________________________________"
  echo "         Run ${EXEC_DIR_NAME} Tests"
  echo 
  echo "GC:         ${gc_name}"
  echo "Mode:       ${mode_value}"
  echo "GC Threads: ${gcThread}"
  echo "___________________________________"
  echo 
}

parse_test_dir() {
  local val=$1
  local gc_opt=("UseG1GC" "UsePSGC") 

  case "$val" in
    "g1evac")
      TESTD="g1_evacuations"
      GC="${gc_opt[0]}"
      ;;
    "g1full")
      TESTD="g1_full_gc"
      GC="${gc_opt[0]}"
      ;;
    "ps")
      TESTD="parallel_gc"
      GC="${gc_opt[1]}"
      ;;
    *)
      TESTD=""
      ;;
  esac
}

# Check for the input arguments
while getopts "m:t:d:g:h" opt
do
  case "${opt}" in
    m)
      MODE=${OPTARG}
      ;;
    t)
      IFS=',' read -r -a PARALLEL_GC_THREADS <<< "$OPTARG"
      ;;
    d)
      EXEC_DIR_NAME="${OPTARG}"
      ;;
    g)
      parse_test_dir "$OPTARG"
      ;;
    h)
      usage
      ;;
    *)
      usage
      ;;
  esac
done

check_args

mkdir -p ${TESTD}/${EXEC_DIR_NAME}/out
# cd ${EXEC_DIR_NAME} || exit

# Additional Test files not used in every case
if [ "${TESTD}" == "g1_evacuations" ]
then
  EXEC_JAVA+=("Array_mine" "Array_List_String")
elif [ "${TESTD}" == "g1_full_gc" ]
then
  EXEC_JAVA+=("Array_List_String")
fi

# Setup exec files
if [ "${EXEC_DIR_NAME}" == "java" ]
then
  EXEC=(${EXEC_JAVA[@]})
else
  EXEC=(${EXEC_PHASES[@]})
fi

# NOTE: you can overwrite EXEC here to run specific tests
# Attention: if you overwrite EXEC make sure you have the
# correct flags.
# EXEC=("Array")

# Add extra flags
if [ "$EXEC_DIR_NAME" == "phases" ]
then
  X_FLAGS="-XX:G1HeapWastePercent=0 $X_FLAGS"
elif [ "$TESTD" == "g1_evacuations" ]
then
  X_FLAGS="-Xbootclasspath/a:./g1_evacuations/Whitebox/wb.jar \
    -XX:+UnlockDiagnosticVMOptions \
    -XX:+WhiteBoxAPI \
    -XX:InitialTenuringThreshold=5 -XX:MaxTenuringThreshold=7 \
    -XX:MaxGCPauseMillis=30000 \
    -XX:G1MixedGCCountTarget=4 $X_FLAGS"
fi

# Run tests
for gcThread in "${PARALLEL_GC_THREADS[@]}"
do
  print_msg "$gcThread"

  for exec_file in "${EXEC[@]}"
  do
    if [ "${exec_file}" == "ClassInstance" ]
    then
      XMS=2
    elif [ "${exec_file}" == "Array_List" ]
    then
      XMS=10
    elif [[ "${exec_file}" == "HashMap" || "${exec_file}" == "Array_List_String" ]]
    then
      XMS=3
    else
      XMS=1
    fi

    MAX=100
    TERACACHE_SIZE=$(echo $(( (MAX-XMS)*1024*1024*1024 )))
    case ${MODE} in
      0)
        # clear_env
        export_env_vars
        run_tests "$exec_file" "$gcThread"
        ;;
      1)
        # clear_env
        export_env_vars
        interpreter_mode "$exec_file" "$gcThread"
        ;;
      2)
        # clear_env
        export_env_vars
        c1_mode "$exec_file" "$gcThread"
        ;;
      3)
        # clear_env
        export_env_vars
        c2_mode "$exec_file" "$gcThread"
        ;;
      4)
        # clear_env
        export_env_vars
        run_tests_debug "$exec_file" "$gcThread"
        ;;
      5)
        # clear_env
        export_env_vars
        run_tests_msg_box "$exec_file" "$gcThread"
        ;;
    esac

    ans=$?

    echo -ne "${exec_file} "

    if [ $ans -eq 0 ]
    then    
      echo -e '\e[30G \e[32;1mPASS\e[0m';
    else    
      echo -e '\e[30G \e[31;1mFAIL\e[0m';
      cp ${TESTD}/${EXEC_DIR_NAME}/out/${exec_file}_out ${TESTD}/${EXEC_DIR_NAME}/out/fail_${exec_file}_out
      cp ${TESTD}/${EXEC_DIR_NAME}/out/${exec_file}_err ${TESTD}/${EXEC_DIR_NAME}/out/fail_${exec_file}_err
      # NOTE: uncomment if you want to break when a test fails.
      # break
    fi
  done

done

cd - > /dev/null || exit
