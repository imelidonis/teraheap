import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Random;
import java.util.concurrent.*;

public class GCAndIOPhasesMixedBarrier {
    private static final sun.misc.Unsafe _UNSAFE;
    static {
        try {
            Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            _UNSAFE = (sun.misc.Unsafe) f.get(null);
        } catch (Exception e) {
            throw new RuntimeException("Failed to get Unsafe", e);
        }
    }

    static void stamp(String phase) {
        long ts = System.currentTimeMillis();
        System.out.println(ts + " | " + phase);
    }

    private static final ThreadLocal<Random> THREAD_LOCAL_RND =
        ThreadLocal.withInitial(() -> new Random(1337));

    public static void main(String[] args) throws InterruptedException {
        // args: [durationSecs]
        int durationSecs = args.length > 0 ? Integer.parseInt(args[0]) : 60;
        int n = 10_000_000;
        int numThreads = 40;

        System.out.println("Running GCAndIOPhasesMixedBarrier for " + durationSecs + " seconds...");
        long startTime = System.currentTimeMillis();
        long endTime = startTime + durationSecs * 1000L;

        // Use a synchronized map for thread-safe access
        HashMap<String, String> map = new HashMap<>(n);
        ExecutorService executor = Executors.newFixedThreadPool(numThreads);

        int cycle = 1;
        _UNSAFE.h2TagAndMoveRoot(map, cycle, 0);

        while (System.currentTimeMillis() < endTime) {
            List<Callable<Void>> tasks = new ArrayList<>();
            int chunkSize = n / numThreads;
            final CyclicBarrier barrier = new CyclicBarrier(numThreads);

            stamp("START_MIXED_PHASE cycle=" + cycle);
            System.err.println("START_MIXED_PHASE cycle=" + cycle);

            for (int threadId = 0; threadId < numThreads; threadId++) {
                final int start = threadId * chunkSize;
                final int end = (threadId == numThreads - 1) ? n : start + chunkSize;
                final int thisCycle = cycle;

                tasks.add(() -> {
                    // --- PHASE 1: Populate the map ---
                    for (int j = start; j < end; j++) {
                        synchronized (map) {
                            map.put("key_" + j, "Hello IO phase " + j);
                        }
                    }

                    // --- BARRIER ---
                    try {
                        barrier.await();
                    } catch (InterruptedException | BrokenBarrierException e) {
                        Thread.currentThread().interrupt();
                        throw new RuntimeException("Barrier was interrupted", e);
                    }

                    // --- PHASE 2: Access the map ---
                    long sum = 0;
                    for (int j = start; j < end; j++) {
                        String key = "key_" + j;
                        String value;
                        synchronized (map) {
                            value = map.get(key);
                        }
                        if (value != null) {
                            sum += key.hashCode() + value.hashCode();
                        }
                    }
                    return null;
                });
            }

            try {
                executor.invokeAll(tasks);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException("Executor was interrupted", e);
            }

            cycle++;
        }

        executor.shutdown();
        executor.awaitTermination(60, TimeUnit.SECONDS);
        stamp("TEST_END total_cycles=" + (cycle - 1));
        System.out.println("GCAndIOPhasesMixedBarrier finished after " + durationSecs + " seconds.");
    }
}

