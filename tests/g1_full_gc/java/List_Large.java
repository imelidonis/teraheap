// Test migration of an object and all the objects that points out
//
import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.util.LinkedList;
import jdk.teraheap.TeraHeap;

public class List_Large {
  public static void mem_info(String str)
  {
    System.out.println("=========================================");
    System.out.println(str + "\n");
    System.out.println("=========================================");
    for(MemoryPoolMXBean memoryPoolMXBean: ManagementFactory.getMemoryPoolMXBeans()){
      System.out.println(memoryPoolMXBean.getName());
      System.out.println(memoryPoolMXBean.getUsage().getUsed());
    }
  }

  public static void gc()
  {
    System.out.println("=========================================");
    System.out.println("Call GC");
    System.gc();
    System.out.println("=========================================");
  }

  public static void calcHashCode(LinkedList<Integer> list, int num_elements) {
    long sum = 0;

    for (int i = 0; i < num_elements; i++)
      sum += list.get(i).hashCode();

    System.out.println("Total Hashcode = " + sum);
  }

  public static void main(String[] args) {
    int num_elements = 100000;

    mem_info("Memory Before");

    LinkedList<Integer> list1 = new LinkedList<Integer>(); 
    TeraHeap.h2TagAndMoveRoot(list1, 0);

    LinkedList<Integer> list2 = new LinkedList<Integer>(); 
    TeraHeap.h2TagAndMoveRoot(list2, 1);

    for (int i = 0; i < num_elements; i++)
      list1.add(i);

    mem_info("Memory After");
    gc();

    calcHashCode(list1, num_elements);

    System.out.println("First Element = " + list1.getFirst());
    System.out.println("Last Element = " + list1.getLast());

    gc();

    list1 = null;

    mem_info("Memory After");

    gc();

    for (int i = 0; i < num_elements; i++)
      list2.add(i);

    calcHashCode(list2, num_elements);

    gc();

    calcHashCode(list2, num_elements);

    gc();

    calcHashCode(list2, num_elements);

    mem_info("Memory After");
  }
}
