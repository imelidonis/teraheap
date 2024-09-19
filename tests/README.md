# TeraHeap Test Files

g1_evacuations dir:
    tests for the g1 evacuations (forces minor and major evacuations)
g1_full_gc dir:
    tests for the g1 full gc cycle
parallel_gc dir:
    tests for parallel scavenge major/full gc
                    
## Description
TeraHeap test files are used to test TeraHeap functionalities during
implementation. All these test files are implemented in JAVA. 

## Build
To build and run all test files for TeraHeap:

```sh
Choose what benchmarks you want 

cd g1_evacuations
./compile.sh

    OR

cd g1_full_gc
./compile.sh


```
## Run Tests
There are different modes that you can run the TeraHeap tests.

```sh

Inside the system_gc or evacuations folder do:

# Run tests in interpreter mode
./run.sh 1

# Run tests using only C1 JIT compiler
./run.sh 2

# Run tests using only C2 JIT compiler
./run.sh 3

# Run tests using gdb
./run.sh 4

```
