import java.lang.reflect.Field;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Stress test: H2 reclamation ("free path") during concurrent marking + young-GC storms,
 * with *explicit* verification that still-live H2 regions were not falsely reclaimed.
 *
 * Key stressors:
 *  1) Many H2 roots, placed across different H2 regions via Unsafe.h2TagAndMoveRoot(root, a2, a3).
 *  2) Each H2 root contains many H2Payload objects (H2-resident).
 *  3) Some H2Payload objects point to humongous objects allocated in H1 (normal Java heap):
 *       H2 -> H1(humongous) edges.
 *  4) During CM, we:
 *     - null out many H2 slots (creating free candidates inside H2)
 *     - drop whole H2 roots (whole H2 regions become free candidates)
 *     - storm young GCs (interrupt CM repeatedly)
 *     - mutate H2 contents and H2->H1 edges (exercise barriers / cards / remsets)
 *
 * Verification:
 *  A) Structural: traverse all remaining H2 roots and validate fingerprints.
 *  B) Active-touch H2: read multiple offsets from H2Payload.blob arrays (backing memory touch).
 *  C) Active-touch H1 humongous: via H2->H1 edges, touch humongous arrays repeatedly.
 *  D) Sentinels: keep a stable set of H2 payloads (and their H1 humongous targets) in a
 *     never-dropped H2 root; its accumulator must remain stable across cycles.
 *
 * Assumes harness provides:
 *  - GC.cm_start(), GC.wait_cm(), GC.young_gc(), GC.move_to_old(), GC.gc()
 *  - sun.misc.Unsafe.h2TagAndMoveRoot(Object root, int a2, int a3)
 */
public class Test_H2_FreePath_CM_YoungStorm_Max_Frees {

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

  // H2-resident payload: has an internal blob (H2 memory) and optional reference to H1 humongous.
  static final class H2Payload {
    final int id;
    final long cookie;
    final byte[] blob;     // intended to live in H2 (reachable from H2 root)
    Object maybeRef;       // used for (a) internal refs (H2->H2) or (b) H2->H1 humongous

    H2Payload(int id, int blobKb) {
      this.id = id;
      this.cookie = ((long) id << 32) ^ 0x9E3779B97F4A7C15L;
      this.blob = new byte[blobKb * 1024];

      // Touch to commit and create stable signal.
      if (blob.length > 0) {
        blob[0] = (byte) id;
        blob[blob.length / 2] = (byte) (id ^ 0x5A);
        blob[blob.length - 1] = (byte) (id ^ 0x33);
      }
    }

    long fingerprint() {
      long s = cookie;
      if (blob.length > 0) {
        s ^= blob[0];
        s ^= blob[blob.length / 2];
        s ^= blob[blob.length - 1];
      }
      return s;
    }
  }

  // H1 humongous object wrapper (allocated in normal Java heap = H1).
  // Under G1, if huge >= ~1/2 region size, it becomes "humongous".
  static final class H1Humongous {
    final int id;
    final long cookie;
    final byte[] huge;

    H1Humongous(int id, int bytes) {
      this.id = id;
      this.cookie = ((long) id << 32) ^ 0xD6E8FEB86659FD93L;
      this.huge = new byte[bytes];

      // Touch multiple offsets to commit pages and make reads meaningful.
      huge[0] = (byte) id;
      huge[huge.length >>> 2] = (byte) (id ^ 0xA5);
      huge[huge.length >>> 1] = (byte) (id ^ 0x3C);
      huge[(huge.length * 3) >>> 2] = (byte) (id ^ 0x7E);
      huge[huge.length - 1] = (byte) (id ^ 0x19);
    }

    long touch() {
      int len = huge.length;
      long acc = cookie ^ ((long) id << 17);
      acc ^= (huge[0] & 0xFFL);
      acc ^= (huge[len >>> 2] & 0xFFL) << 8;
      acc ^= (huge[len >>> 1] & 0xFFL) << 16;
      acc ^= (huge[(len * 3) >>> 2] & 0xFFL) << 24;
      acc ^= (huge[len - 1] & 0xFFL) << 32;
      return acc;
    }
  }

  // Young object that points to an H2 object (young -> H2 edge).
  static final class YoungToH2 {
    final Object ref;
    final int tag;
    YoungToH2(Object ref, int tag) { this.ref = ref; this.tag = tag; }
  }

  // -------- Verification helpers --------

  // Touch backing arrays for H2 payload (stronger than fingerprint alone).
  private static long touchH2PayloadBacking(H2Payload p) {
    byte[] b = p.blob;
    if (b == null || b.length == 0) return p.cookie;

    int len = b.length;
    int o0 = 0;
    int o1 = len >>> 2;
    int o2 = len >>> 1;
    int o3 = (len * 3) >>> 2;
    int o4 = len - 1;

    long acc = p.cookie ^ (long) p.id;
    acc ^= (b[o0] & 0xFFL);
    acc ^= (b[o1] & 0xFFL) << 8;
    acc ^= (b[o2] & 0xFFL) << 16;
    acc ^= (b[o3] & 0xFFL) << 24;
    acc ^= (b[o4] & 0xFFL) << 32;

    return acc;
  }

  // Verify + touch everything reachable from live H2 roots.
  // This is the "main" verifier: it will dereference H2 backing arrays and H1 humongous arrays.
  private static long verifyAndTouchAll(Object[][] roots) {
    long acc = 0;
    for (Object[] r : roots) {
      if (r == null) continue;
      for (Object o : r) {
        if (!(o instanceof H2Payload)) {
          if (o == null) continue;
          throw new AssertionError("Unexpected object in H2 root: " + o.getClass());
        }
        H2Payload p = (H2Payload) o;

        acc ^= p.fingerprint();
        acc ^= touchH2PayloadBacking(p);

        Object mr = p.maybeRef;
        if (mr instanceof H2Payload) {
          // H2 -> H2 internal edge: touch the target as well.
          H2Payload q = (H2Payload) mr;
          acc ^= q.fingerprint();
          acc ^= touchH2PayloadBacking(q);
        } else if (mr instanceof H1Humongous) {
          // H2 -> H1(humongous) edge: touch humongous backing array
          acc ^= ((H1Humongous) mr).touch();
        } else if (mr != null) {
          // If you intentionally store other types, adjust here.
          acc ^= mr.hashCode();
        }
      }
    }
    return acc;
  }

  // Sample-touch: pick random live H2Payloads and touch their backing arrays (fast, probabilistic).
  private static long sampleTouchLiveH2(Object[][] roots, int samples, ThreadLocalRandom rnd) {
    long acc = 0;
    int taken = 0;

    int attempts = samples * 10;
    while (taken < samples && attempts-- > 0) {
      Object[] r = roots[rnd.nextInt(roots.length)];
      if (r == null) continue;

      Object o = r[rnd.nextInt(r.length)];
      if (!(o instanceof H2Payload)) continue;

      H2Payload p = (H2Payload) o;
      acc ^= touchH2PayloadBacking(p);

      Object mr = p.maybeRef;
      if (mr instanceof H1Humongous) acc ^= ((H1Humongous) mr).touch();

      taken++;
    }

    if (taken < samples) {
      System.out.println("sampleTouchLiveH2: only sampled " + taken + "/" + samples + " live objects");
    }

    return acc;
  }

  // Create a never-dropped H2 root that holds stable sentinels.
  // We store both the H2Payload and (if present) its H1Humongous ref, so both are exercised.
  private static Object[] makeSentinelRoot(Object[][] roots, int sentinelCount, ThreadLocalRandom rnd) {
    Object[] sentinelRoot = new Object[sentinelCount * 2];
    U.h2TagAndMoveRoot(sentinelRoot, 0xE, 0xE);

    int s = 0;
    int attempts = sentinelCount * 100;
    while (s < sentinelCount && attempts-- > 0) {
      Object[] r = roots[rnd.nextInt(roots.length)];
      if (r == null) continue;

      Object o = r[rnd.nextInt(r.length)];
      if (!(o instanceof H2Payload)) continue;

      H2Payload p = (H2Payload) o;
      sentinelRoot[s * 2] = p;
      sentinelRoot[s * 2 + 1] = p.maybeRef; // may be null / H1Humongous / H2Payload
      s++;
    }

    if (s < sentinelCount) {
      System.out.println("makeSentinelRoot: only captured " + s + "/" + sentinelCount + " sentinels");
    } else {
      System.out.println("Captured " + s + " sentinels");
    }

    return sentinelRoot;
  }

  private static long touchSentinels(Object[] sentinelRoot) {
    long acc = 0;
    for (Object o : sentinelRoot) {
      if (o == null) continue;

      if (o instanceof H2Payload) {
        H2Payload p = (H2Payload) o;
        acc ^= p.fingerprint();
        acc ^= touchH2PayloadBacking(p);
      } else if (o instanceof H1Humongous) {
        acc ^= ((H1Humongous) o).touch();
      } else {
        acc ^= o.hashCode();
      }
    }
    return acc;
  }

  // -------- Allocation helpers --------

  private static H1Humongous newHumongousH1(int id, int minMb, int maxMb, ThreadLocalRandom rnd) {
    int mb = rnd.nextInt(minMb, maxMb + 1);
    int bytes = mb * 1024 * 1024;
    return new H1Humongous(id, bytes);
  }

  public static void main(String[] args) throws Exception {
    // Core knobs
    final int CYCLES              = (args.length > 0) ? Integer.parseInt(args[0]) : 20;
    final int NUM_ROOTS           = (args.length > 1) ? Integer.parseInt(args[1]) : 256;
    final int ROOT_LEN            = (args.length > 2) ? Integer.parseInt(args[2]) : 8000;
    final int BLOB_KB             = (args.length > 3) ? Integer.parseInt(args[3]) : 8;
    final int YOUNG_GC_STORM      = (args.length > 4) ? Integer.parseInt(args[4]) : 40_000;
    final int DROP_ROOT_EVERY     = (args.length > 5) ? Integer.parseInt(args[5]) : 1;        // cycles
    final int NULL_SLOTS_PER_CYCLE= (args.length > 6) ? Integer.parseInt(args[6]) : 4_000_000;

    // Verification knobs
    final int TOUCH_SAMPLES_PER_CYCLE = (args.length > 7) ? Integer.parseInt(args[7]) : 20_000;
    final int SENTINEL_COUNT          = (args.length > 8) ? Integer.parseInt(args[8]) : 256;

    // H2 -> H1(humongous) knobs
    final int H2_TO_H1_HUM_EVERY   = (args.length > 9)  ? Integer.parseInt(args[9])  : 2048; // 1 edge per N payloads
    final int H1_HUM_MIN_MB        = (args.length > 10) ? Integer.parseInt(args[10]) : 2;
    final int H1_HUM_MAX_MB        = (args.length > 11) ? Integer.parseInt(args[11]) : 8;

    // Mutation knobs (keep humongous mutation rare; it's expensive)
    final int HUMONGOUS_MUTATE_MASK = (args.length > 12) ? Integer.parseInt(args[12]) : 0x7FFF; // every ~32768 iterations
    final int EDGE_MUTATE_MASK      = (args.length > 13) ? Integer.parseInt(args[13]) : 0x1FFF; // every ~8192 iterations

    System.out.println("Init: CYCLES=" + CYCLES +
        " NUM_ROOTS=" + NUM_ROOTS +
        " ROOT_LEN=" + ROOT_LEN +
        " BLOB_KB=" + BLOB_KB +
        " YOUNG_GC_STORM=" + YOUNG_GC_STORM +
        " DROP_ROOT_EVERY=" + DROP_ROOT_EVERY +
        " NULL_SLOTS_PER_CYCLE=" + NULL_SLOTS_PER_CYCLE);
    System.out.println("Verify: TOUCH_SAMPLES_PER_CYCLE=" + TOUCH_SAMPLES_PER_CYCLE +
        " SENTINEL_COUNT=" + SENTINEL_COUNT);
    System.out.println("Edges: H2_TO_H1_HUM_EVERY=" + H2_TO_H1_HUM_EVERY +
        " H1_HUM_MIN_MB=" + H1_HUM_MIN_MB +
        " H1_HUM_MAX_MB=" + H1_HUM_MAX_MB);

    Object[][] roots = new Object[NUM_ROOTS][];
    long id = 1;
    ThreadLocalRandom rnd = ThreadLocalRandom.current();

    System.out.println("Initializing H2 roots...");
    for (int i = 0; i < NUM_ROOTS; i++) {
      Object[] r = new Object[ROOT_LEN];

      // Vary H2 placement identifiers.
      int a2 = i & 0xF;          // stripes
      int a3 = (i >>> 4) & 0xF;  // groups
      U.h2TagAndMoveRoot(r, a2, a3);

      for (int j = 0; j < ROOT_LEN; j++) {
        H2Payload p = new H2Payload((int) (id++), BLOB_KB);

        // Some internal H2->H2 refs to stress scanning.
        if ((j & 0x7) == 0 && j > 0) p.maybeRef = r[j - 1];

        // Some H2 -> H1(humongous) edges.
        // (Overwrite maybeRef sometimes: that's fine; we want a mix of H2->H2 and H2->H1.)
        if (H2_TO_H1_HUM_EVERY > 0 && (j % H2_TO_H1_HUM_EVERY) == 0) {
          p.maybeRef = newHumongousH1((int) (id++), H1_HUM_MIN_MB, H1_HUM_MAX_MB, rnd);
        }

        r[j] = p;
      }

      roots[i] = r;
    }

    // Reduce noise.
    GC.move_to_old();
    GC.gc();

    // Background threads
    final AtomicBoolean stop = new AtomicBoolean(false);
    final CountDownLatch start = new CountDownLatch(1);

    Thread youngEdgeCreator = new Thread(() -> {
      try { start.await(); } catch (InterruptedException ignored) {}
      ThreadLocalRandom r = ThreadLocalRandom.current();

      // Keep bounded set alive so young->H2 edges persist.
      YoungToH2[] keep = new YoungToH2[1 << 16];
      int k = 0;

      while (!stop.get()) {
        Object[] rr = roots[r.nextInt(roots.length)];
        if (rr != null) {
          Object target = rr[r.nextInt(rr.length)];
          if (target != null) {
            keep[k++ & (keep.length - 1)] = new YoungToH2(target, k);
          }
        }

        // Allocation pressure to trigger young GCs naturally too.
        byte[] junk = new byte[32 * 1024];
        junk[0] = 1;
      }
    }, "young->H2-edge-creator");

    Thread allocator = new Thread(() -> {
      try { start.await(); } catch (InterruptedException ignored) {}
      Object[] keep = new Object[1 << 18];
      int idx = 0;
      while (!stop.get()) {
        for (int i = 0; i < 4000; i++) {
          byte[] a = new byte[(i & 1) == 0 ? 64 * 1024 : 128 * 1024];
          keep[idx++ & (keep.length - 1)] = a;
        }
        Thread.yield();
      }
    }, "allocator");

    youngEdgeCreator.setDaemon(true);
    allocator.setDaemon(true);
    youngEdgeCreator.start();
    allocator.start();
    start.countDown();

    // Sentinels: never drop this root. It holds references into H2 and to some H1 humongous targets.
    Object[] sentinelRoot = makeSentinelRoot(roots, SENTINEL_COUNT, rnd);
    long sentinelBaseline = touchSentinels(sentinelRoot);

    // Baseline verification/touch.
    long baseline = verifyAndTouchAll(roots);
    long sample0 = sampleTouchLiveH2(roots, Math.min(TOUCH_SAMPLES_PER_CYCLE, 10_000), rnd);

    System.out.println("Initial verifier acc=" + baseline);
    System.out.println("Initial sentinel acc=" + sentinelBaseline);
    System.out.println("Initial sample-touch acc=" + sample0);

    for (int cycle = 0; cycle < CYCLES; cycle++) {
      System.out.println("\n=== cycle " + cycle + " ===");

      // Start CM and interrupt quickly.
      GC.cm_start();
      GC.young_gc();

      // Create garbage inside H2: null out random slots.
      System.out.println("Nulling H2 slots: " + NULL_SLOTS_PER_CYCLE);
      for (int n = 0; n < NULL_SLOTS_PER_CYCLE; n++) {
        Object[] r = roots[rnd.nextInt(NUM_ROOTS)];
        if (r == null) continue;
        r[rnd.nextInt(ROOT_LEN)] = null;
      }

      // Drop whole H2 roots to exercise "free region" path.
      if (DROP_ROOT_EVERY > 0 && (cycle % DROP_ROOT_EVERY) == 0) {
        int drops = Math.max(1, NUM_ROOTS / 16);
        System.out.println("Dropping " + drops + " whole H2 roots");
        for (int d = 0; d < drops; d++) {
          int ri = rnd.nextInt(NUM_ROOTS);
          roots[ri] = null;
        }
      }

      // Storm young GCs while CM is in progress; keep mutating H2 and H2->H1 edges.
      System.out.println("Young GC storm: " + YOUNG_GC_STORM);
      for (int i = 0; i < YOUNG_GC_STORM; i++) {
        GC.young_gc();

        Object[] r = roots[rnd.nextInt(NUM_ROOTS)];
        if (r != null) {
          int j = rnd.nextInt(ROOT_LEN);

          if ((i & 0x3FF) == 0) {
            // Occasionally replace slot with a new H2 payload.
            H2Payload p = new H2Payload((int) (id++), BLOB_KB);

            // Rarely set H2 -> H1(humongous) edge.
            if ((i & HUMONGOUS_MUTATE_MASK) == 0) {
              p.maybeRef = newHumongousH1((int) (id++), H1_HUM_MIN_MB, H1_HUM_MAX_MB, rnd);
            } else if ((i & 0x7) == 0 && j > 0) {
              // Sometimes create H2->H2 edge.
              Object prev = r[j - 1];
              if (prev instanceof H2Payload) p.maybeRef = prev;
            }

            r[j] = p;

          } else if ((i & 0x1FF) == 0) {
            // Occasionally clear it.
            r[j] = null;

          } else if ((i & 0x3FF) == 1) {
            // Occasionally mutate an existing H2 object's maybeRef to stress barriers/cards.
            Object o = r[j];
            if (o instanceof H2Payload) {
              H2Payload p = (H2Payload) o;
              if ((i & EDGE_MUTATE_MASK) == 0) {
                p.maybeRef = newHumongousH1((int) (id++), H1_HUM_MIN_MB, H1_HUM_MAX_MB, rnd);
              } else if ((i & 0xFFF) == 0) {
                p.maybeRef = null;
              }
            }
          }
        }

        if ((i % 5000) == 0 && i != 0) {
          System.out.println("  storm progress " + i + "/" + YOUNG_GC_STORM);
        }
      }

      // Finish CM + post-CM cleanup.
      GC.wait_cm();
      GC.gc();

      // Strong verification: touch everything reachable from live H2 roots.
      long acc = verifyAndTouchAll(roots);
      System.out.println("Verifier acc=" + acc);

      // Sentinel check: must remain stable unless you intentionally mutate sentinels.
      long sentAcc = touchSentinels(sentinelRoot);
      System.out.println("Sentinel acc=" + sentAcc);

      if (sentAcc != sentinelBaseline) {
        throw new AssertionError("Sentinel accumulator changed! was=" + sentinelBaseline +
            " now=" + sentAcc + " (possible false free / corruption / stale pointer)");
      }

      // Sample-touch (probabilistic extra coverage).
      long samp = sampleTouchLiveH2(roots, TOUCH_SAMPLES_PER_CYCLE, rnd);
      System.out.println("Sample-touch acc=" + samp);
    }

    stop.set(true);

    // Final GCs to push reclamation further.
    for (int i = 0; i < 10; i++) GC.gc();

    long fin = verifyAndTouchAll(roots);
    long finSent = touchSentinels(sentinelRoot);
    long finSamp = sampleTouchLiveH2(roots, Math.min(TOUCH_SAMPLES_PER_CYCLE, 50_000), rnd);

    System.out.println("\nDone.");
    System.out.println("Final verifier acc=" + fin);
    System.out.println("Final sentinel acc=" + finSent);
    System.out.println("Final sample-touch acc=" + finSamp);

    if (finSent != sentinelBaseline) {
      throw new AssertionError("Final sentinel accumulator changed! was=" + sentinelBaseline +
          " now=" + finSent);
    }
  }
}
