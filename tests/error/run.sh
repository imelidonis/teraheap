JAVAC="$(pwd)/../../jdk17/build/base-linux/jdk/bin/javac -Xlint:deprecation -sourcepath /home1/public/mariach/benches/mine/H2/wbextra.jar"
JAVA="$(pwd)/../../jdk17/build/base-linux/jdk/bin/java"

FLAGS="-XX:+EnableTeraHeap  -XX:TeraHeapSize=4294967296 -XX:TeraStripeSize=2048 \
    -Xbootclasspath/a:/home1/public/mariach/benches/mine/H2/wbextra.jar -XX:+UnlockExperimentalVMOptions -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI\
    -XX:-UseCompressedClassPointers -XX:-UseCompressedOops \
    -XX:G1HeapRegionSize=1m -XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP \
    -XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000 \
    -XX:-AlwaysTenure"

FLAGS_NO_TERA="-Xbootclasspath/a:/home1/public/mariach/benches/mine/H2/wbextra.jar -XX:+UnlockExperimentalVMOptions -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI\
    -XX:-UseCompressedClassPointers -XX:-UseCompressedOops \
    -XX:G1HeapRegionSize=1m -XX:InitiatingHeapOccupancyPercent=100 -XX:-G1UseAdaptiveIHOP \
    -XX:G1MixedGCCountTarget=4 -XX:G1HeapWastePercent=0 -XX:G1MixedGCLiveThresholdPercent=100 -XX:MaxGCPauseMillis=30000 \
    -XX:-AlwaysTenure"


EXEC_COMP=("HashMap")
EXEC=("HashMap")
# IN=("5" "400" "20000" "40000" )
IN=("100")

# th1, error : verification (missing ptr)
# 41000 - gc 55 - 2m3.7 - 8m43.3 (no error)
# 45000 - gc 44 - 1m31.04  *** - 9m27.13 (no error)
# 60000 - gc 37 - 1m26.8 ***  - 6m2.1
# 500000 - gc 19 - 3m20.46 **  - 12m49.23

#40000 --> th16, error : h2 objs should have been filtered out

 
MAX=100
XMS=1

# rm -f *_out *_compile *.log

${JAVAC} GC.java

for exec_file in "${EXEC_COMP[@]}"
do
    ${JAVAC} ${exec_file}.java > out/${exec_file}_compile 2>&1
done


PAR=16
for exec_file in "${EXEC_COMP[@]}"
do
    for input in "${IN[@]}"
    do
        time ${JAVA} ${FLAGS} \
        -Djava.compiler=NONE \
        -XX:+UseG1GC \
        -XX:ParallelGCThreads=${PAR} \
        -XX:InitialTenuringThreshold=5 \
        -XX:MaxTenuringThreshold=7 \
        -XX:-UseCompiler \
        -Xmx${MAX}g \
        -Xms${XMS}g \
        -XX:-UseCompressedOops \
        -XX:ErrorFile=out/${exec_file}_hs_err_th${PAR}_${input}.log \
        ${exec_file} ${input} > out/${exec_file}_out_th${PAR}_${input} 2>&1 #|| true 
   done
done


# PAR=16
# for exec_file in "${EXEC_COMP[@]}"
# do
#     for input in "${IN[@]}"
#     do
#         time ${JAVA} ${FLAGS} \
#         -Djava.compiler=NONE \
#         -XX:+UseG1GC \
#         -XX:ParallelGCThreads=${PAR} \
#         -XX:InitialTenuringThreshold=5 \
#         -XX:MaxTenuringThreshold=7 \
#         -XX:-UseCompiler \
#         -Xmx${MAX}g \
#         -Xms${XMS}g \
#         -XX:-UseCompressedOops \
#         -XX:ErrorFile=out/${exec_file}_hs_err_th${PAR}_${input}.log \
#         ${exec_file} ${input} > out/${exec_file}_out_th${PAR}_${input} 2>&1 || true 
#    done
# done

