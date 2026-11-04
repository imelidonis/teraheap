import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.util.ArrayList;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import jdk.teraheap.TeraHeap;

public class Clone 
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

  @SuppressWarnings("unchecked")
  public static void main(String[] args) 
  {
    int num_elements = 4000000;

    System.out.println(Runtime.getRuntime().maxMemory());

    ArrayList<String> arrayListObject = new ArrayList<>(); 
    TeraHeap.h2TagAndMoveRoot(arrayListObject, 0);


    for (int i = 0; i < num_elements/2; i++)
    {
      arrayListObject.add("Jack Kolokasis " + i);
    }

    GC.move_to_old();
    GC.gc();

    for (int i = 0; i < num_elements/2; i++)
    {
      arrayListObject.add("Jack Kolokasis " + i);
    }

    GC.move_to_old();
    GC.gc();

    System.out.println(arrayListObject);   

    for (int i = 0; i < num_elements/4; i++)
    {
      arrayListObject.add("Nicos Kolokasis " + i);
    }

    GC.move_to_old();
    GC.gc();

    ArrayList<String> arrayListClone =  (ArrayList<String>) arrayListObject.clone();

    GC.move_to_old();
    GC.gc();

    System.out.println(arrayListClone);   

    for (int i = 0; i < num_elements/4; i++)
    {
      arrayListObject.add("Jack Kolokasis " + i + "00");
    }

    GC.move_to_old();
    GC.gc();

  }
}
