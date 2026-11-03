import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.AtomicBoolean;
import jdk.teraheap.TeraHeap;

public class Test_H2_CM_YoungInterrupt {
  // Allocate something big enough to be humongous.
  // Humongous threshold is region-based; 8–32MB is usually humongous on typical region sizes.
  static byte[] newHumongous(int mb, int seed) {
    byte[] a = new byte[mb * 1024 * 1024];
    // write a few bytes so the compiler can't elide it
    a[0] = (byte) seed;
    a[a.length - 1] = (byte) (seed ^ 0x5A);
    return a;
  }

  static int checksum(byte[] a) {
    // touch it in a way that forces real reads
    int s = 0;
    s ^= a[0];
    s ^= a[a.length - 1];
    s ^= a[a.length / 2];
    return s;
  }

  public static void main(String[] args) throws Exception {
    final int H2_SIZE = 1 << 20; // 1M slots in H2 container
    final int CYCLES  = (args.length > 0) ? Integer.parseInt(args[0]) : 1;
    final int ALLOCS_PER_CYCLE = (args.length > 1) ? Integer.parseInt(args[1]) : 3000;
    final int HUM_MB = (args.length > 2) ? Integer.parseInt(args[2]) : 32; // try 8/16/32
    final int YOUNG_GC_STORM = (args.length > 3) ? Integer.parseInt(args[3]) : 2000;

    // H2 container that will hold backward references.
    Object[] h2 = new Object[H2_SIZE];
    TeraHeap.h2TagAndMoveRoot(h2, 13);

    // Baseline: fill with something old-ish so many cards look old-only.
    Object baseline = new Object();
    for (int i = 0; i < H2_SIZE; i++) h2[i] = baseline;
    GC.move_to_old();
    GC.gc();

    ThreadLocalRandom rnd = ThreadLocalRandom.current();

    for (int cycle = 0; cycle < CYCLES; cycle++) {
      System.out.println("\n=== cycle " + cycle + " CM start ===");
      GC.cm_start();

      // Ensure CM is interrupted by young GC early.
      GC.young_gc();

      // A young-GC storm thread to ensure "many interruptions while CM active".
      final AtomicBoolean stop = new AtomicBoolean(false);
      final CountDownLatch start = new CountDownLatch(1);

      Thread storm = new Thread(() -> {
        try { start.await(); } catch (InterruptedException ignored) {}
        for (int k = 0; k < YOUNG_GC_STORM && !stop.get(); k++) {
          GC.young_gc();
        }
      }, "young-gc-storm");
      storm.setDaemon(true);
      storm.start();

      start.countDown();

      // Create many NEW H2 -> OLD(H1) backrefs DURING CM.
      // Use humongous objects so reclaim becomes observable if they were not marked.
      int[] slots = new int[Math.min(ALLOCS_PER_CYCLE, 4096)];
      int[] sums  = new int[slots.length];

      for (int i = 0; i < slots.length; i++) {
        int slot = rnd.nextInt(H2_SIZE);
        slots[i] = slot;

        // Create humongous object DURING CM.
        byte[] a = newHumongous(HUM_MB, (cycle << 16) ^ i);

        // Make it "old": in G1 humongous is allocated directly in humongous regions (effectively old space),
        // but keep your harness call anyway for determinism in your fork.
        GC.move_to_old();
        GC.gc();

        // Create the NEW backward reference (H2 -> old/humongous).
        h2[slot] = a;

        // Drop Java strong ref.
        sums[i] = checksum(a);
        a = null;

        // A few young GCs right after creating the new edge while CM is active.
        GC.young_gc();
        GC.young_gc();
      }

      // Wait for CM to complete.
      System.out.println("=== cycle " + cycle + " wait CM ===");
      GC.wait_cm();
      stop.set(true);

      // Now force some more collections so anything unmarked gets reclaimed.
      // (If your bug caused humongous to stay unmarked, it is likely to be reclaimed here.)
      for (int i = 0; i < 50; i++) {
        GC.young_gc();
      }
      GC.gc(); // encourage mixed/full behavior depending on your harness

      // Touch through H2: if a was reclaimed despite being referenced by H2, this may crash or corrupt.
      long ok = 0;
      for (int i = 0; i < slots.length; i++) {
        Object o = h2[slots[i]];
        if (!(o instanceof byte[])) {
          System.err.println("BUG: slot no longer contains byte[] at cycle=" + cycle + " i=" + i + " o=" + o);
          System.exit(2);
        }
        int s = checksum((byte[]) o);
        if (s != sums[i]) {
          System.err.println("BUG: checksum mismatch (corruption/reclaimed) cycle=" + cycle + " i=" + i);
          System.exit(2);
        }
        ok++;
      }
      System.out.println("cycle " + cycle + ": verified " + ok + " humongous refs via H2");
    }

    System.out.println("\nDone.");
  }
}


