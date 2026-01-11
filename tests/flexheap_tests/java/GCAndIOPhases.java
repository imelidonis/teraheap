import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.*;

public class GCAndIOPhases {
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
        int n = 10_000_000;       // number of elements
        int churnMB = 512;
        int blockKB = 256;
        int gcPhaseLen = 130;
        int ioPhaseLen = 2;
        int numThreads = 40;

        System.out.println("Running GCAndIOPhases for " + durationSecs + " seconds...");
        long startTime = System.currentTimeMillis();
        long endTime = startTime + durationSecs * 1000L;

        HashMap<String, String> map = new HashMap<>(n);
        ExecutorService executor = Executors.newFixedThreadPool(numThreads);

        int cycle = 1;
        _UNSAFE.h2TagAndMoveRoot(map, cycle, 0);

        while (System.currentTimeMillis() < endTime) {
            // === GC-HEAVY PHASE ===
            stamp("START_GC_HEAVY_PHASE cycle=" + cycle);
            System.err.println("START_GC_HEAVY_PHASE cycle=" + cycle);

            List<int[]> largeArrays = new ArrayList<>();
            for (int i = 0; i < gcPhaseLen && System.currentTimeMillis() < endTime; i++, cycle++) {
                stamp("GC_ONLY cycle=" + cycle);
                int[] heapArray = new int[n];
                largeArrays.add(heapArray);
                for (int j = 0; j < n; j++) {
                    heapArray[j] = j;
                }
                if (largeArrays.size() > 10) {
                    for (int[] arr : largeArrays) arr[0]++;
                }
            }
            largeArrays.clear();

            // === IO-HEAVY PHASE ===
            stamp("START_IO_HEAVY_PHASE cycle=" + cycle);
            System.err.println("START_IO_HEAVY_PHASE cycle=" + cycle);

            for (int i = 0; i < ioPhaseLen && System.currentTimeMillis() < endTime; i++, cycle++) {
                stamp("IO cycle=" + cycle);

                List<Callable<Void>> tasks = new ArrayList<>();
                int chunkSize = n / numThreads;

                for (int threadId = 0; threadId < numThreads; threadId++) {
                    final int start = threadId * chunkSize;
                    final int end = (threadId == numThreads - 1) ? n : start + chunkSize;

                    tasks.add(() -> {
                        for (int j = start; j < end; j++) {
                            synchronized (map) {
                                map.put("key_" + j, "Hello IO phase " + j);
                            }
                        }
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

                System.out.println("Moved to H2");
            }
        }

        executor.shutdown();
        executor.awaitTermination(60, TimeUnit.SECONDS);
        stamp("TEST_END total_cycles=" + cycle);
        System.out.println("GCAndIOPhases finished after " + durationSecs + " seconds.");
    }
}

