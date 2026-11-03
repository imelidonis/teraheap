import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.util.LinkedList;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import jdk.teraheap.TeraHeap;

// Java program to demonstrate lambda expressions 
// to implement a user defined functional interface. 

// A sample functional interface (An interface with 
// single abstract method 
interface FuncInterface 
{ 
  // An abstract function 
  void abstractFun(int x); 

  // A non-abstract (or default) function 
  default void normalFun() 
  { 
    System.out.println("Hello"); 
  } 
} 

class Extend_Lambda
{ 
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

    System.out.println("Hashcode Element = " + sum);
  }

  public static void main(String args[]) 
  { 
    // Lambda expression to implement above 
    // functional interface. This interface 
    // by default implements abstractFun() 
    FuncInterface fobj = (int x)->System.out.println(2*x); 
    LinkedList<Integer> linkedList = new LinkedList<Integer>(); 
    int num_elements = 100000;

    gc();

    fobj.abstractFun(5); 

    TeraHeap.h2TagAndMoveRoot(linkedList, 1);

    gc();

    for (int i = 0; i < num_elements; i++)
      linkedList.add(new Integer(i));

    fobj.abstractFun(1000); 

    gc();

    fobj.abstractFun(2000); 

    gc();

    linkedList = null;

    LinkedList<Integer> linkedList2 = new LinkedList<Integer>(); 
    TeraHeap.h2TagAndMoveRoot(linkedList2, 2);

    for (int i = 0; i < num_elements; i++)
      linkedList2.add(new Integer(i));

    fobj.abstractFun(4000); 

    gc();

    calcHashCode(linkedList2, num_elements);

    fobj.abstractFun(4000); 

    gc();

    calcHashCode(linkedList2, num_elements);

    gc();

    mem_info("Memory Information");
  } 
} 

