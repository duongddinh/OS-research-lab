Every programmer, at some point, dreams of building their own operating system. As I read and learn more about operating system, I decided that the best way to learn is to actually do it, and the result is jordyOS, a minimal, 32-bit protected-mode operating system written from scratch in C and x86 Assembly.

It works. I’m still learning, and this is a documentation of what I’ve learned and what I know.

The Big Picture: System Architecture
Let’s look at the high-level flow. Everything starts with the PC’s BIOS, which hands control to our custom bootloader. The bootloader then prepares the system, loads our kernel into memory, and switches the CPU from 16-bit real mode to 32-bit protected mode. From there, the C kernel takes over, sets up multitasking, and launches the first process: the shell.

1. bootloader.asm - The First Step
The journey begins here. The BIOS loads this single 512-byte sector into memory at 0x7C00 and executes it. Its job is simple but critical.

Mode: 16-bit Real Mode.
Purpose: Load the kernel from the disk into memory.
Mechanism: It uses the BIOS interrupt int 0x13, the classic way to read from a floppy disk. It reads a specific number of sectors (which we'll see is calculated by the Makefile) and places them in memory right after the bootloader itself, starting at address 0x0800.
The Handoff: Once the kernel is loaded, the bootloader performs a far jump to jmp 0000h:0800h, transferring control to the kernel's entry point.
What is Real Mode and why do we start there?

Real Mode is the initial operating state of an x86 CPU when it powers on.
It is called “Real Mode” because it uses a memory addressing scheme that directly corresponds to real, physical memory addresses.
In this mode, a memory location is calculated using a segment:offset pair. The physical address is calculated as (segment * 16) + offset. This calculation gives direct access to the computer's physical RAM without any form of memory protection or virtualization. It's "real" because the program "sees" the actual layout of the physical hardware memory.
This is in contrast to Protected Mode, where segment selectors point to descriptors in a table, and memory access is virtualized, abstracted, and protected by the CPU hardware.
The 16-bit nature of Real Mode is a legacy feature for backward compatibility with the original IBM PC and its Intel 8088/8086 processors.
The Intel 8086 was a 16-bit processor, meaning its internal registers were 16 bits wide. The entire software ecosystem of the early 1980s, including MS-DOS, was built for this 16-bit architecture.
When Intel created the 32-bit 80386 processor, they needed it to run all the existing 16-bit software. To achieve this, they designed the 32-bit CPU to power on in a state that perfectly mimicked the original 16-bit 8086. This state is Real Mode.
To this day, every modern x86-64 CPU starts up in this 16-bit Real Mode to maintain this unbroken chain of backward compatibility. It is the responsibility of the bootloader or the operating system to switch the CPU into a more modern and powerful state, such as 32-bit Protected Mode or 64-bit Long Mode.
Why is the bootloader 512 bytes? Can it be bigger?
The bootloader itself is exactly 512 bytes because that is the standard size of a boot sector that the BIOS is designed to handle. The BIOS loads this single 512-byte sector from the storage device into memory at address 0x7C00.
To be recognized by the BIOS as a valid, bootable sector, it must end with the magic number 0xAA55.
The bootloader code itself cannot be bigger than 512 bytes. However, its main purpose is to load the rest of the kernel, which can be much larger.
Because 512 bytes is extremely small, this first stage usually has only one job: to load a second-stage bootloader. This second stage can be much larger (many kilobytes) and contains more complex logic, such as the ability to parse file systems, display a boot menu, or prepare the hardware for the operating system kernel.
So, while the initial piece the BIOS loads is fixed at 512 bytes, the overall bootloading process can involve many more kilobytes of code.
2. kernel_entry.asm - Entering the 32-bit World
Our kernel is now in memory, but the CPU is still in 16-bit real mode. This small assembly file handles the crucial transition to 32-bit protected mode, where we can access all our memory:

Disable Interrupts (cli): We don't want any interruptions during the mode switch.
Load the GDT (lgdt): The Global Descriptor Table (GDT) is fundamental to protected mode. It defines the memory segments the CPU can use. I define a simple GDT with a 4GB code segment and a 4GB data segment.
Set the PE Bit: I flip the first bit of the CR0 control register. This is the switch that officially enables protected mode.
Far Jump: I execute a jmp to my 32-bit code segment. This flushes the CPU's execution pipeline and makes it start thinking in 32 bits.
Call kmain: Once fully in 32-bit mode with the stack set up, it's time to hand control over to our C code by calling the kmain function.
What is Protected Mode
Protected mode is an operational state of the CPU that provides features essential for a modern multitasking operating system.

Enabling the Mode: It is enabled by setting the Protection Enable (PE) bit in the CPU’s CR0 control register and then performing a far jump to a 32-bit code segment.
Memory Management: It uses a structure called the Global Descriptor Table (GDT) to define memory segments. In jordyOS, this GDT defines a 32-bit code segment and a 32-bit data segment, allowing for the 4GB flat memory model.
Memory Protection: The current implementation of jordyOS does not use features like user/kernel separation. However, the mode itself is what provides the underlying hardware support for memory protection, which would prevent one task from crashing another or the kernel itself.
Why an OS Jumps to 32-bit Protected Mode
An operating system switches from 16-bit real mode to 32-bit protected mode to overcome the limitations of real mode. The primary reasons are memory access and protection.

Limitations of 16-bit Real Mode:

1MB Memory Limit: Real mode can only address a maximum of 1 megabyte of RAM.
No Memory Protection: This is the most critical flaw. In real mode, any program can write to any part of physical memory. Which is bad!
No Hardware-Assisted Multitasking: While a form of multitasking can be simulated, the CPU provides no hardware support to protect processes from each other, making the system unstable.
Advantages of 32-bit Protected Mode:

Increased Memory: It allows the CPU to access up to 4 gigabytes (GB) of RAM.
Memory Protection: This is the key feature that gives the mode its name. The OS can use the CPU’s hardware to set up rules that “protect” memory. It can make the kernel’s memory inaccessible to user applications. If a program tries to access memory it doesn’t own, the CPU triggers a hardware exception, allowing the OS to terminate the faulty program without the entire system crashing.
Virtual Memory and Paging: Protected mode enables a powerful feature called paging. This allows the OS to give every application its own isolated, private 4GB address space. This means one application cannot see or interfere with another application’s memory. It also allows the OS to use the hard disk as an extension of RAM (swap space).
Privilege Levels: The CPU enforces different privilege levels.
Can it just stay at 16-bit?
Yes, an operating system can run entirely in 16-bit real mode. The most famous example of this is MS-DOS.

3. kernel.c - The Heart of the OS
kmain is the main entry point for our C code. It orchestrates everything.

kernel.c is responsible for several core subsystems:

VGA Driver: Simple functions (putc, puts) that write characters directly to the VGA text mode buffer at memory address 0xB8000. This is how text appears on the screen.
Keyboard Driver: A get_ch function that uses polling. It checks the keyboard controller's I/O port (0x64) for data, reads the scancode from port 0x60, and translates it into an ASCII character.
Task Management: jordyOS uses cooperative multitasking. I have a static array of task_t structs. The task_create function sets up the initial stack for a new task. It cleverly places the task's starting function address on the stack where the return address would normally go, so when we later ret from ctx_switch, we jump right into the new task.
Scheduler (yield): This is a dead-simple, round-robin scheduler. When a task calls sys_yield(), the kernel finds the next available task in the tasks array and calls ctx_switch to jump to it. It's cooperative because tasks must voluntarily give up control.
In-Memory File System: I implemented a simple file system that lives entirely in RAM. An array fs_files holds file data. The sys_read_file, sys_write_file, and other functions provide the API for applications to interact with it. Of course, all files are lost on reboot!
The Shell: The shell function itself is just another task. It sits in an infinite loop, prints the sh> prompt, reads user input, and launches other applications (calc, edit) by creating new tasks for them.
4. ctx_switch.asm - The Multitasking Engine
This tiny but vital piece of assembly is the engine of our multitasking. The ctx_switch function takes two arguments: a pointer to save the old task's stack pointer and the value of the new task's stack pointer.

Become a member
Its job is to:

Save the registers of the current task by pushing them onto its stack.
Save the current stack pointer (esp) to the task_t struct of the outgoing task.
Load the stack pointer of the incoming task into esp.
Pop the saved registers of the new task from its stack.
ret: This is the clever part. The ret instruction pops the return address off the new task's stack and jumps to it. When a task is first created, this "return address" is actually the task's entry function. For a task that's being switched back to, it's the instruction right after its last call to ctx_switch.
Imagine the computer’s CPU is a single game controller, and all the running programs (we’ll call them tasks) are kids who want to play.
The Rule of the Game (The Scheduler)
Our rule is: “You can play as long as you want, but when you get to a good stopping point, you have to offer the controller to the next person in line.”

“You can play as long as you wan”: This is Cooperative Multitasking. No one grabs the controller from you. You decide when you’re ready to pause. In the code, this is when a task calls sys_yield().
“offer the controller to the next person in line”: This is Round-Robin. If kid #1 finishes, they offer it to kid #2. If kid #2 doesn’t want to play right now, they offer it to kid #3, and so on, going around in a circle. In the code, this is the scheduler searching the tasks array for the next non-empty spot.
Passing the Controller (The Context Switch)
This is the action of actually handing the controller from one kid to another. To make sure nobody loses their progress, they have to do it very carefully.

Save Your Game: Before kid #1 hands over the controller, they hit “PAUSE” and the game automatically saves everything: what level they were on, their score, their items. This is the ctx_switch function saving all the registers and the stack pointer.
Hand Over the Controller: Kid #1 gives the physical controller to kid #2. This is the moment the esp register is changed to point to the new task's stack.
Load the Other Game: Kid #2 takes the controller and loads their saved game. They are instantly back to the exact level, with the exact score and items they had when they last played. This is the ctx_switch function restoring the registers from the new task's stack.
The ret instruction at the end is like hitting "RESUME." The game continues for kid #2 exactly where they left off.
5. app_calc.c & app_edit.c - The Applications
To prove the multitasking and system call interface works, I wrote two applications.

app_calc.c: A simple command-line calculator. It shows how an application can read user input (read_line) and print output (sys_write) in its own self-contained loop. It exits by calling sys_exit_task().
app_edit.c: A more complex text editor. This is the primary user of the in-memory file system. It has commands like new, open, save, list, and delete, all of which use the sys_ file system calls I defined in the kernel. This demonstrates how applications can be completely decoupled from the kernel's internal workings, communicating only through the system call API.
6. The Glue: util.h, linker.ld, and Makefile
A few other files are essential for tying everything together.

util.h: This header file is the bridge. It defines the function prototypes for all the system calls (sys_write, sys_yield, etc.) and some shared utility functions. Both the kernel and the applications include it.
linker.ld: This linker script tells the linker ld exactly how to structure our final kernel binary. Most importantly, it sets the base address of our code to 0x0800, matching where the bootloader loaded it.
Makefile: The conductor of the orchestra. The Makefile automates the entire build process:
It compiles all the C files (.c -> .o) and assembles the assembly files (.asm -> .o).
It links all the object files together using linker.ld to create kernel.elf.
It converts the ELF file into a flat binary, kernel.bin.
Crucially, it calculates how many 512-byte sectors kernel.bin occupies and passes this number (KSECT) to the bootloader during assembly. This ensures the bootloader loads the entire kernel, no matter its size.
Finally, it creates a blank floppy disk image (os.img) and uses dd to copy the bootloader to the first sector and the kernel binary to the sectors immediately following it.
A Deeper Dive: Key Assembly Instructions Explained
Here’s a breakdown of the most critical instructions from jordyOS and why they work the way they do.

Inside bootloader.asm (The First Steps)
This code runs in 16-bit real mode and its only job is to load the kernel.

ORG 0x7C00
What it means: This isn’t a CPU instruction, but a directive to the assembler (nasm). It says that the code should be generated as if it will be loaded into memory at address 0x7C00.
Why it’s important: The PC BIOS standard dictates that it will load the first sector of a bootable disk to this exact memory address before executing it.
int 0x13
What it means: This instruction triggers a software interrupt. int 0x13 is a specific interrupt that calls a disk I/O service routine built into the computer's BIOS.
Why it’s important: This is how the bootloader reads the kernel from the disk into memory without needing to know the complex details of the disk hardware. Before calling this, other registers are set up to tell the BIOS what to read (sector number) and where to put it in memory.
jmp 0000h:0800h
What it means: This is a far jump. Unlike a regular jump, it loads two registers at once: the Code Segment (CS) register with 0000h and the Instruction Pointer (IP) register with 0800h.
Why it’s important: This transfers control to the kernel code that was just loaded into memory at address 0x0800. Using a far jump ensures the CS register is set correctly for the new code segment.
dw 0xAA55
What it means: This places a specific 2-byte value, 0xAA55, at the very end of the 512-byte boot sector.
Why it’s important: This is the “boot signature” or “magic number.” The BIOS scans for this value to verify that the disk is actually bootable. If this signature is missing, the BIOS will assume there is no OS and move on to the next boot device.
Inside kernel_entry.asm (The Mode Switch)
This code handles the delicate transition from 16-bit real mode to 32-bit protected mode.

lgdt [gdt_descriptor]
What it means: “Load Global Descriptor Table Register”. This instruction tells the CPU where to find the GDT. The GDT is a data structure required for protected mode that defines memory segments (like code and data).
Why it’s important: You cannot enter protected mode without a valid GDT. This instruction points the CPU’s internal GDTR register to the table we've defined.
gdt_descriptor:
    dw gdt_end - gdt_start - 1   ; size-1
    dd gdt_start                 ; linear address
Why it’s calculated this way: The lgdt instruction expects a 6-byte descriptor.
The first two bytes are the size of the GDT. The calculation gdt_end - gdt_start gives the exact size of the table in bytes. The CPU hardware requires this value to be size - 1, which is why 1 is subtracted.
The next four bytes (dd gdt_start) are the 32-bit linear memory address where the GDT begins.
mov  eax, cr0
or   eax, 1
mov  cr0, eax
What it means: This sequence flips the first bit (bit 0) of the CR0 control register to 1.
Why it’s important: Bit 0 of CR0 is the "Protection Enable" (PE) bit. Setting this bit is the action that officially switches the CPU from real mode into protected mode.
jmp 08h:protected_start
What it means: This is another far jump, but its meaning is different in protected mode. 08h is not a memory address; it is a Segment Selector.
Why it’s important: 08h (binary 0000 1000) refers to the second entry (index 1) in our GDT, which we defined as our 32-bit code segment. This jump tells the CPU to begin executing in 32-bit mode using that code segment's rules. The jump is also crucial because it flushes the CPU's instruction pipeline, which was filled with old 16-bit instructions, ensuring that only 32-bit instructions are executed from this point forward.
Inside ctx_switch.asm (The Multitasking Engine)
This is the low-level routine that performs the context switch between tasks.

mov [esi], esp
What it means: The esi register holds the address of the sp field in the outgoing task's task_t structure. This instruction takes the current value of the stack pointer (esp) and stores it at that memory location.
Why it’s important: This is the precise moment the outgoing task’s state is saved. It bookmarks the exact top of the stack so it can be resumed perfectly later.
mov esp, [esp+24]
What it means: This instruction retrieves the new_sp argument that was passed to the function on the stack and loads it directly into the esp register.
Why it’s important: This is the moment the CPU’s context officially switches to the new task. The CPU is now using the stack of the incoming task.
ret
What it means: “Return from procedure”. Normally, this instruction pops a return address from the stack and jumps to it.
Why it’s important: Here, it’s used to resume the incoming task. When a task is switched away from, the instruction address after the ctx_switch call is saved on its stack. When the task is switched back to, this ret instruction pops that saved address and jumps there, causing the task to resume exactly where it left off. For a brand new task, the task_create function places the task's starting function address here, so ret effectively starts the new task for the first time.
What is long jump?
Imagine the computer’s memory is a very large city, and the CPU is a person who follows a list of instructions.

The Code Segment (CS) is the Street Name you are currently on.
The Instruction Pointer (IP) is the House Number you are currently at.
Your location is always a combination of the Street Name and the House Number (CS:IP).

Near Jump (Visiting a Neighbor)

A near jump is like visiting a different house on the same street. You don’t need to change your street name (CS), only the house number (IP).

The instruction jmp .hang in kernel_entry.asm is a near jump. It tells the CPU: "Stay on the street you're on, but go to the house number labeled .hang."

Changes: Only the House Number (IP).
Purpose: Looping and branching within the same block of code.
Far Jump (Traveling to a Different Street)

A far jump is like traveling to a completely different street in the city.

You need to change both the street name (CS) and the house number (IP). This is why the instruction has two parts, separated by a colon (:).

jmp 0000h:0800h: This instruction from your bootloader tells the CPU: "Stop what you're doing. Go to Street 0000h and find House Number 0800h. That's where the kernel starts."

jmp 08h:protected_start: This instruction from your kernel entry code tells the CPU: "We are now entering a new part of the city (protected mode). Go to Street 08h and find House Number protected_start." This new "street" has different rules (32-bit instead of 16-bit).

Changes: Both the Street Name (CS) and the House Number (IP).
Purpose: To switch between major components of the OS (like from the bootloader to the kernel) or to change fundamental CPU modes.
This is what I learned; I hope you learn something from this as well!
