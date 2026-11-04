// Java program to illustrate creating an array 
// of integers, puts some values in the array, 
// and prints each value to standard output. 

import java.io.*; 
import java.lang.*; 
import java.util.Scanner;
import java.lang.management.ManagementFactory;
import java.lang.management.MemoryPoolMXBean;
import java.util.LinkedList;
import java.util.ArrayList;
import jdk.teraheap.TeraHeap;


public class Array_List_String { 
  public static void mem_info(String str) {
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

  public static void calcHashCode(ArrayList<Integer> arl, int num_elements) {
    long sum = 0;

    for (int i = 0; i < num_elements; i++)
      sum += arl.get(i).hashCode();

    System.out.println("Hashcode Element = " + sum);
  }

  public static void main (String[] args) 
  {		 
    int num_elements =10000000;
    long sum = 0;

    mem_info("Memory Before");

    ArrayList<String> arl = new ArrayList<String>();
    TeraHeap.h2TagAndMoveRoot(arl, 0);


    for (int i = 0; i < num_elements/2; i++){
      arl.add("Bangalore" + i);
    }		

    gc();


    for (int i = 0; i < num_elements/2; i++){
      arl.add("Ammochostos " + i);
    }		

    gc();

    for (int i = num_elements/2; i < num_elements; i++){
      arl.add("Apostolos Andreas" + i);
    }

    gc();

    for (int i = num_elements/2; i < num_elements; i++){
      arl.add("Moires" + i); 
    }

    gc();

    for (int i = num_elements/2; i < num_elements; i++)
      arl.add("Paphos" + i);		

    gc();

    for (int i = num_elements/2; i < num_elements; i++){
      arl.add("Karpasia" + i);
    }
    arl.add("Mia milia");

    gc();

    System.out.println(arl);
  } 
}
