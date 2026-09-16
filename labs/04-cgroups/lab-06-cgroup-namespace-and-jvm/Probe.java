// Probe.java — what does the JVM believe about its resources, and how does it fail?
//
// Run (JDK 11+ can run a single source file directly):
//   java Probe.java                 print what the JVM derived from the cgroup
//   java Probe.java heap            allocate heap until OutOfMemoryError
//   java Probe.java direct          allocate and touch off-heap memory until the kernel intervenes
import java.lang.management.ManagementFactory;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class Probe {
    public static void main(String[] args) throws Exception {
        Runtime rt = Runtime.getRuntime();
        System.out.printf("pid                  : %d%n", ProcessHandle.current().pid());
        System.out.printf("cgroup                : %s%n", Files.readString(Path.of("/proc/self/cgroup")).trim());
        System.out.printf("availableProcessors() : %d%n", rt.availableProcessors());
        System.out.printf("maxMemory() (heap max): %d MiB%n", rt.maxMemory() / (1024 * 1024));
        System.out.printf("GC                    : %s%n",
                ManagementFactory.getGarbageCollectorMXBeans().get(0).getName());
        System.out.printf("common pool parallelism: %d%n",
                java.util.concurrent.ForkJoinPool.commonPool().getParallelism());

        String mode = args.length > 0 ? args[0] : "info";
        Runtime.getRuntime().addShutdownHook(new Thread(() ->
                System.out.println("shutdown hook ran (the JVM exited normally or on SIGTERM)")));

        List<Object> hold = new ArrayList<>();
        int mib = 0;
        switch (mode) {
            case "heap" -> {
                try {
                    while (true) {
                        hold.add(new byte[16 * 1024 * 1024]);   // Java arrays are zeroed: pages are touched
                        mib += 16;
                        System.out.printf("heap allocated %d MiB%n", mib);
                        Thread.sleep(50);
                    }
                } catch (OutOfMemoryError e) {
                    hold.clear();
                    System.out.println("caught " + e);
                }
            }
            case "direct" -> {
                while (true) {
                    ByteBuffer b = ByteBuffer.allocateDirect(16 * 1024 * 1024);
                    for (int i = 0; i < b.capacity(); i += 4096) b.put(i, (byte) 1); // touch every page
                    hold.add(b);
                    mib += 16;
                    System.out.printf("direct allocated %d MiB%n", mib);
                    Thread.sleep(50);
                }
            }
            default -> { }
        }
    }
}
