// burn.c — spin N threads for S seconds, then report how much CPU time we got.
//
// Build: gcc -Wall -O2 -pthread -o burn burn.c
// Run:   ./burn THREADS SECONDS
//
// The report compares wall-clock time with consumed CPU time
// (getrusage), which is exactly what cpu.max limits.
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static volatile int stop = 0;

static void *spin(void *arg) {
    (void)arg;
    unsigned long x = 0;
    while (!stop) x++;
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char *argv[]) {
    int threads = argc > 1 ? atoi(argv[1]) : 1;
    int seconds = argc > 2 ? atoi(argv[2]) : 5;

    cpu_set_t set;
    sched_getaffinity(0, sizeof(set), &set);
    printf("pid=%d threads=%d online_cpus=%ld affinity_cpus=%d\n",
           getpid(), threads, sysconf(_SC_NPROCESSORS_ONLN), CPU_COUNT(&set));
    fflush(stdout);

    pthread_t *t = calloc(threads, sizeof(*t));
    double start = now();
    for (int i = 0; i < threads; i++) pthread_create(&t[i], NULL, spin, NULL);
    sleep(seconds);
    stop = 1;
    for (int i = 0; i < threads; i++) pthread_join(t[i], NULL);
    double wall = now() - start;

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
                 ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    printf("wall=%.2fs cpu=%.2fs  → average CPUs used = %.2f\n", wall, cpu, cpu / wall);
    return 0;
}
