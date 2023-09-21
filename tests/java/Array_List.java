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

import java.lang.reflect.Field;

public class Array_List {
	private static final sun.misc.Unsafe _UNSAFE;

	static {
		try {
			Field unsafeField = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
			unsafeField.setAccessible(true);
			_UNSAFE = (sun.misc.Unsafe) unsafeField.get(null);
		} catch (Exception e) {
			throw new RuntimeException("SimplePartition: Failed to " + "get unsafe", e);
		}
	}

	public static void mem_info(String str) {
		System.out.println("=========================================");
		System.out.println(str + "\n");
		System.out.println("=========================================");
		for(MemoryPoolMXBean memoryPoolMXBean: ManagementFactory.getMemoryPoolMXBeans()){
			System.out.println(memoryPoolMXBean.getName());
			System.out.println(memoryPoolMXBean.getUsage().getUsed());
		}
	}

	public static void gc() {
		System.out.println("=========================================");
		System.out.println("Call GC");
		System.gc();
		System.out.println("=========================================");
	}

	public static void calcHashCode(ArrayList<String> arl, int num_elements) {
		long sum = 0;

		for (int i = 0; i < num_elements; i++)
			sum += arl.get(i).hashCode();

		System.out.println("Hashcode Element = " + sum);
	}

	public static void main (String[] args)
	{
		int num_elements =1000000;
		long sum = 0;

		mem_info("Memory Before");

		// Create the array list
		ArrayList<String> arl = new ArrayList<String>();
		_UNSAFE.h2TagAndMoveRoot(arl, 0, 0);

		for (int i = 0; i < num_elements; i++)
			arl.add(new String("Hello World for the first time " + i));
		
    ArrayList<String> arl2 = new ArrayList<String>();
		for (int i = 0; i < num_elements; i++)
			arl2.add(new String("Hello World " + i));

		_UNSAFE.h2TagAndMoveRoot(arl2, 0, 0);

		calcHashCode(arl, num_elements);
		calcHashCode(arl2, num_elements);
		gc();

		calcHashCode(arl, num_elements);

    gc();
		calcHashCode(arl, num_elements);

		gc();
		calcHashCode(arl, num_elements);

		gc();

		for (int i = 0; i < num_elements; i++)
			arl.add(new String("Hello World its me giannos " + i));

		gc();

		calcHashCode(arl, num_elements);

    gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 

	gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 

	gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 

	gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 

	gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 

	gc();

    calcHashCode(arl2, num_elements);

		mem_info("Memory After"); 
	}
}

