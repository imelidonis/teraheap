import java.lang.ref.Reference;
import java.lang.ref.SoftReference;
import java.lang.ref.WeakReference;
import jdk.teraheap.TeraHeap;


public class Test_Reference {
  public static class Bean {
    String name;
    Integer va;

    public Bean(String name, Integer value) {
      this.name = name;
      this.va = value;
    }

    @Override
    public String toString() {
      return name +": " + va;
    }

  }

  public static void gc()
  {
    System.out.println("=========================================");
    System.out.println("Call GC");
    System.gc();
    System.out.println("=========================================");
  }


  public static void main(String[] args) {
    Bean bean = new Bean("songli", 67);

    Bean[] referent = new Bean[20000]; 
    TeraHeap.h2TagAndMoveRoot(referent, 1);

    for (int i=0; i < referent.length; i++)
      referent[i] = new Bean("mybean:" + i, 100);// Throw Exception 

    System.out.println(referent[1000].toString());

    gc();

    Reference<Bean>[] soft_referent = new SoftReference[20000]; 
    for (int i=0; i < soft_referent.length; i++)
      soft_referent[i] = new SoftReference<Bean>(new Bean("mybean:" + i, 100)); 

    WeakReference<Bean>[] weakReference = new WeakReference[2000];
    TeraHeap.h2TagAndMoveRoot(weakReference, 0);

    gc();
  }
}
