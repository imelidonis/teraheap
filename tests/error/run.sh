JAVAC="$(pwd)/../../jdk17/build/plus-linux/jdk/bin/javac -Xlint:deprecation -sourcepath /home1/public/mariach/benches/mine/H2/wbextra.jar"
JAVA="$(pwd)/../../jdk17/build/plus-linux/jdk/bin/java"

FLAGS="-XX:+EnableTeraHeap  -XX:TeraHeapSize=4294967296 -XX:TeraStripeSize=32768 \
    -Xbootclasspath/a:/home1/public/mariach/benches/mine/H2/wbextra.jar -XX:+UnlockExperimentalVMOptions -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI\
    -XX:-UseCompressedClassPointers -XX:-UseCompressedOops \
    -XX:G1HeapRegionSize=1m -XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP \
    -XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000 \
    -XX:-AlwaysTenure"


EXEC_COMP=("HashMap")
EXEC=("HashMap")



 
MAX=100
XMS=1

# rm -f *_out *_compile *.log

# ${JAVAC} GC.java

# for exec_file in "${EXEC_COMP[@]}"
# do
#     ${JAVAC} ${exec_file}.java > out/${exec_file}_compile 2>&1
# done

PAR=16
for exec_file in "${EXEC_COMP[@]}"
do
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
   ${exec_file} 2000000 > out/${exec_file}_out_th${PAR} 2>&1
done
