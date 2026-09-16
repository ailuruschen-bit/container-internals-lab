#!/usr/bin/env python3
"""memhog.py — allocate and TOUCH anonymous memory in steps.

Usage: python3 memhog.py STEP_MB STEPS [DELAY_SECONDS]
Example: python3 memhog.py 20 15 0.2     # up to 300 MB in 20 MB steps

Pages are written (not just reserved), so each step really increases the
cgroup's anonymous memory charge. Compare with a JVM: -Xmx only reserves
virtual memory; the heap is charged as the JVM touches it.
"""
import os
import sys
import time

step_mb = int(sys.argv[1]) if len(sys.argv) > 1 else 20
steps = int(sys.argv[2]) if len(sys.argv) > 2 else 15
delay = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2

with open("/proc/self/cgroup") as f:
    print(f"pid={os.getpid()} cgroup={f.read().strip()}", flush=True)

chunks = []
for i in range(1, steps + 1):
    t0 = time.monotonic()
    chunks.append(b"\x01" * (step_mb * 1024 * 1024))  # creating the bytes writes every page
    elapsed = time.monotonic() - t0
    print(f"allocated {i * step_mb:5d} MB  (this step took {elapsed:6.3f} s)", flush=True)
    time.sleep(delay)

print("done, holding memory for 5 seconds", flush=True)
time.sleep(5)
