import java.lang.reflect.Field;
import java.util.ArrayList;

public class Phase1_MarkSubObject {
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
    // Create an array of 10 objects of class A
    A[] arrayOfA = new A[10];
    // Create an array of 10 object of class A what will not be marked.
    A[] arrayOfA_nomove = new A[10];

    // Instantiate 10 objects of class A and assign them to the array
    for (int i = 0; i < 10; i++) {
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
    if (!(_UNSAFE.is_marked_move_h2(arraysOfA.get(0))))
      throw new RuntimeException("Object 'arraysOfA[0]' should be marked!");

    // Also, the ArrayList (parent) shouldn't be marked.
    if (_UNSAFE.is_marked_move_h2(arraysOfA))
      throw new RuntimeException("Object 'arraysOfA' shouldn't be marked!");

    // Check that all objects are marked of arrayOfA are marked,
    // and objects of arrayOfA_nomove are not!
    for (int i = 0; i < 10; i++) {
      if (!(_UNSAFE.is_marked_move_h2(arrayOfA[i])))
        throw new RuntimeException("Object 'arrayOfA[" + i + "]' should be marked!");
      if (_UNSAFE.is_marked_move_h2(arrayOfA_nomove[i]))
        throw new RuntimeException("Object 'arrayOfA_nomove[" + i + "]' shouldn't be marked!");
    }
  }
}

