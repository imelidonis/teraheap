package jdk.teraheap;

/**
 * Hints from the application to TeraHeap.
 * <p>
 * TeraHeap extends the Java heap with H2, a second heap on a storage device.
 * The application marks the roots of the data structures that it wants in
 * H2, each with a label, and the garbage collector moves the objects
 * reachable from them to H2. The H2 allocator places the objects of a label
 * together, so they can be freed together.
 * <p>
 * When the JVM does not run with {@code -XX:+EnableTeraHeap}, the methods do
 * nothing and the queries return {@code false}: an application can call them
 * without checking {@link #isEnabled()} first. A {@code null} object is
 * ignored by the same rule.
 * <p>
 * {@code sun.misc.Unsafe} has the same hints.
 */
public final class TeraHeap {
    private TeraHeap() {}

    private static native void registerNatives();
    static {
        registerNatives();
    }

    // -XX:+EnableTeraHeap cannot change while the JVM runs
    private static final boolean ENABLED = isEnabled0();

    /**
     * Returns whether the JVM runs with TeraHeap ({@code -XX:+EnableTeraHeap}).
     *
     * @return {@code true} if TeraHeap is enabled
     */
    public static boolean isEnabled() {
        return ENABLED;
    }

    /**
     * Marks {@code o} as a root to move to H2 with {@code label}. The garbage
     * collector moves {@code o} and the objects reachable from it to H2,
     * grouped by label.
     * <p>
     * Does nothing if {@code o} is {@code null}, is already in H2, or is a
     * {@code Class}, a {@code java.lang.ref.Reference}, a {@code ClassLoader},
     * or an array of them: these stay in the Java heap. The label keeps its
     * low 24 bits (32 bits without compressed class pointers).
     *
     * @param o     the root object
     * @param label the label of the data structure, defined by the application
     *              (for example, the id of a cached dataset)
     */
    public static void h2TagAndMoveRoot(Object o, long label) {
        if (ENABLED && o != null) {
            h2TagAndMoveRoot0(o, label);
        }
    }

    /**
     * Returns whether {@code o} is in H2.
     *
     * @param o the object
     * @return {@code true} if {@code o} is in H2; {@code false} if it is in
     *         the Java heap, is {@code null}, or TeraHeap is not enabled
     */
    public static boolean isInH2(Object o) {
        return ENABLED && o != null && isInH2_0(o);
    }

    /**
     * Returns whether {@code o} is marked to move to H2 (by
     * {@link #h2TagAndMoveRoot(Object, long)}, or as reachable from a
     * marked root).
     *
     * @param o the object
     * @return {@code true} if {@code o} is marked to move to H2; {@code false}
     *         otherwise, if {@code o} is {@code null}, or if TeraHeap is not
     *         enabled
     */
    public static boolean isMarkedMoveH2(Object o) {
        return ENABLED && o != null && isMarkedMoveH2_0(o);
    }

    private static native boolean isEnabled0();
    private static native void h2TagAndMoveRoot0(Object o, long label);
    private static native boolean isInH2_0(Object o);
    private static native boolean isMarkedMoveH2_0(Object o);
}
