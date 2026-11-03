## TeraHeap: Reducing Memory Pressure in Managed Big Data Frameworks

### Description

TeraHeap extends the managed runtime (JVM) to use a second,
high-capacity heap over a fast storage device that coexists with the
regular heap. TeraHeap provides direct access to objects on the second
heap (no S/D). It also reduces GC cost by fencing the garbage
collector from scanning the second heap. TeraHeap leverages
frameworks’ property of choosing specific objects for off-heap
placement and offers frameworks a hint-based interface for moving
such objects to the second heap. 

### Install Prerequisites
Install the following packages:
```sh
sudo yum install python3-pip
pip3 install scan-build --user
pip3 install compdb --user
```

### Build
1. Build allocator.
```sh
cd allocator
./build.sh
cd -
```
Read the README.md file in allocator directory to export the specific
environment variables

2. Build JVM (release mode)
```sh
cd jdk8u345
./compile.sh -r
cd -
```

3. Build JVM (fastdebug mode)
```sh
cd jdk8u345
./compile.sh -d
cd -
```

### Hint API (JDK 17)
Applications mark the roots of the data structures to move to H2 with the
class `jdk.teraheap.TeraHeap` (module `jdk.teraheap`). The garbage collector
moves each root and the objects reachable from it to H2, and places the
objects of the same label together.
```java
import jdk.teraheap.TeraHeap;

TeraHeap.h2TagAndMoveRoot(cachedData, datasetId); // mark a root with a label
TeraHeap.isInH2(cachedData);         // is the object in H2?
TeraHeap.isMarkedMoveH2(cachedData); // is the object marked to move to H2?
TeraHeap.isEnabled();                // does the JVM run with -XX:+EnableTeraHeap?
```
- Without `-XX:+EnableTeraHeap` the methods do nothing and the queries
  return `false`, so the same application runs in both modes.
- A `null` object is ignored. Class mirrors, `Reference` objects, class
  loaders and arrays of them stay in H1.
- Applications on the class path see the module with no extra option. A
  run-time image built with `jlink` must add it:
  `jlink --add-modules jdk.teraheap,...`.
- `sun.misc.Unsafe` keeps its hints (`h2TagAndMoveRoot`, `h2TagRoot`,
  `h2Move`, `is_in_h2`, `is_marked_move_h2`).

## Benchmarks
To run benchmarks please clone the repository
[tera_applications](https://github.com/jackkolokasis/tera_applications)
and read the README.md files in each application directory. There are
instructions about how to compile and run the different applications.
