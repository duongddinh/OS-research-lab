# OS-research-lab
Exploring kernel-level operating system design through simulation and machine learning. Focused on schedulers, memory management, and early experiments toward building a custom OS.

This is an **ongoing research and learning project**, where I explore fundamental OS concepts through simulation, implement new ideas, and eventually work toward building a simple, custom operating system.

---

## Focus Areas

- **CPU Scheduling**  
  Simulations of classic and modern schedulers, including:
  - Round Robin
  - CFS (Completely Fair Scheduler)
  - ML-augmented Round Robin (predicting shortest tasks and dynamic quantum)

- **Memory Management**  
  Experiments on page replacement algorithms:
  - Traditional LRU vs ML-enhanced LRU
  - FIFO, Clock, and custom strategies

- **Machine Learning in the Kernel**  
  Prototyping how lightweight ML models can assist scheduling, page replacement, and resource prediction.

---

## Why This Exists

Most OS behavior happens at the kernel level — but textbooks rarely let you tweak or test these ideas directly.

This repo is my sandbox:
- To **simulate and benchmark** ideas safely before writing C kernel code
- To explore **the boundary between rule-based and data-driven system design**
- And eventually, to build the **foundations of a minimal operating system**

