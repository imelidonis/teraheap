// Test migration of an object and all the objects that points out
//
import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.util.LinkedList;
import jdk.teraheap.TeraHeap;


public class MultiList {
  public static void mem_info(String str) {
    System.out.println("=========================================");
    System.out.println(str + "\n");
    System.out.println("=========================================");
    for(MemoryPoolMXBean memoryPoolMXBean: ManagementFactory.getMemoryPoolMXBeans()){
      System.out.println(memoryPoolMXBean.getName());
      System.out.println(memoryPoolMXBean.getUsage().getUsed());
    }
  }


  public static void calcHashCodeInteger(LinkedList<Integer> list, int num_elements) {
    long sum = 0;

    for (int i = 0; i < num_elements; i++)
      sum += list.get(i).hashCode();

    System.out.println("Hashcode Element = " + sum);
  }

  public static void calcHashCodeString(LinkedList<String> list, int num_elements) {
    long sum = 0;

    for (int i = 0; i < num_elements; i++)
      sum += list.get(i).hashCode();

    System.out.println("Hashcode Element = " + sum);
  }

  public static void main(String[] args) {
    int num_elements = 100000;

    mem_info("Memory Before");

    LinkedList<Integer> list = new LinkedList<Integer>(); 
    LinkedList<String> list2 = new LinkedList<String>(); 
    LinkedList<Integer> list3 = new LinkedList<Integer>(); 

    TeraHeap.h2TagAndMoveRoot(list, 0);
    TeraHeap.h2TagAndMoveRoot(list2, 1);
    TeraHeap.h2TagAndMoveRoot(list3, 2);

    for (int i = 0; i < num_elements; i++)
      list.add(i);

    GC.move_to_old();
    GC.gc();

    System.out.println("First Element = " + list.getFirst());
    System.out.println("Last Element = " + list.getLast());

    for (int i = 0; i < num_elements; i++)
      list2.add(new String("Hello TeraHeap " + i));

    GC.move_to_old();

    calcHashCodeString(list2, num_elements);
    calcHashCodeInteger(list, num_elements);

    System.out.println("First Element = " + list2.getFirst());
    System.out.println("Last Element = " + list2.getLast());

    list2 = null;

    GC.gc();

    for (int i = 0; i < num_elements; i++)
      list3.add(i);

    GC.move_to_old();
    GC.gc();

    System.out.println("First Element = " + list3.getFirst());
    System.out.println("Last Element = " + list3.getLast());

    calcHashCodeInteger(list, num_elements);
    calcHashCodeInteger(list3, num_elements);

    GC.gc();

    mem_info("Memory After");
  }
}
