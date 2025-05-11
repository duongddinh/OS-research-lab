# jordyOS

`jordyOS` is a minimal, bootable operating system that launches directly into a keyboard-driven calculator shell.

It’s written in **x86 Assembly** and **C**, and is built with a cross-compiler toolchain on **macOS**.  
The OS runs in **16-bit real mode** initially (via the bootloader), then jumps to 32-bit protected mode where it executes a tiny kernel.

---

## Features

- A bootloader (`bootloader.asm`) to set up the system and load the kernel
- A basic kernel that:
  - Outputs to VGA text mode
  - Reads keyboard input via I/O ports
  - Implements a shell-like interface
  - Parses commands like `add 4 5`, `mul 3 10`, `div 9 3`, etc.
  - Displays the result interactively

---

## Example Usage

```txt
Welcome to jordyOS
jordyOS ready.  Type e.g.  add 3 5 (operation: add|sub|mul|div)
> add 3 5
= 8
> div 10 2
= 5
> sub 20 7
= 13
```

### Install Toolchain (if not already)

```
$ brew install cdrkit
$ brew install cdrtools
$ brew install i686-elf-binutils i686-elf-gcc
```

### Build and Run the OS

```
$ make
$ make run-floppy
```
