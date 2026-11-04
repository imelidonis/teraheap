import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.AtomicBoolean;
import jdk.teraheap.TeraHeap;

/**
 * Stress the race:
 *   - One thread repeatedly triggers CM + GCs (causing mark_used(seg1) and traversal of seg1 deps).
 *   - Another thread rapidly creates NEW H2 cross-region edges seg1 -> seg2 by mutating fields
 *     (which should invoke references(seg1, seg2) in your runtime), and then quickly drops
 *     other references so seg2 is a reclaim candidate unless it is marked used via propagation.
 *
 * Detection strategy:
 *   - Maintain a stable H2 "anchor" object in a never-dropped H2 root located in a fixed H2 region (seg1).
 *   - Continually set anchor.ref = target where target is placed in varying H2 regions (seg2 varies).
 *   - After GC activity, verify that anchor.ref is still a valid, touchable object (and its backing array can be read).
 *   - If the dependency insertion races with seg1 used marking and propagation is missed, seg2 can be reclaimed
 *     even though anchor still points to it -> crash, corruption, or verifier throws.
 *
 * Notes:
 *   - This test is intentionally aggressive; tune defaults as needed.
 *   - It assumes your H2 region placement depends on the label applied to roots.
 */
public class Test_H2_DependencyList_Race {
  // The object that resides in H2 and is used as the "source" (seg1).
  // We will constantly mutate ref to point to objects in other H2 regions (seg2).
  static final class Anchor {
    volatile Object ref;      // mutated frequently to create seg1 -> seg2 edges
    final byte[] pad;         // touchable memory to keep it "real"

    Anchor(int padKb) {
      this.pad = new byte[padKb * 1024];
      pad[0] = 1;
      pad[pad.length / 2] = 2;
      pad[pad.length - 1] = 3;
    }

    long touch() {
      long acc = 0;
      acc ^= (pad[0] & 0xFFL);
      acc ^= (pad[pad.length / 2] & 0xFFL) << 8;
      acc ^= (pad[pad.length - 1] & 0xFFL) << 16;
      return acc;
    }
  }

  // Target objects placed in many other H2 regions.
  static final class Target {
    final int id;
    final long cookie;
    final byte[] blob;

    Target(int id, int blobKb) {
      this.id = id;
      this.cookie = ((long) id << 32) ^ 0xC6A4A7935BD1E995L;
      this.blob = new byte[blobKb * 1024];
      // touch to commit
      if (blob.length > 0) {
        blob[0] = (byte) id;
        blob[blob.length / 2] = (byte) (id ^ 0x5A);
        blob[blob.length - 1] = (byte) (id ^ 0x33);
      }
    }

    long touch() {
      int len = blob.length;
      long acc = cookie ^ id;
      if (len > 0) {
        acc ^= (blob[0] & 0xFFL);
        acc ^= (blob[len >>> 1] & 0xFFL) << 8;
        acc ^= (blob[len - 1] & 0xFFL) << 16;
      }
      return acc;
    }
  }

  // Fixed H2 "seg1" root: contains exactly one Anchor that never gets dropped.
  private static Object[] makeAnchorRoot(int padKb, int label) {
    Object[] root = new Object[1];
    TeraHeap.h2TagAndMoveRoot(root, label);
    root[0] = new Anchor(padKb);
    return root;
  }

  // A pool of roots used only to place Target objects in many different H2 regions (vary seg2).
  private static Object[][] makeTargetRoots(int numRoots, int rootLen) {
    Object[][] roots = new Object[numRoots][];
    for (int i = 0; i < numRoots; i++) {
      Object[] r = new Object[rootLen];

      int a2 = i & 0xF;
      TeraHeap.h2TagAndMoveRoot(r, a2);

      roots[i] = r;
    }
    return roots;
  }

  private static long verifyAnchor(Anchor a) {
    long acc = a.touch();
    Object o = a.ref;
    if (o == null) return acc;

    if (!(o instanceof Target)) {
      throw new AssertionError("Anchor.ref unexpected type: " + o.getClass());
    }
    Target t = (Target) o;
    acc ^= t.touch();

    // Extra deref/reads to catch stale freed memory patterns earlier
    byte[] b = t.blob;
    if (b.length > 0) {
      acc ^= (b[0] & 0xFFL) << 24;
      acc ^= (b[b.length / 2] & 0xFFL) << 32;
      acc ^= (b[b.length - 1] & 0xFFL) << 40;
    }

    return acc;
  }

  public static void main(String[] args) throws Exception {
    // Knobs (defaults are aggressive)
    final int RUN_SECS          = (args.length > 0) ? Integer.parseInt(args[0]) : 60;
    final int TARGET_ROOTS      = (args.length > 1) ? Integer.parseInt(args[1]) : 256;
    final int TARGET_ROOT_LEN   = (args.length > 2) ? Integer.parseInt(args[2]) : 1024;
    final int TARGET_BLOB_KB    = (args.length > 3) ? Integer.parseInt(args[3]) : 8;
    final int ANCHOR_PAD_KB     = (args.length > 4) ? Integer.parseInt(args[4]) : 64;

    final int STORES_PER_BURST  = (args.length > 5) ? Integer.parseInt(args[5]) : 200_000;
    final int BURSTS_PER_CM     = (args.length > 6) ? Integer.parseInt(args[6]) : 4;

    final int NULL_OUT_RATE     = (args.length > 7) ? Integer.parseInt(args[7]) : 8;   // 1/N store ops also null-out random target slots
    final int YOUNG_GCS_PER_CM  = (args.length > 8) ? Integer.parseInt(args[8]) : 20_000;
    final int FULL_GC_EVERY     = (args.length > 9) ? Integer.parseInt(args[9]) : 2;   // every N CM cycles do GC.gc()

    System.out.println("RUN_SECS=" + RUN_SECS +
        " TARGET_ROOTS=" + TARGET_ROOTS +
        " TARGET_ROOT_LEN=" + TARGET_ROOT_LEN +
        " TARGET_BLOB_KB=" + TARGET_BLOB_KB +
        " ANCHOR_PAD_KB=" + ANCHOR_PAD_KB +
        " STORES_PER_BURST=" + STORES_PER_BURST +
        " BURSTS_PER_CM=" + BURSTS_PER_CM +
        " NULL_OUT_RATE=1/" + NULL_OUT_RATE +
        " YOUNG_GCS_PER_CM=" + YOUNG_GCS_PER_CM +
        " FULL_GC_EVERY=" + FULL_GC_EVERY);

    // Place anchor in a fixed H2 region (seg1).
    Object[] anchorRoot = makeAnchorRoot(ANCHOR_PAD_KB, 0xE);
    Anchor anchor = (Anchor) anchorRoot[0];

    // Roots used to place targets in many other H2 regions (seg2 varies).
    Object[][] targetRoots = makeTargetRoots(TARGET_ROOTS, TARGET_ROOT_LEN);

    // Reduce noise
    GC.move_to_old();
    GC.gc();

    final AtomicBoolean stop = new AtomicBoolean(false);
    final CountDownLatch start = new CountDownLatch(1);

    // Thread 1: create massive numbers of NEW seg1->seg2 edges by mutating anchor.ref.
    Thread mutator = new Thread(() -> {
      try { start.await(); } catch (InterruptedException ignored) {}
      ThreadLocalRandom rnd = ThreadLocalRandom.current();
      int id = 1;

      while (!stop.get()) {
        for (int burst = 0; burst < BURSTS_PER_CM && !stop.get(); burst++) {
          for (int i = 0; i < STORES_PER_BURST; i++) {
            // Pick a target root (seg2 varies) and slot
            Object[] r = targetRoots[rnd.nextInt(targetRoots.length)];
            int idx = rnd.nextInt(r.length);

            // Create a brand new Target and install it in that seg2 root slot
            Target t = new Target(id++, TARGET_BLOB_KB);
            r[idx] = t;

            // Create the critical edge: seg1 (anchor) -> seg2 (t)
            // This should trigger references(seg1, seg2) in your runtime.
            anchor.ref = t;

            // Occasionally null-out some target slots to make seg2 regions reclaim candidates
            if ((i % NULL_OUT_RATE) == 0) {
              Object[] rr = targetRoots[rnd.nextInt(targetRoots.length)];
              rr[rnd.nextInt(rr.length)] = null;
            }

            // Also occasionally drop the edge itself to churn dependencies
            if ((i & 0x3FFF) == 0) {
              anchor.ref = null;
            }
          }
        }
      }
    }, "h2-deps-mutator");

    // Thread 2: repeatedly trigger CM + lots of young GCs + occasional full GC.
    // The goal is to maximize opportunities where seg1 is marked used and dependency lists are traversed
    // concurrently with the insertion of new dependencies.
    Thread gcDriver = new Thread(() -> {
      try { start.await(); } catch (InterruptedException ignored) {}
      long end = System.currentTimeMillis() + RUN_SECS * 1000L;
      int cm = 0;

      while (!stop.get() && System.currentTimeMillis() < end) {
        GC.cm_start();
        GC.young_gc(); // interrupt quickly

        for (int i = 0; i < YOUNG_GCS_PER_CM; i++) {
          GC.young_gc();
          if ((i & 0x1FFF) == 0) {
            // Touch the anchor frequently to keep it hot/live and to force loads through it.
            verifyAnchor(anchor);
          }
        }

        GC.wait_cm();

        if (FULL_GC_EVERY > 0 && (cm % FULL_GC_EVERY) == 0) {
          GC.gc();
        }
        cm++;

        // Strong check every CM cycle: if anchor.ref points somewhere, it must be safe to touch.
        // If a dependent region was falsely reclaimed, this often crashes or throws here.
        long v = verifyAnchor(anchor);
        if ((cm % 10) == 0) {
          System.out.println("gcDriver: cm=" + cm + " verify=" + v);
        }
      }

      stop.set(true);
    }, "gc-driver");

    mutator.setDaemon(true);
    gcDriver.setDaemon(true);
    mutator.start();
    gcDriver.start();
    start.countDown();

    // Wait until gcDriver ends the run
    while (!stop.get()) {
      Thread.sleep(100);
    }

    // Final push
    for (int i = 0; i < 5; i++) GC.gc();
    long fin = verifyAnchor(anchor);
    System.out.println("Done. final verify=" + fin);
  }
}
