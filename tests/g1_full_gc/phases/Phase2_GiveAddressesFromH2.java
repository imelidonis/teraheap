import java.lang.reflect.Field;
import java.util.ArrayList;

public class Phase2_GiveAddressesFromH2 {
  private static final sun.misc.Unsafe _UNSAFE;

  static class A {
    int x;
  }

	static {
		try {
			Field unsafeField = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
			unsafeField.setAccessible(true);
			_UNSAFE = (sun.misc.Unsafe) unsafeField.get(null);
		} catch (Exception e) {
			throw new RuntimeException("SimplePartition: Failed to " + "get unsafe", e);
		}
	}

  public static void main(String[] args) {
    // Create an ArrayList of Arrays of class A
    ArrayList<A[]> arraysOfA = new ArrayList<A[]>();
    // Create an array of 2048 objects of class A
    A[] arrayOfA = new A[2048];
    // Create an array of 2048 object of class A what will not have H2 address.
    A[] arrayOfA_nomove = new A[2048];

    // Instantiate 2048 objects of class A and assign them to the array
    for (int i = 0; i < 2048; i++) {
      arrayOfA[i] = new A();
      arrayOfA_nomove[i] = new A();
    }

    arraysOfA.add(arrayOfA);
    arraysOfA.add(arrayOfA_nomove);

    // Mark to move in H2
	  _UNSAFE.h2TagAndMoveRoot(arraysOfA.get(0), 0, 0);

    // Trigger a Full GC
    System.gc();

    // Check that the first array is marked
    if (!(_UNSAFE.dummy_has_h2_address(arraysOfA.get(0))))
      throw new RuntimeException("Object 'arraysOfA[0]' should have H2 address!");

    // Also, the ArrayList (parent) shouldn't have H2 address.
    if (_UNSAFE.dummy_has_h2_address(arraysOfA))
      throw new RuntimeException("Object 'arraysOfA' shouldn't have H2 address!");

    // Check that all objects are marked of arrayOfA are marked,
    // and objects of arrayOfA_nomove are not!
    for (int i = 0; i < 2048; i++) {
      if (!(_UNSAFE.dummy_has_h2_address(arrayOfA[i])))
        throw new RuntimeException("Object 'arrayOfA[" + i + "]' should have H2 address!");
      if (_UNSAFE.dummy_has_h2_address(arrayOfA_nomove[i]))
        throw new RuntimeException("Object 'arrayOfA_nomove[" + i + "]' shouldn't have H2 address!");
    }
  }
}

