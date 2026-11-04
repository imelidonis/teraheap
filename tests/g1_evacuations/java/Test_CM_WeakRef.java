import java.lang.ref.WeakReference;
import jdk.teraheap.TeraHeap;

// TODO: verify this test works correctly and does what we
// thing it does.
public class Test_CM_WeakRef {  
  private static class A {
    public String _str;

    public A(String str) {
      _str = str;
    }

    @Override
    public String toString() {
      return getClass().getName() + "@" + Integer.toHexString(hashCode()) + " --- " + this._str;
    }
  }

  public static void main(String[] args) throws Exception {  
    A a = null;
    WeakReference<String> weak = null;

    {

      String str = new String("this is a string");  
      a = new A(str); // a --> str

      // Mark the string 
      TeraHeap.h2TagAndMoveRoot(str, 13);

      // -------------------------------
      GC.move_to_old();
      GC.gc();
      // Here: H1: { a } --> H2: { str }
      // -------------------------------

      System.out.println("After fgc:");
      System.out.println("Str: " + str + " -- (in H2: " + TeraHeap.isInH2(str) + ")");
      System.out.println("A: " + a + " -- (in H2: " + TeraHeap.isInH2(a) + ")");
      if (weak != null) {
        System.out.println("Weak: " + weak + " -- (in H2: " + TeraHeap.isInH2(weak) + ")");
        System.out.println(" L " + weak.get() + " -- (in H2: " + TeraHeap.isInH2(weak.get()) + ")");
      } else {
        System.out.println("Weak: " + weak);
      }

      System.out.println("");
      System.out.println("Create weak ref...");

      weak = new WeakReference<>(str);
      a = null;
    }

    // Here:
    // H1: { a } --> null
    // H1: { weak } --> H2: { str }

    System.out.println("Str: -");
    if (a != null) {
      System.out.println("A: " + a + " -- (in H2: " + TeraHeap.isInH2(a) + ")");
    } else {
      System.out.println("A: " + a);
    }
    if (weak != null) {
        System.out.println("Weak: " + weak + " -- (in H2: " + TeraHeap.isInH2(weak) + ")");
        System.out.println(" L " + weak.get() + " -- (in H2: " + TeraHeap.isInH2(weak.get()) + ")");
    } else {
      System.out.println("Weak: " + weak);
    }

    GC.cm_start();
    GC.wait_cm();

    // Here:
    // Propably H2 region containing str should be marked live

    System.out.println("Str: -");
    if (a != null) {
      System.out.println("A: " + a + " -- (in H2: " + TeraHeap.isInH2(a) + ")");
    } else {
      System.out.println("A: " + a);
    }
    if (weak != null) {
        System.out.println("Weak: " + weak + " -- (in H2: " + TeraHeap.isInH2(weak) + ")");
        System.out.println(" L " + weak.get() + " -- (in H2: " + TeraHeap.isInH2(weak.get()) + ")");
    } else {
      System.out.println("Weak: " + weak);
    }
    
    Integer[] iarr = { 4, 8, 15, 16, 23, 42 };

    // Mark the string 
    TeraHeap.h2TagAndMoveRoot(iarr, 13);

    // --------------------------
    GC.move_to_old();
    GC.gc();
    // iarr should be moved to H2
    // and not corrupt str
    // --------------------------


    System.out.println("Str: -");
    if (a != null) {
      System.out.println("A: " + a + " -- (in H2: " + TeraHeap.isInH2(a) + ")");
    } else {
      System.out.println("A: " + a);
    }
    if (weak != null) {
        System.out.println("Weak: " + weak + " -- (in H2: " + TeraHeap.isInH2(weak) + ")");
        System.out.println(" L " + weak.get() + " -- (in H2: " + TeraHeap.isInH2(weak.get()) + ")");
    } else {
      System.out.println("Weak: " + weak);
    }
    

  }
}
