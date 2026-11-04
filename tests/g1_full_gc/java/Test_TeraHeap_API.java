import java.lang.ref.WeakReference;
import java.util.ArrayList;

import jdk.teraheap.TeraHeap;

// Test of the hint API in jdk.teraheap.TeraHeap (objects moved by a Full GC):
//   - isEnabled() reports -XX:+EnableTeraHeap
//   - h2TagAndMoveRoot() marks a root; the GC moves the root and the objects
//     reachable from it to H2, also with a label wider than 24 bits
//   - Class, Reference and ClassLoader objects (and arrays of them) are not
//     marked and stay in H1
//   - null is ignored, as in sun.misc.Unsafe
//   - the data of the objects in H2 is intact
// With -XX:-EnableTeraHeap every call does nothing and the queries return
// false.
public class Test_TeraHeap_API {
  private static int failures = 0;

  private static void check(boolean ok, String what) {
    System.out.println((ok ? "OK    " : "FAIL  ") + what);
    if (!ok)
      failures++;
  }

  private static class Node {
    long value;
    Node next;
    Node(long value, Node next) { this.value = value; this.next = next; }
  }

  private static Node chain(int n, long seed) {
    Node head = null;
    for (int i = 0; i < n; i++)
      head = new Node(seed * 1000003L + i, head);
    return head;
  }

  private static boolean chainIntact(Node head, int n, long seed) {
    int i = n - 1;
    for (Node x = head; x != null; x = x.next, i--)
      if (x.value != seed * 1000003L + i)
        return false;
    return i == -1;
  }

  public static void main(String[] args) {
    final boolean enabled = TeraHeap.isEnabled();
    System.out.println("TeraHeap enabled: " + enabled);

    final int N = 100000;
    long[] array = new long[1 << 20];
    for (int i = 0; i < array.length; i++)
      array[i] = 7L * i;
    Node list = chain(N, 1);
    ArrayList<Node> wide = new ArrayList<>();
    for (int i = 0; i < 1000; i++)
      wide.add(new Node(i, null));

    Object[] meta = { String.class, new WeakReference<>(list), new ClassLoader() {},
                      new Class<?>[] { Integer.class }, new WeakReference<?>[1] };

    TeraHeap.h2TagAndMoveRoot(array, 1);
    TeraHeap.h2TagAndMoveRoot(list, 2);
    TeraHeap.h2TagAndMoveRoot(wide, 0xABCDEF12L);   // wider than 24 bits
    for (Object m : meta)
      TeraHeap.h2TagAndMoveRoot(m, 3);
    TeraHeap.h2TagAndMoveRoot(null, 4);              // ignored

    check(TeraHeap.isMarkedMoveH2(array) == enabled, "array marked");
    check(TeraHeap.isMarkedMoveH2(list) == enabled, "list marked");
    check(TeraHeap.isMarkedMoveH2(wide) == enabled, "wide label marked");
    for (Object m : meta)
      check(!TeraHeap.isMarkedMoveH2(m), "metadata not marked: " + m.getClass().getName());
    check(!TeraHeap.isMarkedMoveH2(null), "isMarkedMoveH2(null) is false");
    check(!TeraHeap.isInH2(null), "isInH2(null) is false");

    
    System.gc();

    check(TeraHeap.isInH2(array) == enabled, "array in H2");
    check(TeraHeap.isInH2(list) == enabled, "list in H2");
    check(TeraHeap.isInH2(list.next.next) == enabled, "object reachable from list in H2");
    check(TeraHeap.isInH2(wide) == enabled, "wide label root in H2");
    check(TeraHeap.isInH2(wide.get(500)) == enabled, "object reachable from wide label root in H2");
    for (Object m : meta)
      check(!TeraHeap.isInH2(m), "metadata in H1: " + m.getClass().getName());

    boolean arrayOk = true;
    for (int i = 0; i < array.length; i++)
      arrayOk &= array[i] == 7L * i;
    check(arrayOk, "array data intact");
    check(chainIntact(list, N, 1), "list data intact");
    boolean wideOk = true;
    for (int i = 0; i < wide.size(); i++)
      wideOk &= wide.get(i).value == i;
    check(wideOk, "wide label data intact");

    // Tagging an object already in H2 does nothing
    TeraHeap.h2TagAndMoveRoot(list, 9);
    check(TeraHeap.isInH2(list) == enabled, "tagging an object in H2 keeps it in H2");

    if (failures > 0)
      throw new RuntimeException(failures + " check(s) failed");
    System.out.println("PASS");
  }
}
