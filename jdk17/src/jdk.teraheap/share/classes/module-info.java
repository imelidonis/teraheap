/**
 * Defines the TeraHeap hint API: applications mark the objects to move to
 * H2, the second heap of TeraHeap on a storage device, and query them.
 * <p>
 * Applications on the class path read this module by default. A run-time
 * image built with {@code jlink} must add it ({@code --add-modules
 * jdk.teraheap}).
 *
 * @moduleGraph
 */
module jdk.teraheap {
    exports jdk.teraheap;
}
