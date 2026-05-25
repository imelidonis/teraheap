import java.lang.reflect.Field;
import java.util.HashSet;
import java.util.Set;
import java.util.SplittableRandom;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Stress test for: humongous objects that are ONLY reachable via backward references
 * from H2 -> H1 (humongous), while concurrent marking (CM) is repeatedly interrupted
 * by Young GCs.
 *
 * Goal: ensure we do NOT lose humongous objects to eager reclaim because the only
 * roots are H2 references.
 *
 * Assumptions:
 *  - Your test harness provides:
 *      GC.cm_start(); GC.wait_cm();
 *      GC.young_gc(); GC.gc();
 *      GC.move_to_old();
 *  - Your fork provides Unsafe.h2TagAndMoveRoot(Object[] obj, int tag, int flags)
 *    (or equivalent) to place the container in H2.
 *
 * Suggested runs:
 *  - Use multiple cycles.
 *  - Increase ALLOCS_PER_CYCLE and HUM_MB.
 *  - Enable your H2 card-segment skipping optimization in the VM flags.
 */
public class Test_H2_HumongousBackRefsStress {

  private static final sun.misc.Unsafe U;
  static {
    try {
      Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
      f.setAccessible(true);
      U = (sun.misc.Unsafe) f.get(null);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  // Allocate something big enough to likely be humongous.
  // Keep this "obviously used" to avoid dead-code elimination.
  static byte[] newHumongous(int mb, int seed) {
    byte[] a = new byte[mb * 1024 * 1024];
    a[0] = (byte) seed;
    a[a.length - 1] = (byte) (seed ^ 0x5A);
    a[a.length / 2] = (byte) (seed ^ 0xA5);
    return a;
  }

  static int checksum(byte[] a) {
    int s = 0;
    s = (s * 31) ^ a[0];
    s = (s * 31) ^ a[a.length - 1];
    s = (s * 31) ^ a[a.length / 2];
    return s;
  }

  // A little extra memory churn to encourage humongous eager reclaim paths to run.
  static void allocateGarbage(int smallAllocs, int smallSize, int humAllocs, int humMb, int seedBase) {
    // Small garbage to trigger lots of young GCs / promotions.
    for (int i = 0; i < smallAllocs; i++) {
      byte[] b = new byte[smallSize];
      b[0] = (byte) (seedBase + i);
    }
    // A few extra humongous allocations to create pressure/fragmentation.
    for (int i = 0; i < humAllocs; i++) {
      byte[] h = newHumongous(humMb, seedBase ^ (i * 17));
      // drop ref
      if (h[0] == 42) System.out.println("unreachable"); // keep compiler honest
    }
  }

  public static void main(String[] args) throws Exception {
    // Keep defaults fairly spicy; you can override via args for local tuning.
    final int H2_SIZE = (args.length > 0) ? Integer.parseInt(args[0]) : (1 << 20); // 1M slots
    final int CYCLES  = (args.length > 1) ? Integer.parseInt(args[1]) : 2;
    final int ALLOCS_PER_CYCLE = (args.length > 2) ? Integer.parseInt(args[2]) : 4096; // edges created during CM
    final int HUM_MB = (args.length > 3) ? Integer.parseInt(args[3]) : 32; // 8/16/32/64
    final int YOUNG_GC_STORM = (args.length > 4) ? Integer.parseInt(args[4]) : 5000;

    // How aggressively we try to force eager reclaim after CM.
    final int POST_CM_YOUNG_GCS = (args.length > 5) ? Integer.parseInt(args[5]) : 200;
    final int POST_CM_FULL_GCS  = (args.length > 6) ? Integer.parseInt(args[6]) : 2;

    // H2 container (the ONLY long-lived reference to humongous objects we create).
    Object[] h2 = new Object[H2_SIZE];
    U.h2TagAndMoveRoot(h2, 13, 0);

    // Baseline fill so many cards appear "old-only" prior to creating new edges during CM.
    Object baseline = new Object();
    for (int i = 0; i < H2_SIZE; i++) h2[i] = baseline;
    GC.move_to_old();
    GC.gc();

    SplittableRandom rnd = new SplittableRandom(0xC0FFEE);

    // We reuse these arrays each cycle to reduce unrelated allocation noise.
    final int keep = Math.min(ALLOCS_PER_CYCLE, 8192);
    int[] slots = new int[keep];
    int[] sums  = new int[keep];

    // FIX: pre-allocated set used to deduplicate slot draws each cycle,
    // sized at 2x keep to keep the load factor low and avoid rehashing.
    // Without this, two entries i and j could draw the same slot: the later
    // write wins in h2[], but sums[i] still holds the checksum of the earlier
    // (now unreachable) array. Verification then either spuriously fails when
    // the GC correctly collects the earlier object, or silently passes when a
    // real bug causes the later object to be reclaimed (if checksums collide).
    final Set<Integer> slotSet = new HashSet<>(keep * 2);

    for (int cycle = 0; cycle < CYCLES; cycle++) {
      System.out.println("\n=== cycle " + cycle + " : start CM + interrupt storm ===");

      // Pre-generate keep *distinct* slot indices before the allocation loop.
      // This must happen outside the CM window to avoid adding allocation noise
      // during the sensitive marking phase; HashSet operations are cheap here.
      slotSet.clear();
      for (int filled = 0; filled < keep; ) {
        int candidate = rnd.nextInt(H2_SIZE);
        if (slotSet.add(candidate)) {
          slots[filled++] = candidate;
        }
      }

      // Start concurrent marking and quickly interrupt with a young GC.
      GC.cm_start();
      GC.young_gc();

      // Generate repeated interruptions while CM is active.
      final AtomicBoolean stop = new AtomicBoolean(false);
      final CountDownLatch start = new CountDownLatch(1);

      // Capture loop vars for the lambda (must be final/effectively final)
      final int cycleFinal = cycle;
      final int humMbFinal = HUM_MB;

      Thread storm = new Thread(() -> {
        try { start.await(); } catch (InterruptedException ignored) {}
        for (int k = 0; k < YOUNG_GC_STORM && !stop.get(); k++) {
          GC.young_gc();
          // Add occasional extra churn to keep pressure high.
          if ((k & 0x3FF) == 0) {
            allocateGarbage(256, 4 * 1024, 1, Math.max(8, humMbFinal / 2), (cycleFinal << 20) ^ k);
          }
        }
      }, "young-gc-storm");
      storm.setDaemon(true);
      storm.start();
      start.countDown();

      // Create many NEW H2 -> humongous backrefs DURING CM, then drop all other refs.
      // Each stored object should remain live solely because H2 points to it.
      // slots[i] is guaranteed distinct for all i in [0, keep), so h2[slots[i]]
      // is never overwritten by a later iteration: sums[i] always matches exactly
      // the object that will be found at h2[slots[i]] during verification.
      for (int i = 0; i < keep; i++) {
        int slot = slots[i]; // distinct by construction — no collision possible

        // Allocate humongous during CM.
        byte[] a = newHumongous(HUM_MB, (cycle << 16) ^ i);

        // Optionally age it / force progress for determinism in your harness.
        // (Even without this, humongous regions are typically handled specially.)
        GC.move_to_old();
        GC.gc();

        // Publish the ONLY durable reference (H2 -> humongous).
        h2[slot] = a;

        // Record expected checksum and drop local reference.
        sums[i] = checksum(a);
        a = null;

        // Immediately try to trigger eager reclaim windows with young GCs.
        // If H2 scanning misses this edge, the humongous could be reclaimed.
        GC.young_gc();
        if ((i & 7) == 0) GC.young_gc();

        // Keep pressure high with some junk allocations now and then.
        if ((i & 0x3F) == 0) allocateGarbage(512, 2 * 1024, 1, Math.max(8, HUM_MB / 2), (cycle << 24) ^ i);
      }

      System.out.println("=== cycle " + cycle + " : wait CM ===");
      GC.wait_cm();
      stop.set(true);

      // After CM completes, encourage reclaim of anything unmarked.
      // If the bug exists, humongous objects only reachable from H2 might get reclaimed here.
      for (int i = 0; i < POST_CM_YOUNG_GCS; i++) {
        GC.young_gc();
        if ((i & 0x1F) == 0) allocateGarbage(256, 4 * 1024, 0, 0, (cycle << 12) ^ i);
      }
      for (int i = 0; i < POST_CM_FULL_GCS; i++) {
        GC.gc();
      }

      // Verify: all slots must still hold the correct humongous arrays and content.
      // Because slots[] has no duplicates, each h2[slots[i]] is an independent cell:
      // a mismatch unambiguously means either the reference was lost (object reclaimed
      // and slot cleared/overwritten) or the object's content was corrupted.
      long ok = 0;
      for (int i = 0; i < keep; i++) {
        Object o = h2[slots[i]];
        if (!(o instanceof byte[])) {
          System.err.println("BUG: slot lost humongous byte[]"
              + " cycle=" + cycle + " i=" + i + " slot=" + slots[i] + " o=" + o);
          System.exit(2);
        }
        int s = checksum((byte[]) o);
        if (s != sums[i]) {
          System.err.println("BUG: checksum mismatch (corruption / reclaimed humongous)"
              + " cycle=" + cycle + " i=" + i + " slot=" + slots[i]
              + " expected=" + sums[i] + " got=" + s);
          System.exit(3);
        }
        ok++;
      }

      System.out.println("cycle " + cycle + " : verified " + ok
          + " humongous objects reachable ONLY via H2 backrefs");

      // Optional: clean some slots to vary the card patterns across cycles.
      // This also helps exercise "old-only" vs "mixed" cards.
      // Note: we index into slots[] (not h2[] directly) so we only reset cells
      // that were written this cycle — no risk of clobbering unrelated entries.
      for (int i = 0; i < keep / 4; i++) {
        int idx = rnd.nextInt(keep);
        h2[slots[idx]] = baseline;
      }
      GC.young_gc();
      GC.gc();
    }

    System.out.println("\nDone.");
  }
}
