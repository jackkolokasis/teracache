import java.util.ArrayList;

public class Array_List_IntArrays {
    public static void main(String[] args) {
        // Create an ArrayList to store arrays of ints
        ArrayList<int[]> arrayListOfArrays = new ArrayList<>();

        // Create some example arrays of ints and add them to the ArrayList
        int[] array1 = {1, 2, 3};
        int[] array2 = {4, 5, 6, 7};
        int[] array3 = {8, 9};

        arrayListOfArrays.add(array1);
        arrayListOfArrays.add(array2);
        arrayListOfArrays.add(array3);

        // Access and print the arrays stored in the ArrayList
        for (int[] array : arrayListOfArrays) {
            for (int num : array) {
                System.out.print(num + " ");
            }
            System.out.println(); // New line after each array
        }
    }
}
