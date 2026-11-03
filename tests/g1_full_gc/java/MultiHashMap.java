import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.util.concurrent.ConcurrentHashMap;
import java.util.ArrayList;
import java.util.HashMap;
import jdk.teraheap.TeraHeap;


public class MultiHashMap {
  static long num = 0;


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



  public static void main(String[] args) {
    int num_elements = 10000;
    int sub_elem = 5000;

    HashMap<String, ArrayList<Integer>> h_map = new HashMap<String, ArrayList<Integer>>();
    TeraHeap.h2TagAndMoveRoot(h_map, 0);

    for (int i = 0; i < num_elements/2; i++)
    {
      String str = new String("Iacovos Kolokasis " + i);
      TeraHeap.h2TagAndMoveRoot(str, 1);

      ArrayList<Integer> array = new ArrayList<Integer>();
      for (int j = 0; j < sub_elem; j++) {
        Integer number = new Integer(j * 10);
        array.add(number);
      }

      h_map.put(str, array);

    }

    gc();

    h_map.entrySet().forEach(entry -> {
      num += entry.getKey().hashCode();
      ArrayList<Integer> arr = entry.getValue();

      // gc();
      for (int i = 0; i < sub_elem; i++)
      {
        num += arr.get(i).hashCode();
      }
    });
  }
}
