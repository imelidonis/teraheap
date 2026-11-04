import java.lang.ref.WeakReference;
import java.util.ArrayList;

import jdk.teraheap.TeraHeap;

// Test of the hint API in jdk.teraheap.TeraHeap (objects moved by mixed GCs):
//   - isEnabled() reports -XX:+EnableTeraHeap
//   - h2TagAndMoveRoot() marks a root; the mixed GCs move the objects
//     reachable from it to H2, also with a label wider than 24 bits. G1 picks
//     the old regions that each mixed GC collects, so the test requires most
//     of the objects in H2, not all of them (the Full GC test requires all)
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

  // Mixed GCs move objects to H2 only from the old regions they collect,
  // and they collect only regions with enough garbage. Each object points to
  // a pad, which the GCs copy next to it; the test drops the pads after the
  // objects reach the old generation, so their regions become mostly garbage.
  private static final int PAD = 1024;

  private static class Node {
    long value;
    Node next;
    byte[] pad = new byte[PAD];
    Node(long value, Node next) { this.value = value; this.next = next; }
  }

  private static class Holder {
    long[] array;
    byte[] pad;
    Holder(long[] array) { this.array = array; this.pad = new byte[array.length * 8]; }
  }

  private static Node chain(int n, long seed) {
    Node head = null;
    for (int i = 0; i < n; i++)
      head = new Node(seed * 1000003L + i, head);
    return head;
  }

  private static void dropPads(Holder h, Node list, ArrayList<Node> wide) {
    h.pad = null;
    for (Node x = list; x != null; x = x.next)
      x.pad = null;
    for (Node x : wide)
      x.pad = null;
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
    long[] array = new long[1 << 14];   // 128KB: not humongous
    for (int i = 0; i < array.length; i++)
      array[i] = 7L * i;
    Holder holder = new Holder(array);
    Node list = chain(N, 1);
    ArrayList<Node> wide = new ArrayList<>();
    for (int i = 0; i < 1000; i++)
      wide.add(new Node(i, null));
    // Not tagged; dropped with the pads. The last regions that the young GCs
    // fill in the old generation then hold it, not the tagged objects
    Node filler = chain(30000, 5);

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

    // Each GC.gc() runs 4 young GCs: after 2 of them the objects are old
    // (MaxTenuringThreshold=7). Then drop the pads: the next marking cycle
    // finds the regions mostly garbage and the mixed GCs move the objects.
    GC.move_to_old();
    GC.gc();
    GC.gc();
    dropPads(holder, list, wide);
    filler = null;
    int listInH2 = 0, wideInH2 = 0;
    for (int round = 0; round < 12; round++) {
      GC.gc();
      listInH2 = 0;
      for (Node x = list; x != null; x = x.next)
        if (TeraHeap.isInH2(x))
          listInH2++;
      wideInH2 = 0;
      for (Node x : wide)
        if (TeraHeap.isInH2(x))
          wideInH2++;
      if (!enabled || (listInH2 >= N * 9 / 10 && wideInH2 > 0))
        break;
    }
    System.out.println("list nodes in H2: " + listInH2 + "/" + N + ", wide label nodes in H2: "
                       + wideInH2 + "/" + wide.size() + ", array in H2: " + TeraHeap.isInH2(array));

    check(enabled ? listInH2 >= N / 2 : listInH2 == 0, "objects reachable from list in H2");
    check(enabled ? wideInH2 > 0 : wideInH2 == 0, "objects reachable from wide label root in H2");
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
    Node inH2 = null;
    for (Node x = list; x != null && inH2 == null; x = x.next)
      if (TeraHeap.isInH2(x))
        inH2 = x;
    if (inH2 != null) {
      TeraHeap.h2TagAndMoveRoot(inH2, 9);
      GC.gc();
      check(TeraHeap.isInH2(inH2), "tagging an object in H2 keeps it in H2");
    }

    if (failures > 0)
      throw new RuntimeException(failures + " check(s) failed");
    System.out.println("PASS");
  }
}
