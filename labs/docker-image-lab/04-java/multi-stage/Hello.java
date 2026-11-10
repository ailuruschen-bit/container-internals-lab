// Prints a greeting and the Java runtime version inside the container.
public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello from Java Docker Container!");
        System.out.println("Java version: " + System.getProperty("java.version"));
    }
}
