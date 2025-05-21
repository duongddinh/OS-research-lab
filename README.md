# OS-research-lab
Exploring kernel-level operating system design through simulation and machine learning. Focused on schedulers, memory management, and early experiments toward building a custom OS.

This is an **ongoing research and learning project**, where I explore fundamental OS concepts through simulation, implement new ideas, and eventually work toward building a simple, custom operating system.

---

## Focus Areas

### CPU Scheduling
Simulations of traditional and modern CPU scheduling strategies, including:
- Round-Robin
- CFS (Linux’s Completely Fair Scheduler)
- ML-augmented RR (shortest-job prediction and dynamic quantum adjustment)

See: [`/cpu/`](./cpu)

---

### Memory Management
Page replacement experiments using various eviction policies:
- Classic LRU, FIFO, Random
- Machine learning–enhanced LRU

See: [`/memory/`](./memory)

---

### Lock
Normal Spinlock vs Predictive spinlock:
- Normal Spinlock
- "AI" spinlock

See: [`/locks/`](./locks)

---

### ML in the Kernel
Early-stage research on using lightweight ML models to assist OS behavior:
- Predict page reuse
- Predict job burst times
- Dynamically adjust scheduler behavior

This layer sits between static OS heuristics and adaptive, data-driven decision making.

---

### jordyOS (WIP)
A hobbyist operating system built from scratch on macOS, featuring:
- A custom bootloader
- A simple VGA text interface
- A built-in calculator shell

See: [`/jordyOS/`](./jordyOS)

---

## Why This Exists

Most OS behavior lives deep in the kernel — but textbooks often leave no room to experiment.  
This repo is my hands-on lab for:

- Simulating and testing OS concepts before writing bare-metal C/ASM
- Exploring the boundary between **traditional kernel logic** and **machine learning**
- Gradually building toward a minimal, bootable operating system

---

## Directory Structure

```bash
cpu/         # Scheduling simulations and ML-enhanced RR
memory/      # Page replacement experiments (LRU, ML, etc.)
locks/       # Lock experiements (Spinlock and predictive spinlock)
jordyOS/     # Bootable toy operating system with calculator shell
docs/        # Github pages 
