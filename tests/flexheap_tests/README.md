HeapPercentage.java simulates varying heap utilization levels (from 0% to 98%) across phases, measuring how the GC and resizing policy react to gradual memory pressure without I/O.
This benchmark is designed to test whether FlexHeap detects high utilization and shrinks/grows the heap correctly in response to shifting application needs.

GCAndIOPhases.java alternates between GC-heavy and I/O-heavy phases in cycles, allocating large arrays to fill the heap followed by synchronized map operations that mimic H2 I/O.
The GC phase forces object retention to generate pause pressure, while the I/O phase simulates off-heap access patterns that should benefit from page cache availability.

GCAndIOPhasesMixedBarrier.java builds on the above by adding strict phase separation: all threads synchronize with a barrier after allocation and before reads to isolate GC and I/O activity cleanly.
This barrier coordination helps for better visualization because it seperates the GC and I/O phases when printing the overheads.
