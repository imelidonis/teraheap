#!/usr/bin/env bash

H2_REGION_SIZE=$((32 * 1024 * 1024))
H2_CARD_SEGMENT_SIZE=$((8 * 1024))
STRIPE_SIZE=$(( H2_REGION_SIZE / H2_CARD_SEGMENT_SIZE ))
H2_SIZE_IN_BYTES=$(echo "700 * 1024 * 1024 * 1024" | bc)
JAVA="../jdk17/build/linux-x86_64-server-release/jdk/bin/java"
H2_MOUNT_POINT="/mnt/spark/"

# These tests exit in both benchmarks suites (evac and full). Same
# name different implementation regarding of the GC trigger.
EXEC=(
  "Array" 
  "Array_List"
  "Array_List_Int"
  "List_Large"
  "MultiList"
  "Simple_Lambda"
  "Extend_Lambda"
  "Test_Reflection"
  "Test_Reference"
  "HashMap"
  "Rehashing"
  "Clone"
  "Groupping"
  "MultiHashMap"
  "Test_WeakHashMap"
  "ClassInstance"
)

# These tests exist only in evac benchmark suite
ONLY_EVAC_TESTS=(
  "Array_mine"
  "Array_List_String"
  "Test_CM_WeakRef"
  "Test_H2_CM_YoungInterrupt"
  "Test_H2_FreePath_CM_YoungStorm"
  "Test_H2_FreePath_CM_YoungStorm_Max_Frees"
  "Test_H2_FreePath_CM_YoungStorm_EdgeStress"
  "Test_H2_DependencyList_Race"
)

# These tests exist only in full benchmark suite
ONLY_FULLGC_TESTS=(
  "Array_List_String"
  "Humongous"
  "HumongousChain"
  "TriggerImplicitGCs"
)

INT_FLAGS=(
  -XX:+UnlockDiagnosticVMOptions
  -XX:+PrintAssembly
  -XX:+PrintInterpreter
  -XX:+PrintNMethods
  -Djava.compiler=NONE
  -XX:+ShowMessageBoxOnError
)

C1_FLAGS=(
  -XX:+UnlockDiagnosticVMOptions
  -XX:+PrintAssembly
  -XX:+PrintCompilation
  -XX:+PrintNMethods
  -XX:+LogCompilation
  -XX:+ShowMessageBoxOnError
  -XX:TieredStopAtLevel=3
)

C2_FLAGS=(
  -server
  -XX:+UnlockDiagnosticVMOptions
  -XX:+PrintNMethods
  -XX:+PrintCompilation
  -XX:+PrintOptoAssembly
  -XX:+PrintAssembly
  -XX:+LogCompilation
  -XX:+ShowMessageBoxOnError
)

DEFAULT_FLAGS=(
  -XX:-UseCompressedOops
  -XX:-UseCompressedClassPointers
  -XX:-ClassUnloading
  -XX:+UseG1GC
  -XX:+EnableTeraHeap 
  -XX:TeraStripeSize=${STRIPE_SIZE}
  -XX:+TeraHeapStatistics
  -XX:AllocateH2At=${H2_MOUNT_POINT}
  -XX:H2FileSize=${H2_SIZE_IN_BYTES}
  -XX:+ProtectH2RegionsOnFree
)
