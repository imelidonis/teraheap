import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;

public class HeapPercentage {
    static final int THREADS = 32;
    static final int CHUNK_KB = 128;
    static final double[] PHASES = {0.98, 0.0, 0.80, 0.0, 0.60, 0.40, 0.20, 0.0};

    // ---- safety headroom ----
    static double headroomFor(double frac){
        if (frac >= 0.95) return 0.12;
        if (frac >= 0.80) return 0.06;
        if (frac >= 0.60) return 0.04;
        if (frac >= 0.40) return 0.03;
        return 0.02;
    }

    // ---- Unsafe + H2 ----
    private static final sun.misc.Unsafe U;
    private static final boolean HAS_H2;
    static {
        try {
            Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            U = (sun.misc.Unsafe) f.get(null);
        } catch (Throwable t) { throw new RuntimeException(t); }
        boolean has;
        try { U.getClass().getMethod("h2TagAndMoveRoot", Object.class, int.class, int.class); has = true; }
        catch (NoSuchMethodException e) { has = false; }
        HAS_H2 = has;
    }

    static void log(String s){ System.out.println(System.currentTimeMillis()+" | "+s); }
    static void nap(long ms){ try { Thread.sleep(ms); } catch (InterruptedException ignored) {} }

    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        // args: [durationSecs]
        int durationSecs = args.length > 0 ? Integer.parseInt(args[0]) : 120;
        System.out.println("Running HeapPercentage for " + durationSecs + " seconds...");
        long startTime = System.currentTimeMillis();
        long endTime = startTime + durationSecs * 1000L;

        final long XMX = Runtime.getRuntime().maxMemory();
        final int CHUNK = CHUNK_KB * 1024;

        ExecutorService pool = Executors.newFixedThreadPool(THREADS);
        List<byte[]>[] H1 = new List[THREADS], H2 = new List[THREADS];

        int phaseNum = 1;
        while (System.currentTimeMillis() < endTime) {
            for (int i = 0; i < THREADS; i++) { H1[i] = new ArrayList<>(); H2[i] = new ArrayList<>(); }

            for (double frac : PHASES) {
                if (System.currentTimeMillis() >= endTime) break;

                if (frac <= 0.0) { // drop everything
                    clear(H1); clear(H2); gc();
                    log("PHASE 0% | used=" + used());
                    nap(500); // short pause between phases
                    phaseNum++;
                    continue;
                }

                long target = (long)(XMX * frac * (1.0 - headroomFor(frac)));
                clear(H1); clear(H2); gc();
                log(String.format("PHASE %.0f%% target≈%,d", frac * 100, target));
                allocateUpTo(pool, H1, H2, CHUNK, target);

                if (HAS_H2) {
                    try {
                        U.getClass().getMethod("h2TagAndMoveRoot", Object.class, int.class, int.class)
                         .invoke(U, H2, phaseNum, 0);
                    } catch (Throwable ignored) {}
                }
                log("AT_PEAK used=" + used());
                nap(500);
                phaseNum++;
            }
        }

        pool.shutdown();
        pool.awaitTermination(30, TimeUnit.SECONDS);
        log("END (duration=" + durationSecs + "s)");
        System.out.println("HeapPercentage finished after " + durationSecs + " seconds.");
    }

    private static void allocateUpTo(ExecutorService pool, List<byte[]>[] H1, List<byte[]>[] H2,
                                     int chunk, long bytes) throws InterruptedException {
        AtomicLong remaining = new AtomicLong(bytes);
        List<Callable<Void>> tasks = new ArrayList<>(THREADS);
        for (int t = 0; t < THREADS; t++) {
            final int tid = t;
            tasks.add(() -> {
                int allocIdx = 0;
                List<byte[]> s1 = H1[tid], s2 = H2[tid];
                while (true) {
                    long rem = remaining.get();
                    if (rem <= 0) break;
                    int sz = (int)Math.min(chunk, rem);
                    if (!remaining.compareAndSet(rem, rem - sz)) continue;
                    byte[] b = new byte[sz];
                    for (int k = 0; k < sz; k += 4096) b[k] = 1;
                    if ((allocIdx++ & 3) == 3) s2.add(b); else s1.add(b);
                }
                return null;
            });
        }
        pool.invokeAll(tasks);
    }

    private static void clear(List<byte[]>[] a){ for (var lst : a) lst.clear(); }
    private static void gc(){ System.gc(); System.runFinalization(); }
    private static long used(){ Runtime rt = Runtime.getRuntime(); return rt.totalMemory() - rt.freeMemory(); }
}

