import java.util.ArrayList;
import java.util.List;
import jdk.teraheap.TeraHeap;

public class Humongous {
  private static volatile A a = new A(0);

  static class A {
    Integer i;

    public A(int _i) {
      this.i = _i;
    }
  }

  // This Object has a reference to humongous object (byte Array)
  static class RefToHumongousObject {
    int size;
    byte[] data;
    List<RefToHumongousObject> list;

    public RefToHumongousObject(int size_in_mb) {
      this.size = size_in_mb * 1024 * 1024;
      this.data = new byte[size];

      for (int i = 0; i < this.size; i++) {
        this.data[i] = (byte) (i % 127);
      }

      this.list = new ArrayList<>();
    }

    public void add_object(RefToHumongousObject obj) {
      this.list.add(obj);
    }
    
    public byte access() {
      byte res = 0;
      for (int i = 0; i < this.size; i++) {
        res |= this.data[i];
      }
      return res;
    }
  }


  public static void gc()
  {
    System.out.println("=========================================");
    System.out.println("Call GC");
    System.gc();
    System.out.println("=========================================");
  }

  // Allocate objects to trigger a collection
  public static void add_noise() {
    List<A> list = new ArrayList<>();
    for (int i = 0; i < 10000000; i++) {
      a = new A(i);

      if (a.i > 10 && a.i * i < 100000) {
        a = new A(i - 1);
      }

      list.add(a);
    }

    System.out.println("Noise A {" + a.i + "}");
  }

  public static void access_all_objects(RefToHumongousObject root) {
    System.out.println("Access Objects ----");
    System.out.println(root.access());

    for (RefToHumongousObject obj : root.list) {
      System.out.println(obj.access());

      for (RefToHumongousObject inner : obj.list) {
        System.out.println(inner.access());
      }

      add_noise();
    }

    System.out.println("-------------------");
  }

  public static void main(String[] args) {
    // Largest region size is 32 MB => any object with size >= 16 MB
    // is humongous.
    int size_in_mb = 16;

    RefToHumongousObject root = new RefToHumongousObject(size_in_mb);

    gc();

    // Allocate some humongous children with some noise to
    // potentially trigger a gc.
    for (int i = 0; i < 5; i++) {
      RefToHumongousObject child = new RefToHumongousObject(size_in_mb * 2);
      TeraHeap.h2TagAndMoveRoot(child, 0);

      // noise
      if (i % 2 == 0) {
        add_noise();
      }

      root.add_object(child);
    }

    // Add some other objects to the childs
    RefToHumongousObject hobj = new RefToHumongousObject(size_in_mb);

    root.list.get(2).add_object(hobj);

    hobj = new RefToHumongousObject(size_in_mb);
    root.list.get(3).add_object(hobj);


    // Access Objects
    access_all_objects(root);
    // -------------

    gc();

    // Create a Backward
    hobj = new RefToHumongousObject(size_in_mb + 10);

    root.list.get(3).add_object(hobj);

    // Access Objects
    access_all_objects(root);
    // -------------
    
    gc();

    // Access Objects
    access_all_objects(root);
    // -------------
  }
}
