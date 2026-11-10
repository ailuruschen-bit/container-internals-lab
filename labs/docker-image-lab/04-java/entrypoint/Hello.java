// Prints each argument it receives, which makes it easy to see
// how ENTRYPOINT, CMD, and `docker run` arguments are combined.
public class Hello {
    public static void main(String[] args) {
        System.out.println("Java application started!");

        for (String arg : args) {
            System.out.println("Argument: " + arg);
        }
    }
}
