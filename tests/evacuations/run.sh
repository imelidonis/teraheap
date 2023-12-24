#!/usr/bin/env bash

PARALLEL_GC_THREADS=16
STRIPE_SIZE=32768

JAVA="../../jdk17/build/base-linux/jdk/bin/java"
 
FLAGS="-Xbootclasspath/a:../../tests/evacuations/Whitebox/wb.jar -XX:+UnlockExperimentalVMOptions -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI \
	-XX:+UseG1GC -XX:G1HeapRegionSize=1m \
	-XX:-UseCompressedClassPointers -XX:-UseCompressedOops \
	-XX:InitialTenuringThreshold=5 -XX:MaxTenuringThreshold=7 \
	-XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000 \
	-XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP \
    -XX:ParallelGCThreads=${PARALLEL_GC_THREADS} -XX:ConcGCThreads=4 \
	-Xlog:gc*=debug:./java/out/gc_log.log:level"

# EXEC=("MultiHashMap" "HashMap" "Test_WeakHashMap" "ClassInstance" \
# 	"Array" "Array_List" "Array_List_Int" "List_Large" "MultiList" \
# 	"Simple_Lambda" "Extend_Lambda" "Test_Reflection" "Test_Reference" \
# 	"Rehashing" "Clone" "Groupping")


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
function interpreter_mode() {
	${JAVA} ${FLAGS} -XX:+EnableTeraHeap -XX:TeraHeapSize=${TERACACHE_SIZE} -XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xmx${MAX}g -Xms${XMS}g \
		-Djava.compiler=NONE -XX:-UseCompiler \
		-XX:+PrintAssembly -XX:+PrintInterpreter -XX:+PrintNMethods -XX:+ShowMessageBoxOnError \
		-XX:ErrorFile=./java/out/$1_hs_err.log -cp ./java/bin $1  > ./java/out/$1_out 2>&1
}

# Run tests using only C1 compiler
function c1_mode() {
	 ${JAVA} ${FLAGS} -XX:+EnableTeraHeap -XX:TeraHeapSize=${TERACACHE_SIZE} -XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xmx${MAX}g -Xms${XMS}g \
		-XX:TieredStopAtLevel=3 \
		-XX:+PrintAssembly -XX:+PrintCompilation -XX:+PrintNMethods -XX:+LogCompilation -XX:+ShowMessageBoxOnError \
		-XX:ErrorFile=./java/out/$1_hs_err.log -cp ./java/bin $1  > ./java/out/$1_out 2>&1 
}
	 
# Run tests using C2 compiler
function c2_mode() {

	 ${JAVA} ${FLAGS} -XX:+EnableTeraHeap -XX:TeraHeapSize=${TERACACHE_SIZE} -XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xmx${MAX}g -Xms${XMS}g \
		-XX:+UnlockDiagnosticVMOptions \
		-server \
		-XX:+PrintOptoAssembly -XX:+PrintAssembly -XX:+PrintCompilation -XX:+PrintNMethods -XX:+LogCompilation -XX:+ShowMessageBoxOnError \
		-XX:ErrorFile=./java/out/$1_hs_err.log -cp ./java/bin $1  > ./java/out/$1_out 2>&1 
} 


# Run tests using all compilers
function run_tests() {
	 ${JAVA} ${FLAGS} -XX:+EnableTeraHeap -XX:TeraHeapSize=${TERACACHE_SIZE} -XX:TeraStripeSize=${STRIPE_SIZE} \
		-Xmx${MAX}g -Xms${XMS}g \
		-XX:+UnlockDiagnosticVMOptions \
		-server \
		-XX:ErrorFile=./java/out/$1_hs_err.log -cp ./java/bin $1  > ./java/out/$1_out 2>&1 

}

# Run tests using gdb
function run_tests_debug() {
	gdb --args ${JAVA} ${FLAGS} \
		-server \
		-XX:+ShowMessageBoxOnError \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:TeraStripeSize=${STRIPE_SIZE} \
		-XX:ErrorFile=./java/out/$1_hs_err.log -cp ./java/bin $1
}

mkdir java/out

clear
echo "___________________________________"
echo 
echo "         Run JAVA Tests"
echo "___________________________________"
echo 

# if the file does not exist then create it
if [ ! -f /tmp/nvme/mariach/H2.txt ]
then
  fallocate -l 700G /tmp/nvme/mariach/H2.txt
fi


for exec_file in "${EXEC[@]}"
do
	if [ "${exec_file}" == "ClassInstance" ] || [ "${exec_file}" == "Array_List" ] || [ "${exec_file}" == "Groupping" ]
	then
		XMS=2
	else
		XMS=1
	fi

	MAX=100
	TERACACHE_SIZE=$(echo $(( (${MAX}-${XMS})*1024*1024*1024 )))
	case $1 in
		1)
			export_env_vars
			interpreter_mode "$exec_file"
			;;
		2)
			export_env_vars
			c1_mode $exec_file
			;;
		3)
			export_env_vars
			c2_mode $exec_file
			;;
		4)
			export_env_vars
			run_tests_debug $exec_file
			;;
		*)
			export_env_vars
			run_tests "$exec_file"
			;;
	esac

	ans=$?

	echo -ne "${exec_file} "

	if [ $ans -eq 0 ]
	then
		echo -e PASS    
		echo -e '\e[30G \e[32;1mPASS\e[0m';    
	else    
		echo -e '\e[30G \e[31;1mFAIL\e[0m';    
		break
	fi    
done

rm -f /tmp/nvme/mariach/H2.txt