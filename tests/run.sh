#!/usr/bin/env bash

PARALLEL_GC_THREADS=16
STRIPE_SIZE=2048 # h2 region size / h2 card size (see sharedDefines.h in allocatpr and hotspot)
				  # REGION_SIZE (16M) / TERA_CARD_SIZE (8K)

# JAVA="$(pwd)/../jdk17/build/linux-x86_64-server-slowdebug/jdk/bin/java"
JAVA="$(pwd)/../jdk17/build/plus-linux/jdk/bin/java"
# JAVA="$(pwd)/../jdk17/build/sith5/jdk/bin/java"
# JAVA="java"


# FLAGS="-Xbootclasspath/a:/home1/public/mariach/benches/mine/H2/wbextra.jar -XX:+UnlockExperimentalVMOptions -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI -XX:G1HeapRegionSize=1m -XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP -XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000"
FLAGS="-XX:+EnableTeraHeap  -XX:TeraHeapSize=4294967296 -XX:TeraStripeSize=32768 -Xbootclasspath/a:/home1/public/mariach/benches/mine/H2/wbextra.jar -XX:+UnlockExperimentalVMOptions -XX:-UseCompressedClassPointers -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI -XX:G1HeapRegionSize=1m -XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP -XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000"


# EXEC=("Array" "Array_List" "Array_List_Int" "List_Large" "MultiList" \
# 	"Simple_Lambda" "Extend_Lambda" "Test_Reflection" "Test_Reference" \
# 	"HashMap" "Rehashing" "Clone" "Groupping" "MultiHashMap" \
# 	"Test_WeakHashMap" "ClassInstance")

EXEC=("HashMap")

# Export Enviroment Variables
export_env_vars() {
	PROJECT_DIR="$(pwd)/../.."

	export LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LD_LIBRARY_PATH
	export PATH=${PROJECT_DIR}/allocator/include/:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$C_INCLUDE_PATH
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$CPLUS_INCLUDE_PATH
}

# Run tests using only interpreter mode
# function interpreter_mode() {
# 	${JAVA} \
# 		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly -XX:+PrintInterpreter -XX:+PrintNMethods \
# 		-Djava.compiler=NONE \
# 		-XX:+ShowMessageBoxOnError \
# 		-XX:+UseG1GC \
# 		-XX:ParallelGCThreads=${PARALLEL_GC_THREADS} \
# 		-XX:-UseCompiler \
# 		-XX:+EnableTeraHeap \
# 		-XX:TeraHeapSize=${TERACACHE_SIZE} \
# 		-Xmx${MAX}g \
# 		-Xms${XMS}g \
# 		-XX:-UseCompressedOops \
# 		-XX:+TeraHeapStatistics \
# 		-XX:TeraStripeSize=${STRIPE_SIZE} \
# 		-Xlogth:llarge_teraCache.txt $1 > err 2>&1 > out
# }

PAR=1
function interpreter_mode() {
	${JAVA} ${FLAGS} \
		-Djava.compiler=NONE \
		-XX:+UseG1GC \
		-XX:ParallelGCThreads=${PAR} \
		-XX:InitialTenuringThreshold=5 \
		-XX:MaxTenuringThreshold=7 \
		-XX:-UseCompiler \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:ErrorFile=out/$1_hs_err_th${PAR}.log \
		-cp ./bin $1 > out/$1_out_th${PAR} 2>&1
		# 100000
}

# Run tests using only C1 compiler
function c1_mode() {
	 ${JAVA} \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly \
		-XX:+PrintInterpreter \
		-XX:+PrintNMethods -XX:+PrintCompilation \
		-XX:+ShowMessageBoxOnError -XX:+LogCompilation \
		-XX:TieredStopAtLevel=3\
		-XX:+UseG1GC \
		-XX:ParallelGCThreads=${PARALLEL_GC_THREADS} \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:+TeraHeapStatistics \
		-Xlogth:llarge_teraCache.txt $1 > err 2>&1 > out
}
	 
# Run tests using C2 compiler
function c2_mode() {
	 ${JAVA} \
		 -server \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly \
		-XX:+PrintNMethods -XX:+PrintCompilation \
		-XX:+ShowMessageBoxOnError -XX:+LogCompilation \
		-XX:+UseG1GC \
		-XX:ParallelGCThreads=${PARALLEL_GC_THREADS} \
		-XX:+EnableTeraCache \
		-XX:TeraCacheSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:TeraCacheThreshold=0 \
		-XX:-UseCompressedOops \
		-XX:+TeraCacheStatistics \
		-Xlogtc:llarge_teraCache.txt $1 > err 2>&1 > out
} 

# Run tests using all compilers
function run_tests() {
	${JAVA} \
		-server \
		-XX:+ShowMessageBoxOnError \
		-XX:+UseG1GC \
		-XX:ParallelGCThreads=${PARALLEL_GC_THREADS} \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:+TeraHeapStatistics \
		-XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xlogth:llarge_teraCache.txt $1 > err 2>&1 > out
}

# Run tests using gdb
function run_tests_debug() {
	gdb --args ${JAVA} \
		-server \
		-XX:+ShowMessageBoxOnError \
		-XX:+UseG1GC \
		-XX:ParallelGCThreads=${PARALLEL_GC_THREADS} \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:+TeraHeapStatistics \
		-XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xlogth:llarge_teraCache.txt $1
}

cd java || exit
make clean;

clear
echo "___________________________________"
echo 
echo "         Run JAVA Tests"
echo "___________________________________"
echo 

for exec_file in "${EXEC[@]}"
do
	if [ "${exec_file}" == "ClassInstance" ]
	then
		XMS=2
	else
		XMS=1
	fi

	MAX=100
	TERACACHE_SIZE=$(echo $(( (${MAX}-${XMS})*1024*1024*1024 )))
	case $1 in
		1)
			# export_env_vars
			interpreter_mode "$exec_file"
			;;
		2)
			# export_env_vars
			c1_mode $exec_file
			;;
		3)
			# export_env_vars
			c2_mode $exec_file
			;;
		4)
			# export_env_vars
			run_tests_debug $exec_file
			;;
		*)
			# export_env_vars
			run_tests "$exec_file"
			;;
	esac

	ans=$?

	echo -ne "${exec_file} "

	if [ $ans -eq 0 ]
	then    
		echo -e '\e[30G \e[32;1mPASS\e[0m';    
	else    
		echo -e '\e[30G \e[31;1mFAIL\e[0m';    
		break
	fi    
done

cd -
