import java.util.ArrayList;
import java.lang.Math;
import jdk.teraheap.TeraHeap;


public class TriggerImplicitGCs {
  static Integer SmallNum = 1000;
  static Integer LargeNum = 100000;

  // Pairs of Outs
  public static class Pair {
    public final Integer x;
    public final Integer y;

    public Pair(int x, int y) {
      this.x = x;
      this.y = y;
    }
  }

  static volatile ArrayList<Pair> outs = new ArrayList<>();
  // ------------------

  // Large Humongous Array of Points
  public static class Point {
    Integer x;
    Integer y;
    Integer total;
    Point[] others;

    // Used for pointing to humongous objects to consume more space.
    public Object a_field_without_usage = null;

    public Point(int x, int y) {
      this.x = x;
      this.y = y;
      
      this.total = 0;

      others = new Point[SmallNum];

      TeraHeap.h2TagAndMoveRoot(others, 0);
    }

    public void add(Point p) {
      others[total++] = p;
    }

    public void reset_others() {
      this.others = new Point[SmallNum];
      this.total = 0;

      TeraHeap.h2TagAndMoveRoot(this.others, 0);
    }

    // Calculate new points from this and others and draw.
    public void draw_inspired() {
      ArrayList<Point> line = new ArrayList<>();
      for (int i = 0; i < total; i++) {
        Point p = others[i];

        Point inspired = new Point(this.x * p.x, this.y - p.y);

        line.add(inspired);
      }

      // Draw: add x and y coordinates to 'out' and print the value
      // (just to access data)

      Integer outx = 0;
      Integer outy = 0;

      for (Point p : line) {
        outx += p.x;
        outy += p.y;

        if (p.a_field_without_usage != null) {
          Integer move_by = (Integer) ((Object[]) ((Object[]) p.a_field_without_usage)[0])[0];
          outx -= move_by;
        }
      }

      // Instead of actualy printing them
      if (!line.isEmpty())
        outs.add(new Pair(outx, outy));
    }
  }

  public static volatile Point[] points = new Point[LargeNum];

  public static class Threaded extends Thread {
    Integer mode = 0;

    public Threaded(int mode) {
      this.mode = mode;
    }
    
    @Override
    public void run() {
      switch (mode) {
        case 0:
          mode_0();
          break;

        case 1:
          try {
            mode_1();
          } catch (Exception e) {}
          break;

        default:
          break;
      }
    }

    // Allocates many objects
    private void mode_0() {
      System.out.println("Mode 0");

      // ----
      // Avoid calling this mode by multiple threads
      // because it modifies global state without
      // using synchronization.
      // ----

      Integer reduce_by = (int) (Math.random() * 20);

      // Run for every 100 points to add more randomness
      // and have different cases.
      for (int i = 0; i < LargeNum; i+=100) {
        points[i].reset_others();
        for (int j = 0; j < SmallNum - reduce_by; j++) {

          // -----
          // Allocate and add new points (more allocations)
          // and for some of them create humongous arrays and add them
          // to the objects. Also mark the humongous arrays to transfer
          // them to H2
          // -----

          Point new_p = new Point(i, j);

          if ((j % 200) == 0 && (i % 1000) == 0) {
            Object[] hum_array = new Object[LargeNum];
            new_p.a_field_without_usage = (Object) hum_array;

            hum_array = new Object[LargeNum];

            hum_array[0] = j;

            ((Object[]) new_p.a_field_without_usage)[0] = (Object) hum_array;

            TeraHeap.h2TagAndMoveRoot(new_p.a_field_without_usage, j % 5);
          }

          points[i].add(new_p);
        }
      }

      for (Point p : points) {
        // Allocates more objects and accesses objects
        // doint calculations
        p.draw_inspired();
      }
    }

    // Do calculations
    private void mode_1() throws InterruptedException  {
      System.out.println("Mode 1");

      // ----
      // In every execution, it just allocates new objects
      // and do some calulations by accessing the points.
      // It is safe to call it multiple times as long as
      // 'points' is not modified by another thread.
      // (x and y coordiantes).
      // ----

      // Find Projections
      Point[] projections = new Point[LargeNum];
      
      for (int i = 0; i < LargeNum; i++) {
        projections[i] = new Point(points[i].x, 0);

        if (i % 1000 == 0) {
          // Add a sleep timer to allow more
          // CM to finish.
          Thread.sleep(100);
        }
      }

      // Sum x coordinates
      Integer sum_x = 0;

      for (Point p : projections) {
        sum_x += p.x;
      }

      System.out.println("Sum of x coordinates: " + sum_x);

      for (int i = 0; i < LargeNum; i++) {
        projections[i] = new Point(0, points[i].y);

        if (i % 1000 == 0) {
          // Add a sleep timer to allow more
          // CM to finish.
          Thread.sleep(100);
        }
      }

      // Sum y coordinates
      Integer sum_y = 0;

      for (Point p : projections) {
        sum_y += p.y;
      }

      System.out.println("Sum of y coordinates: " + sum_y);
    }
  }

  public static void main(String[] args) throws Exception {

    // Make ArrayList consume space
    outs.ensureCapacity(LargeNum * 2);

    // Initialize Points
    for (int i = 0; i < LargeNum; i++) {
      points[i] = new Point(
        (int) (Math.random() * 20) - 10,
        (int) (Math.random() * 20) - 10
      );
    }

    // Random Humongous object
    Object[] just_a_humongous_object_without_any_usage = new Object[LargeNum];
    Object[] temp = just_a_humongous_object_without_any_usage;

    // Run threads multiple times.
    for (int i = 0; i < 5; i++) {
      System.out.println("Start threads");

      // Just to make Heap presure
      if (i < 3) {
        temp[LargeNum - 2] = i;
        temp[LargeNum - 1] = new Object[LargeNum];
        temp = (Object[]) temp[LargeNum - 1];
        TeraHeap.h2TagAndMoveRoot(temp, 0);
      }

      Integer times_to_call_mode1 = 7;

      Threaded m0 = new Threaded(0);
      Threaded[] m1 = new Threaded[times_to_call_mode1];

      for (int j = 0; j < times_to_call_mode1; j++) {
        m1[j] = new Threaded(1);
      }

      m0.start();
      for (int j = 0; j < times_to_call_mode1; j++) {
        m1[j].start();
      }

      System.out.println("Wait to finish");

      m0.join();
      for (int j = 0; j < times_to_call_mode1; j++) {
        m1[j].join();
      }

      System.out.println("Next iter");
    }

    // Find the point of everything
    // (Random point that has the sum of all coordinates with a random direction)
    Integer sumx = 0;
    Integer sumy = 0;

    for (Pair pr : outs) {
      Integer rand_direction = (int) (Math.random() * 2) - 1;

      sumx += rand_direction * pr.x;
      sumy += rand_direction * pr.y;
    }

    System.out.println("The Point of EVERYTHING is P {" + sumx + "," + sumy + "}");

    // Just a check
    temp = just_a_humongous_object_without_any_usage;
    while (temp != null) {
      System.out.println((Integer) temp[LargeNum - 2]);
      temp = (Object[]) temp[LargeNum - 1];
    }
  }
}
